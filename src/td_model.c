/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include "td_models.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>

static void copy_model_id(char *dst, size_t dst_size, const char *src)
{
    size_t i;
    size_t out = 0;

    if (dst_size == 0)
        return;

    /* Ignore a leading "Brother" and punctuation so that values such as
     * "Brother TD-4410D" or "td4410d" match the canonical model id.  Model
     * names are unique when only their alphanumeric characters are compared. */
    while (*src != '\0' && isspace((unsigned char)*src))
        src++;
    if (strncasecmp(src, "brother", 7) == 0) {
        src += 7;
        while (*src != '\0' && (isspace((unsigned char)*src) || *src == '-'))
            src++;
    }

    for (i = 0; src[i] != '\0' && out + 1 < dst_size; i++) {
        unsigned char ch = (unsigned char)src[i];

        if (isalnum(ch))
            dst[out++] = (char)tolower(ch);
    }

    dst[out] = '\0';
}

const td_model_t *td_model_find(const char *id)
{
    char wanted[64];
    size_t i;

    if (id == NULL || id[0] == '\0')
        return NULL;

    copy_model_id(wanted, sizeof(wanted), id);
    if (wanted[0] == '\0')
        return NULL;

    for (i = 0; i < td_num_models; i++) {
        char candidate[64];

        copy_model_id(candidate, sizeof(candidate), td_models[i].id);
        if (strcmp(wanted, candidate) == 0)
            return &td_models[i];
    }

    return NULL;
}

const td_model_t *td_model_for_dpi(int dpi)
{
    size_t i;

    for (i = 0; i < td_num_models; i++) {
        if (td_models[i].dpi == dpi)
            return &td_models[i];
    }

    return NULL;
}
