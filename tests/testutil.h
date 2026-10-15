#pragma once

#include "core/projectdata.h"

namespace testutil {

using namespace spotlight;

inline QUuid addVideo(ProjectData &d, TimeUs duration, bool withAudio = true, const QString &path = "/media/a.mp4")
{
    MediaItem m;
    m.id = QUuid::createUuid();
    m.path = path;
    m.name = "clip";
    m.kind = MediaKind::Video;
    m.duration = duration;
    m.resolution = QSize(1920, 1080);
    m.frameRate = 30;
    m.hasVideo = true;
    m.hasAudio = withAudio;
    d.media.append(m);
    return m.id;
}

inline QUuid addAudio(ProjectData &d, TimeUs duration, const QString &path = "/media/song.mp3")
{
    MediaItem m;
    m.id = QUuid::createUuid();
    m.path = path;
    m.name = "song";
    m.kind = MediaKind::Audio;
    m.duration = duration;
    m.hasAudio = true;
    d.media.append(m);
    return m.id;
}

inline QUuid addImage(ProjectData &d, const QString &path = "/media/logo.png")
{
    MediaItem m;
    m.id = QUuid::createUuid();
    m.path = path;
    m.name = "logo";
    m.kind = MediaKind::Image;
    m.resolution = QSize(500, 500);
    m.hasVideo = true;
    d.media.append(m);
    return m.id;
}

constexpr TimeUs S = kUsPerSecond;

} // namespace testutil
