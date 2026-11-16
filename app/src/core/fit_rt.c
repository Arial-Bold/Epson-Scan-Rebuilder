/*
 * Runtime for the recompiled Epson FIT engine: guest memory, the library
 * functions FIT imports, and the public entry points in fit.h.
 *
 * Call sequence copied from Epson Scan's Scan Control plugin
 * (CExtScanner auto exposure): FIT3() on the preview, then MakeFIT3LUTEx().
 */
#include "fit_rt.h"
#include "fit.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

uint8_t *M;
CPU G;

#define GUEST_SIZE   0x100000000ull
#define IMAGE_END    0x42000u          /* FIT file: segments at their file offsets */
#define SCRATCH      0x0F000000u
#define HEAP_BASE    0x10000000u
#define HEAP_END     0x70000000u
#define STACK_TOP    0x7F000000u

/* entry points (addresses in FIT.bundle, i386) */
void FIT_335e0(void);   /* _FIT3          */
void FIT_34766(void);   /* _MakeFIT3LUTEx */
void FIT_348b6(void);   /* _MakeFIT3LUT   */
void fit_dispatch_translated(uint32_t t, int *found);

static jmp_buf g_fail;
static char g_err[256];

void fit_fatal(const char *what, uint32_t addr)
{
    snprintf(g_err, sizeof g_err, "FIT engine stopped: %s at 0x%x", what, addr);
    longjmp(g_fail, 1);
}

/* ---- guest heap (bump allocator; one FIT run allocates little) ----------- */

static uint32_t g_heap_top;

static uint32_t halloc(uint32_t n)
{
    n = n < 16 ? 16 : (n + 15) & ~15u;
    if ((uint64_t)g_heap_top + n > HEAP_END) return 0;
    uint32_t a = g_heap_top;
    g_heap_top += n;
    memset(M + a, 0, n);
    return a;
}

/* ---- imports ------------------------------------------------------------ */

static uint32_t ARG(int i) { return R32(G.esp + 4 + 4 * i); }
static double ARGD(int off) { return BD(R64(G.esp + 4 + off)); }

void fit_import(uint32_t addr)
{
    switch (addr) {
    case 0x4100A: case 0x41032: case 0x4103C:                 /* free, delete[], delete */
        break;
    case 0x4100F: case 0x41041: case 0x41064:                 /* malloc, new[], new */
        G.eax = halloc(ARG(0)); break;
    case 0x41023:                                             /* calloc */
        G.eax = halloc(ARG(0) * ARG(1)); break;
    case 0x4105F:                                             /* memcpy */
        memmove(M + ARG(0), M + ARG(1), ARG(2)); G.eax = ARG(0); break;
    case 0x41028: FPUSH(log(ARGD(0))); break;                 /* log */
    case 0x4102D: FPUSH(log10(ARGD(0))); break;               /* log10 */
    case 0x41046: FPUSH(pow(ARGD(0), ARGD(8))); break;        /* pow */
    case 0x4104B: FPUSH(cos(ARGD(0))); break;                 /* cos */
    case 0x41050: FPUSH(atan(ARGD(0))); break;                /* atan */
    case 0x41055: FPUSH(sin(ARGD(0))); break;                 /* sin */
    case 0x4105A: {                                           /* modf(x, &ipart) */
        double ip, fr = modf(ARGD(0), &ip);
        W64(ARG(2), DB(ip));
        FPUSH(fr);
        break;
    }
    case 0x41005: case 0x41014: case 0x4101E:                 /* keymgr (C++ runtime) */
        G.eax = 0; break;
    default:
        fit_fatal("unsupported library call", addr);
    }
}

void fit_dispatch(uint32_t t)
{
    int found = 0;
    fit_dispatch_translated(t, &found);
    if (!found) fit_fatal("indirect call to unknown address", t);
}

/* ---- setup -------------------------------------------------------------- */

