# ViewAlyzer UDP — C Library

Standalone C library for sending ViewAlyzer trace data over UDP with COBS framing. No RTOS dependency — suitable for bare-metal firmware, desktop simulations, or any C/C++ program.

The library is split into two layers:

| Layer | Files | Description |
|-------|-------|-------------|
| **Core** | `viewalyzer_udp.h/c` | Generic tracing: int/float values, strings, toggles, function spans |
| **RTOS** | `viewalyzer_udp_rtos.h/c` | Adds tasks, ISRs, semaphores, mutexes, queues, stack usage, contention |

Both layers use `viewalyzer_cobs.h/c` for framing.

> For STLink ITM or J-Link RTT transport, use the main `ViewAlyzer.h` / `ViewAlyzer.c` recorder firmware in the parent directory instead.

## Quick Start (Core Only)

```c
#include "viewalyzer_udp.h"

int main(void)
{
    va_udp_ctx_t *va = va_udp_init("127.0.0.1", 17200, 170000000);
    va_udp_send_sync_and_clock(va);

    va_udp_send_trace_setup(va, 0, VA_UDP_TRACE_GRAPH,   "Temperature");
    va_udp_send_trace_setup(va, 1, VA_UDP_TRACE_COUNTER, "SampleCount");
    va_udp_send_function_map(va, 0, "processData");

    uint64_t ts = 0;
    while (1)
    {
        va_udp_send_trace_float(va, 0, ts, 23.5f);
        va_udp_send_trace_int(va, 1, ts, sample_count++);
        va_udp_send_function(va, 0, true, ts);
        ts += 170 * 500;
        va_udp_send_function(va, 0, false, ts);
        ts += 170 * 500;
    }

    va_udp_close(va);
}
```

## Adding RTOS Support

To add task/ISR/sync-object tracking, also include `viewalyzer_udp_rtos.h` and compile `viewalyzer_udp_rtos.c`:

```c
#include "viewalyzer_udp.h"
#include "viewalyzer_udp_rtos.h"

// Now you can also call:
va_udp_send_task_map(va, 0, "MainTask");
va_udp_send_task_switch(va, 0, true, ts);
va_udp_send_isr(va, 1, true, ts);
va_udp_send_semaphore(va, 0, true, ts);
// ... all RTOS events
```

## Building with CMake (recommended)

Works on Windows (MSVC or MinGW) and Linux/macOS out of the box:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This produces:
- `viewalyzer_core` — static library (core tracing)
- `viewalyzer_rtos` — static library (core + RTOS extension)
- `desktop_example` — ready-to-run x86 example

Run the example:
```bash
./build/desktop_example          # Linux/macOS
.\build\Release\desktop_example   # Windows (MSVC)
```

### Using in your own CMake project

```cmake
add_subdirectory(path/to/ViewAlyzerRecorder/c)

# Core only:
target_link_libraries(my_app PRIVATE viewalyzer_core)

# Core + RTOS:
target_link_libraries(my_app PRIVATE viewalyzer_rtos)
```

## Building without CMake

Just add the source files to your project. No external dependencies.

**GCC (Linux / macOS) — core only:**
```bash
gcc -o my_sender main.c viewalyzer_udp.c viewalyzer_cobs.c -lm
```

**GCC — core + RTOS:**
```bash
gcc -o my_sender main.c viewalyzer_udp.c viewalyzer_udp_rtos.c viewalyzer_cobs.c -lm
```

**MSVC (Windows):**
```
cl main.c viewalyzer_udp.c viewalyzer_cobs.c ws2_32.lib
```

**MinGW (Windows):**
```bash
gcc -o my_sender.exe main.c viewalyzer_udp.c viewalyzer_cobs.c -lws2_32 -lm
```

## File Reference

| File | Description |
|------|-------------|
| `viewalyzer_udp.h` | Core API — init, traces, strings, toggles, functions |
| `viewalyzer_udp.c` | Core implementation |
| `viewalyzer_udp_rtos.h` | RTOS extension API — tasks, ISRs, sync objects |
| `viewalyzer_udp_rtos.c` | RTOS extension implementation |
| `viewalyzer_cobs.h` | COBS encoder header |
| `viewalyzer_cobs.c` | COBS encoder implementation |
| `CMakeLists.txt` | CMake build (Windows + Linux) |
| `examples/desktop_example.c` | x86 desktop example (core only) |

## Protocol Reference

See the full [ViewAlyzer Protocol Specification](https://viewalyzer.net/docs.html) for packet formats.

## License

Copyright (c) 2025 Free Radical Labs. See [LICENSE](../../LICENSE) for details.
