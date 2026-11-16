/* Epson Scan's file writers: the TIFF plug-in's exact header layout and the
 * JPEG plug-in's libjpeg settings.  Input is a headerless raw file of
 * interleaved samples (8 bit, or 16 bit in host byte order). */
#ifndef ES_SAVE_H
#define ES_SAVE_H
#include <stddef.h>

/* big_endian: 1 = "Macintosh" (MM), 0 = "Windows" (II).
 * icc / icc_len: profile to embed (tag 0x8773), or NULL. */
int es_save_tiff(const char *raw, const char *out, int w, int h, int spp, int bits,
                 int dpi_x, int dpi_y, int big_endian,
                 const unsigned char *icc, size_t icc_len, char *err, size_t errlen);

/* Multi-TIFF: add page `page` (0 creates the file). *link_pos carries the previous page's
 * next-IFD link position between calls. */
int es_mtiff_add_page(const char *raw, const char *out, int page, unsigned int *link_pos, int w, int h, int spp,
                      int bits, int dpi_x, int dpi_y, int big_endian, char *err, size_t errlen);

/* level: Epson "Compression Level" 1..100 (quality = 100 - level, min 1).
 * Epson's default level is 16. */
int es_save_jpeg(const char *raw, const char *out, int w, int h, int spp, int bits,
                 int dpi_x, int dpi_y, int level, int progressive,
                 const unsigned char *icc, size_t icc_len, char *err, size_t errlen);

/* Thumbnail Mirroring + Rotation: mirrored left to right (mirror != 0), then quarter turns clockwise. */
int es_transform_raw(const char *in, const char *out, int w, int h, int spp, int bits, int quarters, int mirror,
                     int *ow, int *oh, char *err, size_t errlen);
/* TIFF plug-in Compression > B&W: CCITT Group 4 for 1-bit pages (on) or none (off). */
void es_tiff_set_bw_g4(int on);

/* Editing Page: quarter turns clockwise (negative = counterclockwise) into `out`. */
int es_rotate_raw(const char *in, const char *out, int w, int h, int spp, int bits, int quarters,
                  int *ow, int *oh, char *err, size_t errlen);

#endif
