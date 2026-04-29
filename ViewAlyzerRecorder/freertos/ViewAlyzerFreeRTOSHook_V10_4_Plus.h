/**
 * @file ViewAlyzerFreeRTOS_V10_4_Plus.h
 * @brief ViewAlyzer Recorder Firmware - FreeRTOS v10.4+ Configuration
 * 
 * Copyright (c) 2025 Free Radical Labs
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction for non-commercial purposes, including 
 * without limitation the rights to use, copy, modify, merge, publish, distribute, 
 * and sublicense copies of the Software, subject to the conditions in the LICENSE file.
 * 
 * For commercial licensing or questions about usage restrictions, contact:
 * support@viewalyzer.net
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#ifndef ViewAlyzer_CONFIG_V10_4_PLUS_H
#define ViewAlyzer_CONFIG_V10_4_PLUS_H
#if defined(VA_ENABLED) && (VA_ENABLED == 1) && \
    ((defined(VA_TRACE_FREERTOS) && (VA_TRACE_FREERTOS == 1)) || \
     (defined(VA_RTOS_SELECT) && (VA_RTOS_SELECT == 1)))
// ViewAlyzer Configuration for FreeRTOS V10.4.0 and later
// This version supports the new trace macro signatures with notification indices

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for ViewAlyzer core functions used by trace macros.
// (Cannot #include "ViewAlyzer.h" here — it pulls in main.h which creates
//  a circular dependency through FreeRTOS.h → FreeRTOSConfig.h → this file.)
void va_taskswitchedin(void *taskHandle);
void va_taskswitchedout(void *taskHandle);
void va_taskcreated(void *taskHandle, const char *name);
void va_logtasknotifygive(void *srcHandle, void *destHandle, uint32_t value);
void va_logtasknotifytake(void *taskHandle, uint32_t value);
void va_logQueueObjectCreateWithType(void *queueObject, const char *typeHint);
void va_updateQueueObjectType(void *queueObject, const char *typeHint);
void va_logQueueObjectGive(void *queueObject, uint32_t timeout);
void va_logQueueObjectTake(void *queueObject, uint32_t timeout);
void va_logQueueObjectBlocking(void *queueObject);

#ifdef __cplusplus
}
#endif

// Suggested FreeRTOS Configs
#define configRECORD_STACK_HIGH_ADDRESS 1

// --- FreeRTOS Trace Macro Definitions ---
#define traceTASK_SWITCHED_IN() va_taskswitchedin((void *)pxCurrentTCB)
#define traceTASK_SWITCHED_OUT() va_taskswitchedout((void *)pxCurrentTCB)

// Enhanced task creation macro that captures TCB information
extern volatile void *g_task_pxStack;
extern volatile void *g_task_pxEndOfStack;
extern volatile uint32_t g_task_uxPriority;
extern volatile uint32_t g_task_uxBasePriority;
extern volatile uint32_t g_task_ulStackDepth;

#if (configRECORD_STACK_HIGH_ADDRESS == 1)
#define CALCULATE_STACK_DEPTH(pxTCB) \
    g_task_ulStackDepth = (uint32_t)((pxTCB)->pxEndOfStack - (pxTCB)->pxStack)
#else
#define CALCULATE_STACK_DEPTH(pxTCB) \
    g_task_ulStackDepth = 0
#endif

#define traceTASK_CREATE(pxNewTCB)                                   \
    do                                                               \
    {                                                                \
        g_task_pxStack = (pxNewTCB)->pxStack;                        \
        g_task_uxPriority = (pxNewTCB)->uxPriority;                  \
        g_task_pxEndOfStack = NULL;                                  \
        g_task_uxBasePriority = (pxNewTCB)->uxPriority;              \
        CALCULATE_STACK_DEPTH(pxNewTCB);                             \
        va_taskcreated((void *)(pxNewTCB), pcTaskGetName(pxNewTCB));  \
    } while (0)

// Task notification tracing - real FreeRTOS macros
// Updated for FreeRTOS V10.4.0+ which added uxIndexToNotify/uxIndexToWait parameters

extern volatile uint32_t notificationValue;
#define traceTASK_NOTIFY(uxIndexToNotify) (notificationValue = ulValue, va_logtasknotifygive((void *)pxCurrentTCB, (void *)pxTCB, ulValue))
#define traceTASK_NOTIFY_FROM_ISR(uxIndexToNotify) (notificationValue = ulValue, va_logtasknotifygive(NULL, (void *)pxTCB, ulValue))
#define traceTASK_NOTIFY_GIVE_FROM_ISR(uxIndexToNotify) (notificationValue = pxTCB->ulNotifiedValue[(uxIndexToNotify)], va_logtasknotifygive(NULL, (void *)pxTCB, pxTCB->ulNotifiedValue[(uxIndexToNotify)]))
#define traceTASK_NOTIFY_TAKE(uxIndexToWait) va_logtasknotifytake((void *)pxCurrentTCB, pxCurrentTCB->ulNotifiedValue[(uxIndexToWait)])
#define traceTASK_NOTIFY_TAKE_BLOCK(uxIndexToWait) va_logtasknotifytake((void *)pxCurrentTCB, pxCurrentTCB->ulNotifiedValue[(uxIndexToWait)])
#define traceTASK_NOTIFY_WAIT(uxIndexToWait) va_logtasknotifytake((void *)pxCurrentTCB, pxCurrentTCB->ulNotifiedValue[(uxIndexToWait)])
#define traceTASK_NOTIFY_WAIT_BLOCK(uxIndexToWait) va_logtasknotifytake((void *)pxCurrentTCB, pxCurrentTCB->ulNotifiedValue[(uxIndexToWait)])

// Queue tracing - unified approach using generic queue creation macro
// FreeRTOS uses queues as the underlying mechanism for mutexes, semaphores, and queues
// We use a single hook that inspects the ucQueueType field to determine object type
// Note: For mutexes, traceCREATE_MUTEX() is called AFTER traceQUEUE_CREATE(), so we need
// to avoid double registration
#define traceQUEUE_CREATE(pxNewQueue) \
    va_logQueueObjectCreateWithType((pxNewQueue), "Queue")

#define traceQUEUE_SEND(pxQueue) \
    va_logQueueObjectGive((pxQueue), xTicksToWait)

#define traceQUEUE_SEND_FROM_ISR(pxQueue) \
    va_logQueueObjectGive((pxQueue), 0)

#define traceQUEUE_RECEIVE(pxQueue) \
    va_logQueueObjectTake((pxQueue), xTicksToWait)

#define traceQUEUE_RECEIVE_FROM_ISR(pxQueue) \
    va_logQueueObjectTake((pxQueue), 0)

// Mutex tracing - traceCREATE_MUTEX is called AFTER traceQUEUE_CREATE in FreeRTOS
// We need to update the existing entry to reflect the correct type
#define traceCREATE_MUTEX(pxNewMutex) \
    va_updateQueueObjectType((pxNewMutex), "Mutex")

#define traceGIVE_MUTEX_RECURSIVE(pxMutex) \
    va_logQueueObjectGive((pxMutex), 0)

#define traceTAKE_MUTEX_RECURSIVE(pxMutex) \
    va_logQueueObjectTake((pxMutex), xTicksToWait)

// Additional mutex operations
#define traceGIVE_MUTEX_RECURSIVE_FAILED(pxMutex) \
    va_logQueueObjectTake((pxMutex), 0)

#define traceTAKE_MUTEX_RECURSIVE_FAILED(pxMutex) \
    va_logQueueObjectTake((pxMutex), xTicksToWait)

// Standard mutex operations (non-recursive)
#define traceCREATE_MUTEX_FAILED() \
    ((void)0) 

#define traceGIVE_MUTEX(pxMutex) \
    va_logQueueObjectGive((pxMutex), 0)

#define traceTAKE_MUTEX(pxMutex) \
    va_logQueueObjectTake((pxMutex), xTicksToWait)

// Blocking trace - called BEFORE task blocks waiting for mutex/semaphore/queue
// This is where we can detect contention since the mutex is still held by another task
#define traceBLOCKING_ON_QUEUE_RECEIVE(pxQueue) \
    va_logQueueObjectBlocking((pxQueue))

// Semaphore operations - these are also queues but we can add specific tracing
// Binary semaphores are created through xSemaphoreCreateBinary() which calls xQueueCreateBinary()
// Counting semaphores are created through xSemaphoreCreateCounting() which calls xQueueCreateCountingSemaphore()
// Both eventually call xQueueGenericCreate() which triggers traceQUEUE_CREATE()

// TODO : Future improvement
#define traceCREATE_COUNTING_SEMAPHORE() \
    ((void)0)  // This gets called after traceQUEUE_CREATE, so we can use it to update the name

#define traceCREATE_BINARY_SEMAPHORE() \
    ((void)0)  // This gets called after traceQUEUE_CREATE, so we can use it to update the name

#endif
#endif // ViewAlyzer_CONFIG_V10_4_PLUS_H