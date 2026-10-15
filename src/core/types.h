#pragma once

#include <QString>
#include <QtGlobal>

namespace spotlight {

// All timeline positions and durations are stored in microseconds.
using TimeUs = qint64;

constexpr TimeUs kUsPerMs = 1000;
constexpr TimeUs kUsPerSecond = 1000000;

// Smallest clip length the editor allows (roughly one frame at 30 fps).
constexpr TimeUs kMinClipDuration = kUsPerSecond / 30;

// Default length of a still image placed on the timeline.
constexpr TimeUs kDefaultStillDuration = 5 * kUsPerSecond;

inline TimeUs msToUs(qint64 ms) { return ms * kUsPerMs; }
inline qint64 usToMs(TimeUs us) { return us / kUsPerMs; }
inline double usToSeconds(TimeUs us) { return double(us) / double(kUsPerSecond); }
inline TimeUs secondsToUs(double s) { return TimeUs(s * double(kUsPerSecond)); }

// Formats a time as HH:MM:SS:FF using the given frame rate.
QString formatTimecode(TimeUs t, double fps);

// Formats a time as a compact human readable duration ("1:05", "1:02:03").
QString formatDuration(TimeUs t);

// Snaps a time to the nearest frame boundary at the given frame rate.
TimeUs snapToFrame(TimeUs t, double fps);

} // namespace spotlight
