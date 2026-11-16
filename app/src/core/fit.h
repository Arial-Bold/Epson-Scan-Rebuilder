/*
 * Epson FIT (film/photo auto exposure) — the original engine from Epson Scan
 * 3.9.4 (FIT.bundle), recompiled to native code, called the same way Epson's
 * Scan Control plugin calls it.
 */
#ifndef FIT_H
#define FIT_H

#include <stddef.h>
#include <stdint.h>

/* Document types, as FIT numbers them */
enum { FIT_NEGA = 0, FIT_POSI = 1, FIT_REFLECTIVE = 2, FIT_NEGA_BW = 3 };

typedef struct {
    int    type;            /* FIT_NEGA, FIT_POSI, ...                                   */
    int    sub;             /* 0 = normal, 2 = Color Restoration (FB: 0 photo, 1 document) */
    int    flag4;           /* AutoExposurePara+0x0e (0 in normal use)                    */
    int    cmd_type;        /* 1 for ESC/I-2 ("FS") scanners like the V550                */
    int    screen_gamma10;  /* display gamma x 10: 18 or 22                               */
    int    exposure_level;  /* Configuration > Auto Exposure Level, 1..4                  */
    int    backlight;       /* Backlight Correction level, 0 = off                        */
    double matrix[9];       /* red-correction matrix for this film type (row-major)       */
} FitParams;

/*
 * Auto exposure + tone table.
 *   engine_path : the original FIT i386 file (its constant tables are read from it)
 *   xrgb        : preview as 32-bit pixels, bytes x,R,G,B, gamma-1.8 encoded
 *                 (value = 255 * (linear)^(1/1.8)), rows of `rowbytes`
 *   x0..y1      : area to analyse (right/bottom exclusive)
 *   lut         : out, 3 x 256 bytes (R, G, B) in the gamma-1.8 domain
 * Returns 0 on success.
 */
int fit_auto_lut(const char *engine_path, const uint8_t *xrgb, int width, int height, int rowbytes,
                 int x0, int y0, int x1, int y1, const FitParams *p, uint8_t lut[768],
                 char *err, size_t errlen);

/* A marquee's auto-exposure result, editable by the image-adjustment dialogs:
 * FIT3_PARA (0x80 bytes) + master tone curve (0x84) + R/G/B tone curves (3 x 0x84). */
typedef struct { uint8_t para[0x80]; uint8_t tone_master[0x84]; uint8_t tone_rgb[0x18C]; } FitState;

/* field offsets in FitState.para that the dialogs change */
enum { FIT_BRIGHTNESS = 0x4C, FIT_CONTRAST = 0x50, FIT_BAL_CR = 0x54, FIT_BAL_MG = 0x58, FIT_BAL_YB = 0x5C,
       FIT_SATURATION = 0x68 };

/* Auto exposure only (FIT3): fills the state the way Epson's UI keeps it per marquee. */
int fit_auto_state(const char *engine_path, const uint8_t *xrgb, int width, int height, int rowbytes,
                   int x0, int y0, int x1, int y1, const FitParams *p, FitState *st, char *err, size_t errlen);

/* Final 3 x 256 tone table from a (possibly edited) state, like CFitIF::MakeLUT. */
int fit_state_lut(const char *engine_path, const FitState *st, uint8_t lut[768], char *err, size_t errlen);

/* Colour correction applied after the tone table (Epson's preview MakeMatrix):
 * coef = GenerateColorCoef(saturation, ColorMatrixConv(state, profile)). coef is row-major,
 * out_r = coef[0]*r + coef[1]*g + coef[2]*b, etc. */
int fit_state_matrix(const char *engine_path, const FitState *st, const double profile[9], double coef[9],
                     char *err, size_t errlen);

/* Histogram Adjustment helpers (the host side of the Image Control plug-in):
 *   gray balance: FIT GetGrayBalance(r, g, b (0..255), intensity 0..100) applied to `base`
 *                 (hl, sd, ohl, osd for R, G, B: the levels when the gray point was picked);
 *                 writes hl/sd/ohl/osd of st.
 *   pipette gamma: FIT GetGamma(rgb 0..255) -> each channel's gamma.
 *   screen values: ValueOfScreenGammaConv on 16-bit gamma-1.8 values (in place). */
int fit_state_gray_balance(const char *engine_path, FitState *st, const int base[12], const int rgb[3],
                           int intensity, char *err, size_t errlen);
int fit_state_pipette_gamma(const char *engine_path, FitState *st, const int rgb[3], char *err, size_t errlen);
int fit_state_screen_values(const char *engine_path, const FitState *st, int rgb[3], char *err, size_t errlen);

/* Color Palette (CFitIF::DoConvertColorPalette -> FIT AdjustSpecifiedColor): re-solve the
 * levels so the preview pixel `point` (gamma 1.8, 0..255) displays as `target` (0..255).
 * red = the film's red-correction matrix, coef = the colour coefficients in use. */
int fit_state_adjust_color(const char *engine_path, FitState *st, const int point[3], const int target[3],
                           const double red[9], const double coef[9], char *err, size_t errlen);

/*
 * Build Epson's 16-bit output curve from a 3 x 256 tone table, exactly like
 * the ICE LUT plugin (DICEMakeLUT12): for each 12-bit linear input step,
 * position = 65535 * (in / 4095 / gain)^(1/1.8) in the table, interpolated.
 * out: 3 x 4096 uint16 (R, G, B). gain may be NULL (1.0 each).
 */
void fit_make_lut12(const uint8_t lut[768], const double gain[3], uint16_t out[3 * 4096]);

/* Apply a 12-bit table to one 16-bit linear sample, interpolating like DICE (Linear_Complement). */
static inline uint16_t fit_lookup16(const uint16_t *t4096, uint16_t v)
{
    unsigned i = (v >> 4) & 0xFFF, j = i + 1 < 4096 ? i + 1 : 4095;
    int d = (int)(v & 0xF) * ((int)t4096[j] - (int)t4096[i]);
    if (d < 0) d += 15;
    return (uint16_t)((t4096[i] + (d >> 4)) & 0xFFFF);
}

#endif
