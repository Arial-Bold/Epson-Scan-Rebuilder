/*
 * Runtime for the recompiled Digital ICE engine: guest memory, the libc
 * functions the engine imports, and the public entry point ice_film().
 */
#include "ice_rt.h"
#include "ice_film.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

uint8_t *M;
CPU G;

#define GUEST_SIZE   0x100000000ull
#define IMAGE_END    0x2C000u
#define SCRATCH      0x0F000000u
#define HEAP_BASE    0x10000000u
#define HEAP_END     0x70000000u
#define STACK_TOP    0x7F000000u
#define PROGRESS_CB  (SCRATCH + 0x500u)   /* fake guest address of the progress callback */

/* Entry points (addresses in the original engine) */
void F_1c1c(void);  /* _DICEBegin   */
void F_1eae(void);  /* _DICEProcess */
void F_2052(void);  /* _DICEEnd     */
void ice_dispatch_translated(uint32_t t, int *found);

static jmp_buf g_fail;
static char g_err[256];
static ice_progress_fn g_progress;
static void *g_progress_ctx;

void ice_fatal(const char *what, uint32_t addr)
{
    snprintf(g_err, sizeof g_err, "ICE engine stopped: %s at 0x%x", what, addr);
    longjmp(g_fail, 1);
}

/* ---- guest heap: bump allocator with size-bucketed reuse ---------------- */

#define NBUCKET 4096
typedef struct Free { uint32_t addr; struct Free *next; } Free;
static uint32_t g_heap_top;
static struct { uint32_t size; Free *list; } g_buckets[NBUCKET];
static uint32_t *g_sizes;          /* size of the block at (addr - HEAP_BASE) / 16 */

static uint32_t halloc(uint32_t n, int zero)
{
    n = n < 16 ? 16 : (n + 15) & ~15u;
    unsigned b = (n >> 4) % NBUCKET;
    for (Free **pp = &g_buckets[b].list; *pp; pp = &(*pp)->next) {
        uint32_t a = (*pp)->addr;
        if (g_sizes[(a - HEAP_BASE) >> 4] == n) {
            Free *f = *pp;
            *pp = f->next;
            free(f);
            if (zero) memset(M + a, 0, n);
            return a;
        }
    }
    if ((uint64_t)g_heap_top + n > HEAP_END) return 0;
    uint32_t a = g_heap_top;
    g_heap_top += n;
    g_sizes[(a - HEAP_BASE) >> 4] = n;
    return a;   /* fresh pages are already zero */
}

static void hfree(uint32_t a)
{
    if (a < HEAP_BASE || a >= g_heap_top) return;
    uint32_t n = g_sizes[(a - HEAP_BASE) >> 4];
    if (!n) return;
    Free *f = malloc(sizeof *f);
    f->addr = a;
    unsigned b = (n >> 4) % NBUCKET;
    f->next = g_buckets[b].list;
    g_buckets[b].list = f;
}

/* ---- files -------------------------------------------------------------- */

#define MAXF 16
static FILE *g_files[MAXF];
static uint32_t file_handle(int i) { return SCRATCH + 0x1000u + 16u * (uint32_t)i; }
static FILE *file_of(uint32_t h)
{
    unsigned i = (h - (SCRATCH + 0x1000u)) / 16u;
    return i < MAXF ? g_files[i] : NULL;
}

/* ---- imports ------------------------------------------------------------ */

static uint32_t ARG(int i) { return R32(G.esp + 4 + 4 * i); }
static double ARGD(int off) { return BD(R64(G.esp + 4 + off)); }
static float ARGF(int off) { return BF(R32(G.esp + 4 + off)); }

void ice_import(uint32_t addr)
{
    switch (addr) {
    case 0x2B00A: case 0x2B064: case 0x2B07D:                  /* free, delete */
        hfree(ARG(0)); break;
    case 0x2B00F: case 0x2B0A0: case 0x2B06E:                  /* malloc, new */
        G.eax = halloc(ARG(0), 0); break;
    case 0x2B023:                                              /* calloc */
        G.eax = halloc(ARG(0) * ARG(1), 1); break;
    case 0x2B037:                                              /* memcpy */
        memmove(M + ARG(0), M + ARG(1), ARG(2)); G.eax = ARG(0); break;
    case 0x2B055:                                              /* memset */
        memset(M + ARG(0), (int)(ARG(1) & 0xFF), ARG(2)); G.eax = ARG(0); break;
    case 0x2B05A: FPUSH(log10(ARGD(0))); break;                /* log10 */
    case 0x2B05F: FPUSH((double)log10f(ARGF(0))); break;       /* log10f */
    case 0x2B069: FPUSH(pow(ARGD(0), ARGD(8))); break;         /* pow */
    case 0x2B096: FPUSH((double)floorf(ARGF(0))); break;       /* floorf */
    case 0x2B032: {                                            /* fopen */
        const char *path = (const char *)M + ARG(0), *mode = (const char *)M + ARG(1);
        G.eax = 0;
        for (int i = 0; i < MAXF; i++)
            if (!g_files[i]) {
                g_files[i] = fopen(path, mode);
                if (g_files[i]) G.eax = file_handle(i);
                break;
            }
        break;
    }
    case 0x2B04B: {                                            /* fclose */
        FILE *f = file_of(ARG(0));
        if (f) {
            fclose(f);
            g_files[(ARG(0) - (SCRATCH + 0x1000u)) / 16u] = NULL;
        }
        G.eax = 0;
        break;
    }
    case 0x2B073: {                                            /* fread */
        FILE *f = file_of(ARG(3));
        G.eax = f ? (uint32_t)fread(M + ARG(0), ARG(1), ARG(2), f) : 0;
        break;
    }
    case 0x2B078: {                                            /* fwrite */
        FILE *f = file_of(ARG(3));
        G.eax = f ? (uint32_t)fwrite(M + ARG(0), ARG(1), ARG(2), f) : 0;
        break;
    }
    case 0x2B087: {                                            /* fseek */
        FILE *f = file_of(ARG(0));
        G.eax = f ? (uint32_t)fseek(f, (long)(int32_t)ARG(1), (int)ARG(2)) : (uint32_t)-1;
        break;
    }
    case 0x2B082: G.eax = 0; break;                            /* ferror */
    case 0x2B050: G.eax = (uint32_t)(-5551 & 0xFFFF); break;   /* Gestalt: no AltiVec */
    case 0x2B09B: case 0x2B08C: case 0x2B091: G.eax = 0; break; /* puts, putchar, printf */
    case 0x2B02D: G.eax = halloc(ARG(0) + 64, 1); break;       /* __cxa_allocate_exception */
    case 0x2B046:                                              /* __cxa_throw */
        snprintf(g_err, sizeof g_err, "ICE engine reported error %d", (int32_t)R32(ARG(0)));
        longjmp(g_fail, 1);
    default:
        ice_fatal("unsupported library call", addr);
    }
}

