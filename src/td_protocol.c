/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include "td_protocol.h"

#include <math.h>
#include <string.h>

#include "td_packbits.h"
#include "td_util.h"

#define TD_CMD_INIT 0x1b, 0x40
#define TD_CMD_RASTER_MODE 0x1b, 0x69, 0x61, 0x01
#define TD_CMD_STATUS_OFF 0x1b, 0x69, 0x21, 0x00
#define TD_CMD_PRINT_INFO 0x1b, 0x69, 0x7a
#define TD_CMD_VARIOUS 0x1b, 0x69, 0x4d
#define TD_CMD_MARGIN 0x1b, 0x69, 0x64
#define TD_CMD_EXPANDED 0x1b, 0x69, 0x4b
#define TD_CMD_COMPRESSION 0x4d
#define TD_CMD_GRAPHICS 0x67, 0x00
#define TD_CMD_ZERO 0x5a
#define TD_CMD_FF 0x0c
#define TD_CMD_CTRL_Z 0x1a
#define TD_CMD_MODE_RESET 0x1b, 0x69, 0x61, 0xff

static int write_bytes(td_protocol_t *protocol, const void *data, size_t length)
{
    return td_writer_write(protocol->writer, data, length);
}

static int write_byte(td_protocol_t *protocol, uint8_t value)
{
    return td_writer_putc(protocol->writer, value);
}

static int write_zeros(td_protocol_t *protocol, size_t count)
{
    static const uint8_t zeros[256] = {0};

    while (count > 0) {
        size_t chunk = count > sizeof(zeros) ? sizeof(zeros) : count;

        if (write_bytes(protocol, zeros, chunk) < 0)
            return -1;
        count -= chunk;
    }
    return 0;
}

int td_protocol_begin_job(td_protocol_t *protocol, td_writer_t *writer,
                          const td_model_t *model, const td_options_t *options)
{
    if (protocol == NULL || writer == NULL || model == NULL || options == NULL)
        return -1;

    memset(protocol, 0, sizeof(*protocol));
    protocol->writer = writer;
    protocol->model = model;
    protocol->options = options;

    /* The invalidate sequence is a run of NUL bytes.  NUL is a no-op to the
     * printer; the length depends on the model generation. */
    if (write_zeros(protocol, (size_t)model->init_nuls) < 0)
        return -1;

    {
        static const uint8_t init[] = {TD_CMD_INIT};

        if (write_bytes(protocol, init, sizeof(init)) < 0)
            return -1;
    }

    td_log(TD_LOG_DEBUG, "job: initialized %s with %d NUL bytes",
           model->id, model->init_nuls);
    return 0;
}

static int margin_dots(const td_options_t *options, const td_geometry_t *geometry)
{
    double dots;

    if (geometry->type == TD_MEDIA_DIECUT)
        return 0;

    dots = options->margin_mm * (double)geometry->dpi / 25.4;
    if (dots < 0.0)
        dots = 0.0;
    if (dots > 65535.0)
        dots = 65535.0;

    return (int)ceil(dots);
}

