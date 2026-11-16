/* Epson Scan file writers.  Ported from the EPSON TIFF plug-in
 * (CTiff::SetHeaderRGB48/RGB24/G16/G8) and the EPSON JPEG plug-in (CJpeg). */
#include "es_save.h"
#include "es_g4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include "../libjpeg/jpeglib.h"

/* ---------------------------------------------------------------- TIFF */

typedef struct { uint8_t *p; int be; } Buf;

static void put16(Buf *b, size_t off, unsigned v)
{
    if (b->be) { b->p[off] = v >> 8; b->p[off + 1] = v; }
    else       { b->p[off] = v;      b->p[off + 1] = v >> 8; }
}
static void put32(Buf *b, size_t off, uint32_t v)
{
    if (b->be) { b->p[off] = v >> 24; b->p[off + 1] = v >> 16; b->p[off + 2] = v >> 8; b->p[off + 3] = v; }
    else       { b->p[off] = v; b->p[off + 1] = v >> 8; b->p[off + 2] = v >> 16; b->p[off + 3] = v >> 24; }
}

/* One page: IFD count at file offset `base`, the entries, the next-IFD link (0), BitsPerSample
 * (RGB), XRes, YRes, the ICC profile, then the strip -- CTiff::SetHeaderRGB48/RGB24/G16/G8.
 * multi: the Multi-TIFF plug-in's page (NewSubfileType 2, PageNumber = page as one LONG). */
/* TIFF plug-in Compression > B&W (CTiff +0x8e): 1 none, 4 CCITT Group 4 */
static int g_bw_g4;
void es_tiff_set_bw_g4(int on) { g_bw_g4 = on; }

static int write_page(FILE *fo, uint32_t base, int multi, int page, FILE *fi, int w, int h, int spp, int bits,
                      int dpi_x, int dpi_y, int big_endian, const unsigned char *icc, size_t icc_len,
                      char *err, size_t errlen)
{
    int rgb = spp == 3, wide = bits > 8, mono = bits == 1;
    size_t row = mono ? (size_t)(w + 7) / 8 : (size_t)w * spp * (wide ? 2 : 1);
    uint32_t bytes = (uint32_t)(row * h);                        /* this+0x88 */
    /* B&W: CCITT Group 4 (CTiff::Fax4Encode) needs the whole page first for StripByteCounts */
    uint8_t *g4 = NULL;
    size_t g4len = 0;
    int ccitt = mono && g_bw_g4;
    if (ccitt) {
        uint8_t *all = malloc(row * (size_t)h);
        if (!all || fread(all, 1, row * (size_t)h, fi) != row * (size_t)h ||
            es_g4_encode(all, w, h, row, &g4, &g4len)) {
            free(all);
            snprintf(err, errlen, "can't compress the B&W image");
            return 0;
        }
        free(all);
        bytes = (uint32_t)g4len;
    }
    int n = rgb ? 17 : 14;
    size_t body = (size_t)n * 12 + (rgb ? 0x1a : 0x14);          /* local_18 / local_1c */
    if (icc && icc_len) { n++; body += icc_len + 12; }
    size_t len = 2 + body;                                       /* count + body, from base */
    Buf b = { calloc(1, len), big_endian };
    if (!b.p) { snprintf(err, errlen, "out of memory"); return 0; }
    uint8_t *mem = b.p;
#define OFF(x) ((x) - base)
    put16(&b, 0, n);
    uint32_t after = base + 2 + (uint32_t)n * 12 + 4;            /* first byte after the link */
    uint32_t bps_off = 0, xres_off, yres_off;
    int i = 0;
#define ENTRY(tag, type, count, value) do { \
        size_t o = 2 + (size_t)i * 12; \
        put16(&b, o, tag); put16(&b, o + 2, type); put32(&b, o + 4, count); \
        if ((type) == 3 && (count) == 1) { put16(&b, o + 8, value); put16(&b, o + 10, 0); } \
        else put32(&b, o + 8, value); \
        i++; } while (0)
    ENTRY(0xFE, 4, 1, multi ? 2 : 0);
    ENTRY(0x100, 4, 1, (uint32_t)w);
    ENTRY(0x101, 4, 1, (uint32_t)h);
    if (rgb) {
        bps_off = after;                                         /* n*12 + 0xe (+ base - 8) */
        ENTRY(0x102, 3, 3, bps_off);
        for (int k = 0; k < 3; k++) put16(&b, OFF(bps_off) + 2 * k, wide ? 16 : 8);
        xres_off = bps_off + 6;
    } else {
        ENTRY(0x102, 3, 1, mono ? 1 : wide ? 16 : 8);
        xres_off = after;
    }
    yres_off = xres_off + 8;
    ENTRY(0x103, 3, 1, ccitt ? 4 : 1);
    /* SetHeaderG1: CCITT -> WhiteIsZero, uncompressed -> BlackIsZero (1 = white, as the scanner
     * binarizes) */
    ENTRY(0x106, 3, 1, rgb ? 2 : ccitt ? 0 : 1);
    ENTRY(0x111, 4, 1, base + (uint32_t)len);                    /* right after this header */
    ENTRY(0x115, 3, 1, (uint32_t)spp);
    ENTRY(0x116, 4, 1, (uint32_t)h);
    ENTRY(0x117, 4, 1, bytes);
    if (rgb) {
        ENTRY(0x118, 3, 1, 0);
        ENTRY(0x119, 3, 1, 0xff);                                /* Epson writes 255 for 48-bit too */
    }
    ENTRY(0x11A, 5, 1, xres_off);
    put32(&b, OFF(xres_off), (uint32_t)dpi_x); put32(&b, OFF(xres_off) + 4, 1);
    ENTRY(0x11B, 5, 1, yres_off);
    put32(&b, OFF(yres_off), (uint32_t)dpi_y); put32(&b, OFF(yres_off) + 4, 1);
    if (rgb) ENTRY(0x11C, 3, 1, 1);
    ENTRY(0x128, 3, 1, 2);
    ENTRY(0x129, 3, 2, (uint32_t)page);                          /* written as one LONG, like Epson */
    if (icc && icc_len) {
        ENTRY(0x8773, 7, (uint32_t)icc_len, yres_off + 8);
        memcpy(b.p + OFF(yres_off) + 8, icc, icc_len);
    }
