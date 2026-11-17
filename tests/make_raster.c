/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 *
 * Small synthetic CUPS raster generator used by the integration tests.
 */

#include <cups/raster.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef struct {
    const char *output;
    int width;
    int height;
    int dpi;
    double page_width_mm;
    double page_length_mm;
    int pages;
    int bits;
    int pwg;
    const char *colorspace;
    const char *pattern;
} settings_t;

static void usage(void)
{
    fprintf(stderr,
            "usage: make_raster [options]\n"
            "  --output FILE\n"
            "  --width PX --height PX\n"
            "  --dpi N\n"
            "  --page-width-mm MM --page-length-mm MM\n"
            "  --pages N\n"
            "  --bits 1|8|16\n"
            "  --pwg              write PWG raster instead of CUPS raster\n"
            "  --colorspace K|W|RGB|CMYK|LAB\n"
            "  --pattern white|black|checker|gradient|border|square|stripes\n");
}

static int parse_int(const char *value, int fallback)
{
    char *end = NULL;
    long parsed = strtol(value, &end, 10);

    if (end == value || *end != '\0')
        return fallback;
    return (int)parsed;
}

static double parse_double(const char *value, double fallback)
{
    char *end = NULL;
    double parsed = strtod(value, &end);

    if (end == value || *end != '\0')
        return fallback;
    return parsed;
}

static int parse_args(int argc, char **argv, settings_t *settings)
{
    int i;

    memset(settings, 0, sizeof(*settings));
    settings->width = 816;
    settings->height = 1216;
    settings->dpi = 203;
    settings->page_width_mm = 0.0;
    settings->page_length_mm = 0.0;
    settings->pages = 1;
    settings->bits = 8;
    settings->pwg = 0;
    settings->colorspace = "K";
    settings->pattern = "square";

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)
            settings->output = argv[++i];
        else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
            settings->width = parse_int(argv[++i], settings->width);
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
            settings->height = parse_int(argv[++i], settings->height);
        else if (strcmp(argv[i], "--dpi") == 0 && i + 1 < argc)
            settings->dpi = parse_int(argv[++i], settings->dpi);
        else if (strcmp(argv[i], "--page-width-mm") == 0 && i + 1 < argc)
            settings->page_width_mm = parse_double(argv[++i], 0.0);
        else if (strcmp(argv[i], "--page-length-mm") == 0 && i + 1 < argc)
            settings->page_length_mm = parse_double(argv[++i], 0.0);
        else if (strcmp(argv[i], "--pages") == 0 && i + 1 < argc)
            settings->pages = parse_int(argv[++i], 1);
        else if (strcmp(argv[i], "--bits") == 0 && i + 1 < argc)
            settings->bits = parse_int(argv[++i], 8);
        else if (strcmp(argv[i], "--pwg") == 0)
            settings->pwg = 1;
        else if (strcmp(argv[i], "--colorspace") == 0 && i + 1 < argc)
            settings->colorspace = argv[++i];
        else if (strcmp(argv[i], "--pattern") == 0 && i + 1 < argc)
            settings->pattern = argv[++i];
        else {
            usage();
            return -1;
        }
    }

    if (settings->width <= 0 || settings->height <= 0 || settings->dpi <= 0 ||
        settings->pages <= 0 || (settings->bits != 1 && settings->bits != 8 && settings->bits != 16))
        return -1;

    if (settings->page_width_mm <= 0.0)
        settings->page_width_mm = (double)settings->width * 25.4 / (double)settings->dpi;
    if (settings->page_length_mm <= 0.0)
        settings->page_length_mm = (double)settings->height * 25.4 / (double)settings->dpi;

    return 0;
}

static unsigned resolution_for(const char *name, cups_cspace_t *space)
{
    if (strcasecmp(name, "K") == 0) {
        *space = CUPS_CSPACE_K;
        return 1;
    }
    if (strcasecmp(name, "W") == 0 || strcasecmp(name, "GRAY") == 0) {
        *space = CUPS_CSPACE_W;
        return 1;
    }
    if (strcasecmp(name, "RGB") == 0) {
        *space = CUPS_CSPACE_RGB;
        return 3;
    }
    if (strcasecmp(name, "CMYK") == 0) {
        *space = CUPS_CSPACE_CMYK;
        return 4;
    }
    if (strcasecmp(name, "LAB") == 0) {
        *space = CUPS_CSPACE_CIELab;
        return 3;
    }
    return 0;
}

static int pattern_black(const settings_t *settings, int x, int y)
{
    if (strcmp(settings->pattern, "white") == 0)
        return 0;
    if (strcmp(settings->pattern, "black") == 0)
        return 1;
    if (strcmp(settings->pattern, "checker") == 0)
        return ((x + y) & 1) != 0;
    if (strcmp(settings->pattern, "gradient") == 0)
        return x * 2 < settings->width;
    if (strcmp(settings->pattern, "border") == 0)
        return x < 10 || y < 10 || x >= settings->width - 10 ||
               y >= settings->height - 10;
    if (strcmp(settings->pattern, "stripes") == 0)
        return (y / 8) % 2 == 0;
    return x >= settings->width / 4 && x < settings->width * 3 / 4 &&
           y >= settings->height / 4 && y < settings->height * 3 / 4;
}

