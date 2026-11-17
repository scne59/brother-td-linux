/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "td_image.h"
#include "td_media.h"
#include "td_models.h"
#include "td_options.h"
#include "td_packbits.h"
#include "td_protocol.h"
#include "td_util.h"
#include "td_writer.h"

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
} parser_t;

static uint8_t read_u8(parser_t *p)
{
    assert(p->offset < p->length);
    return p->data[p->offset++];
}

static void expect_bytes(parser_t *p, const uint8_t *expected, size_t length)
{
    assert(p->offset + length <= p->length);
    assert(memcmp(p->data + p->offset, expected, length) == 0);
    p->offset += length;
}

static uint8_t *capture(td_protocol_t *protocol, td_writer_t *writer, size_t *length)
{
    uint8_t *buffer;
    off_t size;

    assert(td_protocol_end_job(protocol) == 0);
    size = lseek(writer->fd, 0, SEEK_END);
    assert(size >= 0);
    buffer = malloc((size_t)size + 1);
    assert(buffer != NULL);
    assert(lseek(writer->fd, 0, SEEK_SET) == 0);
    assert(read(writer->fd, buffer, (size_t)size) == size);
    *length = (size_t)size;
    return buffer;
}

static void check_diecut_page(void)
{
    char filename[] = "/tmp/brother-td-protocol-XXXXXX";
    int fd = mkstemp(filename);
    td_writer_t writer;
    td_protocol_t protocol;
    td_options_t options;
    const td_model_t *model = td_model_find("TD-4410D");
    td_geometry_t geometry;
    td_bitmap bitmap;
    uint8_t *data;
    size_t length;
    parser_t parser;
    uint8_t line[104];
    uint8_t print_info[10];
    size_t i;

    assert(fd >= 0);
    unlink(filename);

    td_options_init(&options);
    options.media_type = TD_MEDIA_DIECUT;
    options.compression = TD_COMPRESSION_NONE;

    assert(model != NULL);
    assert(td_geometry_init(&geometry, model, TD_MEDIA_DIECUT, 102.0, 152.4) == 0);

    td_writer_init(&writer, fd);
    assert(td_protocol_begin_job(&protocol, &writer, model, &options) == 0);
    assert(td_protocol_begin_page(&protocol, &geometry, 2, true) == 0);

    bitmap = td_bitmap_alloc(geometry.print_width_px, 2);
    td_bitmap_set(&bitmap, 0, 0, true);
    td_bitmap_set(&bitmap, geometry.print_width_px - 1, 0, true);
    assert(td_protocol_emit_bitmap(&protocol, &bitmap, &geometry) == 0);
    assert(td_protocol_end_page(&protocol, true) == 0);

    data = capture(&protocol, &writer, &length);
    parser.data = data;
    parser.length = length;
    parser.offset = 0;

    for (i = 0; i < 350; i++)
        assert(read_u8(&parser) == 0x00);
    {
        static const uint8_t init[] = {0x1b, 0x40};
        expect_bytes(&parser, init, sizeof(init));
    }
    {
        static const uint8_t mode[] = {0x1b, 0x69, 0x61, 0x01};
        static const uint8_t status[] = {0x1b, 0x69, 0x21, 0x00};
        static const uint8_t print_info_cmd[] = {0x1b, 0x69, 0x7a};
        static const uint8_t various[] = {0x1b, 0x69, 0x4d, 0x00};
        static const uint8_t margin[] = {0x1b, 0x69, 0x64, 0x00, 0x00};
        static const uint8_t expanded[] = {0x1b, 0x69, 0x4b, 0x08};
        static const uint8_t compression[] = {0x4d, 0x00};

        expect_bytes(&parser, mode, sizeof(mode));
        expect_bytes(&parser, status, sizeof(status));
        expect_bytes(&parser, print_info_cmd, sizeof(print_info_cmd));
        for (i = 0; i < sizeof(print_info); i++)
            print_info[i] = read_u8(&parser);
        assert(print_info[0] == 0x8e);
        assert(print_info[1] == 0x0b);
        assert(print_info[2] == 102);
        assert(print_info[3] == 152);
        assert(print_info[4] == 2 && print_info[5] == 0);
        assert(print_info[8] == 0);

        expect_bytes(&parser, various, sizeof(various));
        expect_bytes(&parser, margin, sizeof(margin));
        expect_bytes(&parser, expanded, sizeof(expanded));
        expect_bytes(&parser, compression, sizeof(compression));
    }

    /* First raster row: g 00 68 + 104 bytes. */
    assert(read_u8(&parser) == 0x67);
    assert(read_u8(&parser) == 0x00);
    assert(read_u8(&parser) == 0x68);
    for (i = 0; i < sizeof(line); i++)
        line[i] = read_u8(&parser);

    /* Image column 0 is at pin 22, i.e. output bit 809. */
    assert(line[101] == 0x40);
    /* Image column 787 is at pin 809, i.e. output bit 22. */
    assert(line[2] == 0x02);
    assert(line[0] == 0x00);

    /* Second row is blank and must use "Z". */
    assert(read_u8(&parser) == 0x5a);
    assert(read_u8(&parser) == 0x1a);

    {
        static const uint8_t reset[] = {0x1b, 0x69, 0x61, 0xff};
        expect_bytes(&parser, reset, sizeof(reset));
    }
    assert(parser.offset == parser.length);

    td_bitmap_free(&bitmap);
    free(data);
    close(fd);
}

