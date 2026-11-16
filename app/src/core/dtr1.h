/*
 * Epson DTR (version 1) engine — Color Smoothing (CColReduction) and Correct Document Skew
 * (CSkew) from Epson Scan 3.9.4, recompiled to native code. Epson Scan reaches these through
 * DTR2 (CCR1 / CSkew1 -> IPC_CRInit / IPC_SKInit) and Scan Control (CEsDTR).
 */
#ifndef DTR1_H
#define DTR1_H
#include <stddef.h>
#include <stdint.h>

/* HRD ColorSmoothing record (PM_ClrSmoothParam) */
typedef struct { int a, b, c; double d, e; } Dtr1SmoothParam;

/* Color Smoothing: the palette comes from the preview (24-bit RGB) inside area[8]
 * (DEC_AREA: left, top, left, top, right, bottom, right, bottom in preview pixels), as
 * CExtScanner::DoSetColorSmoothing does, then the scan (24-bit RGB) is reduced to it in place
 * (CImgProcessMgr::ImageProcess). */
int dtr1_color_smoothing(const char *engine_path, const Dtr1SmoothParam *p,
                         const uint8_t *preview, int pw, int ph, int prb, const int area[8],
                         uint8_t *image, int w, int h, int rb, char *err, size_t errlen);

/* LOCATION_DATA: skew angle and the TWEP location words (top, left, width, height, actual width,
 * actual height, tangent x, tangent y) in the scan's pixels */
typedef struct { double theta; int32_t loc[8]; } Dtr1Location;

/* Correct Document Skew (CScanner::DoSkew): in is the scanned bounding area (bits 24, 8 or 1),
 * out gets loc[4] x loc[5] pixels with rows of out_rb bytes. */
int dtr1_deskew(const char *engine_path, const Dtr1Location *loc, int bits,
                const uint8_t *in, int w, int h, int rb, uint8_t *out, int out_rb, char *err, size_t errlen);
#endif
