#pragma once

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

// Plays the edited timeline ("Program" monitor).
//
// One QMediaPlayer renders the top-most visible video clip into the view and
// one player per audio track plays that track's clips. A master clock drives
// the playhead; players are switched at clip boundaries and re-seeked when
// they drift. Clip gain, track volume, mute/solo and fades are applied live.
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
    };

    void tick();
    void sync(bool forceSeek);
    void syncVideo(bool forceSeek);
    void syncAudio(bool forceSeek);
    void driveLane(Lane &lane, const QString &path, qint64 srcMs, bool forceSeek);
    void stopLane(Lane &lane);
    Lane makeLane(bool withAudio);
    QImage stillImage(const QString &path);
    // Shows the video frame at the current (paused) position. Some backends
    // don't render after a paused seek, so play the silent video player
    // until the first frame arrives, then pause again.
    void requestPausedFrame();

    Project *m_project;
    VideoView *m_view;
    Lane m_video;
    QVector<Lane> m_audio; // index = audio track ordinal
    QTimer m_timer;
    QElapsedTimer m_clock;
    TimeUs m_clockBase = 0;
    TimeUs m_pos = 0;
    bool m_playing = false;
    bool m_frameRequested = false;
    QHash<QString, QImage> m_stills;
};

} // namespace spotlight
