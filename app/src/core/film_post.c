/*
 * Film post-processing helpers: line up the infrared pass with the color
 * pass, and write 16-bit raw data out as TIFF.
 *
 * The V550 scans color and infrared in two separate passes, and the carriage
 * doesn't land in exactly the same place twice (about 2 pixels at 1600 dpi and
 * 6-8 pixels at 3200 dpi on the test scans). Digital ICE needs the two to line up, so we
 * measure the offset and shift the IR before handing both to ICE.
 */
#include "film_post.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>

#define BAND   1536       /* rows used for the measurement */
#define COARSE 4          /* first pass works on 4x4-averaged images ...            */
#define CRANGE 8          /* ... searching +/-8 there (= +/-32 pixels at full size) */
#define FRANGE 3          /* second pass: +/-3 pixels around that, at full size    */

static int read_rows(FILE *f, long row_bytes, int y0, int rows, void *dst)
{
    if (fseek(f, (long)y0 * row_bytes, SEEK_SET)) return -1;
    return fread(dst, (size_t)row_bytes, (size_t)rows, f) == (size_t)rows ? 0 : -1;
}

/* Green-channel detail in a band of rows (sampled), used to pick where to measure. */
static double band_detail(FILE *fr, int w, int y0, int rows, uint16_t *row)
{
    double sum = 0;
    for (int y = y0; y < y0 + rows; y += 16) {
        if (read_rows(fr, (long)w * 6, y, 1, row)) return 0;
        for (int x = 3; x < w - 3; x += 2) {
            int d = (int)row[(x + 2) * 3 + 1] - (int)row[(x - 2) * 3 + 1];
            sum += d < 0 ? -d : d;
        }
    }
    return sum;
}

/* Box-average a 16-bit plane by f, then turn it into "darkness relative to the
 * surroundings" (local mean over a 9x9 box minus the pixel, divided by the local
 * mean). Dust and hairs are dark spots in both the color and infrared passes, so
 * this lines them up sharply; smooth picture content mostly cancels out. */
#define HP_R 4
static float *detail_full(const uint16_t *src, int stride, int ch, int w, int h);

static float *detail_plane(const uint16_t *src, int stride, int ch, int w, int h, int f, int *ow, int *oh)
{
    *ow = w / f; *oh = h / f;
    float *g = detail_full(src, stride, ch, w, h);
    if (!g || f == 1) return g;
    /* shrink AFTER measuring, keeping each block's strongest dark spot, so thin hairs survive */
    int W = w / f, H = h / f;
    float *o = malloc((size_t)W * H * sizeof(float));
    if (!o) { free(g); return NULL; }
    double m = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float v = -1e30f;
            for (int j = 0; j < f; j++)
                for (int i = 0; i < f; i++) {
                    float t = g[(size_t)(y * f + j) * w + x * f + i];
                    if (t > v) v = t;
                }
            o[(size_t)y * W + x] = v;
            m += v;
        }
    m /= (double)W * H;
    for (size_t i = 0; i < (size_t)W * H; i++) o[i] -= (float)m;
    free(g);
    return o;
}

