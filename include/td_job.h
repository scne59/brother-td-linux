/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#ifndef TD_JOB_H
#define TD_JOB_H

#include <cups/raster.h>

#include "td_image.h"
#include "td_media.h"
#include "td_models.h"
#include "td_options.h"
#include "td_protocol.h"
#include "td_writer.h"

typedef struct {
    td_options_t options;
    const td_model_t *model;
    td_geometry_t geometry;
    td_protocol_t protocol;
    td_writer_t *writer;
    bool job_started;
    bool geometry_ready;
    bool first_page;
    bool pending_page_end;
} td_job_t;

int td_job_init(td_job_t *job, const td_options_t *options,
                const td_model_t *model, td_writer_t *writer);
int td_job_begin(td_job_t *job);
int td_job_set_page(td_job_t *job, const cups_page_header2_t *header);
int td_job_emit_gray(td_job_t *job, const td_gray_image *source, bool last_page);
int td_job_process_cups_raster(td_job_t *job, cups_raster_t *raster,
                               const cups_page_header2_t *first_header);
int td_job_finish(td_job_t *job);

#endif /* TD_JOB_H */
