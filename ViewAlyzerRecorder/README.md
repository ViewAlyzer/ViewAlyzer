# ViewAlyzerRecorder

Embedded recorder sources for streaming ViewAlyzer trace data from firmware to the desktop app.

This directory now has three distinct embedded integration paths:

| Path | Use when | Main files |
|------|----------|------------|
| `core/` | Bare-metal firmware, or shared core used by an RTOS adapter | `ViewAlyzer.h`, `ViewAlyzer.c` |
| `freertos/` | FreeRTOS projects that want automatic task and sync-object tracing | `VA_Adapter_FreeRTOS.c`, `ViewAlyzerFreeRTOSHook*.h` |
| `zephyr/` | Zephyr projects using the supplied module and `CONFIG_TRACING_USER` hooks | `VA_Adapter_Zephyr.c`, `Kconfig`, `module.yml` |

There are also two host-side UDP sender libraries:

| Path | Purpose |
|------|---------|
| `c/` | Standalone C library for sending ViewAlyzer packets over UDP |
| `python/` | Python package for sending ViewAlyzer packets over UDP |

Those UDP libraries are useful for desktop tools, simulation, or non-embedded producers. They are not the ITM/SWO or RTT firmware recorder.

## Choose the Right Path

### Bare-metal

Use [core/README.md](core/README.md) if your firmware does not have an RTOS, or if you only want explicit user traces and ISR events.

### FreeRTOS

Use [freertos/README.md](freertos/README.md) if you want task switches, task creation, notifications, queues, semaphores, mutexes, and optional stack usage driven from FreeRTOS trace macros.

### Zephyr

Use [zephyr/README.md](zephyr/README.md) if you want the Zephyr module integration with Kconfig-based transport selection and native tracing callbacks.

## Application API

This section covers the normal application-facing API from `core/ViewAlyzer.h`.

The functions below are the ones firmware code is expected to call directly. RTOS adapter hooks such as `va_taskswitchedin()` or `va_logQueueObjectTake()` are not part of the normal application API; they are used by the FreeRTOS and Zephyr integration layers.

### Initialization and Session Control

| API | Applies To | Purpose |
|-----|------------|---------|
| `VA_Init(uint32_t cpu_freq)` | Bare-metal, FreeRTOS, Zephyr | Starts the recorder and emits the initial sync and setup packets. `cpu_freq` must match the actual timestamp clock so durations in the UI match hardware time rather than host arrival time. In live-attach workflows, a short startup delay before `VA_Init()` can help the host catch the initial setup packets; schema-backed workflows usually do not need that delay. |
| `VA_EmitSetupBundle(void)` | Bare-metal, FreeRTOS, Zephyr | Re-emits the sync marker and all known setup packets so a host can re-sync after attaching late. You should not have to call this manually because the recorder can re-emit setup automatically through `VA_AUTO_SETUP_INTERVAL_MS` unless you modidfy its source code . |
| `VA_TickOverflowCheck(void)` | Bare-metal, FreeRTOS, Zephyr | Handles timestamp rollover in long-running sessions. You should not have to call this manually because the recorder already services this internally. |
| `VA_RegisterTransportSend(VA_TransportSendFn sendFn)` | Bare-metal, FreeRTOS, Zephyr | Registers a custom byte transport when `VA_TRANSPORT=CUSTOM_TRANSPORT`. Not used for ITM/SWO or RTT builds. |

### Trace and Metadata Registration

Register traces once during startup so the host can map IDs to names and display styles.

| API | Applies To | Purpose |
|-----|------------|---------|
| `VA_RegisterUserTrace(uint8_t id, const char *name, VA_UserTraceType_t type)` | Bare-metal, FreeRTOS, Zephyr | Declares a user trace ID, its display name, and its visualization type. If the desktop app is preloaded with a schema, this setup call is optional, but the schema trace ID must match the ID sent on the wire. |
| `VA_RegisterUserEvent(uint8_t id, const char *name)` | Bare-metal, FreeRTOS, Zephyr | Declares a named user event or span ID for `VA_LogEvent()` or the convenience macros. If the desktop app is preloaded with a schema, this setup call is optional, but the schema event ID must match the ID sent on the wire. |


Supported `VA_UserTraceType_t` values:

- `VA_USER_TYPE_GRAPH`
- `VA_USER_TYPE_BAR`
- `VA_USER_TYPE_GAUGE`
- `VA_USER_TYPE_COUNTER`
- `VA_USER_TYPE_TABLE`
- `VA_USER_TYPE_HISTOGRAM`
- `VA_USER_TYPE_TOGGLE`
- `VA_USER_TYPE_TASK`
- `VA_USER_TYPE_ISR`

### Event Logging

Use these calls in normal firmware code to emit runtime data.

