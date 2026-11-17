/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#ifndef TD_DITHER_H
#define TD_DITHER_H

#include "td_image.h"

typedef enum {
    TD_DITHER_FLOYD = 0,
    TD_DITHER_JARVIS,
    TD_DITHER_STUCKI,
    TD_DITHER_ATKINSON,
    TD_DITHER_SIERRA,
    TD_DITHER_SIERRA_LITE,
    TD_DITHER_BURKES,
    TD_DITHER_BAYER4,
    TD_DITHER_BAYER8,
    TD_DITHER_THRESHOLD
} td_dither_t;

const char *td_dither_name(td_dither_t dither);
td_dither_t td_dither_parse(const char *value, td_dither_t fallback);
int td_dither_image(const td_gray_image *source, td_dither_t dither,
                    int threshold, td_bitmap *result);

#endif /* TD_DITHER_H */
