/**
 * @file ViewAlyzer.h
 * @brief ViewAlyzer Recorder Firmware - Main API Header
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

#ifndef VIEWALYZER_H
#define VIEWALYZER_H

#include "main.h"
#include "stdint.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C"
{
#endif

// --- Configuration ---
#ifndef VA_ENABLED
#define VA_ENABLED 1
#endif
#ifndef VA_TRACE_FREERTOS
#define VA_TRACE_FREERTOS 1
#endif
#if (VA_TRACE_FREERTOS == 1)
#include "FreeRTOS.h"
#include "task.h"
#else
#define TaskHandle_t uint32_t *
#endif
#define ST_LINK_ITM 1 // <<< SET THIS TO 1 FOR ITM, 0 FOR RTT >>>
#define LOG_PENDSV 0
#define VA_ITM_PORT 1    // Use ITM Stimulus Port 1 if ST_LINK_ITM = 1
#define VA_RTT_CHANNEL 0 // Use RTT Channel 0 if ST_LINK_ITM = 0

#if (ST_LINK_ITM == 0)
#ifndef VA_RTT_BUFFER_SIZE
#define VA_RTT_BUFFER_SIZE 4096u // Bytes reserved for RTT up-buffer when streaming ViewAlyzer frames
#endif
#endif
#define VA_MAX_TASKS 32
#define VA_MAX_TASK_NAME_LEN 16
#define VA_ALLOWED_TO_DISABLE_INTERRUPTS 0 // Set to 1 if you can disable interrupts safely

// --- Binary Event Type Codes ---
#define VA_EVENT_TYPE_MASK 0x7F
#define VA_EVENT_FLAG_START_END 0x80
#define VA_EVENT_TASK_SWITCH 0x01
#define VA_EVENT_ISR 0x02
#define VA_EVENT_TASK_CREATE 0x03
#define VA_EVENT_USER_TRACE 0x04
#define VA_EVENT_TASK_NOTIFY 0x05
#define VA_EVENT_SEMAPHORE 0x06
#define VA_EVENT_MUTEX 0x07
#define VA_EVENT_QUEUE 0x08
#define VA_EVENT_TASK_STACK_USAGE 0x09
#define VA_EVENT_USER_TOGGLE 0x0A
#define VA_EVENT_USER_FUNCTION 0x0B
#define VA_EVENT_MUTEX_CONTENTION 0x0C  // New: tracks mutex blocking relationships
// ... Add more event types ...

// --- Setup Message Codes ---
#define VA_SETUP_TASK_MAP 0x70
#define VA_SETUP_ISR_MAP 0x71
#define VA_SETUP_INFO 0x7F
#define VA_SETUP_USER_TRACE 0x72
#define VA_SETUP_SEMAPHORE_MAP 0x73
#define VA_SETUP_MUTEX_MAP 0x74
#define VA_SETUP_QUEUE_MAP 0x75
#define VA_SETUP_USER_FUNCTION_MAP 0x76
#define VA_SETUP_CONFIG_FLAGS 0x77

    typedef enum
    {
        VA_USER_TYPE_GRAPH = 0,
        VA_USER_TYPE_BAR = 1,
        VA_USER_TYPE_GAUGE = 2,
        VA_USER_TYPE_COUNTER = 3,
        VA_USER_TYPE_TABLE = 4,
        VA_USER_TYPE_HISTOGRAM = 5,
        VA_USER_TYPE_TOGGLE = 6,
        VA_USER_TYPE_TASK = 7,
        VA_USER_TYPE_ISR = 8
    } VA_UserTraceType_t;

    typedef enum
    {
        TOGGLE_LOW,
        TOGGLE_HIGH
    } VA_UserToggleState_t;

    typedef enum
    {
        USER_EVENT_START, // Function entry/start
        USER_EVENT_END    // Function exit/end
    } VA_UserEventState_t;

    typedef enum
    {
        VA_OBJECT_TYPE_QUEUE = 0,           // queueQUEUE_TYPE_BASE
        VA_OBJECT_TYPE_MUTEX = 1,           // queueQUEUE_TYPE_MUTEX  
        VA_OBJECT_TYPE_COUNTING_SEM = 2,    // queueQUEUE_TYPE_COUNTING_SEMAPHORE
        VA_OBJECT_TYPE_BINARY_SEM = 3,      // queueQUEUE_TYPE_BINARY_SEMAPHORE
        VA_OBJECT_TYPE_RECURSIVE_MUTEX = 4  // queueQUEUE_TYPE_RECURSIVE_MUTEX
    } VA_QueueObjectType_t;

// --- Static ISR IDs ---
#define VA_ISR_ID_SYSTICK 1
#define VA_ISR_ID_PENDSV 2
// ... Add more ISR IDs ...

// --- Public Functions ---
#if (VA_ENABLED == 1)
    void VA_Init(uint32_t cpu_freq);
    void VA_LogISRStart(uint8_t isrId);
    void VA_LogISREnd(uint8_t isrId);
    void VA_TaskSwitchedIn(void);
    void VA_TaskSwitchedOut(void);
    void VA_TaskCreated(TaskHandle_t pxCreatedTask);
    void VA_TrackDWTOverflow(void);
    void VA_RegisterUserTrace(uint8_t id, const char *name, VA_UserTraceType_t type);
    void VA_LogUserTrace(uint8_t id, int32_t value);
    void VA_LogUserToggleEvent(uint8_t id, bool state);
    void VA_RegisterUserFunction(uint8_t id, const char *name);
    void VA_LogUserEvent(uint8_t id, bool state);
    bool VA_IsInit(void);
    // TODO:  new user event here
    void VA_LogTaskNotifyGive(TaskHandle_t destTask, uint32_t value);
    void VA_LogTaskNotifyTake(uint32_t value);
    // Unified object tracking API
    void VA_LogQueueObjectCreate(void *queueObject, const char *name);
    void VA_LogQueueObjectCreateWithType(void *queueObject, const char *typeHint);
    void VA_UpdateQueueObjectType(void *queueObject, const char *typeHint);
    void VA_LogQueueObjectGive(void *queueObject, uint32_t timeout);
    void VA_LogQueueObjectTake(void *queueObject, uint32_t timeout);
    void VA_LogQueueObjectBlocking(void *queueObject);
    
    // Legacy API for backward compatibility
    void VA_LogSemaphoreGive(void *semaphore);
    void VA_LogSemaphoreTake(void *semaphore, uint32_t timeout);
    void VA_LogSemaphoreCreate(void *semaphore, const char *name);
    void VA_LogMutexAcquire(void *mutex, uint32_t timeout);
    void VA_LogMutexRelease(void *mutex);
    void VA_LogMutexCreate(void *mutex, const char *name);
    void VA_LogQueueSend(void *queue, uint32_t timeout);
    void VA_LogQueueReceive(void *queue, uint32_t timeout);
    void VA_LogQueueCreate(void *queue, const char *name);
    extern volatile uint32_t notificationValue;

#else
// --- Empty stubs ---
#define VA_Init(cpu_freq) ((void)0)
#define VA_LogISRStart(isrId) ((void)0)
#define VA_LogISREnd(isrId) ((void)0)
#define VA_TaskSwitchedIn() ((void)0)
#define VA_TaskSwitchedOut() ((void)0)
#define VA_TaskCreated(pxCreatedTask) ((void)0)
#define VA_TrackDWTOverflow() ((void)0)
#define VA_RegisterUserTrace(id, name, type) ((void)0)
#define VA_LogUserTrace(id, value) ((void)0)
#define VA_LogUserToggleEvent(id, state) ((void)0)
#define VA_RegisterUserFunction(id, name) ((void)0)
#define VA_LogUserEvent(id, state) ((void)0)
#define VA_LogTaskNotifyGive(destTask, value) ((void)0)
#define VA_LogTaskNotifyTake(value) ((void)0)
#define VA_LogQueueObjectCreate(queueObject, name) ((void)0)
#define VA_LogQueueObjectCreateWithType(queueObject, typeHint) ((void)0)
#define VA_UpdateQueueObjectType(queueObject, typeHint) ((void)0)
#define VA_LogQueueObjectGive(queueObject, timeout) ((void)0)
#define VA_LogQueueObjectTake(queueObject, timeout) ((void)0)
#define VA_LogSemaphoreGive(semaphore) ((void)0)
#define VA_LogSemaphoreTake(semaphore, timeout) ((void)0)
#define VA_LogSemaphoreCreate(semaphore, name) ((void)0)
#define VA_LogMutexAcquire(mutex, timeout) ((void)0)
#define VA_LogMutexRelease(mutex) ((void)0)
#define VA_LogMutexCreate(mutex, name) ((void)0)
#define VA_LogQueueSend(queue, timeout) ((void)0)
#define VA_LogQueueReceive(queue, timeout) ((void)0)
#define VA_LogQueueCreate(queue, name) ((void)0)
bool VA_IsInit(void);

extern volatile uint32_t notificationValue;

// --- Convenience Macros for User Function Timing ---
#if (VA_ENABLED == 1)
#define VA_FUNCTION_ENTRY(id) VA_LogUserEvent(id, USER_EVENT_IN)
#define VA_FUNCTION_EXIT(id) VA_LogUserEvent(id, USER_EVENT_OUT)
#else
#define VA_FUNCTION_ENTRY(id) ((void)0)
#define VA_FUNCTION_EXIT(id) ((void)0)
#endif
#endif // VA_ENABLED

    // Ensure FreeRTOSConfig.h uses these via trace macros
    /* Example:
     extern void VA_TaskSwitchedIn( void );
     extern void VA_TaskSwitchedOut( void );
     extern void VA_TaskCreated( TaskHandle_t pxNewTCB );
     #define traceTASK_SWITCHED_IN()         VA_TaskSwitchedIn()
     #define traceTASK_SWITCHED_OUT()        VA_TaskSwitchedOut()
     #define traceTASK_CREATE( pxNewTCB )    VA_TaskCreated( pxNewTCB )
    */

#ifdef __cplusplus
}
#endif

#endif // VIEWALYZER_H