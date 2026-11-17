/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "td_image.h"

static td_gray_image make_2x3(void)
{
    td_gray_image image = td_gray_alloc(2, 3, 0);

    /* 1 2
     * 3 4
     * 5 6 */
    image.pixels[0] = 1;
    image.pixels[1] = 2;
    image.pixels[2] = 3;
    image.pixels[3] = 4;
    image.pixels[4] = 5;
    image.pixels[5] = 6;
    return image;
}

static void check_rotation(void)
{
    td_gray_image source = make_2x3();
    td_gray_image rotated;

    assert(td_gray_rotate(&source, 90, &rotated) == 0);
    assert(rotated.width == 3);
    assert(rotated.height == 2);
    /* 5 3 1
     * 6 4 2 */
    assert(rotated.pixels[0] == 5 && rotated.pixels[1] == 3 && rotated.pixels[2] == 1);
    assert(rotated.pixels[3] == 6 && rotated.pixels[4] == 4 && rotated.pixels[5] == 2);
    td_gray_free(&rotated);

    assert(td_gray_rotate(&source, 180, &rotated) == 0);
    assert(rotated.width == 2);
    assert(rotated.height == 3);
    assert(rotated.pixels[0] == 6 && rotated.pixels[1] == 5);
    assert(rotated.pixels[2] == 4 && rotated.pixels[3] == 3);
    assert(rotated.pixels[4] == 2 && rotated.pixels[5] == 1);
    td_gray_free(&rotated);

    assert(td_gray_rotate(&source, 270, &rotated) == 0);
    assert(rotated.width == 3);
    assert(rotated.height == 2);
    /* 2 4 6
     * 1 3 5 */
    assert(rotated.pixels[0] == 2 && rotated.pixels[1] == 4 && rotated.pixels[2] == 6);
    assert(rotated.pixels[3] == 1 && rotated.pixels[4] == 3 && rotated.pixels[5] == 5);
    td_gray_free(&rotated);

    td_gray_free(&source);
}

static void check_mirror(void)
{
    td_gray_image image = make_2x3();

    assert(td_gray_mirror(&image) == 0);
    assert(image.pixels[0] == 2 && image.pixels[1] == 1);
    assert(image.pixels[2] == 4 && image.pixels[3] == 3);
    assert(image.pixels[4] == 6 && image.pixels[5] == 5);
    td_gray_free(&image);
}

static void check_place(void)
{
    td_gray_image source = td_gray_alloc(2, 2, 255);
    td_gray_image result;

    source.pixels[0] = 0;

    /* Fit: source is smaller than the target, so it must not be enlarged. */
    assert(td_gray_place(&source, 4, 4, TD_SCALE_FIT, TD_ALIGN_START,
                         TD_ALIGN_START, &result) == 0);
    assert(result.width == 4 && result.height == 4);
    assert(result.pixels[0] == 0);
    assert(result.pixels[1] == 255);
    assert(result.pixels[5] == 255);
    td_gray_free(&result);

    /* FitEnlarge: 2x2 -> 4x4, nearest/bilinear yields the same corner. */
    assert(td_gray_place(&source, 4, 4, TD_SCALE_FIT_ENLARGE, TD_ALIGN_START,
                         TD_ALIGN_START, &result) == 0);
    assert(result.pixels[0] == 0);
    assert(result.pixels[15] == 255);
    td_gray_free(&result);

    /* None: 2x2 in a 4x4 target, centered. */
    assert(td_gray_place(&source, 4, 4, TD_SCALE_NONE, TD_ALIGN_CENTER,
                         TD_ALIGN_CENTER, &result) == 0);
    assert(result.pixels[1 * 4 + 1] == 0);
    assert(result.pixels[0] == 255);
    td_gray_free(&result);

    /* Crop: 2x2 source upscaled to cover 4x4. */
    assert(td_gray_place(&source, 4, 4, TD_SCALE_CROP, TD_ALIGN_CENTER,
                         TD_ALIGN_CENTER, &result) == 0);
    assert(result.width == 4 && result.height == 4);
    td_gray_free(&result);

    td_gray_free(&source);
}

static void check_bitmap(void)
{
    td_bitmap bitmap = td_bitmap_alloc(10, 6);

    assert(bitmap.stride == 2);
    td_bitmap_set(&bitmap, 0, 0, true);
    td_bitmap_set(&bitmap, 9, 0, true);
    assert(td_bitmap_get(&bitmap, 0, 0));
    assert(td_bitmap_get(&bitmap, 9, 0));
    assert(!td_bitmap_get(&bitmap, 1, 0));
    assert(bitmap.bits[0] == 0x80);
    assert(bitmap.bits[1] == 0x40);

    td_bitmap_set(&bitmap, 0, 0, false);
    td_bitmap_set(&bitmap, 9, 0, false);
    td_bitmap_set(&bitmap, 0, 2, true);
    td_bitmap_set(&bitmap, 9, 4, true);
    assert(td_bitmap_trim(&bitmap, 1, false));
    assert(bitmap.height == 3); /* rows 2..4 */
    assert(td_bitmap_get(&bitmap, 0, 0));
    assert(td_bitmap_get(&bitmap, 9, 2));
    assert(!td_bitmap_get(&bitmap, 0, 1));

    td_bitmap_free(&bitmap);
}

int main(void)
{
    check_rotation();
    check_mirror();
    check_place();
    check_bitmap();
    puts("test_image: OK");
    return 0;
}
