/*
 * PDF "High Compression" — PDFLib's EPHighCompression (PALDoCom segmentation into a
 * background, a foreground and the mask that picks between them) from Epson Scan 3.9.4,
 * recompiled to native code.
 */
#ifndef HC_H
#define HC_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h;
    uint8_t *background;   /* w x h x 3 RGB  (PDF compression 0x301: DCT)                    */
    uint8_t *foreground;   /* w x h x 3 RGB  (0x501: DCT, drawn through the mask)            */
    uint8_t *mask;         /* 1-bit rows of (w + 7) / 8 bytes, 1 = background shows (0x401:   */
                           /* CCITT G4 stencil)                                               */
    int bg_comp, fg_comp, mask_comp;
} HcLayers;

/* rgb: w x h, 24-bit (spp 3) or 8-bit gray (spp 1). Returns 0 and the layers (free with
 * hc_free), or a negative value with a message. */
int hc_segment(const char *engine_path, const uint8_t *pixels, int w, int h, int spp, HcLayers *out,
               char *err, size_t errlen);
void hc_free(HcLayers *l);
#endif
