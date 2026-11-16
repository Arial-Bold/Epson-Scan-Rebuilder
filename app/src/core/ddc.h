/* Epson Dust Removal (EsDDC) — the original plugin from Epson Scan 3.9.4, recompiled. */
#ifndef DDC_H
#define DDC_H
#include <stddef.h>
#include <stdint.h>
/* Run Dust Removal in place on an RGB image (bits 24 or 48), rows of `rowbytes`.
 *   type  : 0 flatbed, 1 negative / B&W film, 2 positive film (DDC_IMAGE_INFO+0)
 *   parm  : the 8 values of the V550 dust table row (PM_DustRemoveParam)
 *   prm   : path of the original EsDDC.prm
 * Returns 0 on success. */
int ddc_remove_dust(const char *engine_path, const char *prm_path, uint8_t *img, int width, int height,
                    int rowbytes, int bits, int dpi, int type, const int parm[8], char *err, size_t errlen);
#endif
