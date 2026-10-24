#pragma once

#include "core/types.h"

#include <QColor>
#include <QString>
#include <QUuid>
#include <QVector>

namespace spotlight {

// Per-clip colour correction and looks (Video Studio).
//
// brightness/contrast/saturation follow FFmpeg's eq filter. temperature,
// tint and mono are folded into a single 3x3 channel mixer (see looks.h) so
// the preview can apply exactly the matrix the export uses.
enum class MonoMode { None, BlackWhite, Sepia };

struct ColorAdjust {
    double brightness = 0.0;  // -1 .. 1
    double contrast = 1.0;    //  0 .. 2
    double saturation = 1.0;  //  0 .. 3
    double temperature = 0.0; // -1 (cool) .. 1 (warm)
    double tint = 0.0;        // -1 (magenta) .. 1 (green)
    MonoMode mono = MonoMode::None;
    double vignette = 0.0;    //  0 .. 1
    double blur = 0.0;        //  0 .. 1 (Gaussian, up to ~2% of the frame height)

    bool isEqIdentity() const
    {
        return qFuzzyIsNull(brightness) && qFuzzyCompare(contrast, 1.0) && qFuzzyCompare(saturation, 1.0);
    }
    bool hasMatrix() const { return !qFuzzyIsNull(temperature) || !qFuzzyIsNull(tint) || mono != MonoMode::None; }
    bool isIdentity() const
    {
        return isEqIdentity() && !hasMatrix() && qFuzzyIsNull(vignette) && qFuzzyIsNull(blur);
    }
    bool operator==(const ColorAdjust &o) const
    {
        return qFuzzyCompare(1 + brightness, 1 + o.brightness) && qFuzzyCompare(contrast, o.contrast)
            && qFuzzyCompare(saturation, o.saturation) && qFuzzyCompare(1 + temperature, 1 + o.temperature)
            && qFuzzyCompare(1 + tint, 1 + o.tint) && mono == o.mono && qFuzzyCompare(1 + vignette, 1 + o.vignette)
            && qFuzzyCompare(1 + blur, 1 + o.blur);
    }
    bool operator!=(const ColorAdjust &o) const { return !(*this == o); }
};

// Position, size and crop of a clip's picture inside the output frame.
// The picture is first fitted (letterboxed) into the frame, then cropped,
// scaled about the frame centre, rotated and offset.
struct Transform {
    double scale = 1.0;    // 0.05 .. 5
    double x = 0.0;        // offset as a fraction of the frame width
    double y = 0.0;        // offset as a fraction of the frame height
    double rotation = 0.0; // degrees, clockwise
    double opacity = 1.0;  // 0 .. 1
    bool flipH = false;
    bool flipV = false;
    double cropLeft = 0.0, cropTop = 0.0, cropRight = 0.0, cropBottom = 0.0; // fractions of the source

    bool hasCrop() const
    {
        return cropLeft > 0 || cropTop > 0 || cropRight > 0 || cropBottom > 0;
    }
    bool isIdentity() const
    {
        return qFuzzyCompare(scale, 1.0) && qFuzzyIsNull(x) && qFuzzyIsNull(y) && qFuzzyIsNull(rotation)
            && qFuzzyCompare(opacity, 1.0) && !flipH && !flipV && !hasCrop();
    }
    bool operator==(const Transform &o) const
    {
        return qFuzzyCompare(scale, o.scale) && qFuzzyCompare(1 + x, 1 + o.x) && qFuzzyCompare(1 + y, 1 + o.y)
            && qFuzzyCompare(1 + rotation, 1 + o.rotation) && qFuzzyCompare(opacity, o.opacity) && flipH == o.flipH
            && flipV == o.flipV && qFuzzyCompare(1 + cropLeft, 1 + o.cropLeft)
            && qFuzzyCompare(1 + cropTop, 1 + o.cropTop) && qFuzzyCompare(1 + cropRight, 1 + o.cropRight)
            && qFuzzyCompare(1 + cropBottom, 1 + o.cropBottom);
    }
};

// Animated "Ken Burns" camera move over the clip's length.
enum class Motion { None, ZoomIn, ZoomOut, PanLeft, PanRight, PanUp, PanDown };

// Green/blue screen removal (FFmpeg chromakey semantics).
struct ChromaKey {
    bool enabled = false;
    QColor color = QColor(0, 255, 0);
    double similarity = 0.15; // 0.01 .. 1
    double blend = 0.08;      // 0 .. 1

    bool operator==(const ChromaKey &o) const
    {
        return enabled == o.enabled && color == o.color && qFuzzyCompare(similarity, o.similarity)
            && qFuzzyCompare(1 + blend, 1 + o.blend);
    }
};

// A transition from the previous clip on the same track into this clip. It
// starts at this clip's start and lasts `duration`; the outgoing clip keeps
// playing underneath (using media past its out-point when available,
// otherwise holding its last frame), so clip timing never changes.
struct Transition {
    QString type;      // empty = none; see transitions() in looks.h
    TimeUs duration = 0;

