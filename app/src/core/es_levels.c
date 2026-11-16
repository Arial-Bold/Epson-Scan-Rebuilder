/* Histogram Adjustment data - see es_levels.h. Ported from Image Control (CPlugInData,
 * CHistogramTable, CHistogramIn/OutIndicator); names follow the originals. */
#include "es_levels.h"
#include <math.h>
#include <string.h>

void lv_from_para(EsLevels *lv, const uint8_t *para)
{
    for (int c = 0; c < 3; c++)
        for (int k = 0; k < 5; k++) memcpy(&lv->ch[c][k], para + 0x0c + c * 0x14 + k * 4, 4);
    lv_calc_master(lv);
}

void lv_to_para(const EsLevels *lv, uint8_t *para)
{
    for (int c = 0; c < 3; c++)
        for (int k = 0; k < 5; k++) memcpy(para + 0x0c + c * 0x14 + k * 4, &lv->ch[c][k], 4);
}

static int ceil3(int a, int b, int c) { return (int)ceil((double)(a + b + c) / 3.0); }

void lv_calc_master(EsLevels *lv)
{
    for (int k = 0; k < 5; k++) lv->master[k] = ceil3(lv->ch[0][k], lv->ch[1][k], lv->ch[2][k]);
}

void lv_get(EsLevels *lv, int channel, int v[5])
{
    if (channel == LV_MASTER) {
        lv_calc_master(lv);
        memcpy(v, lv->master, sizeof lv->master);
    } else
        memcpy(v, lv->ch[channel - 1], sizeof lv->ch[0]);
}

/* ---- CPlugInData::CalculateDistribution*ValueColor ------------------------ */

/* one step of the shared loop: move up to q of d into *p, stopping at a limit */
static void dist_shadow(int target, int *p[3], const int lim[3])
{
    int d = target * 3 - *p[0] - *p[1] - *p[2], n = 3, done[3] = { 0, 0, 0 };
    if (d < 1) {
        do {
            if (n == 0) return;
            d = d < 0 ? -d : d;
            int q = (int)(unsigned)ceil((double)d / (double)n);
            for (int i = 0; i < 3; i++) {
                if (i && d - q < 0) q = d;
                if (!done[i] && d > 0) {
                    if (*p[i] - q < 0) { d -= *p[i]; *p[i] = 0; done[i] = 1; n--; }
                    else { *p[i] -= q; d -= q; }
                }
            }
        } while (d != 0);
    } else {
        do {
            if (n == 0) return;
            int q = (int)(unsigned)ceil((double)d / (double)n);
            for (int i = 0; i < 3; i++) {
                if (i && d - q < 0) q = d;
                if (!done[i] && d > 0) {
                    if (lim[i] - 0x606 < *p[i] + q) { d = (*p[i] - lim[i]) + d + 0x606; *p[i] = lim[i] - 0x606; done[i] = 1; n--; }
                    else { *p[i] += q; d -= q; }
                }
            }
        } while (d != 0);
    }
}

static void dist_highlight(int target, int *p[3], const int lim[3])
{
    int d = target * 3 - *p[0] - *p[1] - *p[2], n = 3, done[3] = { 0, 0, 0 };
    if (d < 1) {
        do {
            if (n == 0) return;
            d = d < 0 ? -d : d;
            int q = (int)(unsigned)ceil((double)d / (double)n);
            for (int i = 0; i < 3; i++) {
                if (i && d - q < 0) q = d;
                if (!done[i] && d > 0) {
                    if (*p[i] - q < lim[i] + 0x606) { d = (lim[i] - *p[i]) + d + 0x606; *p[i] = lim[i] + 0x606; done[i] = 1; n--; }
                    else { *p[i] -= q; d -= q; }
                }
            }
        } while (d != 0);
    } else {
        do {
            if (n == 0) return;
            int q = (int)(unsigned)ceil((double)d / (double)n);
            for (int i = 0; i < 3; i++) {
                if (i && d - q < 0) q = d;
                if (!done[i] && d > 0) {
                    if (*p[i] + q < 0x10000) { *p[i] += q; d -= q; }
                    else { d = (*p[i] + d) - 0xffff; *p[i] = 0xffff; done[i] = 1; n--; }
                }
            }
        } while (d != 0);
    }
}

