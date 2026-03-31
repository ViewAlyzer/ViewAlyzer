# ViewAlyzer Recorder — Architecture

## High-Level Overview

The ViewAlyzer Recorder is a lightweight firmware library that captures real-time trace events (task switches, ISR entry/exit, queue/mutex/semaphore operations, user data, etc.) from an embedded Cortex-M target and streams them to the ViewAlyzer desktop application for timeline visualization and analysis.

```
┌─────────────────────────────────────────────────────────────┐
│                    Your Application                         │
│  main.c, tasks, ISRs                                        │
│                                                             │
│   VA_Init()  VA_LogTrace()  VA_LogISRStart()  ...           │
└────────────────────────┬────────────────────────────────────┘
                         │  Public API (ViewAlyzer.h)
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                   Core Engine (ViewAlyzer.c)                 │
│                                                             │
│  ┌──────────┐  ┌──────────────┐  ┌───────────────────────┐  │
│  │ Timestamp │  │  Packet      │  │  Task / Object Maps   │  │
│  │ (DWT 64b) │  │  Builder     │  │  (ID ↔ handle)        │  │
│  └──────────┘  └──────┬───────┘  └───────────────────────┘  │
│                       │                                     │
│              ┌────────┴────────┐                            │
│              │  Transport      │                            │
│              │  Abstraction    │                            │
│              └──┬────┬─────┬──┘                            │
│                 │    │     │                                │
└─────────────────┼────┼─────┼────────────────────────────────┘
                  │    │     │
           ITM/SWO  J-Link  Custom
           (STM32)   RTT    (COBS → UART/USB/etc.)
                  │    │     │
                  ▼    ▼     ▼
         ┌────────────────────────┐
         │  ViewAlyzer Desktop    │
         │  (Timeline Viewer)     │
         └────────────────────────┘
```

### RTOS Adapter Layer

When an RTOS is active, a thin adapter sits between the RTOS kernel and the core engine:

```
┌──────────────────────────┐     ┌──────────────────────────┐
│  FreeRTOS Kernel         │     │  Zephyr Kernel           │
│  trace macros in         │     │  sys_trace_*_user()      │
│  FreeRTOSConfig.h        │     │  weak-function overrides │
└────────────┬─────────────┘     └────────────┬─────────────┘
             │                                │
             ▼                                ▼
┌──────────────────────┐        ┌──────────────────────┐
│ VA_Adapter_FreeRTOS  │        │  VA_Adapter_Zephyr   │
│  • Queue type mirror │        │  • k_thread stack    │
│  • Stack watermark   │        │  • Thread registry   │
│  • Mutex holder      │        │  • ISR via IPSR      │
└────────────┬─────────┘        └────────────┬─────────┘
             │                                │
             └─────────┬──────────────────────┘
                       │  va_adapter_*() interface
                       ▼
          ┌────────────────────────┐
          │  Core Engine           │
          │  va_taskswitchedin()   │
          │  va_taskcreated()      │
          │  va_logQueueObject*()  │
          └────────────────────────┘
```

---

## Source Tree

```
ViewAlyzerRecorder/
├── core/                          # RTOS-agnostic core (always compiled)
│   ├── ViewAlyzer.h               #   Public API + user configuration
│   ├── ViewAlyzer.c               #   Core engine implementation
│   ├── VA_Internal.h              #   Internal API shared with adapters
│   ├── viewalyzer_cobs.h          #   COBS framing (custom transport)
│   └── viewalyzer_cobs.c
│
├── freertos/                      # FreeRTOS adapter (compile when VA_RTOS_SELECT == 1)
│   ├── VA_Adapter_FreeRTOS.c      #   Queue mirror, stack watermark, mutex holder
│   ├── VA_Adapter_FreeRTOS.h
│   ├── ViewAlyzerFreeRTOSHook.h          # Trace macros for FreeRTOS < 10.4
│   └── ViewAlyzerFreeRTOSHook_V10_4_Plus.h  # Trace macros for FreeRTOS ≥ 10.4
│
├── zephyr/                        # Zephyr adapter (compile when VA_RTOS_SELECT == 2)
│   ├── VA_Adapter_Zephyr.c        #   Thread registration, stack, sys_trace overrides
│   └── VA_Adapter_Zephyr.h
│
├── c/                             # Desktop/host-side helpers
│   ├── viewalyzer_cobs.c/h        #   COBS decoder
│   ├── viewalyzer_udp.c/h         #   UDP transport receiver
│   └── viewalyzer_udp_rtos.c/h    #   UDP + RTOS variant
│
└── python/                        # Python library for .va file decode
```

