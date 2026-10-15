# SPOTLIGHT!

**SPOTLIGHT!** is an all-in-one creative production studio: edit video, mix audio and design
thumbnails and channel art in **one project, one window**. It aims to bring the most important
parts of Premiere Pro, Logic Pro and Photoshop together for YouTubers, podcasters, musicians,
filmmakers, and anyone else making things.

```
┌──────────────────────────────────────────────────────────────────────────┐
│ SPOTLIGHT!   [ Video Studio ]   Audio Studio   Design Studio             │
├──────────────────────────────────────────────────────────────────────────┤
│  one shared project: media bin · timeline · mixer · design document     │
└──────────────────────────────────────────────────────────────────────────┘
```

> Status: **Milestone 1 (foundation)**, with early versions of the Audio and Design studios.
> Builds and tests run on **macOS** and **Linux**. Windows comes in a later stage (see [ROADMAP](docs/ROADMAP.md)).

## What works today

### Video Studio
- Import video, audio and images (file dialog, or drag files from Finder/your file manager)
- Media bin with thumbnails, duration badges, missing-file markers
- **Source monitor**: preview clips, mark in/out, *Add to Timeline*
- **Program monitor**: plays the edited timeline (top video track wins, gaps are black)
- **Multitrack timeline**: drag to place, move between tracks, trim edges, split at the playhead (`S`),
  delete / ripple delete, snapping to clip edges and the playhead, zoom, track hide/mute/solo/lock
- Linked audio/video: a video with sound puts its audio on the matching audio track; edits move both
- Per-clip **color** (brightness / contrast / saturation) and **dip-to-black** fades, previewed live
- **Export** with FFmpeg and presets: YouTube 1080p/1080p60/4K, Shorts/Reels vertical, square,
  H.265 archive, VP9/WebM, plus MP3 / M4A / WAV audio-only. Save your own presets.

### Audio Studio
- Waveform timeline of the audio tracks (same project as the Video Studio)
- **Mixer** with per-track volume (dB), pan, mute and solo
- **Track effects**: 3-band EQ, compressor, and one-click *dialogue cleanup* (rumble filter + FFT noise reduction).
  Effects are rendered on export; the preview plays the dry signal.
- Clip gain and fades

### Design Studio
- Canvas presets: YouTube thumbnail (1280×720), channel art (2560×1440), square/portrait posts, story, A4 poster, custom
- Layers: images, text (outline + drop shadow), rectangles/ellipses; move and scale on canvas, rotate, opacity
- Image adjustments (brightness / contrast / saturation)
- **Capture Frame**: grab the current frame from either monitor and it becomes the thumbnail background
- Export PNG or JPEG at full canvas resolution

### Everywhere
- `.spotlight` project files (JSON). Media paths are stored absolute *and* relative, so moving a project folder with its media still works
- 200-step undo/redo shared by all three studios
- **Autosave** (default every 2 minutes) and **crash recovery** on next launch
- **Customizable layouts**: every panel is a dock you can move, float, close; save named layouts per studio; *Window → Reset Layout*
- **Customizable keyboard shortcuts** (*Edit → Keyboard Shortcuts…*)

## Default shortcuts

| Action | Key | Action | Key |
|---|---|---|---|
| Play / Pause | `Space` | Split at playhead | `S` |
| Previous / next frame | `←` / `→` | Delete / Ripple delete | `Del` / `Shift+Del` |
| Go to start / end | `Home` / `End` | Zoom in / out / fit | `=` / `-` / `\` |
| Snapping | `N` | Add selected bin clip at playhead | `.` |
| Import media | `Ctrl+I` | Export video/audio | `Ctrl+M` |
| Export design image | `Ctrl+Shift+M` | Capture program frame to design | `Ctrl+Shift+F` |
| Video / Audio / Design studio | `Ctrl+1` / `2` / `3` | Add text layer | `Ctrl+T` |

(`Ctrl` is `⌘` on macOS.)

## Building

Requirements: CMake ≥ 3.21, a C++17 compiler, **Qt ≥ 6.4** (Core, Gui, Widgets, Multimedia), and
**FFmpeg** (the `ffmpeg` command, used for export and waveforms).

### macOS
```bash
brew install qt ffmpeg cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/src/app/SPOTLIGHT.app
```
Installer: `packaging/macos/make-icns.sh` (needs `brew install librsvg`) then `cd build && cpack` → `SPOTLIGHT-<ver>-macOS-arm64.dmg`
(Qt is bundled by `macdeployqt`). To ship an ffmpeg binary inside the app, configure with
`-DSPOTLIGHT_BUNDLE_FFMPEG=/path/to/static/ffmpeg`.

### Linux (Ubuntu 24.04 / Debian 13)
```bash
sudo apt install cmake ninja-build g++ ffmpeg qt6-base-dev qt6-base-dev-tools qt6-multimedia-dev qt6-qpa-plugins
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/src/app/spotlight
```
Installers: `cd build && cpack` → `.deb` and `.tar.gz`; `packaging/linux/build-appimage.sh` → portable `.AppImage`
(bundles Qt and ffmpeg). Fedora: `sudo dnf install qt6-qtbase-devel qt6-qtmultimedia-devel ffmpeg cmake gcc-c++`.

### Tests
```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```
Unit tests cover timeline editing, project files, undo, autosave/recovery and export command generation;
an end-to-end test renders a real file with FFmpeg, and a GUI smoke test drives the whole window offscreen.

CI (`.github/workflows/build.yml`) builds, tests and packages on Ubuntu 24.04 and macOS 14 (Apple Silicon),
uploading the `.deb`, `.tar.gz`, `.AppImage` and `.dmg` as artifacts.

## Project layout

```
src/core    document model, timeline operations, undo, autosave, export (FFmpeg command builder),
            design renderer, shortcuts — no widgets, fully unit tested
src/media   Qt Multimedia playback: media probing, video view, timeline player, waveforms
src/ui      the three studios, dialogs, main window
src/app     main()
tests/      QtTest suites
packaging/  macOS Info.plist + icon script, Linux .desktop/MIME + AppImage script
docs/       ARCHITECTURE.md, ROADMAP.md
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how it fits together and
[docs/ROADMAP.md](docs/ROADMAP.md) for what comes next.
