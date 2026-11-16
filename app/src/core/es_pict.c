#include "es_pict.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *p; size_t n, cap; int fail; } Buf;

static void put(Buf *b, const void *d, size_t n)
{
    if (b->fail) return;
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 1 << 20;
        while (c < b->n + n) c *= 2;
        uint8_t *q = realloc(b->p, c);
        if (!q) { b->fail = 1; return; }
        b->p = q; b->cap = c;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}
static void u8(Buf *b, unsigned v) { uint8_t c = (uint8_t)v; put(b, &c, 1); }
static void u16(Buf *b, unsigned v) { u8(b, v >> 8); u8(b, v); }
static void u32(Buf *b, uint32_t v) { u16(b, v >> 16); u16(b, v & 0xFFFF); }
static void rect(Buf *b, int t, int l, int bo, int r) { u16(b, (unsigned)t); u16(b, (unsigned)l); u16(b, (unsigned)bo); u16(b, (unsigned)r); }
static void align(Buf *b) { if (b->n & 1) u8(b, 0); }

/* PackBits (PackBitsRect / UnpackBits) */
static size_t packbits(const uint8_t *s, size_t n, uint8_t *d)
{
    size_t i = 0, o = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 128 && s[i + run] == s[i]) run++;
        if (run >= 3 || (run == 2 && i + run == n)) {
            d[o++] = (uint8_t)(257 - run); d[o++] = s[i]; i += run;
        } else {
            size_t start = i, lit = 0;
            while (i < n && lit < 128) {
                if (i + 2 < n && s[i] == s[i + 1] && s[i] == s[i + 2]) break;
                i++; lit++;
            }
            d[o++] = (uint8_t)(lit - 1);
            memcpy(d + o, s + start, lit); o += lit;
        }
    }
    return o;
}

/* one packed scan line: byte count (word when rowBytes > 250), then the data */
static void packed_row(Buf *b, const uint8_t *row, size_t len, size_t rowbytes, uint8_t *tmp)
{
    if (rowbytes < 8) { put(b, row, len); return; }
    size_t n = packbits(row, len, tmp);
    if (rowbytes > 250) u16(b, (unsigned)n); else u8(b, (unsigned)n);
    put(b, tmp, n);
}