static void dist_gamma(int target, int *p[3])
{
    int d = target * 3 - *p[0] - *p[1] - *p[2], n = 3, done[3] = { 0, 0, 0 };
    if (d < 1) {
        do {
            if (n == 0) return;
            d = d < 0 ? -d : d;
            int q = (int)(unsigned)ceil((double)d / (double)n);
            for (int i = 0; i < 3; i++) {
                if (i && d - q < 0) q = d;
                if (!done[i] && d > 0) {
                    if (*p[i] - q < 10) { d = (d - *p[i]) + 10; *p[i] = 10; done[i] = 1; n--; }
                    else { *p[i] -= q; d -= q; }
                }
            }
        } while (d != 0);
    } else {
        do {
            if (n == 0) return;
            int q = (int)(unsigned)ceil((double)d / (double)n);
            for (int i = 0; i < 3; i++) {
                if (i && d - q < 0) q = d;
                if (!done[i] && d > 0) {
                    if (*p[i] + q < 1000) { *p[i] += q; d -= q; }
                    else { d = (*p[i] + d) - 999; *p[i] = 999; done[i] = 1; n--; }
                }
            }
        } while (d != 0);
    }
}

/* CPlugInData::CalculateRGBHistogram(type): 0 sd, 1 gamma, 2 hl, 3 osd, 4 ohl */
static void calc_rgb(EsLevels *lv, int type)
{
    int *p[3], lim[3];
    switch (type) {
    case 0:
        for (int c = 0; c < 3; c++) { p[c] = &lv->ch[c][LV_SD]; lim[c] = lv->ch[c][LV_HL]; }
        dist_shadow(lv->master[LV_SD], p, lim);
        break;
    case 2:
        for (int c = 0; c < 3; c++) { p[c] = &lv->ch[c][LV_HL]; lim[c] = lv->ch[c][LV_SD]; }
        dist_highlight(lv->master[LV_HL], p, lim);
        break;
    case 3:
        for (int c = 0; c < 3; c++) { p[c] = &lv->ch[c][LV_OSD]; lim[c] = lv->ch[c][LV_OHL]; }
        dist_shadow(lv->master[LV_OSD], p, lim);
        break;
    case 4:
        for (int c = 0; c < 3; c++) { p[c] = &lv->ch[c][LV_OHL]; lim[c] = lv->ch[c][LV_OSD]; }
        dist_highlight(lv->master[LV_OHL], p, lim);
        break;
    case 1:
        for (int c = 0; c < 3; c++) p[c] = &lv->ch[c][LV_GAMMA];
        dist_gamma(lv->master[LV_GAMMA], p);
        break;
    }
}

/* the indicators' clamps (CHistogramIn/OutIndicator::Set*Value) */
static int clamp_field(const int v[5], int field, int x)
{
    switch (field) {
    case LV_SD:
        x = x < 0 ? 0 : x > 0xfdfd ? 0xfdfd : x;
        if (x > v[LV_HL] - 0x606) x = v[LV_HL] - 0x606;
        return x;
    case LV_HL:
        x = x < 2 ? 2 : x > 0xffff ? 0xffff : x;
        if (x < v[LV_SD] + 0x606) x = v[LV_SD] + 0x606;
        return x;
    case LV_OSD:
        x = x < 0 ? 0 : x > 0xffff ? 0xffff : x;
        if (x > v[LV_OHL] - 0x606) x = v[LV_OHL] - 0x606;
        return x;
    case LV_OHL:
        x = x < 0 ? 0 : x > 0xffff ? 0xffff : x;
        if (x < v[LV_OSD] + 0x606) x = v[LV_OSD] + 0x606;
        return x;
    default:
        return x < 10 ? 10 : x > 999 ? 999 : x;
    }
}

int lv_set(EsLevels *lv, int channel, int field, int value)
{
    if (channel != LV_MASTER) {
        int *v = lv->ch[channel - 1];
        v[field] = clamp_field(v, field, value);
        lv_calc_master(lv);
        return v[field];
    }
    lv_calc_master(lv);
    int m[5];
    memcpy(m, lv->master, sizeof m);
    int x = clamp_field(m, field, value);
    if (x == m[field]) return x;              /* DoDataExchange: unchanged master -> nothing */
    lv->master[field] = x;
    for (int t = 0; t < 5; t++) calc_rgb(lv, t);   /* CalculateRGBHistogram(5) */
    return x;
}

/* ---- CHistogramTable ------------------------------------------------------ */

double lv_axis_x(int v, int sg10, int negative)
{
    double x = pow((double)v / 255.0, 18.0 / (double)sg10) * 255.0;
    return negative ? 255.0 - x : x;
}

static double axis_y(int v, double y, int sg10)
{
    double e = 18.0 / (double)sg10 - 1.0, t = (double)v / 255.0, f;
    if (t != 0.0 || e >= 0.0) f = pow(t, e); else f = 0.0;
    double s = (18.0 / (double)sg10) * f;
    return s != 0.0 ? y / s : s;
}

