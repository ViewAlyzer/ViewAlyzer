"""
ViewAlyzer Python SDK — send VA protocol data over UDP with COBS framing.

**Core** (default import — no RTOS required)::

    from viewalyzer import ViewAlyzerSender, TraceType

    va = ViewAlyzerSender("127.0.0.1", 17200, cpu_freq=170_000_000)
    va.send_trace_setup(0, "Temperature", TraceType.GRAPH)
    va.send_trace_float(0, ts, 23.5)
    va.send_string(0, ts, "Hello ViewAlyzer")
    va.close()

**RTOS extensions** (opt-in)::

    from viewalyzer.sender_rtos import ViewAlyzerRtosSender

    va = ViewAlyzerRtosSender("127.0.0.1", 17200, cpu_freq=170_000_000)
    va.send_task_map(0, "MainTask")
    va.send_task_switch(0, enter=True, timestamp=ts)
    va.send_trace_float(0, ts, 23.5)      # core methods still work
    va.send_task_switch(0, enter=False, timestamp=ts)
    va.close()
"""

# ── Core imports (always available) ──────────────────────────────────────
from viewalyzer.protocol import (
    SYNC_MARKER, FLAG_START, TraceType,
    EVT_USER_TRACE, EVT_FLOAT_TRACE, EVT_STRING_EVENT,
    EVT_USER_TOGGLE, EVT_USER_FUNCTION,
    SETUP_USER_TRACE, SETUP_USER_FUNCTION_MAP, SETUP_INFO,
    build_sync, build_info_clk,
    build_user_trace_setup, build_user_function_map,
    build_user_trace_int, build_float_trace,
    build_user_toggle, build_user_function,
    build_string_event,
)
from viewalyzer.cobs import cobs_encode, cobs_decode
from viewalyzer.sender import ViewAlyzerSender

__version__ = "0.1.0"

__all__ = [
    # Main sender class (core)
    "ViewAlyzerSender",
    # COBS
    "cobs_encode", "cobs_decode",
    # Core constants
    "SYNC_MARKER", "FLAG_START", "TraceType",
    "EVT_USER_TRACE", "EVT_FLOAT_TRACE", "EVT_STRING_EVENT",
    "EVT_USER_TOGGLE", "EVT_USER_FUNCTION",
    "SETUP_USER_TRACE", "SETUP_USER_FUNCTION_MAP", "SETUP_INFO",
    # Core packet builders
    "build_sync", "build_info_clk",
    "build_user_trace_setup", "build_user_function_map",
    "build_user_trace_int", "build_float_trace",
    "build_user_toggle", "build_user_function",
    "build_string_event",
]
