/*
 * Runtime for the recompiled Epson EsDDC (Dust Removal) plugin. Call sequence
 * follows Epson Scan's Scan Control (CScanner::PrepareDustRemove / DoDustRemove).
 */
#include "ddc_rt.h"
#include "ddc.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

uint8_t *M;
CPU G;

#define GUEST_SIZE   0x100000000ull
#define IMAGE_END    0xF000u
#define SCRATCH      0x0F000000u
#define HEAP_BASE    0x10000000u
#define HEAP_END     0xE0000000u
#define STACK_TOP    0xEF000000u
#define PROGRESS_CB  (SCRATCH + 0x500u)

void DDC_b7d8(void);   /* _DustCorrectionInit */
void DDC_b946(void);   /* _DustCorrection     */
void DDC_ba46(void);   /* _DustCorrectionEnd  */
void ddc_dispatch_translated(uint32_t t, int *found);

static jmp_buf g_fail;
static char g_err[256];
static const char *g_prm;
static FILE *g_file;

void ddc_fatal(const char *what, uint32_t addr)
{
    snprintf(g_err, sizeof g_err, "Dust Removal stopped: %s at 0x%x", what, addr);
    longjmp(g_fail, 1);
}

/* ---- heap (bump with size records, realloc support) --------------------- */
static uint32_t g_heap_top;
static uint32_t *g_sizes;
static uint32_t halloc(uint32_t n, int zero)
{
    n = n < 16 ? 16 : (n + 15) & ~15u;
    if ((uint64_t)g_heap_top + n > HEAP_END) return 0;
    uint32_t a = g_heap_top;
    g_heap_top += n;
    g_sizes[(a - HEAP_BASE) >> 4] = n;
    if (zero) memset(M + a, 0, n);
    return a;
}
static uint32_t hrealloc(uint32_t a, uint32_t n)
{
    if (!a) return halloc(n, 0);
    uint32_t old = g_sizes[(a - HEAP_BASE) >> 4];
    if (n <= old) return a;
    uint32_t b = halloc(n, 0);
    if (b) memcpy(M + b, M + a, old);
    return b;
}

static uint32_t ARG(int i) { return R32(G.esp + 4 + 4 * i); }

void ddc_import(uint32_t addr)
{
    switch (addr) {
    case 0xE000: G.eax = hrealloc(ARG(0), ARG(1)); break;                  /* realloc */
    case 0xE005: G.eax = halloc(ARG(0), 1); break;                          /* NewPtrClear */
    case 0xE00A: case 0xE019: case 0xE01E: break;                           /* DisposePtr, delete, free */
    case 0xE028: case 0xE02D: G.eax = halloc(ARG(0), 0); break;             /* new, malloc */
    case 0xE014: memset(M + ARG(0), (int)(ARG(1) & 0xFF), ARG(2)); G.eax = ARG(0); break;
    case 0xE03C: memmove(M + ARG(0), M + ARG(1), ARG(2)); G.eax = ARG(0); break;
    case 0xE023: {                                                          /* __udivdi3 */
        uint64_t a = (uint64_t)ARG(0) | ((uint64_t)ARG(1) << 32), b = (uint64_t)ARG(2) | ((uint64_t)ARG(3) << 32);
        uint64_t q = a / b; G.eax = (uint32_t)q; G.edx = (uint32_t)(q >> 32); break;
    }
    /* file access: the plugin opens EsDDC.prm next to its executable */
    case 0xE032: G.eax = SCRATCH + 0x600u; break;                           /* CFBundleGetBundleWithIdentifier */
    case 0xE05A: G.eax = SCRATCH + 0x640u; break;                           /* CFBundleCopyExecutableURL */
    case 0xE037: G.eax = 1; break;                                          /* CFURLGetFSRef */
    case 0xE05F: break;                                                     /* CFRelease */
    case 0xE04B: G.eax = 0; break;                                          /* FSGetCatalogInfo */
    case 0xE055: G.eax = 0; break;                                          /* FSMakeFSSpec */
    case 0xE050:                                                            /* FSpOpenDF(spec, perm, &ref) */
        if (g_file) fclose(g_file);
        g_file = g_prm ? fopen(g_prm, "rb") : NULL;
        W16(ARG(2), 1);
        G.eax = g_file ? 0 : (uint32_t)-43;
        break;
    case 0xE041: {                                                          /* FSRead(ref, &count, buf) */
        uint32_t want = R32(ARG(1));
        size_t got = g_file ? fread(M + ARG(2), 1, want, g_file) : 0;
        W32(ARG(1), (uint32_t)got);
        G.eax = got == want ? 0 : (uint32_t)(-39 & 0xFFFF);
        break;
    }
    case 0xE046: if (g_file) { fclose(g_file); g_file = NULL; } G.eax = 0; break;   /* FSClose */
    default: ddc_fatal("unsupported library call", addr);
    }
}

