/*
 * Epson Image Filter (Unsharp Mask, Descreening/Grain, Resize) — the original
 * plugin from Epson Scan 3.9.4, recompiled to native code.
 */
#ifndef IF_H
#define IF_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int      strength, radius, clipping;   /* from the V550 USM table (or custom)            */
    int      flag;                         /* FocusInfo+0x28, always 1 in Epson Scan          */
    const uint8_t *lut;                    /* 3 x 256 tone table the image went through       */
    int      clip_ctl, clip_rev;           /* HRD ClipCtl / ClipRev                           */
    int      limits[6];                    /* HRD ClipRevParm pairs                            */
    uint16_t revise[13];                   /* tagREVISE_PARMS (MakeReviseParmsForLUT)          */
} IfUsmParams;

/* Unsharp Mask on an RGB image, 8 (bits=24) or 16 (bits=48) bits per sample, rows of
 * `rowbytes`. In and out may not overlap. Returns 0 on success. */
int if_unsharp(const char *engine_path, const uint8_t *in, uint8_t *out, int width, int height, int rowbytes,
               int bits, const IfUsmParams *p, char *err, size_t errlen);
/* Moire filter (Descreening and Grain Reduction): blur with filter2, optional
 * re-sharpen with filter1, as UMOIRE in Epson's Image Filter. res = output dpi,
 * maxres2 = 2 x the scanner's maximum resolution. */
int if_moire(const char *engine_path, const uint8_t *in, uint8_t *out, int width, int height, int rowbytes,
             int bits, int res, const int filter1[3], const int filter2[3], int maxres2, char *err, size_t errlen);
/* The same filter scaling to a different output size on the way (Descreening scans at the
 * Descreening table's resolution and the filter produces the requested one). */
int if_moire_resize(const char *engine_path, const uint8_t *in, int in_w, int in_h, int in_rb,
                    uint8_t *out, int out_w, int out_h, int out_rb, int bits, int res,
                    const int filter1[3], const int filter2[3], int maxres, char *err, size_t errlen);
/* Epson's Resize (IMGFLT_InitResize / Resize_Zoom); mode 3 for gray/colour, 1 for 1-bit. */
int if_resize(const char *engine_path, const uint8_t *in, int in_w, int in_h, int in_rb,
              uint8_t *out, int out_w, int out_h, int out_rb, int bits, int mode, char *err, size_t errlen);

/* Path of EsDDE.prm (Epson's DDE parameter file), read by the three filters below. */
void if_set_dde_param(const char *path);
typedef struct {
    int dev_id;        /* HRD DDE parameter ID (0 for the V550)                    */
    int method;        /* processing method, 0                                     */
    int speed;         /* scan speed type                                          */
    int ce_type;       /* Color Enhance hue mask                                   */
    int resolution;    /* output dpi                                               */
    int threshold;     /* Black & White threshold (0-255)                          */
} IfDdeColor;
/* Color Enhance (DDEColor): 24-bit RGB in, 8-bit gray or 1-bit B&W out. */
int if_dde_color(const char *engine_path, const uint8_t *rgb, int w, int h, int in_rb,
                 uint8_t *out, int out_rb, int out_bits, const IfDdeColor *p, char *err, size_t errlen);
/* Text Enhancement Technology for 8-bit gray / 24-bit colour (DDEEnhance). */
int if_dde_enhance(const char *engine_path, const uint8_t *in, uint8_t *out, int w, int h, int rb, int bits,
                   int dev_id, int resolution, char *err, size_t errlen);
/* Auto Area Segmentation (DDEAas): 8-bit gray in, 1-bit out. */
int if_dde_aas(const char *engine_path, const uint8_t *gray, int w, int h, int in_rb,
               uint8_t *out, int out_rb, int dev_id, int resolution, int mode, const uint8_t gamma[256],
               char *err, size_t errlen);
#endif
