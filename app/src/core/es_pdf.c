#include "es_pdf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <time.h>
#include "../libjpeg/jpeglib.h"
#include "es_pdf_tables.h"
#include "es_g4.h"
#include "hc.h"

/* ------------------------------------------------------------- JPEG (CJpegAPI) */

struct pdf_jerr { struct jpeg_error_mgr pub; jmp_buf jb; char msg[JMSG_LENGTH_MAX]; };
static void pdf_jerror_exit(j_common_ptr c)
{
    struct pdf_jerr *e = (struct pdf_jerr *)c->err;
    (*c->err->format_message)(c, e->msg);
    longjmp(e->jb, 1);
}

/* _demo_jpeg_start: compression rate 1 (Standard) -> quality table 9, sampling pattern 0
 * (4:4:4); gray (0x403) -> one component. PDFLib's FDCT is the floating-point one. */
/* _demo_jpeg_start's rate -> quality table and sampling pattern (0 4:4:4, 2 4:2:2, 3 4:2:0) */
static const int kRateTable[6] = { 0, 9, 6, 5, 4, 4 }, kRateSampling[6] = { 0, 0, 0, 0, 2, 3 };

static int encode_jpeg_mem(const uint8_t *pix, int w, int h, int spp, int rate, unsigned char **out,
                           unsigned long *outlen, char *err, size_t errlen);

static int encode_jpeg(const EsPdfPage *p, int high, unsigned char **out, unsigned long *outlen,
                       char *err, size_t errlen)
{
    FILE *fi = fopen(p->raw, "rb");
    if (!fi) { snprintf(err, errlen, "can't read the scan data"); return 0; }
    int wide = p->bits > 8, mono = p->bits == 1;
    size_t in_row = mono ? (size_t)(p->w + 7) / 8 : (size_t)p->w * p->spp * (wide ? 2 : 1);
    struct jpeg_compress_struct c;
    struct pdf_jerr je;
    uint8_t *volatile ibuf = malloc(in_row);
    JSAMPLE *volatile obuf = malloc((size_t)p->w * p->spp);
    volatile int ok = 0;
    *out = NULL; *outlen = 0;
    c.err = jpeg_std_error(&je.pub);
    je.pub.error_exit = pdf_jerror_exit;
    if (setjmp(je.jb)) { snprintf(err, errlen, "JPEG error: %s", je.msg); goto done; }
    jpeg_create_compress(&c);
    if (!ibuf || !obuf) { snprintf(err, errlen, "out of memory"); goto done; }
    jpeg_mem_dest(&c, out, outlen);
    c.image_width = (JDIMENSION)p->w;
    c.image_height = (JDIMENSION)p->h;
    c.input_components = p->spp;
    c.in_color_space = p->spp == 1 ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&c);
    c.do_fancy_downsampling = FALSE;
    c.dct_method = JDCT_FLOAT;
    c.write_JFIF_header = FALSE;          /* PDFLib writes its own APP marker, not needed in a PDF */
    int rate = high ? 3 : 1, q = kRateTable[rate];
    unsigned int lt[64], ct[64];
    for (int k = 0; k < 64; k++) { lt[k] = kEpPdf_luma[q][k]; ct[k] = kEpPdf_chroma[q][k]; }
    jpeg_add_quant_table(&c, 0, lt, 100, TRUE);
    if (p->spp == 3) {
        jpeg_add_quant_table(&c, 1, ct, 100, TRUE);
        c.comp_info[0].h_samp_factor = kRateSampling[rate] >= 2 ? 2 : 1;
        c.comp_info[0].v_samp_factor = kRateSampling[rate] == 3 ? 2 : 1;
        c.comp_info[1].h_samp_factor = c.comp_info[1].v_samp_factor = 1;
        c.comp_info[2].h_samp_factor = c.comp_info[2].v_samp_factor = 1;
    }
    jpeg_start_compress(&c, TRUE);
    const uint16_t probe = 1;
    int host_le = *(const uint8_t *)&probe == 1;
    for (int y = 0; y < p->h; y++) {
        if (fread(ibuf, 1, in_row, fi) != in_row) { snprintf(err, errlen, "scan data is shorter than expected"); goto done; }
        if (mono) {
            for (int x = 0; x < p->w; x++) obuf[x] = (ibuf[x >> 3] & (0x80 >> (x & 7))) ? 0 : 255;
        } else if (wide) {   /* CPdf::SetImage48Bitto24bit / SetImage16Bitto8bit: the high byte */
            for (size_t k = 0; k < (size_t)p->w * p->spp; k++) obuf[k] = ibuf[2 * k + (host_le ? 1 : 0)];
        } else memcpy(obuf, ibuf, in_row);
        JSAMPROW r = obuf;
        jpeg_write_scanlines(&c, &r, 1);
    }
    jpeg_finish_compress(&c);
    ok = 1;
