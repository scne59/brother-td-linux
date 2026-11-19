/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 *
 * Native PAPPL printer application for Brother TD-4D label printers.
 *
 * This program exposes the labelled printer to CUPS 3 / IPP Everywhere
 * clients through PAPPL.  It accepts PWG/Apple raster directly through the
 * PAPPL raster callbacks and uses Ghostscript to convert PDF/PostScript jobs
 * to CUPS raster for the shared TD job pipeline.
 */

#include <pappl/pappl.h>

#include <cups/cups.h>
#include <cups/pwg.h>
#include <cups/raster.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <unistd.h>

#include "td_image.h"
#include "td_job.h"
#include "td_media.h"
#include "td_models.h"
#include "td_options.h"
#include "td_raster.h"
#include "td_util.h"
#include "td_version.h"
#include "td_writer.h"

/* PAPPL 1.0.x predates these two features.  Keep the numeric option bit so a
 * binary built against 1.0.x still enables no-TLS when it runs with a newer
 * libpappl, and resolve first-run discovery at runtime through a weak symbol. */
#ifndef PAPPL_SOPTIONS_NO_TLS
#  define PAPPL_SOPTIONS_NO_TLS 0x0400
#endif

#if !HAVE_PAPPL_CREATE_PRINTERS
extern bool papplSystemCreatePrinters(pappl_system_t *system,
                                      pappl_devtype_t types,
                                      pappl_pr_create_cb_t cb,
                                      void *cb_data) __attribute__((weak));
#endif

typedef struct {
    int width_mm;
    int length_mm;
} td_media_size_t;

typedef struct {
    const char *type;
    const char *text;
} td_text_resource_t;

typedef struct {
    td_job_t job;
    td_writer_t writer;
    td_gray_image image;
    td_options_t options;
} td_raster_job_t;

static void copy_string(char *dest, const char *src, size_t destsize)
{
    if (dest == NULL || destsize == 0)
        return;
    if (src == NULL)
        src = "";
    snprintf(dest, destsize, "%s", src);
}

static int system_port(pappl_system_t *system)
{
#if HAVE_PAPPL_MODERN_API
    return papplSystemGetHostPort(system);
#else
    return papplSystemGetPort(system);
#endif
}

static char *system_host_name(pappl_system_t *system, char *buffer,
                              size_t bufsize)
{
#if HAVE_PAPPL_MODERN_API
    return papplSystemGetHostName(system, buffer, bufsize);
#else
    return papplSystemGetHostname(system, buffer, bufsize);
#endif
}

static const td_media_size_t media_sizes[] = {
    {102, 152}, {102, 127}, {102, 114}, {102, 102}, {102, 100},
    {102, 76},  {102, 50},  {102, 38},  {102, 25},
    {106, 152}, {106, 100},
    {90, 100},  {80, 100},
    {76, 127},  {76, 100},  {76, 76},  {76, 51},  {76, 38},  {76, 26},
    {58, 100},
    {51, 76},   {51, 51},   {51, 38},  {51, 26},
    {39, 100},

    /* Standard PWG sizes.  Android's built-in and Mopria print services
     * ignore custom media names and only display a fixed whitelist of
     * standard sizes, so these give Android more selectable label sizes.
     * They are advertised but not added to the ready list below. */
    {89, 127},  {51, 89},   {54, 86},  {89, 119},
    {100, 148}, {52, 74},   {74, 105}, {105, 148},
    {89, 89},   {100, 150}, {55, 85},
    {37, 52},   {26, 37},
};

#define NUM_MEDIA_SIZES (int)(sizeof(media_sizes) / sizeof(media_sizes[0]))

/* Pin standard entries to their official PWG names.  Older libcups versions
 * have a smaller media table and otherwise report some of these as custom_…
 * names, which Android's print service will not display. */
static const char * const media_pwg_names[NUM_MEDIA_SIZES] = {
    [0]  = "na_index-4x6_4x6in",
    [3]  = "oe_square-photo_4x4in",
    [13] = "na_index-3x5_3x5in",
    [25] = "oe_photo-l_3.5x5in",
    [26] = "oe_business-card_2x3.5in",
    [27] = "om_card_54x86mm",
    [28] = "om_dsc-photo_89x119mm",
    [29] = "jpn_hagaki_100x148mm",
    [30] = "iso_a8_52x74mm",
    [31] = "iso_a7_74x105mm",
    [32] = "iso_a6_105x148mm",
    [33] = "om_square-photo_89x89mm",
    [34] = "om_small-photo_100x150mm",
    [35] = "om_business-card_55x85mm",
    [36] = "iso_a9_37x52mm",
    [37] = "iso_a10_26x37mm",
};

/* Mobile print dialogs, especially AirPrint, build the paper-size list from
 * media-col-ready and expect one size per media source.  A label printer has
 * one physical roll, so expose the common sizes as virtual rolls.  The list is
 * limited to PAPPL_MAX_SOURCE (16) entries. */
static const int ready_media_indices[] = {
    0, 1, 2, 3, 5, 6, 7, 8,
    13, 14, 15, 16, 17, 19, 20, 24
};

#define NUM_READY_MEDIA (int)(sizeof(ready_media_indices) / sizeof(ready_media_indices[0]))

_Static_assert(sizeof(ready_media_indices) / sizeof(ready_media_indices[0]) <= PAPPL_MAX_SOURCE,
               "ready_media_indices has more entries than PAPPL_MAX_SOURCE");

static const char *vendor_names[] = {
    "td-media-type", "td-dither", "td-compression", "td-scaling",
    "td-rotate", "td-margin", "td-threshold", "td-align-x", "td-align-y",
    "td-invert", "td-mirror", "td-border", "td-auto-trim",
    "td-auto-cut", "td-cut-at-end", "td-peeler",
};

#define NUM_VENDOR (int)(sizeof(vendor_names) / sizeof(vendor_names[0]))

/* CUPS/libppd localizes standard inch media names with a UTF-8 double-prime
 * (for example "4 x 6″") but writes the PPD with *LanguageEncoding: ISOLatin1.
 * Clients that honor the PPD encoding then show mojibake.  Supply an ASCII
 * catalog so the generated PPD uses "4 x 6 in" instead. */
static const char fallback_css[] =
    "body{font-family:system-ui,sans-serif;margin:2rem;background:#fff;color:#111}"
    "a{color:#0f6cbd}"
    "table{border-collapse:collapse}"
    "td,th{padding:.4rem .6rem;border-bottom:1px solid #ddd}";

static const char media_strings[] =
    "\"media.na_index-4x6_4x6in\" = \"4 x 6 in\";\n"
    "\"media.na_index-3x5_3x5in\" = \"3 x 5 in\";\n"
    "\"media.oe_square-photo_4x4in\" = \"4 x 4 in\";\n"
    "\"media.oe_photo-l_3.5x5in\" = \"3.5 x 5 in\";\n"
    "\"media.oe_business-card_2x3.5in\" = \"2 x 3.5 in\";\n";