#undef ENTRY
#undef OFF
    int ok = fwrite(b.p, 1, len, fo) == len;
    free(mem);
    if (!ok) { free(g4); snprintf(err, errlen, "can't write the file (disk full?)"); return 0; }
    if (ccitt) {
        ok = fwrite(g4, 1, g4len, fo) == g4len;
        free(g4);
        if (!ok) snprintf(err, errlen, "can't write the file (disk full?)");
        return ok;
    }

    uint8_t *buf = malloc(row);
    const uint16_t probe = 1;
    int host_le = *(const uint8_t *)&probe == 1;
    int swap = wide && (host_le == big_endian);
    for (int y = 0; ok && y < h; y++) {
        if (!buf || fread(buf, 1, row, fi) != row) { snprintf(err, errlen, "scan data is shorter than expected"); ok = 0; break; }
        if (swap) for (size_t k = 0; k < row; k += 2) { uint8_t t = buf[k]; buf[k] = buf[k + 1]; buf[k + 1] = t; }
        if (mono) for (size_t k = 0; k < row; k++) buf[k] = (uint8_t)~buf[k];   /* BlackIsZero */
        if (fwrite(buf, 1, row, fo) != row) { snprintf(err, errlen, "can't write the file (disk full?)"); ok = 0; }
    }
    free(buf);
    return ok;
}

static void header8(uint8_t h[8], int big_endian)
{
    Buf b = { h, big_endian };
    memcpy(h, big_endian ? "MM" : "II", 2);
    put16(&b, 2, 42);
    put32(&b, 4, 8);
}

int es_save_tiff(const char *raw, const char *out, int w, int h, int spp, int bits,
                 int dpi_x, int dpi_y, int big_endian,
                 const unsigned char *icc, size_t icc_len, char *err, size_t errlen)
{
    FILE *fi = fopen(raw, "rb");
    if (!fi) { snprintf(err, errlen, "can't read the scan data"); return 0; }
    FILE *fo = fopen(out, "wb");
    if (!fo) { fclose(fi); snprintf(err, errlen, "can't create the file"); return 0; }
    uint8_t hd[8];
    header8(hd, big_endian);
    int ok = fwrite(hd, 1, 8, fo) == 8 &&
             write_page(fo, 8, 0, 0, fi, w, h, spp, bits, dpi_x, dpi_y, big_endian, icc, icc_len, err, errlen);
    fclose(fi);
    if (fclose(fo) != 0 && ok) { snprintf(err, errlen, "can't write the file (disk full?)"); ok = 0; }
    if (!ok) remove(out);
    return ok;
}

/* CMultiTiff::DoFinish: page 0 makes the file; each later page goes on the end (its IFD at the
 * old file size, unpadded) and the previous page's next-IFD link is patched to it. */
