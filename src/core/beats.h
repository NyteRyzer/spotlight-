#pragma once

#include "core/types.h"

#include <QString>
#include <QStringList>
#include <QVector>

// Automatic tempo (BPM) and beat-phase detection for music clips, used to
// drop beat markers in time with a song.
namespace spotlight::beats {

constexpr double kEnvelopeRate = 100.0; // energy samples per second

// FFmpeg pass that prints the RMS level every 10 ms of [in, in + duration).
QStringList analysisArgs(const QString &path, TimeUs in, TimeUs duration);
// Level per 10 ms in dBFS (floor -90).
QVector<double> parseEnvelope(const QString &log);

struct Tempo {
    double bpm = 0;        // 0 = no steady beat found
    double firstBeat = 0;  // seconds (relative to the analysed range)
    double confidence = 0; // 0..1
};
// Estimates the dominant tempo between 60 and 200 BPM from the envelope.
Tempo estimateTempo(const QVector<double> &envelopeDb, double rate = kEnvelopeRate);

} // namespace spotlight::beats
