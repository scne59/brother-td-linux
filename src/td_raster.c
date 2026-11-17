/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include "td_raster.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "td_util.h"

#define TD_MAX_PIXELS (100u * 1000u * 1000u)
#define TD_MAX_DIMENSION 100000

static unsigned component_count(cups_cspace_t colorspace)
{
    switch (colorspace) {
    case CUPS_CSPACE_W:
    case CUPS_CSPACE_SW:
    case CUPS_CSPACE_K:
    case CUPS_CSPACE_WHITE:
        return 1;
    case CUPS_CSPACE_RGB:
    case CUPS_CSPACE_SRGB:
    case CUPS_CSPACE_ADOBERGB:
    case CUPS_CSPACE_CMY:
        return 3;
    case CUPS_CSPACE_RGBA:
    case CUPS_CSPACE_CMYK:
    case CUPS_CSPACE_YMCK:
    case CUPS_CSPACE_KCMY:
    case CUPS_CSPACE_RGBW:
        return 4;
    default:
        return 0;
    }
}

static uint8_t luma_from_rgb(int red, int green, int blue)
{
    int value;

    if (red < 0)
        red = 0;
    if (red > 255)
        red = 255;
    if (green < 0)
        green = 0;
    if (green > 255)
        green = 255;
    if (blue < 0)
        blue = 0;
    if (blue > 255)
        blue = 255;

    /* Rec. 601 coefficients, rounded. */
    value = (299 * red + 587 * green + 114 * blue + 500) / 1000;
    return (uint8_t)td_clampi(value, 0, 255);
}

static uint8_t sample_at(const unsigned char *row, const cups_page_header2_t *header,
                         unsigned components, unsigned x, unsigned component)
{
    unsigned bits = header->cupsBitsPerColor;
    unsigned bytes_per_sample = bits > 8 ? bits / 8 : 1;
    size_t offset;

    if (header->cupsColorOrder == CUPS_ORDER_CHUNKED) {
        offset = ((size_t)x * (size_t)components + (size_t)component) *
                 (size_t)bytes_per_sample;
    } else {
        offset = ((size_t)component * (size_t)header->cupsWidth + (size_t)x) *
                 (size_t)bytes_per_sample;
    }

    if (bits == 16) {
        unsigned value = ((unsigned)row[offset] << 8) | row[offset + 1];

        return (uint8_t)((value * 255u + 32767u) / 65535u);
    }

    return row[offset];
}

static uint8_t pixel_from_components(const cups_page_header2_t *header,
                                     unsigned components, const uint8_t *samples)
{
    switch (header->cupsColorSpace) {
    case CUPS_CSPACE_W:
    case CUPS_CSPACE_SW:
        return samples[0];
    case CUPS_CSPACE_K:
        return (uint8_t)(255 - samples[0]);
    case CUPS_CSPACE_WHITE:
        return (uint8_t)(255 - samples[0]);
    case CUPS_CSPACE_RGB:
    case CUPS_CSPACE_SRGB:
    case CUPS_CSPACE_ADOBERGB:
        return luma_from_rgb(samples[0], samples[1], samples[2]);
    case CUPS_CSPACE_RGBA:
    case CUPS_CSPACE_RGBW:
        return luma_from_rgb(samples[0], samples[1], samples[2]);
    case CUPS_CSPACE_CMY:
        return luma_from_rgb(255 - samples[0], 255 - samples[1], 255 - samples[2]);
    case CUPS_CSPACE_CMYK:
        return luma_from_rgb(
            (255 - samples[0]) * (255 - samples[3]) / 255,
            (255 - samples[1]) * (255 - samples[3]) / 255,
            (255 - samples[2]) * (255 - samples[3]) / 255);
    case CUPS_CSPACE_YMCK:
        return luma_from_rgb(
            (255 - samples[2]) * (255 - samples[3]) / 255,
            (255 - samples[1]) * (255 - samples[3]) / 255,
            (255 - samples[0]) * (255 - samples[3]) / 255);
    case CUPS_CSPACE_KCMY:
        return luma_from_rgb(
            (255 - samples[1]) * (255 - samples[0]) / 255,
            (255 - samples[2]) * (255 - samples[0]) / 255,
            (255 - samples[3]) * (255 - samples[0]) / 255);
    default:
        (void)components;
        return 255;
    }
}

