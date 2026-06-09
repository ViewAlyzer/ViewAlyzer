/**
 * @file tracing_user.h
 * @brief ViewAlyzer wrapper over Zephyr's stock tracing_user.h
 *
 * Earlier versions of this file were a *full replacement* for the stock
 * subsys/tracing/user/tracing_user.h. That forced us to mirror EVERY
 * sys_port_trace_* macro — and Zephyr changes those between releases (e.g.
 * 4.3 changed k_condvar_wait_* arity and added k_msgq_put_front_*, timer
 * expiry, RTIO, etc.). A stale mirror breaks the kernel build on a Zephyr
 * bump, even for trace points ViewAlyzer never touches.
 *
 * Instead we now WRAP the stock header:
 *   1. #include_next pulls in Zephyr's own tracing_user.h, so every macro
 *      keeps its correct, version-matched signature.
 *   2. We then #undef + redefine ONLY the kernel-object trace points the
 *      recorder cares about (mutex / semaphore / message-queue / timer /
 *      heap / sleep / pm), pointing them at VA_Adapter_Zephyr.c.
 *
 * Thread / ISR / idle / GPIO tracing is left ENTIRELY to the stock header —
 * it already dispatches those to the *_user weak callbacks that
 * VA_Adapter_Zephyr.c implements, so task slices and the timeline work with
 * no help from us.
 *
 * Mechanism: our directory is placed ahead of subsys/tracing/user on the
 * global zephyr_interface include path (see CMakeLists.txt:
 * target_include_directories(zephyr_interface BEFORE INTERFACE ...)), so
 * Zephyr's `#include "tracing_user.h"` resolves to THIS file first. We use a
 * unique include guard (NOT _TRACE_USER_H) so #include_next still reaches the
 * stock header, which self-guards with _TRACE_USER_H.
 *
 * Copyright (c) 2020 Lexmark International, Inc.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc.
 * Copyright (c) 2025 Free Radical Labs — ViewAlyzer extensions
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VIEWALYZER_TRACING_USER_WRAP_H
#define VIEWALYZER_TRACING_USER_WRAP_H

/* Pull in Zephyr's stock tracing_user.h: all macros at the arity that matches
 * the Zephyr version actually being built, plus the thread/ISR/GPIO dispatch
 * declarations. */
#include_next <tracing_user.h>

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ════════════════════════════════════════════════════════════════
 *  ViewAlyzer adapter dispatch-function declarations.
 *  Defined in VA_Adapter_Zephyr.c (the stock header does not declare
 *  these — they are VA-specific kernel-object hooks).
 * ════════════════════════════════════════════════════════════════ */

#if defined(CONFIG_VIEWALYZER_TRACE_MUTEXES)
void sys_trace_k_mutex_init(struct k_mutex *mutex, int ret);
void sys_trace_k_mutex_lock_enter(struct k_mutex *mutex, k_timeout_t timeout);
void sys_trace_k_mutex_lock_blocking(struct k_mutex *mutex, k_timeout_t timeout);
void sys_trace_k_mutex_lock_exit(struct k_mutex *mutex, k_timeout_t timeout, int ret);
void sys_trace_k_mutex_unlock_enter(struct k_mutex *mutex);
void sys_trace_k_mutex_unlock_exit(struct k_mutex *mutex, int ret);
#endif

#if defined(CONFIG_VIEWALYZER_TRACE_SEMAPHORES)
void sys_trace_k_sem_init(struct k_sem *sem, int ret);
void sys_trace_k_sem_give_enter(struct k_sem *sem);
void sys_trace_k_sem_take_enter(struct k_sem *sem, k_timeout_t timeout);
void sys_trace_k_sem_take_blocking(struct k_sem *sem, k_timeout_t timeout);
void sys_trace_k_sem_take_exit(struct k_sem *sem, k_timeout_t timeout, int ret);
#endif

#if defined(CONFIG_VIEWALYZER_TRACE_MESSAGE_QUEUES)
void sys_trace_k_msgq_init(struct k_msgq *msgq);
void sys_trace_k_msgq_put_enter(struct k_msgq *msgq, k_timeout_t timeout);
void sys_trace_k_msgq_put_blocking(struct k_msgq *msgq, k_timeout_t timeout);
void sys_trace_k_msgq_put_exit(struct k_msgq *msgq, k_timeout_t timeout, int ret);
void sys_trace_k_msgq_get_enter(struct k_msgq *msgq, k_timeout_t timeout);
void sys_trace_k_msgq_get_blocking(struct k_msgq *msgq, k_timeout_t timeout);
void sys_trace_k_msgq_get_exit(struct k_msgq *msgq, k_timeout_t timeout, int ret);
#endif

