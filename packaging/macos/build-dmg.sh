#!/usr/bin/env bash
# Builds a self-contained SPOTLIGHT-<version>-macOS-<arch>.dmg:
#   * Qt frameworks and plugins bundled by macdeployqt
#   * ffmpeg + ffprobe (and every library they need) bundled into the app,
#     so importing and exporting work without Homebrew
#   * ad-hoc code signature (required to run on Apple Silicon)
#
# Requires: Homebrew packages qt, ffmpeg, cmake, ninja.
# Usage: packaging/macos/build-dmg.sh [build-dir]
#
# Optional environment overrides (used by the Intel build, which targets older
# macOS than Homebrew's bottles support):
#   QT_PREFIX       Qt installation to build against (default: brew --prefix qt)
#   FFMPEG_BIN      directory holding ffmpeg + ffprobe (default: Homebrew's)
#   DEPLOY_TARGET   minimum macOS version (default: 14.0)
#   DMG_LABEL       platform label in the file name (default: uname -m)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/build-macos}"
mkdir -p "$BUILD" && BUILD="$(cd "$BUILD" && pwd)"
QT_PREFIX="${QT_PREFIX:-$(brew --prefix qt)}"
FFMPEG_BIN="${FFMPEG_BIN:-$(brew --prefix ffmpeg)/bin}"
DEPLOY_TARGET="${DEPLOY_TARGET:-14.0}"
VERSION="$(grep -A2 '^project(' "$ROOT/CMakeLists.txt" | grep -m1 -oE 'VERSION [0-9.]+' | cut -d' ' -f2)"
DMG_LABEL="${DMG_LABEL:-$(uname -m)}"

if [ ! -f "$ROOT/packaging/macos/spotlight.icns" ]; then
  "$ROOT/packaging/macos/make-icns.sh"
fi

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOY_TARGET" -DSPOTLIGHT_BUILD_TESTS=OFF
cmake --build "$BUILD"

STAGE="$BUILD/dmg"
APP="$STAGE/SPOTLIGHT.app"
rm -rf "$STAGE" && mkdir -p "$STAGE"
cp -R "$BUILD/src/app/SPOTLIGHT.app" "$APP"

for tool in ffmpeg ffprobe; do
  cp "$FFMPEG_BIN/$tool" "$APP/Contents/MacOS/$tool"
  chmod u+w "$APP/Contents/MacOS/$tool"
done

# Deploys Qt plus all non-system dylibs used by the app and the two tools.
# Library search paths for dependencies referenced via @rpath.
LIBPATHS=(-libpath="$QT_PREFIX/lib" -libpath="$FFMPEG_BIN/../lib")
if command -v brew >/dev/null; then
  LIBPATHS+=(-libpath="$(brew --prefix)/lib")
fi
"$QT_PREFIX/bin/macdeployqt" "$APP" -always-overwrite "${LIBPATHS[@]}" \
  -executable="$APP/Contents/MacOS/ffmpeg" \
  -executable="$APP/Contents/MacOS/ffprobe"

# Drop library search paths (LC_RPATH) that point outside the bundle, e.g.
# the build tree's /opt/homebrew/opt/qt/lib. They're harmless on a clean
# Mac but make the app load a second, mismatched Qt on Macs that have
# Homebrew's Qt installed. Bundle-relative (@...) paths are kept.
while IFS= read -r -d '' f; do
  file -b "$f" | grep -q Mach-O || continue
  otool -l "$f" | awk '/LC_RPATH/{getline; getline; print $2}' | grep -v '^@' | sort -u | while read -r rp; do
    chmod u+w "$f"
    while install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null; do :; done
    echo "removed rpath $rp from ${f#$APP/}"
  done
done < <(find "$APP/Contents" -type f -print0)
# The app must still find its own frameworks.
otool -l "$APP/Contents/MacOS/SPOTLIGHT" | grep -q '@executable_path/../Frameworks' \
  || install_name_tool -add_rpath @executable_path/../Frameworks "$APP/Contents/MacOS/SPOTLIGHT"

# Nothing may still point into Homebrew or the Qt install, or the app breaks
# on other Macs.
leaks=0
while IFS= read -r -d '' f; do
  if file "$f" | grep -q 'Mach-O'; then
    # Skip the library's own install name (otool -D); only real load
    # commands matter.
    own="$(otool -D "$f" 2>/dev/null | tail -n +2 | head -1)"
    deps="$(otool -L "$f" | tail -n +2 | awk '{print $1}' | grep -vxF "${own:-<none>}" || true)"
    if echo "$deps" | grep -E "^(/opt/homebrew/|/usr/local/|$QT_PREFIX/)" >/dev/null; then
      echo "Unbundled dependency in $f:"; echo "$deps" | grep -E "^(/opt/homebrew/|/usr/local/|$QT_PREFIX/)"
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
DMG="$BUILD/SPOTLIGHT-$VERSION-macOS-$DMG_LABEL.dmg"
rm -f "$DMG"
hdiutil create -volname "SPOTLIGHT!" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
echo "$DMG"
