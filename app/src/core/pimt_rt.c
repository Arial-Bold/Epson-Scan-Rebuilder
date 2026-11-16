/*
 * Runtime for the recompiled Epson PIM TIFF Support plug-in (PRINT Image Matching II): guest memory,
 * the library and File Manager calls it makes (files are real paths here), and the entry
 * points in pim.h. The call sequence follows the PIM plug-in's CEsExfIF::EsExf_Entry.
 */
#include "pt_rt.h"
#include "pim.h"

#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "pimt_imports.h"

uint8_t *M;
CPU G;
uint16_t pt_fcw = 0x037F;

#define GUEST_SIZE   0x100000000ull
#define IMAGE_END    0x12000u
#define SCRATCH      0x0F000000u
#define HEAP_BASE    0x10000000u
#define HEAP_END     0x70000000u
#define STACK_TOP    0x7F000000u

void pt_dispatch_translated(uint32_t t, int *found);
void PT_c58(void);
void PT_e8c(void);
void PT_fd2(void);
void PT_10a2(void);
void PT_1160(void);
void PT_d20(void);

static jmp_buf g_fail;
static char g_err[256];

void pt_fatal(const char *what, uint32_t addr)
{
    snprintf(g_err, sizeof g_err, "PRINT Image Matching stopped: %s at 0x%x", what, addr);
    longjmp(g_fail, 1);
}

/* ---- guest heap ---------------------------------------------------------- */
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
static const char *GS(uint32_t a) { return (const char *)(M + a); }

/* ---- files: FSSpec = { vRefNum, parID (a directory in g_dirs), Pascal name } --------- */
#define NDIR 64
static char *g_dirs[NDIR];
static int g_ndirs;

static int dir_id(const char *path)
{
    for (int i = 0; i < g_ndirs; i++) if (!strcmp(g_dirs[i], path)) return 1000 + i;
    if (g_ndirs == NDIR) pt_fatal("too many folders", 0);
    g_dirs[g_ndirs] = strdup(path);
    return 1000 + g_ndirs++;
}
static const char *dir_path(uint32_t id)
{
    return id >= 1000 && id < 1000u + (uint32_t)g_ndirs ? g_dirs[id - 1000] : "/tmp";
}
static void join(char *out, size_t n, uint32_t dir, const uint8_t *pname)
{
    char name[256];
    memcpy(name, pname + 1, pname[0]); name[pname[0]] = 0;
    for (char *c = name; *c; c++) if (*c == ':') *c = '/';
    if (name[0]) snprintf(out, n, "%s/%s", dir_path(dir), name);
    else snprintf(out, n, "%s", dir_path(dir));
}
static void spec_path(uint32_t spec, char *out, size_t n) { join(out, n, R32(spec + 2), M + spec + 6); }
static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }

/* a path -> FSSpec (parent folder + name) */
static void make_spec(uint32_t spec, const char *path)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    const char *name = slash ? slash + 1 : dir;
    if (slash == dir) dir[1] = 0; else if (slash) *slash = 0;
    size_t len = strlen(name); if (len > 63) len = 63;
    W16(spec, (uint32_t)-1);
    W32(spec + 2, (uint32_t)dir_id(slash ? dir : "."));
    M[spec + 6] = (uint8_t)len; memcpy(M + spec + 7, name, len);
}

#define NFILE 32
static FILE *g_files[NFILE];
static int fs_open(const char *path, const char *mode)
{
    for (int i = 1; i < NFILE; i++)
        if (!g_files[i]) { g_files[i] = fopen(path, mode); return g_files[i] ? i : 0; }
    return 0;
}
static FILE *fs_file(uint32_t i) { return i > 0 && i < NFILE ? g_files[i] : NULL; }
static void fs_closeall(void) { for (int i = 1; i < NFILE; i++) if (g_files[i]) { fclose(g_files[i]); g_files[i] = NULL; } }
/* guest FILE * values are 0x100 + index into g_files */
static FILE *gfile(uint32_t a) { return a >= 0x100 && a < 0x100 + NFILE ? g_files[a - 0x100] : NULL; }
static uint32_t gfile_add(FILE *f)
{
    if (!f) return 0;
    for (int i = 1; i < NFILE; i++) if (!g_files[i]) { g_files[i] = f; return 0x100u + (uint32_t)i; }
    fclose(f);
    return 0;
}

