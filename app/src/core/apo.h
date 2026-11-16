/*
 * Auto Photo Orientation (Configuration > Auto Photo Orientation) — Epson Scan's own classifier
 * from the UI plug-in, recompiled to native code.
 */
#ifndef APO_H
#define APO_H
#include <stddef.h>
#include <stdint.h>
/* rgb: 24-bit RGB rows of `rowbytes`; reject: Epson passes 0.09. orientation: 0 upright,
 * 1 / 2 / 3 as getOrientation_reject_new reports (negative = not sure). Returns 0 or negative. */
int apo_orientation(const char *engine_path, const uint8_t *rgb, int width, int height, int rowbytes,
                    double reject, int *orientation, char *err, size_t errlen);
#endif