static float *detail_full(const uint16_t *src, int stride, int ch, int w, int h)
{
    int f = 1, W = w, H = h;
    float *a = malloc((size_t)W * H * sizeof(float)), *g = calloc((size_t)W * H, sizeof(float));
    double *ii = malloc((size_t)(W + 1) * (H + 1) * sizeof(double));
    if (!a || !g || !ii) { free(a); free(g); free(ii); return NULL; }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float s = 0;
            for (int j = 0; j < f; j++)
                for (int i = 0; i < f; i++)
                    s += src[(size_t)(y * f + j) * stride + (size_t)(x * f + i) * (stride / w) + ch];
            a[(size_t)y * W + x] = s / (f * f);
        }
    /* integral image for the box mean */
    for (int x = 0; x <= W; x++) ii[x] = 0;
    for (int y = 0; y < H; y++) {
        double run = 0;
        ii[(size_t)(y + 1) * (W + 1)] = 0;
        for (int x = 0; x < W; x++) {
            run += a[(size_t)y * W + x];
            ii[(size_t)(y + 1) * (W + 1) + x + 1] = ii[(size_t)y * (W + 1) + x + 1] + run;
        }
    }
    for (int y = HP_R; y < H - HP_R; y++)
        for (int x = HP_R; x < W - HP_R; x++) {
            int x0 = x - HP_R, x1 = x + HP_R + 1, y0 = y - HP_R, y1 = y + HP_R + 1;
            double sum = ii[(size_t)y1 * (W + 1) + x1] - ii[(size_t)y0 * (W + 1) + x1]
                       - ii[(size_t)y1 * (W + 1) + x0] + ii[(size_t)y0 * (W + 1) + x0];
            double mean = sum / ((2 * HP_R + 1) * (2 * HP_R + 1));
            double d = mean > 1 ? (mean - a[(size_t)y * W + x]) / mean : 0;
            g[(size_t)y * W + x] = d > 0 ? (float)d : 0;      /* only darker-than-surroundings */
        }
    double m = 0;
    for (size_t i = 0; i < (size_t)W * H; i++) m += g[i];
    m /= (double)W * H;
    for (size_t i = 0; i < (size_t)W * H; i++) g[i] -= (float)m;
    free(a); free(ii);
    return g;
}

/* Add correlation scores for shifts (cy +/- r, cx +/- r) of plane I against G. */
static void correlate(const float *G, const float *I, int W, int H, int cy, int cx, int r, int step, double *score)
{
    int m = (cy < 0 ? -cy : cy) + (cx < 0 ? -cx : cx) + r + 2, k = 0;
    for (int sy = cy - r; sy <= cy + r; sy++)
        for (int sx = cx - r; sx <= cx + r; sx++, k++) {
            double sum = 0;
            for (int y = m; y < H - m; y += step) {
                const float *g = G + (size_t)y * W;
                const float *i = I + (size_t)(y - sy) * W - sx;
                for (int x = m; x < W - m; x += step) sum += (double)g[x] * i[x];
            }
            score[k] += sum;
        }
}

static int best_of(const double *score, int r, int cy, int cx, int *dy, int *dx)
{
    int n = 2 * r + 1, bestk = 0;
    for (int k = 1; k < n * n; k++) if (score[k] > score[bestk]) bestk = k;
    *dy = cy + bestk / n - r;
    *dx = cx + bestk % n - r;
    return bestk;
}

int ir_estimate_shift(const char *rgb_raw, const char *ir_raw, int w, int h, int *dy, int *dx)
{
    *dy = *dx = 0;
    FILE *fr = fopen(rgb_raw, "rb"), *fi = fopen(ir_raw, "rb");
    uint16_t *row = malloc((size_t)w * 6);
    uint16_t *rgb = NULL, *ir = NULL;
    int rc = -1;
    if (!fr || !fi || !row) goto out;

    /* Split the scan into bands and measure in the two with the most detail
     * (film-holder bars and empty slots have almost none). */
    int rows = h < BAND ? h : BAND;
    int nb = h / rows;
    if (nb < 1) nb = 1;
    double best[2] = { -1, -1 };
    int pick[2] = { 0, 0 };
    for (int b = 0; b < nb; b++) {
        double d = band_detail(fr, w, b * rows, rows, row);
        if (d > best[0]) { best[1] = best[0]; pick[1] = pick[0]; best[0] = d; pick[0] = b; }
        else if (d > best[1]) { best[1] = d; pick[1] = b; }
    }
    int bands = nb > 1 ? 2 : 1;
    rgb = malloc((size_t)w * rows * 6); ir = malloc((size_t)w * rows * 2);
    if (!rgb || !ir) goto out;

    /* coarse: 4x4-averaged images, wide search */
    double cs[(2 * CRANGE + 1) * (2 * CRANGE + 1)] = { 0 };
    for (int i = 0; i < bands; i++) {
        if (read_rows(fr, (long)w * 6, pick[i] * rows, rows, rgb) || read_rows(fi, (long)w * 2, pick[i] * rows, rows, ir)) goto out;
        int W, H;
        float *G = detail_plane(rgb, w * 3, 1, w, rows, COARSE, &W, &H), *I = detail_plane(ir, w, 0, w, rows, COARSE, &W, &H);
        if (G && I) correlate(G, I, W, H, 0, 0, CRANGE, 1, cs);
        free(G); free(I);
        if (!G || !I) goto out;
    }
    int cy, cx;
    best_of(cs, CRANGE, 0, 0, &cy, &cx);

    /* fine: full size, small search around the coarse answer */
    double fs[(2 * FRANGE + 1) * (2 * FRANGE + 1)] = { 0 };
    for (int i = 0; i < bands; i++) {
        if (read_rows(fr, (long)w * 6, pick[i] * rows, rows, rgb) || read_rows(fi, (long)w * 2, pick[i] * rows, rows, ir)) goto out;
        int W, H;
        float *G = detail_plane(rgb, w * 3, 1, w, rows, 1, &W, &H), *I = detail_plane(ir, w, 0, w, rows, 1, &W, &H);
        if (G && I) correlate(G, I, W, H, cy * COARSE, cx * COARSE, FRANGE, 2, fs);
        free(G); free(I);
        if (!G || !I) goto out;
    }
    best_of(fs, FRANGE, cy * COARSE, cx * COARSE, dy, dx);
    rc = 0;
out:
    if (fr) fclose(fr);
    if (fi) fclose(fi);
    free(row); free(rgb); free(ir);
    return rc;
}

