#pragma once

#include "core/types.h"

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QUuid>

namespace spotlight {

enum class MediaKind { Video, Audio, Image };

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
