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

## Stage 2 — Video Studio depth
- [ ] Titles / lower thirds on the timeline (reuse the Design Studio renderer as a title generator)
- [ ] Cross-dissolve and wipe transitions between adjacent clips (FFmpeg `xfade`)
- [ ] Compositing of upper tracks (picture-in-picture, opacity, position/scale per clip)
- [ ] Speed changes, freeze frames, reverse
- [ ] Proxy media for 4K editing; background thumbnail strips along clips
- [ ] Markers, in/out range export, J/K/L shuttle
- [ ] LUT support and three-way colour wheels

## Stage 3 — Audio Studio depth
- [ ] Real-time effect preview (own audio engine on `QAudioSink` instead of per-track `QMediaPlayer`s)
- [ ] Voice-over recording to a track; input monitoring
- [ ] Volume automation lanes and keyframes; crossfades
- [ ] Loudness normalisation to −14 LUFS (YouTube) / −16 LUFS (podcasts) on export, meters
- [ ] More effects: de-esser, gate, reverb, limiter; VST3/AU/LV2 plugin hosting
- [ ] MIDI and a simple step sequencer for music beds

## Stage 4 — Design Studio depth
- [ ] Layer masks and clipping masks, blend modes
- [ ] Background removal for subjects (on-device segmentation model)
- [ ] Gradients, strokes on shapes, text on path, effects (glow, blur)
- [ ] Brush/eraser tools, crop
- [ ] Templates (thumbnail styles), safe-zone overlays for YouTube/Shorts
- [ ] Export the design straight into the timeline (end card / intro)

## Stage 5 — Performance & polish
- [ ] GPU video pipeline (QRhi) and hardware decoding/encoding (VideoToolbox, VA-API, NVENC)
- [ ] Render cache, background export queue
- [ ] Media management: collect project files, relink dialog
- [ ] Localization, accessibility review, onboarding tutorial

## Stage 6 — Windows & distribution
- [ ] Windows build (MSVC), MSIX/NSIS installer, `windeployqt`
- [ ] Code signing + notarization (macOS), signed Windows installer
- [ ] Flatpak on Flathub; auto-update channel