/* ---------------------------------------------------------------------- */
/* Local alignment. The infrared image isn't just shifted: it is also very
 * slightly larger or smaller than the color image (the lens focuses infrared
 * light a little differently), so the offset changes across the frame. We
 * measure the offset in a grid of tiles and fit
 *     dx = a0 + a1*x + a2*y,   dy = b0 + b1*x + b2*y
 * then resample the infrared through that map. */

#define TILE  384
#define TRANGE 5

/* correlate one tile (band coordinates) for shifts cy/cx +/- TRANGE */
static void correlate_tile(const float *G, const float *I, int W, int x0, int x1, int y0, int y1,
                           int cy, int cx, double *score)
{
    int k = 0;
    for (int sy = cy - TRANGE; sy <= cy + TRANGE; sy++)
        for (int sx = cx - TRANGE; sx <= cx + TRANGE; sx++, k++) {
            double sum = 0;
            for (int y = y0; y < y1; y += 2) {
                const float *g = G + (size_t)y * W;
                const float *i = I + (size_t)(y - sy) * W - sx;
                for (int x = x0; x < x1; x += 2) sum += (double)g[x] * i[x];
            }
            score[k] = sum;
        }
}

static int solve3(double A[3][3], double b[3], double out[3])
{
    double m[3][4];
    for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) m[i][j] = A[i][j]; m[i][3] = b[i]; }
    for (int c = 0; c < 3; c++) {
        int p = c;
        for (int r = c + 1; r < 3; r++) if (fabs(m[r][c]) > fabs(m[p][c])) p = r;
        if (fabs(m[p][c]) < 1e-12) return -1;
        for (int j = 0; j < 4; j++) { double t = m[c][j]; m[c][j] = m[p][j]; m[p][j] = t; }
        for (int r = 0; r < 3; r++) if (r != c) {
            double f = m[r][c] / m[c][c];
            for (int j = c; j < 4; j++) m[r][j] -= f * m[c][j];
        }
    }
    for (int i = 0; i < 3; i++) out[i] = m[i][3] / m[i][i];
    return 0;
}

typedef struct { double x, y, dx, dy, wgt; int use; } TileShift;

static int fit_plane(TileShift *t, int n, int which, double coef[3])
{
    double A[3][3] = { { 0 } }, b[3] = { 0 };
    int used = 0;
    for (int i = 0; i < n; i++) {
        if (!t[i].use) continue;
        double v[3] = { 1, t[i].x, t[i].y }, d = which ? t[i].dy : t[i].dx;
        for (int r = 0; r < 3; r++) { b[r] += t[i].wgt * v[r] * d; for (int c = 0; c < 3; c++) A[r][c] += t[i].wgt * v[r] * v[c]; }
        used++;
    }
    if (used < 6) return -1;
    return solve3(A, b, coef);
}

