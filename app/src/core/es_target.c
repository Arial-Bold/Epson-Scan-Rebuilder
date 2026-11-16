#include "es_target.h"

/* TSIZ 10000 (UI Localized.rsrc): name, width, height, unit */
const EsTargetPreset es_target_presets[] = {
    { "Original", 0, 0, EST_INCHES },
    { "Thumbnail (80 x 120)", 80, 120, EST_PIXELS },
    { "Screen (240 x 320)", 240, 320, EST_PIXELS },
    { "Screen (480 x 640)", 480, 640, EST_PIXELS },
    { "Screen (600 x 800)", 600, 800, EST_PIXELS },
    { "Screen (768 x 1024)", 768, 1024, EST_PIXELS },
    { "10 x 15 cm (4 x 6 in.)", 4, 6, EST_INCHES },
    { "5 x 7 in.", 5, 7, EST_INCHES },
    { "Letter (8 1/2 x 11 in.)", 8.5, 11, EST_INCHES },
    { "A4 (210 x 297 mm)", 210, 297, EST_MM },
    { "Legal (8 1/2 x 14 in.)", 8.5, 14, EST_INCHES },
    { "8 x 10 in.", 8, 10, EST_INCHES },
    { "10 x 13 in.", 10, 13, EST_INCHES },
    { "11 x 14 in.", 11, 14, EST_INCHES },
    { "US B (11 x 17 in.)", 11, 17, EST_INCHES },
    { "A3 (297 x 420 mm)", 297, 420, EST_MM },
};
const int es_target_preset_count = (int)(sizeof es_target_presets / sizeof *es_target_presets);

/* the V550's resolution range (CScanBase::GetResolution: maximum, minimum) */
static const int kMaxRes = 6400, kMinRes = 50;
/* _stADTargetSizeLimitValue: per unit min, max, decimals */
static const double kLimit[3][2] = { { 5, 30000 }, { 2.5, 15240 }, { 0.1, 600 } };
static const int kDecimals[3] = { 0, 1, 2 };

int est_decimals(int unit) { return unit >= 0 && unit < 3 ? kDecimals[unit] : 2; }

void est_scale_limits(int res, double *min_pct, double *max_pct)
{
    double mn = (double)(kMinRes * 50) / res, mx = ((double)(kMaxRes * 2) * 100.0) / res;
    if (mn / 100.0 < 0.01) mn = 1.0;
    *min_pct = mn;
    *max_pct = mx;
}

static void limits(int res, double *mn, double *mx)
{
    double a, b;
    est_scale_limits(res, &a, &b);
    *mn = a / 100.0; *mx = b / 100.0;
}

/* ConvertToUnits(from, to, v, res, 100) */
double est_convert(int from, int to, double v, int res)
{
    if (from == to) return v;
    if (to == EST_MM) return (from == EST_INCHES ? v : v / res) * 2.54 * 10;
    if (to == EST_INCHES) return from == EST_MM ? v / 25.4 : (double)(int)v / res;
    if (to == EST_PIXELS) return (from == EST_MM ? v / 25.4 : v) * res;
    return v;
}

/* ESTargetLimitCheck (inches) */
static double limit_check(int unit, double v)
{
    if (v < kLimit[unit][0]) return kLimit[unit][0];
    if (v > kLimit[unit][1]) return kLimit[unit][1];
    return v;
}

void est_target_inches(const EsTarget *t, int res, double *w, double *h)
{
    *w = est_convert(t->unit, EST_INCHES, t->tw, res);
    *h = est_convert(t->unit, EST_INCHES, t->th, res);
}

/* SetOriginalTargetSize */
static void set_original(EsTarget *t, double w, double h)
{
    t->index = 0;
    t->tw = w; t->th = h;
    t->unit = EST_INCHES;
    t->landscape = h < w;
}

void est_init(EsTarget *t, double sw, double sh, double ox, double oy)
{
    int trimming = t->trimming;
    *t = (EsTarget){ 0 };
    t->trimming = trimming;
    t->sw = t->ow = sw; t->sh = t->oh = sh; t->ox = ox; t->oy = oy;
    t->scale = 100;
    set_original(t, sw, sh);
}

void est_original(EsTarget *t)
{
    t->active = 0;
    set_original(t, t->scale / 100.0 * t->sw, t->scale / 100.0 * t->sh);
}

