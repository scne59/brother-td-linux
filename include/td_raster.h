/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#ifndef TD_RASTER_H
#define TD_RASTER_H

#include <cups/raster.h>

#include "td_image.h"

/* Read the next CUPS raster page header.  Returns 1 on success, 0 at end of
 * file, and -1 on error. */
int td_raster_read_header(cups_raster_t *raster, cups_page_header2_t *header);

/* Convert the pixels of the current page to an 8-bit grayscale image. */
int td_raster_read_gray(cups_raster_t *raster, const cups_page_header2_t *header,
                        td_gray_image *image);

/* Convert one raster line into an existing grayscale image.  This is used by
 * CUPS raster readers and by the PAPPL line callback. */
int td_raster_convert_row(const cups_page_header2_t *header,
                          const unsigned char *row, int y, td_gray_image *image);

#endif /* TD_RASTER_H */