int ir_measure_map(const char *rgb_raw, const char *ir_raw, int w, int h, int gdy, int gdx,
                   IRMap *map, char *report, size_t replen)
{
    FILE *fr = fopen(rgb_raw, "rb"), *fi = fopen(ir_raw, "rb");
    int M = (abs(gdy) > abs(gdx) ? abs(gdy) : abs(gdx)) + TRANGE + 3;
    int nty = h / TILE, ntx = w / TILE, rc = -1, n = 0;
    TileShift *ts = calloc((size_t)(nty > 0 ? nty : 1) * (ntx > 0 ? ntx : 1), sizeof *ts);
    uint16_t *rgb = malloc((size_t)w * (TILE + 2 * M) * 6), *ir = malloc((size_t)w * (TILE + 2 * M) * 2);
    map->ax[0] = gdx; map->ax[1] = map->ax[2] = 0;
    map->ay[0] = gdy; map->ay[1] = map->ay[2] = 0;
    if (report && replen) report[0] = 0;
    if (!fr || !fi || !ts || !rgb || !ir || nty < 1 || ntx < 1) goto out;

    for (int ty = 0; ty < nty; ty++) {
        int y0 = ty * TILE - M, y1 = ty * TILE + TILE + M;
        if (y0 < 0) y0 = 0;
        if (y1 > h) y1 = h;
        int rows = y1 - y0;
        if (read_rows(fr, (long)w * 6, y0, rows, rgb) || read_rows(fi, (long)w * 2, y0, rows, ir)) goto out;
        int W, H;
        float *G = detail_plane(rgb, w * 3, 1, w, rows, 1, &W, &H), *I = detail_plane(ir, w, 0, w, rows, 1, &W, &H);
        if (!G || !I) { free(G); free(I); goto out; }
        for (int tx = 0; tx < ntx; tx++) {
            int ya = ty * TILE - y0, yb = ya + TILE, xa = tx * TILE, xb = xa + TILE;
            if (ya < M) ya = M;
            if (yb > H - M) yb = H - M;
            if (xa < M) xa = M;
            if (xb > W - M) xb = W - M;
            if (yb - ya < TILE / 2 || xb - xa < TILE / 2) continue;
            double sc[(2 * TRANGE + 1) * (2 * TRANGE + 1)];
            correlate_tile(G, I, W, xa, xb, ya, yb, gdy, gdx, sc);
            int N = 2 * TRANGE + 1, bk = 0;
            for (int k = 0; k < N * N; k++) if (sc[k] > sc[bk]) bk = k;
            int by = bk / N, bx = bk % N;
            /* confidence: how far the peak stands above the scores well away from it
             * (a tile with no dust gives a flat field with a random maximum) */
            double mean = 0, var = 0;
            int nf = 0;
            for (int k = 0; k < N * N; k++) {
                int ky = k / N, kx = k % N;
                if (abs(ky - by) < 3 && abs(kx - bx) < 3) continue;
                mean += sc[k]; nf++;
            }
            if (nf) mean /= nf;
            for (int k = 0; k < N * N; k++) {
                int ky = k / N, kx = k % N;
                if (abs(ky - by) < 3 && abs(kx - bx) < 3) continue;
                var += (sc[k] - mean) * (sc[k] - mean);
            }
            double sd = nf ? sqrt(var / nf) : 0, z = sd > 0 ? (sc[bk] - mean) / sd : 0;
            if (getenv("IRDEBUG")) fprintf(stderr, "tile %d,%d z=%.2f peak=%d,%d\n", tx, ty, z, by - TRANGE, bx - TRANGE);
            if (by == 0 || bx == 0 || by == N - 1 || bx == N - 1 || z < 7.0) continue;
            /* sub-pixel peak */
            double l = sc[bk - 1], r = sc[bk + 1], u = sc[bk - N], d = sc[bk + N], c = sc[bk];
            double fx = (l - 2 * c + r) != 0 ? 0.5 * (l - r) / (l - 2 * c + r) : 0;
            double fy = (u - 2 * c + d) != 0 ? 0.5 * (u - d) / (u - 2 * c + d) : 0;
            if (fx > 0.5) fx = 0.5; if (fx < -0.5) fx = -0.5;
            if (fy > 0.5) fy = 0.5; if (fy < -0.5) fy = -0.5;
            TileShift *t = &ts[n++];
            t->x = (tx + 0.5) * TILE; t->y = (ty + 0.5) * TILE;
            t->dx = gdx + bx - TRANGE + fx; t->dy = gdy + by - TRANGE + fy;
            t->wgt = z; t->use = 1;
        }
        free(G); free(I);
    }

    /* robust fit: refit after dropping tiles more than 1.5 px off */
    double cx[3], cy[3];
    int ok = 0;
    for (int it = 0; it < 4; it++) {
        if (fit_plane(ts, n, 0, cx) || fit_plane(ts, n, 1, cy)) { ok = 0; break; }
        ok = 1;
        int dropped = 0;
        for (int i = 0; i < n; i++) {
            if (!ts[i].use) continue;
            double ex = cx[0] + cx[1] * ts[i].x + cx[2] * ts[i].y - ts[i].dx;
            double ey = cy[0] + cy[1] * ts[i].x + cy[2] * ts[i].y - ts[i].dy;
            if (ex * ex + ey * ey > 1.5 * 1.5) { ts[i].use = 0; dropped++; }
        }
        if (!dropped) break;
    }
    int used = 0;
    for (int i = 0; i < n; i++) used += ts[i].use;
    if (ok) {
        for (int i = 0; i < 3; i++) { map->ax[i] = cx[i]; map->ay[i] = cy[i]; }
        rc = 0;
    }
    if (report && replen)
        snprintf(report, replen,
                 "%d of %d tiles measured, %d used; dx %.2f..%.2f, dy %.2f..%.2f across the frame%s",
                 n, nty * ntx, used,
                 map->ax[0], map->ax[0] + map->ax[1] * w + map->ax[2] * h,
                 map->ay[0], map->ay[0] + map->ay[1] * w + map->ay[2] * h,
                 ok ? "" : " (too few tiles: using the single shift)");
out:
    if (fr) fclose(fr);
    if (fi) fclose(fi);
    free(ts); free(rgb); free(ir);
    return rc;
}

