/*
 * Smallest possible Zephyr + ViewAlyzer integration:
 *   - blinks the board LED
 *   - logs a slow sine wave as a single user trace so you can confirm
 *     the recorder is alive end-to-end in the desktop app
 *
 * Hardware: nucleo_g474re (ITM/SWO transport).
 */

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "ViewAlyzer.h"
#include "VA_Adapter_Zephyr.h"

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define USER_TRACE_SINE   1
#define USER_TRACE_COUNTER  2
#define SAMPLE_PERIOD_MS  50    /* 100 samples per cycle */

static float simple_sine_wave(void)
{
    static float angle;
    float value = sinf(angle);
    angle += 0.0628f;           /* 2*pi / 100 */
    return value;
}

int main(void)
{
    if (!device_is_ready(led.port)) {
        return -1;
    }
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

    k_msleep(1000);             /* let debugger attach before first sync */

    VA_Init(SystemCoreClock);
    VA_Zephyr_RegisterExistingThreads();
    VA_RegisterUserTrace(USER_TRACE_SINE, "Sine", VA_USER_TYPE_GRAPH);
    VA_RegisterUserTrace(USER_TRACE_COUNTER, "Counter", VA_USER_TYPE_COUNTER);
    while (1) {
        gpio_pin_toggle_dt(&led);
        VA_LogTraceFloat(USER_TRACE_SINE, simple_sine_wave());
        VA_LogTrace(USER_TRACE_COUNTER, k_uptime_get_32());
        k_msleep(SAMPLE_PERIOD_MS);
    }

    return 0;
}
