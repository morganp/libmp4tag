/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dynamic byte buffer with automatic growth */
typedef struct {
    uint8_t *data;
    size_t   size;      /* Bytes currently used */
    size_t   capacity;  /* Bytes allocated */
} dyn_buffer_t;

void   buffer_init(dyn_buffer_t *buf);
void   buffer_free(dyn_buffer_t *buf);
int    buffer_reserve(dyn_buffer_t *buf, size_t additional);
int    buffer_append(dyn_buffer_t *buf, const void *data, size_t size);
int    buffer_append_byte(dyn_buffer_t *buf, uint8_t byte);
int    buffer_append_zeros(dyn_buffer_t *buf, size_t count);
int    buffer_append_be16(dyn_buffer_t *buf, uint16_t val);
int    buffer_append_be32(dyn_buffer_t *buf, uint32_t val);
int    buffer_append_be64(dyn_buffer_t *buf, uint64_t val);

#ifdef __cplusplus
}
#endif

#endif /* BUFFER_H */
