/* es_scanner.h — Epson Perfection V550 scanner engine (see es_scanner.c). */
#ifndef ES_SCANNER_H
#define ES_SCANNER_H

#include <stddef.h>
#include <stdint.h>

#define ES_DEFAULT_PLUGIN \
    "/Library/Image Capture/Devices/EPSON Scanner.app/Contents/PlugIns/Interpreter EB.bundle"

typedef struct {
    char   model[32];
    int    basic_res, max_res;
    double flatbed_w_mm, flatbed_h_mm;
    double film_w_mm, film_h_mm;
    int    infrared;
} ESInfo;

typedef struct {
    int    film;           /* 1 = film holder (film light), 0 = flatbed */
    int    negative;       /* film type: 1 negative, 0 positive */
    int    color;          /* 1 = RGB, 0 = gray */
    int    infrared;       /* 1 = infrared pass (film only, 800/1600/3200 dpi) */
    int    depth16;        /* 1 = 16 bits per sample, 0 = 8 */
    int    dpi;            /* rounded up to a hardware resolution, unless exact */
    int    dpi_y;          /* exact only: sub-scan resolution (0 = dpi) */
    int    exact;          /* 1 = dpi / dpi_y come from Epson's resolution tables; don't round */
    int    fast_preview;   /* flatbed only */
    double area_x_mm, area_y_mm, area_w_mm, area_h_mm;   /* 0 size = whole area */
} ESScanParams;

typedef struct { int width, height, dpi, channels, depth, dpi_y; } ESScanResult;

/* Called with each block of image rows as it arrives; return nonzero to cancel. */
typedef int (*es_data_fn)(const uint8_t *data, uint32_t len, void *ctx);
typedef void (*es_progress_fn)(double fraction, void *ctx);

int  es_open(const char *plugin_path);    /* NULL = default ICA driver location */
void es_close(void);
int  es_info(ESInfo *out);
int  es_scan(const ESScanParams *p, es_data_fn data, es_progress_fn progress, void *ctx, ESScanResult *res);
void es_cancel(void);
/* Width/height of the scan in progress (valid once the first data block arrives). */
void es_current_size(int *w, int *h);
const char *es_last_error(void);
/* Scanner command trace on stderr (1 = commands, 2 = every status poll and data block). */
void es_set_verbose(int level);

#endif
