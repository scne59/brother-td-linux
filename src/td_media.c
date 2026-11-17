/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include "td_media.h"

#include <math.h>
#include <string.h>
#include <strings.h>

#include "td_util.h"

#define MM_PER_INCH 25.4
#define TD_DIECUT_END_OFFSET_MM 6.0
#define TD_MIN_PRINT_WIDTH_PX 8
#define TD_MAX_MEDIA_LENGTH_MM 3000.0
#define TD_MIN_MEDIA_LENGTH_MM 12.0

typedef struct {
    int width_mm;
    int print_pins;
} width_entry_t;

/* Number of print area pins from the TD-4D raster command reference, section
 * 4.3.5.  Values are given for the common media widths.  Media that is not
 * listed falls back to a 3 mm total edge allowance. */
static const width_entry_t widths_203[] = {
    {39, 288},
    {51, 382},
    {58, 440},
    {76, 583},
    {80, 615},
    {90, 695},
    {102, 788},
    {106, 823},
};

static const width_entry_t widths_300[] = {
    {39, 425},
    {51, 564},
    {58, 649},
    {76, 861},
    {80, 909},
    {90, 1027},
    {102, 1164},
    {106, 1216},
};

const char *td_media_type_name(td_media_type_t type)
{
    return type == TD_MEDIA_CONTINUOUS ? "Continuous" : "DieCut";
}

td_media_type_t td_media_type_parse(const char *value, td_media_type_t fallback)
{
    if (value == NULL || value[0] == '\0')
        return fallback;

    if (strcasecmp(value, "continuous") == 0 ||
        strcasecmp(value, "continuous-length-tape") == 0 ||
        strcasecmp(value, "roll") == 0 ||
        strcasecmp(value, "c") == 0)
        return TD_MEDIA_CONTINUOUS;

    if (strcasecmp(value, "diecut") == 0 ||
        strcasecmp(value, "die-cut") == 0 ||
        strcasecmp(value, "labels") == 0 ||
        strcasecmp(value, "d") == 0)
        return TD_MEDIA_DIECUT;

    return fallback;
}

int td_printable_width_px(int dpi, double media_width_mm)
{
    const width_entry_t *table = NULL;
    size_t table_len = 0;
    size_t i;
    int rounded_width;
    int fallback;

    if (media_width_mm <= 0.0)
        return TD_MIN_PRINT_WIDTH_PX;

    rounded_width = (int)lround(media_width_mm);

    if (dpi == 203) {
        table = widths_203;
        table_len = sizeof(widths_203) / sizeof(widths_203[0]);
    } else if (dpi == 300) {
        table = widths_300;
        table_len = sizeof(widths_300) / sizeof(widths_300[0]);
    }

    for (i = 0; i < table_len; i++) {
        if (table[i].width_mm == rounded_width)
            return table[i].print_pins;
    }

    /* Conservative estimate: 1.5 mm of non-printing edge on each side. */
    fallback = (int)lround((media_width_mm - 3.0) * (double)dpi / MM_PER_INCH);
    if (fallback < TD_MIN_PRINT_WIDTH_PX)
        fallback = TD_MIN_PRINT_WIDTH_PX;

    return fallback;
}

int td_geometry_init(td_geometry_t *geometry, const td_model_t *model,
                     td_media_type_t type, double media_width_mm,
                     double media_length_mm)
{
    int maximum_width;

    if (geometry == NULL || model == NULL)
        return -1;

    memset(geometry, 0, sizeof(*geometry));
    geometry->model = model;
    geometry->dpi = model->dpi;
    geometry->head_pins = model->head_pins;
    geometry->head_bytes = (model->head_pins + 7) / 8;
    geometry->type = type;

    if (!isfinite(media_width_mm) || media_width_mm <= 0.0)
        return -1;
    if (!isfinite(media_length_mm) || media_length_mm <= 0.0)
        return -1;

    maximum_width = model->max_width_mm;
    if (media_width_mm > (double)maximum_width) {
        td_log(TD_LOG_WARN,
               "media width %.1f mm exceeds %s maximum of %d mm; clamping",
               media_width_mm, model->id, maximum_width);
        media_width_mm = (double)maximum_width;
    }
    if (media_width_mm < 19.0) {
        td_log(TD_LOG_WARN, "media width %.1f mm is below the 19 mm minimum", media_width_mm);
        media_width_mm = 19.0;
    }

    if (media_length_mm > TD_MAX_MEDIA_LENGTH_MM) {
        td_log(TD_LOG_WARN, "media length %.1f mm exceeds the 3000 mm maximum; clamping",
               media_length_mm);
        media_length_mm = TD_MAX_MEDIA_LENGTH_MM;
    }
    if (media_length_mm < TD_MIN_MEDIA_LENGTH_MM) {
        td_log(TD_LOG_WARN, "media length %.1f mm is below the 12 mm minimum", media_length_mm);
        media_length_mm = TD_MIN_MEDIA_LENGTH_MM;
    }

    geometry->media_width_mm = media_width_mm;
    geometry->media_length_mm = media_length_mm;
    geometry->media_width_mm_round = td_clampi((int)lround(media_width_mm), 1, 255);

    if (type == TD_MEDIA_CONTINUOUS) {
        geometry->media_length_mm_round = 0;
        geometry->print_height_px =
            (int)lround(media_length_mm * (double)geometry->dpi / MM_PER_INCH);
    } else {
        double printable_length = media_length_mm - TD_DIECUT_END_OFFSET_MM;

        geometry->media_length_mm_round =
            td_clampi((int)lround(media_length_mm), 1, 255);
        if (printable_length < 1.0)
            printable_length = 1.0;
        geometry->print_height_px =
            (int)lround(printable_length * (double)geometry->dpi / MM_PER_INCH);
    }

    if (geometry->print_height_px < 1)
        geometry->print_height_px = 1;

    geometry->print_width_px = td_printable_width_px(geometry->dpi, media_width_mm);
    if (geometry->print_width_px > geometry->head_pins - 2)
        geometry->print_width_px = geometry->head_pins - 2;
    if (geometry->print_width_px < TD_MIN_PRINT_WIDTH_PX)
        geometry->print_width_px = TD_MIN_PRINT_WIDTH_PX;

    geometry->left_offset_px = (geometry->head_pins - geometry->print_width_px) / 2;
    geometry->right_offset_px =
        geometry->head_pins - geometry->print_width_px - geometry->left_offset_px;

    td_log(TD_LOG_DEBUG,
           "geometry: model=%s dpi=%d head=%d media=%.1fx%.1f mm type=%s "
           "print=%dx%d offsets=%d/%d",
           model->id, geometry->dpi, geometry->head_pins, geometry->media_width_mm,
           geometry->media_length_mm, td_media_type_name(type),
           geometry->print_width_px, geometry->print_height_px,
           geometry->left_offset_px, geometry->right_offset_px);

    return 0;
}