/* aligned(x,y) = ir(x - dx(x,y), y - dy(x,y)), bilinear, edges repeated */
int ir_warp_file(const char *ir_in, const char *ir_out, int w, int h, const IRMap *m)
{
    FILE *fi = fopen(ir_in, "rb"), *fo = fopen(ir_out, "wb");
    uint16_t *r0 = malloc((size_t)w * 2), *r1 = malloc((size_t)w * 2), *dst = malloc((size_t)w * 2);
    int rc = -1, have0 = -1, have1 = -1;
    if (!fi || !fo || !r0 || !r1 || !dst) goto out;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double sx = x - (m->ax[0] + m->ax[1] * x + m->ax[2] * y);
            double sy = y - (m->ay[0] + m->ay[1] * x + m->ay[2] * y);
            if (sx < 0) sx = 0;
            if (sx > w - 1) sx = w - 1;
            if (sy < 0) sy = 0;
            if (sy > h - 1) sy = h - 1;
            int iy = (int)sy, ix = (int)sx;
            int iy1 = iy + 1 < h ? iy + 1 : iy, ix1 = ix + 1 < w ? ix + 1 : ix;
            /* rows needed: iy, iy1 (cache two rows; the map changes slowly so this rarely misses) */
            if (have0 != iy) {
                if (have1 == iy) { uint16_t *t = r0; r0 = r1; r1 = t; have0 = iy; have1 = -1; }
                else { if (read_rows(fi, (long)w * 2, iy, 1, r0)) goto out; have0 = iy; }
            }
            if (have1 != iy1) { if (read_rows(fi, (long)w * 2, iy1, 1, r1)) goto out; have1 = iy1; }
            double fx = sx - ix, fy = sy - iy;
            double v = (1 - fy) * ((1 - fx) * r0[ix] + fx * r0[ix1]) + fy * ((1 - fx) * r1[ix] + fx * r1[ix1]);
            dst[x] = (uint16_t)(v + 0.5);
        }
        if (fwrite(dst, 2, (size_t)w, fo) != (size_t)w) goto out;
    }
    rc = 0;
