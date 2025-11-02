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

/**************************************************************
                     USER CONFIGURATION
**************************************************************/
#ifndef VA_ENABLED
#define VA_ENABLED 1         // Can be defined in your build system to 0 to disable ViewAlyzer
#endif

#ifndef VA_TRACE_FREERTOS
#define VA_TRACE_FREERTOS 1  // Can be defined in your build system to 0 to disable FreeRTOS tracing
#endif

#define ST_LINK_ITM 1u
#define JLINK_RTT   2u

#define VA_TRANSPORT ST_LINK_ITM  // Select active transport backend
#define LOG_PENDSV 0             // Experimental, unused

#define VA_ITM_PORT    1         // ITM stimulus port where logs are sent when using ST-LINK transport
#define VA_RTT_CHANNEL 0        // RTT channel when using J-LINK RTT transport

#define VA_MAX_TASKS         32
#define VA_MAX_TASK_NAME_LEN 16
#define VA_ALLOWED_TO_DISABLE_INTERRUPTS 1 // Set to 1 to allow critical sections

// If using J-LINK RTT transport, configure RTT here by setting VA_CONFIGURE_RTT to 1
// otherwise set to 0 to skip RTT configuration and user is expected to do it elsewhere
#define VA_CONFIGURE_RTT       1                        //set to 0 to use your own init
#define VA_RTT_BUFFER_SIZE 4096u                       // Bytes reserved for RTT up-buffer
#define VA_RTT_MODE SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL // RTT buffering mode

// Convenience Macros for User Function Timing
#if (VA_ENABLED == 1)
#define VA_FUNCTION_ENTRY(id) VA_LogUserEvent(id, USER_EVENT_IN)
#define VA_FUNCTION_EXIT(id) VA_LogUserEvent(id, USER_EVENT_OUT)
#else
#define VA_FUNCTION_ENTRY(id) ((void)0)
#define VA_FUNCTION_EXIT(id) ((void)0)
#endif
#endif // VA_ENABLED

/**************************************************************
                        END USER CONFIGURATION
**************************************************************/


/**************************************************************
                     DO NOT EDIT BELOW THIS LINE
**************************************************************/

// --- Derived Configuration ---
#define VA_TRANSPORT_IS_ST_LINK ((VA_TRANSPORT) == ST_LINK_ITM)
#define VA_TRANSPORT_IS_JLINK ((VA_TRANSPORT) == JLINK_RTT)

#if (VA_TRACE_FREERTOS == 1)
#include "FreeRTOS.h"
#include "task.h"
#else
#define TaskHandle_t uint32_t *
#endif

// --- Binary Event Type Codes ---
#define VA_EVENT_TYPE_MASK        0x7F
#define VA_EVENT_FLAG_START_END   0x80
#define VA_EVENT_TASK_SWITCH      0x01
#define VA_EVENT_ISR              0x02
#define VA_EVENT_TASK_CREATE      0x03
#define VA_EVENT_USER_TRACE       0x04
#define VA_EVENT_TASK_NOTIFY      0x05
#define VA_EVENT_SEMAPHORE        0x06
#define VA_EVENT_MUTEX            0x07
#define VA_EVENT_QUEUE            0x08
#define VA_EVENT_TASK_STACK_USAGE 0x09
#define VA_EVENT_USER_TOGGLE      0x0A
#define VA_EVENT_USER_FUNCTION    0x0B
#define VA_EVENT_MUTEX_CONTENTION 0x0C