static void fill_pixel(const settings_t *settings, cups_cspace_t space,
                       int x, int y, uint8_t *pixel)
{
    int dark = 0;
    int components;
    int c;

    switch (space) {
    case CUPS_CSPACE_K:
        components = 1;
        dark = 255;
        break;
    case CUPS_CSPACE_W:
        components = 1;
        dark = 0;
        break;
    case CUPS_CSPACE_CMYK:
        components = 4;
        dark = 255;
        break;
    default:
        components = 3;
        dark = 0;
        break;
    }

    {
        int light = (space == CUPS_CSPACE_K || space == CUPS_CSPACE_CMYK) ? 0 : 255;
        int black = 0;

        if (strcmp(settings->pattern, "gradient") == 0) {
            int value = settings->width > 1 ? x * 255 / (settings->width - 1) : 0;

            for (c = 0; c < components; c++)
                pixel[c] = (uint8_t)(space == CUPS_CSPACE_CMYK ? 0 : value);
            if (space == CUPS_CSPACE_CMYK)
                pixel[0] = pixel[1] = pixel[2] = (uint8_t)value;
            return;
        }

        black = pattern_black(settings, x, y);

        for (c = 0; c < components; c++)
            pixel[c] = (uint8_t)(black ? dark : light);
    }
}

int main(int argc, char **argv)
{
    settings_t settings;
    cups_cspace_t space;
    unsigned components;
    cups_raster_t *raster;
    cups_page_header2_t header;
    uint8_t *row;
    int fd;
    int page;

    if (parse_args(argc, argv, &settings) < 0) {
        usage();
        return 1;
    }

    components = resolution_for(settings.colorspace, &space);
    if (components == 0) {
        fprintf(stderr, "unknown colorspace %s\n", settings.colorspace);
        return 1;
    }
    if (settings.bits == 1 && components != 1) {
        fprintf(stderr, "1-bit output requires K or W colorspace\n");
        return 1;
    }

    if (settings.output != NULL) {
        fd = open(settings.output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror(settings.output);
            return 1;
        }
    } else {
        fd = 1;
    }

    raster = cupsRasterOpen(fd, settings.pwg ? CUPS_RASTER_WRITE_PWG : CUPS_RASTER_WRITE);
    if (raster == NULL) {
        fprintf(stderr, "cupsRasterOpen failed\n");
        if (settings.output != NULL)
            close(fd);
        return 1;
    }

    memset(&header, 0, sizeof(header));
    header.cupsWidth = (unsigned)settings.width;
    header.cupsHeight = (unsigned)settings.height;
    header.cupsBitsPerColor = (unsigned)settings.bits;
    header.cupsBitsPerPixel = (unsigned)settings.bits * components;
    header.cupsBytesPerLine = ((unsigned)settings.width * components *
                               (unsigned)settings.bits + 7) / 8;
    header.cupsColorOrder = CUPS_ORDER_CHUNKED;
    header.cupsColorSpace = space;
    header.cupsNumColors = components;
    header.cupsCompression = 0;
    header.cupsRowCount = (unsigned)settings.height;
    header.HWResolution[0] = (unsigned)settings.dpi;
    header.HWResolution[1] = (unsigned)settings.dpi;
    header.PageSize[0] = (unsigned)(settings.page_width_mm * 72.0 / 25.4);
    header.PageSize[1] = (unsigned)(settings.page_length_mm * 72.0 / 25.4);
    header.cupsPageSize[0] = (float)(settings.page_width_mm * 72.0 / 25.4);
    header.cupsPageSize[1] = (float)(settings.page_length_mm * 72.0 / 25.4);
    header.NumCopies = 1;

    row = malloc(header.cupsBytesPerLine);
    if (row == NULL) {
        cupsRasterClose(raster);
        return 1;
    }

    for (page = 0; page < settings.pages; page++) {
        unsigned x;
        unsigned y;

        if (cupsRasterWriteHeader2(raster, &header) == 0) {
            fprintf(stderr, "cupsRasterWriteHeader2 failed\n");
            free(row);
            cupsRasterClose(raster);
            return 1;
        }

        for (y = 0; y < header.cupsHeight; y++) {
            if (settings.bits == 1) {
                memset(row, 0, header.cupsBytesPerLine);
                for (x = 0; x < header.cupsWidth; x++) {
                    int black = pattern_black(&settings, (int)x, (int)y);
                    int bit = space == CUPS_CSPACE_K ? black : !black;

                    if (bit)
                        row[x / 8] |= (uint8_t)(0x80u >> (x % 8));
                }
            } else if (settings.bits == 16) {
                for (x = 0; x < header.cupsWidth; x++) {
                    uint8_t samples[4];
                    unsigned c;

                    fill_pixel(&settings, space, (int)x, (int)y, samples);
                    for (c = 0; c < components; c++) {
                        unsigned value = (unsigned)samples[c] * 257u;
                        size_t offset = ((size_t)x * components + c) * 2;

                        row[offset] = (uint8_t)(value >> 8);
                        row[offset + 1] = (uint8_t)(value & 0xff);
                    }
                }
            } else {
                for (x = 0; x < header.cupsWidth; x++) {
                    fill_pixel(&settings, space, (int)x, (int)y,
                               row + (size_t)x * components);
                }
            }
            if (cupsRasterWritePixels(raster, row, header.cupsBytesPerLine) !=
                header.cupsBytesPerLine) {
                fprintf(stderr, "cupsRasterWritePixels failed\n");
                free(row);
                cupsRasterClose(raster);
                return 1;
            }
        }
    }

    free(row);
    cupsRasterClose(raster);
    if (settings.output != NULL)
        close(fd);
    return 0;
}
