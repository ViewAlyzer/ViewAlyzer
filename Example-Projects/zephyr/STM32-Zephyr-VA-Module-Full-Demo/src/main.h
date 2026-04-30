/**
 * @file main.h
 * @brief Zephyr-compatible shim so ViewAlyzer.h can `#include "main.h"`
 *
 * Provides CMSIS Core definitions (CoreDebug, DWT, ITM, TPI, intrinsics)
 * and the STM32G4xx device header that ViewAlyzer.c accesses directly.
 */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stm32g4xx.h>
#include <stdio.h>

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
