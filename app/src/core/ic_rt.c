/*
 * Runtime for the recompiled Epson Image Control plug-in: guest memory, the few library
 * calls the palette code makes, a stand-in for the host's GetParameter, and the entry
 * points in ic.h.
 */
#include "ic_rt.h"
#include "ic.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

uint8_t *M;
CPU G;

#define GUEST_SIZE   0x100000000ull
#define IMAGE_END    0x43000u          /* __TEXT + __DATA + __IMPORT (file offset = address) */
#define HEAP_BASE    0x10000000u
#define HEAP_END     0x20000000u
#define STACK_TOP    0x7F000000u
#define HOST_BASE    0x7E000000u       /* fake host interface: vtable entries point here */

#define G_PLUGIN_CONTROLER 0x40CF0u    /* _g_lpPlugInControler */
#define G_PLUGIN_DATA      0x40CF4u    /* _g_lpPlugInData      */
#define VERTEX_HEX         0x331C0u    /* CColorPalette::m_astColorBoxVertex */
#define VERTEX_TRI         0x330A0u    /* CColorPalette::m_astTryangleVertex */

void IC_2d0f2(void);   /* CColorPalette::CColorPalette() */
void IC_2d514(void);   /* CColorPalette::SetColor(long const*) */
void ic_dispatch_translated(uint32_t t, int *found);

static jmp_buf g_fail;
static char g_err[256];
static int g_screen_gamma = 22;

void ic_fatal(const char *what, uint32_t addr)
{
    snprintf(g_err, sizeof g_err, "Color Palette stopped: %s at 0x%x", what, addr);
    longjmp(g_fail, 1);
}

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

static uint32_t ARG(int i) { return R32(G.esp + 4 + 4 * i); }
static double ARGD(int off) { return BD(R64(G.esp + 4 + off)); }

void ic_import(uint32_t addr)
{
    switch (addr) {
    case 0x425D6: FPUSH(pow(ARGD(0), ARGD(8))); break;                       /* pow */
    default: ic_fatal("unsupported library call", addr);
    }
}

/* the host side of CPlugInControler::GetParameter (CHostIF::GetParameter) */
static void host_call(uint32_t slot)
{
    switch (slot) {
    case 2: {                     /* GetParameter(id, buffer) */
        uint32_t id = ARG(1), dst = ARG(2);
        if (id == 3) {            /* FIT3_PARA of the marquee: only +8 (screen gamma) is read here */
            memset(M + dst, 0, 0x80);
            W32(dst + 8, (uint32_t)g_screen_gamma);
        }
        G.eax = 0;
        break;
    }
    default: ic_fatal("unsupported host call", slot);
    }
}

void ic_dispatch(uint32_t t)
{
    if (t >= HOST_BASE && t < HOST_BASE + 0x400) { host_call((t - HOST_BASE) / 4); return; }
    int found = 0;
    ic_dispatch_translated(t, &found);
    if (!found) { snprintf(g_err, sizeof g_err, "Color Palette stopped: indirect call to unknown address 0x%x from 0x%x (args %x %x %x)", t, R32(G.esp), R32(G.esp + 4), R32(G.esp + 8), R32(G.esp + 12)); longjmp(g_fail, 1); }
}

