#include "core/beats.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace spotlight::beats {

QStringList analysisArgs(const QString &path, TimeUs in, TimeUs duration)
{
    // 11025 Hz / 110 samples per frame ~= 100 frames per second.
    return {QStringLiteral("-hide_banner"), QStringLiteral("-nostats"), QStringLiteral("-nostdin"),
            QStringLiteral("-ss"), QString::number(usToSeconds(in), 'f', 3), QStringLiteral("-t"),
            QString::number(usToSeconds(duration), 'f', 3), QStringLiteral("-i"), path, QStringLiteral("-vn"),
            QStringLiteral("-af"),
            QStringLiteral("aresample=11025,asetnsamples=n=110:p=0,astats=metadata=1:reset=1,"
                           "ametadata=mode=print:key=lavfi.astats.Overall.RMS_level:file=-"),
            QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};
}

QVector<double> parseEnvelope(const QString &log)
{
    static const QRegularExpression rmsRe(QStringLiteral("lavfi\\.astats\\.Overall\\.RMS_level=(\\S+)"));
    QVector<double> out;
    auto it = rmsRe.globalMatch(log);
    while (it.hasNext()) {
        bool ok = false;
        double db = it.next().captured(1).toDouble(&ok);
        if (!ok || !std::isfinite(db))
            db = -90;
        out.append(std::max(-90.0, db));
    }
    return out;
}

Tempo estimateTempo(const QVector<double> &envelopeDb, double rate)
{
    Tempo t;
    const int n = envelopeDb.size();
    if (n < int(rate * 4))
        return t; // need a few seconds of music
    // Onset strength: positive jumps in log energy (attacks, drum hits).
    QVector<double> onset(n, 0.0);
    for (int i = 1; i < n; ++i)
        onset[i] = std::max(0.0, envelopeDb[i] - envelopeDb[i - 1]);
    double mean = 0;
    for (double v : onset)
        mean += v;
    mean /= n;
    for (double &v : onset)
        v = std::max(0.0, v - mean);

    // Autocorrelation over the 60..200 BPM lag range, gently preferring
    // tempos near 120 BPM to avoid half/double-tempo mistakes.
    const int minLag = int(std::floor(rate * 60.0 / 200.0));
    const int maxLag = int(std::ceil(rate * 60.0 / 60.0));
    QVector<double> ac(maxLag + 2, 0.0);
    double zero = 0;
    for (int i = 0; i < n; ++i)
        zero += onset[i] * onset[i];
    if (zero <= 1e-9)
        return t;
    int best = -1;
    double bestScore = 0;
    for (int lag = minLag; lag <= maxLag + 1 && lag < n; ++lag) {
        double sum = 0;
        for (int i = 0; i + lag < n; ++i)
            sum += onset[i] * onset[i + lag];
        ac[lag] = sum / zero;
    }
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const double bpm = 60.0 * rate / lag;
        const double w = std::exp(-0.5 * std::pow(std::log2(bpm / 120.0) / 1.0, 2));
        const double score = ac[lag] * w;
        if (score > bestScore) {
            bestScore = score;
            best = lag;
        }
    }
    if (best < 0)
        return t;
    // Parabolic refinement for a fractional period.
    double period = best;
    if (best > minLag && best < maxLag) {
        const double a = ac[best - 1], b = ac[best], c = ac[best + 1];
        const double den = a - 2 * b + c;
        if (std::abs(den) > 1e-12)
            period = best + std::clamp(0.5 * (a - c) / den, -0.5, 0.5);
    }
    // Phase: the offset whose beat grid lands on the most onset energy.
    double bestPhase = 0, phaseScore = -1;
    for (int ph = 0; ph < int(std::ceil(period)); ++ph) {
        double sum = 0;
        for (double k = ph; k < n; k += period) {
            const int i = int(std::lround(k));
            for (int j = std::max(0, i - 1); j <= std::min(n - 1, i + 1); ++j)
                sum += onset[j];
        }
        if (sum > phaseScore) {
            phaseScore = sum;
            bestPhase = ph;
        }
    }
    t.bpm = 60.0 * rate / period;
    t.firstBeat = bestPhase / rate;
    t.confidence = std::clamp(ac[best] * 2.0, 0.0, 1.0);
    return t;
}

} // namespace spotlight::beats
