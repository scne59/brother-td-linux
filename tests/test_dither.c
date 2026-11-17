/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "td_dither.h"
#include "td_image.h"

static int count_black(const td_bitmap *bitmap)
{
    int x;
    int y;
    int count = 0;

    for (y = 0; y < bitmap->height; y++)
        for (x = 0; x < bitmap->width; x++)
            if (td_bitmap_get(bitmap, x, y))
                count++;
    return count;
}

static void check_solid(void)
{
    td_gray_image image = td_gray_alloc(32, 32, 255);
    td_bitmap bitmap;

    assert(td_dither_image(&image, TD_DITHER_FLOYD, 128, &bitmap) == 0);
    assert(bitmap.width == 32 && bitmap.height == 32);
    assert(count_black(&bitmap) == 0);
    td_bitmap_free(&bitmap);

    memset(image.pixels, 0, 32 * 32);
    assert(td_dither_image(&image, TD_DITHER_FLOYD, 128, &bitmap) == 0);
    assert(count_black(&bitmap) == 32 * 32);
    td_bitmap_free(&bitmap);

    td_gray_free(&image);
}

static void check_gradient(void)
{
    td_gray_image image = td_gray_alloc(64, 16, 255);
    td_bitmap bitmap;
    int x;
    int y;

    for (y = 0; y < image.height; y++)
        for (x = 0; x < image.width; x++)
            image.pixels[y * image.width + x] = (uint8_t)(x * 255 / (image.width - 1));

    assert(td_dither_image(&image, TD_DITHER_FLOYD, 128, &bitmap) == 0);
    assert(count_black(&bitmap) > 0);
    assert(count_black(&bitmap) < 64 * 16);
    td_bitmap_free(&bitmap);

    assert(td_dither_image(&image, TD_DITHER_BAYER4, 128, &bitmap) == 0);
    assert(count_black(&bitmap) > 0);
    td_bitmap_free(&bitmap);

    assert(td_dither_image(&image, TD_DITHER_THRESHOLD, 128, &bitmap) == 0);
    assert(count_black(&bitmap) == 32 * 16);
    td_bitmap_free(&bitmap);

    td_gray_free(&image);
}

static void check_names(void)
{
    assert(td_dither_parse("floyd", TD_DITHER_THRESHOLD) == TD_DITHER_FLOYD);
    assert(td_dither_parse("Floyd-Steinberg", TD_DITHER_THRESHOLD) == TD_DITHER_FLOYD);
    assert(td_dither_parse("jarvis", TD_DITHER_THRESHOLD) == TD_DITHER_JARVIS);
    assert(td_dither_parse("stucki", TD_DITHER_THRESHOLD) == TD_DITHER_STUCKI);
    assert(td_dither_parse("atkinson", TD_DITHER_THRESHOLD) == TD_DITHER_ATKINSON);
    assert(td_dither_parse("sierra-lite", TD_DITHER_THRESHOLD) == TD_DITHER_SIERRA_LITE);
    assert(td_dither_parse("burkes", TD_DITHER_THRESHOLD) == TD_DITHER_BURKES);
    assert(td_dither_parse("bayer8", TD_DITHER_THRESHOLD) == TD_DITHER_BAYER8);
    assert(td_dither_parse("none", TD_DITHER_FLOYD) == TD_DITHER_THRESHOLD);
    assert(strcmp(td_dither_name(TD_DITHER_FLOYD), "Floyd") == 0);
}

int main(void)
{
    check_solid();
    check_gradient();
    check_names();
    puts("test_dither: OK");
    return 0;
}