static void check_continuous_page(void)
{
    char filename[] = "/tmp/brother-td-protocol-XXXXXX";
    int fd = mkstemp(filename);
    td_writer_t writer;
    td_protocol_t protocol;
    td_options_t options;
    const td_model_t *model = td_model_find("TD-4410D");
    td_geometry_t geometry;
    uint8_t *data;
    size_t length;
    parser_t parser;
    uint8_t print_info[10];
    size_t i;

    assert(fd >= 0);
    unlink(filename);

    td_options_init(&options);
    options.media_type = TD_MEDIA_CONTINUOUS;

    assert(model != NULL);
    assert(td_geometry_init(&geometry, model, TD_MEDIA_CONTINUOUS, 102.0, 100.0) == 0);

    td_writer_init(&writer, fd);
    assert(td_protocol_begin_job(&protocol, &writer, model, &options) == 0);
    assert(td_protocol_begin_page(&protocol, &geometry, 10, false) == 0);

    data = capture(&protocol, &writer, &length);
    parser.data = data;
    parser.length = length;
    parser.offset = 0;

    for (i = 0; i < 350; i++)
        assert(read_u8(&parser) == 0x00);
    parser.offset += 2; /* ESC @ */
    parser.offset += 4; /* ESC i a */
    parser.offset += 4; /* ESC i ! */
    {
        static const uint8_t print_info_cmd[] = {0x1b, 0x69, 0x7a};

        expect_bytes(&parser, print_info_cmd, sizeof(print_info_cmd));
    }
    for (i = 0; i < sizeof(print_info); i++)
        print_info[i] = read_u8(&parser);

    assert(print_info[1] == 0x0a); /* continuous */
    assert(print_info[3] == 0);    /* media length is not used */
    assert(print_info[4] == 10);
    assert(print_info[8] == 1);    /* not the first page */

    /* ESC i M 00, ESC i d 18 00 (3 mm), ESC i K 08, M 02 */
    {
        static const uint8_t tail[] = {
            0x1b, 0x69, 0x4d, 0x00,
            0x1b, 0x69, 0x64, 0x18, 0x00,
            0x1b, 0x69, 0x4b, 0x08,
            0x4d, 0x02,
        };

        expect_bytes(&parser, tail, sizeof(tail));
    }

    free(data);
    close(fd);
}