int td_raster_read_header(cups_raster_t *raster, cups_page_header2_t *header)
{
    memset(header, 0, sizeof(*header));

    if (cupsRasterReadHeader2(raster, header) == 0) {
        if (ferror(stdin)) {
            td_log(TD_LOG_ERROR, "error reading CUPS raster header");
            return -1;
        }
        return 0;
    }

    if (header->cupsWidth == 0 || header->cupsHeight == 0) {
        td_log(TD_LOG_ERROR, "CUPS raster page has zero dimensions");
        return -1;
    }
    if (header->cupsWidth > TD_MAX_DIMENSION || header->cupsHeight > TD_MAX_DIMENSION ||
        (uint64_t)header->cupsWidth * (uint64_t)header->cupsHeight > TD_MAX_PIXELS) {
        td_log(TD_LOG_ERROR, "CUPS raster page is too large (%ux%u)",
               header->cupsWidth, header->cupsHeight);
        return -1;
    }
    if (header->cupsBitsPerPixel == 0 || header->cupsBitsPerColor == 0) {
        td_log(TD_LOG_ERROR, "CUPS raster page has invalid bit depth (%u/%u)",
               header->cupsBitsPerPixel, header->cupsBitsPerColor);
        return -1;
    }
    if (header->cupsBytesPerLine <
        ((uint64_t)header->cupsWidth * header->cupsBitsPerPixel + 7) / 8) {
        td_log(TD_LOG_ERROR, "CUPS raster line is too short (%u bytes for %u pixels)",
               header->cupsBytesPerLine, header->cupsWidth);
        return -1;
    }

    return 1;
}

static int validate_header(const cups_page_header2_t *header)
{
    unsigned components = component_count(header->cupsColorSpace);
    unsigned bits = header->cupsBitsPerColor;

    if (components == 0) {
        td_log(TD_LOG_ERROR, "unsupported CUPS raster colorspace %d",
               (int)header->cupsColorSpace);
        return -1;
    }

    if (bits == 1) {
        if (!(header->cupsColorSpace == CUPS_CSPACE_K ||
              header->cupsColorSpace == CUPS_CSPACE_W ||
              header->cupsColorSpace == CUPS_CSPACE_SW)) {
            td_log(TD_LOG_ERROR, "1-bit raster is only supported for K/W colorspaces");
            return -1;
        }
    } else if (bits != 8 && bits != 16) {
        td_log(TD_LOG_ERROR, "unsupported CUPS raster bits per color: %u", bits);
        return -1;
    } else if (header->cupsBitsPerPixel != components * bits) {
        td_log(TD_LOG_ERROR,
               "inconsistent CUPS raster bit depth: %u bits/pixel for %u components at %u bits",
               header->cupsBitsPerPixel, components, bits);
        return -1;
    }

    if (header->cupsColorOrder != CUPS_ORDER_CHUNKED &&
        header->cupsColorOrder != CUPS_ORDER_BANDED &&
        header->cupsColorOrder != CUPS_ORDER_PLANAR) {
        td_log(TD_LOG_ERROR, "unsupported CUPS raster color order %d",
               (int)header->cupsColorOrder);
        return -1;
    }

    return 0;
}

int td_raster_convert_row(const cups_page_header2_t *header,
                          const unsigned char *row, int y, td_gray_image *image)
{
    unsigned components;
    unsigned bits;
    unsigned x;
    uint8_t *target;

    if (header == NULL || row == NULL || image == NULL || image->pixels == NULL)
        return -1;
    if (y < 0 || y >= image->height || image->width != (int)header->cupsWidth)
        return -1;
    if (validate_header(header) < 0)
        return -1;

    components = component_count(header->cupsColorSpace);
    bits = header->cupsBitsPerColor;
    target = image->pixels + (size_t)y * (size_t)image->width;

    if (bits == 1) {
        for (x = 0; x < header->cupsWidth; x++) {
            bool bit = (row[x / 8] & (0x80u >> (x % 8))) != 0;
            bool black = header->cupsColorSpace == CUPS_CSPACE_K ? bit : !bit;

            target[x] = black ? 0 : 255;
        }
    } else {
        uint8_t samples[4];

        for (x = 0; x < header->cupsWidth; x++) {
            unsigned component;

            for (component = 0; component < components; component++)
                samples[component] = sample_at(row, header, components, x, component);

            target[x] = pixel_from_components(header, components, samples);
        }
    }

    return 0;
}

int td_raster_read_gray(cups_raster_t *raster, const cups_page_header2_t *header,
                        td_gray_image *image)
{
    unsigned char *row;
    unsigned y;

    if (validate_header(header) < 0)
        return -1;

    *image = td_gray_alloc((int)header->cupsWidth, (int)header->cupsHeight, 255);
    row = td_malloc(header->cupsBytesPerLine);

    for (y = 0; y < header->cupsHeight; y++) {
        if (cupsRasterReadPixels(raster, row, header->cupsBytesPerLine) !=
            header->cupsBytesPerLine) {
            td_log(TD_LOG_ERROR, "short read from CUPS raster at line %u", y);
            free(row);
            td_gray_free(image);
            return -1;
        }
        if (td_raster_convert_row(header, row, (int)y, image) < 0) {
            free(row);
            td_gray_free(image);
            return -1;
        }
    }

    free(row);
    return 0;
}
