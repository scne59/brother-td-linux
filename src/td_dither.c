/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include "td_dither.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "td_util.h"

typedef struct {
    int dx;
    int dy;
    int weight;
} kernel_entry_t;

typedef struct {
    const kernel_entry_t *entries;
    int count;
    int divisor;
} diffusion_kernel_t;

static const kernel_entry_t floyd_entries[] = {
    {1, 0, 7},
    {-1, 1, 3}, {0, 1, 5}, {1, 1, 1},
};

static const kernel_entry_t jarvis_entries[] = {
    {1, 0, 7}, {2, 0, 5},
    {-2, 1, 3}, {-1, 1, 5}, {0, 1, 7}, {1, 1, 5}, {2, 1, 3},
    {-2, 2, 1}, {-1, 2, 3}, {0, 2, 5}, {1, 2, 3}, {2, 2, 1},
};

static const kernel_entry_t stucki_entries[] = {
    {1, 0, 8}, {2, 0, 4},
    {-2, 1, 2}, {-1, 1, 4}, {0, 1, 8}, {1, 1, 4}, {2, 1, 2},
    {-2, 2, 1}, {-1, 2, 2}, {0, 2, 4}, {1, 2, 2}, {2, 2, 1},
};

static const kernel_entry_t atkinson_entries[] = {
    {1, 0, 1}, {2, 0, 1},
    {-1, 1, 1}, {0, 1, 1}, {1, 1, 1},
    {0, 2, 1},
};

static const kernel_entry_t sierra_entries[] = {
    {1, 0, 5}, {2, 0, 3},
    {-2, 1, 2}, {-1, 1, 4}, {0, 1, 5}, {1, 1, 4}, {2, 1, 2},
    {-1, 2, 2}, {0, 2, 3}, {1, 2, 2},
};

static const kernel_entry_t sierra_lite_entries[] = {
    {1, 0, 2},
    {-1, 1, 1}, {0, 1, 1},
};

static const kernel_entry_t burkes_entries[] = {
    {1, 0, 8}, {2, 0, 4},
    {-2, 1, 2}, {-1, 1, 4}, {0, 1, 8}, {1, 1, 4}, {2, 1, 2},
    {-2, 2, 1}, {-1, 2, 2}, {0, 2, 4}, {1, 2, 2}, {2, 2, 1},
};

#define KERNEL_DIVISOR(name, denom) { .entries = name, .count = (int)(sizeof(name) / sizeof(name[0])), .divisor = (denom) }

static const diffusion_kernel_t kernels_with_divisor[] = {
    [TD_DITHER_FLOYD] = KERNEL_DIVISOR(floyd_entries, 16),
    [TD_DITHER_JARVIS] = KERNEL_DIVISOR(jarvis_entries, 48),
    [TD_DITHER_STUCKI] = KERNEL_DIVISOR(stucki_entries, 42),
    [TD_DITHER_ATKINSON] = KERNEL_DIVISOR(atkinson_entries, 8),
    [TD_DITHER_SIERRA] = KERNEL_DIVISOR(sierra_entries, 32),
    [TD_DITHER_SIERRA_LITE] = KERNEL_DIVISOR(sierra_lite_entries, 4),
    [TD_DITHER_BURKES] = KERNEL_DIVISOR(burkes_entries, 32),
};

static const int bayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