int es_mtiff_add_page(const char *raw, const char *out, int page, unsigned int *link_pos, int w, int h, int spp,
                      int bits, int dpi_x, int dpi_y, int big_endian, char *err, size_t errlen)
{
    FILE *fi = fopen(raw, "rb");
    if (!fi) { snprintf(err, errlen, "can't read the scan data"); return 0; }
    FILE *fo = fopen(out, page == 0 ? "wb" : "r+b");
    if (!fo) { fclose(fi); snprintf(err, errlen, "can't open the file"); return 0; }
    int ok = 1;
    uint32_t base = 8;
    if (page == 0) {
        uint8_t hd[8];
        header8(hd, big_endian);
        ok = fwrite(hd, 1, 8, fo) == 8;
    } else {
        fseek(fo, 0, SEEK_END);
        base = (uint32_t)ftell(fo);
        uint8_t l[4];
        Buf b = { l, big_endian };
        put32(&b, 0, base);
        ok = fseek(fo, (long)*link_pos, SEEK_SET) == 0 && fwrite(l, 1, 4, fo) == 4 && fseek(fo, 0, SEEK_END) == 0;
    }
    int n = spp == 3 ? 17 : 14;
    if (ok) ok = write_page(fo, base, 1, page, fi, w, h, spp, bits, dpi_x, dpi_y, big_endian, NULL, 0, err, errlen);
    if (ok) *link_pos = base + 2 + (uint32_t)n * 12;
    fclose(fi);
    if (fclose(fo) != 0 && ok) { snprintf(err, errlen, "can't write the file (disk full?)"); ok = 0; }
    if (!ok && page == 0) remove(out);
    return ok;
}

/* ---------------------------------------------------------------- JPEG */

struct es_jerr { struct jpeg_error_mgr pub; jmp_buf jb; char msg[JMSG_LENGTH_MAX]; };

static void es_jerror_exit(j_common_ptr c)
{
    struct es_jerr *e = (struct es_jerr *)c->err;
    (*c->err->format_message)(c, e->msg);
    longjmp(e->jb, 1);
}

/* write_icc_profile from the JPEG plug-in: APP2 "ICC_PROFILE\0" chunks of
 * at most 0xffef bytes, numbered from 1. */
static void write_icc(j_compress_ptr c, const unsigned char *p, size_t len)
{
    unsigned total = (unsigned)((len + 0xffee) / 0xffef), seq = 1;
    while (len) {
        size_t n = len > 0xffef ? 0xffef : len;
        len -= n;
        jpeg_write_m_header(c, JPEG_APP0 + 2, (unsigned)(n + 14));
        const char *tag = "ICC_PROFILE";
        for (int k = 0; k < 12; k++) jpeg_write_m_byte(c, tag[k]);  /* includes the NUL */
        jpeg_write_m_byte(c, seq++);
        jpeg_write_m_byte(c, total);
        while (n--) jpeg_write_m_byte(c, *p++);
    }
}

int es_save_jpeg(const char *raw, const char *out, int w, int h, int spp, int bits,
                 int dpi_x, int dpi_y, int level, int progressive,
                 const unsigned char *icc, size_t icc_len, char *err, size_t errlen)
{
    int wide = bits > 8, mono = bits == 1;
    size_t in_row = mono ? (size_t)(w + 7) / 8 : (size_t)w * spp * (wide ? 2 : 1);
    FILE *fi = fopen(raw, "rb");
    if (!fi) { snprintf(err, errlen, "can't read the scan data"); return 0; }
    FILE *fo = fopen(out, "wb");
    if (!fo) { fclose(fi); snprintf(err, errlen, "can't create the file"); return 0; }

    struct jpeg_compress_struct c;
    struct es_jerr je;
    uint8_t *volatile ibuf = malloc(in_row);
    JSAMPLE *volatile obuf = malloc((size_t)w * spp);
    volatile int ok = 0;
    c.err = jpeg_std_error(&je.pub);
    je.pub.error_exit = es_jerror_exit;
    if (setjmp(je.jb)) {
        snprintf(err, errlen, "JPEG error: %s", je.msg);
        goto done;
    }
    jpeg_create_compress(&c);
    if (!ibuf || !obuf) { snprintf(err, errlen, "out of memory"); goto done; }
    jpeg_stdio_dest(&c, fo);
    c.image_width = w;
    c.image_height = h;
    c.input_components = spp;
    c.in_color_space = spp == 1 ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&c);
    /* libjpeg 6b (the plug-in's version) has no fancy downsampling */
    c.do_fancy_downsampling = FALSE;
    jpeg_set_quality(&c, 100 - level, TRUE);
    c.write_JFIF_header = TRUE;
    c.JFIF_major_version = 1;
    c.JFIF_minor_version = 1;
    c.density_unit = 1;
    c.X_density = (UINT16)dpi_x;
    c.Y_density = (UINT16)dpi_y;
    if (progressive) jpeg_simple_progression(&c);
    jpeg_start_compress(&c, TRUE);
    if (icc && icc_len) write_icc(&c, icc, icc_len);

    const uint16_t probe = 1;
    int host_le = *(const uint8_t *)&probe == 1;
    for (int y = 0; y < h; y++) {
        if (fread(ibuf, 1, in_row, fi) != in_row) { snprintf(err, errlen, "scan data is shorter than expected"); goto done; }
        if (mono) {   /* Black & White: 1 = black */
            for (int x = 0; x < w; x++) obuf[x] = (ibuf[x >> 3] & (0x80 >> (x & 7))) ? 0 : 255;
        } else if (wide) {   /* the plug-in keeps the high byte of 16-bit samples */
            for (size_t k = 0; k < (size_t)w * spp; k++) obuf[k] = ibuf[2 * k + (host_le ? 1 : 0)];
        } else memcpy(obuf, ibuf, in_row);
        JSAMPROW r = obuf;
        jpeg_write_scanlines(&c, &r, 1);
    }
    jpeg_finish_compress(&c);
    ok = 1;
