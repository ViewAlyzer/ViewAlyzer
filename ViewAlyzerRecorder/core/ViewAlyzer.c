/**
 * @file ViewAlyzer.c
 * @brief ViewAlyzer Recorder Firmware - RTOS-Agnostic Core Engine
 *
 * This file contains the transport layer, timestamp engine, packet emission,
 * and generic task/object map management.  All RTOS-specific logic (stack
 * introspection, queue-type detection, mutex-holder queries) lives in the
 * corresponding adapter file (VA_Adapter_FreeRTOS.c, VA_Adapter_Zephyr.c, …).
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

#include "ViewAlyzer.h"

#ifdef __cplusplus
extern "C"
{
#endif
#if (VA_ENABLED == 1)

#include "VA_Internal.h"
#include <string.h>
#include <stdio.h>

// Include RTT header only if needed
#if VA_TRANSPORT_IS_JLINK
#include "SEGGER_RTT.h"
#ifndef VA_RTT_MODE
#define VA_RTT_MODE SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL
#endif
#if VA_RTT_BUFFER_SIZE > 0
    static uint8_t s_va_rtt_up_buffer[VA_RTT_BUFFER_SIZE];
#endif
#endif

#if VA_TRANSPORT_IS_CUSTOM
#include "viewalyzer_cobs.h"
    static VA_TransportSendFn s_user_send_fn = NULL;
#endif

#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8M_BASE__)
#define DWT_ENABLED 1
    static volatile uint32_t g_dwt_overflow_count = 0;
#else
#define DWT_ENABLED 0
#warning "ViewAlyzer requires DWT Cycle Counter (Cortex-M3/M4/M7). ViewAlyzer functions disabled."
#define DWT_NOT_AVAILABLE
#endif

#ifndef DWT_NOT_AVAILABLE // Only compile the rest if DWT is enabled

    /* ── Shared global state (exposed via VA_Internal.h) ────────── */
    volatile bool VA_IS_INIT = false;

    // --- Stream Sync Marker ---
    static const uint8_t VA_SYNC_MARKER[] = {0x56, 0x41, 0x5A, 0x01, 0x53, 0x59, 0x4E, 0x43, 0x30, 0x31, 0xAA, 0x55};

    // --- Task ID Mapping (RTOS-agnostic) ---
    VA_TaskMapEntry_t taskMap[VA_MAX_TASKS];
    uint8_t next_task_id = 1;
    static uint32_t _va_cpu_freq = 0;

    volatile uint32_t notificationValue = 0;

    // Global variables to store task information during creation
    volatile void *g_task_pxStack = NULL;
    volatile void *g_task_pxEndOfStack = NULL;
    volatile uint32_t g_task_uxPriority = 0;
    volatile uint32_t g_task_uxBasePriority = 0;
    volatile uint32_t g_task_ulStackDepth = 0;

    // User Function Tracking (Independent of RTOS)
    typedef struct
    {
        uint8_t id;
        char name[VA_MAX_TASK_NAME_LEN];
        bool active;
    } VA_UserFunctionMapEntry_t;
    static VA_UserFunctionMapEntry_t userFunctionMap[VA_MAX_USER_FUNCTIONS];

#if VA_HAS_RTOS
    // --- Queue / sync-object map (RTOS-agnostic storage, adapter determines type) ---
    VA_QueueObjectMapEntry_t queueObjectMap[VA_MAX_SYNC_OBJECTS];
    uint8_t next_queue_object_id = 1;
#endif

/* ================================================================
 *  Transport layer
 * ================================================================ */

#if VA_TRANSPORT_IS_ST_LINK
#define ITM_WaitReady(port) while (ITM->PORT[port].u32 == 0)

static inline void ITM_SendU32(uint8_t port, uint32_t value)
{
    ITM_WaitReady(port);
    ITM->PORT[port].u32 = value;
}
static inline void ITM_SendU16(uint8_t port, uint16_t value)
{
    ITM_WaitReady(port);
    ITM->PORT[port].u16 = value;
}
static inline void ITM_SendU8(uint8_t port, uint8_t value)
{
    ITM_WaitReady(port);
    ITM->PORT[port].u8 = value;
}
static void _va_send_bytes(const uint8_t *data, uint32_t length)
{
    if (!VA_IS_INIT)
        return;
    uint32_t i = 0;
    while (length >= 4)
    {
        uint32_t word = ((uint32_t)data[i + 3] << 24) |
                        ((uint32_t)data[i + 2] << 16) |
                        ((uint32_t)data[i + 1] << 8) |
                        ((uint32_t)data[i + 0] << 0);
        ITM_SendU32(VA_ITM_PORT, word);
        i += 4;
        length -= 4;
    }
    while (length > 0)
    {
        ITM_SendU8(VA_ITM_PORT, data[i]);
        i++;
        length--;
    }
}

#elif VA_TRANSPORT_IS_JLINK
static void _va_send_bytes(const uint8_t *data, uint32_t length)
{
    if (!VA_IS_INIT)
        return;
    SEGGER_RTT_Write(VA_RTT_CHANNEL, data, length);
}

#elif VA_TRANSPORT_IS_CUSTOM
static void _va_send_bytes(const uint8_t *data, uint32_t length)
{
    if (!VA_IS_INIT || s_user_send_fn == NULL)
        return;
    s_user_send_fn(data, length);
}

#else
#error "VA_TRANSPORT must be ST_LINK_ITM, JLINK_RTT, or CUSTOM_TRANSPORT"
#endif // VA_TRANSPORT

/* ================================================================
 *  Packet emission layer
 * ================================================================ */
#if VA_TRANSPORT_IS_CUSTOM
void _va_emit_packet(const uint8_t *data, uint32_t length)
{
    uint8_t cobs_buf[VA_MAX_PACKET_SIZE + (VA_MAX_PACKET_SIZE / 254) + 2];
    size_t encoded_len = va_cobs_encode(data, (size_t)length, cobs_buf);
    _va_send_bytes(cobs_buf, (uint32_t)encoded_len);
}
#else
void _va_emit_packet(const uint8_t *data, uint32_t length)
{
    _va_send_bytes(data, length);
}
#endif