static const int bayer8[8][8] = {
    {0, 32, 8, 40, 2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    {3, 35, 11, 43, 1, 33, 9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21},
};

const char *td_dither_name(td_dither_t dither)
{
    switch (dither) {
    case TD_DITHER_FLOYD:
        return "Floyd";
    case TD_DITHER_JARVIS:
        return "Jarvis";
    case TD_DITHER_STUCKI:
        return "Stucki";
    case TD_DITHER_ATKINSON:
        return "Atkinson";
    case TD_DITHER_SIERRA:
        return "Sierra";
    case TD_DITHER_SIERRA_LITE:
        return "SierraLite";
    case TD_DITHER_BURKES:
        return "Burkes";
    case TD_DITHER_BAYER4:
        return "Bayer4";
    case TD_DITHER_BAYER8:
        return "Bayer8";
    case TD_DITHER_THRESHOLD:
    default:
        return "Threshold";
    }
}

td_dither_t td_dither_parse(const char *value, td_dither_t fallback)
{
    if (value == NULL || value[0] == '\0')
        return fallback;

    if (strcasecmp(value, "floyd") == 0 || strcasecmp(value, "floyd-steinberg") == 0)
        return TD_DITHER_FLOYD;
    if (strcasecmp(value, "jarvis") == 0 || strcasecmp(value, "jarvis-judice-ninke") == 0)
        return TD_DITHER_JARVIS;
    if (strcasecmp(value, "stucki") == 0)
        return TD_DITHER_STUCKI;
    if (strcasecmp(value, "atkinson") == 0)
        return TD_DITHER_ATKINSON;
    if (strcasecmp(value, "sierra") == 0)
        return TD_DITHER_SIERRA;
    if (strcasecmp(value, "sierralite") == 0 || strcasecmp(value, "sierra-lite") == 0)
        return TD_DITHER_SIERRA_LITE;
    if (strcasecmp(value, "burkes") == 0)
        return TD_DITHER_BURKES;
    if (strcasecmp(value, "bayer4") == 0 || strcasecmp(value, "ordered4") == 0)
        return TD_DITHER_BAYER4;
    if (strcasecmp(value, "bayer8") == 0 || strcasecmp(value, "bayer") == 0 ||
        strcasecmp(value, "ordered") == 0 || strcasecmp(value, "ordered8") == 0)
        return TD_DITHER_BAYER8;
    if (strcasecmp(value, "threshold") == 0 || strcasecmp(value, "none") == 0)
        return TD_DITHER_THRESHOLD;

    return fallback;
}

static int distribute_error(const diffusion_kernel_t *kernel, float *rows[3],
                            int x, int width, double error)
{
    int i;

    for (i = 0; i < kernel->count; i++) {
        int dx = kernel->entries[i].dx;
        int dy = kernel->entries[i].dy;
        int target_x = x + dx;

        if (target_x < 0 || target_x >= width || dy < 0 || dy > 2)
            continue;

        rows[dy][target_x] += (float)(error * (double)kernel->entries[i].weight /
                                     (double)kernel->divisor);
    }

    return 0;
}

static int dither_diffusion(const td_gray_image *source, td_dither_t dither,
                            int threshold, td_bitmap *result)
{
    const diffusion_kernel_t *kernel = &kernels_with_divisor[dither];
    float *rows[3];
    int x;
    int y;
    int row_size;

    if (kernel->entries == NULL || kernel->divisor == 0)
        return -1;

    row_size = source->width + 4;
    for (y = 0; y < 3; y++)
        rows[y] = td_calloc((size_t)row_size, sizeof(float));

    for (y = 0; y < source->height; y++) {
        const uint8_t *source_row = source->pixels + (size_t)y * (size_t)source->width;

        for (x = 0; x < source->width; x++) {
            double value = (double)source_row[x] + (double)rows[0][x + 2];
            double quantized;
            double error;
            bool black;

            if (value < 0.0)
                value = 0.0;
            if (value > 255.0)
                value = 255.0;

            black = value < (double)threshold;
            quantized = black ? 0.0 : 255.0;
            error = value - quantized;

            td_bitmap_set(result, x, y, black);
            distribute_error(kernel, rows, x + 2, row_size, error);
        }

        {
            float *old = rows[0];

            rows[0] = rows[1];
            rows[1] = rows[2];
            rows[2] = old;
            memset(rows[2], 0, (size_t)row_size * sizeof(float));
        }
    }

    for (y = 0; y < 3; y++)
        free(rows[y]);

    return 0;
}

static int dither_ordered(const td_gray_image *source, int threshold,
                          const int *matrix, int size, td_bitmap *result)
{
    int x;
    int y;
    int shift = threshold - 128;

    for (y = 0; y < source->height; y++) {
        for (x = 0; x < source->width; x++) {
            int level = matrix[(y % size) * size + (x % size)];
            double base = ((double)level + 0.5) * 255.0 / (double)(size * size);
            double value = base + (double)shift;
            bool black;

            if (value < 1.0)
                value = 1.0;
            if (value > 255.0)
                value = 255.0;

            black = source->pixels[(size_t)y * (size_t)source->width + (size_t)x] < value;
            td_bitmap_set(result, x, y, black);
        }
    }

    return 0;
}

int td_dither_image(const td_gray_image *source, td_dither_t dither,
                    int threshold, td_bitmap *result)
{
    int x;
    int y;

    if (source == NULL || result == NULL || source->width <= 0 || source->height <= 0)
        return -1;

    if (threshold < 0)
        threshold = 0;
    if (threshold > 255)
        threshold = 255;

    *result = td_bitmap_alloc(source->width, source->height);
    if (result->bits == NULL)
        return -1;

    switch (dither) {
    case TD_DITHER_FLOYD:
    case TD_DITHER_JARVIS:
    case TD_DITHER_STUCKI:
    case TD_DITHER_ATKINSON:
    case TD_DITHER_SIERRA:
    case TD_DITHER_SIERRA_LITE:
    case TD_DITHER_BURKES:
        return dither_diffusion(source, dither, threshold, result);
    case TD_DITHER_BAYER4:
        return dither_ordered(source, threshold, &bayer4[0][0], 4, result);
    case TD_DITHER_BAYER8:
        return dither_ordered(source, threshold, &bayer8[0][0], 8, result);
    case TD_DITHER_THRESHOLD:
    default:
        for (y = 0; y < source->height; y++) {
            const uint8_t *source_row = source->pixels + (size_t)y * (size_t)source->width;

            for (x = 0; x < source->width; x++)
                td_bitmap_set(result, x, y, source_row[x] < (uint8_t)threshold);
        }
        return 0;
    }
}