/* Small ASCII SVG label icon used for the web UI favicon and navigation. */
static const char icon_svg[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"32\" "
    "viewBox=\"0 0 32 32\">"
    "<rect x=\"4\" y=\"6\" width=\"24\" height=\"20\" rx=\"2\" "
    "fill=\"none\" stroke=\"#0f6cbd\" stroke-width=\"2\"/>"
    "<path d=\"M8 12h16M8 16h12M8 20h16\" stroke=\"#0f6cbd\" "
    "stroke-width=\"2\"/></svg>";

static bool raster_start_job(pappl_job_t *job, pappl_pr_options_t *options,
                             pappl_device_t *device);
static bool raster_start_page(pappl_job_t *job, pappl_pr_options_t *options,
                              pappl_device_t *device, unsigned page);
static bool raster_write_line(pappl_job_t *job, pappl_pr_options_t *options,
                              pappl_device_t *device, unsigned y,
                              const unsigned char *line);
static bool raster_end_page(pappl_job_t *job, pappl_pr_options_t *options,
                            pappl_device_t *device, unsigned page);
static bool raster_end_job(pappl_job_t *job, pappl_pr_options_t *options,
                           pappl_device_t *device);

static void device_error_cb(const char *message, void *data)
{
    (void)data;
    fprintf(stderr, "brother-td-app: device error: %s\n", message);
}

static int mkdir_p(const char *path, mode_t mode)
{
    char *copy;
    char *p;
    struct stat st;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return 0;
        errno = ENOTDIR;
        return -1;
    }

    copy = strdup(path);
    if (copy == NULL)
        return -1;

    for (p = copy + 1; *p != '\0'; p++) {
        if (*p != '/')
            continue;

        *p = '\0';
        if (mkdir(copy, mode) < 0 && errno != EEXIST) {
            int saved = errno;

            free(copy);
            errno = saved;
            return -1;
        }
        *p = '/';
    }

    if (mkdir(copy, mode) < 0 && errno != EEXIST) {
        int saved = errno;

        free(copy);
        errno = saved;
        return -1;
    }

    free(copy);
    return 0;
}

static int read_saved_port(const char *spool_dir)
{
    char path[1024];
    FILE *fp;
    int port = 0;

    snprintf(path, sizeof(path), "%s/port", spool_dir);
    if ((fp = fopen(path, "r")) == NULL)
        return 0;
    if (fscanf(fp, "%d", &port) != 1)
        port = 0;
    fclose(fp);

    return (port >= 1 && port <= 65535) ? port : 0;
}

