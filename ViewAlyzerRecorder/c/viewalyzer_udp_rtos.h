/**
 * @file viewalyzer_udp_rtos.h
 * @brief ViewAlyzer UDP RTOS extension — tasks, ISRs, sync objects.
 *
 * Include this header (in addition to viewalyzer_udp.h) when your
 * firmware runs an RTOS.  It adds setup and event functions for:
 *   - TaskSwitch, ISR, TaskCreate
 *   - TaskNotify, TaskStackUsage
 *   - Semaphore, Mutex, Queue, MutexContention
 *
 * All functions take the same va_udp_ctx_t* handle from va_udp_init().
 *
 * Copyright (c) 2025 Free Radical Labs
 * See LICENSE for details.
 */

#ifndef VIEWALYZER_UDP_RTOS_H
#define VIEWALYZER_UDP_RTOS_H

#include "viewalyzer_udp.h"   /* for va_udp_ctx_t, FLAG_START, etc. */

#ifdef __cplusplus
extern "C" {
#endif

/* ── RTOS event type codes ─────────────────────────────────────────────── */

#define VA_UDP_EVT_TASK_SWITCH      0x01
#define VA_UDP_EVT_ISR              0x02
#define VA_UDP_EVT_TASK_CREATE      0x03
#define VA_UDP_EVT_TASK_NOTIFY      0x05
#define VA_UDP_EVT_SEMAPHORE        0x06
#define VA_UDP_EVT_MUTEX            0x07
#define VA_UDP_EVT_QUEUE            0x08
#define VA_UDP_EVT_TASK_STACK_USAGE 0x09
#define VA_UDP_EVT_MUTEX_CONTENTION 0x0C

/* ── RTOS setup packet codes ──────────────────────────────────────────── */

#define VA_UDP_SETUP_TASK_MAP      0x70
#define VA_UDP_SETUP_ISR_MAP       0x71
#define VA_UDP_SETUP_SEMAPHORE_MAP 0x73
#define VA_UDP_SETUP_MUTEX_MAP     0x74
#define VA_UDP_SETUP_QUEUE_MAP     0x75

/* ── RTOS setup packets ───────────────────────────────────────────────── */

void va_udp_send_task_map     (va_udp_ctx_t *ctx, uint8_t task_id,  const char *name);
void va_udp_send_isr_map      (va_udp_ctx_t *ctx, uint8_t isr_id,   const char *name);
void va_udp_send_semaphore_map(va_udp_ctx_t *ctx, uint8_t sem_id,   const char *name);
void va_udp_send_mutex_map    (va_udp_ctx_t *ctx, uint8_t mutex_id, const char *name);
void va_udp_send_queue_map    (va_udp_ctx_t *ctx, uint8_t queue_id, const char *name);

/* ── RTOS event packets ───────────────────────────────────────────────── */

void va_udp_send_task_switch(va_udp_ctx_t *ctx, uint8_t task_id,
                             bool is_enter, uint64_t timestamp);

void va_udp_send_isr(va_udp_ctx_t *ctx, uint8_t isr_id,
                     bool is_enter, uint64_t timestamp);

void va_udp_send_task_create(va_udp_ctx_t *ctx, uint8_t task_id, uint64_t timestamp,
                             int32_t priority, int32_t base_priority, int32_t stack_size);

void va_udp_send_task_notify(va_udp_ctx_t *ctx, uint8_t src_task_id,
                             uint8_t dst_task_id, uint64_t timestamp, int32_t value);

void va_udp_send_semaphore(va_udp_ctx_t *ctx, uint8_t sem_id,
                           bool is_give, uint64_t timestamp);

void va_udp_send_mutex(va_udp_ctx_t *ctx, uint8_t mutex_id,
                       bool is_acquire, uint64_t timestamp);

void va_udp_send_queue(va_udp_ctx_t *ctx, uint8_t queue_id,
                       bool is_send, uint64_t timestamp);

void va_udp_send_stack_usage(va_udp_ctx_t *ctx, uint8_t task_id, uint64_t timestamp,
                             uint32_t used_bytes, uint32_t total_bytes);

void va_udp_send_mutex_contention(va_udp_ctx_t *ctx, uint8_t mutex_id,
                                  uint8_t waiting_task_id, uint8_t holder_task_id,
                                  uint64_t timestamp);

#ifdef __cplusplus
}
#endif

#endif /* VIEWALYZER_UDP_RTOS_H */
