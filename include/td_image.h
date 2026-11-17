/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#ifndef TD_IMAGE_H
#define TD_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TD_SCALE_FIT = 0,       /* shrink to fit, do not enlarge */
    TD_SCALE_FIT_ENLARGE,   /* scale to fit in either direction */
    TD_SCALE_CROP,          /* fill the target and crop */
    TD_SCALE_NONE           /* actual size, crop or pad */
} td_scale_t;

typedef enum {
    TD_ALIGN_START = 0,
    TD_ALIGN_CENTER = 1,
    TD_ALIGN_END = 2
} td_align_t;

/* 8-bit grayscale, 0 = black and 255 = white. */
typedef struct {
    int width;
    int height;
    uint8_t *pixels;        /* width * height bytes, row-major */
} td_gray_image;

/* 1-bit image, 1 = black.  The most significant bit of a byte is the
 * leftmost pixel in that byte (conventional raster order). */
typedef struct {
    int width;
    int height;
    int stride;             /* (width + 7) / 8 */
    uint8_t *bits;
} td_bitmap;

td_gray_image td_gray_alloc(int width, int height, uint8_t fill);
td_gray_image td_gray_clone(const td_gray_image *source);
void td_gray_free(td_gray_image *image);

int td_gray_rotate(const td_gray_image *source, int degrees, td_gray_image *result);
int td_gray_mirror(td_gray_image *image);
int td_gray_place(const td_gray_image *source, int target_width, int target_height,
                  td_scale_t scaling, td_align_t align_x, td_align_t align_y,
                  td_gray_image *result);

td_bitmap td_bitmap_alloc(int width, int height);
void td_bitmap_free(td_bitmap *bitmap);
bool td_bitmap_get(const td_bitmap *bitmap, int x, int y);
void td_bitmap_set(td_bitmap *bitmap, int x, int y, bool black);

/* Remove blank leading and trailing rows.  The result is at least
 * min_height rows (unless the bitmap itself is shorter).  When inverted is
 * true, a blank row is all ones (because the line emitter will invert it).
 * Returns true if the bitmap was modified. */
bool td_bitmap_trim(td_bitmap *bitmap, int min_height, bool inverted);

#endif /* TD_IMAGE_H */