static void save_port(const char *spool_dir, int port)
{
    char path[1024];
    char temp[1200];
    char data[32];
    int fd;
    int len;

    snprintf(path, sizeof(path), "%s/port", spool_dir);
    snprintf(temp, sizeof(temp), "%s.tmp.%ld", path, (long)getpid());

    if ((fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
        fprintf(stderr, "brother-td-app: cannot save port: %s\n", strerror(errno));
        return;
    }

    len = snprintf(data, sizeof(data), "%d\n", port);
    if (write(fd, data, (size_t)len) != len) {
        fprintf(stderr, "brother-td-app: cannot save port: %s\n", strerror(errno));
        close(fd);
        unlink(temp);
        return;
    }
    close(fd);

    if (rename(temp, path) < 0) {
        fprintf(stderr, "brother-td-app: cannot save port: %s\n", strerror(errno));
        unlink(temp);
    }
}

static bool port_is_free(int port)
{
    struct sockaddr_in addr;
    int fd;
    int one = 1;
    bool free_port;

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return false;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((in_port_t)port);

    free_port = bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    close(fd);

    return free_port;
}

static char *default_spool_dir(void)
{
    const char *xdg_state = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    const char *tmpdir = getenv("TMPDIR");
    char *path = malloc(1024);

    if (path == NULL)
        return NULL;

    if (geteuid() == 0) {
        snprintf(path, 1024, "/var/spool/brother-td-app");
    } else if (xdg_state != NULL && xdg_state[0] != '\0') {
        snprintf(path, 1024, "%s/brother-td-app", xdg_state);
    } else if (home != NULL && home[0] != '\0') {
        snprintf(path, 1024, "%s/.local/state/brother-td-app", home);
    } else {
        snprintf(path, 1024, "%s/brother-td-app-%lu",
                 tmpdir != NULL && tmpdir[0] != '\0' ? tmpdir : "/tmp",
                 (unsigned long)geteuid());
    }

    return path;
}

typedef struct {
    int port;
    int count;
} printer_list_t;

static void print_printer_cb(pappl_printer_t *printer, void *data)
{
    printer_list_t *list = data;
    char uriname[256];

    /* papplPrinterGetPath(..., NULL, ...) returns the per-printer URI name
     * (without a leading slash).  The IPP resource for a multi-queue system
     * is /ipp/print/<uriname>.  Do not use subpath "ipp" here: that is the
     * web UI path (/<name>/ipp), not the IPP endpoint. */
    papplPrinterGetPath(printer, NULL, uriname, sizeof(uriname));
    fprintf(stderr, "  printer: %s  ipp://localhost:%d/ipp/print/%s\n",
            papplPrinterGetName(printer), list->port,
            uriname[0] == '/' ? uriname + 1 : uriname);
    list->count++;
}

static const char *find_style_file(void)
{
    const char *override = getenv("BROTHER_TD_APP_CSS");
    static const char * const candidates[] = {
        "pappl/brother-td.css",
        "/usr/share/brother-td-app/style.css",
        "/usr/local/share/brother-td-app/style.css",
        NULL
    };
    int i;

    if (override != NULL && override[0] != '\0' && access(override, R_OK) == 0)
        return override;

    for (i = 0; candidates[i] != NULL; i++) {
        if (access(candidates[i], R_OK) == 0)
            return candidates[i];
    }

    return NULL;
}

static char *load_style_file(const char *path, size_t *size)
{
    FILE *fp;
    long length;
    char *data;

    if (path == NULL || size == NULL)
        return NULL;

    if ((fp = fopen(path, "rb")) == NULL)
        return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    length = ftell(fp);
    if (length < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    if ((data = malloc((size_t)length + 1)) == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(data, 1, (size_t)length, fp) != (size_t)length) {
        free(data);
        fclose(fp);
        return NULL;
    }

    data[length] = '\0';
    *size = (size_t)length;
    fclose(fp);
    return data;
}

static void usage(FILE *stream)
{
    fprintf(stream,
            "brother-td-app %s\n"
            "PAPPL/IPP Everywhere printer application for Brother TD-4D printers.\n\n"
            "Usage: brother-td-app [options]\n"
            "  --device URI       Create one printer for this device URI (for example\n"
            "                     file:/tmp/capture, socket://host:9100)\n"
            "  --model MODEL      Model for --device (for example TD-4410D)\n"
            "  --printer NAME     Printer name for --device (default: model name)\n"
            "  --port PORT        IPP port, or 0/auto to choose a free port\n"
            "                     (default: auto, persisted in SPOOL/port)\n"
            "  --spool DIR        Spool directory\n"
            "  --log FILE         Log file (default: stderr)\n"
            "  --state FILE       State file (default: SPOOL/state)\n"
            "  --no-state         Do not load or save printer state\n"
            "  --debug            Enable PAPPL debug logging\n"
            "  --check-device     Open the --device URI, report success, and exit\n"
            "  --no-tls           Do not advertise or require IPPS/TLS\n"
            "  --help             Show this help\n\n"
            "Without --device, the application discovers USB/network printers and\n"
            "also allows adding printers through its web interface.\n",
            TD_VERSION);
}

static const char *driver_name_for_model(const td_model_t *model, char *buffer,
                                         size_t bufsize)
{
    size_t out = 0;
    size_t i;
    const char *id = model->id;

    if (bufsize < 4)
        return NULL;

    buffer[out++] = 't';
    buffer[out++] = 'd';
    buffer[out++] = '-';

    for (i = 0; id[i] != '\0' && out + 1 < bufsize; i++) {
        unsigned char ch = (unsigned char)id[i];

        if (ch == '-' || ch == ' ')
            continue;
        buffer[out++] = (char)(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    }
    buffer[out] = '\0';
    return out > 3 ? buffer : NULL;
}

static const td_model_t *model_for_driver(const char *driver_name)
{
    char name[64];
    size_t i;

    if (driver_name == NULL)
        return NULL;

    for (i = 0; i < td_num_models; i++) {
        if (driver_name_for_model(&td_models[i], name, sizeof(name)) != NULL &&
            strcmp(driver_name, name) == 0)
            return &td_models[i];
    }
    return NULL;
}

static int media_margin_100mm(const td_model_t *model, int width_mm)
{
    int printable_px = td_printable_width_px(model->dpi, (double)width_mm);
    double printable_mm = (double)printable_px * 25.4 / (double)model->dpi;
    double margin_mm = ((double)width_mm - printable_mm) / 2.0;

    if (margin_mm < 0.0)
        margin_mm = 0.0;
    return (int)lround(margin_mm * 100.0);
}

static pwg_media_t *media_for_size(int width_mm, int length_mm)
{
    return pwgMediaForSize(width_mm * 100, length_mm * 100);
}

static pwg_media_t *media_for_index(int index)
{
    if (media_pwg_names[index] != NULL)
        return pwgMediaForPWG(media_pwg_names[index]);
    return media_for_size(media_sizes[index].width_mm,
                          media_sizes[index].length_mm);
}

/* pwgMediaForSize() returns a pointer to a single static buffer for custom
 * sizes. Copy each name into stable storage so the IPP media list does not
 * end up with every custom size pointing at the last one. */
static char media_names[NUM_MEDIA_SIZES][64];
static char source_names[NUM_MEDIA_SIZES][32];

static void init_media_names(void)
{
    int i;

    for (i = 0; i < NUM_MEDIA_SIZES; i++) {
        pwg_media_t *pwg = media_for_index(i);

        if (pwg != NULL)
            copy_string(media_names[i], pwg->pwg, sizeof(media_names[i]));
        else
            media_names[i][0] = '\0';

        if (i == 0)
            copy_string(source_names[i], "main-roll",
                            sizeof(source_names[i]));
        else
            snprintf(source_names[i], sizeof(source_names[i]),
                     "roll-%dx%dmm", media_sizes[i].width_mm,
                     media_sizes[i].length_mm);
    }
}

static void fill_media(const td_model_t *model, pappl_media_col_t *media,
                       pwg_media_t *pwg)
{
    int margin = media_margin_100mm(model, pwg->width / 100);

    memset(media, 0, sizeof(*media));
    copy_string(media->size_name, pwg->pwg, sizeof(media->size_name));
    copy_string(media->source, "main-roll", sizeof(media->source));
    copy_string(media->type, "labels", sizeof(media->type));
    media->size_width = pwg->width;
    media->size_length = pwg->length;
    media->left_margin = margin;
    media->right_margin = margin;
    media->bottom_margin = 300;
    media->top_margin = 300;
    media->tracking = PAPPL_MEDIA_TRACKING_MARK;
}

static bool avahi_available(void)
{
    static const char * const paths[] = {
        "/run/avahi-daemon/socket",
        "/var/run/avahi-daemon/socket",
        NULL
    };
    int i;

    for (i = 0; paths[i] != NULL; i++) {
        struct sockaddr_un addr;
        int fd;

        if (strlen(paths[i]) >= sizeof(addr.sun_path))
            continue;
        if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
            continue;

        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        copy_string(addr.sun_path, paths[i], sizeof(addr.sun_path));

        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            close(fd);
            return true;
        }
        close(fd);
    }

    return false;
}

static bool add_printer_fallback_cb(pappl_client_t *client, void *data)
{
    (void)data;

    if (!papplClientRespond(client, HTTP_STATUS_OK, NULL, "text/html", 0, 0))
        return false;

    papplClientHTMLHeader(client, "Add Printer", 0);
    papplClientHTMLPuts(client,
        "<div class=\"content\">\n"
        "<h1 class=\"title\">Add Printer</h1>\n"
        "<div class=\"banner\"><b>Automatic printer discovery is unavailable.</b> "
        "The <code>avahi-daemon</code> service is not running.</div>\n"
        "<p>Start <code>avahi-daemon</code> and restart "
        "<code>brother-td-app</code>, or start the application with "
        "<code>--device</code> and <code>--printer</code>.</p>\n"
        "</div>\n");
    papplClientHTMLFooter(client);
    return true;
}

static bool text_resource_cb(pappl_client_t *client, void *data)
{
    const td_text_resource_t *resource = data;
    http_t *http;

    if (!papplClientRespond(client, HTTP_STATUS_OK, NULL, resource->type, 0, 0))
        return false;

    http = papplClientGetHTTP(client);
    httpWrite2(http, resource->text, strlen(resource->text));
    httpWrite2(http, "", 0);
    return true;
}

static bool td_status(pappl_printer_t *printer)
{
    (void)printer;
    return true;
}

static void add_vendor_attrs(ipp_t *attrs)
{
    static const char * const tf[] = {"false", "true"};
    static const char * const media_types[] = {"diecut", "continuous"};
    static const char * const dithers[] = {
        "floyd", "jarvis", "stucki", "atkinson", "sierra", "sierralite",
        "burkes", "bayer4", "bayer8", "threshold"
    };
    static const char * const compressions[] = {"none", "tiff"};
    static const char * const scalings[] = {"fit", "fitenlarge", "crop", "none"};
    static const char * const rotations[] = {"0", "90", "180", "270"};
    static const char * const alignments[] = {"left", "center", "right"};

    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-media-type-default", NULL, "diecut");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-media-type-supported", 2, NULL, media_types);

    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-dither-default", NULL, "floyd");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-dither-supported", 10, NULL, dithers);

    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-compression-default", NULL, "tiff");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-compression-supported", 2, NULL, compressions);

    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-scaling-default", NULL, "fit");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-scaling-supported", 4, NULL, scalings);

    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-rotate-default", NULL, "0");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-rotate-supported", 4, NULL, rotations);

    ippAddInteger(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                  "td-margin-default", 3);
    ippAddRange(attrs, IPP_TAG_PRINTER, "td-margin-supported", 3, 127);

    ippAddInteger(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                  "td-threshold-default", 128);
    ippAddRange(attrs, IPP_TAG_PRINTER, "td-threshold-supported", 0, 255);

    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-align-x-default", NULL, "center");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-align-x-supported", 3, NULL, alignments);
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-align-y-default", NULL, "center");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-align-y-supported", 3, NULL, alignments);

    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-invert-default", NULL, "false");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-invert-supported", 2, NULL, tf);
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-mirror-default", NULL, "false");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-mirror-supported", 2, NULL, tf);
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-border-default", NULL, "false");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-border-supported", 2, NULL, tf);

    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-auto-trim-default", NULL, "true");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-auto-trim-supported", 2, NULL, tf);
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-auto-cut-default", NULL, "false");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-auto-cut-supported", 2, NULL, tf);
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-cut-at-end-default", NULL, "true");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-cut-at-end-supported", 2, NULL, tf);
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "td-peeler-default", NULL, "false");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "td-peeler-supported", 2, NULL, tf);
}

