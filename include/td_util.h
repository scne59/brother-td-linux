/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#ifndef TD_UTIL_H
#define TD_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TD_LOG_ERROR = 0,
    TD_LOG_WARN,
    TD_LOG_INFO,
    TD_LOG_DEBUG
} td_log_level_t;

extern int td_debug_enabled;

void td_log(td_log_level_t level, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

void *td_malloc(size_t size);
void *td_calloc(size_t count, size_t size);
void *td_realloc(void *ptr, size_t size);
bool td_parse_bool(const char *value, bool fallback);
int td_parse_int(const char *value, int fallback, int minimum, int maximum);
double td_parse_double(const char *value, double fallback, double minimum, double maximum);

int td_clampi(int value, int minimum, int maximum);

#endif /* TD_UTIL_H */
