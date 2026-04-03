# ViewAlyzerRecorder Bare-Metal

Use the `core/` recorder directly when your firmware runs without an RTOS, or when you only need explicit user traces and ISR timing.

This path gives you the recorder protocol, timestamping, framing, and transport backends without automatic task scheduling integration.

## What Bare-Metal Covers

- Integer and float traces
- Strings and toggles
- User-defined function spans
- GPIO and heap usage traces
- Explicit ISR enter and exit events
- ITM/SWO, RTT, or custom transport backends

It does not create task lanes automatically because there is no scheduler adapter in this mode.

## Files

- `ViewAlyzer.h` - public API and compile-time configuration
- `ViewAlyzer.c` - recorder core implementation
- `viewalyzer_cobs.h` / `viewalyzer_cobs.c` - COBS framing used by the transport stream

## Build Defines

At minimum, set:

```c
VA_ENABLED=1
VA_RTOS_SELECT=VA_RTOS_NONE
```

Then choose one transport:

```c
VA_TRANSPORT=ARM_ITM
```

or:

```c
VA_TRANSPORT=JLINK_RTT
```

or:

```c
VA_TRANSPORT=CUSTOM_TRANSPORT
```

Useful optional defines:

- `VA_AUTO_SETUP_INTERVAL_MS` to periodically re-emit setup packets
- `VA_ALLOWED_TO_DISABLE_INTERRUPTS` to control short internal critical sections
- `VA_MAX_USER_FUNCTIONS`, `VA_MAX_TASK_NAME_LEN`, `VA_MAX_SYNC_OBJECTS`

## Minimal Integration

Compile these files into your firmware:

- `core/ViewAlyzer.c`
- `core/viewalyzer_cobs.c`

Add the `core/` directory to your include path, then initialize the recorder once your clocks and debug transport are ready:

```c
#include "ViewAlyzer.h"

void app_init(void)
{
    VA_Init(SystemCoreClock);

    VA_RegisterUserTrace(1, "LoopTime", VA_USER_TYPE_GRAPH);
    VA_RegisterUserTrace(2, "Alive", VA_USER_TYPE_TOGGLE);
    VA_RegisterUserEvent(1, "process_sample");
}
```

Log data where it matters:

```c
VA_LogTrace(1, loop_time_us);
VA_LogToggle(2, heartbeat_state);

VA_EVENT_START(1);
process_sample();
VA_EVENT_END(1);
```

For interrupt timing, instrument the ISR explicitly:

```c
void ADC_IRQHandler(void)
{
    VA_LogISRStart(ADC_IRQn);

    // ISR body

    VA_LogISREnd(ADC_IRQn);
}
```

## Long-Running Sessions

If the target can run for a long time before the host connects, call these periodically from a safe context:

- `VA_TickOverflowCheck()` to keep timestamp rollover handling correct
- `VA_EmitSetupBundle()` if you want the host to recover task, trace, and object maps after a late attach

## Custom Transport

If you set `VA_TRANSPORT=CUSTOM_TRANSPORT`, register your send function before or during initialization:

```c
static void send_bytes(const uint8_t *data, uint32_t length)
{
    my_transport_write(data, length);
}

void app_init(void)
{
    VA_RegisterTransportSend(send_bytes);
    VA_Init(SystemCoreClock);
}
```

## When to Move to an RTOS Adapter

Switch to the FreeRTOS or Zephyr path when you want:

- automatic task or thread switch events
- task creation metadata
- mutex, semaphore, queue, or notification tracing
- RTOS-aware stack usage reporting

## Related Docs

- [../README.md](../README.md)
- [../freertos/README.md](../freertos/README.md)
- [../zephyr/README.md](../zephyr/README.md)