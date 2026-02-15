#!/usr/bin/env bash
# Create AppIcon.icns from AppIcon.iconset for macOS app bundle / Dock icon.
# Pre-made icons are already in AppIcon.iconset. Run from project root:
#   ./scripts/make_icns.sh
# If you see "Invalid Iconset", run this from Terminal (outside Cursor), or
# create AppIcon.icns in Xcode (File > New > App Icons) and put it in the project root.

set -e
cd "$(dirname "$0")/.."
ICONSET=AppIcon.iconset
OUT=AppIcon.icns

[[ -d "$ICONSET" ]] || { echo "Missing $ICONSET"; exit 1; }
iconutil -c icns "$ICONSET" -o "$OUT"
echo "Created $OUT"
