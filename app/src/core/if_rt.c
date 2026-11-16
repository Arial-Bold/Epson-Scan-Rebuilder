/*
 * Runtime for the recompiled Epson Image Filter plugin: guest memory, the
 * library functions it imports, and the entry points in if.h.
 * Call sequences follow Epson Scan's Scan Control (CImgProcessMgr).
 */
#include "if_rt.h"
#include "if.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

uint8_t *M;
CPU G;

#define GUEST_SIZE   0x100000000ull
#define IMAGE_END    0x92000u
#define SCRATCH      0x0F000000u
#define HEAP_BASE    0x10000000u
#define HEAP_END     0x70000000u
#define STACK_TOP    0x7F000000u

void IF_c23a(void);   /* _IMGFLT_InitFocus               */
void IF_c5d6(void);   /* _IMGFLT_CloseFocus              */
void IF_c806(void);   /* _IMGFLT_Focus_FocusFilter       */
void IF_c83e(void);   /* _IMGFLT_Focus_GetSendInLine     */
void IF_c86e(void);   /* _IMGFLT_Focus_ReviseUSM         */
void IF_c970(void);   /* _IMGFLT_Focus_MakeClippingTable */
void IF_c9b8(void);   /* _IMGFLT_Focus_MakeLimitParam    */
void IF_c2be(void);   /* _IMGFLT_InitMoire               */
void IF_c626(void);   /* _IMGFLT_CloseMoire              */
void IF_c8a0(void);   /* _IMGFLT_Moire_RemoveMoire       */
void IF_c8d8(void);   /* _IMGFLT_Moire_GetSendInLine     */
void if_dispatch_translated(uint32_t t, int *found);

static jmp_buf g_fail;
static char g_prm_path[1024];
static FILE *g_prm;

void if_set_dde_param(const char *path) { snprintf(g_prm_path, sizeof g_prm_path, "%s", path ? path : ""); }
static char g_err[256];

void if_fatal(const char *what, uint32_t addr)
{
    snprintf(g_err, sizeof g_err, "Image Filter stopped: %s at 0x%x", what, addr);
    longjmp(g_fail, 1);
}

/* ---- guest heap: bump allocator with size-bucketed reuse ---------------- */
#define NBUCKET 4096
typedef struct Free { uint32_t addr; struct Free *next; } Free;
static uint32_t g_heap_top;
static Free *g_buckets[NBUCKET];
static uint32_t *g_sizes;

static uint32_t halloc(uint32_t n, int zero)
{
    n = n < 16 ? 16 : (n + 15) & ~15u;
    unsigned b = (n >> 4) % NBUCKET;
    for (Free **pp = &g_buckets[b]; *pp; pp = &(*pp)->next) {
        uint32_t a = (*pp)->addr;
        if (g_sizes[(a - HEAP_BASE) >> 4] == n) {
            Free *f = *pp; *pp = f->next; free(f);
            if (zero) memset(M + a, 0, n);
            return a;
        }
    }
    if ((uint64_t)g_heap_top + n > HEAP_END) return 0;
    uint32_t a = g_heap_top;
    g_heap_top += n;
    g_sizes[(a - HEAP_BASE) >> 4] = n;
    memset(M + a, 0, n);
    return a;
}

static void hfree(uint32_t a)
{
    if (a < HEAP_BASE || a >= g_heap_top) return;
    uint32_t n = g_sizes[(a - HEAP_BASE) >> 4];
    if (!n) return;
    Free *f = malloc(sizeof *f);
    f->addr = a;
    unsigned b = (n >> 4) % NBUCKET;
    f->next = g_buckets[b];
    g_buckets[b] = f;
}

/* ---- imports ------------------------------------------------------------ */
static uint32_t ARG(int i) { return R32(G.esp + 4 + 4 * i); }
static double ARGD(int off) { return BD(R64(G.esp + 4 + off)); }
static int64_t ARG64(int i) { return (int64_t)((uint64_t)ARG(i) | ((uint64_t)ARG(i + 1) << 32)); }

