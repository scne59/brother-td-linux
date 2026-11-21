/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "td_media.h"
#include "td_models.h"

static void check_model_lookup(void)
{
    const td_model_t *model;

    model = td_model_find("TD-4410D");
    assert(model != NULL && model->dpi == 203 && model->head_pins == 832);
    assert(strcmp(model->id, "TD-4410D") == 0);

    model = td_model_find("brother td-4410d");
    assert(model != NULL && strcmp(model->id, "TD-4410D") == 0);

    model = td_model_find("td4410d");
    assert(model != NULL && strcmp(model->id, "TD-4410D") == 0);

    model = td_model_find("TD-4550DNWB");
    assert(model != NULL && model->dpi == 300 && model->head_pins == 1280);

    assert(td_model_find("not-a-printer") == NULL);
    assert(td_model_for_dpi(203) != NULL);
    assert(td_model_for_dpi(300) != NULL);
}

static void check_widths(void)
{
    assert(td_printable_width_px(203, 102.0) == 788);
    assert(td_printable_width_px(300, 102.0) == 1164);
    assert(td_printable_width_px(203, 76.0) == 583);
    assert(td_printable_width_px(300, 76.0) == 861);
    assert(td_printable_width_px(203, 106.0) == 823);
    assert(td_printable_width_px(300, 106.0) == 1216);
    assert(td_printable_width_px(203, 39.0) == 288);
    assert(td_printable_width_px(300, 39.0) == 425);

    /* A non-standard width falls back to the 3 mm edge allowance. */
    assert(td_printable_width_px(203, 100.0) == 775);
}

static void check_geometry(void)
{
    const td_model_t *model = td_model_find("TD-4410D");
    td_geometry_t geometry;

    assert(model != NULL);
    assert(td_geometry_init(&geometry, model, TD_MEDIA_DIECUT, 102.0, 152.4) == 0);
    assert(geometry.head_pins == 832);
    assert(geometry.head_bytes == 104);
    assert(geometry.print_width_px == 788);
    assert(geometry.left_offset_px == 22);
    assert(geometry.right_offset_px == 22);
    assert(geometry.print_height_px == 1170);
    assert(geometry.media_width_mm_round == 102);
    assert(geometry.media_length_mm_round == 152);

    assert(td_geometry_init(&geometry, model, TD_MEDIA_CONTINUOUS, 102.0, 100.0) == 0);
    assert(geometry.media_length_mm_round == 0);
    assert(geometry.print_height_px == 799);

    model = td_model_find("TD-4510D");
    assert(model != NULL);
    assert(td_geometry_init(&geometry, model, TD_MEDIA_DIECUT, 102.0, 152.4) == 0);
    assert(geometry.head_pins == 1280);
    assert(geometry.head_bytes == 160);
    assert(geometry.print_width_px == 1164);
    assert(geometry.left_offset_px == 58);
    assert(geometry.right_offset_px == 58);
}

static void check_normalize(void)
{
    const td_model_t *model = td_model_find("TD-4410D");
    double width;
    double length;

    assert(model != NULL);

    width = 152.0;
    length = 102.0;
    assert(td_media_normalize_orientation(model, &width, &length));
    assert(width == 102.0 && length == 152.0);

    width = 102.0;
    length = 152.0;
    assert(!td_media_normalize_orientation(model, &width, &length));
    assert(width == 102.0 && length == 152.0);

    /* Too large in both directions: geometry clamping handles it. */
    width = 152.0;
    length = 152.0;
    assert(!td_media_normalize_orientation(model, &width, &length));
    assert(width == 152.0 && length == 152.0);

    width = 100.0;
    length = 150.0;
    assert(!td_media_normalize_orientation(model, &width, &length));
    assert(width == 100.0 && length == 150.0);
}

int main(void)
{
    check_model_lookup();
    check_widths();
    check_geometry();
    check_normalize();
    puts("test_media: OK");
    return 0;
}
