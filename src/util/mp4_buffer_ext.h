/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef MP4_BUFFER_EXT_H
#define MP4_BUFFER_EXT_H

#include <tag_common/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int buffer_append_be16(dyn_buffer_t *buf, uint16_t val)
{
    uint8_t b[2] = { (uint8_t)(val >> 8), (uint8_t)val };
    return buffer_append(buf, b, 2);
}

static inline int buffer_append_be32(dyn_buffer_t *buf, uint32_t val)
{
    uint8_t b[4] = {
        (uint8_t)(val >> 24), (uint8_t)(val >> 16),
        (uint8_t)(val >> 8),  (uint8_t)val
    };
    return buffer_append(buf, b, 4);
}

static inline int buffer_append_be64(dyn_buffer_t *buf, uint64_t val)
{
    uint8_t b[8] = {
        (uint8_t)(val >> 56), (uint8_t)(val >> 48),
        (uint8_t)(val >> 40), (uint8_t)(val >> 32),
        (uint8_t)(val >> 24), (uint8_t)(val >> 16),
        (uint8_t)(val >> 8),  (uint8_t)val
    };
    return buffer_append(buf, b, 8);
}

#ifdef __cplusplus
}
#endif

#endif /* MP4_BUFFER_EXT_H */