| API | Applies To | Purpose |
|-----|------------|---------|
| `VA_LogTrace(uint8_t id, int32_t value)` | Bare-metal, FreeRTOS, Zephyr | Emits an integer-valued trace sample. This still works when metadata comes from a schema, as long as the runtime ID matches the schema ID. |
| `VA_LogTraceFloat(uint8_t id, float value)` | Bare-metal, FreeRTOS, Zephyr | Emits a floating-point trace sample. This still works when metadata comes from a schema, as long as the runtime ID matches the schema ID. |
| `VA_LogString(uint8_t id, const char *msg)` | Bare-metal, FreeRTOS, Zephyr | Emits a string event or message. |
| `VA_LogToggle(uint8_t id, bool state)` | Bare-metal, FreeRTOS, Zephyr | Emits a boolean state change. |
| `VA_LogEvent(uint8_t id, bool state)` | Bare-metal, FreeRTOS, Zephyr | Emits a start or end event for a registered user event ID. Typically used with `USER_EVENT_START` / `USER_EVENT_END`. In schema-backed workflows, the event metadata can come from the schema instead of a startup registration call. You can use the convenience macros below. |
| `VA_LogISRStart(uint8_t isrId)` | Bare-metal, FreeRTOS, Zephyr | Marks ISR entry for the given interrupt ID. |
| `VA_LogISREnd(uint8_t isrId)` | Bare-metal, FreeRTOS, Zephyr | Marks ISR exit for the given interrupt ID. |
| `VA_LogCounter(uint8_t id, uint32_t value)` | Bare-metal, FreeRTOS, Zephyr | Emits a monotonic or sampled counter value. |


### Convenience Macros

For instrumented spans or paired events, `ViewAlyzer.h` provides:

- `VA_EVENT_START(id)`
- `VA_EVENT_END(id)`

These expand to `VA_LogEvent()` when `VA_ENABLED=1`, and to empty stubs when tracing is disabled.

The older names `VA_RegisterUserFunction()`, `VA_LogUserEvent()`, `VA_FUNCTION_ENTRY()`, and `VA_FUNCTION_EXIT()` remain available as backward-compatible aliases.

### Minimal Startup Example

```c
#include "ViewAlyzer.h"

void app_init(void)
{
     delay_ms(2000); // so app side cant catch init
	VA_Init(SystemCoreClock);
    //only need to register if not using a schema
	VA_RegisterUserTrace(1, "Temperature", VA_USER_TYPE_GRAPH);
	VA_RegisterUserEvent(1, "process_sample");
}

void app_loop(void)
{
	VA_LogTrace(1, current_temperature);

	VA_EVENT_START(1);
	process_sample();
	VA_EVENT_END(1);
}
```

## Transport Backends

The recorder core supports three transport modes through `VA_TRANSPORT`:

- `ST_LINK_ITM` for ARM ITM/SWO
- `JLINK_RTT` for SEGGER RTT
- `CUSTOM_TRANSPORT` for a user-supplied send callback

The transport is selected in build defines, typically alongside `VA_ENABLED` and `VA_RTOS_SELECT`.

`core/viewalyzer_cobs.c` is only needed when your custom transport needs packet framing on a raw byte stream, such as UART or another serial-style link. It is not required for debugger-backed transports such as ITM/SWO or RTT.

This is a work in progress which will be overhauld to be generic openocd backend support as opposed to branded probe driven support. Meaning if openocd supports the probe we should to. Currently only STLink and Jlink have been tested. 
## Typical Embedded Source Set

Bare-metal:

- `core/ViewAlyzer.c`
- `core/viewalyzer_cobs.c` when using a framed custom transport such as UART

FreeRTOS:

- `core/ViewAlyzer.c`
- `core/viewalyzer_cobs.c` when using a framed custom transport such as UART
- `freertos/VA_Adapter_FreeRTOS.c`

Zephyr:

- `core/ViewAlyzer.c`
- `core/viewalyzer_cobs.c` when using a framed custom transport such as UART
- `zephyr/VA_Adapter_Zephyr.c`

## Notes

- `ViewAlyzer.h` includes `main.h`. If your project uses a different board/application header layout, provide a compatible `main.h` or adapt your include path accordingly.
- The recorder is designed so user-facing trace APIs stay the same across bare-metal, FreeRTOS, and Zephyr. The RTOS-specific behavior lives in the adapter layer.
- For live attach workflows, it can be useful to delay `VA_Init()` briefly at startup so the host debugger and desktop app have time to connect before the initial sync and setup packets are emitted. This matters most when the host relies on those startup packets to learn names and types. If you are using a schema-backed workflow that pre-seeds trace metadata on the host side, that startup delay is usually unnecessary.

## License

Copyright (c) 2025 Free Radical Labs. See [../LICENSE](../LICENSE).