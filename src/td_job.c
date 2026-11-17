/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include "td_job.h"

#include <math.h>
#include <string.h>

#include "td_dither.h"
#include "td_raster.h"
#include "td_util.h"

#define MM_PER_INCH 25.4
#define TD_MIN_CONTINUOUS_LENGTH_MM 12.0

static int model_dpi(const td_model_t *model, const cups_page_header2_t *header)
{
    if (model != NULL && model->dpi > 0)
        return model->dpi;
    if (header->HWResolution[0] > 0)
        return (int)header->HWResolution[0];
    return 203;
}

static double page_width_mm(const cups_page_header2_t *header, int dpi)
{
    double mm = (double)header->cupsPageSize[0] * MM_PER_INCH / 72.0;

    if (mm <= 0.0 && header->cupsWidth > 0 && dpi > 0)
        mm = (double)header->cupsWidth * MM_PER_INCH / (double)dpi;
    return mm;
}

static double page_length_mm(const cups_page_header2_t *header, int dpi)
{
    double mm = (double)header->cupsPageSize[1] * MM_PER_INCH / 72.0;

    if (mm <= 0.0 && header->cupsHeight > 0 && dpi > 0)
        mm = (double)header->cupsHeight * MM_PER_INCH / (double)dpi;
    return mm;
}

static int prepare_page(const td_gray_image *source, const td_options_t *options,
                        const td_geometry_t *geometry, td_bitmap *bitmap)
{
    td_gray_image working;
    td_gray_image placed;
    const td_gray_image *current = source;
    bool working_allocated = false;
    int result;

    memset(&working, 0, sizeof(working));
    memset(&placed, 0, sizeof(placed));

    if (options->rotate != 0) {
        if (td_gray_rotate(source, options->rotate, &working) < 0) {
            td_log(TD_LOG_ERROR, "cannot rotate image by %d degrees", options->rotate);
            return -1;
        }
        working_allocated = true;
    } else if (options->mirror) {
        working = td_gray_clone(source);
        working_allocated = true;
    }

    if (working_allocated) {
        current = &working;
        if (options->mirror)
            td_gray_mirror(&working);
    }

    if (td_gray_place(current, geometry->print_width_px, geometry->print_height_px,
                      options->scaling, options->align_x, options->align_y,
                      &placed) < 0) {
        td_log(TD_LOG_ERROR, "cannot scale image into the printable area");
        if (working_allocated)
            td_gray_free(&working);
        return -1;
    }

    if (working_allocated)
        td_gray_free(&working);

    result = td_dither_image(&placed, options->dither, options->threshold, bitmap);
    td_gray_free(&placed);

    if (result < 0) {
        td_log(TD_LOG_ERROR, "cannot dither page");
        return -1;
    }

    if (geometry->type == TD_MEDIA_CONTINUOUS && options->auto_trim &&
        !options->border) {
        int minimum = (int)ceil(TD_MIN_CONTINUOUS_LENGTH_MM *
                                (double)geometry->dpi / MM_PER_INCH);

        if (td_bitmap_trim(bitmap, minimum, options->invert))
            td_log(TD_LOG_DEBUG, "continuous page trimmed to %d rows", bitmap->height);
    }

    return 0;
}

int td_job_init(td_job_t *job, const td_options_t *options,
                const td_model_t *model, td_writer_t *writer)
{
    if (job == NULL || options == NULL || model == NULL || writer == NULL)
        return -1;

    memset(job, 0, sizeof(*job));
    job->options = *options;
    job->model = model;
    job->writer = writer;
    job->first_page = true;
    if (job->options.copies < 1)
        job->options.copies = 1;
    return 0;
}

int td_job_begin(td_job_t *job)
{
    if (job == NULL || job->writer == NULL)
        return -1;
    if (job->job_started)
        return 0;

    if (td_protocol_begin_job(&job->protocol, job->writer, job->model,
                              &job->options) < 0)
        return -1;

    job->job_started = true;
    return 0;
}