int td_protocol_begin_page(td_protocol_t *protocol, const td_geometry_t *geometry,
                           int raster_lines, bool first_page)
{
    const td_options_t *options;
    uint8_t print_info[10];
    uint8_t flags = 0;
    uint8_t compression;
    uint8_t expanded;
    int margin;
    uint32_t lines;

    if (protocol == NULL || geometry == NULL || raster_lines <= 0)
        return -1;

    options = protocol->options;
    lines = (uint32_t)raster_lines;

    {
        static const uint8_t mode[] = {TD_CMD_RASTER_MODE};
        static const uint8_t status[] = {TD_CMD_STATUS_OFF};

        if (write_bytes(protocol, mode, sizeof(mode)) < 0)
            return -1;
        if (write_bytes(protocol, status, sizeof(status)) < 0)
            return -1;
    }

    /* ESC i z: print information. */
    print_info[0] = 0x8e; /* printer recovery + media type + width + length */
    print_info[1] = geometry->type == TD_MEDIA_CONTINUOUS ? 0x0a : 0x0b;
    print_info[2] = (uint8_t)geometry->media_width_mm_round;
    print_info[3] = (uint8_t)geometry->media_length_mm_round;
    print_info[4] = (uint8_t)(lines & 0xffu);
    print_info[5] = (uint8_t)((lines >> 8) & 0xffu);
    print_info[6] = (uint8_t)((lines >> 16) & 0xffu);
    print_info[7] = (uint8_t)((lines >> 24) & 0xffu);
    print_info[8] = first_page ? 0x00 : 0x01;
    print_info[9] = 0x00;

    {
        static const uint8_t command[] = {TD_CMD_PRINT_INFO};

        if (write_bytes(protocol, command, sizeof(command)) < 0)
            return -1;
        if (write_bytes(protocol, print_info, sizeof(print_info)) < 0)
            return -1;
    }

    /* ESC i M: peeler and auto-cut flags. */
    if (options->peeler)
        flags |= 0x10;
    if (options->auto_cut)
        flags |= 0x40;

    {
        static const uint8_t command[] = {TD_CMD_VARIOUS};

        if (write_bytes(protocol, command, sizeof(command)) < 0)
            return -1;
        if (write_byte(protocol, flags) < 0)
            return -1;
    }

    /* ESC i d: feed margin (die-cut labels always use zero). */
    margin = margin_dots(options, geometry);
    {
        static const uint8_t command[] = {TD_CMD_MARGIN};

        if (write_bytes(protocol, command, sizeof(command)) < 0)
            return -1;
        if (write_byte(protocol, (uint8_t)(margin & 0xff)) < 0)
            return -1;
        if (write_byte(protocol, (uint8_t)((margin >> 8) & 0xff)) < 0)
            return -1;
    }

    /* ESC i K: bit 3 controls cutting at the end of the job. */
    expanded = options->cut_at_end ? 0x08 : 0x00;
    {
        static const uint8_t command[] = {TD_CMD_EXPANDED};

        if (write_bytes(protocol, command, sizeof(command)) < 0)
            return -1;
        if (write_byte(protocol, expanded) < 0)
            return -1;
    }

    /* Select no compression or TIFF/PackBits. */
    compression = options->compression == TD_COMPRESSION_TIFF ? 0x02 : 0x00;
    if (write_byte(protocol, TD_CMD_COMPRESSION) < 0)
        return -1;
    if (write_byte(protocol, compression) < 0)
        return -1;

    td_log(TD_LOG_DEBUG,
           "page: %ux%u dots, media %dx%d mm, type=%s, first=%d, lines=%u, "
           "margin=%d dots, flags=0x%02x, compression=%u",
           (unsigned)geometry->print_width_px, (unsigned)raster_lines,
           geometry->media_width_mm_round, geometry->media_length_mm_round,
           td_media_type_name(geometry->type), first_page, lines, margin,
           flags, compression);

    return 0;
}

void td_protocol_build_line(const td_bitmap *bitmap, int row,
                            const td_geometry_t *geometry, bool border,
                            bool invert, uint8_t *line)
{
    int head_pins;
    int image_left;
    int k;
    int minimum_pin;
    int maximum_pin;

    if (line == NULL || geometry == NULL)
        return;

    head_pins = geometry->head_pins;
    memset(line, 0, (size_t)geometry->head_bytes);

    if (bitmap == NULL || bitmap->width <= 0 || bitmap->height <= 0 || row < 0 ||
        row >= bitmap->height || geometry->print_width_px <= 0) {
        if (border) {
            for (k = 0; k < head_pins; k++) {
                int pin = head_pins - 1 - k;

                if (pin < geometry->left_offset_px ||
                    pin >= geometry->left_offset_px + geometry->print_width_px)
                    line[k / 8] |= (uint8_t)(0x80u >> (k % 8));
            }
        }
        if (invert) {
            for (k = 0; k < geometry->head_bytes; k++)
                line[k] ^= 0xff;
        }
        return;
    }

    /* Normally bitmap->width == print_width_px.  If a caller supplies another
     * width, center it in the imageable area. */
    image_left = geometry->left_offset_px +
                 (geometry->print_width_px - bitmap->width) / 2;
    minimum_pin = image_left;
    maximum_pin = image_left + bitmap->width;

    for (k = 0; k < head_pins; k++) {
        int pin = head_pins - 1 - k;
        bool black;

        if (pin >= geometry->left_offset_px &&
            pin < geometry->left_offset_px + geometry->print_width_px &&
            pin >= minimum_pin && pin < maximum_pin) {
            black = td_bitmap_get(bitmap, pin - image_left, row);
        } else {
            black = border;
        }

        if (invert)
            black = !black;

        if (black)
            line[k / 8] |= (uint8_t)(0x80u >> (k % 8));
    }
}

