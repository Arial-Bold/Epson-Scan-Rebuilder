#!/bin/sh
# Builds "Scanner App Builder.app" (and a zip to share) with Apple's Command Line Tools.
set -e
cd "$(dirname "$0")"
APP="build/Scanner App Builder.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
echo "Compiling the builder window..."
swiftc -O -parse-as-library -target arm64-apple-macos12 \
    -o "$APP/Contents/MacOS/Scanner App Builder" Builder/*.swift
cp Builder/Info.plist "$APP/Contents/Info.plist"
iconutil -c icns -o "$APP/Contents/Resources/AppIcon.icns" Builder/AppIcon.iconset
# what the builder works with: its engine, the app's own source, and capstone
for d in engine app vendor; do
    rsync -a --exclude __pycache__ --exclude '*.pyc' --exclude .DS_Store "$d" "$APP/Contents/Resources/"
done
codesign -s - --force --deep "$APP" >/dev/null 2>&1 || true
( cd build && rm -f "Scanner App Builder.zip" && ditto -c -k --keepParent "Scanner App Builder.app" "Scanner App Builder.zip" )
echo "Built $APP"
echo "To share it: build/Scanner App Builder.zip"
