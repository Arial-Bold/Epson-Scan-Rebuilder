/*
 * Runtime for the recompiled EsDDE plug-in: guest memory, the library calls it makes, and
 * dde_binarize. The calls follow Scan Control (CScanner::DoDDEBinarization ->
 * CDDEManager::DDEBinarization: DDEBinNew(parm, 0), DDEBinDo(in, out, info), DDEBinDelete()).
 */
#include "dde_rt.h"
#include "dde.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

uint8_t *M;
CPU G;
uint16_t dde_fcw = 0x037F;

#define GUEST_SIZE   0x100000000ull
#define IMAGE_END    0x12000u
#define SCRATCH      0x0F000000u
#define HEAP_BASE    0x10000000u
#define HEAP_END     0x70000000u
#define STACK_TOP    0x7F000000u

void dde_dispatch_translated(uint32_t t, int *found);
void DDE_d39c(void);   /* _DDEBinNew    */
void DDE_d4bc(void);   /* _DDEBinDo     */
void DDE_d5a8(void);   /* _DDEBinDelete */

static jmp_buf g_fail;
static char g_err[256];
static char g_prm_path[1024];
static FILE *g_prm;

void dde_fatal(const char *what, uint32_t addr)
{
    snprintf(g_err, sizeof g_err, "Text Enhancement stopped: %s at 0x%x", what, addr);
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

/* ---- imports ------------------------------------------------------------ */
static uint32_t ARG(int i) { return R32(G.esp + 4 + 4 * i); }
static uint64_t ARG64(int i) { return (uint64_t)ARG(i) | ((uint64_t)ARG(i + 1) << 32); }

void dde_import(uint32_t addr)
{
    switch (addr) {
    case 0x1100A: case 0x11032: case 0x11041: hfree(ARG(0)); break;         /* free, DisposePtr, delete */
    case 0x1100F: case 0x1104B: G.eax = halloc(ARG(0), 0); break;           /* malloc, new */
    case 0x1102D: G.eax = halloc(ARG(0), 1); break;                         /* NewPtrClear */
    case 0x11023: G.eax = halloc(ARG(0) * ARG(1), 1); break;                /* calloc */
    case 0x11028: {                                                         /* realloc */
        uint32_t old = ARG(0), n = ARG(1), a = halloc(n, 0), os = hsize(old);
        if (a && old) memmove(M + a, M + old, os < n ? os : n);
        hfree(old); G.eax = a; break;
    }
    case 0x1103C: memset(M + ARG(0), (int)(ARG(1) & 0xFF), ARG(2)); G.eax = ARG(0); break;
    case 0x11046: { uint64_t d = ARG64(2), q = d ? ARG64(0) / d : 0;        /* __udivdi3 */
        G.eax = (uint32_t)q; G.edx = (uint32_t)(q >> 32); break; }
    case 0x11005: case 0x11014: case 0x1101E: G.eax = 0; break;            /* keymgr */
    case 0x11000: case 0x11019: G.eax = 0; break;                          /* dlopen, dlsym */
    /* CDDE_Param opens EsDDE.prm next to the bundle's executable; here it is g_prm_path */
    case 0x11055: G.eax = 1; break;                                         /* CFBundleGetBundleWithIdentifier */
    case 0x11078: G.eax = 1; break;                                         /* CFBundleCopyExecutableURL */
    case 0x1105A: G.eax = 1; break;                                         /* CFURLGetFSRef */
    case 0x1107D: break;                                                    /* CFRelease */
    case 0x11069: case 0x11073: G.eax = 0; break;                          /* FSGetCatalogInfo, FSMakeFSSpec */
    case 0x1106E:                                                           /* FSpOpenDF(spec, perm, &ref) */
        if (g_prm) fclose(g_prm);
        g_prm = fopen(g_prm_path, "rb");
        if (!g_prm) { G.eax = (uint32_t)-43; break; }
        W16(ARG(2), 1); G.eax = 0; break;
    case 0x1105F: {                                                         /* FSRead(ref, &count, buf) */
        uint32_t want = R32(ARG(1));
        size_t got = g_prm ? fread(M + ARG(2), 1, want, g_prm) : 0;
        W32(ARG(1), (uint32_t)got);
        G.eax = got == want ? 0 : (uint32_t)-39; break;
    }
    case 0x11050: {                                                         /* SetFPos(ref, mode, offset) */
        int mode = (int)(ARG(1) & 0xFFFF), wh = mode == 2 ? SEEK_END : mode == 3 ? SEEK_CUR : SEEK_SET;
        G.eax = g_prm && fseek(g_prm, (long)(int32_t)ARG(2), wh) == 0 ? 0 : (uint32_t)-40; break;
    }
    case 0x11064: if (g_prm) { fclose(g_prm); g_prm = NULL; } G.eax = 0; break; /* FSClose */
    default: dde_fatal("unsupported library call", addr);
    }
}

void dde_dispatch(uint32_t t)
{
    int found = 0;
    dde_dispatch_translated(t, &found);
    if (!found) dde_fatal("indirect call to unknown address", t);
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
    if (n < 0x11090) { snprintf(g_err, sizeof g_err, "EsDDE file is too short"); return -1; }
    W32(0x11082, SCRATCH + 0x400u); W32(0x11086, SCRATCH + 0x500u);       /* data imports */
    memset(&G, 0, sizeof G);
    dde_fcw = 0x037F;
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

int dde_binarize(const char *engine_path, const char *prm_path, const uint8_t *gray, int w, int h, int in_rb,
                 uint8_t *out, int out_rb, int dev_id, int resolution, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    snprintf(g_prm_path, sizeof g_prm_path, "%s", prm_path);
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t ib = halloc((uint32_t)((size_t)in_rb * h), 0), ob = halloc((uint32_t)((size_t)out_rb * h), 1);
    if (!ib || !ob) { snprintf(g_err, sizeof g_err, "not enough memory for the image"); goto done; }
    memcpy(M + ib, gray, (size_t)in_rb * h);
    uint32_t parm = halloc(8, 1);                     /* DDEBinParm: dev id, resolution */
    W32(parm, (uint32_t)dev_id); W32(parm + 4, (uint32_t)resolution);
    uint32_t info = halloc(0x20, 1);                  /* DDEBinInDataInfo: in w,h,rb,bits; out w,h,rb,bits */
    W32(info, (uint32_t)w); W32(info + 4, (uint32_t)h); W32(info + 8, (uint32_t)in_rb); W16(info + 12, 8);
    W32(info + 16, (uint32_t)w); W32(info + 20, (uint32_t)h); W32(info + 24, (uint32_t)out_rb); W16(info + 28, 1);
    uint32_t a1[2] = { parm, 0 };
    int r = guest_call(DDE_d39c, 2, a1);
    if (r) { snprintf(g_err, sizeof g_err, "Text Enhancement setup failed (%d)", r); goto done; }
    uint32_t a2[3] = { ib, ob, info };
    r = guest_call(DDE_d4bc, 3, a2);
    guest_call(DDE_d5a8, 0, NULL);
    if (r) { snprintf(g_err, sizeof g_err, r == 0x100 ? "not enough memory for Text Enhancement" : "Text Enhancement failed (%d)", r); goto done; }
    memcpy(out, M + ob, (size_t)out_rb * h);
    rc = 0;
done:
    if (g_prm) { fclose(g_prm); g_prm = NULL; }
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}
