# ViewAlyzer UDP — Python Package

Python library for sending ViewAlyzer trace data over UDP with COBS framing. Zero dependencies — stdlib only (Python 3.8+).

The package is split into two layers:

| Layer | Modules | Description |
|-------|---------|-------------|
| **Core** | `protocol`, `sender`, `cobs` | Generic tracing: int/float values, strings, toggles, function spans |
| **RTOS** | `protocol_rtos`, `sender_rtos` | Adds tasks, ISRs, semaphores, mutexes, queues, stack usage, contention |

> For STLink ITM or J-Link RTT transport from embedded firmware, use the C recorder in the parent directory instead.

## Installation

```bash
pip install .          # from this directory
pip install -e .       # editable / development mode
```

No external dependencies.

## Quick Start (Core Only)

```python
from viewalyzer import ViewAlyzerSender, TraceType

va = ViewAlyzerSender("127.0.0.1", 17200, cpu_freq=170_000_000)

# Declare traces and functions
va.send_trace_setup(0, "Temperature", TraceType.GRAPH)
va.send_trace_setup(1, "Counter",     TraceType.COUNTER)
va.send_function_map(0, "processData")

# Stream events
ts = 0
va.send_trace_float(0, ts, 23.5)
va.send_trace_int(1, ts, 42)
va.send_function(0, is_entry=True, timestamp=ts)
ts += 85000
va.send_function(0, is_entry=False, timestamp=ts)
va.send_string(0, ts, "Hello ViewAlyzer")

va.close()
```

Works as a context manager too:

```python
with ViewAlyzerSender("127.0.0.1", 17200, cpu_freq=170_000_000) as va:
    va.send_trace_float(0, ts, 23.5)
```

## Adding RTOS Support

Import `ViewAlyzerRtosSender` — it inherits all core methods and adds RTOS events:

```python
from viewalyzer.sender_rtos import ViewAlyzerRtosSender

va = ViewAlyzerRtosSender("127.0.0.1", 17200, cpu_freq=170_000_000)

va.send_task_map(0, "MainTask")
va.send_task_switch(0, enter=True, timestamp=ts)
va.send_trace_float(0, ts, 23.5)        # core methods still work
va.send_task_switch(0, enter=False, timestamp=ts)

va.close()
```

## Package Structure

```
viewalyzer/
├── __init__.py        # Core public API (import from here)
├── protocol.py        # Core constants + packet builders
├── protocol_rtos.py   # RTOS event constants + packet builders
├── cobs.py            # COBS encode/decode
├── sender.py          # ViewAlyzerSender (core)
└── sender_rtos.py     # ViewAlyzerRtosSender (core + RTOS)
```

## Examples

| Example | Description |
|---------|-------------|
| `examples/basic_example.py` | Core only — sine wave, counter, function spans, strings |
| `examples/comprehensive_example.py` | Full RTOS — all 14 event types, ISRs, sync objects |

```bash
python examples/basic_example.py
python examples/comprehensive_example.py --port 17202
```

## Core Event Methods (ViewAlyzerSender)

| Method | Code | Description |
|--------|------|-------------|
| `send_trace_int()` | 0x04 | Int32 trace value |
| `send_toggle()` | 0x0A | Boolean state change |
| `send_function()` | 0x0B | Function entry/exit span |
| `send_string()` | 0x0D | Variable-length string message |
| `send_trace_float()` | 0x0E | IEEE 754 float trace value |

## RTOS Event Methods (ViewAlyzerRtosSender)

All core methods plus:

| Method | Code | Description |
|--------|------|-------------|
| `send_task_switch()` | 0x01 | RTOS task enter/exit |
| `send_isr()` | 0x02 | ISR enter/exit |
| `send_task_create()` | 0x03 | Task creation with priority/stack |
| `send_task_notify()` | 0x05 | Task-to-task notification |
| `send_semaphore()` | 0x06 | Semaphore give/take |
| `send_mutex()` | 0x07 | Mutex acquire/release |
| `send_queue()` | 0x08 | Queue send/receive |
| `send_stack_usage()` | 0x09 | Task stack usage report |
| `send_mutex_contention()` | 0x0C | Mutex contention event |

## Protocol Reference

See the full [ViewAlyzer Protocol Specification](https://viewalyzer.net/docs.html) for wire-format details.

## License

Copyright (c) 2025 Free Radical Labs. See [LICENSE](../../LICENSE) for details.
