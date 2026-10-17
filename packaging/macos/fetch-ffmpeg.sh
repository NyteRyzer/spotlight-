#!/usr/bin/env bash
# Downloads self-contained (static) ffmpeg + ffprobe binaries for macOS into
# <out-dir>, for builds that should not depend on Homebrew (e.g. the Intel
# build targeting older macOS). Tries two well-known static build providers.
# Usage: packaging/macos/fetch-ffmpeg.sh <out-dir> [x86_64|arm64]
set -euo pipefail

OUT="$1"
ARCH="${2:-$(uname -m)}"
case "$ARCH" in
  x86_64) RIEDL_ARCH=amd64 ;;
  arm64) RIEDL_ARCH=arm64 ;;
  *) echo "unsupported arch $ARCH"; exit 1 ;;
esac
mkdir -p "$OUT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fetch() { # tool
  local tool="$1"
  local urls=("https://ffmpeg.martin-riedl.de/redirect/latest/macos/$RIEDL_ARCH/release/$tool.zip")
  # evermeet.cx only publishes Intel builds.
  [ "$ARCH" = x86_64 ] && urls+=("https://evermeet.cx/ffmpeg/getrelease/$tool/zip")
  for url in "${urls[@]}"; do
    echo "Downloading $tool from $url"
    rm -rf "$TMP/$tool" && mkdir -p "$TMP/$tool"
    if curl -fsSL --retry 3 -o "$TMP/$tool.zip" "$url" && unzip -q -o "$TMP/$tool.zip" -d "$TMP/$tool"; then
      local bin
      bin="$(find "$TMP/$tool" -type f -name "$tool" | head -1)"
      if [ -n "$bin" ] && lipo -archs "$bin" | grep -qw "$ARCH"; then
        install -m 755 "$bin" "$OUT/$tool"
        # Must not depend on anything outside the OS.
        if otool -L "$OUT/$tool" | tail -n +2 | grep -vE '^\s*(/usr/lib/|/System/)' | grep -q .; then
          echo "$tool from $url links non-system libraries; trying next source"
          otool -L "$OUT/$tool"
          continue
        fi
        "$OUT/$tool" -hide_banner -version | head -1
        return 0
      fi
      echo "$tool from $url is not $ARCH; trying next source"
    fi
  done
  echo "error: could not fetch a static $ARCH $tool"
  return 1
}

fetch ffmpeg
fetch ffprobe
