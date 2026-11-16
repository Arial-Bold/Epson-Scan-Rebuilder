/*
 * Runtime for the recompiled PDFLib high-compression code: guest memory, PDFLib's allocator
 * (g_mem), the library calls, and hc_segment. The call sequence follows
 * CCreatePDFImpl::ConvertPDF: EPHighCompression::Init, DoCom(data, &component, 0, comp, 0),
 * Dispose.
 */
#include "hc_rt.h"
#include "hc.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "hc_imports.h"

uint8_t *M;
CPU G;
uint16_t hc_fcw = 0x037F;

#define GUEST_SIZE   0x100000000ull
#define FILE_TEXT    0x8A000u     /* __TEXT + __DATA in the file */
#define IMPORT_VM    0x9D000u     /* __IMPORT: file 0x8A000 -> memory 0x9D000 */
#define IMAGE_END    0x9E000u
#define SCRATCH      0x0F000000u
#define HEAP_BASE    0x10000000u
#define HEAP_END     0x70000000u
#define STACK_TOP    0x7F000000u
#define STUB_ALLOC   0xF0000010u  /* g_mem's virtual functions */
#define STUB_FREE    0xF0000020u
#define STUB_NOP     0xF0000030u

void hc_dispatch_translated(uint32_t t, int *found);
void HC_58a2c(void);   /* EPHighCompression::Init    */
void HC_59310(void);   /* EPHighCompression::DoCom   */
void HC_58a70(void);   /* EPHighCompression::Dispose */

static jmp_buf g_fail;
static char g_err[256];

