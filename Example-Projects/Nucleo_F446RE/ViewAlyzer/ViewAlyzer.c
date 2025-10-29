/**
 * @file ViewAlyzer.c
 * @brief ViewAlyzer Recorder Firmware - Implementation
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
#if (VA_ENABLED == 1) && (VA_TRACE_FREERTOS == 1)
#include "FreeRTOS.h"
#include "list.h"
#include "queue.h"
#endif
#ifdef __cplusplus
extern "C"
{
#endif
#if (VA_ENABLED == 1)

#include <string.h> // For strlen, memcpy
#include <stdio.h>  // For snprintf
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

// Optional critical section helpers (preserve PRIMASK)
#ifdef VA_ALLOWED_TO_DISABLE_INTERRUPTS
#define VA_CS_ENTER()                        \
    uint32_t __va_primask = __get_PRIMASK(); \
    __disable_irq()

#define VA_CS_EXIT() __set_PRIMASK(__va_primask)
#else
#define VA_CS_ENTER() ((void)0)
#define VA_CS_EXIT() ((void)0)
#endif

// --- DWT / Timestamp Globals ---
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
#define DWT_ENABLED 1
    static volatile uint32_t g_dwt_overflow_count = 0;
#else
#define DWT_ENABLED 0
#warning "ViewAlyzer requires DWT Cycle Counter (Cortex-M3/M4/M7). ViewAlyzer functions disabled."
#define DWT_NOT_AVAILABLE
#endif

#ifndef DWT_NOT_AVAILABLE // Only compile the rest if DWT is enabled

    volatile bool VA_IS_INIT = false;

    // --- Stream Sync Marker ---
    // Unique byte sequence to signal the start of valid ViewAlyzer binary data.
    // Helps host-side parsers skip over any transport banners/noise.
    // Pattern: 'V' 'A' 'Z' 0x01 'S' 'Y' 'N' 'C' '0' '1' 0xAA 0x55
    static const uint8_t VA_SYNC_MARKER[] = {0x56, 0x41, 0x5A, 0x01, 0x53, 0x59, 0x4E, 0x43, 0x30, 0x31, 0xAA, 0x55};

    // --- Task ID Mapping ---
    typedef struct
    {
        TaskHandle_t handle;
        uint8_t id;
        char name[VA_MAX_TASK_NAME_LEN];
        bool active;
        TaskHandle_t last_notifier; // task that last sent a notification
        // Stack monitoring fields
        void *pxStack;           // Points to start of stack
        void *pxEndOfStack;      // Points to end of stack (if available)
        uint32_t uxPriority;     // Current priority
        uint32_t uxBasePriority; // Base priority (for mutex inheritance)
        uint32_t ulStackDepth;   // Total stack size in words
    } VA_TaskMapEntry_t;

    static VA_TaskMapEntry_t taskMap[VA_MAX_TASKS];
    static uint8_t next_task_id = 1;
    static uint32_t _va_cpu_freq = 0;

    // Expose last notification value for trace macros
    volatile uint32_t notificationValue = 0;

    // Global variables to store task information during creation
    // These are set by the hack in traceTASK_CREATE and used by VA_TaskCreated
    volatile void *g_task_pxStack = NULL;
    volatile void *g_task_pxEndOfStack = NULL;
    volatile uint32_t g_task_uxPriority = 0;
    volatile uint32_t g_task_uxBasePriority = 0;
    volatile uint32_t g_task_ulStackDepth = 0;

    // --- User Function Tracking --- (Independent of FreeRTOS)
    typedef struct
    {
        uint8_t id;
        char name[VA_MAX_TASK_NAME_LEN];
        bool active;
    } VA_UserFunctionMapEntry_t;
    static VA_UserFunctionMapEntry_t userFunctionMap[VA_MAX_TASKS];

    // --- Low-Level Send Functions (Conditional) ---

#if VA_TRANSPORT_IS_ST_LINK
// --- ITM Send Functions ---
// Basic blocking wait for ITM.
#define ITM_WaitReady(port) while (ITM->PORT[port].u32 == 0)

    static inline void ITM_SendU32(uint8_t port, uint32_t value)
    {
        ITM_WaitReady(port);
        ITM->PORT[port].u32 = value; // Send 32-bit value
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
        // NOTE: no CS here to avoid nested PRIMASK variables; callers are CS-wrapped.
        uint32_t i = 0;
        while (length >= 4)
        {
            uint32_t word = ((uint32_t)data[i + 3] << 24) |
                            ((uint32_t)data[i + 2] << 16) |
                            ((uint32_t)data[i + 1] << 8) |
                            ((uint32_t)data[i + 0] << 0); // Assuming Little Endian CPU
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

#else  // VA_TRANSPORT_IS_ST_LINK
    // --- RTT Send Function ---
    static void _va_send_bytes(const uint8_t *data, uint32_t length)
    {
        if (!VA_IS_INIT)
            return;
        // NOTE: no CS here to avoid nested PRIMASK variables; callers are CS-wrapped.
        SEGGER_RTT_Write(VA_RTT_CHANNEL, data, length);
    }
#endif // VA_TRANSPORT_IS_ST_LINK

    // --- Packet Sending Logic ---

    static void _va_send_event_packet(uint8_t type_byte, uint8_t id, uint64_t timestamp)
    {
        uint8_t packet[10];

        packet[0] = type_byte;
        packet[1] = id;
        // Timestamp (pack as Little Endian bytes)
        packet[2] = (uint8_t)(timestamp >> 0);
        packet[3] = (uint8_t)(timestamp >> 8);
        packet[4] = (uint8_t)(timestamp >> 16);
        packet[5] = (uint8_t)(timestamp >> 24);
        packet[6] = (uint8_t)(timestamp >> 32);
        packet[7] = (uint8_t)(timestamp >> 40);
        packet[8] = (uint8_t)(timestamp >> 48);
        packet[9] = (uint8_t)(timestamp >> 56); // MSB

        _va_send_bytes(packet, 10);
    }

    static void _va_send_setup_packet(uint8_t setupCode, uint8_t id, const char *name)
    {
        uint8_t name_len = (uint8_t)strlen(name);
        if (name_len >= VA_MAX_TASK_NAME_LEN)
        {
            name_len = VA_MAX_TASK_NAME_LEN - 1;
        }

        uint8_t header[3];
        header[0] = setupCode;
        header[1] = id;
        header[2] = name_len;

        _va_send_bytes(header, 3);
        _va_send_bytes((const uint8_t *)name, name_len);
    }

    static void _va_send_user_setup_packet(uint8_t id, uint8_t type, const char *name)
    {
        uint8_t name_len = (uint8_t)strlen(name);
        if (name_len >= VA_MAX_TASK_NAME_LEN)
        {
            name_len = VA_MAX_TASK_NAME_LEN - 1;
        }
        uint8_t header[4];
        header[0] = VA_SETUP_USER_TRACE;
        header[1] = id;
        header[2] = type;
        header[3] = name_len;
        _va_send_bytes(header, 4);
        _va_send_bytes((const uint8_t *)name, name_len);
    }

    static void _va_send_user_event_packet(uint8_t id, int32_t value, uint64_t timestamp)
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
        _va_send_bytes(packet, 14);
    }

    static void _va_send_user_toggle_event_packet(uint8_t id, VA_UserToggleState_t state, uint64_t timestamp)
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

        _va_send_bytes(packet, 11);
    }

    // --- Timestamp Function --- (With automatic overflow detection)
    static inline uint64_t _va_get_timestamp(void)
    {
        static uint32_t last_dwt_value = 0;
        uint32_t high_part;
        uint32_t low_part;
        uint32_t temp_low;
        uint32_t primask_state = __get_PRIMASK();
        __disable_irq();

        // Automatic overflow detection
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

    // --- DWT Initialization ---
    static void _va_enable_dwt_counter(void)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }

#if (VA_TRACE_FREERTOS == 1)
    static void _va_send_notification_event_packet(uint8_t type_byte, uint8_t id, uint8_t other_id, uint32_t value, uint64_t timestamp)
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
        _va_send_bytes(packet, 15);
    }

    // Mutex contention packet: [Type(1B)] [MutexID(1B)] [WaitingTaskID(1B)] [HolderTaskID(1B)] [Timestamp(8B)]
    static void _va_send_mutex_contention_packet(uint8_t mutex_id, uint8_t waiting_task_id, uint8_t holder_task_id, uint64_t timestamp)
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
        _va_send_bytes(packet, 12);
    }

    static void _va_send_task_create_packet(uint8_t id, uint64_t timestamp, uint32_t priority, uint32_t base_priority, uint32_t stack_size)
    {
        uint8_t packet[22];
        packet[0] = VA_EVENT_TASK_CREATE;
        packet[1] = id;
        // Timestamp (8 bytes)
        packet[2] = (uint8_t)(timestamp >> 0);
        packet[3] = (uint8_t)(timestamp >> 8);
        packet[4] = (uint8_t)(timestamp >> 16);
        packet[5] = (uint8_t)(timestamp >> 24);
        packet[6] = (uint8_t)(timestamp >> 32);
        packet[7] = (uint8_t)(timestamp >> 40);
        packet[8] = (uint8_t)(timestamp >> 48);
        packet[9] = (uint8_t)(timestamp >> 56);
        // Priority (4 bytes)
        packet[10] = (uint8_t)(priority >> 0);
        packet[11] = (uint8_t)(priority >> 8);
        packet[12] = (uint8_t)(priority >> 16);
        packet[13] = (uint8_t)(priority >> 24);
        // Base Priority (4 bytes)
        packet[14] = (uint8_t)(base_priority >> 0);
        packet[15] = (uint8_t)(base_priority >> 8);
        packet[16] = (uint8_t)(base_priority >> 16);
        packet[17] = (uint8_t)(base_priority >> 24);
        // Stack Size (4 bytes)
        packet[18] = (uint8_t)(stack_size >> 0);
        packet[19] = (uint8_t)(stack_size >> 8);
        packet[20] = (uint8_t)(stack_size >> 16);
        packet[21] = (uint8_t)(stack_size >> 24);
        _va_send_bytes(packet, 22);
    }

    // Send Stack Usage Packet: [Type(1B)] [ID(1B)] [Timestamp(8B)] [StackUsed(4B)] [StackTotal(4B)]
    static void _va_send_stack_usage_packet(uint8_t id, uint64_t timestamp, uint32_t stack_used, uint32_t stack_total)
    {
        uint8_t packet[18];
        packet[0] = VA_EVENT_TASK_STACK_USAGE;
        packet[1] = id;
        // Timestamp (8 bytes)
        packet[2] = (uint8_t)(timestamp >> 0);
        packet[3] = (uint8_t)(timestamp >> 8);
        packet[4] = (uint8_t)(timestamp >> 16);
        packet[5] = (uint8_t)(timestamp >> 24);
        packet[6] = (uint8_t)(timestamp >> 32);
        packet[7] = (uint8_t)(timestamp >> 40);
        packet[8] = (uint8_t)(timestamp >> 48);
        packet[9] = (uint8_t)(timestamp >> 56);
        // Stack Used (4 bytes)
        packet[10] = (uint8_t)(stack_used >> 0);
        packet[11] = (uint8_t)(stack_used >> 8);
        packet[12] = (uint8_t)(stack_used >> 16);
        packet[13] = (uint8_t)(stack_used >> 24);
        // Stack Total (4 bytes)
        packet[14] = (uint8_t)(stack_total >> 0);
        packet[15] = (uint8_t)(stack_total >> 8);
        packet[16] = (uint8_t)(stack_total >> 16);
        packet[17] = (uint8_t)(stack_total >> 24);
        _va_send_bytes(packet, 18);
    }

    static uint8_t _va_find_task_id(TaskHandle_t handle)
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

    static int _va_find_task_index(TaskHandle_t handle)
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
    static uint32_t _va_calculate_stack_usage(TaskHandle_t handle)
    {
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
        uint32_t free_stack_words = uxTaskGetStackHighWaterMark(handle);
        int idx = _va_find_task_index(handle);
        if (idx >= 0 && taskMap[idx].ulStackDepth > 0)
        {
            uint32_t used_stack_words = taskMap[idx].ulStackDepth - free_stack_words;
            return used_stack_words;
        }
        return free_stack_words;
#else
        return 0;
#endif
    }

    static uint32_t _va_get_total_stack_size(TaskHandle_t handle)
    {
        int idx = _va_find_task_index(handle);
        if (idx >= 0)
        {
            return taskMap[idx].ulStackDepth;
        }
        return 0;
    }

    /* Commented out - currently unused
    static uint32_t _va_get_available_stack(TaskHandle_t handle)
    {
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
        return uxTaskGetStackHighWaterMark(handle);
#else
        return 0;
#endif
    }
    */

    static uint8_t _va_assign_task_id(TaskHandle_t handle, const char *name)
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
            return 0; // No slots or ID wrapped

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
        return new_id; // Return the assigned ID
    }