    bool isNone() const { return type.isEmpty() || duration <= 0; }
    bool operator==(const Transition &o) const { return type == o.type && duration == o.duration; }
};

// A placed piece of media on a track.
//
// The clip occupies [start, start + duration) on the timeline and plays the
// source range [in, out) of its media item.
struct Clip {
    QUuid id;
    QUuid mediaId;
    QUuid linkId;          // clips sharing a non-null linkId move/trim/split together
    TimeUs start = 0;      // timeline position
    TimeUs in = 0;         // source in-point
    TimeUs out = 0;        // source out-point
    double speed = 1.0;    // playback rate (0.1 .. 8); the clip lasts (out - in) / speed
    double gain = 1.0;     // linear audio gain (audio tracks)
    TimeUs fadeIn = 0;     // video: fade from transparent (black on V1), audio: fade in
    TimeUs fadeOut = 0;    // video: fade to transparent, audio: fade out
    ColorAdjust color;     // video tracks only
    Transform transform;   // video tracks only
    Motion motion = Motion::None;
    ChromaKey key;
    Transition transition; // video tracks only
    bool stabilize = false; // video: smooth camera shake on export

    TimeUs sourceDuration() const { return out - in; }
    TimeUs duration() const
    {
        return qFuzzyCompare(speed, 1.0) ? out - in : TimeUs(double(out - in) / speed);
    }
    TimeUs end() const { return start + duration(); }
    bool contains(TimeUs t) const { return t >= start && t < end(); }
    // Source time shown at timeline time t (t inside the clip).
    TimeUs sourceAt(TimeUs t) const { return in + toSource(t - start); }
    // A timeline duration converted to source duration and back. Rounded
    // down so pieces produced by cuts never overlap their neighbours.
    TimeUs toSource(TimeUs timelineDelta) const
    {
        return qFuzzyCompare(speed, 1.0) ? timelineDelta : TimeUs(double(timelineDelta) * speed);
    }
    TimeUs toTimeline(TimeUs sourceDelta) const
    {
        return qFuzzyCompare(speed, 1.0) ? sourceDelta : TimeUs(double(sourceDelta) / speed);
    }
};

enum class TrackType { Video, Audio };

// Export-time audio processing for a track (Audio Studio).
struct AudioFx {
    bool eqEnabled = false;
    double lowGainDb = 0.0;   // shelf around 100 Hz
    double midGainDb = 0.0;   // peak around 1 kHz
    double highGainDb = 0.0;  // shelf around 8 kHz

    bool compressorEnabled = false;
    double thresholdDb = -18.0;
    double ratio = 3.0;

    bool dialogueCleanup = false; // high-pass + FFT denoise

    bool gateEnabled = false;     // noise gate: silences room tone between words
    double gateThresholdDb = -45.0;

    bool deEsser = false;         // tames harsh "s" sounds

    double pitchSemitones = 0.0;  // voice changer, -12 .. 12

    bool reverbEnabled = false;
    double reverbMix = 0.3;       // 0 .. 1
    double reverbSize = 0.5;      // 0 (small room) .. 1 (hall)

    bool echoEnabled = false;     // slap / tempo delay
    double echoDelayMs = 350.0;
    double echoFeedback = 0.35;   // 0 .. 0.9

    bool limiterEnabled = false;  // brick-wall ceiling
    double limiterCeilingDb = -1.0;

    // Auto-ducking: lower this track while clips play on any track marked
    // as a voice track (e.g. music under dialogue).
    bool duckUnderVoice = false;
    double duckAmountDb = -12.0;

    bool isNeutral() const
    {
        return !eqEnabled && !compressorEnabled && !dialogueCleanup && !gateEnabled && !deEsser
            && qFuzzyIsNull(pitchSemitones) && !reverbEnabled && !echoEnabled && !limiterEnabled;
    }
};

struct Track {
    QUuid id;
    TrackType type = TrackType::Video;
    QString name;
    bool muted = false;   // audio: silent, video: hidden
    bool solo = false;    // audio only
    bool locked = false;
    double volume = 1.0;  // linear, audio only
    double pan = 0.0;     // -1 (left) .. 1 (right), audio only
    bool voice = false;   // audio: dialogue track that ducks tracks with duckUnderVoice
    AudioFx fx;
    QVector<Clip> clips;  // kept sorted by start, never overlapping

    bool isVideo() const { return type == TrackType::Video; }
    bool isAudio() const { return type == TrackType::Audio; }
};

} // namespace spotlight
