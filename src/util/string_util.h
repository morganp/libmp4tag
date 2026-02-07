/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef STRING_UTIL_H
#define STRING_UTIL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Duplicate a string (portable, does not rely on POSIX strdup) */
char *str_dup(const char *s);

/* Case-insensitive ASCII comparison. Returns 0 if equal. */
int str_casecmp(const char *a, const char *b);

/* Safe string copy. Returns 0 on success, -1 if truncated. */
int str_copy(char *dst, size_t dst_size, const char *src);

#ifdef __cplusplus
}
#endif

#endif /* STRING_UTIL_H */
