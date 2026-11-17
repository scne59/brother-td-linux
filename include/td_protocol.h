/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#ifndef TD_PROTOCOL_H
#define TD_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "td_image.h"
#include "td_media.h"
#include "td_models.h"
#include "td_options.h"
#include "td_writer.h"

#define TD_MAX_HEAD_PINS 1280
#define TD_MAX_HEAD_BYTES 160

typedef struct {
    td_writer_t *writer;
    const td_model_t *model;
    const td_options_t *options;
    unsigned pages_written;
} td_protocol_t;

int td_protocol_begin_job(td_protocol_t *protocol, td_writer_t *writer,
                          const td_model_t *model, const td_options_t *options);
int td_protocol_begin_page(td_protocol_t *protocol, const td_geometry_t *geometry,
                           int raster_lines, bool first_page);
int td_protocol_emit_bitmap(td_protocol_t *protocol, const td_bitmap *bitmap,
                            const td_geometry_t *geometry);
int td_protocol_terminator(td_protocol_t *protocol, bool last_page);
int td_protocol_end_page(td_protocol_t *protocol, bool last_page);
int td_protocol_end_job(td_protocol_t *protocol);

/* Build one print-head line from a bitmap.  This is exposed for unit tests. */
void td_protocol_build_line(const td_bitmap *bitmap, int row,
                            const td_geometry_t *geometry, bool border,
                            bool invert, uint8_t *line);

#endif /* TD_PROTOCOL_H */