static int guest_init(const char *engine_path)
{
    if (!M) {
        void *p = mmap(NULL, GUEST_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) { snprintf(g_err, sizeof g_err, "can't reserve guest memory"); return -1; }
        M = p;
    } else {
        memset(M, 0, IMAGE_END);
        memset(M + HEAP_BASE, 0, g_heap_top - HEAP_BASE);
    }
    g_heap_top = HEAP_BASE;
    FILE *f = fopen(engine_path, "rb");
    if (!f) { snprintf(g_err, sizeof g_err, "can't open %s", engine_path); return -1; }
    size_t n = fread(M, 1, IMAGE_END, f);
    fclose(f);
    if (n < IMAGE_END) { snprintf(g_err, sizeof g_err, "Image Control engine file is too short"); return -1; }
    memset(&G, 0, sizeof G);
    /* host interface object: vtable of HOST_BASE + 4 * slot */
    uint32_t vt = halloc(0x100), host = halloc(16), ctl = halloc(0x40), data = halloc(0x10000);
    for (uint32_t i = 0; i < 0x40; i++) W32(vt + 4 * i, HOST_BASE + 4 * i);
    W32(host, vt);
    W32(ctl + 0x10, host);
    W32(G_PLUGIN_CONTROLER, ctl);
    W32(G_PLUGIN_DATA, data);
    /* non-lazy pointers dyld fills in (__IMPORT,__pointers): the plug-in's own globals */
    W32(0x42008, G_PLUGIN_CONTROLER);
    W32(0x4200C, G_PLUGIN_DATA);

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

int ic_palette_shape(const char *engine_path, IcPaletteShape *s, char *err, size_t errlen)
{
    int rc = -1;
    g_err[0] = 0;
    if (setjmp(g_fail)) goto done;
    if (guest_init(engine_path)) goto done;
    for (int i = 0; i < IC_HEXAGONS; i++)
        for (int k = 0; k < 6; k++) {
            s->hex[i][k][0] = (int32_t)R32(VERTEX_HEX + 0x30u * (uint32_t)i + 8u * (uint32_t)k);
            s->hex[i][k][1] = (int32_t)R32(VERTEX_HEX + 0x30u * (uint32_t)i + 8u * (uint32_t)k + 4);
        }
    for (int i = 0; i < IC_TRIANGLES; i++) {
        for (int k = 0; k < 3; k++) {
            s->tri[i][k][0] = (int32_t)R32(VERTEX_TRI + 0x24u * (uint32_t)i + 8u * (uint32_t)k);
            s->tri[i][k][1] = (int32_t)R32(VERTEX_TRI + 0x24u * (uint32_t)i + 8u * (uint32_t)k + 4);
        }
        for (int c = 0; c < 3; c++) s->tri_rgb[i][c] = (int32_t)R32(VERTEX_TRI + 0x24u * (uint32_t)i + 0x18 + 4u * (uint32_t)c);
    }
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}

int ic_palette_colors(const char *engine_path, int screen_gamma10, const int rgb[3], int out[IC_HEXAGONS][3],
                      char *err, size_t errlen)
{
    int rc = -1;
    g_err[0] = 0;
    g_screen_gamma = screen_gamma10 > 0 ? screen_gamma10 : 22;
    if (setjmp(g_fail)) goto done;
    if (guest_init(engine_path)) goto done;
    uint32_t pal = halloc(0x1000);
    uint32_t a[1] = { pal };
    guest_call(IC_2d0f2, 1, a);                       /* constructor: cell vtables */
    /* what CColorPalette::Initialize leaves behind (its regions are only for drawing) */
    W32(pal + 0x18, 1);
    W32(pal + 0x24, IC_HEXAGONS);
    for (uint32_t i = 0; i < IC_HEXAGONS; i++) W32(pal + 0x28 + 0x4c * i + 4, 1);
    uint32_t col = halloc(16);
    for (uint32_t c = 0; c < 3; c++) W32(col + 4 * c, (uint32_t)rgb[c]);
    uint32_t b[2] = { pal, col };
    if (guest_call(IC_2d514, 2, b) == 0) { snprintf(g_err, sizeof g_err, "palette colours failed"); goto done; }
    for (uint32_t i = 0; i < IC_HEXAGONS; i++)
        for (uint32_t c = 0; c < 3; c++) out[i][c] = (int32_t)R32(pal + 0x28 + 0x4c * i + 8 + 4 * c);
    rc = 0;
done:
    if (err && errlen) snprintf(err, errlen, "%s", g_err);
    return rc;
}
