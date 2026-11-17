# Scanner App Builder

Build a native Apple Silicon version of **Epson Scan 3.9.4** for the **Epson Perfection V550**, on
your own Mac, from Epson's own installer.

Epson Scan 3.9.4 was a 32-bit app, so it stopped working when macOS dropped 32-bit support. This
builder turns it into a native app again, with DIGITAL ICE, all four modes and Epson's image
processing intact. Nothing from Epson is in this repository or in the builder. Everything Epson is
made on your Mac from the installer you download from Epson.

**[Download the latest release](../../releases/latest)**

## What you get

- **All four modes:** Full Auto, Home, Office and Professional
- **DIGITAL ICE** dust and scratch removal for film
- **Epson's own image processing**, converted from its original code: auto exposure, Unsharp Mask,
  Grain Reduction, Descreening, Dust Removal, Color Restoration, Backlight Correction, Text
  Enhancement and Color Smoothing
- **Film frame and document detection**, Thumbnail preview, Auto Photo Orientation, and film holder
  detection in Full Auto
- **Every file format Epson Scan saved:** JPEG, TIFF, Multi-TIFF, PDF (including High Compression),
  PICT and PRINT Image Matching II
- **Two looks:** the original classic Aqua controls, or native macOS controls

## What you need

- A Mac with Apple Silicon running macOS 12 or later
- **Epson Scan 3.9.4 for the Perfection V550** (`PERV550_WW_OSX_3940_41.dmg`) from
  [Epson's V550 support page](https://epson.com/Support/Scanners/Perfection-Series/Epson-Perfection-V550-Photo/s/SPT_B11B210201?review-filter=macOS+10.14.x)
  (Downloads › Scanner Driver and EPSON Scan Utility)
- Apple's free **Command Line Tools**. The builder offers to install them if they're missing.
- The **Epson Scanner ICA Driver**, installed, to scan with the finished app

## How to use it

1. Download **Scanner App Builder.zip** from the [Releases](../../releases) page and unzip it.
2. Open **Scanner App Builder**. It isn't signed with an Apple Developer ID, so the first time,
   Control-click it, choose **Open**, then click **Open** again.
3. Drop the Epson installer onto the window and click **Build**. It takes a few minutes.
4. **EPSON Scan.app** appears in your Downloads folder. Move it to Applications.

The builder only accepts the exact installer it was made for. Every file it uses is checked before
building, so a different version or another scanner's installer is turned away.

## How it works

1. **Opens the installer.** macOS mounts the disk image, then the builder unpacks the package
   (xar and cpio, in plain Python).
2. **Checks it.** Every file used must match, byte for byte, the version the recipes were made
   from (SHA-256 hashes in `engine/installer.json`).
3. **Converts Epson's code.** Twelve engines are translated from 32-bit Intel code to C: FIT,
   DIGITAL ICE, Image Filter, DTR2, DTR, Auto Photo Orientation, Dust Removal, DDE, Image Control,
   PDF High Compression, PIM JPEG and PIM TIFF. The recipes in `engine/recipes.json` hold only
   function addresses and names.
4. **Copies the windows, artwork and help.** Layouts come from the original dialog resources,
   artwork from the icon and picture resources, and Help from the installer. Epson's messages and
   hardware tables are generated into `es_strings.h`, `es_hrd_tables.h` and `es_pdf_tables.h`.
5. **Puts the app together** with `app/build_app.sh` and Apple's compiler.

## Building the builder yourself

GitHub builds the builder from this source on an Apple Silicon Mac every time the code changes
(see `.github/workflows/build.yml`). The zip is attached to each release and to every run on the
**Actions** tab. To build it on your own Mac:

```sh
cd ScannerAppBuilder
sh build_builder.sh
open "build/Scanner App Builder.app"
```

`build/Scanner App Builder.zip` is the copy to share.

For development, `python3 engine/build.py sources INSTALLER.dmg OUT` stops before compiling and
leaves the full app source in `OUT`. Set `BUILDER_KEEP_WORK=1` to keep the work folder.

## Folders

- `Builder/`: the builder window (SwiftUI), its Info.plist and icon
- `engine/`: `build.py` and the code it uses
  - `extract.py`, `macres.py`, `macgfx.py` read the installer, its resource files and graphics
  - `recomp/` holds the code converters
  - `layouts/` holds the dialog layout exporters
  - `gen/` holds the data table and text generators
- `app/`: the scanner app's own source, without anything from Epson
- `vendor/`: capstone 5.0.7 (BSD licence, see the wheel's `LICENSE.TXT`), the disassembler the
  converters use

## Please note

- Made for the Perfection V550 only.
- This is an independent project. It isn't made, endorsed or supported by Epson. Epson, EPSON Scan
  and DIGITAL ICE are trademarks of their owners.
- **Never commit Epson's installer or anything made from it**, and please don't attach them to
  issues or pull requests. `.gitignore` already blocks disk images and packages.
