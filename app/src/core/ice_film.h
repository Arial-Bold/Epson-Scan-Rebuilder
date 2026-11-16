/*
 * Digital ICE for film — the original Image Trends engine from Epson Scan,
 * recompiled to native code.
 */
#ifndef ICE_FILM_H
#define ICE_FILM_H

#include <stddef.h>

/* Return nonzero to cancel. */
typedef int (*ice_progress_fn)(int percent, void *ctx);

/*
 * engine_path : the original "Digital ICE" i386 file (data tables are read from it)
 * rgb_path    : 16-bit little-endian RGB, 6 bytes/pixel, row by row (film transmittance)
 * ir_path     : 16-bit little-endian infrared, 2 bytes/pixel, aligned to rgb
 * out_path    : written in the same layout as rgb_path
 * film_type   : as Epson Scan passes it (1 or 2; Epson's default is 2)
 * level       : 9 for film (the engine forces this in film mode)
 * Returns 0 on success; on failure fills err.
 */
int ice_film(const char *engine_path, const char *rgb_path, const char *ir_path, const char *out_path,
             int width, int height, int dpi, int film_type, int level,
             ice_progress_fn progress, void *ctx, char *err, size_t errlen);

#endif
