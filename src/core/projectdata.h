#pragma once

#include "core/design.h"
#include "core/media.h"
#include "core/timeline.h"

#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QVector>

namespace spotlight {

// A named point on the timeline (chapter, beat, note to self).
struct Marker {
    QUuid id;
    TimeUs time = 0;
    QString name;
};

struct SequenceSettings {
    int width = 1920;
    int height = 1080;
    double frameRate = 30.0;
    int sampleRate = 48000;
};

// The complete, copyable state of a SPOTLIGHT! project.
//
// Every workspace reads and writes this one structure, which is what lets the
// Video, Audio and Design studios share media and edits. ProjectData is a plain
// value type (Qt containers are implicitly shared), so whole-project snapshots
// are cheap and are used for undo/redo.
struct ProjectData {
    static constexpr int kFormatVersion = 1;

    QUuid id = QUuid::createUuid();
    QString name = QStringLiteral("Untitled Project");
    SequenceSettings sequence;
    QVector<MediaItem> media;
    QVector<Track> tracks; // video tracks first (V1 = bottom), then audio tracks
    DesignDocument design;
    QVector<Marker> markers; // kept sorted by time

    // Lookup helpers -------------------------------------------------------
    const MediaItem *mediaById(const QUuid &id) const;
    int trackIndex(const QUuid &trackId) const;
    // Returns the clip and sets *trackIdx to the index of its track.
    Clip *clipById(const QUuid &clipId, int *trackIdx = nullptr);
    const Clip *clipById(const QUuid &clipId, int *trackIdx = nullptr) const;

    QVector<int> videoTrackIndices() const;
    QVector<int> audioTrackIndices() const;

    // End of the last clip on any track.
    TimeUs duration() const;

    // Adds a track of the given type and returns its index. Video tracks are
    // inserted after the existing video tracks.
    int addTrack(TrackType type, const QString &name = QString());

    // Serialization --------------------------------------------------------
    // projectDir (if non-empty) is used to store media paths relative to the
    // project file so projects survive being moved together with their media.
    QJsonObject toJson(const QString &projectDir = QString()) const;
    static bool fromJson(const QJsonObject &o, ProjectData *out, QString *error,
                         const QString &projectDir = QString());

    // A fresh project with two video and two audio tracks.
    static ProjectData createDefault();
};

// Reads/writes .spotlight project files (UTF-8 JSON). Writes are atomic.
bool saveProjectFile(const ProjectData &data, const QString &path, QString *error);
bool loadProjectFile(const QString &path, ProjectData *out, QString *error);

} // namespace spotlight
