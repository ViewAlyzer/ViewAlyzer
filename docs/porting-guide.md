# Porting ViewAlyzer to a New RTOS

This guide walks through adding support for a new RTOS (e.g. ThreadX, RT-Thread, NuttX) to the ViewAlyzer Recorder.

---

## What You Need to Implement

An adapter is a single `.c` / `.h` pair that lives in its own directory under `ViewAlyzerRecorder/`:

```
ViewAlyzerRecorder/
├── core/              # Don't touch
├── freertos/          # Reference adapter
├── zephyr/            # Reference adapter
└── your_rtos/         # ← New
    ├── VA_Adapter_YourRTOS.c
    └── VA_Adapter_YourRTOS.h
```

### Step 1: Pick an RTOS Slot

Open `core/ViewAlyzer.h` and add a constant. ThreadX is already reserved:

```c
#define VA_RTOS_NONE     0
#define VA_RTOS_FREERTOS 1
#define VA_RTOS_ZEPHYR   2
#define VA_RTOS_THREADX  3   // Already reserved
#define VA_RTOS_MYOS     4   // ← Add yours
```

### Step 2: Implement the Adapter Interface

Your adapter `.c` file must implement these four functions (declared in `ViewAlyzer.h`, gated by `VA_HAS_RTOS`):

```c
#include "ViewAlyzer.h"
#include "VA_Internal.h"

#if (VA_ENABLED == 1) && (VA_RTOS_SELECT == VA_RTOS_MYOS)

/* 1. Determine the sync-object type from a native handle.
 *    Return VA_OBJECT_TYPE_QUEUE, VA_OBJECT_TYPE_MUTEX, etc.
 *    If your RTOS uses distinct types for mutex vs queue (unlike FreeRTOS),
 *    you can identify the type from the object's struct or metadata.
 */
VA_QueueObjectType_t va_adapter_get_queue_object_type(void *handle)
{
    // Your RTOS-specific inspection here
    return VA_OBJECT_TYPE_QUEUE;
}

/* 2. Return how much stack the given task is using (in words or bytes —
 *    be consistent with what you store in ulStackDepth during creation).
 */
uint32_t va_adapter_calculate_stack_usage(void *taskHandle)
{
    // Use your RTOS's stack introspection API
    return 0;
}

/* 3. Return total stack size for the given task handle. */
uint32_t va_adapter_get_total_stack_size(void *taskHandle)
{
    int idx = _va_find_task_index(taskHandle);
    if (idx >= 0)
        return taskMap[idx].ulStackDepth;
    return 0;
}

/* 4. If the object is a mutex and there's contention, emit a
 *    contention packet. Use your RTOS's mutex-owner API if available.
 */
void va_adapter_check_mutex_contention(void *queueObject, uint8_t queue_va_id)
{
    // Optional — can be a no-op stub initially
    (void)queueObject;
    (void)queue_va_id;
}

#endif
```

### Step 3: Hook into Your RTOS Kernel

You need to intercept these kernel events and call the core's hooks:

| When this happens... | Call this core function |
|---------------------|----------------------|
| Task/thread switched in | `va_taskswitchedin((void *)task_handle)` |
| Task/thread switched out | `va_taskswitchedout((void *)task_handle)` |
| Task/thread created | Set `g_task_*` globals, then `va_taskcreated((void *)handle, name)` |
| ISR entered | `VA_LogISRStart(irq_number)` |
| ISR exited | `VA_LogISREnd(irq_number)` |

**Before calling `va_taskcreated()`**, populate the global creation state so the core stores it in the task map:

```c
g_task_pxStack       = (void *)stack_base_address;  // or NULL
g_task_pxEndOfStack  = (void *)stack_end_address;    // or NULL
g_task_uxPriority    = task_priority;
g_task_uxBasePriority = task_base_priority;
g_task_ulStackDepth  = total_stack_size;              // words or bytes, match your stack usage calc
```

How you intercept these events depends on the RTOS:

| Method | RTOS Examples |
|--------|---------------|
| **Compile-time trace macros** | FreeRTOS (`traceTASK_SWITCHED_IN()`, etc.) |
| **Weak-function overrides** | Zephyr (`sys_trace_*_user()`) |
| **Callback registration** | ThreadX (`tx_trace_enable`, event callbacks) |
| **Instrumentation APIs** | NuttX, RT-Thread (hook registration) |

### Step 4: Handle Pre-Init Threads

Some RTOSes create threads (idle, main, timers) before your `VA_Init()` runs. Two strategies:

1. **Lazy registration** — In your switch-in hook, check if the thread is already mapped. If not, register it:
   ```c
   void your_rtos_switch_in_hook(void *thread)
   {
       if (!va_isnit()) return;
       if (_va_find_task_id(thread) == 0)
           register_thread(thread);  // your helper
       va_taskswitchedin(thread);
   }
   ```

2. **Bulk registration** — Provide a function that iterates all threads right after `VA_Init()`:
   ```c
   void VA_YourRTOS_RegisterExistingThreads(void);
   ```

### Step 5: Build Integration

In your project's CMakeLists.txt (or Makefile):

```cmake
target_sources(${PROJECT_NAME} PRIVATE
    ${VA_DIR}/core/ViewAlyzer.c
    ${VA_DIR}/core/viewalyzer_cobs.c
    ${VA_DIR}/your_rtos/VA_Adapter_YourRTOS.c    # ← Add this
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${VA_DIR}/core
    ${VA_DIR}/your_rtos                            # ← Add this
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    VA_ENABLED=1
    VA_RTOS_SELECT=4                               # ← Your slot number
)
```

---

## Checklist

- [ ] Added `VA_RTOS_MYOS` constant to `ViewAlyzer.h`
- [ ] Implemented `va_adapter_get_queue_object_type()`
- [ ] Implemented `va_adapter_calculate_stack_usage()`
- [ ] Implemented `va_adapter_get_total_stack_size()`
- [ ] Implemented `va_adapter_check_mutex_contention()` (stub is fine initially)
- [ ] Hooked task switch in/out → `va_taskswitchedin()` / `va_taskswitchedout()`
- [ ] Hooked task creation → set `g_task_*` globals + `va_taskcreated()`
- [ ] Hooked ISR enter/exit → `VA_LogISRStart()` / `VA_LogISREnd()`
- [ ] Handled pre-init threads (lazy or bulk registration)
- [ ] Updated build system to compile adapter + set `VA_RTOS_SELECT`
- [ ] Verified on hardware — tasks appear in ViewAlyzer timeline

---

## Reference

Study these existing adapters:

- **FreeRTOS** — `freertos/VA_Adapter_FreeRTOS.c` (most complete: queue mirror hack, stack watermark, mutex contention)
- **Zephyr** — `zephyr/VA_Adapter_Zephyr.c` (weak-function override pattern, lazy thread registration, ISR from IPSR)
