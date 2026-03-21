/**
 * @file VA_Adapter_Zephyr.c
 * @brief ViewAlyzer Zephyr Adapter — RTOS-specific logic
 *
 * Implements the adapter interface for Zephyr RTOS:
 *   - Queue-object type detection (Zephyr doesn't use FreeRTOS queue hacks)
 *   - Stack-usage calculation via k_thread_stack_space_get
 *   - Mutex contention (Zephyr k_mutex owner field)
 *
 * Also overrides Zephyr's CONFIG_TRACING_USER weak callbacks to emit
 * native ViewAlyzer task-switch events through the core engine.
 *
 * This file is compiled ONLY when VA_RTOS_SELECT == VA_RTOS_ZEPHYR.
 *
 * Copyright (c) 2025 Free Radical Labs
 */

#include "ViewAlyzer.h"

#if (VA_ENABLED == 1) && (VA_RTOS_SELECT == VA_RTOS_ZEPHYR)

#include "VA_Internal.h"
#include <zephyr/kernel.h>
#include <string.h>

/* ================================================================
 *  Adapter interface — queue object type
 * ================================================================ */

VA_QueueObjectType_t va_adapter_get_queue_object_type(void *handle)
{
    /*
     * Zephyr doesn't have a single "queue" object that doubles as
     * mutex/semaphore like FreeRTOS does.  When the user registers
     * objects via va_logQueueObjectCreateWithType(), the typeHint
     * string already tells us what it is.  This fallback returns
     * QUEUE for anything we can't identify.
     *
     * Future: inspect the Zephyr object type registry if available.
     */
    (void)handle;
    return VA_OBJECT_TYPE_QUEUE;
}

/* ================================================================
 *  Adapter interface — stack usage
 * ================================================================ */

uint32_t va_adapter_calculate_stack_usage(void *taskHandle)
{
    size_t unused = 0;
    int ret = k_thread_stack_space_get((k_tid_t)taskHandle, &unused);
    if (ret == 0)
    {
        int idx = _va_find_task_index(taskHandle);
        if (idx >= 0 && taskMap[idx].ulStackDepth > 0)
        {
            /* ulStackDepth is in bytes for Zephyr (set during registration) */
            uint32_t used = taskMap[idx].ulStackDepth - (uint32_t)unused;
            return used;
        }
        return (uint32_t)unused;
    }
    return 0;
}

uint32_t va_adapter_get_total_stack_size(void *taskHandle)
{
    int idx = _va_find_task_index(taskHandle);
    if (idx >= 0)
    {
        return taskMap[idx].ulStackDepth;
    }
    return 0;
}

/* ================================================================
 *  Adapter interface — mutex contention
 * ================================================================ */

void va_adapter_check_mutex_contention(void *queueObject, uint8_t queue_va_id)
{
    /*
     * Zephyr k_mutex has an owner field.  We could inspect it here,
     * but the k_mutex struct layout is version-dependent.  For now
     * this is a placeholder — contention detection can be added once
     * the Zephyr object-type registration is fleshed out.
     */
    (void)queueObject;
    (void)queue_va_id;
}

/* ================================================================
 *  Thread → ViewAlyzer ID registration helper
 * ================================================================ */

static void va_zephyr_register_thread(k_tid_t tid)
{
    /* Already mapped? */
    uint8_t existing = _va_find_task_id((void *)tid);
    if (existing != 0)
        return;

    const char *name = k_thread_name_get(tid);
    if (name == NULL || name[0] == '\0')
        name = "thread";

    /* Populate the creation globals so _va_assign_task_id stores them */
    g_task_pxStack       = NULL;
    g_task_pxEndOfStack  = NULL;
    g_task_uxPriority    = (uint32_t)k_thread_priority_get(tid);
    g_task_uxBasePriority = g_task_uxPriority;
    g_task_ulStackDepth  = tid->stack_info.size;

    va_taskcreated((void *)tid, name);
}

/* ── Public helper ──────────────────────────────────────────────── */

static void va_zephyr_foreach_cb(const struct k_thread *thread, void *user_data)
{
    ARG_UNUSED(user_data);
    va_zephyr_register_thread((k_tid_t)thread);
}

void VA_Zephyr_RegisterExistingThreads(void)
{
    k_thread_foreach(va_zephyr_foreach_cb, NULL);
}

/* ================================================================
 *  Zephyr tracing weak-function overrides
 * ================================================================ */

void sys_trace_thread_create_user(struct k_thread *thread)
{
    if (!va_isnit())
        return;
    va_zephyr_register_thread((k_tid_t)thread);
}

void sys_trace_thread_switched_in_user(void)
{
    if (!va_isnit())
        return;
    k_tid_t cur = k_current_get();
    if (_va_find_task_id((void *)cur) == 0)
        va_zephyr_register_thread(cur);
    va_taskswitchedin((void *)cur);
}

