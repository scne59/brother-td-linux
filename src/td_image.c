/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include "td_image.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "td_util.h"

td_gray_image td_gray_alloc(int width, int height, uint8_t fill)
{
    td_gray_image image;

    if (width < 0)
        width = 0;
    if (height < 0)
        height = 0;

    image.width = width;
    image.height = height;
    image.pixels = td_malloc((size_t)width * (size_t)height);
    if (image.pixels != NULL && width > 0 && height > 0)
        memset(image.pixels, fill, (size_t)width * (size_t)height);
    return image;
}

td_gray_image td_gray_clone(const td_gray_image *source)
{
    td_gray_image copy;

    if (source == NULL || source->width <= 0 || source->height <= 0) {
        copy.width = 0;
        copy.height = 0;
        copy.pixels = td_malloc(1);
        return copy;
    }

    copy = td_gray_alloc(source->width, source->height, 255);
    memcpy(copy.pixels, source->pixels,
           (size_t)source->width * (size_t)source->height);
    return copy;
}

void td_gray_free(td_gray_image *image)
{
    if (image == NULL)
        return;
    free(image->pixels);
    image->pixels = NULL;
    image->width = 0;
    image->height = 0;
}

int td_gray_rotate(const td_gray_image *source, int degrees, td_gray_image *result)
{
    int width;
    int height;
    int x;
    int y;

    if (source == NULL || result == NULL || source->width <= 0 || source->height <= 0)
        return -1;

    degrees %= 360;
    if (degrees < 0)
        degrees += 360;

    if (degrees == 0) {
        *result = td_gray_clone(source);
        return 0;
    }

    width = source->width;
    height = source->height;

    if (degrees == 90 || degrees == 270) {
        result->width = height;
        result->height = width;
    } else if (degrees == 180) {
        result->width = width;
        result->height = height;
    } else {
        return -1;
    }

    result->pixels = td_malloc((size_t)result->width * (size_t)result->height);

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int dx;
            int dy;
            uint8_t value = source->pixels[(size_t)y * (size_t)width + (size_t)x];

            switch (degrees) {
            case 90: /* clockwise */
                dx = height - 1 - y;
                dy = x;
                break;
            case 180:
                dx = width - 1 - x;
                dy = height - 1 - y;
                break;
            case 270: /* counter-clockwise */
                dx = y;
                dy = width - 1 - x;
                break;
            default:
                return -1;
            }

            result->pixels[(size_t)dy * (size_t)result->width + (size_t)dx] = value;
        }
    }

    return 0;
}

int td_gray_mirror(td_gray_image *image)
{
    int y;

    if (image == NULL || image->width <= 1 || image->height <= 0)
        return 0;

    for (y = 0; y < image->height; y++) {
        uint8_t *row = image->pixels + (size_t)y * (size_t)image->width;
        int left = 0;
        int right = image->width - 1;

        while (left < right) {
            uint8_t tmp = row[left];

            row[left++] = row[right];
            row[right--] = tmp;
        }
    }

    return 0;
}

static td_gray_image resize_area(const td_gray_image *source, int dst_width, int dst_height)
{
    td_gray_image result = td_gray_alloc(dst_width, dst_height, 255);
    int x;
    int y;

    for (y = 0; y < dst_height; y++) {
        double sy0 = (double)y * (double)source->height / (double)dst_height;
        double sy1 = (double)(y + 1) * (double)source->height / (double)dst_height;
        int iy0 = (int)floor(sy0);
        int iy1 = (int)ceil(sy1);

        if (iy0 < 0)
            iy0 = 0;
        if (iy1 > source->height)
            iy1 = source->height;
        if (iy1 <= iy0)
            iy1 = iy0 + 1;
        if (iy1 > source->height)
            iy1 = source->height;

        for (x = 0; x < dst_width; x++) {
            double sx0 = (double)x * (double)source->width / (double)dst_width;
            double sx1 = (double)(x + 1) * (double)source->width / (double)dst_width;
            int ix0 = (int)floor(sx0);
            int ix1 = (int)ceil(sx1);
            unsigned long long sum = 0;
            unsigned long long count = 0;
            int ix;
            int iy;

            if (ix0 < 0)
                ix0 = 0;
            if (ix1 > source->width)
                ix1 = source->width;
            if (ix1 <= ix0)
                ix1 = ix0 + 1;
            if (ix1 > source->width)
                ix1 = source->width;

            for (iy = iy0; iy < iy1; iy++) {
                const uint8_t *row = source->pixels + (size_t)iy * (size_t)source->width;

                for (ix = ix0; ix < ix1; ix++) {
                    sum += row[ix];
                    count++;
                }
            }

            result.pixels[(size_t)y * (size_t)dst_width + (size_t)x] =
                count ? (uint8_t)((sum + count / 2) / count) : 255;
        }
    }

    return result;
}

