/*
 * Image Option filters from Epson's DDE code: Color Enhance (Image Filter DDEColor), Text
 * Enhancement Technology (EsDDE binarization) and Auto Area Segmentation (Image Filter DDEAas).
 * Scan Control runs them after the scan, in that order (CImgProcessMgr::ImageProcess,
 * CScanner::DoDDEBinarization).
 */
#ifndef ES_DDE_H
#define ES_DDE_H
#include <stddef.h>
#include <stdint.h>

enum { ES_DDE_CE = 1, ES_DDE_TET = 2, ES_DDE_AAS = 3 };
/* HRD DDE parameter ID of the V550 (CFileIoHRD +0x4be) */
#define ES_DDE_DEV_ID 0

typedef struct {
    int option;            /* ES_DDE_*                                                         */
    int ce_mask;           /* Color Enhance hue mask: Red 0x20, Green 0x10, Blue 0x08          */
    int bw;                /* Black & White output (1-bit) instead of 8-bit Grayscale          */
    int threshold;         /* Black & White threshold                                          */
    int dpi;               /* output resolution                                                */
    double gamma;          /* the gray/colour encoding gamma (TET forces 1.8, as the UI does)  */
    const char *if_engine, *dde_engine, *prm;
} EsDdeJob;

/* raw: the scanner's data (spp 3 for Color Enhance, else 1 or 3), in_bits 8/16; out: 8-bit gray
 * or 1-bit rows (1 = black, the app's convention) */
int es_dde_run(const EsDdeJob *job, const char *raw, const char *out, int w, int h, int spp, int in_bits,
               int negative, char *err, size_t errlen);

#include "dtr1.h"
/* Correct Document Skew on a headerless raw file (spp 1/3, bits 1/8/16): out gets
 * loc->loc[4] x loc->loc[5] pixels. */
int es_deskew_file(const char *engine, const char *raw, const char *out, int w, int h, int spp, int bits,
                   const Dtr1Location *loc, int *ow, int *oh, char *err, size_t errlen);
/* Color Smoothing on a 24-bit RGB raw file, in place; the palette comes from the 24-bit
 * preview inside area (preview pixels: left, top, right, bottom). */
int es_color_smoothing_file(const char *engine, const char *raw, int w, int h,
                            const uint8_t *preview, int pw, int ph, const int area[4], char *err, size_t errlen);
#endif