/* sprintf with the guest's arguments */
static void gformat(char *out, size_t n, const char *fmt, uint32_t ap)
{
    size_t o = 0;
    for (const char *p = fmt; *p && o + 1 < n; p++) {
        if (*p != '%') { out[o++] = *p; continue; }
        char spec[32]; int k = 0; spec[k++] = '%';
        p++;
        while (*p && strchr("-+ #0123456789.", *p) && k < 24) spec[k++] = *p++;
        while (*p == 'l' || *p == 'h') p++;
        char c = *p, tmp[512];
        if (!c) break;
        spec[k++] = c; spec[k] = 0;
        if (c == '%') snprintf(tmp, sizeof tmp, "%%");
        else if (c == 's') { snprintf(tmp, sizeof tmp, spec, GS(R32(ap))); ap += 4; }
        else if (c == 'f' || c == 'g' || c == 'e') { snprintf(tmp, sizeof tmp, spec, BD(R64(ap))); ap += 8; }
        else if (strchr("dic", c)) { snprintf(tmp, sizeof tmp, spec, (int)R32(ap)); ap += 4; }
        else { snprintf(tmp, sizeof tmp, spec, (unsigned)R32(ap)); ap += 4; }
        for (char *t = tmp; *t && o + 1 < n; t++) out[o++] = *t;
    }
    out[o] = 0;
}

static const char *g_tmpdir = "/tmp";

static void hook_xgetvol(void);
static void hook_getdirid(void);
static void hook_makefulpath(void);

