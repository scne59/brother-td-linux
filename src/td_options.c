/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include "td_options.h"

#include <cups/cups.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "td_util.h"

static const char *option_value(cups_option_t *options, int count, const char *name)
{
    return cupsGetOption(name, count, options);
}

static td_scale_t parse_scaling(const char *value)
{
    if (value == NULL || value[0] == '\0')
        return TD_SCALE_FIT;
    if (strcasecmp(value, "fit") == 0)
        return TD_SCALE_FIT;
    if (strcasecmp(value, "fitenlarge") == 0 || strcasecmp(value, "fit-enlarge") == 0 ||
        strcasecmp(value, "fill") == 0)
        return TD_SCALE_FIT_ENLARGE;
    if (strcasecmp(value, "crop") == 0)
        return TD_SCALE_CROP;
    if (strcasecmp(value, "none") == 0 || strcasecmp(value, "actual") == 0)
        return TD_SCALE_NONE;
    return TD_SCALE_FIT;
}

static td_align_t parse_align_x(const char *value)
{
    if (value == NULL)
        return TD_ALIGN_CENTER;
    if (strcasecmp(value, "left") == 0 || strcasecmp(value, "start") == 0)
        return TD_ALIGN_START;
    if (strcasecmp(value, "right") == 0 || strcasecmp(value, "end") == 0)
        return TD_ALIGN_END;
    return TD_ALIGN_CENTER;
}

static td_align_t parse_align_y(const char *value)
{
    if (value == NULL)
        return TD_ALIGN_CENTER;
    if (strcasecmp(value, "top") == 0 || strcasecmp(value, "start") == 0)
        return TD_ALIGN_START;
    if (strcasecmp(value, "bottom") == 0 || strcasecmp(value, "end") == 0)
        return TD_ALIGN_END;
    return TD_ALIGN_CENTER;
}

static td_compression_t parse_compression(const char *value)
{
    if (value == NULL)
        return TD_COMPRESSION_TIFF;
    if (strcasecmp(value, "none") == 0 || strcasecmp(value, "raw") == 0 ||
        strcasecmp(value, "0") == 0)
        return TD_COMPRESSION_NONE;
    return TD_COMPRESSION_TIFF;
}

static int parse_rotation(const char *value)
{
    int rotation = td_parse_int(value, 0, 0, 359);

    rotation %= 360;
    if (rotation < 0)
        rotation += 360;
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
        td_log(TD_LOG_WARN, "unsupported rotation %d; using 0 degrees", rotation);
        return 0;
    }
    return rotation;
}

void td_options_init(td_options_t *options)
{
    const char *debug_env;
    const char *debug_dir_env;

    memset(options, 0, sizeof(*options));
    options->media_type = TD_MEDIA_DIECUT;
    options->dither = TD_DITHER_FLOYD;
    options->threshold = 128;
    options->scaling = TD_SCALE_FIT;
    options->rotate = 0;
    options->invert = false;
    options->mirror = false;
    options->border = false;
    options->margin_mm = 3.0;
    options->auto_cut = false;
    options->cut_at_end = true;
    options->peeler = false;
    options->auto_trim = true;
    options->align_x = TD_ALIGN_CENTER;
    options->align_y = TD_ALIGN_CENTER;
    options->compression = TD_COMPRESSION_TIFF;
    options->copies = 1;
    options->job_id = 0;
    options->title = "";
    options->debug = false;
    options->model[0] = '\0';
    options->debug_directory[0] = '\0';

    debug_env = getenv("BROTHER_TD_DEBUG");
    if (debug_env != NULL && debug_env[0] != '\0' &&
        strcmp(debug_env, "0") != 0 && strcasecmp(debug_env, "false") != 0)
        options->debug = true;

    debug_dir_env = getenv("BROTHER_TD_DEBUG_DIR");
    if (debug_dir_env != NULL && debug_dir_env[0] != '\0')
        snprintf(options->debug_directory, sizeof(options->debug_directory), "%s",
                 debug_dir_env);
}

