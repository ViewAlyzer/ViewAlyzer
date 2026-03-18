/**
 * @file viewalyzer_udp_rtos.c
 * @brief ViewAlyzer UDP RTOS extension — implementation.
 *
 * Copyright (c) 2025 Free Radical Labs
 * See LICENSE for details.
 */

#include "viewalyzer_udp_rtos.h"

#include <string.h>

/* ── Helpers (same layout as core, duplicated to keep this translation unit self-contained) */

static void write_i32_le(uint8_t *buf, int32_t v)  { memcpy(buf, &v, 4); }
static void write_u32_le(uint8_t *buf, uint32_t v) { memcpy(buf, &v, 4); }
static void write_u64_le(uint8_t *buf, uint64_t v) { memcpy(buf, &v, 8); }

/* ── RTOS setup packets ───────────────────────────────────────────────── */

void va_udp_send_task_map(va_udp_ctx_t *ctx, uint8_t task_id, const char *name)
{
    va_udp_send_name_setup(ctx, VA_UDP_SETUP_TASK_MAP, task_id, name);
}

void va_udp_send_isr_map(va_udp_ctx_t *ctx, uint8_t isr_id, const char *name)
{
    va_udp_send_name_setup(ctx, VA_UDP_SETUP_ISR_MAP, isr_id, name);
}

void va_udp_send_semaphore_map(va_udp_ctx_t *ctx, uint8_t sem_id, const char *name)
{
    va_udp_send_name_setup(ctx, VA_UDP_SETUP_SEMAPHORE_MAP, sem_id, name);
}

void va_udp_send_mutex_map(va_udp_ctx_t *ctx, uint8_t mutex_id, const char *name)
{
    va_udp_send_name_setup(ctx, VA_UDP_SETUP_MUTEX_MAP, mutex_id, name);
}

void va_udp_send_queue_map(va_udp_ctx_t *ctx, uint8_t queue_id, const char *name)
{
    va_udp_send_name_setup(ctx, VA_UDP_SETUP_QUEUE_MAP, queue_id, name);
}

/* ── RTOS event packets ───────────────────────────────────────────────── */

void va_udp_send_task_switch(va_udp_ctx_t *ctx, uint8_t task_id,
                             bool is_enter, uint64_t timestamp)
{
    uint8_t pkt[10];
    pkt[0] = VA_UDP_EVT_TASK_SWITCH | (is_enter ? VA_UDP_FLAG_START : 0);
    pkt[1] = task_id;
    write_u64_le(&pkt[2], timestamp);
    va_udp_send_raw_framed(ctx, pkt, 10);
}

void va_udp_send_isr(va_udp_ctx_t *ctx, uint8_t isr_id,
                     bool is_enter, uint64_t timestamp)
{
    uint8_t pkt[10];
    pkt[0] = VA_UDP_EVT_ISR | (is_enter ? VA_UDP_FLAG_START : 0);
    pkt[1] = isr_id;
    write_u64_le(&pkt[2], timestamp);
    va_udp_send_raw_framed(ctx, pkt, 10);
}

void va_udp_send_task_create(va_udp_ctx_t *ctx, uint8_t task_id, uint64_t timestamp,
                             int32_t priority, int32_t base_priority, int32_t stack_size)
{
    uint8_t pkt[22];
    pkt[0] = VA_UDP_EVT_TASK_CREATE | VA_UDP_FLAG_START;
    pkt[1] = task_id;
    write_u64_le(&pkt[2],  timestamp);
    write_i32_le(&pkt[10], priority);
    write_i32_le(&pkt[14], base_priority);
    write_i32_le(&pkt[18], stack_size);
    va_udp_send_raw_framed(ctx, pkt, 22);
}

void va_udp_send_task_notify(va_udp_ctx_t *ctx, uint8_t src_task_id,
                             uint8_t dst_task_id, uint64_t timestamp, int32_t value)
{
    uint8_t pkt[15];
    pkt[0] = VA_UDP_EVT_TASK_NOTIFY | VA_UDP_FLAG_START;
    pkt[1] = src_task_id;
    pkt[2] = dst_task_id;
    write_u64_le(&pkt[3],  timestamp);
    write_i32_le(&pkt[11], value);
    va_udp_send_raw_framed(ctx, pkt, 15);
}

void va_udp_send_semaphore(va_udp_ctx_t *ctx, uint8_t sem_id,
                           bool is_give, uint64_t timestamp)
{
    uint8_t pkt[10];
    pkt[0] = VA_UDP_EVT_SEMAPHORE | (is_give ? VA_UDP_FLAG_START : 0);
    pkt[1] = sem_id;
    write_u64_le(&pkt[2], timestamp);
    va_udp_send_raw_framed(ctx, pkt, 10);
}

void va_udp_send_mutex(va_udp_ctx_t *ctx, uint8_t mutex_id,
                       bool is_acquire, uint64_t timestamp)
{
    uint8_t pkt[10];
    pkt[0] = VA_UDP_EVT_MUTEX | (is_acquire ? VA_UDP_FLAG_START : 0);
    pkt[1] = mutex_id;
    write_u64_le(&pkt[2], timestamp);
    va_udp_send_raw_framed(ctx, pkt, 10);
}

void va_udp_send_queue(va_udp_ctx_t *ctx, uint8_t queue_id,
                       bool is_send, uint64_t timestamp)
{
    uint8_t pkt[10];
    pkt[0] = VA_UDP_EVT_QUEUE | (is_send ? VA_UDP_FLAG_START : 0);
    pkt[1] = queue_id;
    write_u64_le(&pkt[2], timestamp);
    va_udp_send_raw_framed(ctx, pkt, 10);
}

void va_udp_send_stack_usage(va_udp_ctx_t *ctx, uint8_t task_id, uint64_t timestamp,
                             uint32_t used_bytes, uint32_t total_bytes)
{
    uint8_t pkt[18];
    pkt[0] = VA_UDP_EVT_TASK_STACK_USAGE;
    pkt[1] = task_id;
    write_u64_le(&pkt[2],  timestamp);
    write_u32_le(&pkt[10], used_bytes);
    write_u32_le(&pkt[14], total_bytes);
    va_udp_send_raw_framed(ctx, pkt, 18);
}

void va_udp_send_mutex_contention(va_udp_ctx_t *ctx, uint8_t mutex_id,
                                  uint8_t waiting_task_id, uint8_t holder_task_id,
                                  uint64_t timestamp)
{
    uint8_t pkt[12];
    pkt[0] = VA_UDP_EVT_MUTEX_CONTENTION;
    pkt[1] = mutex_id;
    pkt[2] = waiting_task_id;
    pkt[3] = holder_task_id;
    write_u64_le(&pkt[4], timestamp);
    va_udp_send_raw_framed(ctx, pkt, 12);
}
