#pragma once

#include "core/timeline.h"

#include <QString>
#include <QSize>
#include <QStringList>
#include <QVector>

#include <array>

// Shared definitions for clip looks, transitions and motion presets, used by
// the exporter (FFmpeg filters), the preview compositor and the UI.
namespace spotlight {

// Row-major 3x3 colour matrix: out = M * (r, g, b).
using ColorMatrix = std::array<double, 9>;

// The channel mixer for a clip's temperature, tint and mono settings
// (identity when none are set). Exported as FFmpeg colorchannelmixer.
ColorMatrix colorMatrix(const ColorAdjust &c);

// Vignette angle for FFmpeg's vignette filter (and the preview), radians.
double vignetteAngle(double amount);

// Gaussian blur sigma in output pixels for a frame of the given height.
double blurSigma(double amount, int frameHeight);

// Effect parameters shared by the exporter and the preview so they match.
double sharpenAmount(double amount);           // unsharp-mask strength
int pixelBlock(double amount, int height);     // mosaic block size in pixels
double grainStrength(double amount);           // FFmpeg noise strength (0..100)
int cinemaBarHeight(const QSize &picture);     // bar height for a 2.39:1 frame
int borderPixels(double amount, int frameHeight);
double blurFillSigma(int frameHeight);

// A named one-click look ("filter" in CapCut/Filmora terms).
struct LookPreset {
    QString name;
    ColorAdjust adjust;
};
QVector<LookPreset> lookPresets();

// Transition types offered in the UI. `id` is the FFmpeg xfade name.
struct TransitionType {
    QString id;
    QString name;
};
QVector<TransitionType> transitionTypes();
QString transitionName(const QString &id);

QStringList motionNames(); // index = Motion enum value

// Zoom factor and centre offset (fractions of the frame) of a Ken Burns move
// at progress p (0..1). Shared by preview and export so they match.
struct MotionState {
    double zoom = 1.0;
    double dx = 0.0; // fractions of the picture size
    double dy = 0.0;
    double fx = 0.0; // fractions of the frame size (entrance slides)
    double fy = 0.0;
};
// p = progress through the clip (0..1), elapsed = seconds since it started.
MotionState motionAt(Motion m, double p, double elapsed = 1e9);
constexpr double kMotionAmount = 0.18;   // zoom/pan range of a Ken Burns move
constexpr double kEntranceSeconds = 0.6; // length of slide / pop entrances
bool isEntrance(Motion m);

} // namespace spotlight

namespace spotlight {

struct ProjectData;

// Size of a source picture letterboxed into the output frame (even values).
QSize fitSize(const QSize &source, const QSize &frame);

// Volume multiplier for a track with duckUnderVoice at timeline time t:
// ramps down over kDuckRamp around every clip on a voice track.
struct TimeRange {
    TimeUs start = 0;
    TimeUs end = 0;
};
constexpr TimeUs kDuckRamp = 300 * kUsPerMs;
// Merged ranges where voice tracks have audible clips (excluding `trackIndex`).
QVector<TimeRange> voiceRanges(const ProjectData &d, int trackIndex);
double duckGain(const QVector<TimeRange> &ranges, double amountDb, TimeUs t);

} // namespace spotlight