int td_options_parse(td_options_t *options, int argc, char **argv)
{
    cups_option_t *cups_options = NULL;
    int num_options = 0;
    const char *value;

    if (argc < 6 || argv == NULL) {
        td_log(TD_LOG_ERROR, "invalid filter invocation");
        return -1;
    }

    options->job_id = td_parse_int(argv[1], 0, 0, 1000000000);
    options->title = argv[3] != NULL ? argv[3] : "";
    options->copies = td_parse_int(argv[4], 1, 1, 9999);

    if (argv[5] != NULL && argv[5][0] != '\0')
        num_options = cupsParseOptions(argv[5], 0, &cups_options);

    value = option_value(cups_options, num_options, "TDModel");
    if (value != NULL && value[0] != '\0')
        snprintf(options->model, sizeof(options->model), "%s", value);

    value = option_value(cups_options, num_options, "TDMediaType");
    options->media_type = td_media_type_parse(value, options->media_type);

    value = option_value(cups_options, num_options, "TDDither");
    options->dither = td_dither_parse(value, options->dither);

    value = option_value(cups_options, num_options, "TDThreshold");
    options->threshold = td_parse_int(value, options->threshold, 0, 255);

    value = option_value(cups_options, num_options, "TDScaling");
    options->scaling = parse_scaling(value);

    value = option_value(cups_options, num_options, "TDRotate");
    options->rotate = parse_rotation(value);

    value = option_value(cups_options, num_options, "TDInvert");
    options->invert = td_parse_bool(value, options->invert);

    value = option_value(cups_options, num_options, "TDMirror");
    options->mirror = td_parse_bool(value, options->mirror);

    value = option_value(cups_options, num_options, "TDBorder");
    options->border = td_parse_bool(value, options->border);

    value = option_value(cups_options, num_options, "TDMargin");
    options->margin_mm = td_parse_double(value, options->margin_mm, 0.0, 127.0);

    value = option_value(cups_options, num_options, "TDAutoCut");
    options->auto_cut = td_parse_bool(value, options->auto_cut);

    value = option_value(cups_options, num_options, "TDCutAtEnd");
    options->cut_at_end = td_parse_bool(value, options->cut_at_end);

    value = option_value(cups_options, num_options, "TDPeeler");
    options->peeler = td_parse_bool(value, options->peeler);

    value = option_value(cups_options, num_options, "TDAutoTrim");
    options->auto_trim = td_parse_bool(value, options->auto_trim);

    value = option_value(cups_options, num_options, "TDAlignX");
    options->align_x = parse_align_x(value);

    value = option_value(cups_options, num_options, "TDAlignY");
    options->align_y = parse_align_y(value);

    value = option_value(cups_options, num_options, "TDCompression");
    options->compression = parse_compression(value);

    value = option_value(cups_options, num_options, "TDDebug");
    if (td_parse_bool(value, false))
        options->debug = true;

    cupsFreeOptions(num_options, cups_options);

    /* The printer itself implements die-cut label pitch; a feed margin and
     * auto-cut are only meaningful for continuous media. */
    if (options->media_type == TD_MEDIA_DIECUT) {
        if (options->auto_cut || options->peeler)
            td_log(TD_LOG_WARN, "auto-cut/peeler disabled for die-cut labels");
        options->auto_cut = false;
        options->peeler = false;
        options->auto_trim = false;
        options->margin_mm = 0.0;
    } else {
        if (options->margin_mm < 3.0)
            options->margin_mm = 3.0;
        if (options->margin_mm > 127.0)
            options->margin_mm = 127.0;
    }

    if (options->copies < 1)
        options->copies = 1;

    return 0;
}

void td_options_log(const td_options_t *options)
{
    td_log(TD_LOG_DEBUG,
           "options: job=%d copies=%d model=%s media=%s dither=%s threshold=%d "
           "scale=%d rotate=%d invert=%d mirror=%d border=%d margin=%.1fmm "
           "autocut=%d cutend=%d peeler=%d autotrim=%d align=%d/%d compression=%d",
           options->job_id, options->copies,
           options->model[0] ? options->model : "(auto)", td_media_type_name(options->media_type),
           td_dither_name(options->dither), options->threshold, (int)options->scaling,
           options->rotate, options->invert, options->mirror, options->border,
           options->margin_mm, options->auto_cut, options->cut_at_end,
           options->peeler, options->auto_trim, (int)options->align_x,
           (int)options->align_y, (int)options->compression);
}
