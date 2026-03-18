"""
viewalyzer.sender — Core ViewAlyzer UDP sender with COBS framing.

Wraps a UDP socket and provides typed methods for **core** VA protocol
packets: traces (int / float), string events, toggles, and function spans.
No RTOS dependency.

For RTOS extensions (tasks, ISRs, sync objects), see
``viewalyzer.sender_rtos.ViewAlyzerRtosSender``.

Usage::

    from viewalyzer import ViewAlyzerSender, TraceType

    va = ViewAlyzerSender("127.0.0.1", 17200, cpu_freq=170_000_000)
    va.send_trace_setup(0, "Temperature", TraceType.GRAPH)
    va.send_trace_float(0, ts, 23.5)
    va.close()
"""

import socket
import time
from viewalyzer.cobs import cobs_encode
from viewalyzer.protocol import (
    TraceType,
    build_sync, build_info_clk, build_config_flag,
    build_user_trace_setup, build_user_function_map,
    build_user_trace_int, build_float_trace,
    build_user_toggle, build_user_function,
    build_string_event,
)


class ViewAlyzerSender:
    """COBS-framed UDP sender for **core** ViewAlyzer protocol data.

    Covers generic tracing: int / float values, strings, toggles, and
    function spans.  Subclassed by ``ViewAlyzerRtosSender`` for RTOS
    events (tasks, ISRs, semaphores, mutexes, queues).

    Parameters
    ----------
    host : str
        Destination IP address (default ``"127.0.0.1"``).
    port : int
        Destination UDP port (default ``17200``).
    cpu_freq : int
        CPU clock frequency in Hz.  Sent in the CLK setup packet so
        ViewAlyzer can convert raw cycle-count timestamps to seconds.
        Ignored when *host_timestamps* is True (forced to 1 GHz).
    auto_setup : bool
        If True (default), automatically sends the sync marker and CLK
        info packet on construction.
    host_timestamps : bool
        If True, the sender generates nanosecond timestamps on the host
        using ``time.monotonic_ns()``.  A HOST_TS config flag is sent
        and *cpu_freq* is overridden to 1 GHz.  All ``send_*`` event
        methods accept ``timestamp=0`` (or any value) — the host
        timestamp is substituted automatically.
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 17200, *,
                 cpu_freq: int = 170_000_000, auto_setup: bool = True,
                 host_timestamps: bool = False):
        self._host = host
        self._port = port
        self._host_timestamps = host_timestamps

        if host_timestamps:
            self._cpu_freq = 1_000_000_000
            self._ts_epoch = time.monotonic_ns()
        else:
            self._cpu_freq = cpu_freq
            self._ts_epoch = 0

        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._dest = (host, port)

        if auto_setup:
            self.send_sync_and_clock()

    # ── Core send ────────────────────────────────────────────────────────

    def send_framed(self, raw_packet: bytes) -> None:
        """COBS-encode a raw VA packet and send it via UDP."""
        self._sock.sendto(cobs_encode(raw_packet), self._dest)

    def send_batch(self, packets: list[bytes], mtu: int = 1400) -> None:
        """COBS-encode multiple packets and send in MTU-sized UDP datagrams."""
        buf = bytearray()
        for pkt in packets:
            buf += cobs_encode(pkt)
        offset = 0
        while offset < len(buf):
            end = min(offset + mtu, len(buf))
            self._sock.sendto(bytes(buf[offset:end]), self._dest)
            offset = end

    # ── Setup packets ────────────────────────────────────────────────────

    def send_sync_and_clock(self) -> None:
        """Send the sync marker, CLK info packet, and (if enabled) HOST_TS flag."""
        self.send_framed(build_sync())
        self.send_framed(build_info_clk(self._cpu_freq))
        if self._host_timestamps:
            self.send_framed(build_config_flag("HOST_TS"))

    def send_trace_setup(self, trace_id: int, name: str,
                         trace_type: int = TraceType.GRAPH) -> None:
        """Declare a user-trace channel and its display type."""
        self.send_framed(build_user_trace_setup(trace_id, name, trace_type))

    def send_function_map(self, func_id: int, name: str) -> None:
        """Declare a user-function and its name."""
        self.send_framed(build_user_function_map(func_id, name))

    # ── Core event packets ───────────────────────────────────────────────

    def _ts(self, timestamp: int) -> int:
        """Return a host-generated timestamp when enabled, otherwise pass through."""
        if self._host_timestamps:
            return time.monotonic_ns() - self._ts_epoch
        return timestamp

    def send_trace_int(self, trace_id: int, timestamp: int, value: int) -> None:
        """Send a signed int32 trace value."""
        self.send_framed(build_user_trace_int(trace_id, self._ts(timestamp), value))

    def send_trace_float(self, trace_id: int, timestamp: int, value: float) -> None:
        """Send an IEEE 754 float trace value."""
        self.send_framed(build_float_trace(trace_id, self._ts(timestamp), value))

    def send_toggle(self, toggle_id: int, timestamp: int, state: bool) -> None:
        """Send a boolean toggle change."""
        self.send_framed(build_user_toggle(toggle_id, self._ts(timestamp), state))

    def send_function(self, func_id: int, is_entry: bool, timestamp: int) -> None:
        """Send a function entry/exit event."""
        self.send_framed(build_user_function(func_id, is_entry, self._ts(timestamp)))

    def send_string(self, msg_id: int, timestamp: int, message: str) -> None:
        """Send a free-text log message."""
        self.send_framed(build_string_event(msg_id, self._ts(timestamp), message))

    # ── Lifecycle ────────────────────────────────────────────────────────

    def close(self) -> None:
        """Close the UDP socket."""
        self._sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    @property
    def cpu_freq(self) -> int:
        return self._cpu_freq

    @property
    def dest(self) -> tuple:
        return self._dest
