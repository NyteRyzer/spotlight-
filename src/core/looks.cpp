#include "core/looks.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace spotlight {

namespace {

ColorMatrix multiply(const ColorMatrix &a, const ColorMatrix &b)
{
    ColorMatrix r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double s = 0;
            for (int k = 0; k < 3; ++k)
                s += a[i * 3 + k] * b[k * 3 + j];
            r[i * 3 + j] = s;
        }
    }
    return r;
}

ColorAdjust adj(double b, double c, double s, double temp = 0, double tint = 0, MonoMode mono = MonoMode::None,
                double vignette = 0, double blur = 0)
{
    ColorAdjust a;
    a.brightness = b;
    a.contrast = c;
    a.saturation = s;
    a.temperature = temp;
    a.tint = tint;
    a.mono = mono;
    a.vignette = vignette;
    a.blur = blur;
    return a;
}

} // namespace

ColorMatrix colorMatrix(const ColorAdjust &c)
{
    const double t = std::clamp(c.temperature, -1.0, 1.0);
    const double g = std::clamp(c.tint, -1.0, 1.0);
    // Temperature scales red against blue; tint scales green against both.
    ColorMatrix m{1 + 0.15 * t - 0.04 * g, 0, 0, 0, 1 + 0.08 * g, 0, 0, 0, 1 - 0.15 * t - 0.04 * g};
    if (c.mono == MonoMode::BlackWhite) {
        const ColorMatrix bw{0.2126, 0.7152, 0.0722, 0.2126, 0.7152, 0.0722, 0.2126, 0.7152, 0.0722};
        m = multiply(bw, m);
    } else if (c.mono == MonoMode::Sepia) {
        const ColorMatrix sepia{0.393, 0.769, 0.189, 0.349, 0.686, 0.168, 0.272, 0.534, 0.131};
        m = multiply(sepia, m);
    }
    if (!qFuzzyIsNull(std::fmod(c.hue, 360.0))) {
        // Rotation about the luma axis (the standard hue-rotate matrix).
        const double a = c.hue * M_PI / 180.0;
        const double co = std::cos(a), si = std::sin(a);
        const ColorMatrix h{0.213 + co * 0.787 - si * 0.213, 0.715 - co * 0.715 - si * 0.715,
                            0.072 - co * 0.072 + si * 0.928, 0.213 - co * 0.213 + si * 0.143,
                            0.715 + co * 0.285 + si * 0.140, 0.072 - co * 0.072 - si * 0.283,
                            0.213 - co * 0.213 - si * 0.787, 0.715 - co * 0.715 + si * 0.715,
                            0.072 + co * 0.928 + si * 0.072};
        m = multiply(h, m);
    }
    return m;
}

double vignetteAngle(double amount)
{
    return std::clamp(amount, 0.0, 1.0) * M_PI / 2.0 * 0.9;
}

double blurSigma(double amount, int frameHeight)
{
    return std::clamp(amount, 0.0, 1.0) * 0.02 * frameHeight;
}

double sharpenAmount(double amount)
{
    return std::clamp(amount, 0.0, 1.0) * 2.0;
}

int pixelBlock(double amount, int height)
{
    return std::max(2, int(std::lround(std::clamp(amount, 0.0, 1.0) * height / 16.0)));
}

double grainStrength(double amount)
{
    return std::clamp(amount, 0.0, 1.0) * 40.0;
}

int cinemaBarHeight(const QSize &picture)
{
    const double visible = picture.width() / 2.39;
    return std::max(0, int((picture.height() - visible) / 2.0));
}

int borderPixels(double amount, int frameHeight)
{
    return std::max(2, int(std::lround(std::clamp(amount, 0.0, 0.2) * frameHeight / 2.0)) * 2);
}

double blurFillSigma(int frameHeight)
{
    return std::max(4.0, frameHeight / 30.0);
}

QVector<LookPreset> lookPresets()
{
    auto tr = [](const char *s) { return QCoreApplication::translate("Looks", s); };
    return {
        {tr("None"), ColorAdjust()},
        {tr("Warm"), adj(0.02, 1.05, 1.1, 0.5)},
        {tr("Cool"), adj(0.0, 1.05, 1.05, -0.5)},
        {tr("Vivid"), adj(0.02, 1.15, 1.5)},
        {tr("Cinematic"), adj(-0.03, 1.15, 0.85, 0.15, -0.1, MonoMode::None, 0.35)},
        {tr("Vintage"), adj(0.04, 0.9, 0.7, 0.4, 0.0, MonoMode::None, 0.5)},
        {tr("Faded"), adj(0.07, 0.8, 0.75)},
        {tr("Moody"), adj(-0.05, 1.2, 0.8, -0.2, 0.0, MonoMode::None, 0.6)},
        {tr("Dreamy"), adj(0.05, 0.95, 1.1, 0.1, 0.0, MonoMode::None, 0.2, 0.12)},
        {tr("Black & White"), adj(0.0, 1.1, 1.0, 0, 0, MonoMode::BlackWhite)},
        {tr("Noir"), adj(-0.02, 1.4, 1.0, 0, 0, MonoMode::BlackWhite, 0.6)},
        {tr("Sepia"), adj(0.02, 1.0, 1.0, 0, 0, MonoMode::Sepia, 0.3)},
    };
}