#endif

    void VA_TaskCreated(TaskHandle_t pxCreatedTask)
    {
#if (VA_TRACE_FREERTOS == 1)
        VA_CS_ENTER();
        const char *name = pcTaskGetName(pxCreatedTask);
        uint8_t assigned_id = _va_assign_task_id(pxCreatedTask, name ? name : "???");
        if (assigned_id > 0)
        {
            uint64_t timestamp = _va_get_timestamp();
            _va_send_task_create_packet(assigned_id, timestamp, g_task_uxPriority, g_task_uxBasePriority, g_task_ulStackDepth);
        }
        VA_CS_EXIT();
#endif
    }

    // TODO: this is a freeRTOS hook, need to abstract if supporting other RTOS'
    void VA_TaskSwitchedIn(void)
    {
#if (VA_TRACE_FREERTOS == 1)
        VA_CS_ENTER();
        TaskHandle_t handle = xTaskGetCurrentTaskHandle();
        uint8_t id = _va_find_task_id(handle);
        _va_send_event_packet(VA_EVENT_FLAG_START_END | VA_EVENT_TASK_SWITCH, id, _va_get_timestamp());

        if (id != 0)
        {
            uint32_t stack_used = _va_calculate_stack_usage(handle);
            uint32_t stack_total = _va_get_total_stack_size(handle);
            if (stack_total > 0)
            {
                _va_send_stack_usage_packet(id, _va_get_timestamp(), stack_used, stack_total);
            }
        }
        VA_CS_EXIT();
#endif
    }

    void VA_TaskSwitchedOut(void)
    {
#if (VA_TRACE_FREERTOS == 1)
        VA_CS_ENTER();
        TaskHandle_t handle = xTaskGetCurrentTaskHandle();
        uint8_t id = _va_find_task_id(handle);
        _va_send_event_packet(VA_EVENT_TASK_SWITCH, id, _va_get_timestamp());

        if (id != 0)
        {
            uint32_t stack_used = _va_calculate_stack_usage(handle);
            uint32_t stack_total = _va_get_total_stack_size(handle);
            if (stack_total > 0)
            {
                _va_send_stack_usage_packet(id, _va_get_timestamp(), stack_used, stack_total);
            }
        }
        VA_CS_EXIT();
#endif
    }

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

    void VA_TrackDWTOverflow(void)
    {
        // no CS needed; left intentionally empty
    }

    bool VA_IsInit(void)
    {
        // read-only; no CS needed
        return VA_IS_INIT;
    }

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

    void VA_LogUserTrace(uint8_t id, int32_t value)
    {
        VA_CS_ENTER();
        _va_send_user_event_packet(id, value, _va_get_timestamp());
        VA_CS_EXIT();
    }

    void VA_LogUserToggleEvent(uint8_t id, bool state)
    {
        VA_CS_ENTER();
        _va_send_user_toggle_event_packet(id, state, _va_get_timestamp());
        VA_CS_EXIT();
    }

    void VA_LogTaskNotifyGive(TaskHandle_t dest, uint32_t value)
    {
#if (VA_TRACE_FREERTOS == 1)
        VA_CS_ENTER();
        TaskHandle_t src = xTaskGetCurrentTaskHandle();
        uint8_t src_id = _va_find_task_id(src);
        uint8_t dest_id = _va_find_task_id(dest);

        int idx = _va_find_task_index(dest);
        if (idx >= 0)
        {
            taskMap[idx].last_notifier = src;
        }

        _va_send_notification_event_packet(VA_EVENT_FLAG_START_END | VA_EVENT_TASK_NOTIFY,
                                           src_id, dest_id, value, _va_get_timestamp());
        VA_CS_EXIT();
#endif
    }

    void VA_LogTaskNotifyTake(uint32_t value)
    {
#if (VA_TRACE_FREERTOS == 1)
        VA_CS_ENTER();
        TaskHandle_t dest = xTaskGetCurrentTaskHandle();
        uint8_t dest_id = _va_find_task_id(dest);
        TaskHandle_t src = NULL;

        int idx = _va_find_task_index(dest);
        if (idx >= 0)
        {
            src = taskMap[idx].last_notifier;
            taskMap[idx].last_notifier = NULL;
        }

        uint8_t src_id = _va_find_task_id(src);

        _va_send_notification_event_packet(VA_EVENT_TASK_NOTIFY,
                                           dest_id, src_id, value, _va_get_timestamp());
        VA_CS_EXIT();
#endif
    }

