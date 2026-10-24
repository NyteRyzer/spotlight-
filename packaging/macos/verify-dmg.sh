#!/usr/bin/env bash
# Verifies a built .dmg the way a user receives it: mounts it, checks the
# code signature and bundle metadata, runs the app's built-in self-test from
# the mounted image (bundled FFmpeg, Qt Multimedia, rendering, export), and
# launches the GUI.
# Usage: packaging/macos/verify-dmg.sh <file.dmg> [expected-arch]
set -euo pipefail

DMG="$1"
ARCH="${2:-}"
MNT="$(mktemp -d)/spotlight-dmg"
OUT="$(mktemp -d)/self-test"
mkdir -p "$MNT"

hdiutil attach "$DMG" -nobrowse -readonly -mountpoint "$MNT" >/dev/null
trap 'hdiutil detach "$MNT" -force >/dev/null 2>&1 || true' EXIT
APP="$MNT/SPOTLIGHT.app"
BIN="$APP/Contents/MacOS/SPOTLIGHT"

echo "== Contents"
ls -la "$MNT"
[ -L "$MNT/Applications" ] || { echo "missing Applications shortcut in the dmg"; exit 1; }
for tool in ffmpeg ffprobe; do
  f="$(find "$APP/Contents" -type f -name "$tool" | head -1)"
  [ -n "$f" ] || { echo "$tool is not bundled"; exit 1; }
  echo "$tool: ${f#$APP/} ($(lipo -archs "$f"))"
done

echo "== Architecture"
lipo -archs "$BIN"
if [ -n "$ARCH" ]; then lipo -archs "$BIN" | grep -qw "$ARCH"; fi

echo "== Code signature"
# An invalid signature makes macOS report the app as "damaged".
codesign --verify --deep --strict --verbose=2 "$APP"

echo "== Info.plist"
for key in CFBundleIdentifier CFBundleShortVersionString NSMicrophoneUsageDescription LSMinimumSystemVersion; do
  printf '%s: ' "$key"
  plutil -extract "$key" raw "$APP/Contents/Info.plist"
done

echo "== Self-test (from the mounted image)"
"$BIN" --self-test "$OUT"

echo "== GUI launch"
"$BIN" &
pid=$!
sleep 10
kill -0 "$pid" || { echo "SPOTLIGHT exited early"; exit 1; }
kill "$pid"
wait "$pid" 2>/dev/null || true
echo "dmg verified: $DMG"