void ddc_dispatch(uint32_t t)
{
    if (t == PROGRESS_CB) {                                                /* progress: keep going */
        uint32_t info = R32(G.esp + 4);
        if (info) W32(info + 8, 1);
        if (getenv("DDC_TRACE")) fprintf(stderr, "progress %d\n", (int)R32(info + 4));
        G.eax = 0; G.esp += 4; return;
    }
    int found = 0;
    ddc_dispatch_translated(t, &found);
    if (!found) ddc_fatal("indirect call to unknown address", t);
}

static int guest_init(const char *engine_path)
{
    if (!M) {
        void *p = mmap(NULL, GUEST_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) { snprintf(g_err, sizeof g_err, "can't reserve guest memory"); return -1; }
        M = p;
        g_sizes = calloc((HEAP_END - HEAP_BASE) >> 4, sizeof *g_sizes);
        if (!g_sizes) { snprintf(g_err, sizeof g_err, "out of memory"); return -1; }
    } else {
        memset(M, 0, IMAGE_END);
        memset(g_sizes, 0, ((g_heap_top - HEAP_BASE) >> 4) * sizeof *g_sizes);
        madvise(M + HEAP_BASE, g_heap_top - HEAP_BASE, MADV_DONTNEED);
    }
    g_heap_top = HEAP_BASE;
    FILE *f = fopen(engine_path, "rb");
    if (!f) { snprintf(g_err, sizeof g_err, "can't open %s", engine_path); return -1; }
    size_t n = fread(M, 1, IMAGE_END, f);
    fclose(f);
    if (n < 0xE068) { snprintf(g_err, sizeof g_err, "EsDDC file is too short"); return -1; }
    W32(0xE064, SCRATCH + 0x400u);
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

int ddc_remove_dust(const char *engine_path, const char *prm_path, uint8_t *img, int width, int height,
                    int rowbytes, int bits, int dpi, int type, const int parm[8], char *err, size_t errlen)
{
    g_err[0] = 0;
    g_prm = prm_path;
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    size_t size = (size_t)rowbytes * (size_t)height;
    uint32_t data = halloc((uint32_t)size, 0);
    if (!data) { snprintf(g_err, sizeof g_err, "image too large for Dust Removal"); goto done; }
    memcpy(M + data, img, size);

    /* DDC_IMAGE_INFO */
    uint32_t ii = halloc(0x2c, 1);
    W16(ii + 0x00, (uint32_t)type);
    W32(ii + 0x04, data);
    W32(ii + 0x08, (uint32_t)width);
    W32(ii + 0x0c, (uint32_t)height);
    W32(ii + 0x10, (uint32_t)rowbytes);
    W32(ii + 0x14, (uint32_t)dpi);
    W16(ii + 0x18, (uint32_t)bits);
    W16(ii + 0x1a, 0);
    W32(ii + 0x1c, 0); W32(ii + 0x20, 0);
    W32(ii + 0x24, (uint32_t)(width - 1)); W32(ii + 0x28, (uint32_t)(height - 1));
    /* DDC_PARM_INFO */
    uint32_t pi = halloc(0x18, 1);
    W16(pi + 0x00, (uint32_t)parm[0]);
    W16(pi + 0x02, (uint32_t)parm[1]);
    W32(pi + 0x04, (uint32_t)parm[2]);
    W16(pi + 0x08, (uint32_t)parm[3]);
    W32(pi + 0x0c, (uint32_t)parm[4]);
    W32(pi + 0x10, (uint32_t)parm[5]);
    W16(pi + 0x14, (uint32_t)(dpi < 0x709 ? 0 : dpi < 0xE11 ? parm[6] : parm[7]));

    uint32_t a1[2] = { ii, pi };
    int r = guest_call(DDC_b7d8, 2, a1);
    if (r) { snprintf(g_err, sizeof g_err, "Dust Removal setup failed (%d)", r); goto done; }
    uint32_t prog = halloc(12, 1);
    W32(prog + 8, 1);
    uint32_t a2[4] = { ii, pi, PROGRESS_CB, prog };
    r = guest_call(DDC_b946, 4, a2);
    int r2 = guest_call(DDC_ba46, 0, NULL);
    if (r || r2) { snprintf(g_err, sizeof g_err, "Dust Removal failed (%d/%d)", r, r2); goto done; }
    memcpy(img, M + data, size);
    rc = 0;
done:
    if (g_file) { fclose(g_file); g_file = NULL; }
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}
