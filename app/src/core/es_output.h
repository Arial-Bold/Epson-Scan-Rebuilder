#ifndef ES_OUTPUT_H
#define ES_OUTPUT_H
#include <stddef.h>
#include <stdint.h>
/* Linear raw scan (8/16-bit, 1 or 3 channels) -> TIFF (8/16-bit). Negatives are
 * auto-levelled and inverted; all output gets gamma 2.2. Returns 0 on success. */
int es_convert_to_tiff(const char *raw, const char *tiff, int w, int h, int spp, int in_bits, int out_bits,
                       int negative, int dpi, char *err, size_t errlen);
/* The same conversion, written as a headerless raw file. */
int es_convert_to_raw(const char *raw, const char *out, int w, int h, int spp, int in_bits, int out_bits,
                      int negative, char *err, size_t errlen);
/* es_convert_to_raw with another output gamma (1.8 for ColorSync) */
int es_convert_to_raw_gamma(const char *raw, const char *out, int w, int h, int spp, int in_bits, int out_bits,
                            int negative, double gamma, char *err, size_t errlen);
/* Dropout (channel 0/1/2 of an RGB scan, -1 = none) and Black & White (bw, threshold 0..255):
 * 8-bit gray, or 1-bit rows (1 = black) when bw. */
int es_mono_convert(const char *raw, const char *out, int w, int h, int spp, int in_bits, int channel,
                    double gamma, int bw, int threshold, int negative, char *err, size_t errlen);
/* No Color Correction: straight encoding with `gamma`, negatives only inverted (no levels). */
int es_convert_plain(const char *raw, const char *out, int w, int h, int spp, int in_bits, int out_bits,
                     int negative, double gamma, char *err, size_t errlen);
/* Linear 16-bit RGB raw -> TIFF through Epson's 12-bit tables (3 x 4096, from
 * fit_make_lut12), like the ICE LUT plugin. out_bits 8 or 16. */
int es_convert_lut_to_tiff(const char *raw, const char *tiff, int w, int h, const uint16_t *lut12,
                           int out_bits, int dpi, char *err, size_t errlen);
/* Same curve, written as a headerless raw file (for further processing). */
int es_apply_lut_raw(const char *raw16, const char *out_raw, int w, int h, const uint16_t *lut12,
                     int out_bits, char *err, size_t errlen);
/* Headerless interleaved raw (8/16-bit, spp 1 or 3) -> TIFF, data copied as is. */
int es_raw_to_tiff(const char *raw, const char *tiff, int w, int h, int spp, int bits, int dpi,
                   char *err, size_t errlen);
/* Epson's colour correction after the tone table: out = coef x (r,g,b), in place on an
 * interleaved RGB raw file (8 or 16 bits), rounded and clipped like CPreviewImage::MakeMatrix. */
int es_apply_matrix_raw(const char *raw, int w, int h, int bits16, const double coef[9], char *err, size_t errlen);
#endif