void pt_import(uint32_t addr)
{
    char a[1024], b[1024];
    switch (imp_id(addr)) {
    case I_NewPtr: case I_malloc: G.eax = halloc(ARG(0), 0); break;
    case I_NewPtrClear: G.eax = halloc(ARG(0), 1); break;
    case I_DisposePtr: case I_free: hfree(ARG(0)); break;
    case I_cxa_allocate_exception: G.eax = halloc(ARG(0) + 16, 1); break;
    case I_cxa_throw: pt_fatal("Epson's code reported an error", 0); break;
    case I_Unwind_Resume: pt_fatal("unwinding after an error", 0); break;
    case I_cxa_begin_catch: G.eax = ARG(0); break;
    case I_cxa_end_catch: break;
    case I_memset: memset(M + ARG(0), (int)(ARG(1) & 0xFF), ARG(2)); G.eax = ARG(0); break;
    case I_memcpy: case I_memmove: memmove(M + ARG(0), M + ARG(1), ARG(2)); G.eax = ARG(0); break;
    case I_strcpy: strcpy((char *)M + ARG(0), GS(ARG(1))); G.eax = ARG(0); break;
    case I_strncpy: strncpy((char *)M + ARG(0), GS(ARG(1)), ARG(2)); G.eax = ARG(0); break;
    case I_strcat: strcat((char *)M + ARG(0), GS(ARG(1))); G.eax = ARG(0); break;
    case I_sprintf: { char t[2048]; gformat(t, sizeof t, GS(ARG(1)), G.esp + 12); strcpy((char *)M + ARG(0), t);
                      G.eax = (uint32_t)strlen(t); break; }
    case I_sscanf: G.eax = 0; break;
    case I_getenv: G.eax = 0; break;
    case I_exit: pt_fatal("exit", ARG(0)); break;
    case I_setjmp: G.eax = 0; break;
    case I_longjmp: pt_fatal("JPEG error", 0); break;
    case I_modf: { double ip, f = modf(ARGD(0), &ip); W64(ARG(2), DB(ip)); FPUSH(f); break; }
    /* stdio */
    case I_fopen: G.eax = gfile_add(fopen(GS(ARG(0)), GS(ARG(1)))); break;
    case I_tmpfile: G.eax = gfile_add(tmpfile()); break;
    case I_fclose: {
        FILE *f = gfile(ARG(0));
        G.eax = f ? (uint32_t)fclose(f) : (uint32_t)-1;
        if (f) g_files[ARG(0) - 0x100] = NULL;
        break;
    }
    case I_fread: { FILE *f = gfile(ARG(3)); G.eax = f ? (uint32_t)fread(M + ARG(0), ARG(1), ARG(2), f) : 0; break; }
    case I_fwrite: { FILE *f = gfile(ARG(3)); G.eax = f ? (uint32_t)fwrite(M + ARG(0), ARG(1), ARG(2), f) : 0; break; }
    case I_fseek: { FILE *f = gfile(ARG(0)); G.eax = f ? (uint32_t)fseek(f, (long)(int32_t)ARG(1), (int)ARG(2)) : (uint32_t)-1; break; }
    case I_getc: { FILE *f = gfile(ARG(0)); G.eax = f ? (uint32_t)getc(f) : (uint32_t)-1; break; }
    case I_putc: { FILE *f = gfile(ARG(1)); G.eax = f ? (uint32_t)putc((int)ARG(0), f) : (uint32_t)-1; break; }
    case I_fflush: { FILE *f = gfile(ARG(0)); G.eax = f ? (uint32_t)fflush(f) : 0; break; }
    case I_ferror: { FILE *f = gfile(ARG(0)); G.eax = f ? (uint32_t)ferror(f) : 1; break; }
    case I_fprintf: G.eax = 0; break;
    /* time and memory status */
    case I_GetTime: {
        time_t t = time(NULL); struct tm lt; localtime_r(&t, &lt);
        uint32_t r = ARG(0);
        W16(r, (uint32_t)(lt.tm_year + 1900)); W16(r + 2, (uint32_t)(lt.tm_mon + 1)); W16(r + 4, (uint32_t)lt.tm_mday);
        W16(r + 6, (uint32_t)lt.tm_hour); W16(r + 8, (uint32_t)lt.tm_min); W16(r + 10, (uint32_t)lt.tm_sec);
        W16(r + 12, (uint32_t)(lt.tm_wday + 1));
        break;
    }
    case I_Delay: if (ARG(1)) W32(ARG(1), 0); break;
    case I_TickCount: G.eax = (uint32_t)(clock() * 60 / CLOCKS_PER_SEC); break;
    case I_FreeMem: case I_MaxBlock: case I_CompactMem: G.eax = 0x40000000u; break;
    case I_Gestalt: if (ARG(1)) W32(ARG(1), 0); G.eax = 0; break;
    case I_p2cstrcpy: { uint8_t l = M[ARG(1)]; memmove(M + ARG(0), M + ARG(1) + 1, l); M[ARG(0) + l] = 0; break; }
    /* File Manager on real paths */
    case I_FindFolder:            /* (vRefNum, folderType, create, &vRefNum, &dirID): Temporary Items */
        W16(ARG(3), (uint32_t)-1); W32(ARG(4), (uint32_t)dir_id(g_tmpdir)); G.eax = 0; break;
    case I_FSMakeFSSpec: {        /* (vRefNum, dirID, name, spec) */
        uint32_t spec = ARG(3), dir = ARG(1);
        join(a, sizeof a, dir, M + ARG(2));
        make_spec(spec, a);
        G.eax = exists(a) ? 0 : (uint32_t)-43; break;
    }
    case I_FSpMakeFSRef:          /* (spec, ref): the ref holds a copy of the spec */
        spec_path(ARG(0), a, sizeof a);
        if (!exists(a)) { G.eax = (uint32_t)-43; break; }
        memcpy(M + ARG(1), M + ARG(0), 70); G.eax = 0; break;
    case I_FSRefMakePath:         /* (ref, path, max) */
        spec_path(ARG(0), a, sizeof a);
        snprintf((char *)M + ARG(1), ARG(2), "%s", a); G.eax = 0; break;
    case I_FSPathMakeRef:         /* (path, ref, &isDir) */
        if (!exists(GS(ARG(0)))) { G.eax = (uint32_t)-43; break; }
        make_spec(ARG(1), GS(ARG(0)));
        if (ARG(2)) M[ARG(2)] = (uint8_t)is_dir(GS(ARG(0)));
        G.eax = 0; break;
    case I_FSGetCatalogInfo:      /* (ref, which, info, name, spec, parent) */
        if (ARG(4)) memcpy(M + ARG(4), M + ARG(0), 70);
        G.eax = 0; break;
    case I_FSpGetFInfo:
        spec_path(ARG(0), a, sizeof a);
        if (ARG(1)) memset(M + ARG(1), 0, 16);
        G.eax = exists(a) ? 0 : (uint32_t)-43; break;
    case I_FSpCreate: {
        spec_path(ARG(0), a, sizeof a);
        if (exists(a)) { G.eax = (uint32_t)-48; break; }
        FILE *f = fopen(a, "wb");
        G.eax = f ? 0 : (uint32_t)-35;
        if (f) fclose(f);
        break;
    }
    case I_FSpDelete:
        spec_path(ARG(0), a, sizeof a);
        G.eax = unlink(a) == 0 ? 0 : (uint32_t)-43; break;
    case I_FSpOpenDF: {           /* (spec, permission, &refNum) */
        spec_path(ARG(0), a, sizeof a);
        int r = fs_open(a, (ARG(1) & 0xFF) == 1 ? "rb" : "r+b");
        if (!r) { G.eax = (uint32_t)-43; break; }
        W16(ARG(2), (uint32_t)r); G.eax = 0; break;
    }
    case I_FSRead: case I_FSWrite: {  /* (refNum, &count, buf) */
        FILE *f = fs_file(ARG(0) & 0xFFFF);
        uint32_t want = R32(ARG(1));
        size_t got = !f ? 0 : imp_id(addr) == I_FSRead ? fread(M + ARG(2), 1, want, f) : fwrite(M + ARG(2), 1, want, f);
        W32(ARG(1), (uint32_t)got);
        G.eax = got == want ? 0 : imp_id(addr) == I_FSRead ? (uint32_t)-39 : (uint32_t)-34; break;
    }
    case I_SetFPos: {             /* (refNum, mode, offset): 1 start, 2 end, 3 mark */
        FILE *f = fs_file(ARG(0) & 0xFFFF);
        int m = (int)(ARG(1) & 0xFFFF);
        G.eax = f && fseek(f, (long)(int32_t)ARG(2), m == 2 ? SEEK_END : m == 3 ? SEEK_CUR : SEEK_SET) == 0 ? 0 : (uint32_t)-40;
        break;
    }
    case I_GetFPos: { FILE *f = fs_file(ARG(0) & 0xFFFF); W32(ARG(1), f ? (uint32_t)ftell(f) : 0); G.eax = 0; break; }
    case I_GetEOF: {
        FILE *f = fs_file(ARG(0) & 0xFFFF);
        if (!f) { G.eax = (uint32_t)-51; break; }
        long cur = ftell(f); fseek(f, 0, SEEK_END); W32(ARG(1), (uint32_t)ftell(f)); fseek(f, cur, SEEK_SET);
        G.eax = 0; break;
    }
    case I_FSClose: {
        uint32_t r = ARG(0) & 0xFFFF;
        FILE *f = fs_file(r);
        if (f) { fclose(f); g_files[r] = NULL; }
        G.eax = 0; break;
    }
    case I_CFRelease: break;
    default: (void)b; pt_fatal("unsupported library call", addr);
    }
}

