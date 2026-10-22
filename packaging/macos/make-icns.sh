#!/usr/bin/env bash
# Builds packaging/macos/spotlight.icns from resources/icons/spotlight.png
# using only built-in macOS tools (sips, iconutil).
set -euo pipefail
cd "$(dirname "$0")"
PNG=../../resources/icons/spotlight.png
SET=spotlight.iconset
rm -rf "$SET" && mkdir "$SET"
for s in 16 32 128 256 512; do
  sips -z $s $s "$PNG" --out "$SET/icon_${s}x${s}.png" >/dev/null
  d=$((s * 2)); [ $d -gt 512 ] && d=512
  sips -z $d $d "$PNG" --out "$SET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$SET" -o spotlight.icns
rm -rf "$SET"
echo "Wrote $(pwd)/spotlight.icns"
