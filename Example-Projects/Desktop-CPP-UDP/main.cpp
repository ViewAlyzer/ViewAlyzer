/**
 * @file main.cpp
 * @brief ViewAlyzer Desktop C++ UDP Example
 *
 * Sends traces to the ViewAlyzer desktop app over localhost UDP (port 17200).
 * Exercises: int traces, float traces, function spans with batching,
 * toggles, task switches, and long string events (>200 chars).
 *
 * Runs for ~30 seconds then exits cleanly so you can also replay the
 * recorded .va file.
 *
 * Build:
 *   cmake -B build
 *   cmake --build build --config Release
 *
 * Run:
 *   ./build/Release/desktop_cpp_udp_example
 *
 * Then open ViewAlyzer, load Desktop-CPP-UDP.vaschema, and hit Connect.
 */

#include "viewalyzer_udp.h"
#include "viewalyzer_udp_rtos.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>

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

// ── Timestamp helper ───────────────────────────────────────────────────

static const auto g_start = std::chrono::steady_clock::now();
static constexpr uint32_t CPU_FREQ = 170'000'000; // 170 MHz

static uint64_t timestamp()
{
    auto now = std::chrono::steady_clock::now();
    auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now - g_start).count();
    return static_cast<uint64_t>(static_cast<double>(ns) * (static_cast<double>(CPU_FREQ) / 1.0e9));
}

// ── Trace channel IDs ──────────────────────────────────────────────────

enum TraceId : uint8_t {
    TRACE_SINE      = 0,
    TRACE_COUNTER   = 1,
    TRACE_TEMP      = 2,
    TRACE_COSINE    = 3,
};

enum FuncId : uint8_t {
    FUNC_PROCESS    = 0,
    FUNC_CALIBRATE  = 1,
    FUNC_READ_SENSOR = 2,
};

enum TaskId : uint8_t {
    TASK_MAIN       = 0,
    TASK_SENSOR     = 1,
    TASK_COMM       = 2,
};

enum StringId : uint8_t {
    STRING_LOG      = 0,
    STRING_DETAIL   = 1,
};

// ── Main ───────────────────────────────────────────────────────────────

int main()
{
    const char*    host = "127.0.0.1";
    const uint16_t port = 17200;

    std::printf("ViewAlyzer Desktop C++ UDP Example\n");
    std::printf("Sending to %s:%u for ~30 seconds...\n\n", host, port);

    va_udp_ctx_t* ctx = va_udp_init(host, port, CPU_FREQ);
    if (!ctx) {
        std::fprintf(stderr, "Failed to initialise UDP sender.\n");
        return 1;
    }

    // ── Setup packets ──────────────────────────────────────────────
    va_udp_send_sync_and_clock(ctx);

    // Trace channels
    va_udp_send_trace_setup(ctx, TRACE_SINE,    VA_UDP_TRACE_GRAPH,   "SineWave");
    va_udp_send_trace_setup(ctx, TRACE_COUNTER, VA_UDP_TRACE_COUNTER, "Counter");
    va_udp_send_trace_setup(ctx, TRACE_TEMP,    VA_UDP_TRACE_GAUGE,   "Temperature");
    va_udp_send_trace_setup(ctx, TRACE_COSINE,  VA_UDP_TRACE_GRAPH,   "CosineWave");

    // Function spans
    va_udp_send_function_map(ctx, FUNC_PROCESS,     "processData");
    va_udp_send_function_map(ctx, FUNC_CALIBRATE,   "calibrate");
    va_udp_send_function_map(ctx, FUNC_READ_SENSOR, "readSensor");

    // Tasks
    va_udp_send_task_map(ctx, TASK_MAIN,   "MainTask");
    va_udp_send_task_map(ctx, TASK_SENSOR, "SensorTask");
    va_udp_send_task_map(ctx, TASK_COMM,   "CommTask");

    // ── Main loop (~30 seconds at 1ms per iter) ────────────────────
    const int total_iters = 30000;
    int counter = 0;

    for (int i = 0; i < total_iters; ++i)
    {
        uint64_t ts = timestamp();
        double sim_sec = static_cast<double>(ts) / static_cast<double>(CPU_FREQ);

        // Float traces: sine and cosine waves
        float sine   = static_cast<float>(std::sin(sim_sec * 2.0 * M_PI));
        float cosine = static_cast<float>(std::cos(sim_sec * 2.0 * M_PI));
        va_udp_send_trace_float(ctx, TRACE_SINE,   ts, sine);
        va_udp_send_trace_float(ctx, TRACE_COSINE, ts, cosine);

        // Int trace: counter
        va_udp_send_trace_int(ctx, TRACE_COUNTER, ts, counter);

        // Float trace: simulated temperature with drift
        float temp = 22.5f + 0.5f * static_cast<float>(std::sin(sim_sec * 0.1))
                   + 0.02f * static_cast<float>(sim_sec);
        va_udp_send_trace_float(ctx, TRACE_TEMP, ts, temp);

        // Toggle every 100 iterations
        if (counter % 100 == 0)
            va_udp_send_toggle(ctx, 0, ts, (counter / 100) % 2 == 0);

        // Function spans with batching every 10 iterations
        if (counter % 10 == 0)
        {
            va_udp_batch_begin(ctx);

            uint8_t fid = (counter % 20 == 0) ? FUNC_CALIBRATE : FUNC_PROCESS;
            va_udp_send_function(ctx, fid, true, ts);

            // Nested span
            va_udp_send_function(ctx, FUNC_READ_SENSOR, true, ts);
            va_udp_send_function(ctx, FUNC_READ_SENSOR, false, timestamp());

            va_udp_send_function(ctx, fid, false, timestamp());

            va_udp_batch_flush(ctx);
        }

        // Task switches every 5 iterations
        if (counter % 5 == 0) {
            uint8_t task = static_cast<uint8_t>((counter / 5) % 3);
            va_udp_send_task_switch(ctx, task, true, ts);
        }

        // Short string log every 50 iterations
        if (counter % 50 == 0)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "iter=%d sine=%.3f temp=%.2f", counter, sine, temp);
            va_udp_send_string(ctx, STRING_LOG, ts, msg);
        }

        // Long string (>200 chars) every 500 iterations to exercise new limit
        if (counter % 500 == 0)
        {
            char longMsg[512];
            std::snprintf(longMsg, sizeof(longMsg),
                "DETAILED STATUS REPORT [iter=%d] | "
                "sine=%.6f cosine=%.6f temperature=%.4f counter=%d | "
                "System is operating normally. All sensors are responding within "
                "expected parameters. Communication link stable with no dropped "
                "packets. Memory usage is nominal. Calibration cycle completed "
                "successfully with deviation < 0.01%%. Uptime: %.1f seconds. "
                "Next scheduled maintenance window in approximately %.0f iterations.",
                counter, sine, cosine, temp, counter, sim_sec,
                static_cast<double>(total_iters - counter));
            va_udp_send_string(ctx, STRING_DETAIL, ts, longMsg);
        }

        counter++;

        if (counter % 5000 == 0)
            std::printf("  %d / %d samples (%.1fs)\n", counter, total_iters, sim_sec);

        sleep_ms(1);
    }

    std::printf("\nDone. Sent %d samples over ~30 seconds.\n", counter);
    va_udp_close(ctx);
    return 0;
}