done:
    jpeg_destroy_compress(&c);
    free(ibuf); free(obuf);
    fclose(fi);
    if (!ok) { free(*out); *out = NULL; *outlen = 0; }
    return ok;
}

/* the same encoder on 8-bit pixels in memory (High Compression's layers) */
static int encode_jpeg_mem(const uint8_t *pix, int w, int h, int spp, int rate, unsigned char **out,
                           unsigned long *outlen, char *err, size_t errlen)
{
    struct jpeg_compress_struct c;
    struct pdf_jerr je;
    volatile int ok = 0;
    *out = NULL; *outlen = 0;
    c.err = jpeg_std_error(&je.pub);
    je.pub.error_exit = pdf_jerror_exit;
    if (setjmp(je.jb)) { snprintf(err, errlen, "JPEG error: %s", je.msg); goto done; }
    jpeg_create_compress(&c);
    jpeg_mem_dest(&c, out, outlen);
    c.image_width = (JDIMENSION)w;
    c.image_height = (JDIMENSION)h;
    c.input_components = spp;
    c.in_color_space = spp == 1 ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&c);
    c.do_fancy_downsampling = FALSE;
    c.dct_method = JDCT_FLOAT;
    c.write_JFIF_header = FALSE;
    int q = kRateTable[rate];
    unsigned int lt[64], ct[64];
    for (int k = 0; k < 64; k++) { lt[k] = kEpPdf_luma[q][k]; ct[k] = kEpPdf_chroma[q][k]; }
    jpeg_add_quant_table(&c, 0, lt, 100, TRUE);
    if (spp == 3) {
        jpeg_add_quant_table(&c, 1, ct, 100, TRUE);
        c.comp_info[0].h_samp_factor = kRateSampling[rate] >= 2 ? 2 : 1;
        c.comp_info[0].v_samp_factor = kRateSampling[rate] == 3 ? 2 : 1;
        c.comp_info[1].h_samp_factor = c.comp_info[1].v_samp_factor = 1;
        c.comp_info[2].h_samp_factor = c.comp_info[2].v_samp_factor = 1;
    }
    jpeg_start_compress(&c, TRUE);
    for (int y = 0; y < h; y++) {
        JSAMPROW r = (JSAMPROW)(pix + (size_t)y * w * spp);
        jpeg_write_scanlines(&c, &r, 1);
    }
    jpeg_finish_compress(&c);
    ok = 1;
done:
    jpeg_destroy_compress(&c);
    if (!ok) { free(*out); *out = NULL; *outlen = 0; }
    return ok;
}

static char g_hc_engine[1024];
void es_pdf_set_hc_engine(const char *path) { snprintf(g_hc_engine, sizeof g_hc_engine, "%s", path ? path : ""); }

/* CPdf::DoCheckHighCompLimit: 7000 x 5000 pixels (5000 x 7000 in landscape) */
int es_pdf_high_fits(int w, int h, int landscape)
{
    return landscape ? (w <= 5000 && h <= 7000) : (w <= 7000 && h <= 5000);
}

/* the page as 8-bit samples (16-bit: the high byte, as CPdf::SetImage48Bitto24bit) */
static uint8_t *load_page8(const EsPdfPage *p, char *err, size_t errlen)
{
    size_t row = (size_t)p->w * p->spp, in_row = row * (p->bits > 8 ? 2 : 1);
    FILE *fi = fopen(p->raw, "rb");
    uint8_t *pix = malloc(row * p->h), *ib = malloc(in_row);
    int ok = fi && pix && ib;
    const uint16_t probe = 1;
    int host_le = *(const uint8_t *)&probe == 1;
    for (int y = 0; ok && y < p->h; y++) {
        if (fread(ib, 1, in_row, fi) != in_row) { ok = 0; break; }
        if (p->bits > 8) for (size_t k = 0; k < row; k++) pix[(size_t)y * row + k] = ib[2 * k + (host_le ? 1 : 0)];
        else memcpy(pix + (size_t)y * row, ib, row);
    }
    if (fi) fclose(fi);
    free(ib);
    if (!ok) { free(pix); snprintf(err, errlen, "scan data is shorter than expected"); return NULL; }
    return pix;
}

/* ------------------------------------------------------------- PDF objects */

typedef struct { FILE *f; long *offs; int n, cap; } Pdf;

