/**
 * @file VA_Adapter_FreeRTOS.h
 * @brief ViewAlyzer FreeRTOS Adapter — declarations
 *
 * This header is included automatically by the core when
 * VA_RTOS_SELECT == VA_RTOS_FREERTOS.  User code does not need
 * to include it directly.
 *
 * Copyright (c) 2025 Free Radical Labs
 */

#ifndef VA_ADAPTER_FREERTOS_H
#define VA_ADAPTER_FREERTOS_H

#include "ViewAlyzer.h"

#if (VA_ENABLED == 1) && (VA_RTOS_SELECT == VA_RTOS_FREERTOS)

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The adapter interface functions are declared in ViewAlyzer.h under
 * the VA_HAS_RTOS guard.  This file provides the FreeRTOS-specific
 * implementations.  Nothing additional needs to be forward-declared here
 * beyond what ViewAlyzer.h already exposes.
 */

#ifdef __cplusplus
}
#endif

#endif /* VA_ENABLED && FREERTOS */
#endif /* VA_ADAPTER_FREERTOS_H */
