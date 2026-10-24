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

echo "== Static dependency scan"
# Every Mach-O file may only link the OS or something inside the bundle.
bad=0
while IFS= read -r f; do
  file -b "$f" | grep -q Mach-O || continue
  own="$(otool -D "$f" 2>/dev/null | tail -n +2 | head -1)" # a library's own install name isn't a load
  deps="$(otool -L "$f" | tail -n +2 | awk '{print $1}' | grep -vxF "${own:-<none>}" \
          | grep -vE '^(@rpath|@loader_path|@executable_path|/usr/lib/|/System/)' || true)"
  rpaths="$(otool -l "$f" | awk '/LC_RPATH/{getline; getline; print $2}' | grep -v '^@' || true)"
  if [ -n "$deps$rpaths" ]; then
    echo "outside reference in ${f#$APP/}:"; echo "$deps" "$rpaths" | sed 's/^/    /'
    bad=1
  fi
done < <(find "$APP/Contents" -type f \( -perm -u+x -o -name '*.dylib' \))
[ "$bad" -eq 0 ] || { echo "the bundle references files outside itself"; exit 1; }

echo "== Self-test (from the mounted image), auditing every loaded library"
DYLD_PRINT_LIBRARIES=1 "$BIN" --self-test "$OUT" 2> "$OUT.dyld" || { tail -50 "$OUT.dyld"; exit 1; }
leaks="$(grep -oE '/(opt/homebrew|usr/local|Users)/[^ ]+' "$OUT.dyld" | sort -u || true)"
if [ -n "$leaks" ]; then
  echo "The app loaded libraries from outside the bundle (they won't exist on users' Macs):"
  echo "$leaks" | sed 's/^/    /'
  exit 1
fi
echo "every loaded library came from the bundle or macOS"

echo "== Self-test on a simulated clean Mac (Homebrew and build Qt hidden)"
hidden=()
for d in /opt/homebrew /usr/local/Cellar /usr/local/opt "$HOME/Qt" "$HOME/ffmpeg-static"; do
  if [ -e "$d" ]; then sudo mv "$d" "$d.hidden-for-test"; hidden+=("$d"); fi
done
restore() { for d in "${hidden[@]}"; do sudo mv "$d.hidden-for-test" "$d"; done; }
set +e
"$BIN" --self-test "$OUT-clean"
code=$?
set -e
restore
[ "$code" -eq 0 ] || { echo "self-test failed without Homebrew/Qt present"; exit 1; }

echo "== GUI launch"
"$BIN" &
pid=$!
sleep 10
kill -0 "$pid" || { echo "SPOTLIGHT exited early"; exit 1; }
kill "$pid"
wait "$pid" 2>/dev/null || true
echo "dmg verified: $DMG"