static void check_line_options(void)
{
    const td_model_t *model = td_model_find("TD-4410D");
    td_geometry_t geometry;
    td_bitmap bitmap;
    uint8_t line[104];
    int i;

    assert(model != NULL);
    assert(td_geometry_init(&geometry, model, TD_MEDIA_DIECUT, 102.0, 152.4) == 0);

    bitmap = td_bitmap_alloc(geometry.print_width_px, 1);

    /* White image with black side margins. */
    td_protocol_build_line(&bitmap, 0, &geometry, true, false, line);
    assert(line[0] == 0xff);
    assert(line[1] == 0xff);
    assert((line[2] & 0xc0) == 0xc0); /* pins 16..21 -> bits 2..7 */
    assert(line[101] == 0x3f);        /* pins 810..815 -> bits 1..6 */
    assert(line[102] == 0xff);
    assert(line[103] == 0xff);

    /* Inversion flips the whole line. */
    td_protocol_build_line(&bitmap, 0, &geometry, true, true, line);
    assert(line[0] == 0x00);
    assert(line[102] == 0x00);

    /* A black image with no border has only the imageable area set. */
    for (i = 0; i < bitmap.width; i++)
        td_bitmap_set(&bitmap, i, 0, true);
    td_protocol_build_line(&bitmap, 0, &geometry, false, false, line);
    for (i = 0; i < 2; i++)
        assert(line[i] == 0x00);
    assert(line[2] == 0x03);  /* first two image pins at bits 6 and 7 */
    assert(line[101] == 0xc0);

    td_bitmap_free(&bitmap);
}

static void check_compression(void)
{
    const td_model_t *model = td_model_find("TD-4410D");
    td_geometry_t geometry;
    td_options_t options;
    td_bitmap bitmap;
    uint8_t raw[104];
    uint8_t decoded[256];
    uint8_t payload[256];
    uint8_t *data;
    size_t length;
    size_t encoded;
    parser_t parser;
    char filename[] = "/tmp/brother-td-protocol-XXXXXX";
    int fd = mkstemp(filename);
    td_writer_t writer;
    td_protocol_t protocol;
    size_t i;

    assert(fd >= 0);
    unlink(filename);
    assert(model != NULL);
    assert(td_geometry_init(&geometry, model, TD_MEDIA_DIECUT, 102.0, 152.4) == 0);

    td_options_init(&options);
    options.compression = TD_COMPRESSION_TIFF;

    bitmap = td_bitmap_alloc(geometry.print_width_px, 1);
    for (i = 0; i < (size_t)bitmap.width; i++)
        td_bitmap_set(&bitmap, i, 0, true);

    td_protocol_build_line(&bitmap, 0, &geometry, false, false, raw);

    td_writer_init(&writer, fd);
    assert(td_protocol_begin_job(&protocol, &writer, model, &options) == 0);
    assert(td_protocol_begin_page(&protocol, &geometry, 1, true) == 0);
    assert(td_protocol_emit_bitmap(&protocol, &bitmap, &geometry) == 0);
    data = capture(&protocol, &writer, &length);

    parser.data = data;
    parser.length = length;
    parser.offset = 0;
    parser.offset += 350 + 2;
    parser.offset += 4 + 4 + 3 + 10 + 4 + 5 + 4 + 2;

    assert(read_u8(&parser) == 0x67);
    assert(read_u8(&parser) == 0x00);
    encoded = read_u8(&parser);
    assert(encoded > 0 && encoded <= sizeof(payload));
    for (i = 0; i < encoded; i++)
        payload[i] = read_u8(&parser);

    if (encoded == 105 && payload[0] == 104) {
        assert(memcmp(payload + 1, raw, 104) == 0);
    } else {
        size_t decoded_length = td_packbits_decode(payload, encoded, decoded, sizeof(decoded));

        assert(decoded_length == 104);
        assert(memcmp(decoded, raw, 104) == 0);
    }

    td_bitmap_free(&bitmap);
    free(data);
    close(fd);
}

int main(void)
{
    check_diecut_page();
    check_continuous_page();
    check_line_options();
    check_compression();
    puts("test_protocol: OK");
    return 0;
}
