/*
 * Target Size and Scale for one marquee, as Epson Scan's CMarqCtl does them (UI plug-in:
 * InitTargetSize, SetOriginalTargetSize, SetTargetSizeInfo, ChangeTargetSize, ChangeMarqueeScale,
 * ChangeMarqueeTarget, ChangeMarqueeSource, ChangeMarqueeSize, ESTargetLimitCheck,
 * CScanInf::GetScaleLimitValue). Sizes of the marquee are in inches; the target is kept in its
 * own unit like Epson's tab_UITarget.
 */
#ifndef ES_TARGET_H
#define ES_TARGET_H

enum { EST_PIXELS = 0, EST_MM = 1, EST_INCHES = 2 };

typedef struct {
    int    active;      /* marquee +0x14: target fixed (a size chosen, or Lock on) */
    int    trimming;    /* marquee +0: 0 Standard, 1 High */
    int    index;       /* entry in the Target Size menu (0 Original) */
    double tw, th;      /* target (+0x5c / +0x64) in `unit` */
    int    unit;        /* +0x6c */
    int    landscape;   /* +0x74 */
    double scale;       /* percent (+8) */
    double sw, sh;      /* marquee size, inches (+0x6fc / +0x704) */
    double ox, oy;      /* marquee offset, inches (+0x6ec / +0x6f4) */
    double ow, oh;      /* marquee size before trimming (+0x720 / +0x728) */
} EsTarget;

/* One Target Size menu entry (TSIZ 10000 in Epson's resources) */
typedef struct { const char *name; double w, h; int unit; } EsTargetPreset;
extern const EsTargetPreset es_target_presets[];
extern const int es_target_preset_count;

/* the scale range (percent) for a resolution: 50 x min res / res .. 2 x max res x 100 / res */
void   est_scale_limits(int res, double *min_pct, double *max_pct);
/* ConvertToUnits between pixels / mm / inches at `res` */
double est_convert(int from, int to, double v, int res);
/* Epson's decimals for a unit (0 / 1 / 2) */
int    est_decimals(int unit);

/* a new marquee: Original, 100% (CMarqCtl::InitTargetSize) */
void est_init(EsTarget *t, double sw, double sh, double ox, double oy);
/* the marquee was moved or resized in the preview (CMarqCtl::ChangeTargetSize) */
void est_marquee_changed(EsTarget *t, double sw, double sh, double ox, double oy, int res);
/* Scale typed or stepped (ChangeMarqueeScale). area = scan area, inches. 0 if unchanged. */
int  est_set_scale(EsTarget *t, double pct, int res, double area_w, double area_h);
/* a Target Size menu entry chosen (index > 0): orient it like the marquee, fix it, and trim
 * (High: the marquee is cut to the target's shape; Standard: the target follows the marquee's
 * shape). 0 when the scale would be out of range (Epson then goes back to Original). */
int  est_select(EsTarget *t, int index, double w, double h, int unit, int res, double area_w, double area_h);
/* back to Original with the marquee as it is */
void est_original(EsTarget *t);
/* Original: target width (which = 5) or height (6) typed, in inches (ChangeMarqueeSize) */
int  est_set_target_dim(EsTarget *t, int which, double v, int res);
/* the orientation button (DoADDestPrintDirectionButtonClick) */
int  est_rotate(EsTarget *t, double preset_w, double preset_h, int res, double area_w, double area_h);
/* the Lock button: fix / free the current target size */
void est_lock(EsTarget *t, int on);
/* the target in inches */
void est_target_inches(const EsTarget *t, int res, double *w, double *h);
#endif
