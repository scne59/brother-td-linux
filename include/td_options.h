/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#ifndef TD_OPTIONS_H
#define TD_OPTIONS_H

#include <stdbool.h>

#include "td_dither.h"
#include "td_image.h"
#include "td_media.h"

typedef enum {
    TD_COMPRESSION_NONE = 0,
    TD_COMPRESSION_TIFF = 1
} td_compression_t;

typedef struct {
    char model[64];
    char debug_directory[256];

    td_media_type_t media_type;
    td_dither_t dither;
    int threshold;
    td_scale_t scaling;
    int rotate;
    bool invert;
    bool mirror;
    bool border;

    double margin_mm;
    bool auto_cut;
    bool cut_at_end;
    bool peeler;
    bool auto_trim;

    td_align_t align_x;
    td_align_t align_y;
    td_compression_t compression;

    int copies;
    int job_id;
    const char *title;
    bool debug;
} td_options_t;

void td_options_init(td_options_t *options);
int td_options_parse(td_options_t *options, int argc, char **argv);
void td_options_log(const td_options_t *options);

#endif /* TD_OPTIONS_H */
