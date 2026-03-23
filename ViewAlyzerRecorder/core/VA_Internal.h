/**
 * @file VA_Internal.h
 * @brief ViewAlyzer Internal API — shared between the core engine and RTOS adapters.
 *
 * This header is NOT part of the public user API.  It exposes the low-level
 * packet helpers, timestamp function, and data structures that RTOS-specific
 * adapter files (VA_Adapter_FreeRTOS.c, VA_Adapter_Zephyr.c, …) need to emit
 * binary trace events through the common transport layer.
 *
 * Copyright (c) 2025 Free Radical Labs
 */

#ifndef VA_INTERNAL_H
#define VA_INTERNAL_H

#include "ViewAlyzer.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#if (VA_ENABLED == 1)

/* ── Critical-section helpers (preserve PRIMASK) ───────────────── */
#if VA_ALLOWED_TO_DISABLE_INTERRUPTS
#define VA_CS_ENTER()                        \
    uint32_t __va_primask = __get_PRIMASK(); \
    __disable_irq()
#define VA_CS_EXIT() __set_PRIMASK(__va_primask)
#else
#define VA_CS_ENTER() ((void)0)
#define VA_CS_EXIT()  ((void)0)
#endif

#define VA_UNUSED(x) (void)(x)

/* ── Task / object map entry (RTOS-agnostic) ───────────────────── */
typedef struct {
    void    *handle;                        /* Generic pointer to TCB / thread struct */
    uint8_t  id;                            /* ViewAlyzer internal ID                 */
    char     name[VA_MAX_TASK_NAME_LEN];
    bool     active;
    void    *last_notifier;                 /* For notification tracking              */
    /* Stack monitoring – adapters populate these during task create */
    void    *pxStack;
    void    *pxEndOfStack;
    uint32_t uxPriority;
    uint32_t uxBasePriority;
    uint32_t ulStackDepth;
} VA_TaskMapEntry_t;

/* ── Queue / sync-object map entry (RTOS-agnostic) ─────────────── */
typedef struct {
    void               *handle;
    uint8_t             id;
    char                name[VA_MAX_TASK_NAME_LEN];
    VA_QueueObjectType_t type;
    bool                active;
} VA_QueueObjectMapEntry_t;

/* ── Shared global state (defined in ViewAlyzer.c) ─────────────── */
extern volatile bool      VA_IS_INIT;
extern VA_TaskMapEntry_t  taskMap[VA_MAX_TASKS];
extern uint8_t            next_task_id;
extern volatile uint32_t  notificationValue;

/* Globals set during task creation (adapter populates, core stores) */
extern volatile void     *g_task_pxStack;
extern volatile void     *g_task_pxEndOfStack;
extern volatile uint32_t  g_task_uxPriority;
extern volatile uint32_t  g_task_uxBasePriority;
extern volatile uint32_t  g_task_ulStackDepth;

#if (VA_RTOS_SELECT != VA_RTOS_NONE)
extern VA_QueueObjectMapEntry_t queueObjectMap[VA_MAX_SYNC_OBJECTS];
extern uint8_t                  next_queue_object_id;
#endif

/* ── Packet emission helpers (defined in ViewAlyzer.c) ──────────── */
uint64_t _va_get_timestamp(void);

void _va_emit_packet(const uint8_t *data, uint32_t length);
void _va_send_event_packet(uint8_t type_byte, uint8_t id, uint64_t timestamp);
void _va_send_setup_packet(uint8_t setupCode, uint8_t id, const char *name);
void _va_send_user_setup_packet(uint8_t id, uint8_t type, const char *name);
void _va_send_user_event_packet(uint8_t id, int32_t value, uint64_t timestamp);
void _va_send_float_event_packet(uint8_t id, float value, uint64_t timestamp);
void _va_send_user_toggle_event_packet(uint8_t id, VA_UserToggleState_t state, uint64_t timestamp);
void _va_send_notification_event_packet(uint8_t type_byte, uint8_t id, uint8_t other_id, uint32_t value, uint64_t timestamp);
void _va_send_mutex_contention_packet(uint8_t mutex_id, uint8_t waiting_task_id, uint8_t holder_task_id, uint64_t timestamp);
void _va_send_task_create_packet(uint8_t id, uint64_t timestamp, uint32_t priority, uint32_t base_priority, uint32_t stack_size);
void _va_send_stack_usage_packet(uint8_t id, uint64_t timestamp, uint32_t stack_used, uint32_t stack_total);
void _va_send_data_event_packet(uint8_t type_byte, uint8_t id, uint32_t value, uint64_t timestamp);
void _va_send_heap_setup_packet(uint8_t id, const char *name, uint32_t totalSize);

/* ── Generic map helpers (defined in ViewAlyzer.c) ──────────────── */
uint8_t _va_find_task_id(void *handle);
int     _va_find_task_index(void *handle);
uint8_t _va_assign_task_id(void *handle, const char *name);

#if (VA_RTOS_SELECT != VA_RTOS_NONE)
uint8_t              _va_find_queue_object_id(void *handle);
uint8_t              _va_assign_queue_object_id(void *handle, const char *name, VA_QueueObjectType_t type);
const char          *_va_get_object_type_name(VA_QueueObjectType_t type);
uint8_t              _va_get_setup_packet_type(VA_QueueObjectType_t type);
VA_QueueObjectType_t _va_get_stored_queue_object_type(void *handle);
#endif

#endif /* VA_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* VA_INTERNAL_H */
