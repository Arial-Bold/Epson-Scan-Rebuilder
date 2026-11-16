/*
 * User tone curves the way Epson Scan stores them (Image Control plug-in,
 * CToneCurve::SaveUserToneCurve / LoadUserToneCurve):
 *
 *   file   ~/Library/Preferences/EPSON/EPSON Scan/ES<model>/EPSON Scan AD Prefs
 *          (Professional mode prefs, type 'Pref', creator 'eSTW'), resource fork
 *   'TONU' one resource per curve, IDs 10001, 10002, ... in list order; every save
 *          removes all TONU resources and writes the list again
 *   data   0x248 bytes, the plug-in's ToneCurveInfo in the machine's byte order:
 *          +0x000 name (C string, Mac Roman, max 32 chars)
 *          +0x024 master FIT3_IC (int16 count, pad, 16 x {int32 x, int32 y})
 *          +0x0a8 master selected point     +0x0ac red FIT3_IC
 *          +0x130 green FIT3_IC             +0x1b4 blue FIT3_IC
 *          +0x238 / +0x23c / +0x240 red / green / blue selected point
 *
 * On disk the record is always big-endian: Epson Scan installs a Core Endian flipper for
 * 'TONU' (EPFlipToneCurvePrefRecord -> IMByteSwap_ToneCurveInfo), so the Intel build swaps
 * the count, the 16 points and the selected-point fields on every read and write.
 */
#ifndef ES_TONEFILE_H
#define ES_TONEFILE_H

#include <stdint.h>

#define TONU_SIZE 0x248

typedef struct {
    char    name[0x24];          /* Mac Roman */
    uint8_t curves[4][0x84];     /* master, R, G, B as FIT3_IC, host byte order */
    int32_t selected[4];
} EsToneCurve;

/* Read the TONU resources from the prefs file's resource fork. Returns the number read
 * (0 if the file or fork is missing or holds none), -1 if the fork is damaged.
 * *out is malloc'd. */
int tonu_load(const char *file, EsToneCurve **out);

/* Replace all TONU resources in the file's resource fork with `list`, keeping every
 * other resource. The file must exist. Returns 0 on success. */
int tonu_save(const char *file, const EsToneCurve *list, int n);

#define TONU_MAX_USER 30     /* OnIDC_SAVEClicked: no new curve past 30 */

/* One resource by type and ID (Get1Resource / RemoveResource + AddResource), keeping every
 * other resource in the fork. get: malloc'd data, length in *len; 1 found, 0 missing, -1 damaged. */
int rsrc_get(const char *file, uint32_t type, int16_t id, uint8_t **data, uint32_t *len);
/* data == NULL removes the resource */
int rsrc_put(const char *file, uint32_t type, int16_t id, const char *name, const uint8_t *data, uint32_t len);

#endif