static td_gray_image resize_bilinear(const td_gray_image *source, int dst_width, int dst_height)
{
    td_gray_image result = td_gray_alloc(dst_width, dst_height, 255);
    int x;
    int y;

    for (y = 0; y < dst_height; y++) {
        double sy = ((double)y + 0.5) * (double)source->height / (double)dst_height - 0.5;
        int y0;
        int y1;
        double fy;

        if (sy < 0.0)
            sy = 0.0;
        if (sy > (double)(source->height - 1))
            sy = (double)(source->height - 1);

        y0 = (int)floor(sy);
        y1 = y0 + 1;
        if (y1 >= source->height)
            y1 = source->height - 1;
        fy = sy - (double)y0;

        for (x = 0; x < dst_width; x++) {
            double sx = ((double)x + 0.5) * (double)source->width / (double)dst_width - 0.5;
            int x0;
            int x1;
            double fx;
            double top;
            double bottom;
            double value;

            if (sx < 0.0)
                sx = 0.0;
            if (sx > (double)(source->width - 1))
                sx = (double)(source->width - 1);

            x0 = (int)floor(sx);
            x1 = x0 + 1;
            if (x1 >= source->width)
                x1 = source->width - 1;
            fx = sx - (double)x0;

            top = (1.0 - fx) * (double)source->pixels[(size_t)y0 * (size_t)source->width + (size_t)x0] +
                  fx * (double)source->pixels[(size_t)y0 * (size_t)source->width + (size_t)x1];
            bottom = (1.0 - fx) * (double)source->pixels[(size_t)y1 * (size_t)source->width + (size_t)x0] +
                     fx * (double)source->pixels[(size_t)y1 * (size_t)source->width + (size_t)x1];
            value = (1.0 - fy) * top + fy * bottom;

            if (value < 0.0)
                value = 0.0;
            if (value > 255.0)
                value = 255.0;

            result.pixels[(size_t)y * (size_t)dst_width + (size_t)x] =
                (uint8_t)lround(value);
        }
    }

    return result;
}

static td_gray_image resize_gray(const td_gray_image *source, int dst_width, int dst_height)
{
    if (source->width == dst_width && source->height == dst_height)
        return td_gray_clone(source);

    if (dst_width <= 0 || dst_height <= 0)
        return td_gray_alloc(0, 0, 255);

    if (dst_width * 2 <= source->width || dst_height * 2 <= source->height)
        return resize_area(source, dst_width, dst_height);

    return resize_bilinear(source, dst_width, dst_height);
}

static int align_offset(int target, int item, td_align_t alignment)
{
    int slack = target - item;

    switch (alignment) {
    case TD_ALIGN_START:
        return 0;
    case TD_ALIGN_END:
        return slack;
    case TD_ALIGN_CENTER:
    default:
        return slack / 2;
    }
}

int td_gray_place(const td_gray_image *source, int target_width, int target_height,
                  td_scale_t scaling, td_align_t align_x, td_align_t align_y,
                  td_gray_image *result)
{
    double scale = 1.0;
    int scaled_width;
    int scaled_height;
    int offset_x;
    int offset_y;
    td_gray_image scaled;
    bool scaled_allocated = false;
    int copy_x0;
    int copy_x1;
    int copy_y0;
    int copy_y1;
    int y;

    if (source == NULL || result == NULL || source->width <= 0 || source->height <= 0 ||
        target_width <= 0 || target_height <= 0)
        return -1;

    switch (scaling) {
    case TD_SCALE_CROP:
        scale = fmax((double)target_width / (double)source->width,
                     (double)target_height / (double)source->height);
        break;
    case TD_SCALE_FIT_ENLARGE:
        scale = fmin((double)target_width / (double)source->width,
                     (double)target_height / (double)source->height);
        break;
    case TD_SCALE_NONE:
        scale = 1.0;
        break;
    case TD_SCALE_FIT:
    default:
        scale = fmin((double)target_width / (double)source->width,
                     (double)target_height / (double)source->height);
        if (scale > 1.0)
            scale = 1.0;
        break;
    }

    if (!isfinite(scale) || scale <= 0.0)
        return -1;

    scaled_width = (int)lround((double)source->width * scale);
    scaled_height = (int)lround((double)source->height * scale);
    if (scaled_width < 1)
        scaled_width = 1;
    if (scaled_height < 1)
        scaled_height = 1;

    if (scaled_width == source->width && scaled_height == source->height) {
        scaled = *source;
    } else {
        scaled = resize_gray(source, scaled_width, scaled_height);
        scaled_allocated = true;
    }

    offset_x = align_offset(target_width, scaled_width, align_x);
    offset_y = align_offset(target_height, scaled_height, align_y);

    *result = td_gray_alloc(target_width, target_height, 255);

    copy_x0 = offset_x < 0 ? 0 : offset_x;
    copy_y0 = offset_y < 0 ? 0 : offset_y;
    copy_x1 = offset_x + scaled_width;
    copy_y1 = offset_y + scaled_height;
    if (copy_x1 > target_width)
        copy_x1 = target_width;
    if (copy_y1 > target_height)
        copy_y1 = target_height;

    for (y = copy_y0; y < copy_y1; y++) {
        int source_y = y - offset_y;
        int source_x = copy_x0 - offset_x;
        int count = copy_x1 - copy_x0;

        if (source_y < 0 || source_y >= scaled.height || source_x < 0 || count <= 0)
            continue;
        if (source_x + count > scaled.width)
            count = scaled.width - source_x;
        if (count <= 0)
            continue;

        memcpy(result->pixels + (size_t)y * (size_t)target_width + (size_t)copy_x0,
               scaled.pixels + (size_t)source_y * (size_t)scaled.width + (size_t)source_x,
               (size_t)count);
    }

    if (scaled_allocated)
        td_gray_free(&scaled);

    return 0;
}

