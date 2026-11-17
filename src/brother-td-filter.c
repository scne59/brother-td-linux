/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 *
 * CUPS raster filter for Brother TD-4D label printers.
 *
 * Input:  application/vnd.cups-raster on stdin
 * Output: Brother TD raster command stream on stdout
 */

#include <cups/cups.h>
#include <cups/raster.h>

#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "td_job.h"
#include "td_media.h"
#include "td_models.h"
#include "td_options.h"
#include "td_raster.h"
#include "td_util.h"
#include "td_version.h"
#include "td_writer.h"

static void usage(FILE *stream)
{
    fprintf(stream,
            "brother-td-filter %s\n"
            "CUPS raster filter for Brother TD-4D label printers.\n\n"
            "This program is normally run by cupsd.  It reads a CUPS raster\n"
            "stream from standard input and writes Brother TD raster commands\n"
            "to standard output.\n\n"
            "Options are supplied by the PPD (TDMediaType, TDDither, ...).\n"
            "Set BROTHER_TD_DEBUG=1 for diagnostic logging.\n",
            TD_VERSION);
}

static const td_model_t *choose_model(const td_options_t *options,
                                      const cups_page_header2_t *header)
{
    static td_model_t fallback;
    const td_model_t *model = NULL;
    int dpi = header->HWResolution[0] > 0 ? (int)header->HWResolution[0] : 0;

    if (options->model[0] != '\0') {
        model = td_model_find(options->model);
        if (model == NULL)
            td_log(TD_LOG_WARN, "unknown TDModel %s; inferring from resolution",
                   options->model);
    }

    if (model == NULL) {
        /* CUPS does not pass PPD default custom options to filters, so the
         * model usually has to be inferred from the raster resolution.  The
         * only model-specific value that matters at this point is the
         * invalidate length; the 661-byte form is safe for both generations
         * because NUL is a skipped command.  Use the larger value so newer
         * models are handled correctly when no explicit TDModel is present. */
        if (dpi <= 0)
            dpi = 203;

        memset(&fallback, 0, sizeof(fallback));
        fallback.id = dpi == 300 ? "TD-4D 300dpi (auto)" : "TD-4D 203dpi (auto)";
        fallback.dpi = dpi;
        fallback.head_pins = dpi == 300 ? 1280 : 832;
        fallback.init_nuls = 661;
        fallback.max_width_mm = dpi == 300 ? 108 : 106;
        model = &fallback;

        td_log(TD_LOG_DEBUG, "no TDModel option; using %s inference (%d dpi)",
               model->id, dpi);
    }

    if (model != NULL && dpi > 0 && model->dpi != dpi)
        td_log(TD_LOG_WARN, "PPD model %s is %d dpi but the raster is %d dpi",
               model->id, model->dpi, dpi);

    return model;
}

int main(int argc, char **argv)
{
    td_options_t options;
    td_writer_t writer;
    td_job_t job;
    cups_raster_t *raster = NULL;
    cups_page_header2_t header;
    const td_model_t *model;
    int header_status;
    int debug_fd = -1;
    int result = 1;

    setlocale(LC_ALL, "");
    signal(SIGPIPE, SIG_IGN);

    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)) {
        printf("brother-td-filter %s\n", TD_VERSION);
        return 0;
    }
    if (argc < 6) {
        usage(stderr);
        return 1;
    }

    td_options_init(&options);
    if (td_options_parse(&options, argc, argv) < 0)
        return 1;

    td_debug_enabled = options.debug ? 1 : 0;
    td_options_log(&options);

    raster = cupsRasterOpen(0, CUPS_RASTER_READ);
    if (raster == NULL) {
        td_log(TD_LOG_ERROR, "cannot read CUPS raster data from standard input");
        return 1;
    }

    header_status = td_raster_read_header(raster, &header);
    if (header_status < 0)
        goto cleanup;
    if (header_status == 0) {
        td_log(TD_LOG_WARN, "empty raster job");
        result = 0;
        goto cleanup;
    }

    model = choose_model(&options, &header);
    if (model == NULL) {
        td_log(TD_LOG_ERROR,
               "cannot determine printer model (set TDModel or use a 203/300 dpi PPD)");
        goto cleanup;
    }

    if (header.HWResolution[0] != 0 && header.HWResolution[0] != (unsigned)model->dpi)
        td_log(TD_LOG_WARN, "using %d dpi model settings for a %u dpi raster",
               model->dpi, header.HWResolution[0]);

    td_log(TD_LOG_INFO, "job %d: %s, %s media, %d copies", options.job_id,
           model->id, td_media_type_name(options.media_type), options.copies);

    td_writer_init(&writer, 1);
    if (td_job_init(&job, &options, model, &writer) < 0)
        goto cleanup;

    if (options.debug && options.debug_directory[0] != '\0') {
        char path[PATH_MAX];

        snprintf(path, sizeof(path), "%s/job-%d.prn", options.debug_directory,
                 options.job_id);
        debug_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (debug_fd >= 0) {
            td_writer_set_debug(&writer, debug_fd);
            td_log(TD_LOG_INFO, "writing raw printer stream to %s", path);
        } else {
            td_log(TD_LOG_WARN, "cannot open debug capture %s", path);
        }
    }

    if (td_job_process_cups_raster(&job, raster, &header) < 0)
        goto cleanup;

    if (td_job_finish(&job) < 0)
        goto cleanup;

    td_log(TD_LOG_INFO, "job %d complete: %u page(s) sent", options.job_id,
           job.protocol.pages_written);
    result = 0;

cleanup:
    if (raster != NULL)
        cupsRasterClose(raster);
    if (debug_fd >= 0)
        close(debug_fd);
    return result;
}