void hc_fatal(const char *what, uint32_t addr)
{
    snprintf(g_err, sizeof g_err, "High Compression stopped: %s at 0x%x", what, addr);
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

static void run_at(uint32_t fn)
{
    int found = 0;
    hc_dispatch_translated(fn, &found);
    if (!found) hc_fatal("call to unknown address", fn);
}

void hc_import(uint32_t addr)
{
    switch (imp_id(addr)) {
    case I_malloc: G.eax = halloc(ARG(0), 0); break;
    case I_free: hfree(ARG(0)); break;
    case I_memcpy: memmove(M + ARG(0), M + ARG(1), ARG(2)); G.eax = ARG(0); break;
    case I_memset: memset(M + ARG(0), (int)(ARG(1) & 0xFF), ARG(2)); G.eax = ARG(0); break;
    case I_puts: G.eax = 0; break;
    /* Anoop::CCube<unsigned char>: PDFLib's own (coalesced) code, called through the jump table */
    case I_ZNK5Anoop5CCubeIhE12SixTetLookupEPKhPh: run_at(0x60A08u); G.esp -= 4; break;
    case I_ZN5Anoop5CCubeIhE5SetUpEi: run_at(0x60CA8u); G.esp -= 4; break;
    default: hc_fatal("unsupported library call", addr);
    }
}

void hc_dispatch(uint32_t t)
{
    if (t == STUB_ALLOC) { G.eax = halloc(ARG(1), 0); G.esp += 4; return; }   /* CPDFMemory::alloc(this, n) */
    if (t == STUB_FREE) { hfree(ARG(1)); G.esp += 4; return; }                /* CPDFMemory::free(this, p)  */
    if (t == STUB_NOP) { G.eax = 0; G.esp += 4; return; }
    int found = 0;
    hc_dispatch_translated(t, &found);
    if (!found) hc_fatal("indirect call to unknown address", t);
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
    size_t n = fread(M, 1, FILE_TEXT, f);
    size_t n2 = fread(M + IMPORT_VM, 1, 0x1000, f);
    fclose(f);
    if (n < FILE_TEXT || n2 < 0x1000) { snprintf(g_err, sizeof g_err, "PDFLib file is too short"); return -1; }
    for (size_t i = 0; i < sizeof kDataImp / sizeof *kDataImp; i++)
        W32(kDataImp[i][0], kDataImp[i][1] ? kDataImp[i][1] : SCRATCH + 0x400u + 0x40u * (uint32_t)i);
    /* g_mem: PDFLib's allocator object (CPDFMemoryImpl), made at load time in the original */
    uint32_t vt = SCRATCH + 0x8000u, obj = SCRATCH + 0x8100u;
    for (int i = 0; i < 16; i++) W32(vt + 4u * (uint32_t)i, STUB_NOP);
    W32(vt + 8, STUB_ALLOC); W32(vt + 0x14, STUB_FREE);
    W32(obj, vt);
    W32(0x9C2E8u, obj);
    memset(&G, 0, sizeof G);
    hc_fcw = 0x037F;
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

void hc_free(HcLayers *l)
{
    free(l->background); free(l->foreground); free(l->mask);
    memset(l, 0, sizeof *l);
}

int hc_segment(const char *engine_path, const uint8_t *pixels, int w, int h, int spp, HcLayers *out,
               char *err, size_t errlen)
{
    g_err[0] = 0;
    memset(out, 0, sizeof *out);
    int rc = -1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    size_t size = (size_t)w * h * spp;
    uint32_t img = halloc((uint32_t)size, 0);
    if (!img) { snprintf(g_err, sizeof g_err, "not enough memory for High Compression"); goto done; }
    memcpy(M + img, pixels, size);
    /* _t_PDFCreateData: type (0x404 RGB / 0x403 gray), data, size, width, height, ..., bits */
    uint32_t d = halloc(0x50, 1);
    W32(d, spp == 3 ? 0x404u : 0x403u); W32(d + 4, img); W32(d + 8, (uint32_t)size);
    W32(d + 0xc, (uint32_t)w); W32(d + 0x10, (uint32_t)h); W16(d + 0x34, (uint32_t)(spp * 8));
    /* _t_PDFCreateComp: CCreatePDFImpl::SetDefaultCompParams */
    static const uint8_t comp[0x1c] = { 3, 0, 0, 0, 2, 0, 0, 0, 0x4c, 0xc8, 0x70, 0x80, 0x03, 0x87, 0x1e, 0x32,
                                        0x04, 0x17, 0x80, 0x40, 0, 0, 0, 1, 1, 0, 0, 0 };
    uint32_t cp = halloc(0x1c, 1);
    memcpy(M + cp, comp, sizeof comp);
    uint32_t hc = halloc(0x400, 1), dc = halloc(8, 1);
    uint32_t a0[1] = { hc };
    guest_call(HC_58a2c, 1, a0);
    uint32_t a1[6] = { hc, d, dc, 0, cp, 0 };
    int r = guest_call(HC_59310, 6, a1);
    if (r || R32(dc) != 3) { snprintf(g_err, sizeof g_err, "High Compression segmentation failed (%d)", r); goto dispose; }
    uint32_t e = R32(dc + 4);
    out->w = w; out->h = h;
    size_t rgb = (size_t)w * h * 3, mb = (size_t)(w + 7) / 8 * h;
    out->background = malloc(rgb); out->foreground = malloc(rgb); out->mask = malloc(mb);
    if (!out->background || !out->foreground || !out->mask) { snprintf(g_err, sizeof g_err, "out of memory"); hc_free(out); goto dispose; }
    /* layer 0: background (0x404), 1: mask (0x401), 2: foreground (0x404, masked by layer 1) */
    memcpy(out->background, M + R32(e + 4), rgb);
    memcpy(out->mask, M + R32(e + 0x50 + 4), mb);
    memcpy(out->foreground, M + R32(e + 0xa0 + 4), rgb);
    out->bg_comp = (int)R32(e + 0x4c); out->mask_comp = (int)R32(e + 0x50 + 0x4c); out->fg_comp = (int)R32(e + 0xa0 + 0x4c);
    rc = 0;
dispose:
    guest_call(HC_58a70, 1, a0);
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}