/* ShadingOff: triangular smoothing, radius up to 5, narrower at the ends */
static void shading_off(const double *y, double *out)
{
    for (int i = 0; i < 256; i++) {
        if (i < 2 || i > 253) { out[i] = y[i]; continue; }
        int r = i - 1;
        if (254 - i < r) r = 254 - i;
        if (r > 5) r = 5;
        double s = 0;
        for (int k = -r; k <= r; k++) s += (double)(r + 1 - (k < 0 ? -k : k)) * y[i + k];
        out[i] = s / (double)((r + 1) * (r + 1));
    }
}

void lv_histogram(EsHistogram *hg, const uint8_t *xrgb, int rowbytes, int x0, int y0, int x1, int y1,
                  int sg10, int negative, int converted)
{
    double cnt[4][256];
    memset(hg, 0, sizeof *hg);
    memset(cnt, 0, sizeof cnt);
    if (x1 <= x0 || y1 <= y0) return;
    for (int y = y0; y < y1; y++) {
        const uint8_t *p = xrgb + (size_t)y * rowbytes + (size_t)x0 * 4;
        for (int x = x0; x < x1; x++, p += 4) {
            cnt[1][p[1]] += 1; cnt[2][p[2]] += 1; cnt[3][p[3]] += 1;
            cnt[0][((unsigned)p[1] + p[2] + p[3]) / 3] += 1;
        }
    }
    double w = (double)(x1 - x0) + 1, h = (double)(y1 - y0) + 1;
    int full = (int)ceil(w * h / 256.0 * 8.0);
    hg->scale = 105.0 / (double)full;
    for (int c = 0; c < 4; c++) {
        double dst[256], sm[256];
        memset(dst, 0, sizeof dst);
        for (int i = 0; i < 256; i++) {
            double xx = (double)i, yy = cnt[c][i];
            if (!converted) {
                /* CHistogramTable::ConvertXYAxis(raw): x stays, y stays (the film-type branch is a no-op) */
            } else {
                xx = lv_axis_x(i, sg10, negative);
                yy = axis_y(i, (double)(int)yy, sg10);
            }
            int k = (int)floor(xx + 0.5);
            if (k >= 0 && k < 256) dst[k] = yy;
        }
        shading_off(dst, sm);
        for (int i = 0; i < 256; i++) {           /* MakeDrawPoint */
            hg->x[c][i] = (int)floor((double)i / 255.0 * 235.0);
            hg->h[c][i] = (int)floor(hg->scale * sm[i]);
        }
    }
    hg->valid = 1;
}

/* ---- CHistogramInIndicator::CalculateGamma ------------------------------- */

int lv_gamma_to_pos(int left, int right, int gamma)
{
    double p = pow(2.0, (double)gamma / 100.0);
    return (int)floor((double)((right - 4) - (left + 4)) / p + (double)(left + 4) + 0.5);
}

int lv_pos_to_gamma(int left, int right, int pos)
{
    if (left + 4 < pos) {
        double g = log10((double)(right + (-8 - left)) / (double)(pos - (left + 4))) / log10(2.0);
        int v = (int)ceil(g * 100.0);
        return v < 10 ? 10 : v > 999 ? 999 : v;
    }
    return 999;
}

/* ---- Tone Correction ---------------------------------------------------- */

int tc_count(const uint8_t *ic)
{
    int16_t n;
    memcpy(&n, ic, 2);
    return n < 0 ? 0 : n > TC_MAX ? TC_MAX : n;
}

void tc_point(const uint8_t *ic, int i, int *x, int *y)
{
    int32_t v[2];
    memcpy(v, ic + 4 + 8 * i, 8);
    *x = v[0]; *y = v[1];
}

static void put_point(uint8_t *ic, int i, int x, int y)
{
    int32_t v[2] = { x, y };
    memcpy(ic + 4 + 8 * i, v, 8);
}

static void set_count(uint8_t *ic, int n)
{
    int16_t s = (int16_t)n;
    memcpy(ic, &s, 2);
}

void tc_linear(uint8_t *ic)
{
    memset(ic, 0, TC_SIZE);
    set_count(ic, 2);
    put_point(ic, 0, 0, 0);
    put_point(ic, 1, 255, 255);
}

int tc_equal(const uint8_t *a, const uint8_t *b)
{
    int n = tc_count(a);
    if (n != tc_count(b)) return 0;
    for (int i = 0; i < n; i++) {
        int ax, ay, bx, by;
        tc_point(a, i, &ax, &ay); tc_point(b, i, &bx, &by);
        if (ax != bx || ay != by) return 0;
    }
    return 1;
}

void tc_survival(const uint8_t *ic, int i, int *lo, int *hi)
{
    int n = tc_count(ic), x, y;
    if (i <= 0) *lo = 0; else { tc_point(ic, i - 1, &x, &y); *lo = x + 2; }
    if (i >= n - 1) *hi = 255; else { tc_point(ic, i + 1, &x, &y); *hi = x - 2; }
}