void if_import(uint32_t addr)
{
    switch (addr) {
    case 0x9100A: case 0x9102D: case 0x9105A: hfree(ARG(0)); break;        /* free, DisposePtr, delete */
    case 0x9100F: case 0x91028: case 0x9105F: G.eax = halloc(ARG(0), 0); break; /* malloc, NewPtr, new */
    case 0x91069: G.eax = halloc(ARG(0), 1); break;                         /* NewPtrClear */
    case 0x91023: G.eax = halloc(ARG(0) * ARG(1), 1); break;                /* calloc */
    case 0x9103C: case 0x9107D:                                             /* memmove, memcpy */
        memmove(M + ARG(0), M + ARG(1), ARG(2)); G.eax = ARG(0); break;
    case 0x91055: memset(M + ARG(0), (int)(ARG(1) & 0xFF), ARG(2)); G.eax = ARG(0); break;
    case 0x91046: FPUSH(pow(ARGD(0), ARGD(8))); break;
    case 0x91064: FPUSH(atan(ARGD(0))); break;
    case 0x91032: { int64_t q = ARG64(0) / ARG64(2); G.eax = (uint32_t)q; G.edx = (uint32_t)((uint64_t)q >> 32); break; }
    case 0x91037: { int64_t r = ARG64(0) % ARG64(2); G.eax = (uint32_t)r; G.edx = (uint32_t)((uint64_t)r >> 32); break; }
    case 0x91005: case 0x91014: case 0x9101E: G.eax = 0; break;            /* keymgr */
    case 0x91050: G.eax = ARG(0); break;                                    /* __cxa_begin_catch */
    case 0x9104B: break;                                                    /* __cxa_end_catch */
    /* CDDE_Param::Open finds EsDDE.prm next to the EsDDE bundle's executable; here the file
     * comes from the path set by if_set_dde_param (Resources/EsDDE.prm) */
    case 0x91073: G.eax = g_prm_path[0] ? 1 : 0; break;                     /* CFBundleGetBundleWithIdentifier */
    case 0x9109B: G.eax = 1; break;                                         /* CFBundleCopyExecutableURL */
    case 0x91078: G.eax = 1; break;                                         /* CFURLGetFSRef */
    case 0x910A0: break;                                                    /* CFRelease */
    case 0x9108C: G.eax = 0; break;                                         /* FSGetCatalogInfo */
    case 0x91096: G.eax = 0; break;                                         /* FSMakeFSSpec */
    case 0x91091: {                                                         /* FSpOpenDF(spec, perm, &ref) */
        if (g_prm) fclose(g_prm);
        g_prm = fopen(g_prm_path, "rb");
        if (!g_prm) { G.eax = (uint32_t)-43; break; }
        W16(ARG(2), 1); G.eax = 0; break;
    }
    case 0x91082: {                                                         /* FSRead(ref, &count, buf) */
        uint32_t want = R32(ARG(1));
        size_t got = g_prm ? fread(M + ARG(2), 1, want, g_prm) : 0;
        W32(ARG(1), (uint32_t)got);
        G.eax = got == want ? 0 : (uint32_t)-39; break;
    }
    case 0x9106E: {                                                         /* SetFPos(ref, mode, offset) */
        int mode = (int)(ARG(1) & 0xFFFF), wh = mode == 2 ? SEEK_END : mode == 3 ? SEEK_CUR : SEEK_SET;
        G.eax = g_prm && fseek(g_prm, (long)(int32_t)ARG(2), wh) == 0 ? 0 : (uint32_t)-40; break;
    }
    case 0x91087: if (g_prm) { fclose(g_prm); g_prm = NULL; } G.eax = 0; break; /* FSClose */
    default: if_fatal("unsupported library call", addr);
    }
}

void if_dispatch(uint32_t t)
{
    int found = 0;
    if_dispatch_translated(t, &found);
    if (!found) if_fatal("indirect call to unknown address", t);
}

