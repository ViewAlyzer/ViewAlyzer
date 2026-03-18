/**
 * @file desktop_example.c
 * @brief ViewAlyzer desktop sender — core tracing only, no RTOS.
 *
 * Compiles and runs on x86 Windows/Linux/macOS with no embedded hardware.
 * Sends a sine wave, a counter, function spans, toggles, and string logs
 * to the ViewAlyzer desktop app over localhost UDP.
 *
 * Build (CMake):
 *   cmake -B build -DCMAKE_BUILD_TYPE=Release
 *   cmake --build build --config Release
 *
 * Build (manual):
 *   Windows (MSVC):   cl desktop_example.c ../viewalyzer_udp.c ../viewalyzer_cobs.c /I.. ws2_32.lib
 *   Windows (MinGW):  gcc desktop_example.c ../viewalyzer_udp.c ../viewalyzer_cobs.c -I.. -lws2_32 -o desktop_example
 *   Linux / macOS:    gcc desktop_example.c ../viewalyzer_udp.c ../viewalyzer_cobs.c -I.. -lm -o desktop_example
 *
 * Run:
 *   ./desktop_example            (sends to 127.0.0.1:17200)
 */

#include "viewalyzer_udp.h"

#include <stdio.h>
#include <math.h>

#ifdef _WIN32
  #include <windows.h>
  static void sleep_ms(int ms) { Sleep(ms); }
#else
  #include <unistd.h>
  static void sleep_ms(int ms) { usleep(ms * 1000); }
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main(void)
{
    const char  *host     = "127.0.0.1";
    const uint16_t port   = 17200;
    const uint32_t cpu_freq = 170000000;   /* 170 MHz — just a label for timestamps */

    printf("ViewAlyzer desktop example (core only, no RTOS)\n");
    printf("Sending to %s:%u  CPU freq: %u Hz\n\n", host, port, cpu_freq);

    va_udp_ctx_t *ctx = va_udp_init(host, port, cpu_freq);
    if (!ctx) {
        fprintf(stderr, "Failed to initialise UDP sender.\n");
        return 1;
    }

    /* ── Setup ─────────────────────────────────────────────────────── */
    va_udp_send_sync_and_clock(ctx);

    va_udp_send_trace_setup(ctx, 0, VA_UDP_TRACE_GRAPH,   "SineWave");
    va_udp_send_trace_setup(ctx, 1, VA_UDP_TRACE_COUNTER, "Counter");
    va_udp_send_trace_setup(ctx, 2, VA_UDP_TRACE_GAUGE,   "Voltage");
    va_udp_send_function_map(ctx, 0, "processData");
    va_udp_send_function_map(ctx, 1, "calibrate");

    /* ── Main loop ─────────────────────────────────────────────────── */
    uint64_t ts = 0;
    const uint64_t step = (uint64_t)(cpu_freq / 1000);  /* 1 ms per step */
    int counter = 0;

    printf("Streaming... press Ctrl+C to stop.\n");

    for (;;)
    {
        double sim_sec = (double)ts / (double)cpu_freq;

        /* Float trace: sine wave */
        float sine = (float)sin(sim_sec * 2.0 * M_PI);
        va_udp_send_trace_float(ctx, 0, ts, sine);

        /* Int32 trace: counter */
        va_udp_send_trace_int(ctx, 1, ts, counter);

        /* Float trace: simulated voltage */
        float voltage = 3.3f - 0.001f * (float)sim_sec
                        + 0.01f * (float)sin(sim_sec * 7.0);
        va_udp_send_trace_float(ctx, 2, ts, voltage);

        /* Toggle every 100 iterations */
        if (counter % 100 == 0)
            va_udp_send_toggle(ctx, 0, ts, (counter / 100) % 2 == 0);

        /* Function span every 10 iterations */
        if (counter % 10 == 0)
        {
            uint8_t fid = (counter % 20 == 0) ? 1 : 0;
            va_udp_send_function(ctx, fid, true,  ts);
            ts += step / 2;
            va_udp_send_function(ctx, fid, false, ts);
        }

        /* String log every 50 iterations */
        if (counter % 50 == 0)
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "iter=%d  sine=%.3f  V=%.3f",
                     counter, sine, voltage);
            va_udp_send_string(ctx, 0, ts, msg);
        }

        counter++;
        ts += step;

        /* Print progress every 1000 iterations */
        if (counter % 1000 == 0)
            printf("  sent %d samples  sim=%.1fs\n", counter, sim_sec);

        sleep_ms(1);
    }

    va_udp_close(ctx);
    return 0;
}