// --- Unified Queue Object Tracking ---
#if (VA_TRACE_FREERTOS == 1)
    static uint8_t next_queue_object_id = 1;
    typedef struct
    {
        void *handle;
        uint8_t id;
        char name[VA_MAX_TASK_NAME_LEN];
        VA_QueueObjectType_t type;
        bool active;
    } VA_QueueObjectMapEntry_t;
    static VA_QueueObjectMapEntry_t queueObjectMap[VA_MAX_TASKS];

    // Helper function to determine object type from FreeRTOS queue handle
    static VA_QueueObjectType_t _va_get_queue_object_type(void *handle)
    {
        if (handle == NULL)
            return VA_OBJECT_TYPE_QUEUE;

        // FreeRTOS queue structure has ucQueueType at offset 44 for most configurations
        // This is a bit fragile but necessary for runtime type detection
        // Alternative: could be passed as parameter if caller knows the type
        typedef struct
        {
            volatile uint8_t *pcHead;    // 0
            volatile uint8_t *pcWriteTo; // 4
            union
            {
                volatile uint8_t *pcReadFrom; // 8
                volatile UBaseType_t uxRecursiveCallCount;
            } u;
            List_t xTasksWaitingToSend;             // 12 (20 bytes)
            List_t xTasksWaitingToReceive;          // 32 (20 bytes)
            volatile UBaseType_t uxMessagesWaiting; // 52
            UBaseType_t uxLength;                   // 56
            UBaseType_t uxItemSize;                 // 60
            volatile int8_t cRxLock;                // 64
            volatile int8_t cTxLock;                // 65
            uint8_t ucStaticallyAllocated;          // 66
            uint8_t ucQueueType;                    // 67  <- This is what we need
        } QueueDefinition;

        QueueDefinition *pxQueue = (QueueDefinition *)handle;
        return (VA_QueueObjectType_t)(pxQueue->ucQueueType);
    }

    static const char *_va_get_object_type_name(VA_QueueObjectType_t type)
    {
        switch (type)
        {
        case VA_OBJECT_TYPE_QUEUE:
            return "Queue";
        case VA_OBJECT_TYPE_MUTEX:
            return "Mutex";
        case VA_OBJECT_TYPE_COUNTING_SEM:
            return "CountingSem";
        case VA_OBJECT_TYPE_BINARY_SEM:
            return "BinarySem";
        case VA_OBJECT_TYPE_RECURSIVE_MUTEX:
            return "RecursiveMutex";
        default:
            return "Unknown";
        }
    }

    static uint8_t _va_get_setup_packet_type(VA_QueueObjectType_t type)
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

    static uint8_t _va_find_queue_object_id(void *handle)
    {
        for (int i = 0; i < VA_MAX_TASKS; ++i)
        {
            if (queueObjectMap[i].active && queueObjectMap[i].handle == handle)
            {
                return queueObjectMap[i].id;
            }
        }
        return 0;
    }

    // Get the stored type from the map (not from FreeRTOS structure)
    static VA_QueueObjectType_t _va_get_stored_queue_object_type(void *handle)
    {
        for (int i = 0; i < VA_MAX_TASKS; ++i)
        {
            if (queueObjectMap[i].active && queueObjectMap[i].handle == handle)
            {
                return queueObjectMap[i].type;
            }
        }
        // Fallback to reading from FreeRTOS structure
        return _va_get_queue_object_type(handle);
    }

    static uint8_t _va_assign_queue_object_id(void *handle, const char *name, VA_QueueObjectType_t type)
    {
        if (handle == NULL)
            return 0;

        int empty_slot = -1;
        for (int i = 0; i < VA_MAX_TASKS; ++i)
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

        // Use provided name or generate default name based on type
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

    // Legacy compatibility functions - map to unified system
    static uint8_t _va_find_semaphore_id(void *handle) { return _va_find_queue_object_id(handle); }
    static uint8_t _va_find_mutex_id(void *handle) { return _va_find_queue_object_id(handle); }
    static uint8_t _va_find_queue_id(void *handle) { return _va_find_queue_object_id(handle); }

    static uint8_t _va_assign_semaphore_id(void *handle, const char *name)
    {
        VA_QueueObjectType_t type = _va_get_queue_object_type(handle);
        return _va_assign_queue_object_id(handle, name, type);
    }
    static uint8_t _va_assign_mutex_id(void *handle, const char *name)
    {
        VA_QueueObjectType_t type = _va_get_queue_object_type(handle);
        return _va_assign_queue_object_id(handle, name, type);
    }
    static uint8_t _va_assign_queue_id(void *handle, const char *name)
    {
        VA_QueueObjectType_t type = _va_get_queue_object_type(handle);
        return _va_assign_queue_object_id(handle, name, type);
    }
#endif

    static uint8_t _va_find_user_function_id(uint8_t function_id)
    {
        for (int i = 0; i < VA_MAX_TASKS; ++i)
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
        for (int i = 0; i < VA_MAX_TASKS; ++i)
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

    // --- Unified Queue Object API ---
    void VA_LogQueueObjectCreate(void *queueObject, const char *name)
    {
        VA_LogQueueObjectCreateWithType(queueObject, name);
    }

    void VA_UpdateQueueObjectType(void *queueObject, const char *typeHint)
    {
#if (VA_TRACE_FREERTOS == 1)
        if (queueObject == NULL)
            return;

        VA_CS_ENTER();

        // Find the existing entry (should have been created by traceQUEUE_CREATE)
        int idx = -1;
        for (int i = 0; i < VA_MAX_TASKS; ++i)
        {
            if (queueObjectMap[i].active && queueObjectMap[i].handle == queueObject)
            {
                idx = i;
                break;
            }
        }

        if (idx >= 0)
        {
            // Determine the type from typeHint string (since FreeRTOS doesn't distinguish
            // between queues and mutexes in ucQueueType field)
            VA_QueueObjectType_t type = VA_OBJECT_TYPE_QUEUE; // default

            if (typeHint != NULL)
            {
                if (strstr(typeHint, "RecMutex") != NULL || strstr(typeHint, "RecursiveMutex") != NULL)
                {
                    type = VA_OBJECT_TYPE_RECURSIVE_MUTEX;
                }
                else if (strstr(typeHint, "Mutex") != NULL)
                {
                    type = VA_OBJECT_TYPE_MUTEX;
                }
                else if (strstr(typeHint, "CountSem") != NULL || strstr(typeHint, "CountingSem") != NULL)
                {
                    type = VA_OBJECT_TYPE_COUNTING_SEM;
                }
                else if (strstr(typeHint, "BinSem") != NULL || strstr(typeHint, "BinarySem") != NULL)
                {
                    type = VA_OBJECT_TYPE_BINARY_SEM;
                }
                // else remains VA_OBJECT_TYPE_QUEUE
            }

            // Update the stored type
            queueObjectMap[idx].type = type;

            // Generate a more descriptive name based on type hint and detected type
            char descriptiveName[VA_MAX_TASK_NAME_LEN];
            const char *finalName = typeHint;

            if (typeHint != NULL && strlen(typeHint) > 0)
            {
                // Combine type hint with detected type for better identification
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

            // Update the name
            strncpy(queueObjectMap[idx].name, finalName, VA_MAX_TASK_NAME_LEN - 1);
            queueObjectMap[idx].name[VA_MAX_TASK_NAME_LEN - 1] = '\0';

            // Re-send setup packet with correct type
            _va_send_setup_packet(_va_get_setup_packet_type(type), queueObjectMap[idx].id, queueObjectMap[idx].name);
        }

        VA_CS_EXIT();
#endif
    }

    void VA_LogQueueObjectCreateWithType(void *queueObject, const char *typeHint)
    {
#if (VA_TRACE_FREERTOS == 1)
        if (queueObject == NULL)
            return;

        VA_CS_ENTER();
        VA_QueueObjectType_t type = _va_get_queue_object_type(queueObject);

        // Generate a more descriptive name based on type hint and detected type
        char descriptiveName[VA_MAX_TASK_NAME_LEN];
        const char *finalName = typeHint;

        if (typeHint != NULL && strlen(typeHint) > 0)
        {
            // Combine type hint with detected type for better identification
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
                snprintf(descriptiveName, sizeof(descriptiveName), "%s_RecMutex", typeHint);
                finalName = descriptiveName;
                break;
            case VA_OBJECT_TYPE_COUNTING_SEM:
                snprintf(descriptiveName, sizeof(descriptiveName), "%s_CountSem", typeHint);
                finalName = descriptiveName;
                break;
            case VA_OBJECT_TYPE_BINARY_SEM:
                snprintf(descriptiveName, sizeof(descriptiveName), "%s_BinSem", typeHint);
                finalName = descriptiveName;
                break;
            default:
                break;
            }
        }

        _va_assign_queue_object_id(queueObject, finalName, type);
        VA_CS_EXIT();
#endif
    }

    void VA_LogQueueObjectGive(void *queueObject, uint32_t timeout)
    {
#if (VA_TRACE_FREERTOS == 1)
        if (queueObject == NULL)
            return;

        VA_CS_ENTER();
        uint8_t id = _va_find_queue_object_id(queueObject);
        if (id == 0)
        {
            VA_QueueObjectType_t type = _va_get_queue_object_type(queueObject);
            id = _va_assign_queue_object_id(queueObject, NULL, type);
        }

        // Determine the appropriate event type based on object type
        // Use the stored type from the map (which may have been updated by VA_UpdateQueueObjectType)
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

    void VA_LogQueueObjectTake(void *queueObject, uint32_t timeout)
    {
#if (VA_TRACE_FREERTOS == 1)
        if (queueObject == NULL)
            return;

        VA_CS_ENTER();
        (void)timeout;
        uint8_t id = _va_find_queue_object_id(queueObject);
        if (id == 0)
        {
            VA_QueueObjectType_t type = _va_get_queue_object_type(queueObject);
            id = _va_assign_queue_object_id(queueObject, NULL, type);
        }

        // Determine the appropriate event type based on object type
        // Use the stored type from the map (which may have been updated by VA_UpdateQueueObjectType)
        uint8_t event_type;
        VA_QueueObjectType_t type = _va_get_stored_queue_object_type(queueObject);

        switch (type)
        {
        case VA_OBJECT_TYPE_MUTEX:
        case VA_OBJECT_TYPE_RECURSIVE_MUTEX:
            event_type = VA_EVENT_MUTEX;
            // Note: Contention detection is now handled in VA_LogQueueObjectBlocking()
            // which is called via traceBLOCKING_ON_QUEUE_RECEIVE before the task blocks
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
#endif
    }

    void VA_LogQueueObjectBlocking(void *queueObject)
    {
#if (VA_TRACE_FREERTOS == 1)
        if (queueObject == NULL)
            return;

        VA_CS_ENTER();

        uint8_t id = _va_find_queue_object_id(queueObject);
        if (id == 0)
        {
            VA_QueueObjectType_t type = _va_get_queue_object_type(queueObject);
            id = _va_assign_queue_object_id(queueObject, NULL, type);
        }

        // Get the stored type (which may have been updated by VA_UpdateQueueObjectType)
        VA_QueueObjectType_t type = _va_get_stored_queue_object_type(queueObject);

        // Only check for contention on mutexes
        if (type == VA_OBJECT_TYPE_MUTEX || type == VA_OBJECT_TYPE_RECURSIVE_MUTEX)
        {
// Check if there's contention (another task holds it)
#if (INCLUDE_xQueueGetMutexHolder == 1)
            {
                TaskHandle_t holder = xQueueGetMutexHolder((QueueHandle_t)queueObject);
                if (holder != NULL)
                {
                    // Mutex is currently held by another task - log contention
                    TaskHandle_t current = xTaskGetCurrentTaskHandle();
                    if (holder != current)
                    {
                        uint8_t holder_id = _va_find_task_id(holder);
                        uint8_t waiter_id = _va_find_task_id(current);
                        if (holder_id != 0 && waiter_id != 0)
                        {
                            _va_send_mutex_contention_packet(id, waiter_id, holder_id, _va_get_timestamp());
                        }
                    }
                }
            }
#endif
        }

        VA_CS_EXIT();
#endif
    }

    // --- Legacy API - Updated to use unified system ---
    void VA_LogSemaphoreGive(void *semaphore)
    {
        VA_LogQueueObjectGive(semaphore, 0);
    }

    void VA_LogSemaphoreTake(void *semaphore, uint32_t timeout)
    {
        VA_LogQueueObjectTake(semaphore, timeout);
    }

    void VA_LogSemaphoreCreate(void *semaphore, const char *name)
    {
        VA_LogQueueObjectCreate(semaphore, name);
    }

    void VA_LogMutexAcquire(void *mutex, uint32_t timeout)
    {
        VA_LogQueueObjectTake(mutex, timeout);
    }

    void VA_LogMutexRelease(void *mutex)
    {
        VA_LogQueueObjectGive(mutex, 0);
    }

    void VA_LogMutexCreate(void *mutex, const char *name)
    {
        VA_LogQueueObjectCreate(mutex, name);
    }

    void VA_LogQueueSend(void *queue, uint32_t timeout)
    {
        VA_LogQueueObjectGive(queue, timeout);
    }

    void VA_LogQueueReceive(void *queue, uint32_t timeout)
    {
        VA_LogQueueObjectTake(queue, timeout);
    }

    void VA_LogQueueCreate(void *queue, const char *name)
    {
        VA_LogQueueObjectCreate(queue, name);
    }

    // --- User Function Event Logging ---
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

    // --- Initialization ---
    void VA_Init(uint32_t cpu_freq)
    {
        VA_CS_ENTER();
        _va_cpu_freq = cpu_freq;

#if (VA_TRACE_FREERTOS == 1)
        for (int i = 0; i < VA_MAX_TASKS; ++i)
        {
            taskMap[i].active = false;
            taskMap[i].handle = NULL;
            taskMap[i].id = 0;
            taskMap[i].last_notifier = NULL;
        }
        next_task_id = 1;
        notificationValue = 0;
#endif

        for (int i = 0; i < VA_MAX_TASKS; ++i)
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
#else
        SEGGER_RTT_Init();
#if VA_RTT_BUFFER_SIZE > 0
        // Default to a dedicated buffer so RTT payloads survive host connect latency.
        SEGGER_RTT_ConfigUpBuffer(VA_RTT_CHANNEL, "ViewAlyzer", s_va_rtt_up_buffer, sizeof(s_va_rtt_up_buffer), VA_RTT_MODE);
#else
        SEGGER_RTT_ConfigUpBuffer(VA_RTT_CHANNEL, "ViewAlyzer", NULL, 0, VA_RTT_MODE);
#endif
#endif
        VA_IS_INIT = true;

        // Emit sync marker first so host parsers can discard any preceding text/banners
        _va_send_bytes(VA_SYNC_MARKER, sizeof(VA_SYNC_MARKER));

        char info_buf[40];
        snprintf(info_buf, sizeof(info_buf), "CLK:%lu", _va_cpu_freq);
        _va_send_setup_packet(VA_SETUP_INFO, 0, info_buf);
        _va_send_setup_packet(VA_SETUP_ISR_MAP, VA_ISR_ID_SYSTICK, "SysTick");
#if (LOG_PENDSV == 1)
        _va_send_setup_packet(VA_SETUP_ISR_MAP, VA_ISR_ID_PENDSV, "PendSV");
#endif
#if (VA_TRACE_FREERTOS == 0)
        _va_send_setup_packet(VA_SETUP_CONFIG_FLAGS, 0, "NO_RTOS");
#endif
        VA_CS_EXIT();
    }

#endif // DWT_NOT_AVAILABLE check
#endif // VA_ENABLED check
