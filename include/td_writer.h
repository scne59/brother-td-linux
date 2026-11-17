/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#ifndef TD_WRITER_H
#define TD_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define TD_WRITER_BUFFER_SIZE (64 * 1024)

typedef ssize_t (*td_write_cb_t)(void *context, const void *data, size_t length);

typedef struct {
    int fd;
    int debug_fd;
    td_write_cb_t write_cb;
    void *write_context;
    unsigned char buffer[TD_WRITER_BUFFER_SIZE];
    size_t used;
    int error;
} td_writer_t;

void td_writer_init(td_writer_t *writer, int fd);
void td_writer_init_cb(td_writer_t *writer, td_write_cb_t write_cb, void *context);
void td_writer_set_debug(td_writer_t *writer, int debug_fd);
int td_writer_write(td_writer_t *writer, const void *data, size_t length);
int td_writer_putc(td_writer_t *writer, unsigned char byte);
int td_writer_flush(td_writer_t *writer);

#endif /* TD_WRITER_H */
