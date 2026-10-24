#pragma once

#include "core/media.h"

#include <QString>
#include <QStringList>
#include <QVector>

// Auto-Clip: finds the best moments in long videos and stream VODs.
//
// One FFmpeg pass measures loudness every second and detects scene cuts.
// Moments that are much louder than their surroundings (shouting, laughter,
// big reactions, explosions, crowd noise) or full of fast cuts score high;
// the best non-overlapping windows become clips, with boundaries nudged to
// quiet points so they don't start or end mid-word.
namespace spotlight::highlights {

// Raw signals from the analysis pass.
struct Signals {
    QVector<double> loudnessDb; // one value per second (RMS dBFS, floor -90)
    QVector<double> sceneCuts;  // times in seconds
    double duration = 0;        // seconds
};

struct Options {
    double clipSeconds = 30;   // length of each clip
    int maxClips = 8;
    double sensitivity = 0.5;  // 0 (only huge moments) .. 1 (more, smaller ones)
    bool useScenes = true;
};

QStringList analysisArgs(const QString &path, bool hasVideo, bool useScenes);
// Parses FFmpeg's output (stdout + stderr) into signals.
Signals parseAnalysis(const QString &log, double durationSeconds);
// Per-second excitement score (exposed for tests and the results graph).
QVector<double> excitement(const Signals &s, bool useScenes);
// The best clips, highest score first.
QVector<Highlight> findHighlights(const Signals &s, const Options &opt);

} // namespace spotlight::highlights
