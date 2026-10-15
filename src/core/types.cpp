#include "core/types.h"

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
