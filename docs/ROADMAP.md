# Roadmap

SPOTLIGHT! is built in stages instead of trying to recreate three huge applications at once.

## Stage 1 — Foundation ✅ (this release)
- [x] Desktop app on Qt 6, modular CMake build (core / media / ui / app)
- [x] Import video, audio and images; media bin with thumbnails
- [x] Source and program monitors; timeline playback
- [x] Multitrack timeline: place, move, trim, split, delete, ripple delete, snapping, zoom, linked A/V
- [x] Save / open projects, relative media relinking, undo/redo
- [x] Autosave and crash recovery
- [x] Export via FFmpeg with presets (YouTube, Shorts, 4K, archive, audio-only) and user presets
- [x] Customizable dock layouts and keyboard shortcuts
- [x] macOS (.dmg) and Linux (.deb, .tar.gz, AppImage) packaging + CI

## Stage 2 — Pro features ✅ (v0.2)
- [x] Layered compositing: picture-in-picture, opacity, position/scale/rotation/crop per clip
- [x] Transitions (FFmpeg `xfade`, previewed live), titles and lower thirds, SRT captions
- [x] Speed, reverse, freeze frame, insert edits, close gaps, markers
- [x] Looks/filters, temperature/tint, vignette, blur, chroma key, Ken Burns, stabilization
- [x] Remove silences, scene edit detection, beat markers, auto reframe
- [x] Voiceover recording, gate, de-esser, voice changer, reverb, echo, limiter, auto-ducking, loudness targets
- [x] Design: blend modes, drop shadow/glow/stroke, background removal, crop, fade masks, brush, more shapes, templates

## Stage 3 — Next up
- [ ] Keyframes for transform, opacity and volume (automation lanes)
- [ ] Automatic captions (on-device speech-to-text, e.g. whisper.cpp)
- [ ] AI background removal for complex scenes (segmentation model)
- [ ] Real-time audio effect preview (own audio engine on `QAudioSink`), level meters
- [ ] Proxy media for 4K, LUT import and colour wheels, J/K/L shuttle
- [ ] MIDI, step sequencer and VST3/AU/LV2 hosting
- [ ] Text on a path, clipping masks, more templates

## Stage 5 — Performance & polish
- [ ] GPU video pipeline (QRhi) and hardware decoding/encoding (VideoToolbox, VA-API, NVENC)
- [ ] Render cache, background export queue
- [ ] Media management: collect project files, relink dialog
- [ ] Localization, accessibility review, onboarding tutorial

## Stage 6 — Windows & distribution
- [ ] Windows build (MSVC), MSIX/NSIS installer, `windeployqt`
- [ ] Code signing + notarization (macOS), signed Windows installer
- [ ] Flatpak on Flathub; auto-update channel