void est_lock(EsTarget *t, int on)
{
    t->active = on ? 1 : 0;
    if (!on) est_original(t);
}

/* CMarqCtl::ChangeTargetSize */
void est_marquee_changed(EsTarget *t, double sw, double sh, double ox, double oy, int res)
{
    t->sw = sw; t->sh = sh; t->ox = ox; t->oy = oy;
    if (t->active) {
        double tw = est_convert(t->unit, EST_INCHES, t->tw, res), th = est_convert(t->unit, EST_INCHES, t->th, res);
        if (sw > 0 && sh > 0) {
            double s = tw / sw, s2 = th / sh, mn, mx;
            if (s < s2) s = s2;
            limits(res, &mn, &mx);
            if (s < mn) { s = mn; t->sw = tw / mn; t->sh = th / mn; }
            else if (mx < s) { s = mx; t->sw = tw / mx; t->sh = th / mx; }
            t->scale = s * 100.0;
        }
    } else {
        set_original(t, t->scale / 100.0 * t->sw, t->scale / 100.0 * t->sh);
    }
    t->ow = t->sw; t->oh = t->sh;
}

/* SetMarqueeArea, kept inside the scan area */
static void place(EsTarget *t, double x, double y, double w, double h, double aw, double ah)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (aw < x + w) x = aw - w;
    if (ah < y + h) y = ah - h;
    t->ox = x; t->oy = y; t->sw = w; t->sh = h;
}

/* CMarqCtl::ChangeMarqueeScale */
int est_set_scale(EsTarget *t, double pct, int res, double area_w, double area_h)
{
    if (t->scale == pct) return 0;
    if (t->active) {
        /* the target stays: the marquee becomes target / scale */
        double tw = est_convert(t->unit, EST_INCHES, t->tw, res), th = est_convert(t->unit, EST_INCHES, t->th, res);
        double x = t->ox, y = t->oy, w, h, s;
        if (1.0 < tw / th) {
            w = tw * 100.0 / pct;
            s = pct;
            if (area_w < x + w) { s = tw * 100.0 / (area_w - x); w = tw * 100.0 / s; }
            else if (w < 0.1) { s = tw * 100.0 / 0.1; w = tw * 100.0 / s; }
            s = tw * 100.0 / w;
            h = th * 100.0 / s;
            if (area_h < y + h) { s = th * 100.0 / (area_h - y); w = tw * 100.0 / s; h = th * 100.0 / s; }
            else if (h < 0.1) { s = th * 100.0 / 0.1; w = tw * 100.0 / s; h = th * 100.0 / s; }
        } else {
            h = th * 100.0 / pct;
            s = pct;
            if (area_h < y + h) { s = th * 100.0 / (area_h - y); h = th * 100.0 / s; }
            else if (h < 0.1) { s = th * 100.0 / 0.1; h = th * 100.0 / s; }
            s = th * 100.0 / h;
            w = tw * 100.0 / s;
            if (area_w < x + w) { s = tw * 100.0 / (area_w - x); w = tw * 100.0 / s; h = th * 100.0 / s; }
            else if (w < 0.1) { s = tw * 100.0 / 0.1; w = tw * 100.0 / s; h = th * 100.0 / s; }
        }
        t->scale = s;
        place(t, x, y, w, h, area_w, area_h);
        t->ow = t->sw; t->oh = t->sh;
    } else {
        double w = pct / 100.0 * t->sw, h = pct / 100.0 * t->sh, s = pct;
        if (w < h) {
            double l = limit_check(EST_INCHES, w);
            if (w != l && l != 600.0) { s = l / t->sw * 100.0; w = l; h = t->sh * l / t->sw; }
        } else {
            double l = limit_check(EST_INCHES, h);
            if (h != l && l != 600.0) { s = l / t->sh * 100.0; h = l; w = t->sw * l / t->sh; }
        }
        t->scale = s;
        t->tw = w; t->th = h;
        t->unit = EST_INCHES;
        t->landscape = h < w;
        t->index = 0;
    }
    return 1;
}

/* CMarqCtl::ChangeMarqueeTarget (High): the marquee is cut to the target's shape around its
 * centre, the scale makes it the target size */
