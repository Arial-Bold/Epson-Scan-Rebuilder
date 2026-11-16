/*
 * Epson DTR2 (frame detection for film and documents) — the original plug-in from
 * Epson Scan 3.9.4, recompiled to native code.
 */
#ifndef DTR_H
#define DTR_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *xrgb;        /* preview, 32-bit xRGB (x, R, G, B bytes), gamma 1.8 */
    int width, height;          /* rows of width * 4 bytes */
    int dpi;                    /* film: 300 (the only resolution DTR2 accepts for film) */
    int model_id;               /* scanner model ID (V550: 0xEB, the ESxxxx.dti file name) */
    int connected_unit;         /* TWEP unit types: 3..5 = transparency units */
    int scanned_unit;
    int doc_source;             /* 1 negative film, 2 positive film (light path from the TPU) */
    int holder;                 /* 0 = detect the film holder */
    int film;                   /* 1 film, 0 reflective */
    int film_size;              /* 0 = from the holder */
    int film_type;              /* TWEP_TFT, -1 = detect */
    double red_matrix[9];       /* the scanner's red correction matrix (for the negative preview) */
    int margin_id;              /* Thumbnail Cropping Area: 0 small, 1 medium, 2 large */
} DtrImage;

typedef struct {
    int32_t pt[8];              /* four corners x, y in preview pixels */
    double angle;               /* skew */
    uint32_t info;              /* type / orientation word */
    int holder, film_type;      /* what DTR found: film holder, TWEP_TFT film type */
    int32_t status;
} DtrFrame;

/* Detects the film frames (or documents) in a preview. Returns 0 and the frames found
 * (count may be 0), or a negative value with a message in err. */
int dtr_detect(const char *engine_path, const char *dti_dir, const DtrImage *img, DtrFrame *frames, int max,
               int *count, char *err, size_t errlen);
/* Auto Locate: the original's rectangle (left, top, right, bottom in preview pixels) inside
 * area. Returns 0 when found, 1 when not, negative on error. */
int dtr_autolocate(const char *engine_path, const char *dti_dir, const DtrImage *img, const int area[4], int out[4],
                   char *err, size_t errlen);
/* Full Auto: is there a film holder on the glass? (DTR_DetectFilmHolder on the glass preview)
 * 1 yes, 0 no, negative on error. */
int dtr_detect_holder(const char *engine_path, const char *dti_dir, const DtrImage *img, char *err, size_t errlen);
#endif
