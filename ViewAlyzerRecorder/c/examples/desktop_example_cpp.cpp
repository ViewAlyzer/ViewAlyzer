/**
 * @file desktop_example_cpp.cpp
 * @brief ViewAlyzer C++ desktop example with pluggable transport.
 *
 * Demonstrates how to use the ViewAlyzer C library from C++ with a
 * user-supplied send handler.  By default it uses a built-in UDP socket,
 * but you can replace it with any transport (serial, raw ethernet, etc.)
 * by calling va_udp_set_send_fn() after init.
 *
 * Build (CMake):
 *   cmake -B build -DCMAKE_BUILD_TYPE=Release
 *   cmake --build build --config Release
 *
 * Build (manual):
 *   Windows (MSVC):   cl /EHsc /std:c++17 desktop_example_cpp.cpp ../viewalyzer_udp.c ../viewalyzer_cobs.c /I.. ws2_32.lib
 *   Windows (MinGW):  g++ -std=c++17 desktop_example_cpp.cpp ../viewalyzer_udp.c ../viewalyzer_cobs.c -I.. -lws2_32 -o desktop_example_cpp
 *   Linux / macOS:    g++ -std=c++17 desktop_example_cpp.cpp ../viewalyzer_udp.c ../viewalyzer_cobs.c -I.. -lm -o desktop_example_cpp
 *
 * Run:
 *   ./desktop_example_cpp                        (uses default UDP socket)
 *   ./desktop_example_cpp --custom-transport     (uses example custom send handler)
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

// ── Example custom transport ───────────────────────────────────────────
//
// Replace this with your own transport: serial port, raw ethernet, etc.
// The callback receives COBS-framed data ready to send as-is.

struct MyTransport {
    const char* label;
    int packets_sent;
};

static void my_custom_send(void* arg, const uint8_t* data, size_t len)
{
    auto* transport = static_cast<MyTransport*>(arg);
    transport->packets_sent++;

    // --- Replace this with your actual send logic ---
    // For example: serial_write(data, len);
    //              raw_ethernet_send(data, len);
    //              my_custom_socket_send(data, len);
    //
    // For this demo, we just log that we would have sent it.
    if (transport->packets_sent % 500 == 0) {
        std::printf("  [%s] sent %d packets (last was %zu bytes)\n",
                    transport->label, transport->packets_sent, len);
    }
}

// ── Timestamp helper ───────────────────────────────────────────────────
//
// On a real embedded target you'd read a hardware timer (e.g. DWT->CYCCNT).
// On desktop we synthesise timestamps from std::chrono.

static const auto g_start = std::chrono::steady_clock::now();
static constexpr uint32_t CPU_FREQ = 170'000'000; // 170 MHz — just a label

static uint64_t timestamp()
{
    auto now = std::chrono::steady_clock::now();
    auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now - g_start).count();
    return static_cast<uint64_t>(static_cast<double>(ns) * (static_cast<double>(CPU_FREQ) / 1.0e9));
}

// ── Main ───────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    const char*    host = "127.0.0.1";
    const uint16_t port = 17200;

    bool use_custom_transport = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--custom-transport") == 0)
            use_custom_transport = true;
    }

    std::printf("ViewAlyzer C++ example (%s transport)\n",
                use_custom_transport ? "custom" : "default UDP");
    std::printf("Sending to %s:%u  CPU freq: %u Hz\n\n", host, port, CPU_FREQ);

    // 1. Initialise — this creates the context (and a socket for default mode)
    va_udp_ctx_t* ctx = va_udp_init(host, port, CPU_FREQ);
    if (!ctx) {
        std::fprintf(stderr, "Failed to initialise.\n");
        return 1;
    }

    // 2. (Optional) Plug in your own transport
    MyTransport transport{"custom", 0};
    if (use_custom_transport) {
        va_udp_set_send_fn(ctx, my_custom_send, &transport);
        std::printf("Custom send handler installed.\n");
    }

    // 3. Send setup packets
    va_udp_send_sync_and_clock(ctx);

    // Register trace channels
    va_udp_send_trace_setup(ctx, 0, VA_UDP_TRACE_GRAPH,     "SineWave");
    va_udp_send_trace_setup(ctx, 1, VA_UDP_TRACE_COUNTER,   "Counter");
    va_udp_send_trace_setup(ctx, 2, VA_UDP_TRACE_GAUGE,     "Temperature");
    va_udp_send_trace_setup(ctx, 3, VA_UDP_TRACE_GRAPH,     "CosineWave");

    // Register function/span names
    va_udp_send_function_map(ctx, 0, "processData");
    va_udp_send_function_map(ctx, 1, "calibrate");
    va_udp_send_function_map(ctx, 2, "readSensor");

    // Register RTOS names (if using RTOS extension)
    va_udp_send_task_map(ctx, 0, "MainTask");
    va_udp_send_task_map(ctx, 1, "SensorTask");
    va_udp_send_task_map(ctx, 2, "CommTask");

    // 4. Main loop
    int counter = 0;
    std::printf("Streaming... press Ctrl+C to stop.\n");

    for (;;)
    {
        uint64_t ts = timestamp();
        double sim_sec = static_cast<double>(ts) / static_cast<double>(CPU_FREQ);

        // Float traces
        float sine = static_cast<float>(std::sin(sim_sec * 2.0 * M_PI));
        float cosine = static_cast<float>(std::cos(sim_sec * 2.0 * M_PI));
        va_udp_send_trace_float(ctx, 0, ts, sine);
        va_udp_send_trace_float(ctx, 3, ts, cosine);

        // Int trace: counter
        va_udp_send_trace_int(ctx, 1, ts, counter);

        // Float trace: simulated temperature
        float temp = 22.5f + 0.5f * static_cast<float>(std::sin(sim_sec * 0.1));
        va_udp_send_trace_float(ctx, 2, ts, temp);

        // Toggle every 100 iterations
        if (counter % 100 == 0)
            va_udp_send_toggle(ctx, 0, ts, (counter / 100) % 2 == 0);

        // Function spans with batching — groups enter+exit into one UDP packet
        if (counter % 10 == 0)
        {
            va_udp_batch_begin(ctx);

            uint8_t fid = (counter % 20 == 0) ? 1 : 0;
            va_udp_send_function(ctx, fid, true, ts);

            // Nested span inside the batch
            va_udp_send_function(ctx, 2, true, ts);
            va_udp_send_function(ctx, 2, false, timestamp());

            va_udp_send_function(ctx, fid, false, timestamp());

            va_udp_batch_flush(ctx);
        }

        // Simulate RTOS task switches
        if (counter % 5 == 0) {
            uint8_t task = static_cast<uint8_t>(counter / 5 % 3);
            va_udp_send_task_switch(ctx, task, true, ts);
        }

        // String log every 50 iterations
        if (counter % 50 == 0)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "iter=%d  sine=%.3f  temp=%.2f",
                          counter, sine, temp);
            va_udp_send_string(ctx, 0, ts, msg);
        }

        counter++;

        if (counter % 1000 == 0)
            std::printf("  sent %d samples  sim=%.1fs\n", counter, sim_sec);

        sleep_ms(1);
    }

    va_udp_close(ctx);
    return 0;
}