---

## Core Engine (`core/`)

The core engine is completely RTOS-agnostic. It handles:

### Timestamp (`_va_get_timestamp`)

Uses the **DWT Cycle Counter** (CYCCNT) available on Cortex-M3/M4/M7/M33 to produce a 64-bit timestamp. The lower 32 bits come from `DWT->CYCCNT`, which counts CPU clock cycles. Overflow is tracked by incrementing a 32-bit overflow counter, giving effective 64-bit resolution at the CPU clock frequency.

```
 63                  32 31                   0
│   overflow_count      │   DWT->CYCCNT       │
```

`VA_TickOverflowCheck()` should be called periodically (every 1–10 seconds) to ensure no overflows are missed when the recorder is idle.

### Transport Layer

Three backends, selected at compile time by `VA_TRANSPORT`:

| Backend | Macro | How it works |
|---------|-------|-------------|
| ST-Link ITM/SWO | `ST_LINK_ITM` | Writes to `ITM->PORT[n]` using 32-bit or 8-bit stimulus writes |
| J-Link RTT | `JLINK_RTT` | Writes to SEGGER RTT channel via `SEGGER_RTT_Write()` |
| Custom | `CUSTOM_TRANSPORT` | User provides a send callback; data is COBS-framed before sending |

The custom transport wraps every packet with COBS encoding (Consistent Overhead Byte Stuffing) so the desktop side can reliably frame packets out of a raw byte stream (e.g. UART).

### Packet Format

All packets are emitted through `_va_emit_packet()`. There are two categories:

**Event packets** — generated at runtime with a timestamp:
```
┌──────────┬──────┬────────────────────────────────────────┬───────────┐
│ Type (1B)│ ID   │  Timestamp (8 bytes, little-endian)    │  Payload  │
│          │ (1B) │                                        │  (varies) │
└──────────┴──────┴────────────────────────────────────────┴───────────┘
```

| Type Code | Event | Payload |
|-----------|-------|---------|
| `0x01` | Task Switch | — |
| `0x02` | ISR enter/exit | — |
| `0x03` | Task Create | priority (4B) + basePriority (4B) + stackSize (4B) |
| `0x04` | User Trace (int32) | value (4B) |
| `0x05` | Task Notify | otherTaskID (1B) + value (4B) |
| `0x06` | Semaphore give/take | — |
| `0x07` | Mutex give/take | — |
| `0x08` | Queue send/receive | — |
| `0x09` | Stack Usage | stackUsed (4B) + stackTotal (4B) |
| `0x0A` | User Toggle | state (1B) |
| `0x0B` | User Event | — |
| `0x0C` | Mutex Contention | waitingTaskID (1B) + holderTaskID (1B) |
| `0x0D` | String Event | length (1B) + string (up to 200B) |
| `0x0E` | Float Trace | IEEE 754 float (4B) |

The high bit (`0x80`) of the type byte is the **START/END flag**:
- `type | 0x80` = start/enter/give (e.g. task switched IN, ISR entered, mutex given)
- `type` alone = end/exit/take (e.g. task switched OUT, ISR exited, mutex taken)

**Setup packets** — emitted once at init or on object creation (no timestamp):
```
┌──────────────┬──────┬──────────┬──────────────────────────┐
│ SetupCode(1B)│ ID   │ NameLen  │  Name string (N bytes)   │
│              │ (1B) │  (1B)    │                          │
└──────────────┴──────┴──────────┴──────────────────────────┘
```

| Setup Code | Meaning |
|------------|---------|
| `0x70` | Task Map (ID → name) |
| `0x71` | ISR Map (ID → name) |
| `0x72` | User Trace Map (ID → name + type) |
| `0x73` | Semaphore Map |
| `0x74` | Mutex Map |
| `0x75` | Queue Map |
| `0x76` | User Event Map |
| `0x77` | Config Flags |
| `0x7F` | Info (e.g. `CLK:170000000`) |

### ID Mapping

The core maintains lookup tables that map opaque RTOS handles (`void*`) to compact 8-bit IDs:

- **taskMap[VA_MAX_TASKS]** — maps task/thread handles to IDs (default 16 slots)
- **queueObjectMap[VA_MAX_SYNC_OBJECTS]** — maps queue/mutex/semaphore handles to IDs (default 32 slots)
- **userEventMap[VA_MAX_USER_EVENTS]** — maps user-event IDs to names (default 16 slots)

