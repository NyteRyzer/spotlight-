#!/usr/bin/env bash
# Builds packaging/macos/spotlight.icns from resources/icons/spotlight.svg.
# Requires macOS (sips, iconutil) plus rsvg-convert (brew install librsvg).
set -euo pipefail
cd "$(dirname "$0")"
SVG=../../resources/icons/spotlight.svg
SET=spotlight.iconset
rm -rf "$SET" && mkdir "$SET"
for s in 16 32 128 256 512; do
  rsvg-convert -w $s -h $s "$SVG" -o "$SET/icon_${s}x${s}.png"
  rsvg-convert -w $((s*2)) -h $((s*2)) "$SVG" -o "$SET/icon_${s}x${s}@2x.png"
done
iconutil -c icns "$SET" -o spotlight.icns
rm -rf "$SET"
echo "Wrote $(pwd)/spotlight.icns"
