/*
 * Histogram Adjustment data, ported from Epson's Image Control plug-in
 * (CPlugInData master/channel distribution, CHistogramTable, CHistogram indicators).
 *
 * Values are 16-bit (0..65535) in the display-gamma domain, as FIT keeps them in
 * FIT3_PARA +0x0c..+0x47: per channel input highlight, input shadow, output highlight,
 * output shadow, gamma x100.
 */
#ifndef ES_LEVELS_H
#define ES_LEVELS_H

#include <stdint.h>

enum { LV_HL = 0, LV_SD = 1, LV_OHL = 2, LV_OSD = 3, LV_GAMMA = 4 };
enum { LV_MASTER = 0, LV_R = 1, LV_G = 2, LV_B = 3 };

typedef struct {
    int ch[3][5];      /* R, G, B: hl, sd, ohl, osd, gamma   (plug-in data +0x20..+0x5b) */
    int master[5];     /* derived master values               (plug-in data +0x78..+0x8b) */
} EsLevels;

/* FIT3_PARA (0x80 bytes) <-> levels */
void lv_from_para(EsLevels *lv, const uint8_t *para);
void lv_to_para(const EsLevels *lv, uint8_t *para);

/* CPlugInData::CalculateMasterHistogram(5) (colour) */
void lv_calc_master(EsLevels *lv);

/* Values the dialog shows for a channel (master is recalculated first). */
void lv_get(EsLevels *lv, int channel, int v[5]);

/*
 * Set one value the way the dialog does: the indicator clamps it against its partner
 * (shadow <= highlight - 0x606, gamma 10..999), then channel values are stored directly
 * and master values are spread over R, G and B (CalculateRGBHistogram(5)).
 * Returns the value actually stored.
 */
int lv_set(EsLevels *lv, int channel, int field, int value);

/* ---- histogram (CHistogramTable) ---------------------------------------- */

typedef struct {
    int valid;
    double scale;            /* 105 / ceil(w * h / 256 * 8) */
    int x[4][256];           /* draw x (0..235) per channel: 0 master, 1 R, 2 G, 3 B */
    int h[4][256];           /* bar height */
} EsHistogram;

/*
 * Build the histogram from 32-bit x,R,G,B gamma-1.8 pixels (what FIT gets) inside
 * [x0,x1) x [y0,y1). converted = 1 maps the axis to the display gamma (and flips
 * negatives) like the input histogram; 0 is used for "Show Output".
 */
void lv_histogram(EsHistogram *hg, const uint8_t *xrgb, int rowbytes, int x0, int y0, int x1, int y1,
                  int screen_gamma10, int negative, int converted);

/* GetConvertedXAxis: gamma-1.8 value 0..255 -> display axis 0..255 */
double lv_axis_x(int v, int screen_gamma10, int negative);

/* CHistogramInIndicator::CalculateGamma: gamma <-> centre arrow position (arrow coords 0..235) */
int lv_gamma_to_pos(int left, int right, int gamma);
int lv_pos_to_gamma(int left, int right, int pos);

/* ---- Tone Correction (CToneCurve) ----------------------------------------
 * A curve is FIT3_IC: int16 count (+2 pad), then up to 16 points of int32 x, y (0..255);
 * 0x84 bytes. FitState.tone_master and tone_rgb hold four of them (master, R, G, B). */
#define TC_SIZE 0x84
#define TC_MAX 16
int  tc_count(const uint8_t *ic);
void tc_point(const uint8_t *ic, int i, int *x, int *y);
void tc_linear(uint8_t *ic);
int  tc_equal(const uint8_t *a, const uint8_t *b);       /* same points */
/* survival range for point i: previous x + 2 .. next x - 2 (ends 0 / 255) */
void tc_survival(const uint8_t *ic, int i, int *lo, int *hi);
int  tc_insert(uint8_t *ic, int i, int x, int y);        /* InsertFixedPointToCurrent */
int  tc_remove(uint8_t *ic, int i);                     /* RemoveFixedPointFromCurrent (keeps >= 2) */
void tc_set(uint8_t *ic, int i, int x, int y);          /* SetCurrentFixedPoint (clamped) */
/* GetGammaCurve: natural cubic spline through the points, 0..255, flat past the ends */
void tc_curve(const uint8_t *ic, double out[256]);

/* Epson's presets (Image Control __DATA 0x41000): Linear, Lighten, Darken, Flat Contrast,
 * High Contrast, Open Shadow. Fills 4 curves (master, R, G, B). */
#define TC_PRESETS 6
const char *tc_preset_name(int i);
void tc_preset(int i, uint8_t curves[4][TC_SIZE]);

#endif
