# ViewAlyzerRecorder FreeRTOS

Use the FreeRTOS adapter when you want the recorder core plus automatic RTOS event capture from FreeRTOS trace macros.

This is the right path for timeline views that need task switches, notifications, queues, semaphores, mutexes, and optional stack usage.

## What the Adapter Adds

On top of the core recorder APIs, the FreeRTOS adapter maps native kernel activity into ViewAlyzer packets for:

- task creation
- task switch in and switch out
- task notifications
- queues
- semaphores and mutexes
- mutex contention
- stack usage, when enabled

## Files

- `VA_Adapter_FreeRTOS.c` - FreeRTOS-specific adapter implementation
- `VA_Adapter_FreeRTOS.h` - adapter declaration layer included through the core
- `ViewAlyzerFreeRTOSHook.h` - trace macro hook header for older FreeRTOS macro signatures
- `ViewAlyzerFreeRTOSHook_V10_4_Plus.h` - trace macro hook header for FreeRTOS 10.4+

## Build Defines

Set the recorder mode to FreeRTOS:

```c
VA_ENABLED=1
VA_RTOS_SELECT=VA_RTOS_FREERTOS
```

Then select a transport the same way you would for bare-metal:

```c
VA_TRANSPORT=ARM_ITM
```

or:

```c
VA_TRANSPORT=JLINK_RTT
```

## Source Files to Compile

Compile these files into your firmware:

- `core/ViewAlyzer.c`
- `core/viewalyzer_cobs.c`
- `freertos/VA_Adapter_FreeRTOS.c`

Add both `core/` and `freertos/` to the include path.

## FreeRTOSConfig.h Integration

Include the correct hook header from `FreeRTOSConfig.h` so the trace macros call into the recorder.

For FreeRTOS 10.4 and newer:

```c
#include "ViewAlyzerFreeRTOSHook_V10_4_Plus.h"
```

For older macro signatures:

```c
#include "ViewAlyzerFreeRTOSHook.h"
```

The hook headers define the `traceTASK_*`, queue, mutex, semaphore, and notification macros used by the adapter.

## FreeRTOS Options That Matter

These options affect how much detail the adapter can provide:

- `configUSE_TRACE_FACILITY=1` gives the adapter reliable queue object typing
- `INCLUDE_uxTaskGetStackHighWaterMark=1` enables stack usage reporting
- `INCLUDE_xSemaphoreGetMutexHolder=1` or `INCLUDE_xQueueGetMutexHolder=1` enables mutex contention ownership detection

If those options are off, the recorder still works, but the missing data stays unavailable.

## Startup Sequence

Initialize the recorder after clocks and the selected transport backend are ready:

```c
#include "ViewAlyzer.h"

void app_init(void)
{
    VA_Init(SystemCoreClock);
    VA_RegisterUserTrace(1, "CpuLoad", VA_USER_TYPE_GRAPH);
}
```

User traces still work exactly the same as in bare-metal mode. The difference is that task and sync-object events are now emitted automatically through FreeRTOS.

## What You Usually Do Not Need to Call Manually

These functions are public because the hook layer needs them, but application code normally should not call them directly:

- `va_taskswitchedin()`
- `va_taskswitchedout()`
- `va_taskcreated()`
- `va_logtasknotifygive()`
- `va_logQueueObjectGive()`
- `va_logQueueObjectTake()`

Prefer the FreeRTOS trace macro path for scheduler and kernel-object events.

## Manual User Traces Still Apply

You can mix automatic RTOS events with explicit instrumentation:

```c
VA_RegisterUserEvent(1, "filter_block");

VA_EVENT_START(1);
filter_block();
VA_EVENT_END(1);
```

That is usually the best balance: let the adapter capture scheduler behavior automatically, then add user traces only around application-specific work.

## Related Docs

- [../README.md](../README.md)
- [../core/README.md](../core/README.md)
- [../zephyr/README.md](../zephyr/README.md)