/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#ifndef TD_MODELS_H
#define TD_MODELS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *id;
    int dpi;
    int head_pins;
    int init_nuls;
    int max_width_mm;
    uint16_t usb_vid;
    uint16_t usb_pid;
} td_model_t;

extern const td_model_t td_models[];
extern const size_t td_num_models;

const td_model_t *td_model_find(const char *id);
const td_model_t *td_model_for_dpi(int dpi);

#endif /* TD_MODELS_H */