out:
    if (fi) fclose(fi);
    if (fo && fclose(fo)) rc = -1;
    free(r0); free(r1); free(dst);
    return rc;
}

/* out[y][x] = in[y - dy][x - dx], edges repeated */
int ir_shift_file(const char *ir_in, const char *ir_out, int w, int h, int dy, int dx)
{
    FILE *fi = fopen(ir_in, "rb"), *fo = fopen(ir_out, "wb");
    uint16_t *src = malloc((size_t)w * 2), *dst = malloc((size_t)w * 2);
    int rc = -1;
    if (!fi || !fo || !src || !dst) goto out;
    for (int y = 0; y < h; y++) {
        int sy = y - dy;
        if (sy < 0) sy = 0;
        if (sy >= h) sy = h - 1;
        if (read_rows(fi, (long)w * 2, sy, 1, src)) goto out;
        for (int x = 0; x < w; x++) {
            int sx = x - dx;
            if (sx < 0) sx = 0;
            if (sx >= w) sx = w - 1;
            dst[x] = src[sx];
        }
        if (fwrite(dst, 2, (size_t)w, fo) != (size_t)w) goto out;
    }
    rc = 0;
out:
    if (fi) fclose(fi);
    if (fo && fclose(fo)) rc = -1;
    free(src); free(dst);
    return rc;
}

/* ---- TIFF ---------------------------------------------------------------- */

