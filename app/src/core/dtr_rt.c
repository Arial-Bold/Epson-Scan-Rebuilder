/*
 * Runtime for the recompiled Epson DTR2 plug-in (film frame detection, auto locate, margins):
 * guest memory, the library calls it makes, and the entry points in dtr.h.
 * The call sequences follow Epson Scan's UI (CDTR) and Scan Control (CEsExtDTR2).
 */
#include "dtr_rt.h"
#include "dtr.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "dtr_imports.h"

uint8_t *M;
CPU G;
uint16_t dtr_fcw = 0x037F;

#define GUEST_SIZE   0x100000000ull
#define FILE_TEXT    0xA8000u     /* __TEXT + __DATA in the file (same addresses in memory) */
#define IMPORT_VM    0xA9000u     /* __IMPORT: file 0xA8000 -> memory 0xA9000 */
#define IMAGE_END    0xAA000u
#define SCRATCH      0x0F000000u
#define HEAP_BASE    0x10000000u
#define HEAP_END     0x70000000u
#define STACK_TOP    0x7F000000u
#define STUB_BASE    0xF0000000u  /* fake addresses for library functions DTR1 would have given */
#define RET_MAGIC    0xDEADBEEFu

void dtr_dispatch_translated(uint32_t t, int *found);
void DTR_62fe(void);   /* _DTR_Initialize       */
void DTR_6650(void);   /* _DTR_DetectFilmHolder */
void DTR_6756(void);   /* _DTR_DetectImageInfo  */
void DTR_689c(void);   /* _DTR_GetImageLocation */
void DTR_6954(void);   /* _DTR_CalcMargin       */
void DTR_6aa8(void);   /* _DTR_AutoLocate       */

static jmp_buf g_fail;
static int g_failing;
static char g_err[256];
static char g_dir[1024];          /* where ESxxxx.dti lives */

