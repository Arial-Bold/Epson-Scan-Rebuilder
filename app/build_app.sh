#!/bin/sh
# Builds "EPSON Scan.app" (rebuild) for Apple Silicon with the Command Line Tools.
set -e
cd "$(dirname "$0")"
APP="build/EPSON Scan.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
CC="clang -O2 -arch arm64 -mmacosx-version-min=12.0"
$CC -Wno-deprecated-declarations -c -o build/es_scanner.o src/core/es_scanner.c
$CC -c -o build/es_output.o src/core/es_output.c
$CC -c -o build/film_post.o src/core/film_post.c
# Digital ICE engine (recompiled): keep floating point bit-exact with the original
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/ice_rt.o src/core/ice_rt.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/ice_film_gen.o src/core/ice_film_gen.c
# Epson FIT film/photo auto exposure engine (recompiled), same rules
echo "Compiling Epson FIT (takes a minute)..."
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/fit_rt.o src/core/fit_rt.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/fit_gen.o src/core/fit_gen.c
# Epson Image Filter (Unsharp Mask, ...) (recompiled), same rules
echo "Compiling Epson Image Filter (takes a minute)..."
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/if_rt.o src/core/if_rt.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/if_gen.o src/core/if_gen.c
# Epson DTR2 (film frame detection, Auto Locate, Thumbnail Cropping Area) (recompiled)
echo "Compiling Epson DTR (frame detection, takes a minute)..."
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/dtr_rt.o src/core/dtr_rt.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/dtr_gen.o src/core/dtr_gen.c
# Epson Auto Photo Orientation (from the UI plug-in) (recompiled)
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/apo_rt.o src/core/apo_rt.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/apo_gen.o src/core/apo_gen.c
# Epson Dust Removal (EsDDC) (recompiled)
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/ddc_rt.o src/core/ddc_rt.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/ddc_gen.o src/core/ddc_gen.c
# Epson DDE (Text Enhancement Technology binarization) (recompiled)
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/dde_rt.o src/core/dde_rt.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/dde_gen.o src/core/dde_gen.c
# Epson DTR version 1 (Color Smoothing, Correct Document Skew) (recompiled)
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/dtr1_rt.o src/core/dtr1_rt.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/dtr1_gen.o src/core/dtr1_gen.c
$CC -c -o build/es_dde.o src/core/es_dde.c
# Epson PRINT Image Matching II (PIM JPEG / PIM TIFF Support) (recompiled), PICT writer
echo "Compiling Epson PRINT Image Matching II..."
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/pimj_rt.o src/core/pimj_rt.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/pimj_gen.o src/core/pimj_gen.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/pimt_rt.o src/core/pimt_rt.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/pimt_gen.o src/core/pimt_gen.c
$CC -c -o build/es_pict.o src/core/es_pict.c
# Epson PDFLib High Compression (EPHighCompression / PALDoCom segmentation) (recompiled)
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/hc_rt.o src/core/hc_rt.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/hc_gen.o src/core/hc_gen.c
$CC -c -o build/es_adjust.o src/core/es_adjust.c
echo "Compiling Epson Image Control (Color Palette, takes a minute)..."
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/ic_rt.o src/core/ic_rt.c
$CC -ffp-contract=off -fno-strict-aliasing -w -c -o build/ic_gen.o src/core/ic_gen.c
$CC -c -o build/es_levels.o src/core/es_levels.c
$CC -c -o build/es_tonefile.o src/core/es_tonefile.c
$CC -c -o build/es_target.o src/core/es_target.c
# libjpeg (IJG) for the JPEG file format, and Epson's TIFF / JPEG writers
JPEGOBJS=""
for f in src/libjpeg/*.c; do
    o="build/jpeg_$(basename "$f" .c).o"
    $CC -w -c -o "$o" "$f"
    JPEGOBJS="$JPEGOBJS $o"
done
$CC -c -o build/es_save.o src/core/es_save.c
$CC -c -o build/es_g4.o src/core/es_g4.c
$CC -c -o build/es_pdf.o src/core/es_pdf.c
OBJS="$JPEGOBJS build/es_save.o build/es_g4.o build/es_pdf.o build/ic_rt.o build/ic_gen.o build/es_levels.o build/es_tonefile.o build/es_target.o build/ddc_rt.o build/ddc_gen.o build/es_scanner.o build/es_output.o build/film_post.o build/ice_rt.o build/ice_film_gen.o build/fit_rt.o build/fit_gen.o build/if_rt.o build/if_gen.o build/dtr_rt.o build/dtr_gen.o build/apo_rt.o build/apo_gen.o build/dde_rt.o build/dde_gen.o build/dtr1_rt.o build/dtr1_gen.o build/es_dde.o build/pimj_rt.o build/pimj_gen.o build/pimt_rt.o build/pimt_gen.o build/es_pict.o build/hc_rt.o build/hc_gen.o build/es_adjust.o"
clang -fobjc-arc -O2 -arch arm64 -mmacosx-version-min=12.0 -Wall -Wno-deprecated-declarations \
    -framework Cocoa -framework ApplicationServices -framework IOKit -framework CoreFoundation \
    -o "$APP/Contents/MacOS/EPSON Scan" src/app.m $OBJS
cp Info.plist "$APP/Contents/Info.plist"
cp -R Resources/ "$APP/Contents/Resources/"
codesign -s - --force --deep "$APP" >/dev/null 2>&1 || true
echo "Built $APP"
echo "Open it with:  open \"$APP\""
