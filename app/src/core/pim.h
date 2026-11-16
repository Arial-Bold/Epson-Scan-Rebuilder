/*
 * PRINT Image Matching II — Epson's PIM JPEG Support / PIM TIFF Support plug-ins from
 * Epson Scan 3.9.4, recompiled to native code. Each turns a plain 24-bit RGB TIFF into the
 * PIM file (Exif with the PrintIM data, a JPEG for PIM JPEG), as the PRINT Image Matching II
 * file formats did.
 */
#ifndef PIM_H
#define PIM_H
#include <stddef.h>

/* work_dir: a folder for temporary files; tmp_tiff: the uncompressed 24-bit RGB TIFF;
 * dest: the file to write; software: Exif Software ("EPSON Scan"); value: for JPEG the
 * quality (100 - the PIM JPEG dialog's compression level). Returns 0 on success. */
int pim_jpeg_make(const char *engine_path, const char *work_dir, const char *tmp_tiff, const char *dest,
                  const char *software, unsigned long value, char *err, size_t errlen);
int pim_tiff_make(const char *engine_path, const char *work_dir, const char *tmp_tiff, const char *dest,
                  const char *software, unsigned long value, char *err, size_t errlen);
#endif