done:
    jpeg_destroy_compress(&c);
    free(ibuf);
    free(obuf);
    fclose(fi);
    if (fclose(fo) != 0 && ok) { snprintf(err, errlen, "can't write the file (disk full?)"); ok = 0; }
    if (!ok) remove(out);
    return ok;
}

/* Editing Page: turn a page by quarter turns (1 = 90 degrees clockwise), into another raw file. */
int es_rotate_raw(const char *in, const char *out, int w, int h, int spp, int bits, int quarters,
                  int *ow, int *oh, char *err, size_t errlen)
{
    return es_transform_raw(in, out, w, h, spp, bits, quarters, 0, ow, oh, err, errlen);
}

int es_transform_raw(const char *in, const char *out, int w, int h, int spp, int bits, int quarters, int mirror,
                     int *ow, int *oh, char *err, size_t errlen)
{
    quarters = ((quarters % 4) + 4) % 4;
    int mono = bits == 1;
    size_t bpp = mono ? 1 : (size_t)spp * (bits > 8 ? 2 : 1);
    size_t n = (size_t)w * h * bpp;
    uint8_t *src = malloc(n), *dst = malloc(n);
    FILE *fi = fopen(in, "rb");
    int ok = src && dst && fi;
    if (ok && mono) {                  /* unpack 1-bit rows to a byte per pixel */
        size_t rb = (size_t)(w + 7) / 8;
        uint8_t *row = malloc(rb);
        for (int y = 0; ok && y < h; y++) {
            ok = row && fread(row, 1, rb, fi) == rb;
            for (int x = 0; ok && x < w; x++) src[(size_t)y * w + x] = (row[x >> 3] >> (7 - (x & 7))) & 1;
        }
        free(row);
    } else if (ok) ok = fread(src, 1, n, fi) == n;
    if (fi) fclose(fi);
    if (!ok) { free(src); free(dst); snprintf(err, errlen, "can't read the page"); return 0; }
    int W = quarters & 1 ? h : w, H = quarters & 1 ? w : h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int nx, ny, sx = mirror ? w - 1 - x : x;     /* mirrored left to right first */
            switch (quarters) {
            case 1: nx = h - 1 - y; ny = x; break;            /* clockwise */
            case 2: nx = w - 1 - x; ny = h - 1 - y; break;
            case 3: nx = y; ny = w - 1 - x; break;            /* counterclockwise */
            default: nx = x; ny = y;
            }
            memcpy(dst + ((size_t)ny * W + nx) * bpp, src + ((size_t)y * w + sx) * bpp, bpp);
        }
    FILE *fo = fopen(out, "wb");
    if (mono) {                        /* pack again */
        size_t rb = (size_t)(W + 7) / 8;
        uint8_t *row = calloc(1, rb);
        ok = fo && row;
        for (int y = 0; ok && y < H; y++) {
            memset(row, 0, rb);
            for (int x = 0; x < W; x++) if (dst[(size_t)y * W + x]) row[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
            ok = fwrite(row, 1, rb, fo) == rb;
        }
        free(row);
    } else ok = fo && fwrite(dst, 1, n, fo) == n;
    if (fo && fclose(fo) != 0) ok = 0;
    free(src); free(dst);
    if (!ok) { snprintf(err, errlen, "can't write the page"); return 0; }
    *ow = W; *oh = H;
    return 1;
}
