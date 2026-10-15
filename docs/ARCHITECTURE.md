# Architecture

SPOTLIGHT! is a C++17 / Qt 6 application split into four layers. Each layer only depends on the ones below it.

```
 app    main(): theme, command line, macOS "open file" events
  │
 ui     MainWindow ─┬─ VideoStudio  (MediaBin, SourceMonitor, ProgramMonitor, TimelineWidget, ClipInspector)
  │                 ├─ AudioStudio  (TimelineWidget[audio], Mixer/ChannelStrip, TrackFxPanel, ClipInspector)
  │                 └─ DesignStudio (DesignCanvas, layers, properties)
  │                    dialogs: Export, Shortcuts, Settings, Recovery
  │
 media  MediaProbe (metadata + thumbnail), VideoView (frame display), TimelinePlayer (program playback),
  │     WaveformCache (peaks via ffmpeg / QAudioDecoder)               — Qt Multimedia, FFmpeg backend
  │
 core   ProjectData (plain value type) ── Project (QObject + undo) ── AutosaveManager
        timeline:: operations · Exporter (FFmpeg filter graph) · DesignRenderer · ShortcutManager
```

## One project, three studios

`ProjectData` holds *everything*: media bin, tracks and clips, sequence settings and the design document.
All three studios observe the same `Project` and redraw on its `changed()` signal. That is what makes the
workflows connect: audio edited in the Audio Studio is the same clip you see in the Video Studio, and a frame
captured from the program monitor becomes a layer in the Design Studio.

## Editing model

* Every change goes through `Project::edit(text, fn)`. It copies the data, runs `fn` on the copy and pushes a
  `SnapshotCommand(before, after)` on the `QUndoStack` if anything changed. Qt containers are implicitly shared,
  so snapshots are cheap. Continuous controls (sliders) pass a *merge key* so a drag is one undo step.
* `timeline::` functions are pure operations on `ProjectData` with documented invariants: clips on a track are
  sorted and never overlap, never start before 0, and are never shorter than a frame. Placing/moving uses
  overwrite semantics (like most NLEs). Linked clips (video + its audio) move, trim, split and delete together.
* Interactive drags (timeline, design canvas) edit a *preview copy* and commit once on mouse release.

## Playback

`TimelinePlayer` keeps a master clock. On each tick it asks `timeline::topVideoClipAt()` which clip should be
visible and drives one `QMediaPlayer` into the `VideoView`; one more player per audio track handles sound.
Players are switched at clip boundaries and re-seeked when they drift more than 120 ms. Track volume, clip
gain, mute/solo and fades are applied live; colour correction and dip-to-black fades are applied by `VideoView`
using the same maths as FFmpeg's `eq` filter so preview matches export.

## Export

`Exporter::buildArguments()` turns the project into one FFmpeg command:

1. `timeline::videoSegments()` flattens the video tracks into consecutive segments (top track wins, gaps black).
2. Each segment is an input seeked to its source in-point, scaled/padded to the output size, colour corrected,
   faded, then all segments are `concat`-ed.
3. Each audible audio clip is trimmed, gained, faded and `adelay`-ed to its timeline position, mixed per track,
   run through the track's effects (`highpass`+`afftdn`, `bass`/`equalizer`/`treble`, `acompressor`),
   volume and pan, then mixed to the master and padded to the timeline length.

The command builder is pure and unit tested; `Exporter` runs it with `QProcess` and reads `-progress`.

## Persistence

* Project files are JSON (`"format": "spotlight-project"`, versioned) written atomically with `QSaveFile`.
* `AutosaveManager` writes `<app data>/autosave/<project-id>.spotlight` when there are unsaved changes and deletes
  it on save or clean exit. Anything left at startup means the last session was interrupted → recovery dialog.
* Layouts (`QMainWindow::saveState` per studio), named layouts, shortcuts, recent files and preferences live in `QSettings`.

## Platform notes

* Qt Multimedia's **FFmpeg backend** is selected in `main()` so decoding behaves the same on macOS and Linux.
* GUI apps on macOS don't inherit the shell `PATH`, so `Exporter::findFfmpeg()` checks the app bundle first, then
  Homebrew/MacPorts locations.