void pt_dispatch(uint32_t t)
{
    int found = 0;
    pt_dispatch_translated(t, &found);
    if (!found) pt_fatal("indirect call to unknown address", t);
}

/* ---- hooks: MoreFiles / Carbon path helpers, answered from the host --------------- */
/* XGetVolumeInfoNoName(path, vRefNum, XVolumeParam *): enough free space */
static void hook_xgetvol(void)
{
    uint32_t pb = ARG(2);
    W32(pb + 0x82, 0x7FFFFFFFu); W32(pb + 0x86, 0x7FFFFFFFu);
    G.eax = 0;
}
/* GetDirectoryID(vRefNum, dirID, name, &theDirID, &isDirectory) */
static void hook_getdirid(void)
{
    char p[1024];
    join(p, sizeof p, ARG(1), M + ARG(2));
    if (!is_dir(p)) { G.eax = (uint32_t)-43; return; }
    W32(ARG(3), (uint32_t)dir_id(p));
    M[ARG(4)] = 1;
    G.eax = 0;
}
/* makeFulPath(char *path, FSSpec *spec): the POSIX path of the spec */
static void hook_makefulpath(void)
{
    char p[1024];
    spec_path(ARG(1), p, sizeof p);
    if (is_dir(p)) strncat(p, "/", sizeof p - strlen(p) - 1);
    snprintf((char *)M + ARG(0), 256, "%s", p);
    G.eax = 0;
}
void PT_aa52(void) { hook_xgetvol(); G.esp += 4; }
void PT_b106(void) { hook_getdirid(); G.esp += 4; }
void PT_9fba(void) { hook_makefulpath(); G.esp += 4; }

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
    if (n < IMAGE_END) { snprintf(g_err, sizeof g_err, "PIM file is too short"); return -1; }
    for (size_t i = 0; i < sizeof kDataImp / sizeof *kDataImp; i++)
        W32(kDataImp[i][0], kDataImp[i][1] ? kDataImp[i][1] : SCRATCH + 0x400u + 0x40u * (uint32_t)i);
    memset(&G, 0, sizeof G);
    pt_fcw = 0x037F;
    fs_closeall();
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

