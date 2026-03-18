/**
 * @file viewalyzer_udp.c
 * @brief ViewAlyzer UDP sender — core tracing implementation.
 *
 * Copyright (c) 2025 Free Radical Labs
 * See LICENSE for details.
 */

#include "viewalyzer_udp.h"
#include "viewalyzer_cobs.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Platform socket headers ──────────────────────────────────────────── */
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET va_socket_t;
  #define VA_INVALID_SOCKET INVALID_SOCKET
#else
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int va_socket_t;
  #define VA_INVALID_SOCKET (-1)
#endif

/* ── Internal context ─────────────────────────────────────────────────── */

struct va_udp_ctx
{
    va_socket_t        sock;
    struct sockaddr_in dest;
    uint32_t           cpu_freq_hz;
};

/* ── Sync marker ──────────────────────────────────────────────────────── */

static const uint8_t VA_SYNC_MARKER[12] = {
    0x56, 0x41, 0x5A, 0x01, 0x53, 0x59, 0x4E, 0x43,
    0x30, 0x31, 0xAA, 0x55
};

/* ── Helpers ──────────────────────────────────────────────────────────── */

static void write_i32_le(uint8_t *buf, int32_t v)  { memcpy(buf, &v, 4); }
static void write_u64_le(uint8_t *buf, uint64_t v) { memcpy(buf, &v, 8); }
static void write_f32_le(uint8_t *buf, float v)    { memcpy(buf, &v, 4); }

static void va_udp_sendto(va_udp_ctx_t *ctx, const uint8_t *data, size_t len)
{
    sendto((int)ctx->sock, (const char *)data, (int)len, 0,
           (struct sockaddr *)&ctx->dest, sizeof(ctx->dest));
}

/* ── Public: init / close ─────────────────────────────────────────────── */

va_udp_ctx_t *va_udp_init(const char *dest_ip, uint16_t dest_port, uint32_t cpu_freq_hz)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return NULL;
#endif

    va_udp_ctx_t *ctx = (va_udp_ctx_t *)calloc(1, sizeof(va_udp_ctx_t));
    if (!ctx) return NULL;

    ctx->cpu_freq_hz = cpu_freq_hz;

    ctx->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->sock == VA_INVALID_SOCKET)
    {
        free(ctx);
        return NULL;
    }

    memset(&ctx->dest, 0, sizeof(ctx->dest));
    ctx->dest.sin_family = AF_INET;
    ctx->dest.sin_port   = htons(dest_port);
    inet_pton(AF_INET, dest_ip, &ctx->dest.sin_addr);

    return ctx;
}

void va_udp_close(va_udp_ctx_t *ctx)
{
    if (!ctx) return;

#ifdef _WIN32
    closesocket(ctx->sock);
    WSACleanup();
#else
    close(ctx->sock);
#endif

    free(ctx);
}

/* ── Public: send raw framed ──────────────────────────────────────────── */

void va_udp_send_raw_framed(va_udp_ctx_t *ctx, const uint8_t *pkt, size_t pkt_len)
{
    uint8_t buf[280];
    size_t encoded_len = va_cobs_encode(pkt, pkt_len, buf);
    va_udp_sendto(ctx, buf, encoded_len);
}

/* ── Public: name setup helper (used by core + rtos extension) ────────── */

void va_udp_send_name_setup(va_udp_ctx_t *ctx, uint8_t code, uint8_t id, const char *name)
{
    uint8_t len = (uint8_t)strlen(name);
    uint8_t pkt[3 + 255];
    pkt[0] = code;
    pkt[1] = id;
    pkt[2] = len;
    memcpy(&pkt[3], name, len);
    va_udp_send_raw_framed(ctx, pkt, 3 + len);
}

/* ── Public: core setup packets ───────────────────────────────────────── */

