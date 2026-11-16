/*
 * CCITT Group 4 (T.6) encoder for B&W TIFF pages — what Epson's TIFF plug-in writes when
 * Compression > B&W is "CCITT Group4" (CTiff::Fax4Encode / Fax3Encode2DRow, from libtiff).
 * Input rows: 1 bit per pixel, MSB first, 1 = black (TIFF WhiteIsZero).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "es_g4.h"

typedef struct { uint16_t code, len; } Code;

/* ITU-T T.4 run-length codes: terminating 0..63, make-up 64..1728, extended make-up 1792..2560 */
static const Code kWhiteTerm[64] = {
    {0x35,8},{0x07,6},{0x07,4},{0x08,4},{0x0B,4},{0x0C,4},{0x0E,4},{0x0F,4},{0x13,5},{0x14,5},{0x07,5},{0x08,5},
    {0x08,6},{0x03,6},{0x34,6},{0x35,6},{0x2A,6},{0x2B,6},{0x27,7},{0x0C,7},{0x08,7},{0x17,7},{0x03,7},{0x04,7},
    {0x28,7},{0x2B,7},{0x13,7},{0x24,7},{0x18,7},{0x02,8},{0x03,8},{0x1A,8},{0x1B,8},{0x12,8},{0x13,8},{0x14,8},
    {0x15,8},{0x16,8},{0x17,8},{0x28,8},{0x29,8},{0x2A,8},{0x2B,8},{0x2C,8},{0x2D,8},{0x04,8},{0x05,8},{0x0A,8},
    {0x0B,8},{0x52,8},{0x53,8},{0x54,8},{0x55,8},{0x24,8},{0x25,8},{0x58,8},{0x59,8},{0x5A,8},{0x5B,8},{0x4A,8},
    {0x4B,8},{0x32,8},{0x33,8},{0x34,8} };
static const Code kWhiteMakeup[27] = {      /* 64, 128, ... 1728 */
    {0x1B,5},{0x12,5},{0x17,6},{0x37,7},{0x36,8},{0x37,8},{0x64,8},{0x65,8},{0x68,8},{0x67,8},{0xCC,9},{0xCD,9},
    {0xD2,9},{0xD3,9},{0xD4,9},{0xD5,9},{0xD6,9},{0xD7,9},{0xD8,9},{0xD9,9},{0xDA,9},{0xDB,9},{0x98,9},{0x99,9},
    {0x9A,9},{0x18,6},{0x9B,9} };
static const Code kBlackTerm[64] = {
    {0x37,10},{0x02,3},{0x03,2},{0x02,2},{0x03,3},{0x03,4},{0x02,4},{0x03,5},{0x05,6},{0x04,6},{0x04,7},{0x05,7},
    {0x07,7},{0x04,8},{0x07,8},{0x18,9},{0x17,10},{0x18,10},{0x08,10},{0x67,11},{0x68,11},{0x6C,11},{0x37,11},{0x28,11},
    {0x17,11},{0x18,11},{0xCA,12},{0xCB,12},{0xCC,12},{0xCD,12},{0x68,12},{0x69,12},{0x6A,12},{0x6B,12},{0xD2,12},{0xD3,12},
    {0xD4,12},{0xD5,12},{0xD6,12},{0xD7,12},{0x6C,12},{0x6D,12},{0xDA,12},{0xDB,12},{0x54,12},{0x55,12},{0x56,12},{0x57,12},
    {0x64,12},{0x65,12},{0x52,12},{0x53,12},{0x24,12},{0x37,12},{0x38,12},{0x27,12},{0x28,12},{0x58,12},{0x59,12},{0x2B,12},
    {0x2C,12},{0x5A,12},{0x66,12},{0x67,12} };
static const Code kBlackMakeup[27] = {
    {0x0F,10},{0xC8,12},{0xC9,12},{0x5B,12},{0x33,12},{0x34,12},{0x35,12},{0x6C,13},{0x6D,13},{0x4A,13},{0x4B,13},{0x4C,13},
    {0x4D,13},{0x72,13},{0x73,13},{0x74,13},{0x75,13},{0x76,13},{0x77,13},{0x52,13},{0x53,13},{0x54,13},{0x55,13},{0x5A,13},
    {0x5B,13},{0x64,13},{0x65,13} };
static const Code kExtMakeup[13] = {        /* 1792, 1856, ... 2560 */
    {0x08,11},{0x0C,11},{0x0D,11},{0x12,12},{0x13,12},{0x14,12},{0x15,12},{0x16,12},{0x17,12},{0x1C,12},{0x1D,12},
    {0x1E,12},{0x1F,12} };