QVector<TransitionType> transitionTypes()
{
    auto tr = [](const char *s) { return QCoreApplication::translate("Transitions", s); };
    return {
        {QStringLiteral("fade"), tr("Cross Dissolve")},   {QStringLiteral("fadeblack"), tr("Dip to Black")},
        {QStringLiteral("fadewhite"), tr("Dip to White")}, {QStringLiteral("wipeleft"), tr("Wipe Left")},
        {QStringLiteral("wiperight"), tr("Wipe Right")},   {QStringLiteral("wipeup"), tr("Wipe Up")},
        {QStringLiteral("wipedown"), tr("Wipe Down")},     {QStringLiteral("slideleft"), tr("Slide Left")},
        {QStringLiteral("slideright"), tr("Slide Right")}, {QStringLiteral("slideup"), tr("Slide Up")},
        {QStringLiteral("slidedown"), tr("Slide Down")},   {QStringLiteral("circleopen"), tr("Iris Open")},
        {QStringLiteral("circleclose"), tr("Iris Close")}, {QStringLiteral("radial"), tr("Clock Wipe")},
        {QStringLiteral("pixelize"), tr("Pixelate")},      {QStringLiteral("zoomin"), tr("Zoom In")},
    };
}

QString transitionName(const QString &id)
{
    for (const auto &t : transitionTypes()) {
        if (t.id == id)
            return t.name;
    }
    return id;
}

QStringList motionNames()
{
    auto tr = [](const char *s) { return QCoreApplication::translate("Motion", s); };
    return {tr("None"),          tr("Zoom In"),        tr("Zoom Out"),     tr("Pan Left"),
            tr("Pan Right"),     tr("Pan Up"),         tr("Pan Down"),     tr("Slide In from Left"),
            tr("Slide In from Right"), tr("Slide In from Bottom"), tr("Pop In")};
}

bool isEntrance(Motion m)
{
    return m == Motion::SlideInLeft || m == Motion::SlideInRight || m == Motion::SlideInUp || m == Motion::PopIn;
}

MotionState motionAt(Motion m, double p, double elapsed)
{
    p = std::clamp(p, 0.0, 1.0);
    // Entrances ease out (cubic) over kEntranceSeconds.
    const double e0 = std::clamp(elapsed / kEntranceSeconds, 0.0, 1.0);
    const double e = 1.0 - (1.0 - e0) * (1.0 - e0) * (1.0 - e0);
    const double a = kMotionAmount;
    MotionState s;
    switch (m) {
    case Motion::None:
        break;
    case Motion::ZoomIn:
        s.zoom = 1.0 + a * p;
        break;
    case Motion::ZoomOut:
        s.zoom = 1.0 + a * (1.0 - p);
        break;
    // Pans run on a slightly zoomed picture so no edge is revealed: with
    // zoom 1+a the picture overhangs by a/2 on each side.
    case Motion::PanLeft:
        s.zoom = 1.0 + a;
        s.dx = a / 2.0 * (1.0 - 2.0 * p);
        break;
    case Motion::PanRight:
        s.zoom = 1.0 + a;
        s.dx = -a / 2.0 * (1.0 - 2.0 * p);
        break;
    case Motion::PanUp:
        s.zoom = 1.0 + a;
        s.dy = a / 2.0 * (1.0 - 2.0 * p);
        break;
    case Motion::PanDown:
        s.zoom = 1.0 + a;
        s.dy = -a / 2.0 * (1.0 - 2.0 * p);
        break;
    case Motion::SlideInLeft:
        s.fx = -(1.0 - e);
        break;
    case Motion::SlideInRight:
        s.fx = 1.0 - e;
        break;
    case Motion::SlideInUp:
        s.fy = 1.0 - e;
        break;
    case Motion::PopIn:
        // Grow from 60 % with a small overshoot, then settle.
        s.zoom = e0 < 0.7 ? 0.6 + 0.5 * (e0 / 0.7) : 1.1 - 0.1 * ((e0 - 0.7) / 0.3);
        break;
    }
    return s;
}

} // namespace spotlight

#include "core/projectdata.h"
#include "core/timelineops.h"

namespace spotlight {

QSize fitSize(const QSize &source, const QSize &frame)
{
    if (source.isEmpty())
        return QSize(frame.width() & ~1, frame.height() & ~1);
    QSize s = source.scaled(frame, Qt::KeepAspectRatio);
    return QSize(std::max(2, s.width() & ~1), std::max(2, s.height() & ~1));
}

QVector<TimeRange> voiceRanges(const ProjectData &d, int trackIndex)
{
    QVector<TimeRange> r;
    for (int ti = 0; ti < d.tracks.size(); ++ti) {
        const Track &t = d.tracks[ti];
        if (ti == trackIndex || !t.isAudio() || !t.voice || !timeline::isTrackAudible(d, ti))
            continue;
        for (const Clip &c : t.clips) {
            const MediaItem *m = d.mediaById(c.mediaId);
            if (m && m->hasAudio)
                r.append({c.start, c.end()});
        }
    }
    std::sort(r.begin(), r.end(), [](const TimeRange &a, const TimeRange &b) { return a.start < b.start; });
    // Merge ranges whose ramps would touch so the envelope never overlaps.
    QVector<TimeRange> merged;
    for (const auto &x : r) {
        if (!merged.isEmpty() && x.start - merged.last().end <= 2 * kDuckRamp)
            merged.last().end = std::max(merged.last().end, x.end);
        else
            merged.append(x);
    }
    return merged;
}

double duckGain(const QVector<TimeRange> &ranges, double amountDb, TimeUs t)
{
    const double g = std::pow(10.0, std::min(0.0, amountDb) / 20.0);
    double w = 0;
    for (const auto &r : ranges) {
        const double up = double(t - (r.start - kDuckRamp)) / double(kDuckRamp);
        const double down = double((r.end + kDuckRamp) - t) / double(kDuckRamp);
        w += std::clamp(std::min(up, down), 0.0, 1.0);
    }
    return 1.0 - (1.0 - g) * std::min(1.0, w);
}

} // namespace spotlight
