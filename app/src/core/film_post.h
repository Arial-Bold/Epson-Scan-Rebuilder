#ifndef FILM_POST_H
#define FILM_POST_H

#include <stddef.h>
#include <stdint.h>

/* Measure how far the infrared pass is offset from the color pass.
 * Shifting the IR by (dy, dx) lines it up: aligned[y][x] = ir[y - dy][x - dx]. */
int ir_estimate_shift(const char *rgb_raw, const char *ir_raw, int w, int h, int *dy, int *dx);

/* Offset map: dx = ax[0] + ax[1]*x + ax[2]*y, dy = ay[0] + ay[1]*x + ay[2]*y */
typedef struct { double ax[3], ay[3]; } IRMap;

/* Measure the offset in tiles around a starting shift (from ir_estimate_shift)
 * and fit the map. On failure the map holds the starting shift. report gets a
 * one-line summary. Returns 0 if the fit succeeded. */
int ir_measure_map(const char *rgb_raw, const char *ir_raw, int w, int h, int gdy, int gdx,
                   IRMap *map, char *report, size_t replen);

/* Resample the IR through the map (bilinear). */
int ir_warp_file(const char *ir_in, const char *ir_out, int w, int h, const IRMap *m);

/* Write a shifted copy of a 16-bit IR raw file. */
int ir_shift_file(const char *ir_in, const char *ir_out, int w, int h, int dy, int dx);

/* 16-bit little-endian raw (spp = 1 or 3) to an uncompressed TIFF. */
int raw16_to_tiff(const char *raw, const char *tiff, int w, int h, int spp, int dpi);


#include <stddef.h>

typedef struct { int x, y, w, h; } FrameRect;

/* Find the film frames in a film-holder scan (skips holder bars and empty slots). */
int detect_frames(const char *rgb_raw, int w, int h, int dpi, FrameRect *out, int max);

/* Copy a rectangle out of a raw file (bpp = bytes per pixel). */
int crop_raw(const char *in, const char *out, int w, int bpp, FrameRect r);

/* Align IR to RGB, find the frames, run Digital ICE on each frame and write
 * OUT_BASE.tif (one frame) or OUT_BASE_1.tif, OUT_BASE_2.tif, ...
 * engine = path to the original DigitalICE_i386 file. Returns 0 on success. */
int film_ice_pipeline(const char *engine, const char *rgb_raw, const char *ir_raw, const char *out_base,
                      int w, int h, int dpi, int keep_temp, char *err, size_t errlen);

#endif
