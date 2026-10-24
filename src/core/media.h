#pragma once

#include "core/types.h"

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QUuid>
#include <QVector>

namespace spotlight {

enum class MediaKind { Video, Audio, Image };

// A moment found by Auto-Clip in a long video (source times).
struct Highlight {
    TimeUs start = 0;
    TimeUs end = 0;
    double score = 0;  // 0..100
    QString reason;    // e.g. "Big reaction", "Fast action"

    TimeUs duration() const { return end - start; }
};

// A file imported into the project's media bin.
struct MediaItem {
    QUuid id;
    QString path;          // absolute path on disk
    QString name;          // display name (file name by default)
    MediaKind kind = MediaKind::Video;
    TimeUs duration = 0;   // 0 for still images
    QSize resolution;      // empty for audio
    double frameRate = 0;  // 0 if unknown
    bool hasVideo = false;
    bool hasAudio = false;
    // Titles are transparent PNGs rendered from a design document kept here
    // so they can be re-edited (and re-rendered if the PNG goes missing).
    QJsonObject titleDesign;
    // Auto-Clip results (empty until analysed).
    QVector<Highlight> highlights;
    bool highlightsAnalyzed = false;

    bool isStill() const { return kind == MediaKind::Image; }
    bool isTitle() const { return !titleDesign.isEmpty(); }
};

inline QString mediaKindToString(MediaKind k)
{
    switch (k) {
    case MediaKind::Video: return QStringLiteral("video");
    case MediaKind::Audio: return QStringLiteral("audio");
    case MediaKind::Image: return QStringLiteral("image");
    }
    return QStringLiteral("video");
}

inline MediaKind mediaKindFromString(const QString &s)
{
    if (s == QLatin1String("audio"))
        return MediaKind::Audio;
    if (s == QLatin1String("image"))
        return MediaKind::Image;
    return MediaKind::Video;
}

} // namespace spotlight
