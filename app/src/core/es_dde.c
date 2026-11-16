#include "es_dde.h"
#include "es_output.h"
#include "if.h"
#include "dde.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t *slurp(const char *path, size_t want)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *b = malloc(want ? want : 1);
    size_t n = b ? fread(b, 1, want, f) : 0;
    fclose(f);
    if (n != want) { free(b); return NULL; }
    return b;
}

int es_dde_run(const EsDdeJob *job, const char *raw, const char *out, int w, int h, int spp, int in_bits,
               int negative, char *err, size_t errlen)
{
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.dde", out);
    int rc = -1;
    uint8_t *in = NULL, *res = NULL;
    size_t grb = (size_t)w, brb = (size_t)(w + 7) / 8;
    int out_bits = job->bw ? 1 : 8;
    size_t orb = job->bw ? brb : grb;

    if (job->option == ES_DDE_CE) {
        /* Color Enhance reads 24-bit colour (RotationScan sets 0x18 bits when CEType != 0) */
        if (es_convert_to_raw_gamma(raw, tmp, w, h, spp, in_bits, 8, negative, job->gamma, err, errlen)) goto done;
        in = slurp(tmp, (size_t)w * 3 * h);
        res = calloc(orb, (size_t)h);
        if (!in || !res) { snprintf(err, errlen, "not enough memory for Color Enhance"); goto done; }
        if_set_dde_param(job->prm);
        IfDdeColor p = { ES_DDE_DEV_ID, 0, 0, job->ce_mask, job->dpi, job->threshold };
        if (if_dde_color(job->if_engine, in, w, h, w * 3, res, (int)orb, out_bits, &p, err, errlen)) goto done;
    } else {
        /* TET and AAS read 8-bit gray */
        if (es_mono_convert(raw, tmp, w, h, spp, in_bits, -1, job->gamma, 0, 128, negative, err, errlen)) goto done;
        in = slurp(tmp, grb * h);
        res = calloc(brb, (size_t)h);
        if (!in || !res) { snprintf(err, errlen, "not enough memory for the Image Option"); goto done; }
        if (job->option == ES_DDE_TET) {
            if (dde_binarize(job->dde_engine, job->prm, in, w, h, w, res, (int)brb, ES_DDE_DEV_ID, job->dpi,
                             err, errlen)) goto done;
        } else {
            /* AasInfo + 0x21: the gamma table the gray data went through (GetGammaTable) */
            uint8_t gam[256];
            for (int v = 0; v < 256; v++) gam[v] = (uint8_t)lround(255 * pow(v / 255.0, 1 / job->gamma));
            if_set_dde_param(job->prm);
            if (if_dde_aas(job->if_engine, in, w, h, w, res, (int)brb, ES_DDE_DEV_ID, job->dpi, 0, gam,
                           err, errlen)) goto done;
        }
        out_bits = 1; orb = brb;
    }
    /* Epson's filters write 1 = white; the app keeps 1 = black until the file is written */
    if (out_bits == 1) for (size_t i = 0; i < orb * h; i++) res[i] = (uint8_t)~res[i];
    FILE *fo = fopen(out, "wb");
    if (!fo || fwrite(res, 1, orb * h, fo) != orb * h) { if (fo) fclose(fo); snprintf(err, errlen, "disk write failed"); goto done; }
    if (fclose(fo)) { snprintf(err, errlen, "disk write failed"); goto done; }
    rc = 0;
done:
    unlink(tmp);
    free(in); free(res);
    return rc;
}

int es_deskew_file(const char *engine, const char *raw, const char *out, int w, int h, int spp, int bits,
                   const Dtr1Location *loc, int *ow, int *oh, char *err, size_t errlen)
{
    int pbits = spp * bits;
    size_t rb = ((size_t)w * pbits + 7) / 8;
    int W = loc->loc[4], H = loc->loc[5];
    size_t orb = ((size_t)W * pbits + 7) / 8;
    uint8_t *in = slurp(raw, rb * h), *o = calloc(orb ? orb : 1, H > 0 ? (size_t)H : 1);
    int rc = -1;
    if (!in || !o || W <= 0 || H <= 0) { snprintf(err, errlen, "can't read the image for Correct Document Skew"); goto done; }
    if (dtr1_deskew(engine, loc, pbits, in, w, h, (int)rb, o, (int)orb, err, errlen)) goto done;
    FILE *fo = fopen(out, "wb");
    if (!fo || fwrite(o, 1, orb * H, fo) != orb * H) { if (fo) fclose(fo); snprintf(err, errlen, "disk write failed"); goto done; }
    if (fclose(fo)) { snprintf(err, errlen, "disk write failed"); goto done; }
    *ow = W; *oh = H;
    rc = 0;
done:
    free(in); free(o);
    return rc;
}

/* HRD ColorSmoothing record of the V550 (CFileIoHRD::ReadColorSmoothing: the fourth word x 100) */
static const Dtr1SmoothParam kSmooth = { 28, -1, 230, 0 * 100.0, 5.0 };

int es_color_smoothing_file(const char *engine, const char *raw, int w, int h,
                            const uint8_t *preview, int pw, int ph, const int area[4], char *err, size_t errlen)
{
    size_t rb = (size_t)w * 3;
    uint8_t *im = slurp(raw, rb * h);
    int rc = -1;
    if (!im) { snprintf(err, errlen, "can't read the image for Color Smoothing"); return -1; }
    /* DEC_AREA as CPreviewDialog::SetColorSmoothingImage builds it */
    int dec[8] = { area[0], area[1], area[0], area[1], area[2], area[3], area[2], area[3] };
    if (dtr1_color_smoothing(engine, &kSmooth, preview, pw, ph, pw * 3, dec, im, w, h, (int)rb, err, errlen)) goto done;
    FILE *fo = fopen(raw, "r+b");
    if (!fo || fwrite(im, 1, rb * h, fo) != rb * h) { if (fo) fclose(fo); snprintf(err, errlen, "disk write failed"); goto done; }
    if (fclose(fo)) { snprintf(err, errlen, "disk write failed"); goto done; }
    rc = 0;
done:
    free(im);
    return rc;
}
