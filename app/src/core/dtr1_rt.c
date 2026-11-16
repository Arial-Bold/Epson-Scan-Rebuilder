/*
 * Runtime for the recompiled Epson DTR1 engine: guest memory, the library calls it makes, and
 * the entry points in dtr1.h.
 */
#include "dt1_rt.h"
#include "dtr1.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

uint8_t *M;
CPU G;
uint16_t dt1_fcw = 0x037F;

#define GUEST_SIZE   0x100000000ull
#define IMAGE_END    0x96000u
#define SCRATCH      0x0F000000u
#define HEAP_BASE    0x10000000u
#define HEAP_END     0x70000000u
#define STACK_TOP    0x7F000000u

void dt1_dispatch_translated(uint32_t t, int *found);
void DT1_48d0c(void);   /* _IPC_CRInit  */
void DT1_48dd2(void);   /* _IPC_CRClose */
void DT1_48e30(void);   /* _IPC_SKInit  */
void DT1_48ed0(void);   /* _IPC_SKClose */

static jmp_buf g_fail;
static char g_err[256];

void dt1_fatal(const char *what, uint32_t addr)
{
    snprintf(g_err, sizeof g_err, "DTR stopped: %s at 0x%x", what, addr);
    longjmp(g_fail, 1);
}

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

static uint32_t hsize(uint32_t a)
{
    if (a < HEAP_BASE || a >= g_heap_top) return 0;
    return g_sizes[(a - HEAP_BASE) >> 4];
}

static void hfree(uint32_t a)
{
    uint32_t n = hsize(a);
    if (!n) return;
    Free *f = malloc(sizeof *f);
    f->addr = a;
    unsigned b = (n >> 4) % NBUCKET;
    f->next = g_buckets[b];
    g_buckets[b] = f;
}

static uint32_t ARG(int i) { return R32(G.esp + 4 + 4 * i); }
static double ARGD(int off) { return BD(R64(G.esp + 4 + off)); }

void dt1_import(uint32_t addr)
{
    switch (addr) {
    case 0x95005: case 0x95023: case 0x95032: case 0x950C8: hfree(ARG(0)); break; /* DisposePtr, free, delete, delete[] */
    case 0x95000: case 0x95019: case 0x950C3: G.eax = halloc(ARG(0), 0); break;   /* NewPtr, malloc, new[] */
    case 0x9509B: G.eax = halloc(ARG(0), 1); break;                               /* NewPtrClear */
    case 0x95037: {                                                               /* realloc */
        uint32_t old = ARG(0), n = ARG(1), a = halloc(n, 0), os = hsize(old);
        if (a && old) memmove(M + a, M + old, os < n ? os : n);
        hfree(old); G.eax = a; break;
    }
    case 0x9501E: memset(M + ARG(0), (int)(ARG(1) & 0xFF), ARG(2)); G.eax = ARG(0); break;
    case 0x95096: case 0x950BE: memmove(M + ARG(0), M + ARG(1), ARG(2)); G.eax = ARG(0); break;
    case 0x9500A: FPUSH(cos(ARGD(0))); break;
    case 0x9500F: FPUSH(atan(ARGD(0))); break;
    case 0x95014: FPUSH(sin(ARGD(0))); break;
    case 0x9508C: FPUSH(tan(ARGD(0))); break;
    case 0x95091: FPUSH(atan2(ARGD(0), ARGD(8))); break;
    case 0x9502D: FPUSH(pow(ARGD(0), ARGD(8))); break;
    case 0x950A0: G.eax = ARG(0); break;                                          /* __cxa_begin_catch */
    case 0x950B9: break;                                                          /* __cxa_end_catch */
    default: dt1_fatal("unsupported library call", addr);
    }
}

void dt1_dispatch(uint32_t t)
{
    int found = 0;
    dt1_dispatch_translated(t, &found);
    if (!found) dt1_fatal("indirect call to unknown address", t);
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
    if (n < 0x950DE) { snprintf(g_err, sizeof g_err, "DTR file is too short"); return -1; }
    /* data imports: __gxx_personality_v0 (unused), the IColReduction / ISkew vtables DTR defines */
    W32(0x950D2, SCRATCH + 0x400u); W32(0x950D6, 0x94E10u); W32(0x950DA, 0x94E70u);
    memset(&G, 0, sizeof G);
    dt1_fcw = 0x037F;
    return 0;
}

static int32_t guest_call(void (*fn)(void), int nwords, const uint32_t *args)
{
    uint32_t esp = STACK_TOP - 0x1000;
    esp -= 4u * (uint32_t)nwords;
    esp &= ~0xFu;
    for (int i = 0; i < nwords; i++) W32(esp + 4u * (uint32_t)i, args[i]);
    esp -= 4;
    W32(esp, 0xDEADBEEFu);
    G.esp = esp;
    fn();
    return (int32_t)G.eax;
}

/* obj->vtbl[slot](obj, args...) */
static int32_t vcall(uint32_t obj, int slot, int nwords, const uint32_t *args)
{
    uint32_t esp = STACK_TOP - 0x1000;
    esp -= 4u * (uint32_t)(nwords + 1);
    esp &= ~0xFu;
    W32(esp, obj);
    for (int i = 0; i < nwords; i++) W32(esp + 4u + 4u * (uint32_t)i, args[i]);
    esp -= 4;
    W32(esp, 0xDEADBEEFu);
    G.esp = esp;
    dt1_dispatch(R32(R32(obj) + 4u * (uint32_t)slot));
    return (int32_t)G.eax;
}