static int trim_marquee(EsTarget *t, double tw, double th, int res, double aw, double ah)
{
    double mn, mx, s, w, h;
    limits(res, &mn, &mx);
    if (tw / th <= t->ow / t->oh) s = th / t->oh;
    else s = tw / t->ow;
    if (s < mn || mx < s) return 0;
    w = tw / s; h = th / s;
    if (w < 0.1 || h < 0.1) return 0;
    t->scale = s * 100.0;
    double x = (t->sw - w) / 2.0 + t->ox, y = (t->sh - h) / 2.0 + t->oy;
    if (aw < w) w = aw;
    if (ah < h) h = ah;
    place(t, x, y, w, h, aw, ah);
    return 1;
}

/* CMarqCtl::ChangeMarqueeSource (Standard): the marquee keeps its shape (back to its size before
 * any trimming), one side of the target follows it */
static int fit_target(EsTarget *t, double tw, double th, int res, double aw, double ah)
{
    double mn, mx, s, nw = -1, nh = -1;
    limits(res, &mn, &mx);
    if (tw / th <= t->ow / t->oh) {
        s = tw / t->ow;
        if (s < mn || mx < s) return 0;
        nh = s * t->oh;
    } else {
        s = th / t->oh;
        if (s < mn || mx < s) return 0;
        nw = s * t->ow;
    }
    if ((nw != -1 && nw < 0.1) || (nh != -1 && nh < 0.1)) return 0;
    t->scale = s * 100.0;
    double x = (t->sw - t->ow) / 2.0 + t->ox, y = (t->sh - t->oh) / 2.0 + t->oy;
    place(t, x, y, t->ow, t->oh, aw, ah);
    if (nw > 0) t->tw = est_convert(EST_INCHES, t->unit, nw, res);
    if (nh > 0) t->th = est_convert(EST_INCHES, t->unit, nh, res);
    return 1;
}

static int apply(EsTarget *t, int res, double aw, double ah)
{
    double tw = est_convert(t->unit, EST_INCHES, t->tw, res), th = est_convert(t->unit, EST_INCHES, t->th, res);
    if (tw <= 0 || th <= 0) return 0;
    return t->trimming ? trim_marquee(t, tw, th, res, aw, ah) : fit_target(t, tw, th, res, aw, ah);
}

int est_select(EsTarget *t, int index, double w, double h, int unit, int res, double area_w, double area_h)
{
    EsTarget keep = *t;
    /* SetTargetSizeInfo: the same way round as the marquee */
    t->landscape = t->sh < t->sw;
    if (t->landscape ? w < h : h < w) { double k = w; w = h; h = k; }
    t->index = index;
    t->tw = w; t->th = h; t->unit = unit;
    t->active = 1;
    if (!apply(t, res, area_w, area_h)) { *t = keep; return 0; }
    t->landscape = t->th < t->tw;
    return 1;
}

int est_rotate(EsTarget *t, double pw, double ph, int res, double area_w, double area_h)
{
    EsTarget keep = *t;
    int land = !t->landscape;
    double lo, hi;
    if (t->index == 0) { lo = t->th; hi = t->tw; }          /* Original: just swap */
    else { lo = pw < ph ? pw : ph; hi = pw < ph ? ph : pw; }
    if (t->index == 0) { t->tw = lo; t->th = hi; }
    else if (land) { t->tw = hi; t->th = lo; }
    else { t->tw = lo; t->th = hi; }
    if (!apply(t, res, area_w, area_h)) { *t = keep; return 0; }
    t->landscape = land;
    return 1;
}

/* CMarqCtl::ChangeMarqueeSize: Original, a target side typed: the scale follows, the other side
 * keeps the marquee's shape */
int est_set_target_dim(EsTarget *t, int which, double v, int res)
{
    double mn, mx, s;
    limits(res, &mn, &mx);
    double in = v;
    if (which == 5) s = in / t->sw;
    else s = in / t->sh;
    if (s < mn) s = mn;
    else if (mx < s) s = mx;
    double other = s * (which == 5 ? t->sh : t->sw);
    if (other < 0.1 || other > 600.0) s = (other < 0.1 ? 0.1 : 600.0) / (which == 5 ? t->sh : t->sw);
    t->scale = s * 100.0;
    double w = t->scale / 100.0 * t->sw, h = t->scale / 100.0 * t->sh;
    t->tw = w; t->th = h; t->unit = EST_INCHES; t->index = 0;
    t->landscape = h < w;
    return 1;
}
