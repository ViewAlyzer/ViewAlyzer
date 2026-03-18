/**
 * @file viewalyzer_udp.h
 * @brief ViewAlyzer UDP sender — core tracing over UDP with COBS framing.
 *
 * This is a lightweight, standalone C library for sending ViewAlyzer
 * trace data to the desktop app over UDP.  It has **no RTOS dependency**
 * and covers the generic "inner circle": int/float traces, string events,
 * toggles, and function entry/exit spans.
 *
 * For RTOS events (TaskSwitch, ISR, Semaphore, Mutex, Queue, etc.),
 * include "viewalyzer_udp_rtos.h" which extends this header.
 *
 * Usage:
 *   1. Call va_udp_init() with destination IP, port, and CPU frequency.
 *   2. Send setup packets: va_udp_send_trace_setup(), va_udp_send_function_map().
 *   3. In your loop, send events: va_udp_send_trace_int(), va_udp_send_trace_float(),
 *      va_udp_send_string(), va_udp_send_toggle(), va_udp_send_function().
 *
 * Copyright (c) 2025 Free Radical Labs
 * See LICENSE for details.
 */

#ifndef VIEWALYZER_UDP_H
#define VIEWALYZER_UDP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Core protocol constants ───────────────────────────────────────────── */

/* Core event type codes (lower 7 bits of type byte) */
#define VA_UDP_EVT_USER_TRACE       0x04  /* int32 value */
#define VA_UDP_EVT_USER_TOGGLE      0x0A
#define VA_UDP_EVT_USER_FUNCTION    0x0B
#define VA_UDP_EVT_STRING_EVENT     0x0D  /* variable-length string */
#define VA_UDP_EVT_FLOAT_TRACE      0x0E  /* IEEE 754 float value */

#define VA_UDP_FLAG_START           0x80  /* MSB: start/enter */

/* Core setup packet codes */
#define VA_UDP_SETUP_USER_TRACE        0x72
#define VA_UDP_SETUP_USER_FUNCTION_MAP 0x76
#define VA_UDP_SETUP_INFO              0x7F

/* Trace visualisation type hints (byte 2 of UserTrace setup) */
#define VA_UDP_TRACE_GRAPH      0
#define VA_UDP_TRACE_BAR        1
#define VA_UDP_TRACE_GAUGE      2
#define VA_UDP_TRACE_COUNTER    3
#define VA_UDP_TRACE_TABLE      4
#define VA_UDP_TRACE_HISTOGRAM  5
#define VA_UDP_TRACE_REGISTER   9

/* Maximum string length for string events */
#define VA_UDP_MAX_STRING_LEN   200

/* ── Context ───────────────────────────────────────────────────────────── */

/** Opaque handle — call va_udp_init() to populate. */
typedef struct va_udp_ctx va_udp_ctx_t;

/**
 * Initialise the UDP sender.
 *
 * @param dest_ip     Destination IP address (e.g. "127.0.0.1").
 * @param dest_port   Destination UDP port (e.g. 17200).
 * @param cpu_freq_hz CPU clock frequency in Hz (for the CLK setup packet).
 * @return            Heap-allocated context, or NULL on failure.
 *                    Free with va_udp_close().
 */
va_udp_ctx_t *va_udp_init(const char *dest_ip, uint16_t dest_port, uint32_t cpu_freq_hz);

/** Close the socket and free the context. */
void va_udp_close(va_udp_ctx_t *ctx);

/* ── Core setup packets ────────────────────────────────────────────────── */

/** Send the sync marker + CLK info.  Call once at startup. */
void va_udp_send_sync_and_clock(va_udp_ctx_t *ctx);

/** Register a user trace channel (id, display type, name). */
void va_udp_send_trace_setup(va_udp_ctx_t *ctx, uint8_t trace_id,
                             uint8_t trace_type, const char *name);

/** Register a user function name. */
void va_udp_send_function_map(va_udp_ctx_t *ctx, uint8_t func_id, const char *name);

/* ── Core event packets ────────────────────────────────────────────────── */

/** User trace with a signed 32-bit integer value. */
void va_udp_send_trace_int(va_udp_ctx_t *ctx, uint8_t trace_id,
                           uint64_t timestamp, int32_t value);

/** User trace with an IEEE 754 float value (FloatTrace 0x0E). */
void va_udp_send_trace_float(va_udp_ctx_t *ctx, uint8_t trace_id,
                             uint64_t timestamp, float value);

/** Boolean toggle state change. */
void va_udp_send_toggle(va_udp_ctx_t *ctx, uint8_t toggle_id,
                        uint64_t timestamp, bool state);

/** User function entry/exit. */
void va_udp_send_function(va_udp_ctx_t *ctx, uint8_t func_id,
                          bool is_entry, uint64_t timestamp);

/** Variable-length string message (max 200 chars). */
void va_udp_send_string(va_udp_ctx_t *ctx, uint8_t msg_id,
                        uint64_t timestamp, const char *message);

/* ── Low-level helpers (for advanced use / RTOS extension) ─────────────── */

/**
 * COBS-encode a raw packet and send it over the context's UDP socket.
 * Most users should use the typed functions above instead.
 */
void va_udp_send_raw_framed(va_udp_ctx_t *ctx, const uint8_t *pkt, size_t pkt_len);

/**
 * Send a name-mapping setup packet: [code][id][len][name...].
 * Used internally and by viewalyzer_udp_rtos.c.
 */
void va_udp_send_name_setup(va_udp_ctx_t *ctx, uint8_t code, uint8_t id, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* VIEWALYZER_UDP_H */
