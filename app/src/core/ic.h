/*
 * Epson Image Control plug-in (Color Palette) - the original 32-bit code from
 * Epson Scan 3.9.4, statically recompiled (IC_gen.c), called the way the
 * plug-in's CColorPaletteDialog calls it.
 */
#ifndef IC_H
#define IC_H

#include <stddef.h>
#include <stdint.h>

#define IC_HEXAGONS 44
#define IC_TRIANGLES 8

/* the palette's fixed shape, from the plug-in's tables (dialog-item coordinates of the
 * 300 x 250 palette control) */
typedef struct {
    int hex[IC_HEXAGONS][6][2];      /* CColorPalette::m_astColorBoxVertex      */
    int tri[IC_TRIANGLES][3][2];     /* CColorPalette::m_astTryangleVertex      */
    int tri_rgb[IC_TRIANGLES][3];    /* the triangles' fixed colours            */
} IcPaletteShape;

int ic_palette_shape(const char *engine_path, IcPaletteShape *shape, char *err, size_t errlen);

/* CColorPalette::SetColor: the 44 hexagon colours around `rgb` (cell 0 = the colour itself).
 * screen_gamma10 is the display gamma x 10 (what GetDisplayGamma returns). */
int ic_palette_colors(const char *engine_path, int screen_gamma10, const int rgb[3], int out[IC_HEXAGONS][3],
                      char *err, size_t errlen);

#endif
