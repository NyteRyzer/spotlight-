#!/usr/bin/env bash
# Builds SPOTLIGHT-<version>-x86_64.AppImage with linuxdeploy + its Qt plugin.
# Usage: packaging/linux/build-appimage.sh [build-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/build-release}"
APPDIR="$BUILD/AppDir"
TOOLS="$BUILD/tools"
ARCH="$(uname -m)"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DSPOTLIGHT_BUILD_TESTS=OFF
cmake --build "$BUILD" --parallel
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD"

mkdir -p "$TOOLS"
for tool in linuxdeploy linuxdeploy-plugin-qt; do
  if [ ! -x "$TOOLS/$tool.AppImage" ]; then
    curl -fsSL -o "$TOOLS/$tool.AppImage" \
      "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-$ARCH.AppImage"
    chmod +x "$TOOLS/$tool.AppImage"
  fi
done

# Ship ffmpeg inside the AppImage so exports work out of the box.
if command -v ffmpeg >/dev/null; then
  install -Dm755 "$(command -v ffmpeg)" "$APPDIR/usr/bin/ffmpeg"
fi

export PATH="$TOOLS:$PATH" APPIMAGE_EXTRACT_AND_RUN=1
export EXTRA_QT_PLUGINS="multimedia" QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
cd "$BUILD"
"$TOOLS/linuxdeploy.AppImage" --appdir "$APPDIR" --plugin qt --output appimage \
  --desktop-file "$APPDIR/usr/share/applications/spotlight.desktop" \
  --icon-file "$ROOT/resources/icons/spotlight.svg"
ls -1 "$BUILD"/*.AppImage
