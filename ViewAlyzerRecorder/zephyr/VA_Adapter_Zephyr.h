/**
 * @file VA_Adapter_Zephyr.h
 * @brief ViewAlyzer Zephyr Adapter — declarations
 *
 * Bridges Zephyr's CONFIG_TRACING_USER weak callbacks to the ViewAlyzer
 * binary recorder protocol.  Include this header from your application.
 *
 * Traced events:
 *   - Thread create          → registers task name + ID
 *   - Thread switch in / out → native VA_EVENT_TASK_SWITCH events
 *   - ISR enter / exit       → VA_LogISRStart / VA_LogISREnd
 *
 * Copyright (c) 2025 Free Radical Labs
 */

#ifndef VA_ADAPTER_ZEPHYR_H
#define VA_ADAPTER_ZEPHYR_H

#include "ViewAlyzer.h"

#if (VA_ENABLED == 1) && (VA_RTOS_SELECT == VA_RTOS_ZEPHYR)

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Emit ViewAlyzer setup packets for all threads that were created
 *        before VA_Init() ran (static K_THREAD_DEFINE threads, idle, etc.).
 *
 * Call once after VA_Init() so the host knows the task-name → id mapping.
 */
void VA_Zephyr_RegisterExistingThreads(void);

#ifdef __cplusplus
}
#endif

#endif /* VA_ENABLED && ZEPHYR */
#endif /* VA_ADAPTER_ZEPHYR_H */
