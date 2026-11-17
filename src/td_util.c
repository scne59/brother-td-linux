/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include "td_util.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int td_debug_enabled = 0;

static void vlog_line(td_log_level_t level, const char *fmt, va_list ap)
{
    const char *prefix;

    switch (level) {
    case TD_LOG_ERROR:
        prefix = "ERROR: ";
        break;
    case TD_LOG_WARN:
        prefix = "WARNING: ";
        break;
    case TD_LOG_INFO:
        prefix = "INFO: ";
        break;
    case TD_LOG_DEBUG:
    default:
        prefix = "DEBUG: ";
        break;
    }

    if (level == TD_LOG_DEBUG && !td_debug_enabled)
        return;

    fputs(prefix, stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
}

void td_log(td_log_level_t level, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vlog_line(level, fmt, ap);
    va_end(ap);
}

static void *alloc_or_die(size_t size)
{
    void *ptr = malloc(size ? size : 1);

    if (ptr == NULL) {
        td_log(TD_LOG_ERROR, "out of memory allocating %zu bytes", size);
        abort();
    }
    return ptr;
}

void *td_malloc(size_t size)
{
    return alloc_or_die(size);
}

void *td_calloc(size_t count, size_t size)
{
    void *ptr;

    if (count != 0 && size > SIZE_MAX / count) {
        td_log(TD_LOG_ERROR, "allocation size overflow");
        abort();
    }

    ptr = calloc(count ? count : 1, size ? size : 1);
    if (ptr == NULL) {
        td_log(TD_LOG_ERROR, "out of memory allocating %zu bytes", count * size);
        abort();
    }
    return ptr;
}

void *td_realloc(void *ptr, size_t size)
{
    void *result = realloc(ptr, size ? size : 1);

    if (result == NULL) {
        td_log(TD_LOG_ERROR, "out of memory reallocating %zu bytes", size);
        abort();
    }
    return result;
}

bool td_parse_bool(const char *value, bool fallback)
{
    if (value == NULL || value[0] == '\0')
        return fallback;

    if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0 || strcasecmp(value, "1") == 0)
        return true;

    if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 ||
        strcasecmp(value, "off") == 0 || strcasecmp(value, "0") == 0)
        return false;

    return fallback;
}

int td_parse_int(const char *value, int fallback, int minimum, int maximum)
{
    char *end = NULL;
    long parsed;

    if (value == NULL || value[0] == '\0')
        return fallback;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value)
        return fallback;

    while (*end != '\0' && isspace((unsigned char)*end))
        end++;
    if (*end != '\0')
        return fallback;

    if (parsed < minimum)
        parsed = minimum;
    if (parsed > maximum)
        parsed = maximum;

    return (int)parsed;
}

double td_parse_double(const char *value, double fallback, double minimum, double maximum)
{
    char *end = NULL;
    double parsed;

    if (value == NULL || value[0] == '\0')
        return fallback;

    errno = 0;
    parsed = strtod(value, &end);
    if (errno != 0 || end == value || !isfinite(parsed))
        return fallback;

    while (*end != '\0' && isspace((unsigned char)*end))
        end++;
    if (*end != '\0')
        return fallback;

    if (parsed < minimum)
        parsed = minimum;
    if (parsed > maximum)
        parsed = maximum;

    return parsed;
}

int td_clampi(int value, int minimum, int maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}