static void w16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }
static void w32(FILE *f, uint32_t v) { w16(f, v & 0xFFFF); w16(f, v >> 16); }
static void entry(FILE *f, uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
{
    w16(f, tag); w16(f, type); w32(f, count);
    if (type == 3 && count == 1) { w16(f, (uint16_t)value); w16(f, 0); }
    else w32(f, value);
}

int raw16_to_tiff(const char *raw, const char *tiff, int w, int h, int spp, int dpi)
{
    FILE *fi = fopen(raw, "rb"), *fo = fopen(tiff, "wb");
    size_t row = (size_t)w * spp * 2;
    uint8_t *buf = malloc(row);
    int rc = -1;
    if (!fi || !fo || !buf) goto out;
    w16(fo, 0x4949); w16(fo, 42); w32(fo, 8);
    /* IFD right after the header, data after the IFD and its extras */
    const uint16_t n = 13;
    uint32_t ifd_end = 8 + 2 + n * 12 + 4;
    uint32_t bps_off = ifd_end, xres = bps_off + (spp > 1 ? spp * 2 : 0), yres = xres + 8;
    uint32_t data = yres + 8;
    w16(fo, n);
    entry(fo, 256, 4, 1, (uint32_t)w);
    entry(fo, 257, 4, 1, (uint32_t)h);
    entry(fo, 258, 3, (uint32_t)spp, spp > 1 ? bps_off : 16);
    entry(fo, 259, 3, 1, 1);
    entry(fo, 262, 3, 1, spp == 3 ? 2 : 1);
    entry(fo, 273, 4, 1, data);
    entry(fo, 277, 3, 1, (uint32_t)spp);
    entry(fo, 278, 4, 1, (uint32_t)h);
    entry(fo, 279, 4, 1, (uint32_t)(row * (size_t)h));
    entry(fo, 282, 5, 1, xres);
    entry(fo, 283, 5, 1, yres);
    entry(fo, 284, 3, 1, 1);
    entry(fo, 296, 3, 1, 2);
    w32(fo, 0);
    if (spp > 1) for (int i = 0; i < spp; i++) w16(fo, 16);
    w32(fo, (uint32_t)dpi); w32(fo, 1);
    w32(fo, (uint32_t)dpi); w32(fo, 1);
    for (int y = 0; y < h; y++) {
        if (fread(buf, 1, row, fi) != row) goto out;
        if (fwrite(buf, 1, row, fo) != row) goto out;
    }
    rc = 0;
out:
    if (fi) fclose(fi);
    if (fo && fclose(fo)) rc = -1;
    free(buf);
    return rc;
}

/* ---- frame detection ------------------------------------------------------ */

#define OPEN_LEVEL   1000     /* film-holder bars read ~30-40; film reads thousands */
#define CLEAR_LEVEL  65000    /* an empty slot is mostly saturated */

static int read_row(FILE *f, int w, int y, uint16_t *row)
{
    return read_rows(f, (long)w * 6, y, 1, row);
}

int detect_frames(const char *rgb_raw, int w, int h, int dpi, FrameRect *out, int max)
{
    FILE *f = fopen(rgb_raw, "rb");
    uint16_t *row = malloc((size_t)w * 6);
    unsigned char *open = calloc((size_t)h, 1);
    int n = 0;
    if (!f || !row || !open) goto out;

    /* 1. rows where light comes through (film or an empty slot) */
    for (int y = 0; y < h; y++) {
        if (read_row(f, w, y, row)) goto out;
        double sum = 0;
        int cnt = 0;
        for (int x = 0; x < w; x += 4, cnt++) sum += row[x * 3 + 1];
        open[y] = sum / cnt > OPEN_LEVEL;
    }
    int min_len = dpi * 35 / 100;            /* ignore anything shorter than ~0.35" */
    for (int y = 0; y < h && n < max;) {
        if (!open[y]) { y++; continue; }
        int y0 = y;
        while (y < h && open[y]) y++;
        int y1 = y;                          /* [y0, y1) */
        if (y1 - y0 < min_len) continue;

        /* 2. columns with light, and how much of the run is saturated (empty slot) */
        double *col = calloc((size_t)w, sizeof(double));
        long sat = 0, tot = 0;
        int step = (y1 - y0) / 200 + 1, rows = 0;
        for (int yy = y0; yy < y1; yy += step, rows++) {
            if (read_row(f, w, yy, row)) { free(col); goto out; }
            for (int x = 0; x < w; x++) {
                uint16_t g = row[x * 3 + 1];
                col[x] += g;
                if (x % 4 == 0) { tot++; if (g >= CLEAR_LEVEL) sat++; }
            }
        }
        int x0 = 0, x1 = w;
        while (x0 < w && col[x0] / rows <= OPEN_LEVEL) x0++;
        while (x1 > x0 && col[x1 - 1] / rows <= OPEN_LEVEL) x1--;
        free(col);
        if (x1 - x0 < min_len) continue;
        if (sat * 2 > tot) continue;         /* empty slot, no film */

        int inset = dpi / 200 + 2;           /* stay clear of the soft holder edge */
        FrameRect r = { x0 + inset, y0 + inset, (x1 - x0) - 2 * inset, (y1 - y0) - 2 * inset };
        if (r.w < 256 || r.h < 256) continue; /* ICE needs at least 256 px */
        out[n++] = r;
    }
out:
    if (f) fclose(f);
    free(row);
    free(open);
    return n;
}

int crop_raw(const char *in, const char *outp, int w, int bpp, FrameRect r)
{
    FILE *fi = fopen(in, "rb"), *fo = fopen(outp, "wb");
    uint8_t *buf = malloc((size_t)r.w * bpp);
    int rc = -1;
    if (!fi || !fo || !buf) goto out;
    for (int y = r.y; y < r.y + r.h; y++) {
        if (fseek(fi, ((long)y * w + r.x) * bpp, SEEK_SET)) goto out;
        if (fread(buf, (size_t)bpp, (size_t)r.w, fi) != (size_t)r.w) goto out;
        if (fwrite(buf, (size_t)bpp, (size_t)r.w, fo) != (size_t)r.w) goto out;
    }
    rc = 0;
out:
    if (fi) fclose(fi);
    if (fo && fclose(fo)) rc = -1;
    free(buf);
    return rc;
}

/* ---- full post-scan pipeline --------------------------------------------- */

#include "ice_film.h"
#include <time.h>

static int print_progress(int pct, void *ctx)
{
    fprintf(stderr, "\r  Digital ICE, frame %d: %3d%%", *(int *)ctx, pct);
    return 0;
}

/*
 * Epson Scan ran ICE on each selected frame, never on a whole strip: ICE
 * calibrates itself from the image it's given, and black holder bars and
 * empty slots throw that off (it then over-corrects in blocks). So: align
 * the infrared once for the whole strip, find the frames, and run ICE on
 * each frame separately.
 */
int film_ice_pipeline(const char *engine, const char *rgb_raw, const char *ir_raw, const char *out_base,
                      int w, int h, int dpi, int keep_temp, char *err, size_t errlen)
{
    char ir_aligned[1100], c_rgb[1100], c_ir[1100], c_out[1100], tif[1100];
    snprintf(ir_aligned, sizeof ir_aligned, "%s.aligned", ir_raw);
    snprintf(c_rgb, sizeof c_rgb, "%s.frame", rgb_raw);
    snprintf(c_ir, sizeof c_ir, "%s.frame", ir_raw);
    snprintf(c_out, sizeof c_out, "%s.ice", rgb_raw);
    int dy = 0, dx = 0, rc = -1;

    if (ir_estimate_shift(rgb_raw, ir_raw, w, h, &dy, &dx)) {
        snprintf(err, errlen, "couldn't measure the infrared alignment");
        return -1;
    }
    IRMap map;
    char rep[256];
    ir_measure_map(rgb_raw, ir_raw, w, h, dy, dx, &map, rep, sizeof rep);
    printf("Infrared pass offset: about %d row(s), %d pixel(s); local alignment: %s\n", dy, dx, rep);
    if (ir_warp_file(ir_raw, ir_aligned, w, h, &map)) {
        snprintf(err, errlen, "couldn't write the aligned infrared file");
        return -1;
    }

    FrameRect frames[32];
    int nf = detect_frames(rgb_raw, w, h, dpi, frames, 32);
    if (nf == 0) {
        snprintf(err, errlen, "no film frames found in the scan (is film loaded in the holder?)");
        goto out;
    }
    printf("Found %d frame%s\n", nf, nf == 1 ? "" : "s");

    for (int i = 0; i < nf; i++) {
        FrameRect r = frames[i];
        int num = i + 1;
        if (nf == 1) snprintf(tif, sizeof tif, "%s.tif", out_base);
        else snprintf(tif, sizeof tif, "%s_%d.tif", out_base, num);
        if (crop_raw(rgb_raw, c_rgb, w, 6, r) || crop_raw(ir_aligned, c_ir, w, 2, r)) {
            snprintf(err, errlen, "couldn't cut out frame %d", num);
            goto out;
        }
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        char ierr[256] = "";
        int ri = ice_film(engine, c_rgb, c_ir, c_out, r.w, r.h, dpi, 2, 9, print_progress, &num,
                          ierr, sizeof ierr);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        fprintf(stderr, "\n");
        if (ri) {
            snprintf(err, errlen, "frame %d: %s", num, ierr);
            goto out;
        }
        if (raw16_to_tiff(c_out, tif, r.w, r.h, 3, dpi)) {
            snprintf(err, errlen, "couldn't write %s", tif);
            goto out;
        }
        printf("Frame %d: %d x %d px at (%d, %d), Digital ICE %.1f s -> %s\n", num, r.w, r.h, r.x, r.y,
               (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9, tif);
    }
    rc = 0;
out:
    remove(c_rgb);
    remove(c_ir);
    remove(c_out);
    if (!keep_temp) remove(ir_aligned);
    return rc;
}