int es_save_pict(const char *raw, const char *out, int w, int h, int spp, int bits, int dpi,
                 char *err, size_t errlen)
{
    if (w <= 0 || h <= 0 || w > 32767 || h > 32767) {
        snprintf(err, errlen, "PICT can hold up to 32767 x 32767 pixels");
        return 0;
    }
    size_t in_rb = bits == 1 ? (size_t)(w + 7) / 8 : (size_t)w * spp * (bits > 8 ? 2 : 1);
    FILE *fi = fopen(raw, "rb");
    uint8_t *img = malloc(in_rb * h);
    if (!fi || !img || fread(img, 1, in_rb * h, fi) != in_rb * h) {
        if (fi) fclose(fi);
        free(img);
        snprintf(err, errlen, "scan data is shorter than expected");
        return 0;
    }
    fclose(fi);
    int depth = bits == 1 ? 1 : spp == 3 ? 32 : 8;
    uint32_t fres = (uint32_t)dpi << 16;
    Buf b = { 0 };
    for (int i = 0; i < 512; i++) u8(&b, 0);                    /* file header */
    size_t start = b.n;
    u16(&b, 0);                                                  /* picSize (low word) */
    rect(&b, 0, 0, h, w);                                        /* picFrame */
    u16(&b, 0x0011); u16(&b, 0x02FF);                            /* version 2 */
    u16(&b, 0x0C00);                                             /* extended header (OpenCPicture) */
    u16(&b, 0xFFFE); u16(&b, 0); u32(&b, fres); u32(&b, fres); rect(&b, 0, 0, h, w); u32(&b, 0);
    u16(&b, 0x001E);                                             /* DefHilite */
    u16(&b, 0x0001); u16(&b, 10); rect(&b, 0, 0, h, w);          /* ClipRect */
    size_t maxrow = (size_t)500 * 4 + 64;
    uint8_t *row = malloc(maxrow), *tmp = malloc(maxrow * 2);
    for (int y0 = 0; y0 < h; y0 += 500) {
        int y1 = y0 + 500 < h ? y0 + 500 : h;
        for (int x0 = 0; x0 < w; x0 += 500) {
            int x1 = x0 + 500 < w ? x0 + 500 : w, tw = x1 - x0;
            size_t rb;
            if (depth == 32) {
                rb = (size_t)tw * 4;
                u16(&b, 0x009A);                                 /* DirectBitsRect */
                u32(&b, 0x000000FF);                             /* baseAddr */
                u16(&b, (unsigned)(rb | 0x8000)); rect(&b, y0, x0, y1, x1);
                u16(&b, 0); u16(&b, rb < 8 ? 1 : 4); u32(&b, 0); u32(&b, fres); u32(&b, fres);
                u16(&b, 16); u16(&b, 32); u16(&b, 3); u16(&b, 8); u32(&b, 0); u32(&b, 0); u32(&b, 0);
            } else if (depth == 8) {
                rb = (size_t)tw;
                u16(&b, 0x0098);                                 /* PackBitsRect */
                u16(&b, (unsigned)(rb | 0x8000)); rect(&b, y0, x0, y1, x1);
                u16(&b, 0); u16(&b, 0); u32(&b, 0); u32(&b, fres); u32(&b, fres);
                u16(&b, 0); u16(&b, 8); u16(&b, 1); u16(&b, 8); u32(&b, 0); u32(&b, 0); u32(&b, 0);
                /* CTable 40: 256 grays, index 0 white */
                u32(&b, 40); u16(&b, 0); u16(&b, 255);
                for (int i = 0; i < 256; i++) { unsigned g = (unsigned)(255 - i) * 257; u16(&b, (unsigned)i); u16(&b, g); u16(&b, g); u16(&b, g); }
            } else {
                /* a 1-bit GWorld is a PixMap too: CTable 33 (0 white, 1 black) */
                rb = ((size_t)tw + 15) / 16 * 2;
                u16(&b, 0x0098);
                u16(&b, (unsigned)(rb | 0x8000)); rect(&b, y0, x0, y1, x1);
                u16(&b, 0); u16(&b, 0); u32(&b, 0); u32(&b, fres); u32(&b, fres);
                u16(&b, 0); u16(&b, 1); u16(&b, 1); u16(&b, 1); u32(&b, 0); u32(&b, 0); u32(&b, 0);
                u32(&b, 33); u16(&b, 0); u16(&b, 1);
                u16(&b, 0); u16(&b, 0xFFFF); u16(&b, 0xFFFF); u16(&b, 0xFFFF);
                u16(&b, 1); u16(&b, 0); u16(&b, 0); u16(&b, 0);
            }
            rect(&b, y0, x0, y1, x1); rect(&b, y0, x0, y1, x1); u16(&b, 0);   /* src, dst, srcCopy */
            for (int y = y0; y < y1; y++) {
                const uint8_t *s = img + (size_t)y * in_rb;
                memset(row, 0, maxrow);
                if (depth == 32) {                               /* packType 4: R, G, B planes */
                    for (int x = 0; x < tw; x++)
                        for (int c = 0; c < 3; c++)
                            row[(size_t)c * tw + x] = bits > 8 ? (uint8_t)(((const uint16_t *)s)[(size_t)(x0 + x) * 3 + c] >> 8)
                                                               : s[(size_t)(x0 + x) * 3 + c];
                    if (rb < 8) {                                /* packType 1: xRGB unpacked */
                        uint8_t t[8] = { 0 };
                        for (int x = 0; x < tw; x++) { t[x * 4 + 1] = row[x]; t[x * 4 + 2] = row[tw + x]; t[x * 4 + 3] = row[2 * tw + x]; }
                        put(&b, t, rb);
                    } else packed_row(&b, row, (size_t)tw * 3, rb, tmp);
                } else if (depth == 8) {
                    for (int x = 0; x < tw; x++) {
                        uint8_t v = bits > 8 ? (uint8_t)(((const uint16_t *)s)[(size_t)(x0 + x) * spp + (spp == 3 ? 1 : 0)] >> 8)
                                             : s[(size_t)(x0 + x) * spp + (spp == 3 ? 1 : 0)];
                        row[x] = (uint8_t)(255 - v);
                    }
                    packed_row(&b, row, rb, rb, tmp);
                } else {
                    for (int x = 0; x < tw; x++)
                        if (s[(x0 + x) >> 3] & (0x80 >> ((x0 + x) & 7))) row[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
                    packed_row(&b, row, rb, rb, tmp);
                }
            }
            align(&b);
        }
    }
    u16(&b, 0x00FF);                                             /* end of picture */
    free(row); free(tmp); free(img);
    if (b.fail) { free(b.p); snprintf(err, errlen, "not enough memory for the PICT file"); return 0; }
    size_t size = b.n - start;
    b.p[start] = (uint8_t)(size >> 8); b.p[start + 1] = (uint8_t)size;
    FILE *fo = fopen(out, "wb");
    int ok = fo && fwrite(b.p, 1, b.n, fo) == b.n;
    if (fo && fclose(fo)) ok = 0;
    free(b.p);
    if (!ok) snprintf(err, errlen, "can't write the file (disk full?)");
    return ok;
}

int es_raw_to_rgb8(const char *raw, const char *out, int w, int h, int spp, int bits, char *err, size_t errlen)
{
    size_t in_rb = bits == 1 ? (size_t)(w + 7) / 8 : (size_t)w * spp * (bits > 8 ? 2 : 1);
    FILE *fi = fopen(raw, "rb"), *fo = fopen(out, "wb");
    uint8_t *in = malloc(in_rb), *o = malloc((size_t)w * 3);
    int ok = fi && fo && in && o;
    for (int y = 0; ok && y < h; y++) {
        if (fread(in, 1, in_rb, fi) != in_rb) { ok = 0; break; }
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < 3; c++) {
                int k = spp == 3 ? c : 0;
                uint8_t v = bits == 1 ? ((in[x >> 3] & (0x80 >> (x & 7))) ? 0 : 255)
                          : bits > 8 ? (uint8_t)(((uint16_t *)in)[(size_t)x * spp + k] >> 8) : in[(size_t)x * spp + k];
                o[(size_t)x * 3 + c] = v;
            }
        }
        if (fwrite(o, 1, (size_t)w * 3, fo) != (size_t)w * 3) ok = 0;
    }
    if (fi) fclose(fi);
    if (fo && fclose(fo)) ok = 0;
    free(in); free(o);
    if (!ok) snprintf(err, errlen, "can't convert the image to 24-bit color");
    return ok;
}