/* ================================================================
 *  Packet construction helpers (non-static — adapters use these)
 * ================================================================ */

void _va_send_event_packet(uint8_t type_byte, uint8_t id, uint64_t timestamp)
{
    uint8_t packet[10];
    packet[0] = type_byte;
    packet[1] = id;
    packet[2] = (uint8_t)(timestamp >> 0);
    packet[3] = (uint8_t)(timestamp >> 8);
    packet[4] = (uint8_t)(timestamp >> 16);
    packet[5] = (uint8_t)(timestamp >> 24);
    packet[6] = (uint8_t)(timestamp >> 32);
    packet[7] = (uint8_t)(timestamp >> 40);
    packet[8] = (uint8_t)(timestamp >> 48);
    packet[9] = (uint8_t)(timestamp >> 56);
    _va_emit_packet(packet, 10);
}

void _va_send_setup_packet(uint8_t setupCode, uint8_t id, const char *name)
{
    uint8_t name_len = (uint8_t)strlen(name);
    if (name_len >= VA_MAX_TASK_NAME_LEN)
    {
        name_len = VA_MAX_TASK_NAME_LEN - 1;
    }
    uint8_t buf[3 + VA_MAX_TASK_NAME_LEN];
    buf[0] = setupCode;
    buf[1] = id;
    buf[2] = name_len;
    memcpy(&buf[3], name, name_len);
    _va_emit_packet(buf, 3 + name_len);
}

void _va_send_user_setup_packet(uint8_t id, uint8_t type, const char *name)
{
    uint8_t name_len = (uint8_t)strlen(name);
    if (name_len >= VA_MAX_TASK_NAME_LEN)
    {
        name_len = VA_MAX_TASK_NAME_LEN - 1;
    }
    uint8_t buf[4 + VA_MAX_TASK_NAME_LEN];
    buf[0] = VA_SETUP_USER_TRACE;
    buf[1] = id;
    buf[2] = type;
    buf[3] = name_len;
    memcpy(&buf[4], name, name_len);
    _va_emit_packet(buf, 4 + name_len);
}

void _va_send_user_event_packet(uint8_t id, int32_t value, uint64_t timestamp)
{
    uint8_t packet[14];
    packet[0] = VA_EVENT_USER_TRACE;
    packet[1] = id;
    packet[2] = (uint8_t)(timestamp >> 0);
    packet[3] = (uint8_t)(timestamp >> 8);
    packet[4] = (uint8_t)(timestamp >> 16);
    packet[5] = (uint8_t)(timestamp >> 24);
    packet[6] = (uint8_t)(timestamp >> 32);
    packet[7] = (uint8_t)(timestamp >> 40);
    packet[8] = (uint8_t)(timestamp >> 48);
    packet[9] = (uint8_t)(timestamp >> 56);
    packet[10] = (uint8_t)(value >> 0);
    packet[11] = (uint8_t)(value >> 8);
    packet[12] = (uint8_t)(value >> 16);
    packet[13] = (uint8_t)(value >> 24);
    _va_emit_packet(packet, 14);
}

void _va_send_float_event_packet(uint8_t id, float value, uint64_t timestamp)
{
    uint8_t packet[14];
    uint32_t fbits;
    memcpy(&fbits, &value, sizeof(fbits));
    packet[0] = VA_EVENT_FLOAT_TRACE;
    packet[1] = id;
    packet[2] = (uint8_t)(timestamp >> 0);
    packet[3] = (uint8_t)(timestamp >> 8);
    packet[4] = (uint8_t)(timestamp >> 16);
    packet[5] = (uint8_t)(timestamp >> 24);
    packet[6] = (uint8_t)(timestamp >> 32);
    packet[7] = (uint8_t)(timestamp >> 40);
    packet[8] = (uint8_t)(timestamp >> 48);
    packet[9] = (uint8_t)(timestamp >> 56);
    packet[10] = (uint8_t)(fbits >> 0);
    packet[11] = (uint8_t)(fbits >> 8);
    packet[12] = (uint8_t)(fbits >> 16);
    packet[13] = (uint8_t)(fbits >> 24);
    _va_emit_packet(packet, 14);
}

void _va_send_user_toggle_event_packet(uint8_t id, VA_UserToggleState_t state, uint64_t timestamp)
{
    uint8_t packet[11];
    packet[0] = VA_EVENT_USER_TOGGLE;
    packet[1] = id;
    packet[2] = (uint8_t)(timestamp >> 0);
    packet[3] = (uint8_t)(timestamp >> 8);
    packet[4] = (uint8_t)(timestamp >> 16);
    packet[5] = (uint8_t)(timestamp >> 24);
    packet[6] = (uint8_t)(timestamp >> 32);
    packet[7] = (uint8_t)(timestamp >> 40);
    packet[8] = (uint8_t)(timestamp >> 48);
    packet[9] = (uint8_t)(timestamp >> 56);
    packet[10] = (uint8_t)(state);
    _va_emit_packet(packet, 11);
}

void _va_send_notification_event_packet(uint8_t type_byte, uint8_t id, uint8_t other_id, uint32_t value, uint64_t timestamp)
{
    uint8_t packet[15];
    packet[0] = type_byte;
    packet[1] = id;
    packet[2] = other_id;
    packet[3] = (uint8_t)(timestamp >> 0);
    packet[4] = (uint8_t)(timestamp >> 8);
    packet[5] = (uint8_t)(timestamp >> 16);
    packet[6] = (uint8_t)(timestamp >> 24);
    packet[7] = (uint8_t)(timestamp >> 32);
    packet[8] = (uint8_t)(timestamp >> 40);
    packet[9] = (uint8_t)(timestamp >> 48);
    packet[10] = (uint8_t)(timestamp >> 56);
    packet[11] = (uint8_t)(value >> 0);
    packet[12] = (uint8_t)(value >> 8);
    packet[13] = (uint8_t)(value >> 16);
    packet[14] = (uint8_t)(value >> 24);
    _va_emit_packet(packet, 15);
}

