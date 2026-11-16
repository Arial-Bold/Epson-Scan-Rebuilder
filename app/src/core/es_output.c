/*
 * es_output.c — turn the scanner's linear raw data into a finished image file.
 *
 * Interim conversion until Epson's own film processing (FIT) is ported:
 *   - negatives: per-channel auto levels over the frame, inverted
 *   - everything: display gamma 2.2 (the scanner data is linear)
 */
#include "es_output.h"
#include "fit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void w16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }
static void w32(FILE *f, uint32_t v) { w16(f, v & 0xFFFF); w16(f, v >> 16); }
static void entry(FILE *f, uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
{
    w16(f, tag); w16(f, type); w32(f, count);
    if (type == 3 && count == 1) { w16(f, (uint16_t)value); w16(f, 0); }
    else w32(f, value);
}

/* Uncompressed baseline TIFF header + IFD; image data follows directly. */
static void write_tiff_header(FILE *fo, int w, int h, int spp, int out_bits, int dpi)
{
    size_t out_row = (size_t)w * spp * (out_bits / 8);
    const uint16_t ne = 13;
    uint32_t ifd_end = 8 + 2 + ne * 12 + 4;
    uint32_t bps_off = ifd_end, xres = bps_off + (spp > 1 ? spp * 2 : 0), yres = xres + 8, data = yres + 8;
    w16(fo, 0x4949); w16(fo, 42); w32(fo, 8);
    w16(fo, ne);
    entry(fo, 256, 4, 1, (uint32_t)w);
    entry(fo, 257, 4, 1, (uint32_t)h);
    entry(fo, 258, 3, (uint32_t)spp, spp > 1 ? bps_off : (uint32_t)out_bits);
    entry(fo, 259, 3, 1, 1);
    entry(fo, 262, 3, 1, spp == 3 ? 2 : 1);
    entry(fo, 273, 4, 1, data);
    entry(fo, 277, 3, 1, (uint32_t)spp);
    entry(fo, 278, 4, 1, (uint32_t)h);
    entry(fo, 279, 4, 1, (uint32_t)(out_row * (size_t)h));
    entry(fo, 282, 5, 1, xres);
    entry(fo, 283, 5, 1, yres);
    entry(fo, 284, 3, 1, 1);
    entry(fo, 296, 3, 1, 2);
    w32(fo, 0);
    if (spp > 1) for (int i = 0; i < spp; i++) w16(fo, (uint16_t)out_bits);
    w32(fo, (uint32_t)dpi); w32(fo, 1);
    w32(fo, (uint32_t)dpi); w32(fo, 1);
}

static int lut_convert(const char *raw, const char *dst, int w, int h, const uint16_t *lut12,
                       int out_bits, int dpi, int tiff_header, char *err, size_t errlen);

int es_convert_lut_to_tiff(const char *raw, const char *tiff, int w, int h, const uint16_t *lut12,
                           int out_bits, int dpi, char *err, size_t errlen)
{
    return lut_convert(raw, tiff, w, h, lut12, out_bits, dpi, 1, err, errlen);
}

int es_apply_lut_raw(const char *raw16, const char *out_raw, int w, int h, const uint16_t *lut12,
                     int out_bits, char *err, size_t errlen)
{
    return lut_convert(raw16, out_raw, w, h, lut12, out_bits, 0, 0, err, errlen);
}

int es_raw_to_tiff(const char *raw, const char *tiff, int w, int h, int spp, int bits, int dpi,
                   char *err, size_t errlen)
{
    FILE *fi = fopen(raw, "rb"), *fo = NULL;
    size_t row = (size_t)w * spp * (bits / 8);
    uint8_t *buf = malloc(row);
    int rc = -1;
    if (!fi || !buf) { snprintf(err, errlen, "can't read the scan data"); goto out; }
    fo = fopen(tiff, "wb");
    if (!fo) { snprintf(err, errlen, "can't create %s", tiff); goto out; }
    write_tiff_header(fo, w, h, spp, bits, dpi);
    for (int y = 0; y < h; y++) {
        if (fread(buf, 1, row, fi) != row) { snprintf(err, errlen, "scan data is shorter than expected"); goto out; }
        if (fwrite(buf, 1, row, fo) != row) { snprintf(err, errlen, "disk write failed"); goto out; }
    }
    rc = 0;
out:
    if (fi) fclose(fi);
    if (fo && fclose(fo)) rc = -1;
    free(buf);
    return rc;
}

static int lut_convert(const char *raw, const char *tiff, int w, int h, const uint16_t *lut12,
                       int out_bits, int dpi, int tiff_header, char *err, size_t errlen)
{
    FILE *fi = fopen(raw, "rb"), *fo = NULL;
    size_t in_row = (size_t)w * 6, out_row = (size_t)w * 3 * (out_bits / 8);
    uint16_t *ibuf = malloc(in_row);
    uint8_t *obuf = malloc(out_row);
    int rc = -1;
    if (!fi || !ibuf || !obuf) { snprintf(err, errlen, "can't read the scan data"); goto out; }
    fo = fopen(tiff, "wb");
    if (!fo) { snprintf(err, errlen, "can't create %s", tiff); goto out; }
    if (tiff_header) write_tiff_header(fo, w, h, 3, out_bits, dpi);
    for (int y = 0; y < h; y++) {
        if (fread(ibuf, 1, in_row, fi) != in_row) { snprintf(err, errlen, "scan data is shorter than expected"); goto out; }
        for (size_t k = 0; k < (size_t)w * 3; k++) {
            uint16_t v = fit_lookup16(lut12 + 4096 * (k % 3), ibuf[k]);
            if (out_bits == 16) ((uint16_t *)obuf)[k] = v;
            else obuf[k] = (uint8_t)(v >> 8);
        }
        if (fwrite(obuf, 1, out_row, fo) != out_row) { snprintf(err, errlen, "disk write failed"); goto out; }
    }
    rc = 0;
out:
    if (fi) fclose(fi);
    if (fo && fclose(fo)) rc = -1;
    free(ibuf); free(obuf);
    return rc;
}

static int convert(const char *raw, const char *tiff, int w, int h, int spp, int in_bits, int out_bits,
                   int negative, int dpi, int header, double gamma, int autolevel, char *err, size_t errlen);

int es_convert_to_tiff(const char *raw, const char *tiff, int w, int h, int spp, int in_bits, int out_bits,
                       int negative, int dpi, char *err, size_t errlen)
{
    return convert(raw, tiff, w, h, spp, in_bits, out_bits, negative, dpi, 1, 2.2, negative, err, errlen);
}

int es_convert_to_raw(const char *raw, const char *out, int w, int h, int spp, int in_bits, int out_bits,
                      int negative, char *err, size_t errlen)
{
    return convert(raw, out, w, h, spp, in_bits, out_bits, negative, 0, 0, 2.2, negative, err, errlen);
}

int es_convert_to_raw_gamma(const char *raw, const char *out, int w, int h, int spp, int in_bits, int out_bits,
                            int negative, double gamma, char *err, size_t errlen)
{
    return convert(raw, out, w, h, spp, in_bits, out_bits, negative, 0, 0, gamma, negative, err, errlen);
}

int es_convert_plain(const char *raw, const char *out, int w, int h, int spp, int in_bits, int out_bits,
                     int negative, double gamma, char *err, size_t errlen)
{
    return convert(raw, out, w, h, spp, in_bits, out_bits, negative, 0, 0, gamma, 0, err, errlen);
}

static int convert(const char *raw, const char *tiff, int w, int h, int spp, int in_bits, int out_bits,
                   int negative, int dpi, int header, double gamma, int autolevel, char *err, size_t errlen)
{
    FILE *fi = fopen(raw, "rb"), *fo = NULL;
    int rc = -1;
    size_t in_bps = in_bits / 8, out_bps = out_bits / 8;
    size_t in_row = (size_t)w * spp * in_bps, out_row = (size_t)w * spp * out_bps;
    uint8_t *ibuf = malloc(in_row), *obuf = malloc(out_row);
    uint32_t maxin = in_bits == 16 ? 65535 : 255, maxout = out_bits == 16 ? 65535 : 255;
    uint32_t *hist = NULL;
    uint16_t *lut[3] = { NULL, NULL, NULL };
    if (!fi || !ibuf || !obuf) { snprintf(err, errlen, "can't read the scan data"); goto out; }

    /* 1. levels: 0.1% / 99.9% per channel over the central 90% (negatives only) */
    double lo[3] = { 0, 0, 0 }, hi[3] = { maxin, maxin, maxin };
    if (autolevel) {
        hist = calloc((size_t)3 * (maxin + 1), sizeof(uint32_t));
        if (!hist) { snprintf(err, errlen, "out of memory"); goto out; }
        long n = 0;
        int y0 = h / 20, y1 = h - h / 20, x0 = w / 20, x1 = w - w / 20;
        for (int y = y0; y < y1; y += 2) {
            if (fseek(fi, (long)y * (long)in_row, SEEK_SET) || fread(ibuf, 1, in_row, fi) != in_row) break;
            for (int x = x0; x < x1; x += 2, n++)
                for (int c = 0; c < spp; c++) {
                    size_t k = ((size_t)x * spp + c);
                    uint32_t v = in_bits == 16 ? ((uint16_t *)ibuf)[k] : ibuf[k];
                    hist[(size_t)c * (maxin + 1) + v]++;
                }
        }
        for (int c = 0; c < spp; c++) {
            long acc = 0;
            for (uint32_t v = 0; v <= maxin; v++) { acc += hist[(size_t)c * (maxin + 1) + v]; if (acc > n / 1000) { lo[c] = v; break; } }
            acc = 0;
            for (long v = maxin; v >= 0; v--) { acc += hist[(size_t)c * (maxin + 1) + v]; if (acc > n / 1000) { hi[c] = v; break; } }
            if (hi[c] <= lo[c]) hi[c] = lo[c] + 1;
        }
        rewind(fi);
    }
    for (int c = 0; c < spp; c++) {
        lut[c] = malloc(sizeof(uint16_t) * (maxin + 1));
        if (!lut[c]) { snprintf(err, errlen, "out of memory"); goto out; }
        for (uint32_t v = 0; v <= maxin; v++) {
            double t = (v - lo[c]) / (hi[c] - lo[c]);
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            if (negative) t = 1 - t;
            lut[c][v] = (uint16_t)lround(maxout * pow(t, 1 / gamma));
        }
    }

    /* 2. write TIFF: header, IFD, then the converted rows */
    fo = fopen(tiff, "wb");
    if (!fo) { snprintf(err, errlen, "can't create %s", tiff); goto out; }
    if (header) write_tiff_header(fo, w, h, spp, out_bits, dpi);
    for (int y = 0; y < h; y++) {
        if (fread(ibuf, 1, in_row, fi) != in_row) { snprintf(err, errlen, "scan data is shorter than expected"); goto out; }
        for (size_t k = 0; k < (size_t)w * spp; k++) {
            int c = (int)(k % spp);
            uint32_t v = in_bits == 16 ? ((uint16_t *)ibuf)[k] : ibuf[k];
            uint16_t o = lut[c][v];
            if (out_bits == 16) ((uint16_t *)obuf)[k] = o;
            else obuf[k] = (uint8_t)o;
        }
        if (fwrite(obuf, 1, out_row, fo) != out_row) { snprintf(err, errlen, "disk write failed"); goto out; }
    }
    rc = 0;
out:
    if (fi) fclose(fi);
    if (fo && fclose(fo)) rc = -1;
    free(ibuf); free(obuf); free(hist);
    for (int c = 0; c < 3; c++) free(lut[c]);
    return rc;
}

int es_apply_matrix_raw(const char *raw, int w, int h, int bits16, const double coef[9], char *err, size_t errlen)
{
    FILE *f = fopen(raw, "r+b");
    size_t row = (size_t)w * 3 * (bits16 ? 2 : 1);
    uint8_t *buf = malloc(row);
    int rc = -1;
    double mx = bits16 ? 65535.0 : 255.0;
    if (!f || !buf) { snprintf(err, errlen, "can't open the image for colour correction"); goto out; }
    for (int y = 0; y < h; y++) {
        long pos = (long)y * (long)row;
        if (fseek(f, pos, SEEK_SET) || fread(buf, 1, row, f) != row) { snprintf(err, errlen, "read failed"); goto out; }
        for (int x = 0; x < w; x++) {
            double v[3];
            for (int c = 0; c < 3; c++) v[c] = bits16 ? ((uint16_t *)buf)[x * 3 + c] : buf[x * 3 + c];
            for (int c = 0; c < 3; c++) {
                double r = coef[c * 3] * v[0] + coef[c * 3 + 1] * v[1] + coef[c * 3 + 2] * v[2] + 0.5;
                r = r < 0 ? 0 : r > mx ? mx : r;
                if (bits16) ((uint16_t *)buf)[x * 3 + c] = (uint16_t)r; else buf[x * 3 + c] = (uint8_t)r;
            }
        }
        if (fseek(f, pos, SEEK_SET) || fwrite(buf, 1, row, f) != row) { snprintf(err, errlen, "write failed"); goto out; }
    }
    rc = 0;
out:
    if (f && fclose(f)) rc = -1;
    free(buf);
    return rc;
}

/* Black & White and Dropout (cScanner::Mono_Extract_Fr_Color / cScanner::Binarization in Epson's
 * interpreter): one channel of a colour scan (dropout), or the gray scan, encoded with the screen
 * gamma; Black & White then keeps "value >= threshold" as white. Output: 8-bit gray, or 1-bit rows
 * (MSB first, 1 = black, as a WhiteIsZero TIFF stores them). */
int es_mono_convert(const char *raw, const char *out, int w, int h, int spp, int in_bits, int channel,
                    double gamma, int bw, int threshold, int negative, char *err, size_t errlen)
{
    FILE *fi = fopen(raw, "rb"), *fo = fopen(out, "wb");
    size_t ib = in_bits > 8 ? 2 : 1, in_row = (size_t)w * spp * ib;
    size_t out_row = bw ? (size_t)(w + 7) / 8 : (size_t)w;
    uint8_t *ibuf = malloc(in_row), *obuf = malloc(out_row);
    uint8_t lut8[256];
    uint8_t *lut16 = in_bits > 8 ? malloc(65536) : NULL;
    int rc = -1;
    if (!fi || !fo || !ibuf || !obuf || (in_bits > 8 && !lut16)) { snprintf(err, errlen, "can't convert the scan"); goto out; }
    for (int v = 0; v < 256; v++) {
        double t = v / 255.0; if (negative) t = 1 - t;
        lut8[v] = (uint8_t)lround(255 * pow(t, 1 / gamma));
    }
    if (lut16) for (int v = 0; v < 65536; v++) {
        double t = v / 65535.0; if (negative) t = 1 - t;
        lut16[v] = (uint8_t)lround(255 * pow(t, 1 / gamma));
    }
    int ch = spp == 3 ? (channel >= 0 && channel < 3 ? channel : 1) : 0;
    for (int y = 0; y < h; y++) {
        if (fread(ibuf, 1, in_row, fi) != in_row) { snprintf(err, errlen, "scan data is shorter than expected"); goto out; }
        if (bw) memset(obuf, 0, out_row);
        for (int x = 0; x < w; x++) {
            size_t k = (size_t)x * spp + ch;
            uint8_t v = lut16 ? lut16[((uint16_t *)ibuf)[k]] : lut8[ibuf[k]];
            if (!bw) obuf[x] = v;
            else if (v < threshold) obuf[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
        }
        if (fwrite(obuf, 1, out_row, fo) != out_row) { snprintf(err, errlen, "disk write failed"); goto out; }
    }
    rc = 0;
out:
    if (fi) fclose(fi);
    if (fo && fclose(fo) != 0 && rc == 0) { snprintf(err, errlen, "disk write failed"); rc = -1; }
    free(ibuf); free(obuf); free(lut16);
    return rc;
}
