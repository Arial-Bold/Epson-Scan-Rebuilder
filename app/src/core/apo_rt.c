/*
 * Runtime for Epson Scan's Auto Photo Orientation (getOrientation_reject_new in the UI plug-in,
 * with its face detector), recompiled from the original code; entry point in apo.h.
 */
#include "apo_rt.h"
#include "apo.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "apo_imports.h"

uint8_t *M;
CPU G;
uint16_t apo_fcw = 0x037F;

#define GUEST_SIZE   0x100000000ull
#define FILE_MAIN    0x200000u    /* __TEXT + __DATA: same offsets in the file and in memory */
#define IMPORT_VM    0x201000u    /* __IMPORT: file 0x200000 -> memory 0x201000 */
#define IMAGE_END    0x202000u
#define SCRATCH      0x0F000000u
#define HEAP_BASE    0x10000000u
#define HEAP_END     0x70000000u
#define STACK_TOP    0x7F000000u

void apo_dispatch_translated(uint32_t t, int *found);
void APO_15f762(void);   /* _getOrientation_reject_new(uchar *, long, long, long, double) */

static jmp_buf g_fail;
static int g_failing;
static char g_err[256];
static unsigned long g_rand = 1;

void apo_fatal(const char *what, uint32_t addr)
{
    snprintf(g_err, sizeof g_err, "Auto Photo Orientation stopped: %s at 0x%x", what, addr);
    if (g_failing) longjmp(g_fail, 1);
    abort();
}

/* ---- guest heap ----------------------------------------------------------- */
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

static uint32_t ARG(int i) { return R32(G.esp + 4 + 4 * i); }
static double ARGD(int off) { return BD(R64(G.esp + 4 + off)); }

static void run_at(uint32_t fn)
{
    int found = 0;
    apo_dispatch_translated(fn, &found);
    if (!found) apo_fatal("call to unknown address", fn);
}

/* Darwin libc rand(): Park-Miller "minimal standard" */
static int darwin_rand(void)
{
    long hi, lo, x;
    if (g_rand == 0) g_rand = 123459876;
    hi = (long)(g_rand / 127773);
    lo = (long)(g_rand % 127773);
    x = 16807 * lo - 2836 * hi;
    if (x < 0) x += 0x7fffffff;
    g_rand = (unsigned long)x;
    return (int)(x % 0x80000000L);
}

void apo_import(uint32_t addr)
{
    switch (addr) {
    case IMP_free: case IMP__ZdlPv: case IMP__ZdaPv: hfree(ARG(0)); return;
    case IMP_malloc: case IMP__Znwm: case IMP__Znam: G.eax = halloc(ARG(0), 0); return;
    case IMP_memmove: memmove(M + ARG(0), M + ARG(1), ARG(2)); G.eax = ARG(0); return;
    case IMP_memset: memset(M + ARG(0), (int)(ARG(1) & 0xFF), ARG(2)); G.eax = ARG(0); return;
    case IMP_exp: FPUSH(exp(ARGD(0))); return;
    case IMP_log: FPUSH(log(ARGD(0))); return;
    case IMP_pow: FPUSH(pow(ARGD(0), ARGD(8))); return;
    case IMP_rand: G.eax = (uint32_t)darwin_rand(); return;
    case IMP_fwrite: G.eax = ARG(2); return;                 /* diagnostics to stderr: dropped */
    case IMP_exit: apo_fatal("exit", ARG(0)); return;
    }
    for (size_t i = 0; i < sizeof kSelf / sizeof *kSelf; i++)
        if (kSelf[i][0] == addr) {      /* coalesced templates the UI plug-in defines itself */
            run_at(kSelf[i][1]);
            G.esp -= 4;
            return;
        }
    apo_fatal("unsupported library call", addr);
}

void apo_dispatch(uint32_t t)
{
    int found = 0;
    apo_dispatch_translated(t, &found);
    if (!found) apo_fatal("indirect call to unknown address", t);
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
    size_t n = fread(M, 1, FILE_MAIN, f);
    size_t n2 = fread(M + IMPORT_VM, 1, 0x1000, f);
    fclose(f);
    if (n < FILE_MAIN || n2 < 0x1000) { snprintf(g_err, sizeof g_err, "UI file is too short"); return -1; }
    for (uint32_t a = 0x201000; a <= 0x201100; a += 4) W32(a, SCRATCH + 0x400u + (a - 0x201000) * 16);
    for (size_t i = 0; i < sizeof kOwnData / sizeof *kOwnData; i++) W32(kOwnData[i][0], kOwnData[i][1]);   /* the classifier's tables */
    memset(&G, 0, sizeof G);
    apo_fcw = 0x037F;
    g_rand = 1;
    return 0;
}

int apo_orientation(const char *engine_path, const uint8_t *rgb, int width, int height, int rowbytes,
                    double reject, int *orientation, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    g_failing = 1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t img = halloc((uint32_t)rowbytes * (uint32_t)height, 0);
    if (!img) { snprintf(g_err, sizeof g_err, "not enough memory"); goto done; }
    memcpy(M + img, rgb, (size_t)rowbytes * height);
    uint32_t esp = STACK_TOP - 0x1000;
    esp -= 24;
    esp &= ~0xFu;
    W32(esp, img); W32(esp + 4, (uint32_t)width); W32(esp + 8, (uint32_t)height); W32(esp + 12, (uint32_t)rowbytes);
    W64(esp + 16, DB(reject));
    esp -= 4;
    W32(esp, 0xDEADBEEFu);
    G.esp = esp;
    APO_15f762();
    *orientation = (int32_t)G.eax;
    rc = 0;
done:
    g_failing = 0;
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}