Each pool is independently sized so you can tune RAM usage per your application:

```c
// In ViewAlyzer.h or your build system
#define VA_MAX_TASKS          16  // RTOS task/thread slots (~40 bytes each)
#define VA_MAX_SYNC_OBJECTS   32  // Mutexes, semaphores, queues
#define VA_MAX_USER_EVENTS   16  // User-profiled spans or events
```

### Critical Sections

All packet-emitting functions use `VA_CS_ENTER()` / `VA_CS_EXIT()` which save and restore the ARM `PRIMASK` register. This prevents preemption from corrupting multi-byte packet writes. Controlled by `VA_ALLOWED_TO_DISABLE_INTERRUPTS`.

---

## RTOS Selection

RTOS support is configured via a single define:

```c
// In your build system (CMakeLists.txt, Makefile, etc.)
-DVA_RTOS_SELECT=1   // FreeRTOS
-DVA_RTOS_SELECT=2   // Zephyr
-DVA_RTOS_SELECT=0   // Bare-metal (no RTOS tracing)
```

Named constants in `ViewAlyzer.h`:
```c
#define VA_RTOS_NONE     0
#define VA_RTOS_FREERTOS 1
#define VA_RTOS_ZEPHYR   2
#define VA_RTOS_THREADX  3   // Reserved
```

The old `VA_TRACE_FREERTOS=1` define is still supported for backward compatibility — it automatically sets `VA_RTOS_SELECT=1`.

The convenience macro `VA_HAS_RTOS` is true when any RTOS adapter is active. It gates all task/object tracing code in the core.

---

## Adapter Pattern

Each RTOS adapter must implement four functions declared in `ViewAlyzer.h`:

```c
VA_QueueObjectType_t va_adapter_get_queue_object_type(void *handle);
uint32_t             va_adapter_calculate_stack_usage(void *taskHandle);
uint32_t             va_adapter_get_total_stack_size(void *taskHandle);
void                 va_adapter_check_mutex_contention(void *queueObject, uint8_t queue_va_id);
```

These are called by the core engine when it needs RTOS-specific information. The adapter also needs a **hooking mechanism** to intercept kernel events and forward them to the core's generic hooks:

| Core Hook | FreeRTOS Source | Zephyr Source |
|-----------|-----------------|---------------|
| `va_taskswitchedin(void*)` | `traceTASK_SWITCHED_IN()` macro | `sys_trace_thread_switched_in_user()` |
| `va_taskswitchedout(void*)` | `traceTASK_SWITCHED_OUT()` macro | `sys_trace_thread_switched_out_user()` |
| `va_taskcreated(void*, name)` | `traceTASK_CREATE(pxNewTCB)` macro | `sys_trace_thread_create_user()` |
| `va_logtasknotifygive()` | `traceTASK_NOTIFY()` macro | — |
| `va_logQueueObject*()` | `traceQUEUE_CREATE`, `traceQUEUE_SEND`, etc. | User calls in application |
| `VA_LogISRStart/End()` | User calls in ISR handlers | `sys_trace_isr_enter/exit_user()` |

### Internal API (`VA_Internal.h`)

Adapters that need to access core internals (e.g. to look up a task ID, find the stack depth stored during creation, or emit a custom packet) include `VA_Internal.h` rather than building their own data structures. This header exposes:

- `taskMap[]`, `queueObjectMap[]` — direct access to ID mapping tables
- `_va_find_task_id()`, `_va_find_task_index()`, `_va_assign_task_id()` — map helpers
- `_va_send_*_packet()` — all packet construction and emission functions
- `_va_get_timestamp()` — 64-bit DWT timestamp
- `VA_CS_ENTER()` / `VA_CS_EXIT()` — critical section macros
- Global creation state: `g_task_pxStack`, `g_task_uxPriority`, `g_task_ulStackDepth`, etc.

---

## FreeRTOS Adapter Details

### Hook Mechanism

FreeRTOS provides compile-time trace macros that are `#define`'d in user code. ViewAlyzer supplies two hook headers (one for FreeRTOS < 10.4, one for ≥ 10.4) that define these macros. The user includes the appropriate header in `FreeRTOSConfig.h`:

```c
// FreeRTOSConfig.h
#include "ViewAlyzerFreeRTOSHook_V10_4_Plus.h"
```

The macros have access to FreeRTOS kernel internals (`pxCurrentTCB`, `pxNewTCB`, `pxTCB`, `ulValue`, etc.) because they are expanded inside FreeRTOS source code at compile time.