#if defined(CONFIG_VIEWALYZER_TRACE_SLEEP)
void sys_trace_k_thread_sleep_enter(k_timeout_t timeout);
void sys_trace_k_thread_sleep_exit(k_timeout_t timeout, int ret);
void sys_trace_k_thread_msleep_enter(int32_t ms);
void sys_trace_k_thread_msleep_exit(int32_t ms, int ret);
void sys_trace_k_thread_usleep_enter(int32_t us);
void sys_trace_k_thread_usleep_exit(int32_t us, int ret);
#endif

#if defined(CONFIG_VIEWALYZER_TRACE_TIMERS)
void sys_trace_k_timer_init(struct k_timer *timer);
void sys_trace_k_timer_start(struct k_timer *timer, k_timeout_t duration, k_timeout_t period);
void sys_trace_k_timer_stop(struct k_timer *timer);
void sys_trace_k_timer_status_sync_blocking(struct k_timer *timer, k_timeout_t timeout);
#endif

#if defined(CONFIG_VIEWALYZER_TRACE_HEAPS)
void sys_trace_k_heap_init(struct k_heap *heap);
void sys_trace_k_heap_alloc_exit_impl(struct k_heap *heap, uint32_t alloc_bytes, void *ret);
void sys_trace_k_heap_free(struct k_heap *heap);
void sys_trace_k_heap_alloc_blocking(struct k_heap *heap);
#endif

#if defined(CONFIG_VIEWALYZER_TRACE_PM)
void sys_trace_va_pm_system_suspend_enter(uint32_t ticks);
void sys_trace_va_pm_system_suspend_exit(uint32_t ticks, uint8_t state);
#endif

/* ════════════════════════════════════════════════════════════════
 *  Override ONLY the trace points ViewAlyzer records. The stock header
 *  defined each of these as a no-op; #undef then re-point at the adapter.
 *  Categories left disabled keep the stock no-op (no #undef needed).
 * ════════════════════════════════════════════════════════════════ */

#if defined(CONFIG_VIEWALYZER_TRACE_MUTEXES)
#undef  sys_port_trace_k_mutex_init
#define sys_port_trace_k_mutex_init(mutex, ret) sys_trace_k_mutex_init(mutex, ret)
#undef  sys_port_trace_k_mutex_lock_enter
#define sys_port_trace_k_mutex_lock_enter(mutex, timeout) sys_trace_k_mutex_lock_enter(mutex, timeout)
#undef  sys_port_trace_k_mutex_lock_blocking
#define sys_port_trace_k_mutex_lock_blocking(mutex, timeout) sys_trace_k_mutex_lock_blocking(mutex, timeout)
#undef  sys_port_trace_k_mutex_lock_exit
#define sys_port_trace_k_mutex_lock_exit(mutex, timeout, ret) sys_trace_k_mutex_lock_exit(mutex, timeout, ret)
#undef  sys_port_trace_k_mutex_unlock_enter
#define sys_port_trace_k_mutex_unlock_enter(mutex) sys_trace_k_mutex_unlock_enter(mutex)
#undef  sys_port_trace_k_mutex_unlock_exit
#define sys_port_trace_k_mutex_unlock_exit(mutex, ret) sys_trace_k_mutex_unlock_exit(mutex, ret)
#endif

#if defined(CONFIG_VIEWALYZER_TRACE_SEMAPHORES)
#undef  sys_port_trace_k_sem_init
#define sys_port_trace_k_sem_init(sem, ret) sys_trace_k_sem_init(sem, ret)
#undef  sys_port_trace_k_sem_give_enter
#define sys_port_trace_k_sem_give_enter(sem) sys_trace_k_sem_give_enter(sem)
#undef  sys_port_trace_k_sem_take_enter
#define sys_port_trace_k_sem_take_enter(sem, timeout) sys_trace_k_sem_take_enter(sem, timeout)
#undef  sys_port_trace_k_sem_take_blocking
#define sys_port_trace_k_sem_take_blocking(sem, timeout) sys_trace_k_sem_take_blocking(sem, timeout)
#undef  sys_port_trace_k_sem_take_exit
#define sys_port_trace_k_sem_take_exit(sem, timeout, ret) sys_trace_k_sem_take_exit(sem, timeout, ret)
#endif

