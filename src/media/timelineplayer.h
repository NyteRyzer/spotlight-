#pragma once

#include "core/timeline.h"
#include "core/types.h"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <QUuid>
#include <QVector>

class QMediaPlayer;
class QAudioOutput;

namespace spotlight {

class Project;
class VideoView;
struct ViewLayer;

// Plays the edited timeline ("Program" monitor).
//
// Every video track gets a QMediaPlayer feeding its own compositor slot in the
// view (plus a second one that keeps the outgoing clip playing during a
// transition), and one player per audio track plays that track's clips. A
// master clock drives the playhead; players are switched at clip boundaries
// and re-seeked when they drift. Speed, gain, track volume, mute/solo, fades
// and auto-ducking are applied live; the view composites the tracks.
class TimelinePlayer : public QObject {
    Q_OBJECT
public:
    TimelinePlayer(Project *project, VideoView *view, QObject *parent = nullptr);
    ~TimelinePlayer() override;

    TimeUs position() const { return m_pos; }
    bool isPlaying() const { return m_playing; }

public slots:
    void play();
    void pause();
    void togglePlay();
    void seek(spotlight::TimeUs t);
    void stepFrames(int frames);

signals:
    void positionChanged(spotlight::TimeUs t);
    void playingChanged(bool playing);

private:
    struct Lane {
        QMediaPlayer *player = nullptr;
        QAudioOutput *output = nullptr;
        QString source;
        qint64 pendingSeekMs = -1;
        int slot = -1;              // video lanes: compositor slot
        bool frameRequested = false;
    };

    void tick();
    void sync(bool forceSeek);
    void syncVideo(bool forceSeek);
    void syncAudio(bool forceSeek);
    void driveLane(Lane &lane, const QString &path, qint64 srcMs, bool forceSeek, double rate = 1.0);
    void stopLane(Lane &lane);
    Lane makeLane(bool withAudio, int slot = -1);
    Lane *laneFor(QMediaPlayer *player);
    // Sets up a compositor layer for `clip` at timeline time `at`, driving
    // `lane` for video media.
    bool prepareLayer(ViewLayer &layer, Lane &lane, const Clip &clip, TimeUs at, bool forceSeek);
    QImage stillImage(const QString &path);
    // Shows the video frame at the current (paused) position. Some backends
    // don't render after a paused seek, so play the silent video player
    // until the first frame arrives, then pause again.
    void requestPausedFrame(Lane &lane);

    Project *m_project;
    VideoView *m_view;
    QVector<Lane> m_video; // 2 per video track ordinal: [main, outgoing]
    QVector<Lane> m_audio; // index = audio track ordinal
    QTimer m_timer;
    QElapsedTimer m_clock;
    TimeUs m_clockBase = 0;
    TimeUs m_pos = 0;
    bool m_playing = false;
    QHash<QString, QImage> m_stills;
};

} // namespace spotlight