td_bitmap td_bitmap_alloc(int width, int height)
{
    td_bitmap bitmap;

    if (width < 0)
        width = 0;
    if (height < 0)
        height = 0;

    bitmap.width = width;
    bitmap.height = height;
    bitmap.stride = (width + 7) / 8;
    bitmap.bits = td_calloc((size_t)bitmap.stride * (size_t)height, 1);
    return bitmap;
}

void td_bitmap_free(td_bitmap *bitmap)
{
    if (bitmap == NULL)
        return;
    free(bitmap->bits);
    bitmap->bits = NULL;
    bitmap->width = 0;
    bitmap->height = 0;
    bitmap->stride = 0;
}

bool td_bitmap_get(const td_bitmap *bitmap, int x, int y)
{
    if (bitmap == NULL || bitmap->bits == NULL || x < 0 || x >= bitmap->width ||
        y < 0 || y >= bitmap->height)
        return false;

    return (bitmap->bits[(size_t)y * (size_t)bitmap->stride + (size_t)(x / 8)] &
            (uint8_t)(0x80u >> (unsigned)(x % 8))) != 0;
}

void td_bitmap_set(td_bitmap *bitmap, int x, int y, bool black)
{
    uint8_t *byte;
    uint8_t mask;

    if (bitmap == NULL || bitmap->bits == NULL || x < 0 || x >= bitmap->width ||
        y < 0 || y >= bitmap->height)
        return;

    byte = &bitmap->bits[(size_t)y * (size_t)bitmap->stride + (size_t)(x / 8)];
    mask = (uint8_t)(0x80u >> (unsigned)(x % 8));

    if (black)
        *byte |= mask;
    else
        *byte &= (uint8_t)~mask;
}

static bool row_is_blank(const td_bitmap *bitmap, int y, bool inverted)
{
    const uint8_t *row = bitmap->bits + (size_t)y * (size_t)bitmap->stride;
    uint8_t blank = inverted ? 0xff : 0x00;
    int i;

    for (i = 0; i < bitmap->stride; i++) {
        /* The unused low bits of the final byte are padding.  They are not
         * meaningful pixels and must not prevent blank-row detection. */
        if (i == bitmap->stride - 1 && (bitmap->width % 8) != 0) {
            uint8_t mask = (uint8_t)(0xffu << (8 - (bitmap->width % 8)));

            if ((row[i] & mask) != (blank & mask))
                return false;
        } else if (row[i] != blank) {
            return false;
        }
    }
    return true;
}

bool td_bitmap_trim(td_bitmap *bitmap, int min_height, bool inverted)
{
    uint8_t blank = inverted ? 0xff : 0x00;
    int top;
    int bottom;
    int content_height;
    int new_height;

    if (bitmap == NULL || bitmap->bits == NULL || bitmap->height <= 0)
        return false;

    if (min_height < 1)
        min_height = 1;
    if (min_height > bitmap->height)
        min_height = bitmap->height;

    top = 0;
    while (top < bitmap->height && row_is_blank(bitmap, top, inverted))
        top++;

    if (top == bitmap->height) {
        new_height = min_height;
        if (new_height == bitmap->height)
            return false;
        memset(bitmap->bits, blank, (size_t)bitmap->stride * (size_t)new_height);
        bitmap->height = new_height;
        return true;
    }

    bottom = bitmap->height - 1;
    while (bottom > top && row_is_blank(bitmap, bottom, inverted))
        bottom--;

    content_height = bottom - top + 1;
    new_height = content_height > min_height ? content_height : min_height;
    if (new_height > bitmap->height)
        new_height = bitmap->height;

    if (top > 0 && content_height > 0) {
        memmove(bitmap->bits,
                bitmap->bits + (size_t)top * (size_t)bitmap->stride,
                (size_t)content_height * (size_t)bitmap->stride);
    }

    if (new_height > content_height) {
        memset(bitmap->bits + (size_t)content_height * (size_t)bitmap->stride,
               blank, (size_t)(new_height - content_height) * (size_t)bitmap->stride);
    }

    if (new_height == bitmap->height && top == 0)
        return false;

    bitmap->height = new_height;
    return true;
}