static uint32_t gstr(const char *s)
{
    uint32_t a = halloc((uint32_t)strlen(s) + 1, 1);
    memcpy(M + a, s, strlen(s) + 1);
    return a;
}

/* CEsExfIF::EsExf_Entry: Create(work folder) -> SetTmpFilePath(the plain 24-bit TIFF) ->
 * SetExifBaseParam(software, value) -> SetSceneID(1, 3) -> DoMakePimFile(destination) -> Delete */
int pim_tiff_make(const char *engine_path, const char *work_dir, const char *tmp_tiff, const char *dest,
            const char *software, unsigned long value, char *err, size_t errlen)
{
    g_err[0] = 0;
    int rc = -1;
    g_tmpdir = work_dir;
    if (setjmp(g_fail)) { rc = -100; goto done; }
    if (guest_init(engine_path)) { rc = -101; goto done; }
    g_ndirs = 0;
    uint32_t work = halloc(0x50, 1), tmp = halloc(0x50, 1), dst = halloc(0x50, 1), pobj = halloc(4, 1);
    make_spec(work, work_dir);
    make_spec(tmp, tmp_tiff);
    make_spec(dst, dest);
    uint32_t a1[2] = { pobj, work };
    if (guest_call(PT_c58, 2, a1) & 0xFF) { snprintf(g_err, sizeof g_err, "PIM setup failed"); goto done; }
    uint32_t obj = R32(pobj);
    int r;
    uint32_t a2[2] = { obj, tmp };
    if ((r = (int16_t)guest_call(PT_e8c, 2, a2))) { snprintf(g_err, sizeof g_err, "PIM temporary file failed (%d)", r); goto del; }
    uint32_t a3[3] = { obj, gstr(software), (uint32_t)value };
    if ((r = (int16_t)guest_call(PT_fd2, 3, a3))) { snprintf(g_err, sizeof g_err, "PIM Exif data failed (%d)", r); goto del; }
    uint32_t a4[3] = { obj, 1, 3 };
    if ((r = (int16_t)guest_call(PT_10a2, 3, a4))) { snprintf(g_err, sizeof g_err, "PIM scene failed (%d)", r); goto del; }
    uint32_t a5[2] = { obj, dst };
    if ((r = (int16_t)guest_call(PT_1160, 2, a5))) { snprintf(g_err, sizeof g_err, "PIM file failed (%d)", r); goto del; }
    rc = 0;
del:;
    uint32_t a6[1] = { obj };
    guest_call(PT_d20, 1, a6);
done:
    fs_closeall();
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}