static void dbl(uint32_t *w, double d) { uint64_t u = DB(d); w[0] = (uint32_t)u; w[1] = (uint32_t)(u >> 32); }

int dtr1_color_smoothing(const char *engine_path, const Dtr1SmoothParam *p,
                         const uint8_t *preview, int pw, int ph, int prb, const int area[8],
                         uint8_t *image, int w, int h, int rb, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t pp = halloc(4, 1);
    uint32_t a1[8] = { (uint32_t)p->a, (uint32_t)p->b, (uint32_t)p->c, 0, 0, 0, 0, pp };
    dbl(a1 + 3, p->d); dbl(a1 + 5, p->e);
    int r = guest_call(DT1_48d0c, 8, a1);
    uint32_t obj = R32(pp);
    if (r || !obj) { snprintf(g_err, sizeof g_err, "Color Smoothing setup failed (%d)", r); goto done; }
    /* tagCS_IMAGE_INFO: data, width, lines, rowbytes, bits */
    uint32_t pd = halloc((uint32_t)((size_t)prb * ph), 0);
    if (!pd) { snprintf(g_err, sizeof g_err, "not enough memory for Color Smoothing"); goto done; }
    memcpy(M + pd, preview, (size_t)prb * ph);
    uint32_t pi = halloc(0x14, 1);
    W32(pi, pd); W32(pi + 4, (uint32_t)pw); W32(pi + 8, (uint32_t)ph); W32(pi + 12, (uint32_t)prb); W32(pi + 16, 24);
    uint32_t a2[10] = { pi, 2 };
    for (int i = 0; i < 8; i++) a2[2 + i] = (uint32_t)area[i];
    r = (int16_t)vcall(obj, 0, 10, a2);                  /* CreateColorPalette */
    hfree(pd);
    if (r) { snprintf(g_err, sizeof g_err, "Color Smoothing palette failed (%d)", r); goto close; }
    /* the scan, in bands so the guest heap holds it */
    int band = (int)((256u << 20) / (uint32_t)rb); if (band < 1) band = 1;
    uint32_t id = halloc((uint32_t)((size_t)rb * (band < h ? band : h)), 0), ii = halloc(0x14, 1);
    if (!id) { snprintf(g_err, sizeof g_err, "not enough memory for Color Smoothing"); goto close; }
    for (int y = 0; y < h && !r; y += band) {
        int n = h - y < band ? h - y : band;
        memcpy(M + id, image + (size_t)y * rb, (size_t)rb * n);
        W32(ii, id); W32(ii + 4, (uint32_t)w); W32(ii + 8, (uint32_t)n); W32(ii + 12, (uint32_t)rb); W32(ii + 16, 24);
        uint32_t a3[2] = { ii, 0 };
        r = (int16_t)vcall(obj, 1, 2, a3);               /* DoColorReduction */
        memcpy(image + (size_t)y * rb, M + id, (size_t)rb * n);
    }
    if (r) snprintf(g_err, sizeof g_err, "Color Smoothing failed (%d)", r);
    else rc = 0;
close:;
    uint32_t a4[1] = { obj };
    guest_call(DT1_48dd2, 1, a4);
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

int dtr1_deskew(const char *engine_path, const Dtr1Location *loc, int bits,
                const uint8_t *in, int w, int h, int rb, uint8_t *out, int out_rb, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    int ow = loc->loc[4], oh = loc->loc[5];
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t ld = halloc(0x30, 1);                       /* LOCATION_DATA */
    W64(ld, DB(loc->theta));
    for (int i = 0; i < 8; i++) W32(ld + 0xc + 4u * (uint32_t)i, (uint32_t)loc->loc[i]);
    uint32_t pp = halloc(4, 1);
    uint32_t a1[4] = { 0, (uint32_t)bits, ld, pp };
    int r = guest_call(DT1_48e30, 4, a1);
    uint32_t obj = R32(pp);
    if (r || !obj) { snprintf(g_err, sizeof g_err, "Correct Document Skew setup failed (%d)", r); goto done; }
    uint32_t a2[1] = { (uint32_t)oh };
    vcall(obj, 0, 1, a2);                                 /* GetSendInLine */
    uint32_t id = halloc((uint32_t)((size_t)rb * h), 0), od = halloc((uint32_t)((size_t)out_rb * oh), 1);
    if (!id || !od) { snprintf(g_err, sizeof g_err, "not enough memory for Correct Document Skew"); goto close; }
    memcpy(M + id, in, (size_t)rb * h);
    uint32_t si = halloc(0x20, 1);                       /* tagIMAGE_SKEW_INFO in / out: data, w, h, rowbytes */
    W32(si, id); W32(si + 4, (uint32_t)w); W32(si + 8, (uint32_t)h); W32(si + 12, (uint32_t)rb);
    W32(si + 16, od); W32(si + 20, (uint32_t)ow); W32(si + 24, (uint32_t)oh); W32(si + 28, (uint32_t)out_rb);
    uint32_t a3[2] = { si, si + 16 };
    r = vcall(obj, 1, 2, a3);
    if (r) snprintf(g_err, sizeof g_err, "Correct Document Skew failed (%d)", r);
    else { memcpy(out, M + od, (size_t)out_rb * oh); rc = 0; }
close:;
    uint32_t a4[1] = { obj };
    guest_call(DT1_48ed0, 1, a4);
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}
