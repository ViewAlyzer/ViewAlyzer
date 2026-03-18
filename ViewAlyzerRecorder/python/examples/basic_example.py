#!/usr/bin/env python3
"""
basic_example.py — Minimal ViewAlyzer UDP sender (core only, no RTOS).

Sends a simulated sine wave (float trace) and a counter (int trace) to
the ViewAlyzer desktop app.  No tasks, ISRs, or sync objects needed.

Usage:
    pip install ../          # install the viewalyzer package
    python basic_example.py
    python basic_example.py --port 17201
"""

import argparse
import math
import time

from viewalyzer import ViewAlyzerSender, TraceType


def main():
    parser = argparse.ArgumentParser(description="Basic ViewAlyzer UDP example")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17200)
    parser.add_argument("--cpu-freq", type=int, default=170_000_000)
    parser.add_argument("--duration", type=float, default=0, help="0 = infinite")
    args = parser.parse_args()

    cpu_freq = args.cpu_freq

    # Create the sender — sync marker and CLK are sent automatically
    va = ViewAlyzerSender(args.host, args.port, cpu_freq=cpu_freq)

    # ── Declare metadata (core only) ─────────────────────────────────
    va.send_trace_setup(0, "SineWave", TraceType.GRAPH)
    va.send_trace_setup(1, "Counter",  TraceType.COUNTER)
    va.send_function_map(0, "processData")

    print(f"Sending to {args.host}:{args.port}  (CPU freq: {cpu_freq:,} Hz)")
    print("Press Ctrl+C to stop.\n")

    # ── Stream events ─────────────────────────────────────────────────
    ts = 0
    counter = 0
    step_cycles = int(cpu_freq / 1000)   # 1 ms per step
    start = time.monotonic()

    try:
        while True:
            elapsed = time.monotonic() - start
            if args.duration > 0 and elapsed >= args.duration:
                break

            sim_sec = ts / cpu_freq

            # Float trace: sine wave
            va.send_trace_float(0, ts, math.sin(sim_sec * 2.0 * math.pi))

            # Int32 trace: counter
            va.send_trace_int(1, ts, counter)

            # Function span every 10 iterations
            if counter % 10 == 0:
                va.send_function(0, is_entry=True, timestamp=ts)
                ts += step_cycles // 2
                va.send_function(0, is_entry=False, timestamp=ts)

            # String log every 50 iterations
            if counter % 50 == 0:
                va.send_string(0, ts, f"iteration {counter}")

            counter += 1
            ts += step_cycles
            time.sleep(0.001)

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        va.close()
        print(f"Sent {counter} samples in {time.monotonic() - start:.1f}s")


if __name__ == "__main__":
    main()