static bool driver_cb(pappl_system_t *system, const char *driver_name,
                      const char *device_uri, const char *device_id,
                      pappl_pr_driver_data_t *driver_data, ipp_t **driver_attrs,
                      void *data)
{
    const td_model_t *model;
    int i;
    int num_media = 0;

    (void)device_uri;
    (void)device_id;
    (void)data;

    if (driver_name == NULL || driver_data == NULL || driver_attrs == NULL)
        return false;

    model = model_for_driver(driver_name);
    if (model == NULL) {
        papplLog(system, PAPPL_LOGLEVEL_ERROR, "Unknown driver '%s'", driver_name);
        return false;
    }

    driver_data->printfile_cb = NULL;
    driver_data->rendjob_cb = raster_end_job;
    driver_data->rendpage_cb = raster_end_page;
    driver_data->rstartjob_cb = raster_start_job;
    driver_data->rstartpage_cb = raster_start_page;
    driver_data->rwriteline_cb = raster_write_line;
    driver_data->status_cb = td_status;

    snprintf(driver_data->make_and_model, sizeof(driver_data->make_and_model),
             "Brother %s label printer", model->id);
    driver_data->format = "image/pwg-raster";
    driver_data->kind = PAPPL_KIND_LABEL | PAPPL_KIND_ROLL;
    driver_data->ppm = 6;
    driver_data->color_supported = PAPPL_COLOR_MODE_MONOCHROME;
    driver_data->color_default = PAPPL_COLOR_MODE_MONOCHROME;
    driver_data->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 |
                                PAPPL_PWG_RASTER_TYPE_SGRAY_8 |
                                PAPPL_PWG_RASTER_TYPE_SRGB_8;
    driver_data->force_raster_type = PAPPL_PWG_RASTER_TYPE_SGRAY_8;
    driver_data->orient_default = IPP_ORIENT_NONE;
    driver_data->quality_default = IPP_QUALITY_NORMAL;
    driver_data->scaling_default = PAPPL_SCALING_FIT;
    driver_data->sides_supported = PAPPL_SIDES_ONE_SIDED;
    driver_data->sides_default = PAPPL_SIDES_ONE_SIDED;

    driver_data->num_resolution = 1;
    driver_data->x_resolution[0] = model->dpi;
    driver_data->y_resolution[0] = model->dpi;
    driver_data->x_default = model->dpi;
    driver_data->y_default = model->dpi;

    driver_data->num_source = 0;
    driver_data->num_type = 2;
    driver_data->type[0] = "labels";
    driver_data->type[1] = "continuous";
    driver_data->num_bin = 0;

    driver_data->num_media = 0;
    for (i = 0; i < NUM_MEDIA_SIZES; i++) {
        pwg_media_t *pwg;

        if (media_sizes[i].width_mm > model->max_width_mm)
            continue;
        pwg = media_for_index(i);
        if (pwg == NULL || media_names[i][0] == '\0')
            continue;
        driver_data->media[driver_data->num_media++] = media_names[i];
        num_media++;
    }
    if (num_media == 0) {
        papplLog(system, PAPPL_LOGLEVEL_ERROR, "No media sizes for %s", model->id);
        return false;
    }

    for (i = 0; i < NUM_READY_MEDIA; i++) {
        int index = ready_media_indices[i];
        pwg_media_t *pwg;
        pappl_media_col_t *ready;

        if (index < 0 || index >= NUM_MEDIA_SIZES)
            continue;
        if (media_sizes[index].width_mm > model->max_width_mm)
            continue;
        if (media_names[index][0] == '\0')
            continue;
        pwg = media_for_index(index);
        if (pwg == NULL)
            continue;

        ready = &driver_data->media_ready[driver_data->num_source];
        fill_media(model, ready, pwg);
        copy_string(ready->source, source_names[index],
                        sizeof(ready->source));
        driver_data->source[driver_data->num_source] = source_names[index];
        driver_data->num_source++;
    }

    if (driver_data->num_source == 0) {
        pwg_media_t *pwg = media_for_index(0);
        if (pwg == NULL) {
            papplLog(system, PAPPL_LOGLEVEL_ERROR,
                     "No ready media for %s", model->id);
            return false;
        }
        fill_media(model, &driver_data->media_ready[0], pwg);
        copy_string(driver_data->media_ready[0].source, "main-roll",
                        sizeof(driver_data->media_ready[0].source));
        driver_data->source[0] = "main-roll";
        driver_data->num_source = 1;
    }

    driver_data->media_default = driver_data->media_ready[0];
    driver_data->left_right = driver_data->media_ready[0].left_margin;
    driver_data->bottom_top = driver_data->media_ready[0].bottom_margin;
    for (i = 0; i < driver_data->num_source; i++) {
        if (driver_data->media_ready[i].left_margin < driver_data->left_right)
            driver_data->left_right = driver_data->media_ready[i].left_margin;
        if (driver_data->media_ready[i].right_margin < driver_data->left_right)
            driver_data->left_right = driver_data->media_ready[i].right_margin;
        if (driver_data->media_ready[i].top_margin < driver_data->bottom_top)
            driver_data->bottom_top = driver_data->media_ready[i].top_margin;
        if (driver_data->media_ready[i].bottom_margin < driver_data->bottom_top)
            driver_data->bottom_top = driver_data->media_ready[i].bottom_margin;
    }
    driver_data->tracking_supported = PAPPL_MEDIA_TRACKING_MARK |
                                      PAPPL_MEDIA_TRACKING_CONTINUOUS;

    driver_data->mode_supported = PAPPL_LABEL_MODE_TEAR_OFF |
                                  PAPPL_LABEL_MODE_CUTTER |
                                  PAPPL_LABEL_MODE_PEEL_OFF;
    driver_data->mode_configured = PAPPL_LABEL_MODE_TEAR_OFF;

    driver_data->num_vendor = NUM_VENDOR;
    for (i = 0; i < NUM_VENDOR; i++)
        driver_data->vendor[i] = vendor_names[i];

    *driver_attrs = ippNew();
    add_vendor_attrs(*driver_attrs);

    {
        char host[256];
        char uri[512];
        int host_port;

        system_host_name(system, host, sizeof(host));
        host_port = system_port(system);
        if (host[0] == '\0')
            copy_string(host, "localhost", sizeof(host));
        snprintf(uri, sizeof(uri), "http://%s:%d/strings-en", host, host_port);
        ippAddString(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_URI,
                     "printer-strings-uri", NULL, uri);
        ippAddString(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_LANGUAGE,
                     "printer-strings-languages-supported", NULL, "en");
    }

    return true;
}