#if defined(CONFIG_VIEWALYZER_TRACE_MESSAGE_QUEUES)
#undef  sys_port_trace_k_msgq_init
#define sys_port_trace_k_msgq_init(msgq) sys_trace_k_msgq_init(msgq)
#undef  sys_port_trace_k_msgq_put_enter
#define sys_port_trace_k_msgq_put_enter(msgq, timeout) sys_trace_k_msgq_put_enter(msgq, timeout)
#undef  sys_port_trace_k_msgq_put_blocking
#define sys_port_trace_k_msgq_put_blocking(msgq, timeout) sys_trace_k_msgq_put_blocking(msgq, timeout)
#undef  sys_port_trace_k_msgq_put_exit
#define sys_port_trace_k_msgq_put_exit(msgq, timeout, ret) sys_trace_k_msgq_put_exit(msgq, timeout, ret)
#undef  sys_port_trace_k_msgq_get_enter
#define sys_port_trace_k_msgq_get_enter(msgq, timeout) sys_trace_k_msgq_get_enter(msgq, timeout)
#undef  sys_port_trace_k_msgq_get_blocking
#define sys_port_trace_k_msgq_get_blocking(msgq, timeout) sys_trace_k_msgq_get_blocking(msgq, timeout)
#undef  sys_port_trace_k_msgq_get_exit
#define sys_port_trace_k_msgq_get_exit(msgq, timeout, ret) sys_trace_k_msgq_get_exit(msgq, timeout, ret)
#endif

#if defined(CONFIG_VIEWALYZER_TRACE_SLEEP)
#undef  sys_port_trace_k_thread_sleep_enter
#define sys_port_trace_k_thread_sleep_enter(timeout) sys_trace_k_thread_sleep_enter(timeout)
#undef  sys_port_trace_k_thread_sleep_exit
#define sys_port_trace_k_thread_sleep_exit(timeout, ret) sys_trace_k_thread_sleep_exit(timeout, ret)
#undef  sys_port_trace_k_thread_msleep_enter
#define sys_port_trace_k_thread_msleep_enter(ms) sys_trace_k_thread_msleep_enter(ms)
#undef  sys_port_trace_k_thread_msleep_exit
#define sys_port_trace_k_thread_msleep_exit(ms, ret) sys_trace_k_thread_msleep_exit(ms, ret)
#undef  sys_port_trace_k_thread_usleep_enter
#define sys_port_trace_k_thread_usleep_enter(us) sys_trace_k_thread_usleep_enter(us)
#undef  sys_port_trace_k_thread_usleep_exit
#define sys_port_trace_k_thread_usleep_exit(us, ret) sys_trace_k_thread_usleep_exit(us, ret)
#endif

#if defined(CONFIG_VIEWALYZER_TRACE_TIMERS)
#undef  sys_port_trace_k_timer_init
#define sys_port_trace_k_timer_init(timer) sys_trace_k_timer_init(timer)
#undef  sys_port_trace_k_timer_start
#define sys_port_trace_k_timer_start(timer, duration, period) sys_trace_k_timer_start(timer, duration, period)
#undef  sys_port_trace_k_timer_stop
#define sys_port_trace_k_timer_stop(timer) sys_trace_k_timer_stop(timer)
#undef  sys_port_trace_k_timer_status_sync_blocking
#define sys_port_trace_k_timer_status_sync_blocking(timer, timeout) sys_trace_k_timer_status_sync_blocking(timer, timeout)
#endif

#if defined(CONFIG_VIEWALYZER_TRACE_HEAPS)
/* k_heap_alloc routes through k_heap_aligned_alloc; `bytes` is in scope at the
 * trace point in the kernel heap code (same pattern as Zephyr's test format). */
#undef  sys_port_trace_k_heap_init
#define sys_port_trace_k_heap_init(heap) sys_trace_k_heap_init(heap)
#undef  sys_port_trace_k_heap_aligned_alloc_blocking
#define sys_port_trace_k_heap_aligned_alloc_blocking(heap, timeout) sys_trace_k_heap_alloc_blocking(heap)
#undef  sys_port_trace_k_heap_aligned_alloc_exit
#define sys_port_trace_k_heap_aligned_alloc_exit(heap, timeout, ret) sys_trace_k_heap_alloc_exit_impl(heap, (uint32_t)(bytes), ret)
#undef  sys_port_trace_k_heap_free
#define sys_port_trace_k_heap_free(heap) sys_trace_k_heap_free(heap)
#undef  sys_port_trace_k_heap_realloc_exit
#define sys_port_trace_k_heap_realloc_exit(h, ptr, bytes, timeout, ret) sys_trace_k_heap_alloc_exit_impl(h, (uint32_t)(bytes), ret)
#endif

#if defined(CONFIG_VIEWALYZER_TRACE_PM)
#undef  sys_port_trace_pm_system_suspend_enter
#define sys_port_trace_pm_system_suspend_enter(ticks) sys_trace_va_pm_system_suspend_enter(ticks)
#undef  sys_port_trace_pm_system_suspend_exit
#define sys_port_trace_pm_system_suspend_exit(ticks, state) sys_trace_va_pm_system_suspend_exit(ticks, state)
#endif

#ifdef __cplusplus
}
#endif

#endif /* VIEWALYZER_TRACING_USER_WRAP_H */
