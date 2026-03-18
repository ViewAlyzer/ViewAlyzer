/**
 * @file viewalyzer_cobs.c
 * @brief COBS encoder implementation for ViewAlyzer UDP transport.
 *
 * Copyright (c) 2025 Free Radical Labs
 * See LICENSE for details.
 */

#include "viewalyzer_cobs.h"

size_t va_cobs_encode(const uint8_t *input, size_t in_len, uint8_t *output)
{
    size_t out_idx  = 0;
    size_t code_idx = out_idx++;   /* reserve space for first code byte */
    uint8_t code    = 1;

    for (size_t i = 0; i < in_len; i++)
    {
        if (input[i] != 0x00)
        {
            output[out_idx++] = input[i];
            code++;
        }
        else
        {
            output[code_idx] = code;
            code_idx = out_idx++;
            code = 1;
        }

        if (code == 0xFF)
        {
            output[code_idx] = code;
            code_idx = out_idx++;
            code = 1;
        }
    }

    output[code_idx] = code;
    output[out_idx++] = 0x00;   /* frame delimiter */

    return out_idx;
}