void va_udp_send_sync_and_clock(va_udp_ctx_t *ctx)
{
    va_udp_send_raw_framed(ctx, VA_SYNC_MARKER, sizeof(VA_SYNC_MARKER));

    char payload[32];
    int n = snprintf(payload, sizeof(payload), "CLK:%u", (unsigned)ctx->cpu_freq_hz);
    uint8_t pkt[3 + 32];
    pkt[0] = VA_UDP_SETUP_INFO;
    pkt[1] = 0x00;
    pkt[2] = (uint8_t)n;
    memcpy(&pkt[3], payload, (size_t)n);
    va_udp_send_raw_framed(ctx, pkt, 3 + (size_t)n);
}

void va_udp_send_trace_setup(va_udp_ctx_t *ctx, uint8_t trace_id,
                             uint8_t trace_type, const char *name)
{
    uint8_t len = (uint8_t)strlen(name);
    uint8_t pkt[4 + 255];
    pkt[0] = VA_UDP_SETUP_USER_TRACE;
    pkt[1] = trace_id;
    pkt[2] = trace_type;
    pkt[3] = len;
    memcpy(&pkt[4], name, len);
    va_udp_send_raw_framed(ctx, pkt, 4 + len);
}

void va_udp_send_function_map(va_udp_ctx_t *ctx, uint8_t func_id, const char *name)
{
    va_udp_send_name_setup(ctx, VA_UDP_SETUP_USER_FUNCTION_MAP, func_id, name);
}

/* ── Public: core event packets ───────────────────────────────────────── */

void va_udp_send_trace_int(va_udp_ctx_t *ctx, uint8_t trace_id,
                           uint64_t timestamp, int32_t value)
{
    uint8_t pkt[14];
    pkt[0] = VA_UDP_EVT_USER_TRACE | VA_UDP_FLAG_START;
    pkt[1] = trace_id;
    write_u64_le(&pkt[2],  timestamp);
    write_i32_le(&pkt[10], value);
    va_udp_send_raw_framed(ctx, pkt, 14);
}

void va_udp_send_trace_float(va_udp_ctx_t *ctx, uint8_t trace_id,
                             uint64_t timestamp, float value)
{
    uint8_t pkt[14];
    pkt[0] = VA_UDP_EVT_FLOAT_TRACE | VA_UDP_FLAG_START;
    pkt[1] = trace_id;
    write_u64_le(&pkt[2],  timestamp);
    write_f32_le(&pkt[10], value);
    va_udp_send_raw_framed(ctx, pkt, 14);
}

void va_udp_send_toggle(va_udp_ctx_t *ctx, uint8_t toggle_id,
                        uint64_t timestamp, bool state)
{
    uint8_t pkt[11];
    pkt[0]  = VA_UDP_EVT_USER_TOGGLE;
    pkt[1]  = toggle_id;
    write_u64_le(&pkt[2], timestamp);
    pkt[10] = state ? 1 : 0;
    va_udp_send_raw_framed(ctx, pkt, 11);
}

void va_udp_send_function(va_udp_ctx_t *ctx, uint8_t func_id,
                          bool is_entry, uint64_t timestamp)
{
    uint8_t pkt[10];
    pkt[0] = VA_UDP_EVT_USER_FUNCTION | (is_entry ? VA_UDP_FLAG_START : 0);
    pkt[1] = func_id;
    write_u64_le(&pkt[2], timestamp);
    va_udp_send_raw_framed(ctx, pkt, 10);
}

void va_udp_send_string(va_udp_ctx_t *ctx, uint8_t msg_id,
                        uint64_t timestamp, const char *message)
{
    size_t msg_len = strlen(message);
    if (msg_len > VA_UDP_MAX_STRING_LEN)
        msg_len = VA_UDP_MAX_STRING_LEN;

    uint8_t pkt[11 + VA_UDP_MAX_STRING_LEN];
    pkt[0]  = VA_UDP_EVT_STRING_EVENT;
    pkt[1]  = msg_id;
    write_u64_le(&pkt[2], timestamp);
    pkt[10] = (uint8_t)msg_len;
    memcpy(&pkt[11], message, msg_len);
    va_udp_send_raw_framed(ctx, pkt, 11 + msg_len);
}
