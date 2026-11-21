/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#ifndef TD_MEDIA_H
#define TD_MEDIA_H

#include <stdbool.h>

#include "td_models.h"

typedef enum {
    TD_MEDIA_DIECUT = 0,
    TD_MEDIA_CONTINUOUS = 1
} td_media_type_t;

typedef struct {
    const td_model_t *model;
    int dpi;
    int head_pins;
    int head_bytes;

    td_media_type_t type;
    double media_width_mm;
    double media_length_mm;

    int media_width_mm_round;   /* n3 in ESC i z */
    int media_length_mm_round;  /* n4 in ESC i z (0 for continuous) */

    int print_width_px;         /* imageable pins */
    int print_height_px;        /* imageable rows */
    int left_offset_px;
    int right_offset_px;
} td_geometry_t;

const char *td_media_type_name(td_media_type_t type);
td_media_type_t td_media_type_parse(const char *value, td_media_type_t fallback);

/* Return the number of imageable pins for a media width, using the values in
 * Brother's TD-4D raster command reference where available and a conservative
 * 3 mm total edge allowance otherwise. */
int td_printable_width_px(int dpi, double media_width_mm);

/* Normalize a requested page size to the printer's physical orientation.
 *
 * Label printers only feed media along one axis, but clients (for example
 * Inkscape) describe the document page, which may be landscape.  When the
 * requested width exceeds the model's maximum media width and the length
 * still fits, the dimensions are swapped and true is returned.  Callers that
 * already have a raster image must rotate it by 90 degrees. */
bool td_media_normalize_orientation(const td_model_t *model,
                                    double *width_mm, double *length_mm);

int td_geometry_init(td_geometry_t *geometry, const td_model_t *model,
                     td_media_type_t type, double media_width_mm,
                     double media_length_mm);

#endif /* TD_MEDIA_H */
