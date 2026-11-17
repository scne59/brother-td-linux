/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include "td_writer.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "td_util.h"

void td_writer_init(td_writer_t *writer, int fd)
{
    memset(writer, 0, sizeof(*writer));
    writer->fd = fd;
    writer->debug_fd = -1;
}

void td_writer_init_cb(td_writer_t *writer, td_write_cb_t write_cb, void *context)
{
    memset(writer, 0, sizeof(*writer));
    writer->fd = -1;
    writer->debug_fd = -1;
    writer->write_cb = write_cb;
    writer->write_context = context;
}

void td_writer_set_debug(td_writer_t *writer, int debug_fd)
{
    writer->debug_fd = debug_fd;
}

static int write_all(int fd, const unsigned char *data, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        ssize_t written = write(fd, data + offset, length - offset);

        if (written < 0) {
            if (errno == EINTR)
                continue;
            return errno;
        }
        if (written == 0)
            return EIO;
        offset += (size_t)written;
    }

    return 0;
}

static int flush_bytes(td_writer_t *writer, const unsigned char *data, size_t length)
{
    int error;

    if (writer->write_cb != NULL) {
        size_t offset = 0;

        while (offset < length) {
            ssize_t written = writer->write_cb(writer->write_context, data + offset,
                                               length - offset);

            if (written < 0) {
                writer->error = EIO;
                td_log(TD_LOG_ERROR, "cannot write printer data to device");
                return -1;
            }
            if (written == 0) {
                writer->error = EIO;
                td_log(TD_LOG_ERROR, "short write to printer device");
                return -1;
            }
            offset += (size_t)written;
        }
    } else {
        error = write_all(writer->fd, data, length);
        if (error != 0) {
            writer->error = error;
            td_log(TD_LOG_ERROR, "cannot write printer data: %s", strerror(error));
            return -1;
        }
    }

    if (writer->debug_fd >= 0) {
        error = write_all(writer->debug_fd, data, length);
        if (error != 0) {
            td_log(TD_LOG_WARN, "cannot write debug capture: %s", strerror(error));
            close(writer->debug_fd);
            writer->debug_fd = -1;
        }
    }

    return 0;
}

int td_writer_flush(td_writer_t *writer)
{
    int result;

    if (writer->error != 0)
        return -1;
    if (writer->used == 0)
        return 0;

    result = flush_bytes(writer, writer->buffer, writer->used);
    writer->used = 0;
    return result;
}

int td_writer_write(td_writer_t *writer, const void *data, size_t length)
{
    const unsigned char *bytes = data;

    if (writer->error != 0)
        return -1;
    if (length == 0)
        return 0;

    if (length >= sizeof(writer->buffer)) {
        if (td_writer_flush(writer) < 0)
            return -1;
        return flush_bytes(writer, bytes, length);
    }

    if (writer->used + length > sizeof(writer->buffer)) {
        if (td_writer_flush(writer) < 0)
            return -1;
    }

    memcpy(writer->buffer + writer->used, bytes, length);
    writer->used += length;
    return 0;
}

int td_writer_putc(td_writer_t *writer, unsigned char byte)
{
    return td_writer_write(writer, &byte, 1);
}