static int guest_init(const char *engine_path)
{
    if (!M) {
        void *p = mmap(NULL, GUEST_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) { snprintf(g_err, sizeof g_err, "can't reserve guest memory"); return -1; }
        M = p;
        g_sizes = calloc((HEAP_END - HEAP_BASE) >> 4, sizeof *g_sizes);
    } else {
        memset(M, 0, IMAGE_END);
        memset(g_sizes, 0, ((g_heap_top - HEAP_BASE) >> 4) * sizeof *g_sizes);
        madvise(M + HEAP_BASE, g_heap_top - HEAP_BASE, MADV_DONTNEED);
    }
    for (int b = 0; b < NBUCKET; b++) {
        Free *f = g_buckets[b];
        while (f) { Free *n = f->next; free(f); f = n; }
        g_buckets[b] = NULL;
    }
    g_heap_top = HEAP_BASE;
    FILE *f = fopen(engine_path, "rb");
    if (!f) { snprintf(g_err, sizeof g_err, "can't open %s", engine_path); return -1; }
    size_t n = fread(M, 1, IMAGE_END, f);
    fclose(f);
    if (n < 0x910C9) { snprintf(g_err, sizeof g_err, "Image Filter file is too short"); return -1; }
    for (uint32_t a = 0x910A5; a < 0x910C9; a += 4) W32(a, SCRATCH + 0x400u);   /* data imports */
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

/* tagIMAGE_INFO: data, width, lines, rowbytes, bits (short) */
static void set_info(uint32_t a, uint32_t data, int w, int lines, int rb, int bits)
{
    W32(a, data); W32(a + 4, (uint32_t)w); W32(a + 8, (uint32_t)lines); W32(a + 12, (uint32_t)rb); W16(a + 16, (uint32_t)bits);
}

int if_unsharp(const char *engine_path, const uint8_t *in, uint8_t *out, int width, int height, int rowbytes,
               int bits, const IfUsmParams *p, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }

    /* tagIMAGE_SETTING: in w,h,rowbytes; out w,h,rowbytes; bits */
    uint32_t set = halloc(0x1c, 1);
    W32(set, (uint32_t)width); W32(set + 4, (uint32_t)height); W32(set + 8, (uint32_t)rowbytes);
    W32(set + 12, (uint32_t)width); W32(set + 16, (uint32_t)height); W32(set + 20, (uint32_t)rowbytes);
    W16(set + 24, (uint32_t)bits);
    uint32_t flt = halloc(12, 1);
    W32(flt, (uint32_t)p->strength); W32(flt + 4, (uint32_t)p->radius); W32(flt + 8, (uint32_t)p->clipping);
    uint32_t a1[4] = { set, flt, (uint32_t)(p->flag & 0xFF), 0 };
    if (guest_call(IF_c23a, 4, a1) & 0xFF) { snprintf(g_err, sizeof g_err, "Unsharp Mask setup failed"); goto done; }

    uint32_t rev = halloc(0x1c, 1);
    for (int i = 0; i < 13; i++) W16(rev + 2u * (uint32_t)i, p->revise[i]);
    uint32_t a2[1] = { rev };
    guest_call(IF_c86e, 1, a2);
    uint32_t lut = halloc(768, 0);
    memcpy(M + lut, p->lut, 768);
    uint32_t a3[4] = { lut, (uint32_t)p->clip_ctl, (uint32_t)p->clip_rev, (uint32_t)p->clipping };
    guest_call(IF_c970, 4, a3);
    uint32_t lim = halloc(24, 0);
    for (int i = 0; i < 6; i++) W32(lim + 4u * (uint32_t)i, (uint32_t)p->limits[i]);
    uint32_t a4[2] = { lim, (uint32_t)p->radius };
    guest_call(IF_c9b8, 2, a4);

    /* stream: ask how many input lines each block of output needs */
    int block = getenv("IF_BLOCK") ? atoi(getenv("IF_BLOCK")) : 64, done_out = 0, done_in = 0;
    uint32_t inf = halloc(0x28, 1);
    while (done_out < height) {
        int n_out = height - done_out < block ? height - done_out : block;
        uint32_t a5[1] = { (uint32_t)n_out };
        int n_in = guest_call(IF_c83e, 1, a5);
        if (n_in < 0 || done_in + n_in > height) n_in = height - done_in;
        uint32_t ib = halloc((uint32_t)(rowbytes * (n_in ? n_in : 1)), 0);
        uint32_t ob = halloc((uint32_t)(rowbytes * n_out), 0);
        if (n_in) memcpy(M + ib, in + (size_t)done_in * rowbytes, (size_t)rowbytes * n_in);
        set_info(inf, ib, width, n_in, rowbytes, bits);
        set_info(inf + 0x14, ob, width, n_out, rowbytes, bits);
        uint32_t a6[2] = { inf, inf + 0x14 };
        int r = guest_call(IF_c806, 2, a6);
        if (r) { snprintf(g_err, sizeof g_err, "Unsharp Mask failed (%d)", r); goto done; }
        memcpy(out + (size_t)done_out * rowbytes, M + ob, (size_t)rowbytes * n_out);
        hfree(ib); hfree(ob);
        done_in += n_in;
        done_out += n_out;
    }
    uint32_t a7[1] = { 0 };
    guest_call(IF_c5d6, 1, a7);
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

/* shared block loop: fn_send(n_out) -> n_in, fn_run(&in, &out) */
/* Feeds the filter block by block: the filter says how many input lines it needs for the next
 * block of output lines (GetSendInLine), then runs on them. In and out may differ in size when
 * the filter also resizes (UMOIRE with Descreening). */
static int stream_ex(void (*fn_send)(void), void (*fn_run)(void), const uint8_t *in, uint8_t *out,
                     int in_w, int in_h, int in_rb, int out_w, int out_h, int out_rb, int bits)
{
    int block = getenv("IF_BLOCK") ? atoi(getenv("IF_BLOCK")) : 64, done_out = 0, done_in = 0;
    uint32_t inf = halloc(0x28, 1);
    while (done_out < out_h) {
        int n_out = out_h - done_out < block ? out_h - done_out : block;
        uint32_t a5[1] = { (uint32_t)n_out };
        int n_in = guest_call(fn_send, 1, a5);
        if (n_in < 0 || done_in + n_in > in_h) n_in = in_h - done_in;
        uint32_t ib = halloc((uint32_t)(in_rb * (n_in ? n_in : 1)), 0);
        uint32_t ob = halloc((uint32_t)(out_rb * n_out), 0);
        if (n_in) memcpy(M + ib, in + (size_t)done_in * in_rb, (size_t)in_rb * n_in);
        set_info(inf, ib, in_w, n_in, in_rb, bits);
        set_info(inf + 0x14, ob, out_w, n_out, out_rb, bits);
        uint32_t a6[2] = { inf, inf + 0x14 };
        int r = guest_call(fn_run, 2, a6);
        if (r) { snprintf(g_err, sizeof g_err, "filter failed (%d)", r); return -1; }
        memcpy(out + (size_t)done_out * out_rb, M + ob, (size_t)out_rb * n_out);
        hfree(ib); hfree(ob);
        done_in += n_in;
        done_out += n_out;
    }
    return 0;
}

static int stream(void (*fn_send)(void), void (*fn_run)(void), const uint8_t *in, uint8_t *out,
                  int width, int height, int rowbytes, int bits)
{
    return stream_ex(fn_send, fn_run, in, out, width, height, rowbytes, width, height, rowbytes, bits);
}

int if_moire_resize(const char *engine_path, const uint8_t *in, int in_w, int in_h, int in_rb,
                    uint8_t *out, int out_w, int out_h, int out_rb, int bits, int res,
                    const int filter1[3], const int filter2[3], int maxres, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t set = halloc(0x1c, 1);                 /* IMGSIZE: in w,h,rowbytes, out w,h,rowbytes, bits */
    W32(set, (uint32_t)in_w); W32(set + 4, (uint32_t)in_h); W32(set + 8, (uint32_t)in_rb);
    W32(set + 12, (uint32_t)out_w); W32(set + 16, (uint32_t)out_h); W32(set + 20, (uint32_t)out_rb);
    W16(set + 24, (uint32_t)bits);
    uint32_t a1[10] = { set, (uint32_t)res, (uint32_t)filter1[0], (uint32_t)filter1[1], (uint32_t)filter1[2],
                        (uint32_t)filter2[0], (uint32_t)filter2[1], (uint32_t)filter2[2], (uint32_t)maxres, 0 };
    if (guest_call(IF_c2be, 10, a1) & 0xFF) { snprintf(g_err, sizeof g_err, "filter setup failed"); goto done; }
    if (stream_ex(IF_c8d8, IF_c8a0, in, out, in_w, in_h, in_rb, out_w, out_h, out_rb, bits)) goto done;
    uint32_t a7[1] = { 0 };
    guest_call(IF_c626, 1, a7);
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

void IF_c388(void);   /* _IMGFLT_InitResize              */
void IF_c676(void);   /* _IMGFLT_CloseResize             */
void IF_c908(void);   /* _IMGFLT_Resize_Zoom             */
void IF_c940(void);   /* _IMGFLT_Resize_GetSendInLine    */

int if_resize(const char *engine_path, const uint8_t *in, int in_w, int in_h, int in_rb,
              uint8_t *out, int out_w, int out_h, int out_rb, int bits, int mode, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t set = halloc(0x1c, 1);
    W32(set, (uint32_t)in_w); W32(set + 4, (uint32_t)in_h); W32(set + 8, (uint32_t)in_rb);
    W32(set + 12, (uint32_t)out_w); W32(set + 16, (uint32_t)out_h); W32(set + 20, (uint32_t)out_rb);
    W16(set + 24, (uint32_t)bits);
    uint32_t a1[3] = { set, (uint32_t)mode, 0 };
    if (guest_call(IF_c388, 3, a1) & 0xFF) { snprintf(g_err, sizeof g_err, "resize setup failed"); goto done; }
    if (stream_ex(IF_c940, IF_c908, in, out, in_w, in_h, in_rb, out_w, out_h, out_rb, bits)) goto done;
    uint32_t a7[1] = { 0 };
    guest_call(IF_c676, 1, a7);
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

int if_moire(const char *engine_path, const uint8_t *in, uint8_t *out, int width, int height, int rowbytes,
             int bits, int res, const int filter1[3], const int filter2[3], int maxres2, char *err, size_t errlen)
{
    return if_moire_resize(engine_path, in, width, height, rowbytes, out, width, height, rowbytes, bits, res,
                           filter1, filter2, maxres2, err, errlen);
}

/* ---- DDE filters: Color Enhance, Text Enhancement (grayscale), Auto Area Segmentation ------ */
void IF_c406(void);   /* _IMGFLT_InitDDEColor    */
void IF_c4ee(void);   /* _IMGFLT_InitDDEEnhance  */
void IF_c562(void);   /* _IMGFLT_InitDDEAas      */
void IF_c6c6(void);   /* _IMGFLT_CloseDDEColor   */
void IF_c766(void);   /* _IMGFLT_CloseDDEEnhance */
void IF_c7ac(void);   /* _IMGFLT_CloseDDEAas     */

static uint32_t imgset(int in_w, int in_h, int in_rb, int out_w, int out_h, int out_rb, int bits)
{
    uint32_t set = halloc(0x1c, 1);
    W32(set, (uint32_t)in_w); W32(set + 4, (uint32_t)in_h); W32(set + 8, (uint32_t)in_rb);
    W32(set + 12, (uint32_t)out_w); W32(set + 16, (uint32_t)out_h); W32(set + 20, (uint32_t)out_rb);
    W16(set + 24, (uint32_t)bits);
    return set;
}

/* CImgProcessMgr::ImageProcess runs the object's first virtual (Color / DoEnhance /
 * AutoAreaSegmentation) on the image info pair; here once over the whole image. */
static int run_obj(uint32_t obj, const uint8_t *in, int w, int h, int in_rb, int in_bits,
                   uint8_t *out, int out_rb, int out_bits)
{
    uint32_t ib = halloc((uint32_t)((size_t)in_rb * h), 0), ob = halloc((uint32_t)((size_t)out_rb * h), 1);
    if (!ib || !ob) { snprintf(g_err, sizeof g_err, "not enough memory for the image"); return -1; }
    memcpy(M + ib, in, (size_t)in_rb * h);
    uint32_t inf = halloc(0x28, 1);
    set_info(inf, ib, w, h, in_rb, in_bits);
    set_info(inf + 0x14, ob, w, h, out_rb, out_bits);
    uint32_t esp = STACK_TOP - 0x1000;
    esp -= 12; esp &= ~0xFu;
    W32(esp, obj); W32(esp + 4, inf); W32(esp + 8, inf + 0x14);
    esp -= 4; W32(esp, 0xDEADBEEFu);
    G.esp = esp;
    if_dispatch(R32(R32(obj)));
    int r = (int32_t)G.eax;
    if (r) { snprintf(g_err, sizeof g_err, "filter failed (%d)", r); return -1; }
    memcpy(out, M + ob, (size_t)out_rb * h);
    return 0;
}

int if_dde_color(const char *engine_path, const uint8_t *rgb, int w, int h, int in_rb,
                 uint8_t *out, int out_rb, int out_bits, const IfDdeColor *p, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    /* SetImgInfo for Color Enhance: in 24-bit, out 1 (B&W without ADE) or 8 */
    uint32_t set = imgset(w, h, in_rb, w, h, out_rb, out_bits);
    /* tagCOLOR_SETTING: dev id, method, speed, CE type x2, resolution, threshold, ADE flag */
    uint32_t cs = halloc(0x14, 1);
    W32(cs, (uint32_t)p->dev_id);
    M[cs + 4] = (uint8_t)p->method; M[cs + 5] = (uint8_t)p->speed;
    M[cs + 6] = (uint8_t)p->ce_type; M[cs + 7] = (uint8_t)p->ce_type;
    W32(cs + 8, (uint32_t)p->resolution); W32(cs + 12, (uint32_t)p->threshold);
    M[cs + 16] = (uint8_t)(out_bits == 8);
    uint32_t pobj = halloc(4, 1);
    uint32_t a1[3] = { set, cs, pobj };
    if (guest_call(IF_c406, 3, a1) & 0xFF) { snprintf(g_err, sizeof g_err, "Color Enhance setup failed"); goto done; }
    uint32_t obj = R32(pobj);
    if (run_obj(obj, rgb, w, h, in_rb, 24, out, out_rb, out_bits)) goto done;
    uint32_t a2[1] = { obj };
    guest_call(IF_c6c6, 1, a2);
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

int if_dde_enhance(const char *engine_path, const uint8_t *in, uint8_t *out, int w, int h, int rb, int bits,
                   int dev_id, int resolution, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t set = imgset(w, h, rb, w, h, rb, bits);
    uint32_t ti = halloc(8, 1);                       /* tagTXTENHSINFO: dev id, resolution */
    W32(ti, (uint32_t)dev_id); W32(ti + 4, (uint32_t)resolution);
    uint32_t pobj = halloc(4, 1);
    uint32_t a1[3] = { set, ti, pobj };
    if (guest_call(IF_c4ee, 3, a1) & 0xFF) { snprintf(g_err, sizeof g_err, "Text Enhancement setup failed"); goto done; }
    uint32_t obj = R32(pobj);
    if (run_obj(obj, in, w, h, rb, bits, out, rb, bits)) goto done;
    uint32_t a2[1] = { obj };
    guest_call(IF_c766, 1, a2);
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

int if_dde_aas(const char *engine_path, const uint8_t *gray, int w, int h, int in_rb,
               uint8_t *out, int out_rb, int dev_id, int resolution, int mode, const uint8_t gamma[256],
               char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t set = imgset(w, h, in_rb, w, h, out_rb, 8);
    uint32_t gt = halloc(256, 0);
    memcpy(M + gt, gamma, 256);
    /* tagDDEAASINFO: dev id, resolution, mode byte, gamma table pointer, 0, 0 */
    uint32_t ai = halloc(0x18, 1);
    W32(ai, (uint32_t)dev_id); W32(ai + 4, (uint32_t)resolution); M[ai + 8] = (uint8_t)mode;
    W32(ai + 12, gt);
    uint32_t pobj = halloc(4, 1);
    uint32_t a1[3] = { set, ai, pobj };
    if (guest_call(IF_c562, 3, a1) & 0xFF) { snprintf(g_err, sizeof g_err, "Auto Area Segmentation setup failed"); goto done; }
    uint32_t obj = R32(pobj);
    if (run_obj(obj, gray, w, h, in_rb, 8, out, out_rb, 1)) goto done;
    uint32_t a2[1] = { obj };
    guest_call(IF_c7ac, 1, a2);
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}
