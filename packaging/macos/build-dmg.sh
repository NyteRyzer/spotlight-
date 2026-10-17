#!/usr/bin/env bash
# Builds a self-contained SPOTLIGHT-<version>-macOS-<arch>.dmg:
#   * Qt frameworks and plugins bundled by macdeployqt
#   * ffmpeg + ffprobe (and every library they need) bundled into the app,
#     so importing and exporting work without Homebrew
#   * ad-hoc code signature (required to run on Apple Silicon)
#
# Requires: Homebrew packages qt, ffmpeg, cmake, ninja (and librsvg for the icon).
# Usage: packaging/macos/build-dmg.sh [build-dir]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/build-macos}"
mkdir -p "$BUILD" && BUILD="$(cd "$BUILD" && pwd)"
QT_PREFIX="$(brew --prefix qt)"
FFMPEG_PREFIX="$(brew --prefix ffmpeg)"
VERSION="$(grep -A2 '^project(' "$ROOT/CMakeLists.txt" | grep -m1 -oE 'VERSION [0-9.]+' | cut -d' ' -f2)"
ARCH="$(uname -m)"

if [ ! -f "$ROOT/packaging/macos/spotlight.icns" ] && command -v rsvg-convert >/dev/null; then
  "$ROOT/packaging/macos/make-icns.sh"
fi

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DSPOTLIGHT_BUILD_TESTS=OFF
cmake --build "$BUILD"

STAGE="$BUILD/dmg"
APP="$STAGE/SPOTLIGHT.app"
rm -rf "$STAGE" && mkdir -p "$STAGE"
cp -R "$BUILD/src/app/SPOTLIGHT.app" "$APP"

for tool in ffmpeg ffprobe; do
  cp "$FFMPEG_PREFIX/bin/$tool" "$APP/Contents/MacOS/$tool"
  chmod u+w "$APP/Contents/MacOS/$tool"
done

# Deploys Qt plus all non-system dylibs used by the app and the two tools.
"$QT_PREFIX/bin/macdeployqt" "$APP" -always-overwrite \
  -executable="$APP/Contents/MacOS/ffmpeg" \
  -executable="$APP/Contents/MacOS/ffprobe"

# Nothing may still point into Homebrew, or the app breaks on other Macs.
leaks=0
while IFS= read -r -d '' f; do
  if file "$f" | grep -q 'Mach-O'; then
    if otool -L "$f" | tail -n +2 | grep -E '/(opt/homebrew|usr/local)/' >/dev/null; then
      echo "Unbundled dependency in $f:"; otool -L "$f" | grep -E '/(opt/homebrew|usr/local)/'
      leaks=1
    fi
  fi
done < <(find "$APP/Contents" -type f -print0)
[ "$leaks" -eq 0 ] || { echo "error: bundle still references Homebrew libraries"; exit 1; }

# Ad-hoc signature (no Apple Developer ID needed; users approve the first launch).
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

# Smoke test the bundled tools.
"$APP/Contents/MacOS/ffprobe" -version | head -1

ln -s /Applications "$STAGE/Applications"
DMG="$BUILD/SPOTLIGHT-$VERSION-macOS-$ARCH.dmg"
rm -f "$DMG"
hdiutil create -volname "SPOTLIGHT!" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
echo "$DMG"