void sys_trace_thread_switched_out_user(void)
{
    if (!va_isnit())
        return;
    k_tid_t cur = k_current_get();
    va_taskswitchedout((void *)cur);
}

void sys_trace_isr_enter_user(void)
{
    if (!va_isnit())
        return;
    uint32_t exception = __get_IPSR() & 0xFFu;
    VA_LogISRStart((uint8_t)exception);
}

void sys_trace_isr_exit_user(void)
{
    if (!va_isnit())
        return;
    uint32_t exception = __get_IPSR() & 0xFFu;
    VA_LogISREnd((uint8_t)exception);
}

/* ================================================================
 *  Mutex tracing overrides
 * ================================================================ */

void sys_trace_k_mutex_init_user(struct k_mutex *mutex, int ret)
{
    if (!va_isnit() || ret != 0)
        return;
    va_logQueueObjectCreateWithType((void *)mutex, "Mutex");
}

void sys_trace_k_mutex_lock_enter_user(struct k_mutex *mutex, k_timeout_t timeout)
{
    (void)mutex;
    (void)timeout;
}

void sys_trace_k_mutex_lock_blocking_user(struct k_mutex *mutex, k_timeout_t timeout)
{
    if (!va_isnit())
        return;
    va_logQueueObjectBlocking((void *)mutex);
}

void sys_trace_k_mutex_lock_exit_user(struct k_mutex *mutex, k_timeout_t timeout, int ret)
{
    if (!va_isnit() || ret != 0)
        return;
    va_logQueueObjectTake((void *)mutex, (uint32_t)k_ticks_to_ms_floor64(timeout.ticks));
}

void sys_trace_k_mutex_unlock_enter_user(struct k_mutex *mutex)
{
    (void)mutex;
}

void sys_trace_k_mutex_unlock_exit_user(struct k_mutex *mutex, int ret)
{
    if (!va_isnit() || ret != 0)
        return;
    va_logQueueObjectGive((void *)mutex, 0);
}

/* ================================================================
 *  Semaphore tracing overrides
 * ================================================================ */

void sys_trace_k_sem_init_user(struct k_sem *sem, int ret)
{
    if (!va_isnit() || ret != 0)
        return;
    va_logQueueObjectCreateWithType((void *)sem, "Semaphore");
}

void sys_trace_k_sem_give_enter_user(struct k_sem *sem)
{
    if (!va_isnit())
        return;
    va_logQueueObjectGive((void *)sem, 0);
}

void sys_trace_k_sem_take_enter_user(struct k_sem *sem, k_timeout_t timeout)
{
    (void)sem;
    (void)timeout;
}

void sys_trace_k_sem_take_blocking_user(struct k_sem *sem, k_timeout_t timeout)
{
    if (!va_isnit())
        return;
    va_logQueueObjectBlocking((void *)sem);
}

void sys_trace_k_sem_take_exit_user(struct k_sem *sem, k_timeout_t timeout, int ret)
{
    if (!va_isnit() || ret != 0)
        return;
    va_logQueueObjectTake((void *)sem, (uint32_t)k_ticks_to_ms_floor64(timeout.ticks));
}

/* ================================================================
 *  Message queue tracing overrides
 * ================================================================ */

void sys_trace_k_msgq_init_user(struct k_msgq *msgq)
{
    if (!va_isnit())
        return;
    va_logQueueObjectCreateWithType((void *)msgq, "Queue");
}

void sys_trace_k_msgq_put_enter_user(struct k_msgq *msgq, k_timeout_t timeout)
{
    (void)msgq;
    (void)timeout;
}

void sys_trace_k_msgq_put_blocking_user(struct k_msgq *msgq, k_timeout_t timeout)
{
    if (!va_isnit())
        return;
    va_logQueueObjectBlocking((void *)msgq);
}

void sys_trace_k_msgq_put_exit_user(struct k_msgq *msgq, k_timeout_t timeout, int ret)
{
    if (!va_isnit() || ret != 0)
        return;
    va_logQueueObjectGive((void *)msgq, (uint32_t)k_ticks_to_ms_floor64(timeout.ticks));
}

void sys_trace_k_msgq_get_enter_user(struct k_msgq *msgq, k_timeout_t timeout)
{
    (void)msgq;
    (void)timeout;
}

void sys_trace_k_msgq_get_blocking_user(struct k_msgq *msgq, k_timeout_t timeout)
{
    if (!va_isnit())
        return;
    va_logQueueObjectBlocking((void *)msgq);
}

void sys_trace_k_msgq_get_exit_user(struct k_msgq *msgq, k_timeout_t timeout, int ret)
{
    if (!va_isnit() || ret != 0)
        return;
    va_logQueueObjectTake((void *)msgq, (uint32_t)k_ticks_to_ms_floor64(timeout.ticks));
}

#endif /* VA_ENABLED && VA_RTOS_ZEPHYR */
