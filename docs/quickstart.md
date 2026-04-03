# Quick Start Guide

Get ViewAlyzer tracing running in your project in minutes.

---

## Prerequisites

- ARM Cortex-M3/M4/M7/M33 target (DWT cycle counter required)
- A debug probe: ST-Link (SWO), J-Link (RTT), or any UART/USB connection (Custom transport)
- ViewAlyzer desktop application ([download](https://viewalyzer.net/#pricing))

---

## 1. Add Files to Your Project

Copy or reference the `ViewAlyzerRecorder/` directory in your project.

**Bare-metal (no RTOS):**
```
core/ViewAlyzer.c
core/viewalyzer_cobs.c     (only needed for custom transport)
```

**FreeRTOS:**
```
core/ViewAlyzer.c
core/viewalyzer_cobs.c
freertos/VA_Adapter_FreeRTOS.c
```

**Zephyr:**
```
core/ViewAlyzer.c
core/viewalyzer_cobs.c
zephyr/VA_Adapter_Zephyr.c
```

Add the corresponding include directories (`core/` and the adapter folder).

## 2. Set Compile Definitions

```cmake
# CMakeLists.txt
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VA_ENABLED=1
    VA_RTOS_SELECT=1    # 0=bare-metal, 1=FreeRTOS, 2=Zephyr
)
```

## 3. Configure Transport

Edit `VA_TRANSPORT` in `core/ViewAlyzer.h`:

```c
#define VA_TRANSPORT ARM_ITM    // ARM ITM/SWO
// #define VA_TRANSPORT JLINK_RTT   // J-Link RTT
// #define VA_TRANSPORT CUSTOM_TRANSPORT  // UART, USB, etc.
```

For **custom transport**, register a send callback before `VA_Init()`:

```c
static void my_uart_send(const uint8_t *data, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        UART->TDR = data[i];
        while (!(UART->ISR & USART_ISR_TXE));
    }
}

int main(void) {
    // ...
    VA_RegisterTransportSend(my_uart_send);
    VA_Init(SystemCoreClock);
}
```

For **ST-Link ITM/SWO**, initialize SWO before `VA_Init()`:
```c
SWO_Init(cpu_freq, swo_baudrate, itm_port);
VA_Init(SystemCoreClock);
```

## 4. Initialize

```c
#include "ViewAlyzer.h"

int main(void) {
    HAL_Init();
    SystemClock_Config();
    // ... peripheral init ...

    VA_Init(SystemCoreClock);

    // Optional: register named user traces for visualization
    VA_RegisterUserTrace(42, "Sensor Value", VA_USER_TYPE_GRAPH);
    VA_RegisterUserTrace(43, "LED Toggle", VA_USER_TYPE_TOGGLE);
    VA_RegisterUserEvent(44, "ProcessData");

    // Start RTOS...
}
```

## 5. FreeRTOS: Include Hook Header

In your `FreeRTOSConfig.h`, add at the bottom (inside `USER CODE BEGIN Defines`):

```c
#include "ViewAlyzerFreeRTOSHook_V10_4_Plus.h"   // For FreeRTOS ≥ 10.4
// or
#include "ViewAlyzerFreeRTOSHook.h"               // For FreeRTOS < 10.4
```

Ensure these FreeRTOS config options are enabled:
```c
#define configUSE_TRACE_FACILITY        1
#define configRECORD_STACK_HIGH_ADDRESS  1
```

That's it — task switches, creation, notifications, queues, mutexes, and semaphores are now traced automatically.

## 6. Zephyr: Enable Tracing in Kconfig

In your `prj.conf`:
```
CONFIG_TRACING=y
CONFIG_TRACING_USER=y
CONFIG_THREAD_NAME=y
CONFIG_THREAD_STACK_INFO=y
```

The adapter automatically hooks into Zephyr's tracing callbacks. No code changes needed beyond `VA_Init()`.

## 7. Log Custom Data

```c
// Integer trace (graph, bar, counter, etc.)
VA_LogTrace(42, sensor_reading);

// Floating point trace
VA_LogTraceFloat(60, temperature);

// String event
VA_LogString(42, "threshold exceeded");

// Toggle state (high/low)
VA_LogToggle(43, true);

// User event span
VA_EVENT_START(44);
process_data(buffer);
VA_EVENT_END(44);

// ISR entry/exit (bare-metal or manual ISR tracking)
void SysTick_Handler(void) {
    VA_LogISRStart(VA_ISR_ID_SYSTICK);
    // ... handler code ...
    VA_LogISREnd(VA_ISR_ID_SYSTICK);
}
```

## 8. Connect and View

1. Flash your firmware
2. Open ViewAlyzer desktop
3. Connect to your probe (ST-Link, J-Link, or serial port)
4. Start recording — you'll see the timeline populate with tasks, ISRs, and your custom traces

---

## Periodic Maintenance

Call `VA_TickOverflowCheck()` every 1–10 seconds to prevent DWT cycle counter overflow misses:

```c
// In a low-priority task or timer callback
void vTimerCallback(TimerHandle_t xTimer) {
    VA_TickOverflowCheck();
}
```

---

## User Trace Types

| Type | Constant | Use Case |
|------|----------|----------|
| Graph | `VA_USER_TYPE_GRAPH` | Line plot of values over time |
| Bar | `VA_USER_TYPE_BAR` | Bar chart visualization |
| Counter | `VA_USER_TYPE_COUNTER` | Incrementing/decrementing counter |
| Toggle | `VA_USER_TYPE_TOGGLE` | Binary on/off state |
| Gauge | `VA_USER_TYPE_GAUGE` | Gauge-style display |
| Histogram | `VA_USER_TYPE_HISTOGRAM` | Distribution of values |

---

## Disabling ViewAlyzer

Set `VA_ENABLED=0` and all API calls become zero-cost no-ops:
```cmake
target_compile_definitions(${PROJECT_NAME} PRIVATE VA_ENABLED=0)
```
No code is compiled, no RAM is used.
