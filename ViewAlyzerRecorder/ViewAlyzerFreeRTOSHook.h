/**
 * @file ViewAlyzerConfig.h
 * @brief ViewAlyzer Recorder Firmware - Configuration Header
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

#ifndef ViewAlyzer_CONFIG_H
#define ViewAlyzer_CONFIG_H
#if defined(VA_ENABLED) && (VA_ENABLED == 1) && (VA_TRACE_FREERTOS == 1)
// Suggesteed FreeRTOS Configs
#define configRECORD_STACK_HIGH_ADDRESS 1

// --- FreeRTOS Trace Macro Definitions ---
// These macros hook into FreeRTOS to capture events using only REAL FreeRTOS trace macros

// Task tracing - these are the actual FreeRTOS macros
#define traceTASK_SWITCHED_IN() VA_TaskSwitchedIn()
#define traceTASK_SWITCHED_OUT() VA_TaskSwitchedOut()

// Enhanced task creation macro that captures TCB information
// This hack extracts information from the TCB before calling our profiler
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
        /* Capture basic task information from TCB */                \
        g_task_pxStack = (pxNewTCB)->pxStack;                        \
        g_task_uxPriority = (pxNewTCB)->uxPriority;                  \
        /* Set defaults */                                           \
        g_task_pxEndOfStack = NULL;                                  \
        g_task_uxBasePriority = (pxNewTCB)->uxPriority;              \
        CALCULATE_STACK_DEPTH(pxNewTCB);                             \
        /* Try to capture additional fields that may be available */ \
        /* These will be safely ignored if the fields don't exist */ \
        /* Call our profiler function */                             \
        VA_TaskCreated(pxNewTCB);                                    \
    } while (0)

// Task notification tracing - real FreeRTOS macros

extern volatile uint32_t notificationValue;
#define traceTASK_NOTIFY() (notificationValue = ulValue, VA_LogTaskNotifyGive(pxTCB, ulValue))
#define traceTASK_NOTIFY_FROM_ISR() (notificationValue = ulValue, VA_LogTaskNotifyGive(pxTCB, ulValue))
#define traceTASK_NOTIFY_GIVE_FROM_ISR() (notificationValue = pxTCB->ulNotifiedValue, VA_LogTaskNotifyGive(pxTCB, pxTCB->ulNotifiedValue))
#define traceTASK_NOTIFY_TAKE() VA_LogTaskNotifyTake(pxCurrentTCB->ulNotifiedValue)

// Queue tracing - unified approach using generic queue creation macro
// FreeRTOS uses queues as the underlying mechanism for mutexes, semaphores, and queues
// We use a single hook that inspects the ucQueueType field to determine object type
// Note: For mutexes, traceCREATE_MUTEX() is called AFTER traceQUEUE_CREATE(), so we need
// to avoid double registration
#define traceQUEUE_CREATE(pxNewQueue) \
    VA_LogQueueObjectCreateWithType((pxNewQueue), "Queue")

#define traceQUEUE_SEND(pxQueue) \
    VA_LogQueueObjectGive((pxQueue), xTicksToWait)

#define traceQUEUE_SEND_FROM_ISR(pxQueue) \
    VA_LogQueueObjectGive((pxQueue), 0)

#define traceQUEUE_RECEIVE(pxQueue) \
    VA_LogQueueObjectTake((pxQueue), xTicksToWait)

#define traceQUEUE_RECEIVE_FROM_ISR(pxQueue) \
    VA_LogQueueObjectTake((pxQueue), 0)

// Mutex tracing - traceCREATE_MUTEX is called AFTER traceQUEUE_CREATE in FreeRTOS
// We need to update the existing entry to reflect the correct type
#define traceCREATE_MUTEX(pxNewMutex) \
    VA_UpdateQueueObjectType((pxNewMutex), "Mutex")

#define traceGIVE_MUTEX_RECURSIVE(pxMutex) \
    VA_LogQueueObjectGive((pxMutex), 0)

#define traceTAKE_MUTEX_RECURSIVE(pxMutex) \
    VA_LogQueueObjectTake((pxMutex), xTicksToWait)

// Additional mutex operations
#define traceGIVE_MUTEX_RECURSIVE_FAILED(pxMutex) \
    VA_LogQueueObjectTake((pxMutex), 0)  // Log as failed take

#define traceTAKE_MUTEX_RECURSIVE_FAILED(pxMutex) \
    VA_LogQueueObjectTake((pxMutex), xTicksToWait)  // Log the attempt

// Standard mutex operations (non-recursive)
#define traceCREATE_MUTEX_FAILED() \
    ((void)0)  // Could log creation failure if needed

#define traceGIVE_MUTEX(pxMutex) \
    VA_LogQueueObjectGive((pxMutex), 0)

#define traceTAKE_MUTEX(pxMutex) \
    VA_LogQueueObjectTake((pxMutex), xTicksToWait)

// Blocking trace - called BEFORE task blocks waiting for mutex/semaphore/queue
// This is where we can detect contention since the mutex is still held by another task
#define traceBLOCKING_ON_QUEUE_RECEIVE(pxQueue) \
    VA_LogQueueObjectBlocking((pxQueue))

// Semaphore operations - these are also queues but we can add specific tracing
// Binary semaphores are created through xSemaphoreCreateBinary() which calls xQueueCreateBinary()
// Counting semaphores are created through xSemaphoreCreateCounting() which calls xQueueCreateCountingSemaphore()
// Both eventually call xQueueGenericCreate() which triggers traceQUEUE_CREATE()

// However, we can add specific semaphore creation hooks if FreeRTOS provides them
#define traceCREATE_COUNTING_SEMAPHORE() \
    ((void)0)  // This gets called after traceQUEUE_CREATE, so we can use it to update the name

#define traceCREATE_BINARY_SEMAPHORE() \
    ((void)0)  // This gets called after traceQUEUE_CREATE, so we can use it to update the name

#endif
#endif // ViewAlyzer_CONFIG_H