static const char *autoadd_cb(const char *device_info, const char *device_uri,
                              const char *device_id, void *data)
{
    static char driver_name[64];
    const char *mdl;
    char model_id[64];
    size_t i = 0;
    const td_model_t *model;

    (void)device_info;
    (void)device_uri;
    (void)data;

    if (device_id == NULL || (mdl = strstr(device_id, "MDL:")) == NULL)
        return NULL;

    mdl += 4;
    while (mdl[i] != '\0' && mdl[i] != ';' && i + 1 < sizeof(model_id)) {
        model_id[i] = mdl[i];
        i++;
    }
    model_id[i] = '\0';

    model = td_model_find(model_id);
    if (model == NULL)
        return NULL;

    return driver_name_for_model(model, driver_name, sizeof(driver_name));
}

static const char *vendor_value(pappl_pr_options_t *options, const char *name)
{
    if (options == NULL || options->num_vendor <= 0)
        return NULL;
    return cupsGetOption(name, options->num_vendor, options->vendor);
}

static bool vendor_bool(pappl_pr_options_t *options, const char *name, bool fallback)
{
    const char *value = vendor_value(options, name);

    return td_parse_bool(value, fallback);
}

static void options_from_pappl(td_options_t *td, pappl_pr_options_t *options,
                               const td_model_t *model, int job_id)
{
    const char *value;

    td_options_init(td);
    snprintf(td->model, sizeof(td->model), "%s", model->id);
    td->job_id = job_id;
    td->copies = options->copies > 0 ? options->copies : 1;

    value = vendor_value(options, "td-media-type");
    if (value != NULL)
        td->media_type = td_media_type_parse(value, td->media_type);
    if (options->media.type[0] != '\0' &&
        strstr(options->media.type, "continuous") != NULL)
        td->media_type = TD_MEDIA_CONTINUOUS;

    value = vendor_value(options, "td-dither");
    if (value != NULL)
        td->dither = td_dither_parse(value, td->dither);

    value = vendor_value(options, "td-compression");
    if (value != NULL && strcasecmp(value, "none") == 0)
        td->compression = TD_COMPRESSION_NONE;
    else if (value != NULL)
        td->compression = TD_COMPRESSION_TIFF;

    value = vendor_value(options, "td-scaling");
    if (value != NULL && strcasecmp(value, "none") == 0)
        td->scaling = TD_SCALE_NONE;
    else if (value != NULL && strcasecmp(value, "crop") == 0)
        td->scaling = TD_SCALE_CROP;
    else if (value != NULL && strcasecmp(value, "fitenlarge") == 0)
        td->scaling = TD_SCALE_FIT_ENLARGE;
    else if (value != NULL)
        td->scaling = TD_SCALE_FIT;
    else if (options->print_scaling == PAPPL_SCALING_NONE)
        td->scaling = TD_SCALE_NONE;
    else if (options->print_scaling == PAPPL_SCALING_FILL)
        td->scaling = TD_SCALE_CROP;

    value = vendor_value(options, "td-rotate");
    if (value != NULL)
        td->rotate = td_parse_int(value, 0, 0, 359);

    value = vendor_value(options, "td-margin");
    if (value != NULL)
        td->margin_mm = td_parse_double(value, td->margin_mm, 0.0, 127.0);

    value = vendor_value(options, "td-threshold");
    if (value != NULL)
        td->threshold = td_parse_int(value, 128, 0, 255);

    value = vendor_value(options, "td-align-x");
    if (value != NULL && strcasecmp(value, "left") == 0)
        td->align_x = TD_ALIGN_START;
    else if (value != NULL && strcasecmp(value, "right") == 0)
        td->align_x = TD_ALIGN_END;

    value = vendor_value(options, "td-align-y");
    if (value != NULL && strcasecmp(value, "top") == 0)
        td->align_y = TD_ALIGN_START;
    else if (value != NULL && strcasecmp(value, "bottom") == 0)
        td->align_y = TD_ALIGN_END;

    td->invert = vendor_bool(options, "td-invert", td->invert);
    td->mirror = vendor_bool(options, "td-mirror", td->mirror);
    td->border = vendor_bool(options, "td-border", td->border);
    td->auto_trim = vendor_bool(options, "td-auto-trim", td->auto_trim);
    td->auto_cut = vendor_bool(options, "td-auto-cut", td->auto_cut);
    td->cut_at_end = vendor_bool(options, "td-cut-at-end", td->cut_at_end);
    td->peeler = vendor_bool(options, "td-peeler", td->peeler);

    if (td->media_type == TD_MEDIA_DIECUT) {
        td->auto_cut = false;
        td->peeler = false;
        td->auto_trim = false;
        td->margin_mm = 0.0;
    } else if (td->margin_mm < 3.0) {
        td->margin_mm = 3.0;
    }
}

static ssize_t device_write_cb(void *context, const void *data, size_t length)
{
    return papplDeviceWrite((pappl_device_t *)context, data, length);
}

static int copy_file(const char *source, const char *destination)
{
    int input = open(source, O_RDONLY);
    int output;
    unsigned char buffer[65536];
    ssize_t bytes;

    if (input < 0)
        return -1;

    output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (output < 0) {
        close(input);
        return -1;
    }

    while ((bytes = read(input, buffer, sizeof(buffer))) > 0) {
        ssize_t offset = 0;

        while (offset < bytes) {
            ssize_t written = write(output, buffer + offset,
                                    (size_t)(bytes - offset));

            if (written < 0) {
                if (errno == EINTR)
                    continue;
                close(input);
                close(output);
                return -1;
            }
            offset += written;
        }
    }

    close(input);
    close(output);
    return bytes < 0 ? -1 : 0;
}