int tc_insert(uint8_t *ic, int i, int x, int y)
{
    int n = tc_count(ic);
    if (i >= TC_MAX || n >= TC_MAX) return 0;
    if (i < n) {
        for (int k = n; k > i; k--) { int px, py; tc_point(ic, k - 1, &px, &py); put_point(ic, k, px, py); }
        put_point(ic, i, x, y);
    } else
        put_point(ic, n, x, y);
    set_count(ic, n + 1);
    return 1;
}

int tc_remove(uint8_t *ic, int i)
{
    int n = tc_count(ic);
    if (i >= TC_MAX || n <= 2 || i < 0 || i >= n) return 0;
    for (int k = i; k < n - 1; k++) { int px, py; tc_point(ic, k + 1, &px, &py); put_point(ic, k, px, py); }
    put_point(ic, n - 1, 0, 0);
    set_count(ic, n - 1);
    return 1;
}

void tc_set(uint8_t *ic, int i, int x, int y)
{
    int lo, hi;
    if (i < 0 || i >= tc_count(ic)) return;
    tc_survival(ic, i, &lo, &hi);
    x = x < lo ? lo : x > hi ? hi : x;
    y = y < 0 ? 0 : y > 255 ? 255 : y;
    put_point(ic, i, x, y);
}

/* Numerical Recipes spline / splint, 1-based like the original, natural ends (yp = 1e30) */
void tc_curve(const uint8_t *ic, double out[256])
{
    int n = tc_count(ic);
    double xa[TC_MAX + 2], ya[TC_MAX + 2], y2[TC_MAX + 2], u[TC_MAX + 2];
    if (n < 2) { for (int i = 0; i < 256; i++) out[i] = i; return; }
    for (int k = 0; k < n; k++) { int x, y; tc_point(ic, k, &x, &y); xa[k + 1] = x; ya[k + 1] = y; }
    y2[1] = u[1] = 0.0;
    for (int i = 2; i <= n - 1; i++) {
        double sig = (xa[i] - xa[i - 1]) / (xa[i + 1] - xa[i - 1]);
        double p = sig * y2[i - 1] + 2.0;
        y2[i] = (sig - 1.0) / p;
        u[i] = (ya[i + 1] - ya[i]) / (xa[i + 1] - xa[i]) - (ya[i] - ya[i - 1]) / (xa[i] - xa[i - 1]);
        u[i] = (6.0 * u[i] / (xa[i + 1] - xa[i - 1]) - sig * u[i - 1]) / p;
    }
    y2[n] = 0.0;
    for (int k = n - 1; k >= 1; k--) y2[k] = y2[k] * y2[k + 1] + u[k];
    for (int v = 0; v < 256; v++) {
        int lo = 1, hi = n;
        while (hi - lo > 1) { int k = (hi + lo) >> 1; if ((double)v < xa[k]) hi = k; else lo = k; }
        double h = xa[hi] - xa[lo], r;
        if (h == 0) r = ya[lo];
        else {
            double a = (xa[hi] - v) / h, b = (v - xa[lo]) / h;
            r = ((a * a * a - a) * y2[lo] + (b * b * b - b) * y2[hi]) * h * h / 6.0 + a * ya[lo] + b * ya[hi];
        }
        if (r > 255.0) r = 255.0;
        if (r < 0.0) r = 0.0;
        if ((double)v < xa[1]) r = ya[1];
        if (xa[n] < (double)v) r = ya[n];
        out[v] = r;
    }
}

static const char *const kPresetNames[TC_PRESETS] = { "Linear", "Lighten", "Darken", "Flat Contrast",
                                                     "High Contrast", "Open Shadow" };
static const int kPresetMid[TC_PRESETS][3] = {          /* master y at x = 64, 128, 192 */
    { 0, 0, 0 }, { 74, 133, 193 }, { 38, 82, 152 }, { 72, 129, 182 }, { 51, 129, 204 }, { 80, 129, 193 } };

const char *tc_preset_name(int i) { return i >= 0 && i < TC_PRESETS ? kPresetNames[i] : ""; }

void tc_preset(int i, uint8_t curves[4][TC_SIZE])
{
    for (int c = 0; c < 4; c++) tc_linear(curves[c]);
    if (i <= 0 || i >= TC_PRESETS) return;
    uint8_t *m = curves[0];
    memset(m, 0, TC_SIZE);
    set_count(m, 5);
    put_point(m, 0, 0, 0);
    put_point(m, 1, 64, kPresetMid[i][0]);
    put_point(m, 2, 128, kPresetMid[i][1]);
    put_point(m, 3, 192, kPresetMid[i][2]);
    put_point(m, 4, 255, 255);
}