void _va_send_mutex_contention_packet(uint8_t mutex_id, uint8_t waiting_task_id, uint8_t holder_task_id, uint64_t timestamp)
{
    uint8_t packet[12];
    packet[0] = VA_EVENT_MUTEX_CONTENTION;
    packet[1] = mutex_id;
    packet[2] = waiting_task_id;
    packet[3] = holder_task_id;
    packet[4] = (uint8_t)(timestamp >> 0);
    packet[5] = (uint8_t)(timestamp >> 8);
    packet[6] = (uint8_t)(timestamp >> 16);
    packet[7] = (uint8_t)(timestamp >> 24);
    packet[8] = (uint8_t)(timestamp >> 32);
    packet[9] = (uint8_t)(timestamp >> 40);
    packet[10] = (uint8_t)(timestamp >> 48);
    packet[11] = (uint8_t)(timestamp >> 56);
    _va_emit_packet(packet, 12);
}

void _va_send_task_create_packet(uint8_t id, uint64_t timestamp, uint32_t priority, uint32_t base_priority, uint32_t stack_size)
{
    uint8_t packet[22];
    packet[0] = VA_EVENT_TASK_CREATE;
    packet[1] = id;
    packet[2] = (uint8_t)(timestamp >> 0);
    packet[3] = (uint8_t)(timestamp >> 8);
    packet[4] = (uint8_t)(timestamp >> 16);
    packet[5] = (uint8_t)(timestamp >> 24);
    packet[6] = (uint8_t)(timestamp >> 32);
    packet[7] = (uint8_t)(timestamp >> 40);
    packet[8] = (uint8_t)(timestamp >> 48);
    packet[9] = (uint8_t)(timestamp >> 56);
    packet[10] = (uint8_t)(priority >> 0);
    packet[11] = (uint8_t)(priority >> 8);
    packet[12] = (uint8_t)(priority >> 16);
    packet[13] = (uint8_t)(priority >> 24);
    packet[14] = (uint8_t)(base_priority >> 0);
    packet[15] = (uint8_t)(base_priority >> 8);
    packet[16] = (uint8_t)(base_priority >> 16);
    packet[17] = (uint8_t)(base_priority >> 24);
    packet[18] = (uint8_t)(stack_size >> 0);
    packet[19] = (uint8_t)(stack_size >> 8);
    packet[20] = (uint8_t)(stack_size >> 16);
    packet[21] = (uint8_t)(stack_size >> 24);
    _va_emit_packet(packet, 22);
}

void _va_send_stack_usage_packet(uint8_t id, uint64_t timestamp, uint32_t stack_used, uint32_t stack_total)
{
    uint8_t packet[18];
    packet[0] = VA_EVENT_TASK_STACK_USAGE;
    packet[1] = id;
    packet[2] = (uint8_t)(timestamp >> 0);
    packet[3] = (uint8_t)(timestamp >> 8);
    packet[4] = (uint8_t)(timestamp >> 16);
    packet[5] = (uint8_t)(timestamp >> 24);
    packet[6] = (uint8_t)(timestamp >> 32);
    packet[7] = (uint8_t)(timestamp >> 40);
    packet[8] = (uint8_t)(timestamp >> 48);
    packet[9] = (uint8_t)(timestamp >> 56);
    packet[10] = (uint8_t)(stack_used >> 0);
    packet[11] = (uint8_t)(stack_used >> 8);
    packet[12] = (uint8_t)(stack_used >> 16);
    packet[13] = (uint8_t)(stack_used >> 24);
    packet[14] = (uint8_t)(stack_total >> 0);
    packet[15] = (uint8_t)(stack_total >> 8);
    packet[16] = (uint8_t)(stack_total >> 16);
    packet[17] = (uint8_t)(stack_total >> 24);
    _va_emit_packet(packet, 18);
}

void _va_send_data_event_packet(uint8_t type_byte, uint8_t id, uint32_t value, uint64_t timestamp)
{
    uint8_t packet[14];
    packet[0] = type_byte;
    packet[1] = id;
    packet[2] = (uint8_t)(timestamp >> 0);
    packet[3] = (uint8_t)(timestamp >> 8);
    packet[4] = (uint8_t)(timestamp >> 16);
    packet[5] = (uint8_t)(timestamp >> 24);
    packet[6] = (uint8_t)(timestamp >> 32);
    packet[7] = (uint8_t)(timestamp >> 40);
    packet[8] = (uint8_t)(timestamp >> 48);
    packet[9] = (uint8_t)(timestamp >> 56);
    packet[10] = (uint8_t)(value >> 0);
    packet[11] = (uint8_t)(value >> 8);
    packet[12] = (uint8_t)(value >> 16);
    packet[13] = (uint8_t)(value >> 24);
    _va_emit_packet(packet, 14);
}

void _va_send_heap_setup_packet(uint8_t id, const char *name, uint32_t totalSize)
{
    uint8_t name_len = (uint8_t)strlen(name);
    if (name_len >= VA_MAX_TASK_NAME_LEN)
    {
        name_len = VA_MAX_TASK_NAME_LEN - 1;
    }
    uint8_t buf[7 + VA_MAX_TASK_NAME_LEN];
    buf[0] = VA_SETUP_HEAP_INFO;
    buf[1] = id;
    buf[2] = (uint8_t)(totalSize >> 0);
    buf[3] = (uint8_t)(totalSize >> 8);
    buf[4] = (uint8_t)(totalSize >> 16);
    buf[5] = (uint8_t)(totalSize >> 24);
    buf[6] = name_len;
    memcpy(&buf[7], name, name_len);
    _va_emit_packet(buf, 7 + name_len);
}

/* ================================================================
 *  Timestamp
 * ================================================================ */

