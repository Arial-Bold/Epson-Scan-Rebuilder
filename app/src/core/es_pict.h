/*
 * PICT file writer, as Epson's PICT plug-in (CPict) records it: a QuickDraw picture
 * (OpenCPicture, version 2 extended header with the resolution) holding CopyBits of the image
 * in 500 x 500 tiles from a 32-bit GWorld (colour), an 8-bit GWorld with the standard gray
 * table (CTable 40, gray) or a 1-bit one (CTable 33, Black & White), preceded by the 512-byte
 * file header.
 */
#ifndef ES_PICT_H
#define ES_PICT_H
#include <stddef.h>

/* raw: headerless rows, spp 1/3, bits 1/8/16 (16 is reduced to 8, as the 32-bit / 8-bit
 * GWorld does); 1-bit rows use 1 = black. Returns 1 on success. */
int es_save_pict(const char *raw, const char *out, int w, int h, int spp, int bits, int dpi,
                 char *err, size_t errlen);
/* Any raw (spp 1/3, bits 1/8/16) -> 24-bit RGB raw, for PRINT Image Matching II's temporary
 * TIFF (Epson's EXIF plug-in hands the PIM engine 24-bit colour only). Returns 1 on success. */
int es_raw_to_rgb8(const char *raw, const char *out, int w, int h, int spp, int bits, char *err, size_t errlen);
#endif