int td_job_set_page(td_job_t *job, const cups_page_header2_t *header)
{
    int dpi;
    double width_mm;
    double length_mm;

    if (job == NULL || header == NULL)
        return -1;

    dpi = model_dpi(job->model, header);
    width_mm = page_width_mm(header, dpi);
    length_mm = page_length_mm(header, dpi);

    if (width_mm <= 0.0 || length_mm <= 0.0) {
        td_log(TD_LOG_ERROR, "invalid page size (%.2f x %.2f mm)", width_mm, length_mm);
        return -1;
    }

    if (td_geometry_init(&job->geometry, job->model, job->options.media_type,
                         width_mm, length_mm) < 0) {
        td_log(TD_LOG_ERROR, "cannot determine label geometry");
        return -1;
    }

    if (job->geometry.type == TD_MEDIA_CONTINUOUS && header->cupsHeight > 0)
        job->geometry.print_height_px = (int)header->cupsHeight;

    job->geometry_ready = true;
    return 0;
}

int td_job_emit_gray(td_job_t *job, const td_gray_image *source, bool last_page)
{
    td_bitmap bitmap;
    int copies;
    int copy;

    if (job == NULL || source == NULL || !job->geometry_ready)
        return -1;
    if (td_job_begin(job) < 0)
        return -1;

    /* The previous page was held open because we did not know whether another
     * page would follow.  Now we do. */
    if (job->pending_page_end) {
        if (td_protocol_terminator(&job->protocol, false) < 0)
            return -1;
        job->pending_page_end = false;
    }

    if (prepare_page(source, &job->options, &job->geometry, &bitmap) < 0)
        return -1;

    copies = job->options.copies;
    if (copies < 1)
        copies = 1;

    for (copy = 0; copy < copies; copy++) {
        bool is_first = job->first_page && copy == 0;

        if (td_protocol_begin_page(&job->protocol, &job->geometry, bitmap.height,
                                   is_first) < 0) {
            td_bitmap_free(&bitmap);
            return -1;
        }
        if (td_protocol_emit_bitmap(&job->protocol, &bitmap, &job->geometry) < 0) {
            td_bitmap_free(&bitmap);
            return -1;
        }

        if (copy + 1 < copies) {
            if (td_protocol_end_page(&job->protocol, false) < 0) {
                td_bitmap_free(&bitmap);
                return -1;
            }
        }
    }

    td_bitmap_free(&bitmap);
    job->first_page = false;

    if (last_page) {
        if (td_protocol_end_page(&job->protocol, true) < 0)
            return -1;
    } else {
        job->pending_page_end = true;
    }

    return 0;
}

int td_job_process_cups_raster(td_job_t *job, cups_raster_t *raster,
                               const cups_page_header2_t *first_header)
{
    cups_page_header2_t header;
    cups_page_header2_t next_header;
    int status;

    if (job == NULL || raster == NULL)
        return -1;

    if (first_header != NULL) {
        header = *first_header;
        status = 1;
    } else {
        status = td_raster_read_header(raster, &header);
    }
    if (status < 0)
        return -1;
    if (status == 0)
        return 0;

    if (td_job_begin(job) < 0)
        return -1;

    for (;;) {
        td_gray_image gray;
        bool next_ok;

        memset(&gray, 0, sizeof(gray));

        if (td_job_set_page(job, &header) < 0)
            return -1;

        if (td_raster_read_gray(raster, &header, &gray) < 0) {
            td_gray_free(&gray);
            return -1;
        }

        memset(&next_header, 0, sizeof(next_header));
        status = td_raster_read_header(raster, &next_header);
        if (status < 0) {
            td_gray_free(&gray);
            return -1;
        }
        next_ok = status > 0;

        if (td_job_emit_gray(job, &gray, !next_ok) < 0) {
            td_gray_free(&gray);
            return -1;
        }
        td_gray_free(&gray);

        if (!next_ok)
            break;

        header = next_header;
    }

    return 0;
}

int td_job_finish(td_job_t *job)
{
    if (job == NULL)
        return -1;

    if (job->pending_page_end) {
        if (td_protocol_terminator(&job->protocol, true) < 0)
            return -1;
        job->pending_page_end = false;
        job->protocol.pages_written++;
    }

    if (job->job_started)
        return td_protocol_end_job(&job->protocol);

    if (job->writer != NULL)
        return td_writer_flush(job->writer);

    return 0;
}