### QueueDefinitionMirror Hack

FreeRTOS uses a single internal `Queue_t` struct for queues, mutexes, semaphores, and recursive mutexes. The adapter mirrors this struct layout (`QueueDefinitionMirror`) to read the `ucQueueType` field and determine the actual object type. This is a well-known technique — the mirror struct must match FreeRTOS's internal layout.

### Stack Monitoring

Uses `uxTaskGetStackHighWaterMark()` to get the minimum free stack in words, then subtracts from the total stack depth (captured at `traceTASK_CREATE` time) to get actual usage.

### Mutex Contention

Uses `xSemaphoreGetMutexHolder()` to get the current holder of a mutex. If the holder is a different task than the current one, a contention event is emitted.

---

## Zephyr Adapter Details

### Hook Mechanism

Zephyr uses `CONFIG_TRACING` and `CONFIG_TRACING_USER` Kconfig options. When enabled, the kernel calls weak `sys_trace_*_user()` functions at key points. The adapter overrides these:

- `sys_trace_thread_switched_in_user()` → `va_taskswitchedin()`
- `sys_trace_thread_switched_out_user()` → `va_taskswitchedout()`
- `sys_trace_thread_create_user()` → `va_zephyr_register_thread()`
- `sys_trace_isr_enter_user()` → `VA_LogISRStart()` (reads IPSR for IRQ number)
- `sys_trace_isr_exit_user()` → `VA_LogISREnd()`

Required Kconfig:
```
CONFIG_TRACING=y
CONFIG_TRACING_USER=y
CONFIG_THREAD_NAME=y
CONFIG_THREAD_STACK_INFO=y
```

### Thread Registration

Zephyr threads created before `VA_Init()` won't trigger `sys_trace_thread_create_user()`. Two mechanisms handle this:

1. **Lazy registration**: `sys_trace_thread_switched_in_user()` auto-registers any thread it hasn't seen before.
2. **Bulk registration**: `VA_Zephyr_RegisterExistingThreads()` iterates all known threads via `k_thread_foreach()`.

### Stack Monitoring

Uses `k_thread_stack_space_get()` to determine unused stack bytes, then subtracts from `thread->stack_info.size` (stored at registration time).

---

## Build Integration

### Files to Compile

| Configuration | Source Files |
|---------------|-------------|
| Bare-metal (`VA_RTOS_SELECT=0`) | `core/ViewAlyzer.c`, `core/viewalyzer_cobs.c` |
| FreeRTOS (`VA_RTOS_SELECT=1`) | Above + `freertos/VA_Adapter_FreeRTOS.c` |
| Zephyr (`VA_RTOS_SELECT=2`) | Above + `zephyr/VA_Adapter_Zephyr.c` |

### Include Paths

| Configuration | Include Directories |
|---------------|--------------------|
| Bare-metal | `core/` |
| FreeRTOS | `core/`, `freertos/` |
| Zephyr | `core/`, `zephyr/` |

### Compile Definitions

```cmake
-DVA_ENABLED=1
-DVA_RTOS_SELECT=1   # or 0, 2
```

### CMake Example (FreeRTOS with STM32CubeMX)

```cmake
set(VA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/path/to/ViewAlyzerRecorder")

target_sources(${PROJECT_NAME} PRIVATE
    ${VA_DIR}/core/ViewAlyzer.c
    ${VA_DIR}/core/viewalyzer_cobs.c
    ${VA_DIR}/freertos/VA_Adapter_FreeRTOS.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${VA_DIR}/core
    ${VA_DIR}/freertos
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    VA_ENABLED=1
    VA_RTOS_SELECT=1
)
```

### CMake Example (Zephyr)

```cmake
set(VA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/path/to/ViewAlyzerRecorder")

target_sources(app PRIVATE
    ${VA_DIR}/core/ViewAlyzer.c
    ${VA_DIR}/core/viewalyzer_cobs.c
    ${VA_DIR}/zephyr/VA_Adapter_Zephyr.c
)

target_include_directories(app PRIVATE
    ${VA_DIR}/core
    ${VA_DIR}/zephyr
)

target_compile_definitions(app PRIVATE
    VA_ENABLED=1
    VA_RTOS_SELECT=2
)
```

---

## Disabling ViewAlyzer

Set `VA_ENABLED=0` in your build system. All public API functions become no-op macros with zero overhead — no code is compiled.
