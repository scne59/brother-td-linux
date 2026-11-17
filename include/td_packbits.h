/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#ifndef TD_PACKBITS_H
#define TD_PACKBITS_H

#include <stddef.h>
#include <stdint.h>

/* TIFF/PackBits encoder used by the Brother TD "M 2" compression mode.
 * Returns the encoded length, or 0 if the destination is too small. */
size_t td_packbits_encode(const uint8_t *source, size_t length,
                          uint8_t *destination, size_t destination_size);

/* Decoder, primarily for tests and diagnostics.  Returns the decoded length,
 * or 0 on malformed input. */
size_t td_packbits_decode(const uint8_t *source, size_t length,
                          uint8_t *destination, size_t destination_size);

#endif /* TD_PACKBITS_H */