typedef struct { uint8_t *p; size_t n, cap; uint32_t acc; int bits; int failed; } Out;

static void put(Out *o, uint32_t code, int len)
{
    o->acc = (o->acc << len) | (code & ((1u << len) - 1));
    o->bits += len;
    while (o->bits >= 8) {
        if (o->n == o->cap) {
            size_t c = o->cap ? o->cap * 2 : 65536;
            uint8_t *q = realloc(o->p, c);
            if (!q) { o->failed = 1; return; }
            o->p = q; o->cap = c;
        }
        o->p[o->n++] = (uint8_t)(o->acc >> (o->bits - 8));
        o->bits -= 8;
    }
}

static void put_run(Out *o, int run, int black)
{
    const Code *term = black ? kBlackTerm : kWhiteTerm, *mk = black ? kBlackMakeup : kWhiteMakeup;
    while (run >= 2560) { put(o, kExtMakeup[12].code, kExtMakeup[12].len); run -= 2560; }
    if (run >= 64) {
        int m = run / 64;                       /* 1..39 */
        if (m <= 27) put(o, mk[m - 1].code, mk[m - 1].len);
        else put(o, kExtMakeup[m - 28].code, kExtMakeup[m - 28].len);
        run -= m * 64;
    }
    put(o, term[run].code, term[run].len);
}

static int pix(const uint8_t *row, int x) { return (row[x >> 3] >> (7 - (x & 7))) & 1; }

/* first position from bs on whose pixel isn't `color` (libtiff finddiff), or be */
static int finddiff(const uint8_t *row, int bs, int be, int color)
{
    while (bs < be && pix(row, bs) == color) bs++;
    return bs;
}
static int finddiff2(const uint8_t *row, int bs, int be, int color) { return bs < be ? finddiff(row, bs, be, color) : be; }

int es_g4_encode(const uint8_t *rows, int w, int h, size_t rowbytes, uint8_t **out, size_t *outlen)
{
    /* vertical codes indexed by b1 - a1 + 3: VR3 VR2 VR1 V0 VL1 VL2 VL3 */
    static const Code vcodes[7] = { {0x03,7},{0x03,6},{0x03,3},{0x1,1},{0x2,3},{0x02,6},{0x02,7} };
    Out o = { 0 };
    uint8_t *white = calloc(1, rowbytes ? rowbytes : 1);
    if (!white) return -1;
    const uint8_t *ref = white;
    for (int y = 0; y < h && !o.failed; y++) {
        const uint8_t *cur = rows + (size_t)y * rowbytes;
        /* Fax3Encode2DRow */
        int a0 = 0;
        int a1 = pix(cur, 0) ? 0 : finddiff(cur, 0, w, 0);
        int b1 = pix(ref, 0) ? 0 : finddiff(ref, 0, w, 0);
        for (;;) {
            int b2 = finddiff2(ref, b1, w, b1 < w ? pix(ref, b1) : 0);
            if (b2 >= a1) {
                int d = b1 - a1;
                if (d < -3 || d > 3) {          /* horizontal mode */
                    int a2 = finddiff2(cur, a1, w, a1 < w ? pix(cur, a1) : 0);
                    put(&o, 0x1, 3);
                    if (a0 + a1 == 0 || pix(cur, a0) == 0) { put_run(&o, a1 - a0, 0); put_run(&o, a2 - a1, 1); }
                    else { put_run(&o, a1 - a0, 1); put_run(&o, a2 - a1, 0); }
                    a0 = a2;
                } else {                        /* vertical mode */
                    put(&o, vcodes[d + 3].code, vcodes[d + 3].len);
                    a0 = a1;
                }
            } else {                            /* pass mode */
                put(&o, 0x1, 4);
                a0 = b2;
            }
            if (a0 >= w) break;
            int c = pix(cur, a0);
            a1 = finddiff(cur, a0, w, c);
            b1 = finddiff(ref, a0, w, !c);
            b1 = finddiff(ref, b1, w, c);
        }
        ref = cur;
    }
    put(&o, 0x001, 12);                          /* EOFB */
    put(&o, 0x001, 12);
    if (o.bits) put(&o, 0, 8 - o.bits);
    free(white);
    if (o.failed) { free(o.p); return -1; }
    *out = o.p;
    *outlen = o.n;
    return 0;
}
