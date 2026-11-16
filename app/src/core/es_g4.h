#ifndef ES_G4_H
#define ES_G4_H
#include <stddef.h>
#include <stdint.h>
/* CCITT Group 4 encoding of a 1-bit image (MSB first, 1 = black). Returns 0 and a malloc'ed buffer. */
int es_g4_encode(const uint8_t *rows, int w, int h, size_t rowbytes, uint8_t **out, size_t *outlen);
#endif
