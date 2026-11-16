/*
 * Adjustments (Unsharp Mask, ...) as Epson Scan sets them up for the V550:
 * parameter lookups ported from Epson's Device Control / Scan Control plugins,
 * with the V550's tables from its hardware data file (EPSON Scan ES00EB HRD).
 */
#ifndef ES_ADJUST_H
#define ES_ADJUST_H
#include <stddef.h>
#include <stdint.h>
#include "if.h"

/* Epson's FilmType numbering */
enum { ES_FILM_POSI = 0, ES_FILM_NEGA = 1, ES_FILM_NONE = 2, ES_FILM_NEGA_BW = 3 };
/* USM level: 0 Low, 1 Medium, 2 High */

/* Fill Unsharp Mask parameters (strength/radius/clipping, clip tables, revise table).
 * dpi = output resolution, blc = Backlight Correction level (0 off). Returns 0 on success. */
int es_usm_params(int dpi, int level, int film_type, int blc, double screen_gamma,
                  const uint8_t lut[768], IfUsmParams *out);

/* Run Unsharp Mask on a raw RGB file (8 or 16 bits/sample), writing another raw file. */
int es_usm_file(const char *engine, const char *in_raw, const char *out_raw, int w, int h, int bits16,
                const IfUsmParams *p, char *err, size_t errlen);
/* Grain Reduction: V550 grain value for the output resolution and level (0 Low,
 * 1 Standard, 2 High), as Device Control's GetGrainReductionParam. -1 if none. */
int es_grain_value(int dpi, int level);

/* Grain Reduction on a raw RGB file (Epson's Moire filter, blur radius = grain value). */
int es_grain_file(const char *engine, const char *in_raw, const char *out_raw, int w, int h, int bits16,
                  int dpi, int grain, char *err, size_t errlen);
/* Descreening: Screen Ruling as listed in the menu (0 General, 1 Newspaper (85 lpi), 2 Magazine
 * (133 lpi), 3 Fine Prints (175 lpi)). For the output resolution, gives the resolution to scan
 * at and the filter value (Device Control's GetMoireParam with the V550's Descreening table).
 * Returns 0, or -1 when the resolution is above what Descreening supports. */
int es_descreen_params(int dpi, int ruling, int *scan_dpi, int *value);
/* Highest output resolution Descreening supports on the V550 (end of its table). */
int es_descreen_max_res(void);
/* Descreening on a raw file (1 or 3 samples per pixel, 8 or 16 bits): Epson's Moire filter
 * blurs the screen pattern and scales from the scanned size to the requested one. */
int es_descreen_file(const char *engine, const char *in_raw, int in_w, int in_h, const char *out_raw,
                     int out_w, int out_h, int spp, int bits16, int dpi, int value, char *err, size_t errlen);
/* The resolutions Epson scans at for a requested one (Device Control GetResolution with the
 * V550's tables; DIGITAL ICE uses its own). X and Y can differ (e.g. 300 dpi -> 400 x 320). */
int es_scan_resolution(int film, int ice, int dpi, int *scan_x, int *scan_y);
/* Epson's Resize filter from the scanned size to the requested one (1 or 3 samples, 8/16 bits). */
int es_resize_file(const char *engine, const char *in_raw, int in_w, int in_h, const char *out_raw,
                   int out_w, int out_h, int spp, int bits16, char *err, size_t errlen);
/* Dust Removal: the V550 dust table row for level (0 Low, 1 Standard, 2 High) and
 * film type, and the DDC image type (0 flatbed, 1 negative/B&W, 2 positive). */
void es_dust_params(int level, int film_type, int parm[8], int *ddc_type);

/* Dust Removal in place on a raw RGB file (Epson's EsDDC). */
int es_dust_file(const char *engine, const char *prm, const char *raw, int w, int h, int bits16, int dpi,
                 int ddc_type, const int parm[8], char *err, size_t errlen);
#endif