static int run_ghostscript(pappl_job_t *job, const td_model_t *model,
                           pappl_pr_options_t *options,
                           const char *input, const char *output)
{
    char resolution[32];
    char width[32];
    char height[32];
    char output_arg[512];
    char *argv[32];
    int argc = 0;
    int dpi = options->printer_resolution[0] > 0 ?
              options->printer_resolution[0] : model->dpi;
    double width_pt = options->media.size_width * 72.0 / 2540.0;
    double height_pt = options->media.size_length * 72.0 / 2540.0;
    pid_t pid;
    int status;
    time_t deadline;

    if (width_pt <= 0.0)
        width_pt = 288.0;
    if (height_pt <= 0.0)
        height_pt = 432.0;

    snprintf(resolution, sizeof(resolution), "-r%dx%d", dpi, dpi);
    snprintf(width, sizeof(width), "-dDEVICEWIDTHPOINTS=%d", (int)lround(width_pt));
    snprintf(height, sizeof(height), "-dDEVICEHEIGHTPOINTS=%d", (int)lround(height_pt));
    snprintf(output_arg, sizeof(output_arg), "-sOutputFile=%s", output);

    argv[argc++] = "gs";
    argv[argc++] = "-dQUIET";
    argv[argc++] = "-dSAFER";
    argv[argc++] = "-dNOPAUSE";
    argv[argc++] = "-dBATCH";
    argv[argc++] = "-dNOINTERPOLATE";
    argv[argc++] = "-dFIXEDMEDIA";
    argv[argc++] = "-dPDFFitPage";
    argv[argc++] = "-sDEVICE=cups";
    argv[argc++] = resolution;
    argv[argc++] = width;
    argv[argc++] = height;
    argv[argc++] = "-dcupsBitsPerColor=8";
    argv[argc++] = "-dcupsColorOrder=0";
    argv[argc++] = "-dcupsColorSpace=3";
    argv[argc++] = output_arg;
    argv[argc++] = (char *)input;
    argv[argc] = NULL;

    papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                "running Ghostscript: %dx%d dpi, %.1fx%.1f mm, output '%s'",
                dpi, dpi,
                (double)options->media.size_width / 100.0,
                (double)options->media.size_length / 100.0, output);

    pid = fork();
    if (pid < 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "cannot fork Ghostscript: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        int nullfd = open("/dev/null", O_WRONLY);

        if (nullfd >= 0) {
            dup2(nullfd, STDOUT_FILENO);
            close(nullfd);
        }
        execvp("gs", argv);
        _exit(127);
    }

    deadline = time(NULL) + 120;
    for (;;) {
        pid_t waited = waitpid(pid, &status, WNOHANG);

        if (waited == pid)
            break;
        if (waited < 0) {
            if (errno == EINTR)
                continue;
            papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                        "cannot wait for Ghostscript: %s", strerror(errno));
            return -1;
        }
        if (time(NULL) >= deadline) {
            papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                        "Ghostscript timed out after 120 seconds");
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                ;
            return -1;
        }
        sleep(1);
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Ghostscript failed for '%s' (status %d)", input, status);
        return -1;
    }
    papplLogJob(job, PAPPL_LOGLEVEL_INFO, "Ghostscript completed");
    return 0;
}

static bool pdf_filter(pappl_job_t *job, pappl_device_t *device, void *data)
{
    pappl_pr_options_t *options = NULL;
    const td_model_t *model;
    td_options_t td;
    td_writer_t writer;
    td_job_t tdjob;
    char tempname[] = "/tmp/brother-td-pappl-XXXXXX";
    char inputname[] = "/tmp/brother-td-input-XXXXXX";
    int fd = -1;
    int input_fd = -1;
    int raster_fd = -1;
    cups_raster_t *raster = NULL;
    bool result = false;

    (void)data;

    model = model_for_driver(papplPrinterGetDriverName(papplJobGetPrinter(job)));
    if (model == NULL) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Cannot determine printer model");
        return false;
    }

    options = papplJobCreatePrintOptions(job, 0, false);
    if (options == NULL) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Cannot create print options");
        return false;
    }

    papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                "PDF/PostScript job: format=%s, device=%s, copies=%d",
                papplJobGetFormat(job) ? papplJobGetFormat(job) : "(unknown)",
                papplPrinterGetDeviceURI(papplJobGetPrinter(job)) ?
                papplPrinterGetDeviceURI(papplJobGetPrinter(job)) : "(none)",
                options->copies);

    options_from_pappl(&td, options, model, papplJobGetID(job));
    td_writer_init_cb(&writer, device_write_cb, device);
    if (td_job_init(&tdjob, &td, model, &writer) < 0)
        goto out;

    fd = mkstemp(tempname);
    if (fd < 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Cannot create temporary raster file: %s",
                    strerror(errno));
        goto out;
    }
    close(fd);
    fd = -1;

    input_fd = mkstemp(inputname);
    if (input_fd < 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "cannot create temporary input file: %s", strerror(errno));
        goto out;
    }
    close(input_fd);
    input_fd = -1;

    if (copy_file(papplJobGetFilename(job), inputname) < 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "cannot copy '%s' to temporary file: %s",
                    papplJobGetFilename(job), strerror(errno));
        goto out;
    }

    if (run_ghostscript(job, model, options, inputname, tempname) < 0)
        goto out;

    raster_fd = open(tempname, O_RDONLY);
    if (raster_fd < 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Cannot open raster file '%s': %s",
                    tempname, strerror(errno));
        goto out;
    }

    raster = cupsRasterOpen(raster_fd, CUPS_RASTER_READ);
    if (raster == NULL) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Cannot parse Ghostscript raster output");
        goto out;
    }

    papplLogJob(job, PAPPL_LOGLEVEL_INFO, "sending raster data to device");
    if (td_job_process_cups_raster(&tdjob, raster, NULL) < 0)
        goto out;
    if (td_job_finish(&tdjob) < 0)
        goto out;

    {
        int pages = (int)tdjob.protocol.pages_written;

        if (pages < 1)
            pages = 1;
        papplJobSetImpressions(job, pages);
        papplJobSetImpressionsCompleted(job, pages);
    }
    result = true;

out:
    if (raster != NULL)
        cupsRasterClose(raster);
    if (raster_fd >= 0)
        close(raster_fd);
    if (fd >= 0)
        close(fd);
    if (input_fd >= 0)
        close(input_fd);
    unlink(tempname);
    unlink(inputname);
    if (options != NULL)
        papplJobDeletePrintOptions(options);
    return result;
}

static bool raster_start_job(pappl_job_t *job, pappl_pr_options_t *options,
                             pappl_device_t *device)
{
    td_raster_job_t *raster_job;
    const td_model_t *model;

    model = model_for_driver(papplPrinterGetDriverName(papplJobGetPrinter(job)));
    if (model == NULL)
        return false;

    raster_job = calloc(1, sizeof(*raster_job));
    if (raster_job == NULL)
        return false;

    papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                "raster job: format=%s, device=%s, copies=%d",
                papplJobGetFormat(job) ? papplJobGetFormat(job) : "(unknown)",
                papplPrinterGetDeviceURI(papplJobGetPrinter(job)) ?
                papplPrinterGetDeviceURI(papplJobGetPrinter(job)) : "(none)",
                options->copies);

    options_from_pappl(&raster_job->options, options, model, papplJobGetID(job));
    td_writer_init_cb(&raster_job->writer, device_write_cb, device);
    if (td_job_init(&raster_job->job, &raster_job->options, model,
                    &raster_job->writer) < 0) {
        free(raster_job);
        return false;
    }
    if (td_job_begin(&raster_job->job) < 0) {
        free(raster_job);
        return false;
    }

    papplJobSetData(job, raster_job);
    return true;
}