void ice_dispatch(uint32_t t)
{
    if (t == PROGRESS_CB) {
        int pct = (int32_t)ARG(0);
        int stop = g_progress ? g_progress(pct, g_progress_ctx) : 0;
        G.eax = stop ? 1u : 0u;
        G.esp += 4;                       /* pop the return address, like 'ret' */
        return;
    }
    int found = 0;
    ice_dispatch_translated(t, &found);
    if (!found) ice_fatal("indirect call to unknown address", t);
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
        g_sizes = calloc((HEAP_END - HEAP_BASE) >> 4, sizeof *g_sizes);
    } else {
        memset(M, 0, IMAGE_END);
        memset(M + HEAP_BASE, 0, g_heap_top - HEAP_BASE);
        memset(g_sizes, 0, ((g_heap_top - HEAP_BASE) >> 4) * sizeof *g_sizes);
    }
    for (int b = 0; b < NBUCKET; b++) {
        Free *f = g_buckets[b].list;
        while (f) { Free *n = f->next; free(f); f = n; }
        g_buckets[b].list = NULL;
    }
    g_heap_top = HEAP_BASE;

    FILE *f = fopen(engine_path, "rb");
    if (!f) {
        snprintf(g_err, sizeof g_err, "can't open ICE engine file %s", engine_path);
        return -1;
    }
    size_t n = fread(M, 1, IMAGE_END, f);   /* segment file offsets == addresses */
    fclose(f);
    if (n < 0x2B0A5) {
        snprintf(g_err, sizeof g_err, "ICE engine file is too short");
        return -1;
    }
    /* data imports (type info, nothrow tag, personality) -> harmless scratch */
    static const uint32_t data_imports[] = { 0x2B0B5, 0x2B0C1, 0x2B121 };
    for (unsigned i = 0; i < 3; i++) W32(data_imports[i], SCRATCH + 0x400u);
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
    W32(esp, 0xDEADBEEFu);     /* return address; never jumped to */
    G.esp = esp;
    fn();
    return (int32_t)G.eax;
}

static uint32_t guest_str(const char *s)
{
    size_t n = strlen(s) + 1;
    uint32_t a = halloc((uint32_t)n, 0);
    memcpy(M + a, s, n);
    return a;
}

int ice_film(const char *engine_path, const char *rgb_path, const char *ir_path, const char *out_path,
             int width, int height, int dpi, int film_type, int level,
             ice_progress_fn progress, void *ctx, char *err, size_t errlen)
{
    g_err[0] = 0;
    g_progress = progress;
    g_progress_ctx = ctx;
    int rc;
    if (setjmp(g_fail)) {
        for (int i = 0; i < MAXF; i++) if (g_files[i]) { fclose(g_files[i]); g_files[i] = NULL; }
        rc = -100;
        goto done;
    }
    if (guest_init(engine_path)) { rc = -101; goto done; }

    /* DICEInfo: size, height, width, dpi, bits, mode(5 = film), film type, level, channels */
    int32_t info[9] = { 0x24, height, width, dpi, 16, 5, film_type, level, 3 };
    uint32_t pinfo = halloc(sizeof info, 0);
    memcpy(M + pinfo, info, sizeof info);

    uint32_t a1[2] = { pinfo, PROGRESS_CB };
    rc = guest_call(F_1c1c, 2, a1);
    if (rc != 0) {
        snprintf(g_err, sizeof g_err, "ICE rejected the settings (error %d)", rc);
        goto done;
    }
    uint32_t a2[4] = { guest_str(out_path), guest_str(rgb_path), guest_str(ir_path), 0 };
    rc = guest_call(F_1eae, 4, a2);
    guest_call(F_2052, 0, NULL);
    if (rc != 0) snprintf(g_err, sizeof g_err, "ICE processing failed (error %d)", rc);
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}
