"""
viewalyzer.protocol — Core VA protocol constants and packet builders.

This module contains everything needed for **generic schema-powered tracing**:
trace values (int32 / float), string messages, toggles, and function spans.
No RTOS dependency.

For RTOS-specific events (TaskSwitch, ISR, Semaphore, Mutex, Queue, etc.),
see ``viewalyzer.protocol_rtos``.

All packet builders return raw bytes (not COBS-encoded).  The sender
class or your own code should COBS-encode each packet before sending.
"""

import struct
from enum import IntEnum

# ── Sync marker ──────────────────────────────────────────────────────────────

SYNC_MARKER = bytes([0x56, 0x41, 0x5A, 0x01,
                     0x53, 0x59, 0x4E, 0x43,
                     0x30, 0x31, 0xAA, 0x55])

# ── Core event type codes (lower 7 bits of type byte) ───────────────────────

EVT_USER_TRACE       = 0x04   # int32 value → widget
EVT_USER_TOGGLE      = 0x0A   # boolean state change
EVT_USER_FUNCTION    = 0x0B   # function/span entry/exit → timeline
EVT_STRING_EVENT     = 0x0D   # variable-length string → log
EVT_FLOAT_TRACE      = 0x0E   # IEEE 754 float value → widget

FLAG_START = 0x80              # MSB: start / enter

# ── Core setup packet codes ──────────────────────────────────────────────────

SETUP_USER_TRACE        = 0x72
SETUP_USER_FUNCTION_MAP = 0x76
SETUP_CONFIG_FLAGS      = 0x77
SETUP_INFO              = 0x7F

# ── Trace visualisation type hints ───────────────────────────────────────────

class TraceType(IntEnum):
    """Display type for a user trace channel (byte 2 of UserTrace setup)."""
    GRAPH     = 0
    BAR       = 1
    GAUGE     = 2
    COUNTER   = 3
    TABLE     = 4
    HISTOGRAM = 5
    REGISTER  = 9


# ── Helpers ──────────────────────────────────────────────────────────────────

def _ts_mask(ts: int) -> int:
    return ts & 0xFFFFFFFFFFFFFFFF


# ── Setup packet builders ───────────────────────────────────────────────────

def build_sync() -> bytes:
    """12-byte sync marker — send once at the start of a session."""
    return SYNC_MARKER


def build_info_clk(cpu_freq_hz: int) -> bytes:
    """Info packet declaring the CPU clock frequency."""
    payload = f"CLK:{cpu_freq_hz}".encode("ascii")
    return struct.pack("BBB", SETUP_INFO, 0x00, len(payload)) + payload


def build_user_trace_setup(trace_id: int, name: str,
                           trace_type: int = TraceType.GRAPH) -> bytes:
    """Setup::UserTrace — declare a trace channel (id, type, name)."""
    nb = name.encode("ascii")
    return struct.pack("BBBB", SETUP_USER_TRACE, trace_id, int(trace_type), len(nb)) + nb


def build_user_function_map(func_id: int, name: str) -> bytes:
    """Setup::UserFunctionMap — map a function ID to a name."""
    nb = name.encode("ascii")
    return struct.pack("BBB", SETUP_USER_FUNCTION_MAP, func_id, len(nb)) + nb


def build_config_flag(flag_name: str) -> bytes:
    """Setup::ConfigFlags — emit a named configuration flag (e.g. HOST_TS)."""
    nb = flag_name.encode("ascii")
    return struct.pack("BBB", SETUP_CONFIG_FLAGS, 0x00, len(nb)) + nb


# ── Core event packet builders ──────────────────────────────────────────────

def build_user_trace_int(trace_id: int, timestamp: int, value: int) -> bytes:
    """UserTrace (0x04) — 14 bytes, signed int32 value."""
    tb = EVT_USER_TRACE | FLAG_START
    return struct.pack("<BBQi", tb, trace_id, _ts_mask(timestamp), value)


def build_float_trace(trace_id: int, timestamp: int, value: float) -> bytes:
    """FloatTrace (0x0E) — 14 bytes, IEEE 754 float value."""
    tb = EVT_FLOAT_TRACE | FLAG_START
    return struct.pack("<BBQf", tb, trace_id, _ts_mask(timestamp), value)


def build_user_toggle(toggle_id: int, timestamp: int, state: bool) -> bytes:
    """UserToggle (0x0A) — 11 bytes."""
    return struct.pack("<BBQB", EVT_USER_TOGGLE, toggle_id,
                       _ts_mask(timestamp), 1 if state else 0)


def build_user_function(func_id: int, is_entry: bool, timestamp: int) -> bytes:
    """UserFunction (0x0B) — 10 bytes.  Entry/exit of a profiled function or span."""
    tb = EVT_USER_FUNCTION | (FLAG_START if is_entry else 0)
    return struct.pack("<BBQ", tb, func_id, _ts_mask(timestamp))


def build_string_event(msg_id: int, timestamp: int, message: str) -> bytes:
    """StringEvent (0x0D) — 12 + N bytes (max 200 chars)."""
    mb = message.encode("ascii")[:200]
    return struct.pack("<BBQ", EVT_STRING_EVENT, msg_id,
                       _ts_mask(timestamp)) + struct.pack("<H", len(mb)) + mb
