/*
 * Epson DDE binarization (Text Enhancement Technology for Black & White) — EsDDE.bundle from
 * Epson Scan 3.9.4, recompiled to native code.
 */
#ifndef DDE_H
#define DDE_H
#include <stddef.h>
#include <stdint.h>

/* 8-bit gray in, 1-bit out (1 = white, as the scanner binarizes). dev_id is the HRD DDE
 * parameter ID (0 for the V550); prm_path is EsDDE.prm. Returns 0 on success. */
int dde_binarize(const char *engine_path, const char *prm_path, const uint8_t *gray, int w, int h, int in_rb,
                 uint8_t *out, int out_rb, int dev_id, int resolution, char *err, size_t errlen);
#endif