static bool raster_start_page(pappl_job_t *job, pappl_pr_options_t *options,
                              pappl_device_t *device, unsigned page)
{
    td_raster_job_t *raster_job = papplJobGetData(job);

    (void)device;
    (void)page;

    if (raster_job == NULL)
        return false;
    if (td_job_set_page(&raster_job->job, &options->header) < 0)
        return false;

    td_gray_free(&raster_job->image);
    raster_job->image = td_gray_alloc((int)options->header.cupsWidth,
                                      (int)options->header.cupsHeight, 255);
    return raster_job->image.pixels != NULL;
}

static bool raster_write_line(pappl_job_t *job, pappl_pr_options_t *options,
                              pappl_device_t *device, unsigned y,
                              const unsigned char *line)
{
    td_raster_job_t *raster_job = papplJobGetData(job);

    (void)device;

    if (raster_job == NULL)
        return false;
    return td_raster_convert_row(&options->header, line, (int)y,
                                 &raster_job->image) == 0;
}

static bool raster_end_page(pappl_job_t *job, pappl_pr_options_t *options,
                            pappl_device_t *device, unsigned page)
{
    td_raster_job_t *raster_job = papplJobGetData(job);
    bool result;

    (void)options;
    (void)device;
    (void)page;

    if (raster_job == NULL)
        return false;

    result = td_job_emit_gray(&raster_job->job, &raster_job->image, false) == 0;
    td_gray_free(&raster_job->image);
    return result;
}

static bool raster_end_job(pappl_job_t *job, pappl_pr_options_t *options,
                           pappl_device_t *device)
{
    td_raster_job_t *raster_job = papplJobGetData(job);
    bool result = true;

    (void)options;
    (void)device;

    if (raster_job == NULL)
        return false;

    if (td_job_finish(&raster_job->job) < 0)
        result = false;
    td_gray_free(&raster_job->image);
    free(raster_job);
    papplJobSetData(job, NULL);
    return result;
}

