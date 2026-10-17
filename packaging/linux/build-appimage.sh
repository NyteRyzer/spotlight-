#!/usr/bin/env bash
# Builds SPOTLIGHT-<version>-x86_64.AppImage with linuxdeploy + its Qt plugin.
# Usage: packaging/linux/build-appimage.sh [build-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$(realpath -m "${1:-$ROOT/build-release}")"
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

# Ship ffmpeg/ffprobe inside the AppImage so import and export work out of
# the box; linuxdeploy bundles their shared libraries via --executable.
EXTRA_EXES=()
for tool in ffmpeg ffprobe; do
  if command -v "$tool" >/dev/null; then
    install -Dm755 "$(command -v "$tool")" "$APPDIR/usr/bin/$tool"
    EXTRA_EXES+=(--executable "$APPDIR/usr/bin/$tool")
  fi
done

export PATH="$TOOLS:$PATH" APPIMAGE_EXTRACT_AND_RUN=1
export EXTRA_QT_PLUGINS="multimedia" QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
cd "$BUILD"
export VERSION="$(grep -A2 '^project(' "$ROOT/CMakeLists.txt" | grep -m1 -oP 'VERSION \K[0-9.]+')"
export OUTPUT="SPOTLIGHT-${VERSION}-${ARCH}.AppImage"
"$TOOLS/linuxdeploy.AppImage" --appdir "$APPDIR" --plugin qt --output appimage \
  --desktop-file "$APPDIR/usr/share/applications/spotlight.desktop" \
  --icon-file "$ROOT/resources/icons/spotlight.svg" "${EXTRA_EXES[@]}"
ls -1 "$BUILD"/*.AppImage