static int obj_begin(Pdf *p, int id)
{
    if (id >= p->cap) {
        int nc = id * 2 + 16;
        long *o = realloc(p->offs, sizeof(long) * (size_t)nc);
        if (!o) return 0;
        memset(o + p->cap, 0, sizeof(long) * (size_t)(nc - p->cap));
        p->offs = o; p->cap = nc;
    }
    p->offs[id] = ftell(p->f);
    if (id >= p->n) p->n = id + 1;
    fprintf(p->f, "%d 0 obj\n", id);
    return 1;
}

static int write_stream_obj(Pdf *p, int id, const char *dict, const void *data, unsigned long len)
{
    obj_begin(p, id);
    fprintf(p->f, "<< %s /Length %lu >>\nstream\n", dict, len);
    int ok = fwrite(data, 1, len, p->f) == len;
    fputs("\nendstream\nendobj\n", p->f);
    return ok;
}

int es_save_pdf(const char *out, const EsPdfPage *pages, int count, int paper_w10, int paper_h10,
                int landscape, int left10, int top10, int high, char *err, size_t errlen)
{
    if (count <= 0) { snprintf(err, errlen, "no pages"); return 0; }
    Pdf p = { fopen(out, "wb"), NULL, 1, 0 };
    if (!p.f) { snprintf(err, errlen, "can't create the file"); return 0; }
    int ok = 1;
    int *page_ids = calloc((size_t)count, sizeof *page_ids);
    fputs("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n", p.f);
    /* objects: 1 catalog, 2 pages, 3 info, then per page: page, contents, image(s) */
    int next = 4;
    for (int i = 0; ok && i < count; i++) {
        const EsPdfPage *pg = &pages[i];
        /* CEpPDFCreate::SetPaperSize / SetMargin: 1/10 mm -> points, rounded */
        int pw, ph;
        double left = 0, top = 0;
        int dpi = pg->dpi;
        double iw = dpi ? (double)pg->w * 72.0 / dpi : pg->w * 0.72;
        double ih = dpi ? (double)pg->h * 72.0 / dpi : pg->h * 0.72;
        iw = (int)(iw + 0.5); ih = (int)(ih + 0.5);
        if (paper_w10 > 0 && paper_h10 > 0) {
            int a = landscape ? paper_h10 : paper_w10, b = landscape ? paper_w10 : paper_h10;
            pw = (int)(a * 72.0 / 254.0 + 0.5);
            ph = (int)(b * 72.0 / 254.0 + 0.5);
            left = (int)(left10 * 72.0 / 254.0 + 0.5);
            top = (int)(top10 * 72.0 / 254.0 + 0.5);
        } else { pw = (int)iw; ph = (int)ih; }       /* Actual Image Size */
        /* CalculateImageRect (rotation 0): x = left, y = page height - image height - top */
        double x = left, y = ph - ih - top;
        int id = next;
        page_ids[i] = id;
        /* High Compression (EPHighCompression, flag 8): background, mask and foreground layers for
         * colour pages that fit DoCheckHighCompLimit (DoCom takes 0x404 RGB only; other pages fall
         * back to a single JPEG, as ConvertPDF does when DoCom declines) */
        int mrc = high && g_hc_engine[0] && pg->spp == 3 && pg->bits >= 8 && es_pdf_high_fits(pg->w, pg->h, landscape);
        HcLayers L = { 0 };
        if (mrc) {
            uint8_t *pix = load_page8(pg, err, errlen);
            if (!pix) { ok = 0; break; }
            int r = hc_segment(g_hc_engine, pix, pg->w, pg->h, pg->spp, &L, err, errlen);
            free(pix);
            if (r) { ok = 0; break; }
        }
        char content[512];
        int clen;
        if (mrc) {
            clen = snprintf(content, sizeof content, "q\n%.2f %.2f %.2f %.2f %.2f %.2f cm\n/Im%d Do\nQ\n"
                            "q\n%.2f %.2f %.2f %.2f %.2f %.2f cm\n/Im%d Do\nQ\n",
                            iw, 0.0, 0.0, ih, x, y, 2 * i, iw, 0.0, 0.0, ih, x, y, 2 * i + 1);
            obj_begin(&p, id);
            fprintf(p.f, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Resources << /XObject << /Im%d %d 0 R "
                         "/Im%d %d 0 R >> /ProcSet [/PDF /ImageC] >> /Contents %d 0 R >>\nendobj\n",
                    pw, ph, 2 * i, id + 2, 2 * i + 1, id + 4, id + 1);
        } else {
            clen = snprintf(content, sizeof content, "q\n%.2f %.2f %.2f %.2f %.2f %.2f cm\n/Im%d Do\nQ\n",
                            iw, 0.0, 0.0, ih, x, y, 2 * i);
            obj_begin(&p, id);
            fprintf(p.f, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Resources << /XObject << /Im%d %d 0 R >> "
                         "/ProcSet [/PDF /Image%c] >> /Contents %d 0 R >>\nendobj\n",
                    pw, ph, 2 * i, id + 2, pg->spp == 1 ? 'B' : 'C', id + 1);
        }
        obj_begin(&p, id + 1);
        fprintf(p.f, "<< /Length %d >>\nstream\n%sendstream\nendobj\n", clen, content);
        char dict[256];
        if (mrc) {
            unsigned char *jb = NULL, *jf = NULL;
            unsigned long lb = 0, lf = 0;
            uint8_t *g4 = NULL;
            size_t lg = 0;
            int rb = L.bg_comp >> 8, rf = L.fg_comp >> 8;
            if (rb < 0 || rb > 5) rb = 3;
            if (rf < 0 || rf > 5) rf = 5;
            ok = encode_jpeg_mem(L.background, L.w, L.h, 3, rb, &jb, &lb, err, errlen) &&
                 encode_jpeg_mem(L.foreground, L.w, L.h, 3, rf, &jf, &lf, err, errlen) &&
                 es_g4_encode(L.mask, L.w, L.h, (size_t)(L.w + 7) / 8, &g4, &lg) == 0;
            if (ok) {
                snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB "
                         "/BitsPerComponent 8 /Filter /DCTDecode", L.w, L.h);
                ok = write_stream_obj(&p, id + 2, dict, jb, lb);
                /* the mask (0x401, CCITT G4): 1 where the foreground shows */
                snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d /Height %d /ImageMask true "
                         "/Decode [1 0] /Filter /CCITTFaxDecode /DecodeParms << /K -1 /Columns %d /Rows %d /BlackIs1 true >>",
                         L.w, L.h, L.w, L.h);
                ok = ok && write_stream_obj(&p, id + 3, dict, g4, lg);
                snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB "
                         "/BitsPerComponent 8 /Mask %d 0 R /Filter /DCTDecode", L.w, L.h, id + 3);
                ok = ok && write_stream_obj(&p, id + 4, dict, jf, lf);
                if (!ok && !err[0]) snprintf(err, errlen, "can't write the file (disk full?)");
            } else if (!err[0]) snprintf(err, errlen, "High Compression encoding failed");
            free(jb); free(jf); free(g4);
            hc_free(&L);
            next = id + 5;
        } else {
            unsigned char *jpg = NULL;
            unsigned long jlen = 0;
            if (!encode_jpeg(pg, high, &jpg, &jlen, err, errlen)) { ok = 0; break; }
            snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /%s "
                     "/BitsPerComponent 8 /Filter /DCTDecode", pg->w, pg->h, pg->spp == 1 ? "DeviceGray" : "DeviceRGB");
            if (!write_stream_obj(&p, id + 2, dict, jpg, jlen)) { snprintf(err, errlen, "can't write the file (disk full?)"); ok = 0; }
            free(jpg);
            next = id + 3;
        }
    }
    if (ok) {
        obj_begin(&p, 1);
        fputs("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", p.f);
        obj_begin(&p, 2);
        fprintf(p.f, "<< /Type /Pages /Count %d /Kids [", count);
        for (int i = 0; i < count; i++) fprintf(p.f, "%s%d 0 R", i ? " " : "", page_ids[i]);
        fputs("] >>\nendobj\n", p.f);
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        char date[32];
        strftime(date, sizeof date, "D:%Y%m%d%H%M%S", &tm);
        obj_begin(&p, 3);
        fprintf(p.f, "<< /Producer (EPSON Scan) /CreationDate (%s) >>\nendobj\n", date);
        long xref = ftell(p.f);
        fprintf(p.f, "xref\n0 %d\n0000000000 65535 f \n", p.n);
        for (int i = 1; i < p.n; i++) fprintf(p.f, "%010ld 00000 n \n", p.offs[i]);
        fprintf(p.f, "trailer\n<< /Size %d /Root 1 0 R /Info 3 0 R >>\nstartxref\n%ld\n%%%%EOF\n", p.n, xref);
    }
    free(p.offs);
    free(page_ids);
    if (fclose(p.f) != 0 && ok) { snprintf(err, errlen, "can't write the file (disk full?)"); ok = 0; }
    if (!ok) remove(out);
    return ok;
}