int main(int argc, char **argv)
{
    pappl_system_t *system;
    pappl_pr_driver_t *drivers;
    const char *device_uri = NULL;
    const char *model_name = NULL;
    const char *printer_name = NULL;
    const char *spool_dir = NULL;
    char *spool_alloc = NULL;
    const char *log_file = "-";
    const char *state_file = NULL;
    char state_buffer[1024];
    bool state_loaded;
    bool no_state = false;
    bool check_device = false;
    bool no_tls = false;
    pappl_loglevel_t loglevel = PAPPL_LOGLEVEL_INFO;
    int port = 0;
    bool auto_port = true;
    char *style_data = NULL;
    size_t style_len = 0;
    td_text_resource_t style_resource;
    td_text_resource_t strings_resource;
    td_text_resource_t icon_resource;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            device_uri = argv[++i];
        } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_name = argv[++i];
        } else if (strcmp(argv[i], "--printer") == 0 && i + 1 < argc) {
            printer_name = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            const char *value = argv[++i];

            if (strcasecmp(value, "auto") == 0) {
                port = 0;
                auto_port = true;
            } else {
                port = td_parse_int(value, 0, 0, 65535);
                auto_port = port == 0;
            }
        } else if (strcmp(argv[i], "--spool") == 0 && i + 1 < argc) {
            spool_dir = argv[++i];
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_file = argv[++i];
        } else if (strcmp(argv[i], "--state") == 0 && i + 1 < argc) {
            state_file = argv[++i];
        } else if (strcmp(argv[i], "--no-state") == 0) {
            no_state = true;
        } else if (strcmp(argv[i], "--debug") == 0) {
            loglevel = PAPPL_LOGLEVEL_DEBUG;
        } else if (strcmp(argv[i], "--check-device") == 0) {
            check_device = true;
        } else if (strcmp(argv[i], "--no-tls") == 0) {
            no_tls = true;
        } else {
            fprintf(stderr, "brother-td-app: unknown option '%s'\n", argv[i]);
            usage(stderr);
            return 1;
        }
    }

    if (spool_dir == NULL) {
        spool_alloc = default_spool_dir();
        if (spool_alloc == NULL) {
            fprintf(stderr, "brother-td-app: out of memory\n");
            return 1;
        }
        spool_dir = spool_alloc;
    }

    init_media_names();

    if (mkdir_p(spool_dir, 0700) < 0) {
        fprintf(stderr,
                "brother-td-app: cannot create spool directory '%s': %s\n"
                "Use --spool with a writable directory, for example:\n"
                "  brother-td-app --spool \"$HOME/.local/state/brother-td-app\" ...\n",
                spool_dir, strerror(errno));
        free(spool_alloc);
        return 1;
    }

    if (auto_port) {
        int saved = read_saved_port(spool_dir);

        if (saved > 0 && port_is_free(saved))
            port = saved;
        else
            port = 0;
    }

    if (check_device) {
        pappl_device_t *device;

        if (device_uri == NULL) {
            fprintf(stderr, "brother-td-app: --check-device requires --device\n");
            free(spool_alloc);
            return 1;
        }

        device = papplDeviceOpen(device_uri, printer_name != NULL ? printer_name : "Brother TD",
                                 device_error_cb, NULL);
        if (device == NULL) {
            fprintf(stderr, "brother-td-app: cannot open device '%s'\n", device_uri);
            free(spool_alloc);
            return 1;
        }
        papplDeviceClose(device);
        fprintf(stderr, "brother-td-app: device '%s' opened successfully\n", device_uri);
        free(spool_alloc);
        return 0;
    }

    drivers = calloc(td_num_models, sizeof(*drivers));
    if (drivers == NULL) {
        fprintf(stderr, "brother-td-app: out of memory\n");
        return 1;
    }

    for (i = 0; i < (int)td_num_models; i++) {
        char name[64];

        driver_name_for_model(&td_models[i], name, sizeof(name));
        drivers[i].name = strdup(name);
        drivers[i].description = td_models[i].id;
        drivers[i].device_id = NULL;
        drivers[i].extension = (void *)&td_models[i];
    }

    {
        pappl_soptions_t system_options = PAPPL_SOPTIONS_MULTI_QUEUE |
                                          PAPPL_SOPTIONS_WEB_INTERFACE |
                                          PAPPL_SOPTIONS_RAW_SOCKET |
                                          PAPPL_SOPTIONS_DNSSD_HOST;

        if (no_tls)
            system_options |= PAPPL_SOPTIONS_NO_TLS;

        /* The "_universal" DNS-SD subtype is what AirPrint uses to find
           printers; PAPPL registers it under both _ipp._tcp and _ipps._tcp. */
        system = papplSystemCreate(system_options, "Brother TD", port,
                                   "_universal", spool_dir, log_file, loglevel,
                                   NULL, false);
    }
    if (system == NULL) {
        fprintf(stderr, "brother-td-app: cannot create PAPPL system (spool '%s'): %s\n",
                spool_dir, strerror(errno));
        free(spool_alloc);
        return 1;
    }

    {
        const char *style = find_style_file();

        if (style != NULL)
            style_data = load_style_file(style, &style_len);
        if (style_data == NULL)
            papplLog(system, PAPPL_LOGLEVEL_WARN,
                     "Cannot read brother-td.css; using built-in fallback stylesheet");

        style_resource.type = "text/css";
        style_resource.text = style_data != NULL ? style_data : fallback_css;
        papplSystemAddResourceCallback(system, "/style.css", "text/css",
                                       text_resource_cb, &style_resource);
    }

    strings_resource.type = "text/plain";
    strings_resource.text = media_strings;
    papplSystemAddResourceCallback(system, "/strings-en", "text/plain",
                                   text_resource_cb, &strings_resource);
    icon_resource.type = "image/svg+xml";
    icon_resource.text = icon_svg;
    papplSystemRemoveResource(system, "/favicon.png");
    papplSystemRemoveResource(system, "/navicon.png");
    papplSystemAddResourceCallback(system, "/favicon.png", "image/svg+xml",
                                   text_resource_cb, &icon_resource);
    papplSystemAddResourceCallback(system, "/navicon.png", "image/svg+xml",
                                   text_resource_cb, &icon_resource);

    papplSystemAddLink(system, "Settings", "/config", PAPPL_LOPTIONS_NAVIGATION);
    papplSystemAddLink(system, "Add Printer", "/addprinter", PAPPL_LOPTIONS_NAVIGATION);
    papplSystemAddLink(system, "Printers", "/", PAPPL_LOPTIONS_NAVIGATION);

    if (!avahi_available()) {
        papplLog(system, PAPPL_LOGLEVEL_WARN,
                 "avahi-daemon is not running; automatic printer discovery on the Add Printer page is disabled");
        papplSystemAddResourceCallback(system, "/addprinter", "text/html",
                                       add_printer_fallback_cb, system);
    }

    papplSystemSetFooterHTML(system,
        "brother-td-linux &middot; <a href=\"https://github.com/scne59/brother-td-linux\">project page</a>");

    if (!papplSystemAddListeners(system, NULL)) {
        fprintf(stderr, "brother-td-app: cannot add listeners\n");
        papplSystemDelete(system);
        return 1;
    }

    /* PAPPL's explicit-port path only adds the IPv4 listener because of the
       short-circuit in papplSystemAddListeners().  Add IPv6 explicitly when
       reusing a saved or user-specified port. */
    if (port != 0)
        (void)papplSystemAddListeners(system, "[::]");

    port = system_port(system);
    if (port <= 0) {
        fprintf(stderr, "brother-td-app: cannot determine IPP port\n");
        papplSystemDelete(system);
        return 1;
    }
    if (auto_port)
        save_port(spool_dir, port);

    papplSystemSetPrinterDrivers(system, (int)td_num_models, drivers,
                                 autoadd_cb, NULL, driver_cb, NULL);

    papplSystemAddMIMEFilter(system, "application/pdf", "image/pwg-raster",
                             pdf_filter, NULL);
    papplSystemAddMIMEFilter(system, "application/postscript", "image/pwg-raster",
                             pdf_filter, NULL);
    papplSystemAddMIMEFilter(system, "application/vnd.cups-postscript",
                             "image/pwg-raster", pdf_filter, NULL);

    if (state_file == NULL) {
        snprintf(state_buffer, sizeof(state_buffer), "%s/state", spool_dir);
        state_file = state_buffer;
    }
    if (!no_state) {
        papplSystemSetSaveCallback(system, (pappl_save_cb_t)papplSystemSaveState,
                                   (void *)state_file);
        state_loaded = papplSystemLoadState(system, state_file);
    } else {
        state_loaded = false;
    }

    if (device_uri != NULL) {
        const td_model_t *model;
        char driver_name[64];
        char default_name[64];
        char device_id[128];

        if (model_name == NULL) {
            fprintf(stderr, "brother-td-app: --device requires --model\n");
            papplSystemDelete(system);
            return 1;
        }

        model = td_model_find(model_name);
        if (model == NULL) {
            fprintf(stderr, "brother-td-app: unknown model '%s'\n", model_name);
            papplSystemDelete(system);
            return 1;
        }

        driver_name_for_model(model, driver_name, sizeof(driver_name));
        snprintf(device_id, sizeof(device_id), "MFG:Brother;MDL:%s;", model->id);
        if (printer_name == NULL) {
            snprintf(default_name, sizeof(default_name), "%s", model->id);
            printer_name = default_name;
        }

        {
            char resource[256];
            pappl_printer_t *existing;

            snprintf(resource, sizeof(resource), "/ipp/print/%s", printer_name);
            existing = papplSystemFindPrinter(system, resource, 0, NULL);
            if (existing != NULL && papplPrinterGetDeviceURI(existing) != NULL &&
                strcmp(papplPrinterGetDeviceURI(existing), device_uri) != 0) {
                papplLog(system, PAPPL_LOGLEVEL_WARN,
                         "Replacing existing printer '%s' (device '%s') with device '%s'.",
                         printer_name, papplPrinterGetDeviceURI(existing), device_uri);
                papplPrinterDelete(existing);
                existing = NULL;
            }
            if (existing != NULL) {
                papplLog(system, PAPPL_LOGLEVEL_WARN,
                         "Printer '%s' already exists (device '%s'); using it.",
                         printer_name,
                         papplPrinterGetDeviceURI(existing) ?
                         papplPrinterGetDeviceURI(existing) : "(none)");
            } else if (papplPrinterCreate(system, 0, printer_name, driver_name,
                                          device_id, device_uri) == NULL) {
                fprintf(stderr,
                        "brother-td-app: cannot create printer '%s' for '%s': %s\n",
                        printer_name, device_uri, strerror(errno));
                papplSystemDelete(system);
                return 1;
            }
        }
    } else if (!state_loaded) {
#if HAVE_PAPPL_CREATE_PRINTERS
        papplSystemCreatePrinters(system,
                                  PAPPL_DEVTYPE_USB | PAPPL_DEVTYPE_SOCKET |
                                  PAPPL_DEVTYPE_DNS_SD,
                                  NULL, NULL);
#else
        if (papplSystemCreatePrinters != NULL)
            papplSystemCreatePrinters(system,
                                      PAPPL_DEVTYPE_USB | PAPPL_DEVTYPE_SOCKET |
                                      PAPPL_DEVTYPE_DNS_SD,
                                      NULL, NULL);
        else
            papplLog(system, PAPPL_LOGLEVEL_WARN,
                     "This PAPPL version cannot auto-discover printers; use --device or add one through the web interface.");
#endif
    }

    fprintf(stderr,
            "brother-td-app %s\n"
            "  web interface: http://localhost:%d/\n"
            "  spool:         %s\n",
            TD_VERSION, port, spool_dir);
    {
        printer_list_t list = {port, 0};

        papplSystemIteratePrinters(system, print_printer_cb, &list);
        if (list.count == 0)
            fprintf(stderr, "  no printers yet; add one through the web interface\n");
    }
    fflush(stderr);

    papplSystemRun(system);
    papplSystemDelete(system);
    free(style_data);

    for (i = 0; i < (int)td_num_models; i++)
        free((char *)drivers[i].name);
    free(drivers);
    free(spool_alloc);

    return 0;
}