static bool line_is_zero(const uint8_t *line, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++) {
        if (line[i] != 0)
            return false;
    }
    return true;
}

static int emit_line(td_protocol_t *protocol, const uint8_t *line,
                     const td_geometry_t *geometry)
{
    uint8_t payload[TD_MAX_HEAD_BYTES + TD_MAX_HEAD_BYTES / 128 + 8];
    uint8_t header[3];
    size_t encoded;
    size_t length;

    if (line_is_zero(line, (size_t)geometry->head_bytes)) {
        return write_byte(protocol, TD_CMD_ZERO);
    }

    if (protocol->options->compression == TD_COMPRESSION_TIFF) {
        encoded = td_packbits_encode(line, (size_t)geometry->head_bytes, payload,
                                     sizeof(payload));
        if (encoded > 0 && encoded < (size_t)geometry->head_bytes) {
            length = encoded;
        } else {
            /* In compression mode an incompressible line is sent as one
             * length byte followed by the full raw line. */
            payload[0] = (uint8_t)geometry->head_bytes;
            memcpy(payload + 1, line, (size_t)geometry->head_bytes);
            length = (size_t)geometry->head_bytes + 1;
        }
    } else {
        memcpy(payload, line, (size_t)geometry->head_bytes);
        length = (size_t)geometry->head_bytes;
    }

    header[0] = 0x67; /* 'g' */
    header[1] = 0x00;
    header[2] = (uint8_t)length;

    if (write_bytes(protocol, header, sizeof(header)) < 0)
        return -1;
    if (write_bytes(protocol, payload, length) < 0)
        return -1;

    return 0;
}

int td_protocol_emit_bitmap(td_protocol_t *protocol, const td_bitmap *bitmap,
                            const td_geometry_t *geometry)
{
    uint8_t line[TD_MAX_HEAD_BYTES];
    int y;

    if (protocol == NULL || bitmap == NULL || geometry == NULL)
        return -1;
    if (geometry->head_bytes <= 0 || geometry->head_bytes > TD_MAX_HEAD_BYTES)
        return -1;

    for (y = 0; y < bitmap->height; y++) {
        td_protocol_build_line(bitmap, y, geometry, protocol->options->border,
                               protocol->options->invert, line);
        if (emit_line(protocol, line, geometry) < 0)
            return -1;
    }

    return 0;
}

int td_protocol_terminator(td_protocol_t *protocol, bool last_page)
{
    if (protocol == NULL)
        return -1;

    return write_byte(protocol, last_page ? TD_CMD_CTRL_Z : TD_CMD_FF);
}

int td_protocol_end_page(td_protocol_t *protocol, bool last_page)
{
    if (td_protocol_terminator(protocol, last_page) < 0)
        return -1;

    protocol->pages_written++;
    return 0;
}

int td_protocol_end_job(td_protocol_t *protocol)
{
    static const uint8_t reset[] = {TD_CMD_MODE_RESET};

    if (protocol == NULL)
        return -1;

    if (write_bytes(protocol, reset, sizeof(reset)) < 0)
        return -1;

    return td_writer_flush(protocol->writer);
}