uint64_t _va_get_timestamp(void)
{
    static uint32_t last_dwt_value = 0;
    uint32_t high_part;
    uint32_t low_part;
    uint32_t temp_low;
    uint32_t primask_state = __get_PRIMASK();
    __disable_irq();

    uint32_t current_dwt = DWT->CYCCNT;
    if (current_dwt < last_dwt_value)
    {
        g_dwt_overflow_count++;
    }
    last_dwt_value = current_dwt;
    high_part = g_dwt_overflow_count;
    low_part = current_dwt;
    temp_low = DWT->CYCCNT;
    if (temp_low < low_part)
    {
        high_part = g_dwt_overflow_count;
        low_part = temp_low;
    }
    __set_PRIMASK(primask_state);
    return (((uint64_t)high_part) << 32) | low_part;
}

void VA_TickOverflowCheck(void)
{
    if (!VA_IS_INIT) return;
    (void)_va_get_timestamp();
}

static void _va_enable_dwt_counter(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* ================================================================
 *  Generic task-map helpers
 * ================================================================ */

uint8_t _va_find_task_id(void *handle)
{
    for (int i = 0; i < VA_MAX_TASKS; ++i)
    {
        if (taskMap[i].active && taskMap[i].handle == handle)
        {
            return taskMap[i].id;
        }
    }
    return 0;
}

int _va_find_task_index(void *handle)
{
    for (int i = 0; i < VA_MAX_TASKS; ++i)
    {
        if (taskMap[i].active && taskMap[i].handle == handle)
        {
            return i;
        }
    }
    return -1;
}

uint8_t _va_assign_task_id(void *handle, const char *name)
{
    if (handle == NULL || name == NULL)
        return 0;
    int empty_slot = -1;
    for (int i = 0; i < VA_MAX_TASKS; ++i)
    {
        if (!taskMap[i].active)
        {
            empty_slot = i;
            break;
        }
    }
    if (empty_slot == -1 || next_task_id == 0)
        return 0;

    uint8_t new_id = next_task_id++;
    taskMap[empty_slot].active = true;
    taskMap[empty_slot].handle = handle;
    taskMap[empty_slot].id = new_id;
    taskMap[empty_slot].last_notifier = NULL;

    taskMap[empty_slot].pxStack = (void *)g_task_pxStack;
    taskMap[empty_slot].pxEndOfStack = (void *)g_task_pxEndOfStack;
    taskMap[empty_slot].uxPriority = g_task_uxPriority;
    taskMap[empty_slot].uxBasePriority = g_task_uxBasePriority;
    taskMap[empty_slot].ulStackDepth = g_task_ulStackDepth;

    strncpy(taskMap[empty_slot].name, name, VA_MAX_TASK_NAME_LEN - 1);
    taskMap[empty_slot].name[VA_MAX_TASK_NAME_LEN - 1] = '\0';

    _va_send_setup_packet(VA_SETUP_TASK_MAP, new_id, taskMap[empty_slot].name);
    return new_id;
}

/* ================================================================
 *  Queue / sync-object map helpers
 * ================================================================ */
#if VA_HAS_RTOS

const char *_va_get_object_type_name(VA_QueueObjectType_t type)
{
    switch (type)
    {
    case VA_OBJECT_TYPE_QUEUE:           return "Queue";
    case VA_OBJECT_TYPE_MUTEX:           return "Mutex";
    case VA_OBJECT_TYPE_COUNTING_SEM:    return "CountingSem";
    case VA_OBJECT_TYPE_BINARY_SEM:      return "BinarySem";
    case VA_OBJECT_TYPE_RECURSIVE_MUTEX: return "RecursiveMutex";
    default:                             return "Unknown";
    }
}

uint8_t _va_get_setup_packet_type(VA_QueueObjectType_t type)
{
    switch (type)
    {
    case VA_OBJECT_TYPE_QUEUE:
        return VA_SETUP_QUEUE_MAP;
    case VA_OBJECT_TYPE_MUTEX:
    case VA_OBJECT_TYPE_RECURSIVE_MUTEX:
        return VA_SETUP_MUTEX_MAP;
    case VA_OBJECT_TYPE_COUNTING_SEM:
    case VA_OBJECT_TYPE_BINARY_SEM:
        return VA_SETUP_SEMAPHORE_MAP;
    default:
        return VA_SETUP_QUEUE_MAP;
    }
}

uint8_t _va_find_queue_object_id(void *handle)
{
    for (int i = 0; i < VA_MAX_SYNC_OBJECTS; ++i)
    {
        if (queueObjectMap[i].active && queueObjectMap[i].handle == handle)
        {
            return queueObjectMap[i].id;
        }
    }
    return 0;
}

VA_QueueObjectType_t _va_get_stored_queue_object_type(void *handle)
{
    for (int i = 0; i < VA_MAX_SYNC_OBJECTS; ++i)
    {
        if (queueObjectMap[i].active && queueObjectMap[i].handle == handle)
        {
            return queueObjectMap[i].type;
        }
    }
    return va_adapter_get_queue_object_type(handle);
}

uint8_t _va_assign_queue_object_id(void *handle, const char *name, VA_QueueObjectType_t type)
{
    if (handle == NULL)
        return 0;

    int empty_slot = -1;
    for (int i = 0; i < VA_MAX_SYNC_OBJECTS; ++i)
    {
        if (!queueObjectMap[i].active)
        {
            empty_slot = i;
            break;
        }
    }
    if (empty_slot == -1 || next_queue_object_id == 0)
        return 0;

    uint8_t new_id = next_queue_object_id++;
    queueObjectMap[empty_slot].active = true;
    queueObjectMap[empty_slot].handle = handle;
    queueObjectMap[empty_slot].id = new_id;
    queueObjectMap[empty_slot].type = type;

    if (name && strlen(name) > 0)
    {
        strncpy(queueObjectMap[empty_slot].name, name, VA_MAX_TASK_NAME_LEN - 1);
    }
    else
    {
        strncpy(queueObjectMap[empty_slot].name, _va_get_object_type_name(type), VA_MAX_TASK_NAME_LEN - 1);
    }
    queueObjectMap[empty_slot].name[VA_MAX_TASK_NAME_LEN - 1] = '\0';

    _va_send_setup_packet(_va_get_setup_packet_type(type), new_id, queueObjectMap[empty_slot].name);
    return new_id;
}

#endif /* VA_HAS_RTOS */

/* ================================================================
 *  User-function map (RTOS-independent)
 * ================================================================ */

static uint8_t _va_find_user_function_id(uint8_t function_id)
{
    for (int i = 0; i < VA_MAX_USER_FUNCTIONS; ++i)
    {
        if (userFunctionMap[i].active && userFunctionMap[i].id == function_id)
        {
            return userFunctionMap[i].id;
        }
    }
    return 0;
}

static uint8_t _va_assign_user_function_id(uint8_t function_id, const char *name)
{
    if (name == NULL || function_id == 0)
        return 0;

    if (_va_find_user_function_id(function_id) != 0)
        return function_id;

    int empty_slot = -1;
    for (int i = 0; i < VA_MAX_USER_FUNCTIONS; ++i)
    {
        if (!userFunctionMap[i].active)
        {
            empty_slot = i;
            break;
        }
    }
    if (empty_slot == -1)
        return 0;

    userFunctionMap[empty_slot].active = true;
    userFunctionMap[empty_slot].id = function_id;
    strncpy(userFunctionMap[empty_slot].name, name, VA_MAX_TASK_NAME_LEN - 1);
    userFunctionMap[empty_slot].name[VA_MAX_TASK_NAME_LEN - 1] = '\0';

    _va_send_setup_packet(VA_SETUP_USER_FUNCTION_MAP, function_id, userFunctionMap[empty_slot].name);
    return function_id;
}

/* ================================================================
 *  RTOS task-event hooks (generic — delegate to adapter for OS specifics)
 * ================================================================ */

void va_taskcreated(void *taskHandle, const char *name)
{
#if VA_HAS_RTOS
    VA_CS_ENTER();
    uint8_t assigned_id = _va_assign_task_id(taskHandle, name ? name : "???");
    if (assigned_id > 0)
    {
        uint64_t timestamp = _va_get_timestamp();
        _va_send_task_create_packet(assigned_id, timestamp,
                                     g_task_uxPriority, g_task_uxBasePriority, g_task_ulStackDepth);
    }
    VA_CS_EXIT();
#else
    VA_UNUSED(taskHandle);
    VA_UNUSED(name);
#endif
}

void va_taskswitchedin(void *taskHandle)
{
#if VA_HAS_RTOS
    VA_CS_ENTER();
    uint8_t id = _va_find_task_id(taskHandle);
    _va_send_event_packet(VA_EVENT_FLAG_START_END | VA_EVENT_TASK_SWITCH, id, _va_get_timestamp());

    if (id != 0)
    {
        uint32_t stack_used = va_adapter_calculate_stack_usage(taskHandle);
        uint32_t stack_total = va_adapter_get_total_stack_size(taskHandle);
        if (stack_total > 0)
        {
            _va_send_stack_usage_packet(id, _va_get_timestamp(), stack_used, stack_total);
        }
    }
    VA_CS_EXIT();
#else
    VA_UNUSED(taskHandle);
#endif
}

void va_taskswitchedout(void *taskHandle)
{
#if VA_HAS_RTOS
    VA_CS_ENTER();
    uint8_t id = _va_find_task_id(taskHandle);
    _va_send_event_packet(VA_EVENT_TASK_SWITCH, id, _va_get_timestamp());

    if (id != 0)
    {
        uint32_t stack_used = va_adapter_calculate_stack_usage(taskHandle);
        uint32_t stack_total = va_adapter_get_total_stack_size(taskHandle);
        if (stack_total > 0)
        {
            _va_send_stack_usage_packet(id, _va_get_timestamp(), stack_used, stack_total);
        }
    }
    VA_CS_EXIT();
#else
    VA_UNUSED(taskHandle);
#endif
}

/* ================================================================
 *  ISR logging (RTOS-independent)
 * ================================================================ */

void VA_LogISRStart(uint8_t isrId)
{
    VA_CS_ENTER();
    if (!VA_IS_INIT)
    {
        VA_CS_EXIT();
        return;
    }
    _va_send_event_packet(VA_EVENT_FLAG_START_END | VA_EVENT_ISR, isrId, _va_get_timestamp());
    VA_CS_EXIT();
}

void VA_LogISREnd(uint8_t isrId)
{
    VA_CS_ENTER();
    if (!VA_IS_INIT)
    {
        VA_CS_EXIT();
        return;
    }
    _va_send_event_packet(VA_EVENT_ISR, isrId, _va_get_timestamp());
    VA_CS_EXIT();
}

bool va_isnit(void)
{
    return VA_IS_INIT;
}

/* ================================================================
 *  User-trace / data logging (RTOS-independent)
 * ================================================================ */

void VA_RegisterUserTrace(uint8_t id, const char *name, VA_UserTraceType_t type)
{
    VA_CS_ENTER();
    if (id == 0 || name == NULL)
    {
        VA_CS_EXIT();
        return;
    }
    if (type == VA_USER_TYPE_ISR)
        _va_send_setup_packet(VA_SETUP_ISR_MAP, id, name);
    else
        _va_send_user_setup_packet(id, (uint8_t)type, name);
    VA_CS_EXIT();
}

void VA_LogTrace(uint8_t id, int32_t value)
{
    VA_CS_ENTER();
    _va_send_user_event_packet(id, value, _va_get_timestamp());
    VA_CS_EXIT();
}

void VA_LogTraceFloat(uint8_t id, float value)
{
    VA_CS_ENTER();
    _va_send_float_event_packet(id, value, _va_get_timestamp());
    VA_CS_EXIT();
}

void VA_LogString(uint8_t id, const char *msg)
{
    if (!msg) return;
    uint8_t len = (uint8_t)strlen(msg);
    if (len == 0) return;
    if (len > 200) len = 200;

    VA_CS_ENTER();
    uint64_t ts = _va_get_timestamp();

    uint8_t buf[11 + 200];
    buf[0] = VA_EVENT_STRING_EVENT;
    buf[1] = id;
    buf[2]  = (uint8_t)(ts >> 0);
    buf[3]  = (uint8_t)(ts >> 8);
    buf[4]  = (uint8_t)(ts >> 16);
    buf[5]  = (uint8_t)(ts >> 24);
    buf[6]  = (uint8_t)(ts >> 32);
    buf[7]  = (uint8_t)(ts >> 40);
    buf[8]  = (uint8_t)(ts >> 48);
    buf[9]  = (uint8_t)(ts >> 56);
    buf[10] = len;
    memcpy(&buf[11], msg, len);

    _va_emit_packet(buf, 11 + len);
    VA_CS_EXIT();
}

void VA_LogToggle(uint8_t id, bool state)
{
    VA_CS_ENTER();
    _va_send_user_toggle_event_packet(id, state, _va_get_timestamp());
    VA_CS_EXIT();
}

void VA_LogGPIO(uint8_t id, bool state)
{
    VA_CS_ENTER();
    _va_send_data_event_packet(VA_EVENT_GPIO, id, (uint32_t)state, _va_get_timestamp());
    VA_CS_EXIT();
}

void VA_LogCounter(uint8_t id, uint32_t value)
{
    VA_CS_ENTER();
    _va_send_data_event_packet(VA_EVENT_COUNTER, id, value, _va_get_timestamp());
    VA_CS_EXIT();
}

void VA_LogHeap(uint8_t id, uint32_t usedBytes)
{
    VA_CS_ENTER();
    _va_send_data_event_packet(VA_EVENT_HEAP, id, usedBytes, _va_get_timestamp());
    VA_CS_EXIT();
}

void VA_RegisterGPIO(uint8_t id, const char *name)
{
    VA_CS_ENTER();
    if (id == 0 || name == NULL)
    {
        VA_CS_EXIT();
        return;
    }
    _va_send_setup_packet(VA_SETUP_GPIO_MAP, id, name);
    VA_CS_EXIT();
}

void VA_RegisterHeap(uint8_t id, const char *name, uint32_t totalSize)
{
    VA_CS_ENTER();
    if (id == 0 || name == NULL)
    {
        VA_CS_EXIT();
        return;
    }
    _va_send_heap_setup_packet(id, name, totalSize);
    VA_CS_EXIT();
}

/* ================================================================
 *  Task notification hooks
 * ================================================================ */

void va_logtasknotifygive(void *srcHandle, void *destHandle, uint32_t value)
{
#if VA_HAS_RTOS
    VA_CS_ENTER();
    uint8_t src_id = _va_find_task_id(srcHandle);
    uint8_t dest_id = _va_find_task_id(destHandle);

    int idx = _va_find_task_index(destHandle);
    if (idx >= 0)
    {
        taskMap[idx].last_notifier = srcHandle;
    }

    _va_send_notification_event_packet(VA_EVENT_FLAG_START_END | VA_EVENT_TASK_NOTIFY,
                                        src_id, dest_id, value, _va_get_timestamp());
    VA_CS_EXIT();
#else
    VA_UNUSED(srcHandle);
    VA_UNUSED(destHandle);
    VA_UNUSED(value);
#endif
}

void va_logtasknotifytake(void *taskHandle, uint32_t value)
{
#if VA_HAS_RTOS
    VA_CS_ENTER();
    uint8_t dest_id = _va_find_task_id(taskHandle);
    void *src = NULL;

    int idx = _va_find_task_index(taskHandle);
    if (idx >= 0)
    {
        src = taskMap[idx].last_notifier;
        taskMap[idx].last_notifier = NULL;
    }

    uint8_t src_id = _va_find_task_id(src);

    _va_send_notification_event_packet(VA_EVENT_TASK_NOTIFY,
                                        dest_id, src_id, value, _va_get_timestamp());
    VA_CS_EXIT();
#else
    VA_UNUSED(taskHandle);
    VA_UNUSED(value);
#endif
}

/* ================================================================
 *  Queue / sync-object event hooks
 * ================================================================ */

void va_logQueueObjectCreate(void *queueObject, const char *name)
{
    va_logQueueObjectCreateWithType(queueObject, name);
}

void va_updateQueueObjectType(void *queueObject, const char *typeHint)
{
#if VA_HAS_RTOS
    if (queueObject == NULL)
        return;

    VA_CS_ENTER();

    int idx = -1;
    for (int i = 0; i < VA_MAX_SYNC_OBJECTS; ++i)
    {
        if (queueObjectMap[i].active && queueObjectMap[i].handle == queueObject)
        {
            idx = i;
            break;
        }
    }

    if (idx >= 0)
    {
        VA_QueueObjectType_t type = VA_OBJECT_TYPE_QUEUE;

        if (typeHint != NULL)
        {
            if (strstr(typeHint, "RecMutex") != NULL || strstr(typeHint, "RecursiveMutex") != NULL)
                type = VA_OBJECT_TYPE_RECURSIVE_MUTEX;
            else if (strstr(typeHint, "Mutex") != NULL)
                type = VA_OBJECT_TYPE_MUTEX;
            else if (strstr(typeHint, "CountSem") != NULL || strstr(typeHint, "CountingSem") != NULL)
                type = VA_OBJECT_TYPE_COUNTING_SEM;
            else if (strstr(typeHint, "BinSem") != NULL || strstr(typeHint, "BinarySem") != NULL)
                type = VA_OBJECT_TYPE_BINARY_SEM;
            else if (strstr(typeHint, "Semaphore") != NULL || strstr(typeHint, "Sem") != NULL)
                type = VA_OBJECT_TYPE_COUNTING_SEM;
        }

        queueObjectMap[idx].type = type;

        char descriptiveName[VA_MAX_TASK_NAME_LEN];
        const char *finalName = typeHint;

        if (typeHint != NULL && strlen(typeHint) > 0)
        {
            switch (type)
            {
            case VA_OBJECT_TYPE_MUTEX:
                if (strstr(typeHint, "Mutex") == NULL)
                {
                    snprintf(descriptiveName, sizeof(descriptiveName), "%s_Mutex", typeHint);
                    finalName = descriptiveName;
                }
                break;
            case VA_OBJECT_TYPE_RECURSIVE_MUTEX:
                snprintf(descriptiveName, sizeof(descriptiveName), "%s_RecMutex", typeHint);
                finalName = descriptiveName;
                break;
            default:
                break;
            }
        }

        strncpy(queueObjectMap[idx].name, finalName, VA_MAX_TASK_NAME_LEN - 1);
        queueObjectMap[idx].name[VA_MAX_TASK_NAME_LEN - 1] = '\0';

        _va_send_setup_packet(_va_get_setup_packet_type(type), queueObjectMap[idx].id, queueObjectMap[idx].name);
    }

    VA_CS_EXIT();
#else
    VA_UNUSED(queueObject);
    VA_UNUSED(typeHint);
#endif
}

void va_logQueueObjectCreateWithType(void *queueObject, const char *typeHint)
{
#if VA_HAS_RTOS
    if (queueObject == NULL)
        return;

    VA_CS_ENTER();
    VA_QueueObjectType_t type = va_adapter_get_queue_object_type(queueObject);

    /* If the adapter returned the default (QUEUE), infer from typeHint.
     * This is essential for Zephyr where k_mutex/k_sem/k_msgq are separate
     * types that can't be distinguished from a void* alone. */
    if (type == VA_OBJECT_TYPE_QUEUE && typeHint != NULL)
    {
        if (strstr(typeHint, "RecMutex") != NULL || strstr(typeHint, "RecursiveMutex") != NULL)
            type = VA_OBJECT_TYPE_RECURSIVE_MUTEX;
        else if (strstr(typeHint, "Mutex") != NULL)
            type = VA_OBJECT_TYPE_MUTEX;
        else if (strstr(typeHint, "CountSem") != NULL || strstr(typeHint, "CountingSem") != NULL)
            type = VA_OBJECT_TYPE_COUNTING_SEM;
        else if (strstr(typeHint, "BinSem") != NULL || strstr(typeHint, "BinarySem") != NULL)
            type = VA_OBJECT_TYPE_BINARY_SEM;
        else if (strstr(typeHint, "Semaphore") != NULL || strstr(typeHint, "Sem") != NULL)
            type = VA_OBJECT_TYPE_COUNTING_SEM;
    }

    char descriptiveName[VA_MAX_TASK_NAME_LEN];
    const char *finalName = typeHint;

    if (typeHint != NULL && strlen(typeHint) > 0)
    {
        switch (type)
        {
        case VA_OBJECT_TYPE_QUEUE:
            if (strstr(typeHint, "Queue") == NULL)
            {
                snprintf(descriptiveName, sizeof(descriptiveName), "%s_Queue", typeHint);
                finalName = descriptiveName;
            }
            break;
        case VA_OBJECT_TYPE_MUTEX:
            if (strstr(typeHint, "Mutex") == NULL)
            {
                snprintf(descriptiveName, sizeof(descriptiveName), "%s_Mutex", typeHint);
                finalName = descriptiveName;
            }
            break;
        case VA_OBJECT_TYPE_RECURSIVE_MUTEX:
            if (strstr(typeHint, "RecMutex") == NULL && strstr(typeHint, "RecursiveMutex") == NULL)
            {
                snprintf(descriptiveName, sizeof(descriptiveName), "%s_RecMutex", typeHint);
                finalName = descriptiveName;
            }
            break;
        case VA_OBJECT_TYPE_COUNTING_SEM:
            if (strstr(typeHint, "Sem") == NULL)
            {
                snprintf(descriptiveName, sizeof(descriptiveName), "%s_CountSem", typeHint);
                finalName = descriptiveName;
            }
            break;
        case VA_OBJECT_TYPE_BINARY_SEM:
            if (strstr(typeHint, "Sem") == NULL)
            {
                snprintf(descriptiveName, sizeof(descriptiveName), "%s_BinSem", typeHint);
                finalName = descriptiveName;
            }
            break;
        default:
            break;
        }
    }

    _va_assign_queue_object_id(queueObject, finalName, type);
    VA_CS_EXIT();
#else
    VA_UNUSED(queueObject);
    VA_UNUSED(typeHint);
#endif
}

void va_logQueueObjectGive(void *queueObject, uint32_t timeout)
{
    VA_UNUSED(timeout);
#if VA_HAS_RTOS
    if (queueObject == NULL)
        return;

    VA_CS_ENTER();
    uint8_t id = _va_find_queue_object_id(queueObject);
    if (id == 0)
    {
        VA_QueueObjectType_t type = va_adapter_get_queue_object_type(queueObject);
        id = _va_assign_queue_object_id(queueObject, NULL, type);
    }

    uint8_t event_type;
    VA_QueueObjectType_t type = _va_get_stored_queue_object_type(queueObject);
    switch (type)
    {
    case VA_OBJECT_TYPE_MUTEX:
    case VA_OBJECT_TYPE_RECURSIVE_MUTEX:
        event_type = VA_EVENT_MUTEX;
        break;
    case VA_OBJECT_TYPE_COUNTING_SEM:
    case VA_OBJECT_TYPE_BINARY_SEM:
        event_type = VA_EVENT_SEMAPHORE;
        break;
    case VA_OBJECT_TYPE_QUEUE:
    default:
        event_type = VA_EVENT_QUEUE;
        break;
    }

    _va_send_event_packet(VA_EVENT_FLAG_START_END | event_type, id, _va_get_timestamp());
    VA_CS_EXIT();
#endif
}

void va_logQueueObjectTake(void *queueObject, uint32_t timeout)
{
#if VA_HAS_RTOS
    if (queueObject == NULL)
        return;

    VA_CS_ENTER();
    VA_UNUSED(timeout);
    uint8_t id = _va_find_queue_object_id(queueObject);
    if (id == 0)
    {
        VA_QueueObjectType_t type = va_adapter_get_queue_object_type(queueObject);
        id = _va_assign_queue_object_id(queueObject, NULL, type);
    }

    uint8_t event_type;
    VA_QueueObjectType_t type = _va_get_stored_queue_object_type(queueObject);

    switch (type)
    {
    case VA_OBJECT_TYPE_MUTEX:
    case VA_OBJECT_TYPE_RECURSIVE_MUTEX:
        event_type = VA_EVENT_MUTEX;
        break;
    case VA_OBJECT_TYPE_COUNTING_SEM:
    case VA_OBJECT_TYPE_BINARY_SEM:
        event_type = VA_EVENT_SEMAPHORE;
        break;
    case VA_OBJECT_TYPE_QUEUE:
    default:
        event_type = VA_EVENT_QUEUE;
        break;
    }

    _va_send_event_packet(event_type, id, _va_get_timestamp());
    VA_CS_EXIT();
#else
    VA_UNUSED(queueObject);
    VA_UNUSED(timeout);
#endif
}

void va_logQueueObjectBlocking(void *queueObject)
{
#if VA_HAS_RTOS
    if (queueObject == NULL)
        return;

    VA_CS_ENTER();

    uint8_t id = _va_find_queue_object_id(queueObject);
    if (id == 0)
    {
        VA_QueueObjectType_t type = va_adapter_get_queue_object_type(queueObject);
        id = _va_assign_queue_object_id(queueObject, NULL, type);
    }

    VA_QueueObjectType_t type = _va_get_stored_queue_object_type(queueObject);

    if (type == VA_OBJECT_TYPE_MUTEX || type == VA_OBJECT_TYPE_RECURSIVE_MUTEX)
    {
        va_adapter_check_mutex_contention(queueObject, id);
    }

    VA_CS_EXIT();
#else
    VA_UNUSED(queueObject);
#endif
}

/* ================================================================
 *  User Function Event Logging
 * ================================================================ */

void VA_RegisterUserFunction(uint8_t id, const char *name)
{
    VA_CS_ENTER();
    if (id == 0 || name == NULL)
    {
        VA_CS_EXIT();
        return;
    }
    _va_assign_user_function_id(id, name);
    VA_CS_EXIT();
}

void VA_LogUserEvent(uint8_t id, bool state)
{
    VA_CS_ENTER();
    if (id == 0)
    {
        VA_CS_EXIT();
        return;
    }
    uint8_t event_flags = (state == USER_EVENT_START) ? (VA_EVENT_FLAG_START_END | VA_EVENT_USER_FUNCTION) : VA_EVENT_USER_FUNCTION;
    _va_send_event_packet(event_flags, id, _va_get_timestamp());
    VA_CS_EXIT();
}

/* ================================================================
 *  Initialization
 * ================================================================ */

#if VA_TRANSPORT_IS_CUSTOM
void VA_RegisterTransportSend(VA_TransportSendFn sendFn)
{
    s_user_send_fn = sendFn;
}
#endif

void VA_Init(uint32_t cpu_freq)
{
    VA_CS_ENTER();
    _va_cpu_freq = cpu_freq;

#if VA_HAS_RTOS
    for (int i = 0; i < VA_MAX_TASKS; ++i)
    {
        taskMap[i].active = false;
        taskMap[i].handle = NULL;
        taskMap[i].id = 0;
        taskMap[i].last_notifier = NULL;
    }
    next_task_id = 1;
    notificationValue = 0;

    for (int i = 0; i < VA_MAX_SYNC_OBJECTS; ++i)
    {
        queueObjectMap[i].active = false;
        queueObjectMap[i].handle = NULL;
        queueObjectMap[i].id = 0;
    }
    next_queue_object_id = 1;
#endif

    for (int i = 0; i < VA_MAX_USER_FUNCTIONS; ++i)
    {
        userFunctionMap[i].active = false;
        userFunctionMap[i].id = 0;
        userFunctionMap[i].name[0] = '\0';
    }

    _va_enable_dwt_counter();

#if VA_TRANSPORT_IS_ST_LINK
    ITM->LAR = 0xC5ACCE55;
    ITM->TCR |= ITM_TCR_ITMENA_Msk;
    ITM->TER |= (1UL << VA_ITM_PORT);
#elif VA_TRANSPORT_IS_JLINK
#if (VA_CONFIGURE_RTT == 1)
        SEGGER_RTT_Init();
    #if VA_RTT_BUFFER_SIZE > 0
        SEGGER_RTT_ConfigUpBuffer(VA_RTT_CHANNEL, "ViewAlyzer", s_va_rtt_up_buffer, sizeof(s_va_rtt_up_buffer), VA_RTT_MODE);
    #else
        SEGGER_RTT_ConfigUpBuffer(VA_RTT_CHANNEL, "ViewAlyzer", NULL, 0, VA_RTT_MODE);
    #endif // VA_RTT_BUFFER_SIZE > 0
#endif // VA_CONFIGURE_RTT
#elif VA_TRANSPORT_IS_CUSTOM
    // Nothing to init — user provides send function via VA_RegisterTransportSend()
#endif // VA_TRANSPORT
    VA_IS_INIT = true;

    _va_emit_packet(VA_SYNC_MARKER, sizeof(VA_SYNC_MARKER));

    char info_buf[40];
    snprintf(info_buf, sizeof(info_buf), "CLK:%u", _va_cpu_freq);
    _va_send_setup_packet(VA_SETUP_INFO, 0, info_buf);
    _va_send_setup_packet(VA_SETUP_ISR_MAP, VA_ISR_ID_SYSTICK, "SysTick");
#if (LOG_PENDSV == 1)
    _va_send_setup_packet(VA_SETUP_ISR_MAP, VA_ISR_ID_PENDSV, "PendSV");
#endif
#if (!VA_HAS_RTOS)
    _va_send_setup_packet(VA_SETUP_CONFIG_FLAGS, 0, "NO_RTOS");
#endif

#if (VA_RTOS_SELECT == VA_RTOS_FREERTOS)
    _va_send_setup_packet(VA_SETUP_OS_INFO, 0, "FreeRTOS");
#elif (VA_RTOS_SELECT == VA_RTOS_ZEPHYR)
    _va_send_setup_packet(VA_SETUP_OS_INFO, 0, "Zephyr");
#else
    _va_send_setup_packet(VA_SETUP_OS_INFO, 0, "BareMetal");
#endif

    VA_CS_EXIT();
}

#endif // DWT_NOT_AVAILABLE check
#endif // VA_ENABLED check

#ifdef __cplusplus
}
#endif