static int guest_init(const char *engine_path)
{
    if (!M) {
        void *p = mmap(NULL, GUEST_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) {
            snprintf(g_err, sizeof g_err, "can't reserve guest memory");
            return -1;
        }
        M = p;
    } else {
        memset(M, 0, IMAGE_END);
        memset(M + HEAP_BASE, 0, g_heap_top - HEAP_BASE);
    }
    g_heap_top = HEAP_BASE;
    FILE *f = fopen(engine_path, "rb");
    if (!f) {
        snprintf(g_err, sizeof g_err, "can't open the FIT engine file %s", engine_path);
        return -1;
    }
    size_t n = fread(M, 1, IMAGE_END, f);
    fclose(f);
    if (n < 0x41071) {
        snprintf(g_err, sizeof g_err, "FIT engine file is too short");
        return -1;
    }
    W32(0x41069, SCRATCH + 0x400u);   /* data imports -> harmless scratch */
    W32(0x4106D, SCRATCH + 0x400u);
    memset(&G, 0, sizeof G);
    return 0;
}

static int32_t guest_call(void (*fn)(void), int nargs, const uint32_t *args)
{
    uint32_t esp = STACK_TOP - 0x1000;
    esp -= 4u * (uint32_t)nargs;
    esp &= ~0xFu;
    for (int i = 0; i < nargs; i++) W32(esp + 4u * (uint32_t)i, args[i]);
    esp -= 4;
    W32(esp, 0xDEADBEEFu);
    G.esp = esp;
    fn();
    return (int32_t)G.eax;
}

/* ---- public API --------------------------------------------------------- */

/* FIT's own ValueOfScreenGammaConv(value, para) */
void FIT_34608(void);
static uint32_t screen_conv(uint32_t para, uint32_t v)
{
    uint32_t a[2] = { v, para };
    return (uint32_t)guest_call(FIT_34608, 2, a);
}

/* CFitIF::ResetFIT3_IC (UI plugin): starting histogram values per film type */
static void reset_fit3_ic(uint32_t para, int ft)
{
    W32(para + 0x4c, 0); W32(para + 0x50, 0); W32(para + 0x68, 0);
    W32(para + 0x54, 0); W32(para + 0x58, 0); W32(para + 0x5c, 0);
    if (ft == 1) {                                   /* flatbed document (colour management mode 0) */
        W16(para + 0x48, 1); W16(para + 0x4a, 5);
        W32(para + 0x0c, 0xe6e6); W32(para + 0x10, 0x2323); W32(para + 0x1c, 0x6b);
        W32(para + 0x14, 0xffff); W32(para + 0x18, 0xa0a);
        for (uint32_t o = 0x0c; o <= 0x18; o += 4) W32(para + o, screen_conv(para, R32(para + o)));
    } else if (ft < 1 || ft > 3) {                   /* positive film, flatbed photo */
        W16(para + 0x48, 1); W16(para + 0x4a, 4);
        W32(para + 0x0c, 0xf5f5); W32(para + 0x10, 0xa0a); W32(para + 0x14, 0xf5f5); W32(para + 0x18, 0xa0a);
        W32(para + 0x1c, 100);
        for (uint32_t o = 0x0c; o <= 0x18; o += 4) W32(para + o, screen_conv(para, R32(para + o)));
    } else {                                         /* negatives */
        W16(para + 0x48, 2); W16(para + 0x4a, 5);
        W32(para + 0x0c, 0x1e1e); W32(para + 0x10, 0xc8c8); W32(para + 0x14, 0xc8c8); W32(para + 0x18, 0xa0a);
        W32(para + 0x1c, 0x96);
        W32(para + 0x0c, screen_conv(para, R32(para + 0x0c)));
        W32(para + 0x10, screen_conv(para, R32(para + 0x10)));
        W32(para + 0x14, 0xffff - screen_conv(para, R32(para + 0x14)));
        W32(para + 0x18, 0xffff - screen_conv(para, R32(para + 0x18)));
    }
    for (uint32_t i = 0; i < 5; i++) {
        W32(para + 0x20 + 4 * i, R32(para + 0x0c + 4 * i));
        W32(para + 0x34 + 4 * i, R32(para + 0x0c + 4 * i));
    }
}

/* Flatbed red-correction matrix: the UI's final MakeLUT always asks GetRedCorrection(0, ..) */
static const double kFlatbedMatrix[9] = { 0.974, 0, 0, 0, 1.020, 0, 0, 0, 1.006 };

