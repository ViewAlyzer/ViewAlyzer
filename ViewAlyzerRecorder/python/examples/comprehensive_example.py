#!/usr/bin/env python3
"""
comprehensive_example.py — Exercises ALL ViewAlyzer protocol features.

Uses ``ViewAlyzerRtosSender`` (outer circle) which inherits all core
methods and adds RTOS events: tasks, ISRs, semaphores, mutexes, queues,
task notifications, mutex contention, and stack usage.

Usage:
    python comprehensive_example.py
    python comprehensive_example.py --port 17202 --duration 30
"""

import argparse
import math
import random
import time

from viewalyzer import TraceType
from viewalyzer.sender_rtos import ViewAlyzerRtosSender


def main():
    parser = argparse.ArgumentParser(description="Comprehensive VA protocol example")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17202)
    parser.add_argument("--duration", type=float, default=0, help="0 = infinite")
    parser.add_argument("--cpu-freq", type=int, default=170_000_000)
    args = parser.parse_args()

    cpu_freq = args.cpu_freq
    cyc_per_us = cpu_freq / 1_000_000

    va = ViewAlyzerRtosSender(args.host, args.port, cpu_freq=cpu_freq)

    # ── Setup burst ───────────────────────────────────────────────────

    # Tasks
    tasks = [(0, "AppMain"), (1, "SensorTask"), (2, "CommTask"), (3, "IdleTask")]
    for tid, name in tasks:
        va.send_task_map(tid, name)

    # ISRs
    isrs = [(0, "SysTick_IRQ"), (1, "UART_IRQ"), (2, "DMA_IRQ")]
    for iid, name in isrs:
        va.send_isr_map(iid, name)

    # Int32 traces
    va.send_trace_setup(0, "Counter_A",   TraceType.COUNTER)
    va.send_trace_setup(1, "ADC_Raw",     TraceType.GRAPH)
    va.send_trace_setup(2, "StatusFlags", TraceType.BAR)
    va.send_trace_setup(3, "QueueDepth",  TraceType.HISTOGRAM)

    # Float traces
    va.send_trace_setup(4, "Temperature_F", TraceType.GRAPH)
    va.send_trace_setup(5, "Voltage_V",     TraceType.GAUGE)
    va.send_trace_setup(6, "Current_mA",    TraceType.GRAPH)
    va.send_trace_setup(7, "SineWave",      TraceType.GRAPH)

    # Register trace
    va.send_trace_setup(8, "STATUS_REG", TraceType.REGISTER)

    # Sync objects
    va.send_semaphore_map(0, "DataReady")
    va.send_mutex_map(0, "SPI_Mutex")
    va.send_queue_map(0, "CmdQueue")

        # User events / spans
    va.send_function_map(0, "processFrame")
    va.send_function_map(1, "calibrate")

    print(f"[ComprehensiveExample] Target: {args.host}:{args.port}")
    print(f"[ComprehensiveExample] CPU freq: {cpu_freq:,} Hz")
    print(f"[ComprehensiveExample] Exercising ALL VA protocol features")
    print()

    # ── Send TaskCreate events ────────────────────────────────────────
    ts = 0
    for tid, name in tasks:
        va.send_task_create(tid, ts, priority=3 - tid,
                            base_priority=3 - tid, stack_size=1024 * (4 - tid))
        ts += int(10 * cyc_per_us)

    # ── Simulation state ──────────────────────────────────────────────
    active_task = 0
    counter_a = 0
    toggle_state = False
    total_bytes = 0
    start_time = time.monotonic()
    last_report = start_time
    iteration = 0
    task_order = [0, 1, 2, 0, 1, 3]

    try:
        while True:
            now = time.monotonic()
            elapsed = now - start_time
            if args.duration > 0 and elapsed >= args.duration:
                break

            iteration += 1

            for batch in range(10):
                next_task = task_order[(iteration * 10 + batch) % len(task_order)]

                # End current task
                va.send_task_switch(active_task, enter=False, timestamp=ts)
                ts += int(5 * cyc_per_us)

                # ISR (intermittent)
                if random.random() < 0.25:
                    isr_id = random.choice([0, 1, 2])
                    va.send_isr(isr_id, enter=True, timestamp=ts)
                    ts += int(random.randint(3, 40) * cyc_per_us)
                    va.send_isr(isr_id, enter=False, timestamp=ts)
                    ts += int(2 * cyc_per_us)

                # Start next task
                active_task = next_task
                va.send_task_switch(active_task, enter=True, timestamp=ts)
                slice_us = random.randint(100, 800)
                ts += int(slice_us * cyc_per_us)

                # Int32 traces
                counter_a += 1
                va.send_trace_int(0, ts, counter_a)
                va.send_trace_int(1, ts, random.randint(0, 4095))
                va.send_trace_int(2, ts, random.randint(0, 255))
                va.send_trace_int(3, ts, random.randint(0, 16))

                # Float traces
                sim_sec = ts / cpu_freq
                temp_f = 72.0 + 5.0 * math.sin(sim_sec * 0.3) + random.gauss(0, 0.2)
                voltage = 3.3 - 0.001 * sim_sec + random.gauss(0, 0.01)
                current = 150.0 + 20.0 * math.sin(sim_sec * 2.0) + random.gauss(0, 1.0)
                sine = math.sin(sim_sec * 5.0)

                va.send_trace_float(4, ts, temp_f)
                va.send_trace_float(5, ts, voltage)
                va.send_trace_float(6, ts, current)
                va.send_trace_float(7, ts, sine)

                # Register trace (simulated status register)
                reg_en   = 1 if (iteration % 10 < 7) else 0
                reg_mode = (iteration // 3) & 0x03
                reg_busy = 1 if random.random() < 0.3 else 0
                reg_err  = min(15, iteration % 20)
                status_reg = reg_en | (reg_mode << 1) | (reg_busy << 3) | (reg_err << 4)
                va.send_trace_int(8, ts, status_reg)

                # Toggle
                if batch == 0:
                    toggle_state = not toggle_state
                    va.send_toggle(0, ts, toggle_state)

                # Sync objects
                if random.random() < 0.10:
                    va.send_semaphore(0, is_give=True, timestamp=ts)
                    ts += int(2 * cyc_per_us)
                    va.send_semaphore(0, is_give=False, timestamp=ts)

                if random.random() < 0.08:
                    va.send_mutex(0, is_acquire=True, timestamp=ts)
                    ts += int(random.randint(10, 100) * cyc_per_us)
                    va.send_mutex(0, is_acquire=False, timestamp=ts)

                if random.random() < 0.06:
                    va.send_queue(0, is_send=True, timestamp=ts)
                    ts += int(5 * cyc_per_us)
                    va.send_queue(0, is_send=False, timestamp=ts)

                # Task notification
                if random.random() < 0.05:
                    src = active_task
                    dst = random.choice([t for t, _ in tasks if t != src])
                    va.send_task_notify(src, dst, ts, random.randint(1, 100))

                # Mutex contention
                if random.random() < 0.03:
                    waiter = random.choice([t for t, _ in tasks if t != active_task])
                    va.send_mutex_contention(0, waiter, active_task, ts)

                # User event spans
                if batch % 3 == 0:
                    func_id = 0 if batch < 5 else 1
                    va.send_function(func_id, is_entry=True, timestamp=ts)
                    ts += int(random.randint(20, 200) * cyc_per_us)
                    va.send_function(func_id, is_entry=False, timestamp=ts)

                # Stack usage
                if batch == 5:
                    for tid, name in tasks:
                        total_stack = 1024 * (4 - tid)
                        used = random.randint(total_stack // 4, total_stack * 3 // 4)
                        va.send_stack_usage(tid, ts, used, total_stack)

                # String messages
                if batch == 0 and iteration % 5 == 0:
                    msgs = [
                        f"Sensor calibration OK (iter={iteration})",
                        f"ADC sample rate: {random.randint(8000, 12000)} Hz",
                        f"Heap free: {random.randint(10000, 50000)} bytes",
                        f"Comm link RSSI: -{random.randint(40, 90)} dBm",
                    ]
                    va.send_string(0, ts, random.choice(msgs))

            # Pace to ~real-time
            sim_sec = ts / cpu_freq
            real_elapsed = time.monotonic() - start_time
            if real_elapsed < sim_sec * 0.5:
                time.sleep(0.005)

            # Progress report
            now2 = time.monotonic()
            if now2 - last_report >= 2.0:
                sim_s = ts / cpu_freq
                print(f"  sim={sim_s:.1f}s | counter={counter_a}"
                      f" | temp={temp_f:.1f}F | V={voltage:.3f}V | sine={sine:.3f}")
                last_report = now2

    except KeyboardInterrupt:
        print("\n[ComprehensiveExample] Interrupted.")
    finally:
        va.close()
        duration = time.monotonic() - start_time
        print(f"[ComprehensiveExample] Done. {duration:.1f}s")
        print(f"[ComprehensiveExample] Features exercised:")
        print(f"  - TaskSwitch, ISR, TaskCreate, UserTrace (int32), FloatTrace (float32)")
        print(f"  - TaskNotify, Semaphore, Mutex, Queue, TaskStackUsage")
        print(f"  - UserToggle, UserFunction, MutexContention, StringEvent")


if __name__ == "__main__":
    main()
