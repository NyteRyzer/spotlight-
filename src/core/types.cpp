#include "core/types.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <cmath>

namespace spotlight {

QString formatTimecode(TimeUs t, double fps)
{
    if (t < 0)
        t = 0;
    if (fps <= 0)
        fps = 30.0;
    const qint64 totalSeconds = t / kUsPerSecond;
    const TimeUs remainder = t % kUsPerSecond;
    const int frame = int(std::floor(usToSeconds(remainder) * fps + 1e-6));
    const qint64 h = totalSeconds / 3600;
    const qint64 m = (totalSeconds / 60) % 60;
    const qint64 s = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(frame, 2, 10, QLatin1Char('0'));
}

TimeUs parseTimecode(const QString &text, double fps, TimeUs current)
{
    if (fps <= 0)
        fps = 30.0;
    const QString t = text.trimmed();
    if (t.isEmpty())
        return -1;
    bool ok = false;
    if (t.startsWith(QLatin1Char('+')) || t.startsWith(QLatin1Char('-'))) {
        const int frames = t.toInt(&ok);
        if (!ok)
            return -1;
        return std::max<TimeUs>(0, current + TimeUs(std::llround(frames * double(kUsPerSecond) / fps)));
    }
    const QStringList parts = t.split(QRegularExpression(QStringLiteral("[:;.,]")));
    if (!t.contains(QLatin1Char(':')) && !t.contains(QLatin1Char(';'))) {
        const double secs = QString(t).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        return ok && secs >= 0 ? secondsToUs(secs) : -1;
    }
    QVector<qint64> v;
    for (const QString &p : parts) {
        const qint64 n = p.toLongLong(&ok);
        if (!ok || n < 0)
            return -1;
        v.append(n);
    }
    // HH:MM:SS:FF has four fields; otherwise [H:]M:S (with optional .fraction
    // handled as frames only in the four-field form).
    qint64 h = 0, m = 0, s = 0, f = 0;
    if (v.size() == 4) {
        h = v[0], m = v[1], s = v[2], f = v[3];
    } else if (v.size() == 3) {
        h = v[0], m = v[1], s = v[2];
    } else if (v.size() == 2) {
        m = v[0], s = v[1];
    } else {
        return -1;
    }
    return (h * 3600 + m * 60 + s) * kUsPerSecond + TimeUs(std::llround(f * double(kUsPerSecond) / fps));
}

QString formatDuration(TimeUs t)
{
    if (t < 0)
        t = 0;
    const qint64 totalSeconds = (t + kUsPerSecond / 2) / kUsPerSecond;
    const qint64 h = totalSeconds / 3600;
    const qint64 m = (totalSeconds / 60) % 60;
    const qint64 s = totalSeconds % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

TimeUs snapToFrame(TimeUs t, double fps)
{
    if (fps <= 0)
        return t;
    const double frames = std::round(usToSeconds(t) * fps);
    return TimeUs(std::llround(frames / fps * double(kUsPerSecond)));
}

} // namespace spotlight