/* guest copies of a FitState: para with pointers to the tone buffers */
static uint32_t put_state(const FitState *st, uint32_t *toneM, uint32_t *toneRGB)
{
    uint32_t para = halloc(0x80);
    *toneM = halloc(0x84); *toneRGB = halloc(0x18C);
    memcpy(M + para, st->para, 0x80);
    memcpy(M + *toneM, st->tone_master, 0x84);
    memcpy(M + *toneRGB, st->tone_rgb, 0x18C);
    W32(para + 0x60, *toneM);
    W32(para + 0x64, *toneRGB);
    return para;
}

int fit_auto_state(const char *engine_path, const uint8_t *xrgb, int width, int height, int rowbytes,
                   int x0, int y0, int x1, int y1, const FitParams *p, FitState *st, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }

    uint32_t pix = halloc((uint32_t)rowbytes * (uint32_t)height);
    if (!pix) { snprintf(g_err, sizeof g_err, "preview too large"); goto done; }
    memcpy(M + pix, xrgb, (size_t)rowbytes * (size_t)height);

    /* FIT3_BmpInfo */
    uint32_t bmp = halloc(0x38);
    W32(bmp + 0x00, pix);
    W32(bmp + 0x04, (uint32_t)width);
    W32(bmp + 0x08, (uint32_t)height);
    W32(bmp + 0x0C, (uint32_t)rowbytes);
    W32(bmp + 0x10, 0x18);
    W32(bmp + 0x14, (uint32_t)x0); W32(bmp + 0x18, (uint32_t)y0);
    W32(bmp + 0x1C, (uint32_t)x0); W32(bmp + 0x20, (uint32_t)y0);
    W32(bmp + 0x24, (uint32_t)x1); W32(bmp + 0x28, (uint32_t)y1);
    W32(bmp + 0x2C, (uint32_t)x1); W32(bmp + 0x30, (uint32_t)y1);
    W16(bmp + 0x34, 2);                                   /* 32-bit pixels: x,R,G,B */

    /* FIT3_PARA, as the UI's CFitIF::SetFIT3_PARA + DoAutoExpose build it */
    uint32_t para = halloc(0x80);
    uint32_t toneM = halloc(0x84), toneRGB = halloc(0x18C);
    W16(para + 0x00, (uint32_t)p->type);
    W16(para + 0x02, (uint32_t)p->sub);
    W16(para + 0x04, 2);                                  /* colour management mode 0 */
    W16(para + 0x06, (uint32_t)p->cmd_type);
    W32(para + 0x08, (uint32_t)p->screen_gamma10);
    int ft = p->type == FIT_NEGA ? 3 : p->type == FIT_NEGA_BW ? 2 : (p->type == FIT_REFLECTIVE && p->sub == 1) ? 1 : 0;
    reset_fit3_ic(para, ft);
    W32(para + 0x60, toneM);
    W32(para + 0x64, toneRGB);
    W32(para + 0x70, (uint32_t)p->exposure_level);
    W32(para + 0x74, (uint32_t)p->backlight);
    W32(para + 0x78, toneM);                              /* FIT writes its tone curves here */
    W32(para + 0x7C, toneRGB);
    uint32_t mat = halloc(9 * 8);
    for (int i = 0; i < 9; i++) W64(mat + 8u * (uint32_t)i, DB(p->matrix[i]));

    uint32_t a[3] = { bmp, para, mat };
    if ((guest_call(FIT_335e0, 3, a) & 0xFF) == 0) {
        snprintf(g_err, sizeof g_err, "auto exposure failed");
        goto done;
    }
    if (getenv("FIT_DEBUG")) {
        fprintf(stderr, "after FIT3: +6c=%d +74=%d master n=%d:", (int)R32(para + 0x6C), (int)R32(para + 0x74), (int)(int16_t)R16(toneM));
        for (int i = 0; i < 10; i++) fprintf(stderr, " %d", (int)R32(toneM + 4 + 4u * (uint32_t)i));
        fprintf(stderr, "\n  hl/sd:");
        for (int i = 0; i < 15; i++) fprintf(stderr, " %d", (int)R32(para + 0x0C + 4u * (uint32_t)i));
        fprintf(stderr, "\n");
    }
    /* the marquee keeps FIT's results; SetFIT3_PARA clears +0x6c..+0x7f for the final table */
    W32(para + 0x6C, 0); W32(para + 0x74, 0); W32(para + 0x78, 0); W32(para + 0x7C, 0);
    memcpy(st->para, M + para, 0x80);
    memset(st->para + 0x60, 0, 8);                        /* pointers are re-made on use */
    memcpy(st->tone_master, M + toneM, 0x84);
    memcpy(st->tone_rgb, M + toneRGB, 0x18C);
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