void dtr_fatal(const char *what, uint32_t addr)
{
    snprintf(g_err, sizeof g_err, "Epson DTR stopped: %s at 0x%x", what, addr);
    if (g_failing) longjmp(g_fail, 1);
    abort();
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

/* ---- calls into guest code ---------------------------------------------- */
static void run_at(uint32_t fn)
{
    int found = 0;
    dtr_dispatch_translated(fn, &found);
    if (!found) dtr_fatal("call to unknown address", fn);
}

/* call a guest function by address with 32-bit arguments; registers are preserved around it */
static uint32_t call_guest(uint32_t fn, int nargs, const uint32_t *args)
{
    CPU save = G;
    uint32_t esp = (G.esp ? G.esp - 64 : STACK_TOP - 0x1000);
    esp -= 4u * (uint32_t)nargs;
    esp &= ~0xFu;
    for (int i = 0; i < nargs; i++) W32(esp + 4u * (uint32_t)i, args[i]);
    esp -= 4;
    W32(esp, RET_MAGIC);
    G.esp = esp;
    run_at(fn);
    uint32_t r = G.eax;
    G = save;
    return r;
}

static int32_t entry_call(void (*fn)(void), int nargs, const uint32_t *args)
{
    uint32_t esp = STACK_TOP - 0x1000;
    esp -= 4u * (uint32_t)nargs;
    esp &= ~0xFu;
    for (int i = 0; i < nargs; i++) W32(esp + 4u * (uint32_t)i, args[i]);
    esp -= 4;
    W32(esp, RET_MAGIC);
    G.esp = esp;
    fn();
    return (int32_t)G.eax;
}

/* ---- File Manager: just enough for the .dti file ------------------------- */
typedef struct { FILE *f; } Fork;
static Fork g_forks[16];

static void pstr_c(uint32_t p, char *out, size_t cap)
{
    unsigned n = M[p];
    if (n >= cap) n = (unsigned)cap - 1;
    memcpy(out, M + p + 1, n);
    out[n] = 0;
}

static int16_t fsp_open(uint32_t spec, uint32_t refp)
{
    char name[300], path[1400];
    pstr_c(spec + 6, name, sizeof name);
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    for (int i = 1; i < 16; i++)
        if (!g_forks[i].f) {
            g_forks[i].f = fopen(path, "rb");
            if (getenv("DTR_DEBUG")) fprintf(stderr, "DTR opens %s: %s\n", path, g_forks[i].f ? "ok" : "missing");
            if (!g_forks[i].f) return -43;          /* fnfErr */
            W16(refp, (uint32_t)i);
            return 0;
        }
    return -42;                                      /* tmfoErr */
}

/* ---- CFBundleGetFunctionPointerForName: DTR1's IPC_* are compiled into DTR2 ---- */
typedef struct { const char *name; uint32_t addr; } Sym;
static const Sym kIPC[] = {
#include "dtr_ipc.h"
};

static uint32_t function_for_cfstring(uint32_t cfs)
{
    /* __cfstring: isa, flags, const char *, length */
    uint32_t p = R32(cfs + 8), n = R32(cfs + 12);
    char name[128];
    if (n >= sizeof name) n = sizeof name - 1;
    memcpy(name, M + p, n);
    name[n] = 0;
    for (size_t i = 0; i < sizeof kIPC / sizeof *kIPC; i++)
        if (!strcmp(kIPC[i].name, name)) return kIPC[i].addr;
    /* not in DTR2 (skew / colour-reduction v1): a stub that reports failure */
    return STUB_BASE + (uint32_t)(p & 0xFFFF);
}

/* ---- imports ------------------------------------------------------------ */
static uint32_t ARG(int i) { return R32(G.esp + 4 + 4 * i); }
static double ARGD(int off) { return BD(R64(G.esp + 4 + off)); }

static uint32_t g_qsort_fn;
static int qsort_cmp(const void *a, const void *b);
static uint32_t g_qsort_base, g_qsort_size;

/* functions DTR2 defines itself but calls through its jump table (coalesced C++ helpers) */
static void self_call(uint32_t fn)
{
    run_at(fn);          /* runs with esp at the return address; its ret pops it */
    G.esp -= 4;          /* the caller pops it again after dtr_import */
}

void dtr_import(uint32_t addr)
{
    switch (imp_id(addr)) {
    /* memory */
    case I_free: case I_DisposePtr: case I__ZdlPv: case I__ZdaPv: hfree(ARG(0)); break;
    case I_malloc: case I_NewPtr: case I__Znwm: case I__Znam: G.eax = halloc(ARG(0), 0); break;
    case I_NewPtrClear: G.eax = halloc(ARG(0), 1); break;
    case I_realloc: {
        uint32_t old = ARG(0), n = ARG(1), a = halloc(n, 0), os = hsize(old);
        if (a && old) memmove(M + a, M + old, os < n ? os : n);
        hfree(old);
        G.eax = a;
        break;
    }
    case I_memcpy: case I_memmove: memmove(M + ARG(0), M + ARG(1), ARG(2)); G.eax = ARG(0); break;
    case I_memset: memset(M + ARG(0), (int)(ARG(1) & 0xFF), ARG(2)); G.eax = ARG(0); break;
    case I_qsort: {
        uint32_t base = ARG(0), n = ARG(1), size = ARG(2), fn = ARG(3);
        uint32_t sb = g_qsort_base, ss = g_qsort_size, sf = g_qsort_fn;
        uint8_t *tmp = malloc((size_t)n * size ? (size_t)n * size : 1);
        uint32_t *idx = malloc(sizeof(uint32_t) * (n ? n : 1));
        for (uint32_t i = 0; i < n; i++) idx[i] = i;
        g_qsort_base = base; g_qsort_size = size; g_qsort_fn = fn;
        qsort(idx, n, sizeof *idx, qsort_cmp);   /* compare by index: elements stay put meanwhile */
        for (uint32_t i = 0; i < n; i++) memcpy(tmp + (size_t)i * size, M + base + (size_t)idx[i] * size, size);
        memcpy(M + base, tmp, (size_t)n * size);
        free(tmp); free(idx);
        g_qsort_base = sb; g_qsort_size = ss; g_qsort_fn = sf;
        break;
    }
    /* math */
    case I_pow: FPUSH(pow(ARGD(0), ARGD(8))); break;
    case I_cos: FPUSH(cos(ARGD(0))); break;
    case I_sin: FPUSH(sin(ARGD(0))); break;
    case I_tan: FPUSH(tan(ARGD(0))); break;
    case I_atan: FPUSH(atan(ARGD(0))); break;
    case I_atan2: FPUSH(atan2(ARGD(0), ARGD(8))); break;
    /* C++ runtime */
    case I___cxa_allocate_exception: G.eax = halloc(ARG(0) + 16, 1); break;
    case I___cxa_throw: dtr_fatal("C++ exception", R32(ARG(0))); break;
    case I__Unwind_Resume: dtr_fatal("C++ unwind", 0); break;
    case I___cxa_begin_catch: G.eax = ARG(0); break;
    case I___cxa_end_catch: break;
    case I_abort: dtr_fatal("abort", 0); break;
    case I_fprintf: case I_fflush: G.eax = 0; break;
    /* the coalesced helpers live in DTR2 itself */
    case I__Z20IsCorrectImgLocationllll: self_call(0x980B8u); break;
    case I__Z16IsCorrectImgSizell: self_call(0x98088u); break;
    case I__Z10SetFailurePtS_tPll: self_call(0x9804Eu); break;
    case I__Z15Init_ReturnCodePtS_Pl: self_call(0x9801Au); break;
    case I__Z11EnableLogicPvPtS0_Pl: self_call(0x98116u); break;
    case I__Z12FloatToFix32fP8TW_FIX32: self_call(0x98180u); break;
    case I__Z13Fix32ToDoublePK8TW_FIX32: self_call(0x9822Au); break;
    case I__Z10PixelsToMMll: self_call(0x9827Cu); break;
    /* Pascal strings */
    case I_PLstrcpy: { uint32_t d = ARG(0), s = ARG(1); memmove(M + d, M + s, (size_t)M[s] + 1); G.eax = d; break; }
    case I_PLstrcat: {
        uint32_t d = ARG(0), s = ARG(1);
        unsigned n = M[d], k = M[s];
        if (n + k > 255) k = 255 - n;
        memmove(M + d + 1 + n, M + s + 1, k);
        M[d] = (uint8_t)(n + k);
        G.eax = d;
        break;
    }
    case I_StringToNum: {
        char s[260]; pstr_c(ARG(0), s, sizeof s);
        W32(ARG(1), (uint32_t)atol(s));
        break;
    }
    /* File Manager */
    case I_FSMakeFSSpec: {            /* (vRefNum, dirID, name, spec) */
        uint32_t spec = ARG(3), name = ARG(2);
        W16(spec, ARG(0)); W32(spec + 2, ARG(1));
        if (name) memmove(M + spec + 6, M + name, (size_t)M[name] + 1);
        G.eax = 0;
        break;
    }
    case I_FSpOpenDF: G.eax = (uint32_t)(int32_t)fsp_open(ARG(0), ARG(2)); break;
    case I_FSRead: {                  /* (ref, long *count, buf) */
        int ref = (int16_t)ARG(0);
        uint32_t cp = ARG(1), n = R32(cp);
        if (ref <= 0 || ref >= 16 || !g_forks[ref].f) { G.eax = (uint32_t)-51; break; }
        size_t got = fread(M + ARG(2), 1, n, g_forks[ref].f);
        W32(cp, (uint32_t)got);
        G.eax = got < n ? (uint32_t)-39 : 0;   /* eofErr */
        break;
    }
    case I_FSClose: {
        int ref = (int16_t)ARG(0);
        if (ref > 0 && ref < 16 && g_forks[ref].f) { fclose(g_forks[ref].f); g_forks[ref].f = NULL; }
        G.eax = 0;
        break;
    }
    case I_FSWrite: case I_FSpCreate: case I_FSpDelete: case I_FSGetCatalogInfo: case I_PBGetCatInfoSync:
    case I_GetVRefNum:
        G.eax = (uint32_t)-43; break;
    case I_FindFolder: G.eax = (uint32_t)-43; break;
    case I_Gestalt: W32(ARG(1), 0x1050); G.eax = 0; break;
    /* Resource Manager: no resources (debug switches and the reflective TPU table stay off) */
    case I_CurResFile: G.eax = 0; break;
    case I_UseResFile: case I_ReleaseResource: case I_HLock: case I_HUnlock: case I_CloseResFile: break;
    case I_GetResource: case I_Get1NamedResource: G.eax = 0; break;
    case I_HOpenResFile: G.eax = 0xFFFFFFFFu; break;
    case I_ResError: G.eax = (uint32_t)-192; break;
    /* Core Foundation (bundle lookups are answered by the hooks below) */
    case I_CFBundleGetFunctionPointerForName: G.eax = function_for_cfstring(ARG(1)); break;
    case I_CFRelease: case I_CFBundleCloseBundleResourceMap: break;
    case I_CFRetain: G.eax = ARG(0); break;
    case I_CFStringGetSystemEncoding: G.eax = 0; break;
    case I_CFStringGetPascalString: G.eax = 0; break;
    case I_CFBundleOpenBundleResourceMap: G.eax = 0xFFFFFFFFu; break;
    case I_CFBundleGetBundleWithIdentifier: case I_CFURLCreateFromFSRef: case I_CFStringCreateWithPascalString:
    case I_CFBundleCopyBuiltInPlugInsURL: case I_CFURLGetFSRef: case I_CFBundleCopyPrivateFrameworksURL:
    case I_CFURLCreateWithFileSystemPath: case I_CFBundleCopySharedSupportURL: case I_CFBundleCopyBundleURL:
    case I_CFURLCreateWithFileSystemPathRelativeToBase: case I_CFBundleCopyResourcesDirectoryURL:
    case I_CFBundleCreate: case I_CFBundleCopyExecutableURL: case I_CFStringCreateWithFormat:
        G.eax = 0; break;
    default: dtr_fatal("unsupported library call", addr);
    }
}

static int qsort_cmp(const void *a, const void *b)
{
    uint32_t args[2] = { g_qsort_base + *(const uint32_t *)a * g_qsort_size,
                         g_qsort_base + *(const uint32_t *)b * g_qsort_size };
    return (int32_t)call_guest(g_qsort_fn, 2, args);
}

void dtr_dispatch(uint32_t t)
{
    if (t >= STUB_BASE) {           /* a DTR1 function DTR2 doesn't have: fail politely */
        G.eax = 5;
        G.esp += 4;
        return;
    }
    int found = 0;
    dtr_dispatch_translated(t, &found);
    if (!found) dtr_fatal("indirect call to unknown address", t);
}

/* ---- hooks: functions replaced because they only find files through Carbon ---- */

/* CDTRData::GetHrdFileSpec(this, id, FSSpec *): "ES" + 4 hex digits + ".dti" next to the plug-in */
void DTR_4750(void)
{
    uint32_t id = ARG(1), spec = ARG(2);
    char name[32];
    snprintf(name, sizeof name, "ES%04X.dti", (unsigned)(id & 0xFFFF));
    memset(M + spec, 0, 70);
    M[spec + 6] = (uint8_t)strlen(name);
    memcpy(M + spec + 7, name, strlen(name));
    G.eax = 1;
    G.esp += 4;
}

/* CreateParentBundleFromPlugInBundle(CFBundle **mine, CFBundle **parent): a dummy parent */
void DTR_37d0(void)
{
    W32(ARG(1), 2);
    G.eax = 0;
    G.esp += 4;
}

/* LoadCFBundleRelativeToParent(name, id, parent, bool *loaded, bool *owned, CFBundle **out):
 * "DTR.bundle" is DTR2 itself here, its IPC_* functions answer by name */
void DTR_3ade(void)
{
    W8(ARG(3), 1);
    W8(ARG(4), 0);
    W32(ARG(5), 3);
    G.eax = 0;
    G.esp += 4;
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
    if (n < FILE_TEXT || n2 < 0x1000) { snprintf(g_err, sizeof g_err, "DTR file is too short"); return -1; }
    /* data imports: DTR2's own objects where it defines them, scratch cells otherwise */
    static const uint32_t own[][2] = {
        { 0xA919A, 0xA7B80 }, { 0xA91A2, 0x99008 }, { 0xA91A6, 0x9900C }, { 0xA91AE, 0x99148 },
        { 0xA91B2, 0x99480 }, { 0xA91B6, 0xA6760 }, { 0xA91BA, 0xA67E0 }, { 0xA91BE, 0xA6860 },
        { 0xA91C2, 0xA7BC0 },
    };
    for (uint32_t a = 0xA919A; a < 0xA91CE; a += 4) W32(a, SCRATCH + 0x400u + (a - 0xA919A) * 16);
    for (size_t i = 0; i < sizeof own / sizeof *own; i++) W32(own[i][0], own[i][1]);
    W32(0x99008, 1);          /* g_MyBundle: "loaded from a bundle" */
    memset(&G, 0, sizeof G);
    dtr_fcw = 0x037F;
    for (int i = 0; i < 16; i++) if (g_forks[i].f) { fclose(g_forks[i].f); g_forks[i].f = NULL; }
    return 0;
}

/* ---- entry points --------------------------------------------------------- */

int dtr_detect(const char *engine_path, const char *dti_dir, const DtrImage *img, DtrFrame *frames, int max,
               int *count, char *err, size_t errlen)
{
    g_err[0] = 0;
    *count = 0;
    int rc = -1;
    snprintf(g_dir, sizeof g_dir, "%s", dti_dir);
    g_failing = 1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }

    /* the preview: 32-bit xRGB, gamma 1.8 (FIT3_BmpInfo, as CDTR::InitDetectImgInfo passes it) */
    uint32_t rb = (uint32_t)img->width * 4, data = halloc(rb * (uint32_t)img->height, 0);
    if (!data) { snprintf(g_err, sizeof g_err, "not enough memory for the preview"); goto done; }
    memcpy(M + data, img->xrgb, (size_t)rb * img->height);

    /* TWEP_DETECTIMAGEINFO */
    uint32_t info = halloc(0x40, 1);
    W32(info + 0x00, data);
    W16(info + 0x04, 2);                         /* 32 bits per pixel */
    W32(info + 0x08, (uint32_t)img->width);
    W32(info + 0x0c, (uint32_t)img->height);
    W32(info + 0x10, rb);
    W32(info + 0x14, (uint32_t)img->dpi);
    W32(info + 0x18, (uint32_t)img->model_id);
    W16(info + 0x1c, (uint32_t)img->connected_unit);
    W16(info + 0x1e, (uint32_t)img->scanned_unit);
    W16(info + 0x20, (uint32_t)img->doc_source);
    W16(info + 0x22, (uint32_t)img->holder);
    W16(info + 0x24, img->film ? 1u : 0u);
    W16(info + 0x26, (uint32_t)img->film_size);
    W16(info + 0x28, 6);
    W16(info + 0x2e, (uint32_t)(int32_t)img->film_type);
    uint32_t matrix = halloc(72, 1);
    for (int i = 0; i < 9; i++) W64(matrix + 8u * (uint32_t)i, DB(img->red_matrix[i]));
    uint32_t cond = halloc(16, 1), cnt = cond + 4;
    uint32_t a1[4] = { cond, info, matrix, cnt };
    int r = entry_call(DTR_6756, 4, a1) & 0xFFFF;
    int32_t n = (int32_t)R32(cnt);
    if (getenv("DTR_DEBUG"))
        fprintf(stderr, "DTR_DetectImageInfo: return %d condition %u status %d count %d holder %u film type %d\n", r,
                R16(cond), (int32_t)R32(info + 0x34), n, R16(info + 0x22), (int16_t)R16(info + 0x2e));
    if (r || n < 0) {
        snprintf(g_err, sizeof g_err, "frame detection failed (return %d, condition %u, status %d)", r, R16(cond),
                 (int32_t)R32(info + 0x34));
        goto done;
    }
    if (R32(info + 0x34) == 2 || n == 0) { rc = 0; goto done; }   /* no images */
    uint32_t det = halloc((uint32_t)n * 0x34, 1);
    uint32_t a2[3] = { cond, (uint32_t)n, det };
    r = entry_call(DTR_689c, 3, a2) & 0xFFFF;
    if (r) { snprintf(g_err, sizeof g_err, "frame locations failed (%d, condition %u)", r, R16(cond)); goto done; }
    uint16_t holder = (uint16_t)R16(info + 0x22);
    int16_t tft = (int16_t)R16(info + 0x2e);
    for (int i = 0; i < n && i < max; i++) {
        uint32_t d = det + 0x34u * (uint32_t)i;
        /* Configuration > Thumbnail Cropping Area (CDTR::CalcurateActualMargin): DTR_CalcMargin with
         * the margin size ID moves the corners in by the holder's margin table */
        if (img->margin_id > 0) {
            uint32_t cm = halloc(0x70, 1);
            W32(cm + 0x00, (uint32_t)img->model_id);
            W16(cm + 0x04, (uint32_t)img->scanned_unit);
            W16(cm + 0x06, holder);
            W16(cm + 0x08, img->film ? 1u : 0u);
            W16(cm + 0x0a, (uint32_t)(int32_t)tft);
            W32(cm + 0x0c, (uint32_t)img->width);
            W32(cm + 0x10, (uint32_t)img->height);
            W32(cm + 0x14, (uint32_t)img->dpi);
            W16(cm + 0x18, (uint32_t)img->margin_id);
            memcpy(M + cm + 0x3c, M + d, 40);
            uint32_t a3[4] = { cond, cm, 0, 0 };
            int rm = entry_call(DTR_6954, 4, a3) & 0xFFFF;
            if (getenv("DTR_DEBUG")) fprintf(stderr, "DTR_CalcMargin frame %d: return %d status %d\n", i, rm, (int32_t)R32(cm + 0x64));
            if (!rm) memcpy(M + d, M + cm + 0x3c, 40);
        }
        DtrFrame *fr = &frames[i];
        for (int k = 0; k < 8; k++) fr->pt[k] = (int32_t)R32(d + 4 + 4u * (uint32_t)k);
        fr->angle = (int16_t)R16(d + 0x24) + R16(d + 0x26) / 65536.0;   /* TW_FIX32: Whole, Frac */
        fr->info = R32(d + 0x28);
        fr->status = (int32_t)R32(d + 0x2c);
        fr->holder = holder;
        fr->film_type = tft;
    }
    *count = n < max ? n : max;
    rc = 0;
done:
    g_failing = 0;
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

/* Auto Locate (CDTR::DoAutoLocation -> DTR_AutoLocate): the area of the original inside
 * `area` (preview pixels); 0 and *out filled, 1 when nothing was found, negative on error */
int dtr_autolocate(const char *engine_path, const char *dti_dir, const DtrImage *img, const int area[4], int out[4],
                   char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    snprintf(g_dir, sizeof g_dir, "%s", dti_dir);
    g_failing = 1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t rb = (uint32_t)img->width * 4, data = halloc(rb * (uint32_t)img->height, 0);
    if (!data) { snprintf(g_err, sizeof g_err, "not enough memory for the preview"); goto done; }
    memcpy(M + data, img->xrgb, (size_t)rb * img->height);
    /* TWEP_AUTOLOCATE */
    uint32_t al = halloc(0x60, 1);
    W32(al + 0x00, data);
    W16(al + 0x04, 2);
    W32(al + 0x08, (uint32_t)img->width);
    W32(al + 0x0c, (uint32_t)img->height);
    W32(al + 0x10, rb);
    W32(al + 0x14, (uint32_t)img->dpi);
    W32(al + 0x18, 0);
    W8(al + 0x1c, (img->width >= area[2] && img->height >= area[3]) ? 1 : 0);
    W8(al + 0x1d, 0);                                     /* auto layout off */
    W32(al + 0x20, (uint32_t)area[2]); W32(al + 0x24, (uint32_t)area[3]);   /* right, bottom */
    W32(al + 0x28, (uint32_t)area[0]); W32(al + 0x2c, (uint32_t)area[1]);   /* left, top */
    W32(al + 0x30, (uint32_t)area[0]); W32(al + 0x34, (uint32_t)area[1]);
    W32(al + 0x38, (uint32_t)area[2]); W32(al + 0x3c, (uint32_t)area[3]);
    W32(al + 0x40, (uint32_t)img->model_id);
    W16(al + 0x44, (uint32_t)img->connected_unit);
    uint32_t cond = halloc(16, 1);
    uint32_t a1[2] = { cond, al };
    int r = entry_call(DTR_6aa8, 2, a1) & 0xFFFF;
    int32_t st = (int32_t)R32(al + 0x58);
    if (getenv("DTR_DEBUG"))
        fprintf(stderr, "DTR_AutoLocate: return %d condition %u status %d -> %d %d %d %d\n", r, R16(cond), st,
                (int32_t)R32(al + 0x48), (int32_t)R32(al + 0x4c), (int32_t)R32(al + 0x50), (int32_t)R32(al + 0x54));
    if (r) { snprintf(g_err, sizeof g_err, "auto locate failed (%d, condition %u)", r, R16(cond)); goto done; }
    for (int k = 0; k < 4; k++) out[k] = (int32_t)R32(al + 0x48 + 4u * (uint32_t)k);
    rc = st == 0 ? 0 : 1;
done:
    g_failing = 0;
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

/* Film holder detection (CDTR::CheckDocSource -> CScanInf::GetDetectFilmHolder -> Scan Control ->
 * DTR_DetectFilmHolder): Full Auto looks at the glass preview and DTR2 says whether a film holder
 * is lying on the glass. TWEP_DETECTFILMHOLDER: +0 image, +4 2 (32 bpp), +8 width, +0xc height,
 * +0x10 row bytes, +0x14 resolution, +0x18 model ID, +0x1c unit type, +0x1e result, +0x20 status.
 * Returns 1 for a holder, 0 for none, negative on error. */
int dtr_detect_holder(const char *engine_path, const char *dti_dir, const DtrImage *img, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    snprintf(g_dir, sizeof g_dir, "%s", dti_dir);
    g_failing = 1;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    uint32_t rb = (uint32_t)img->width * 4, data = halloc(rb * (uint32_t)img->height, 0);
    if (!data) { snprintf(g_err, sizeof g_err, "not enough memory for the preview"); goto done; }
    memcpy(M + data, img->xrgb, (size_t)rb * img->height);
    uint32_t fh = halloc(0x40, 1);
    W32(fh + 0x00, data);
    W16(fh + 0x04, 2);
    W32(fh + 0x08, (uint32_t)img->width);
    W32(fh + 0x0c, (uint32_t)img->height);
    W32(fh + 0x10, rb);
    W32(fh + 0x14, (uint32_t)img->dpi);
    W32(fh + 0x18, (uint32_t)img->model_id);
    W16(fh + 0x1c, (uint32_t)img->connected_unit);
    uint32_t cond = halloc(16, 1);
    uint32_t a1[2] = { cond, fh };
    int r = entry_call(DTR_6650, 2, a1) & 0xFFFF;
    int result = R16(fh + 0x1e) & 0xFF;
    if (getenv("DTR_DEBUG"))
        fprintf(stderr, "DTR_DetectFilmHolder: return %d condition %u status %d result %d\n", r, R16(cond),
                (int32_t)R32(fh + 0x20), result);
    if (r) { snprintf(g_err, sizeof g_err, "film holder detection failed (%d, condition %u)", r, R16(cond)); goto done; }
    rc = result ? 1 : 0;
done:
    g_failing = 0;
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}