// --- Setup Message Codes ---
#define VA_SETUP_TASK_MAP          0x70
#define VA_SETUP_ISR_MAP           0x71
#define VA_SETUP_INFO              0x7F
#define VA_SETUP_USER_TRACE        0x72
#define VA_SETUP_SEMAPHORE_MAP     0x73
#define VA_SETUP_MUTEX_MAP         0x74
#define VA_SETUP_QUEUE_MAP         0x75
#define VA_SETUP_USER_FUNCTION_MAP 0x76
#define VA_SETUP_CONFIG_FLAGS      0x77

    typedef enum
    {
        VA_USER_TYPE_GRAPH     = 0,
        VA_USER_TYPE_BAR       = 1,
        VA_USER_TYPE_GAUGE     = 2,
        VA_USER_TYPE_COUNTER   = 3,
        VA_USER_TYPE_TABLE     = 4,
        VA_USER_TYPE_HISTOGRAM = 5,
        VA_USER_TYPE_TOGGLE    = 6,
        VA_USER_TYPE_TASK      = 7,
        VA_USER_TYPE_ISR       = 8
    } VA_UserTraceType_t;

    typedef enum
    {
        TOGGLE_LOW,
        TOGGLE_HIGH
    } VA_UserToggleState_t;

    typedef enum
    {
        USER_EVENT_START,
        USER_EVENT_END
    } VA_UserEventState_t;

    typedef enum
    {
        VA_OBJECT_TYPE_QUEUE           = 0,
        VA_OBJECT_TYPE_MUTEX           = 1,
        VA_OBJECT_TYPE_COUNTING_SEM    = 2,
        VA_OBJECT_TYPE_BINARY_SEM      = 3,
        VA_OBJECT_TYPE_RECURSIVE_MUTEX = 4
    } VA_QueueObjectType_t;

// --- Static ISR IDs ---
#define VA_ISR_ID_SYSTICK 1
#define VA_ISR_ID_PENDSV 2
// ... Add more ISR IDs ...

// --- Public Functions ---
#if (VA_ENABLED == 1)
    // user API
    void VA_Init(uint32_t cpu_freq);
    void VA_RegisterUserTrace(uint8_t id, const char *name, VA_UserTraceType_t type);
    void VA_RegisterUserFunction(uint8_t id, const char *name);
    void VA_LogISRStart(uint8_t isrId);
    void VA_LogISREnd(uint8_t isrId);
    void VA_LogTrace(uint8_t id, int32_t value);
    void VA_LogToggle(uint8_t id, bool state);
    void VA_LogUserEvent(uint8_t id, bool state);

    // internal FreeRTOS hooks
    void va_taskswitchedin(void);
    void va_taskswitchedout(void);
    void va_taskcreated(TaskHandle_t pxCreatedTask);
    bool va_isnit(void);
    
    void va_logtasknotifygive(TaskHandle_t destTask, uint32_t value);
    void va_logtasknotifytake(uint32_t value);
    // Unified object tracking API
    void va_logQueueObjectCreate(void *queueObject, const char *name);
    void va_logQueueObjectCreateWithType(void *queueObject, const char *typeHint);
    void va_updateQueueObjectType(void *queueObject, const char *typeHint);
    void va_logQueueObjectGive(void *queueObject, uint32_t timeout);
    void va_logQueueObjectTake(void *queueObject, uint32_t timeout);
    void va_logQueueObjectBlocking(void *queueObject);

    extern volatile uint32_t notificationValue;

#else
// --- Empty stubs ---
#define VA_Init(cpu_freq) ((void)0)
#define VA_RegisterUserTrace(id, name, type) ((void)0)
#define VA_RegisterUserFunction(id, name) ((void)0)
#define VA_LogISRStart(isrId) ((void)0)
#define VA_LogISREnd(isrId) ((void)0)
#define VA_LogTrace(id, value) ((void)0)
#define VA_LogToggle(id, state) ((void)0)
#define VA_LogUserEvent(id, state) ((void)0)

#define va_taskswitchedin() ((void)0)
#define va_taskswitchedout() ((void)0)
#define va_taskcreated(pxCreatedTask) ((void)0)
#define va_logtasknotifygive(destTask, value) ((void)0)
#define va_logtasknotifytake(value) ((void)0)
#define va_logQueueObjectCreate(queueObject, name) ((void)0)
#define va_logQueueObjectCreateWithType(queueObject, typeHint) ((void)0)
#define va_updateQueueObjectType(queueObject, typeHint) ((void)0)
#define va_logQueueObjectGive(queueObject, timeout) ((void)0)
#define va_logQueueObjectTake(queueObject, timeout) ((void)0)

bool va_isnit(void);

extern volatile uint32_t notificationValue;

#ifdef __cplusplus
}
#endif

#endif // VIEWALYZER_H