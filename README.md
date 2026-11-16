# Scanner App Builder

Builds a native Apple Silicon version of Epson Scan 3.9.4 for the Epson Perfection V550, on the
user's own Mac, from Epson's own installer (`PERV550_WW_OSX_3940_41.dmg`, from Epson's support
site). Nothing from Epson is in this folder or in the builder app. All Epson material is made on
the user's Mac from the installer they download.

## Build the builder (once, on your Mac)

```sh
cd ~/Downloads/ScannerAppBuilder
sh build_builder.sh
open "build/Scanner App Builder.app"
```

`build/Scanner App Builder.zip` is the copy to share. People who use it need Apple's free
Command Line Tools; the builder offers to install them.

## What the builder does

1. **Opens the installer.** macOS mounts the DMG, then the builder unpacks the package (xar and
   cpio, in plain Python).
2. **Checks it.** The installer must be byte-for-byte the version the recipes were made from
   (SHA-256 of every file used, `engine/installer.json`).
3. **Converts Epson's code.** Twelve engines are translated from their 32-bit Intel code to C:
   FIT, DIGITAL ICE, Image Filter, DTR2, DTR, Auto Photo Orientation, Dust Removal, DDE, Image
   Control, PDF High Compression, PIM JPEG and PIM TIFF. The recipes in `engine/recipes.json` are
   only function addresses and names.
4. **Copies the windows, artwork and help.** Layouts come from the original dialog resources,
   artwork from the icon and picture resources, and Help from the installer. Epson's messages and
   hardware tables are generated into `es_strings.h`, `es_hrd_tables.h` and `es_pdf_tables.h`.
5. **Puts the app together** with `app/build_app.sh` and Apple's compiler.

The finished `EPSON Scan.app` goes to the Downloads folder.

## Folders

- `Builder/` — the builder window (SwiftUI), its Info.plist and icon
- `engine/` — `build.py` and the code it uses:
  - `extract.py`, `macres.py`, `macgfx.py` — read the installer, its resource files and graphics
  - `recomp/` — the code converters
  - `layouts/` — dialog layout exporters
  - `gen/` — data table and text generators
- `app/` — the scanner app's own source, without anything from Epson
- `vendor/` — capstone 5.0.7 (BSD licence, see the wheel's `LICENSE.TXT`), the disassembler the
  converters use

## Checked

Run on Linux from the original DMG, every generated file matched the hand-made app:
- all 12 converted engines are byte-identical;
- all 93 images are pixel-identical;
- all 19 layouts and every Epson file it copies are identical;
- es_adjust.c compiles to identical machine code with its generated table header;
- app.m, with the generated texts put back, is the same source apart from one alert, which now
  uses Epson's four ready-made messages instead of building one.

On a Mac, step 3 takes about a minute and step 5 a few minutes.

## Development

`python3 engine/build.py sources INSTALLER.dmg OUT` stops before compiling and leaves the full
app source in `OUT`. `BUILDER_KEEP_WORK=1` keeps the work folder.
