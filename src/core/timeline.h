#pragma once

#include "core/types.h"

#include <QString>
#include <QUuid>
#include <QVector>

namespace spotlight {

// Basic per-clip color correction (Video Studio).
struct ColorAdjust {
    double brightness = 0.0; // -1 .. 1
    double contrast = 1.0;   //  0 .. 2
    double saturation = 1.0; //  0 .. 3

    bool isIdentity() const
    {
        return qFuzzyIsNull(brightness) && qFuzzyCompare(contrast, 1.0) && qFuzzyCompare(saturation, 1.0);
    }
    bool operator==(const ColorAdjust &o) const
    {
        return qFuzzyCompare(1 + brightness, 1 + o.brightness) && qFuzzyCompare(contrast, o.contrast)
            && qFuzzyCompare(saturation, o.saturation);
    }
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
    double gain = 1.0;     // linear audio gain (audio tracks)
    TimeUs fadeIn = 0;     // video: dip from black, audio: fade in
    TimeUs fadeOut = 0;    // video: dip to black, audio: fade out
    ColorAdjust color;     // video tracks only

    TimeUs duration() const { return out - in; }
    TimeUs end() const { return start + duration(); }
    bool contains(TimeUs t) const { return t >= start && t < end(); }
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

    bool isNeutral() const { return !eqEnabled && !compressorEnabled && !dialogueCleanup; }
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
    AudioFx fx;
    QVector<Clip> clips;  // kept sorted by start, never overlapping

    bool isVideo() const { return type == TrackType::Video; }
    bool isAudio() const { return type == TrackType::Audio; }
};

} // namespace spotlight