int fit_state_lut(const char *engine_path, const FitState *st, uint8_t lut[768], char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t tm, tr, para = put_state(st, &tm, &tr);
    uint32_t out = halloc(768), fm = 0;
    if ((int16_t)R16(para) != FIT_NEGA_BW) {
        fm = halloc(9 * 8);
        for (int i = 0; i < 9; i++) W64(fm + 8u * (uint32_t)i, DB(kFlatbedMatrix[i]));
    }
    uint32_t b[3] = { para, fm, out };
    if ((guest_call(FIT_34766, 3, b) & 0xFF) != 1) {
        uint32_t c[2] = { para, out };
        if ((guest_call(FIT_348b6, 2, c) & 0xFF) != 1) {
            snprintf(g_err, sizeof g_err, "couldn't build the tone table");
            goto done;
        }
    }
    memcpy(lut, M + out, 768);
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

void FIT_348e0(void);   /* _ColorMatrixConv */

int fit_state_matrix(const char *engine_path, const FitState *st, const double profile[9], double coef[9],
                     char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t tm, tr, para = put_state(st, &tm, &tr);
    uint32_t in = halloc(72), outm = halloc(72);
    for (int i = 0; i < 9; i++) W64(in + 8u * (uint32_t)i, DB(profile[i]));
    uint32_t a[3] = { para, in, outm };
    if ((guest_call(FIT_348e0, 3, a) & 0xFF) != 1) { snprintf(g_err, sizeof g_err, "colour matrix failed"); goto done; }
    double m[9];
    for (int i = 0; i < 9; i++) m[i] = BD(R64(outm + 8u * (uint32_t)i));
    /* CPreviewImage::GenerateColorCoef(saturation, m) */
    double s = (double)(int32_t)R32(para + FIT_SATURATION);
    double d1 = (s / 100.0 + s / 100.0) / 3.0 + 1.0, d2 = (s / -100.0) / 3.0;
    coef[0] = m[6] * d2 + d1 * m[0] + m[3] * d2;
    coef[1] = m[7] * d2 + d1 * m[1] + m[4] * d2;
    coef[2] = m[8] * d2 + d1 * m[2] + m[5] * d2;
    coef[3] = m[6] * d2 + d2 * m[0] + m[3] * d1;
    coef[4] = m[7] * d2 + d2 * m[1] + m[4] * d1;
    coef[5] = m[8] * d2 + d2 * m[2] + m[5] * d1;
    coef[6] = m[6] * d1 + d2 * m[0] + m[3] * d2;
    coef[7] = m[7] * d1 + d2 * m[1] + m[4] * d2;
    coef[8] = m[8] * d1 + d2 * m[2] + m[5] * d2;
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

void FIT_34532(void);   /* _GetGrayBalance   */
void FIT_34b5c(void);   /* _GetGamma         */

/* CHostIF::SetParameter(0) with a gray balance set: FIT's GetGrayBalance on the levels the
 * marquee had when the gray point was picked. */
int fit_state_gray_balance(const char *engine_path, FitState *st, const int base[12], const int rgb[3],
                           int intensity, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t tm, tr, para = put_state(st, &tm, &tr);
    for (uint32_t c = 0; c < 3; c++)
        for (uint32_t k = 0; k < 4; k++) W32(para + 0x0c + c * 0x14 + k * 4, (uint32_t)base[c * 4 + k]);
    uint32_t a[5] = { (uint32_t)rgb[0], (uint32_t)rgb[1], (uint32_t)rgb[2], (uint32_t)intensity, para };
    if ((guest_call(FIT_34532, 5, a) & 0xFF) == 0) { snprintf(g_err, sizeof g_err, "gray balance failed"); goto done; }
    for (uint32_t c = 0; c < 3; c++)
        for (uint32_t k = 0; k < 4; k++) {
            uint32_t v = R32(para + 0x0c + c * 0x14 + k * 4);
            memcpy(st->para + 0x0c + c * 0x14 + k * 4, &v, 4);
        }
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

/* CFitIF::SetParameter_GetGamma: the mid-tone pipette sets each channel's gamma */
int fit_state_pipette_gamma(const char *engine_path, FitState *st, const int rgb[3], char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t tm, tr, para = put_state(st, &tm, &tr);
    uint32_t v = halloc(12);
    for (uint32_t i = 0; i < 3; i++) W32(v + 4 * i, (uint32_t)rgb[i]);
    uint32_t a[2] = { v, para };
    if ((guest_call(FIT_34b5c, 2, a) & 0xFF) == 0) { snprintf(g_err, sizeof g_err, "gamma pipette failed"); goto done; }
    for (uint32_t c = 0; c < 3; c++) {
        uint32_t g = R32(para + 0x1c + c * 0x14);
        memcpy(st->para + 0x1c + c * 0x14, &g, 4);
    }
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

void FIT_34c06(void);   /* _AdjustSpecifiedColor */

/* CFitIF::DoConvertColorPalette: FIT re-solves the levels (hl, sd, ohl, osd, gamma per
 * channel) so that the preview pixel `point` (gamma-1.8, 0..255) comes out as `target`
 * (display colour, 0..255). red = the film's red-correction matrix, coef = the preview's
 * colour coefficients (SaturationColorCoef). On success st's levels are replaced. */
int fit_state_adjust_color(const char *engine_path, FitState *st, const int point[3], const int target[3],
                           const double red[9], const double coef[9], char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t tm, tr, para = put_state(st, &tm, &tr);
    uint32_t p = halloc(12), t = halloc(12), rm = halloc(72), cm = halloc(72);
    for (uint32_t i = 0; i < 3; i++) { W32(p + 4 * i, (uint32_t)point[i]); W32(t + 4 * i, (uint32_t)target[i]); }
    for (uint32_t i = 0; i < 9; i++) { W64(rm + 8 * i, DB(red[i])); W64(cm + 8 * i, DB(coef[i])); }
    uint32_t a[5] = { para, p, t, rm, cm };
    if ((guest_call(FIT_34c06, 5, a) & 0xFF) == 0) { snprintf(g_err, sizeof g_err, "colour palette adjustment failed"); goto done; }
    for (uint32_t i = 0; i < 15; i++) {
        uint32_t v = R32(para + 0x0c + 4 * i);
        memcpy(st->para + 0x0c + 4 * i, &v, 4);
    }
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

/* CFitIF::Call_ValueOfScreenGammaConv for three pipette values (gamma 1.8, 16-bit) */
int fit_state_screen_values(const char *engine_path, const FitState *st, int rgb[3], char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t tm, tr, para = put_state(st, &tm, &tr);
    for (int i = 0; i < 3; i++) rgb[i] = (int)screen_conv(para, (uint32_t)rgb[i]);
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

int fit_auto_lut(const char *engine_path, const uint8_t *xrgb, int width, int height, int rowbytes,
                 int x0, int y0, int x1, int y1, const FitParams *p, uint8_t lut[768],
                 char *err, size_t errlen)
{
    FitState st;
    int rc = fit_auto_state(engine_path, xrgb, width, height, rowbytes, x0, y0, x1, y1, p, &st, err, errlen);
    return rc ? rc : fit_state_lut(engine_path, &st, lut, err, errlen);
}

void fit_make_lut12(const uint8_t lut[768], const double gain[3], uint16_t out[3 * 4096])
{
    for (int c = 0; c < 3; c++) {
        const uint8_t *L = lut + 256 * c;
        double g = gain ? gain[c] : 1.0;
        for (int i = 0; i < 4096; i++) {
            unsigned t = (unsigned)(pow(((double)i / g) / 4095.0, 0.5555555555555556) * 65535.0);
            int k = (int)(t >> 8), frac = (int)(t & 0xFF);
            int a = k < 256 ? k : 255, b = k + 1 < 256 ? k + 1 : 255;
            int d = ((int)L[b] * 0x101 - (int)L[a] * 0x101) * frac;
            if (d < 0) d += 0xFF;
            out[4096 * c + i] = (uint16_t)((d >> 8) + (int)L[a] * 0x101);
        }
    }
}
