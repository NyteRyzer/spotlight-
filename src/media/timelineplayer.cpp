#include "media/timelineplayer.h"

#include "core/project.h"
#include "core/timelineops.h"
#include "media/videoview.h"

#include <QAudioOutput>
#include <QImageReader>
#include <QMediaPlayer>
#include <QUrl>

#include <cmath>

namespace spotlight {

namespace {
constexpr qint64 kDriftToleranceMs = 120;
}

TimelinePlayer::TimelinePlayer(Project *project, VideoView *view, QObject *parent)
    : QObject(parent)
    , m_project(project)
    , m_view(view)
{
    m_video = makeLane(false);
    m_video.player->setVideoSink(view->videoSink());

    m_timer.setInterval(15);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &TimelinePlayer::tick);
    connect(project, &Project::changed, this, [this] {
        m_pos = std::min(m_pos, m_project->data().duration());
        // While playing, let drift correction handle it; a forced seek on
        // every edit (fader moves, inspector tweaks) makes audio stutter.
        sync(!m_playing);
    });
    sync(true);
}

TimelinePlayer::~TimelinePlayer() = default;

TimelinePlayer::Lane TimelinePlayer::makeLane(bool withAudio)
{
    Lane lane;
    lane.player = new QMediaPlayer(this);
    if (withAudio) {
        lane.output = new QAudioOutput(this);
        lane.player->setAudioOutput(lane.output);
    }
    // Seeking is only reliable once the media is loaded; apply it then.
    QMediaPlayer *player = lane.player;
    connect(player, &QMediaPlayer::mediaStatusChanged, this, [this, player](QMediaPlayer::MediaStatus s) {
        if (s != QMediaPlayer::LoadedMedia && s != QMediaPlayer::BufferedMedia)
            return;
        Lane *l = nullptr;
        if (m_video.player == player)
            l = &m_video;
        for (auto &a : m_audio) {
            if (a.player == player)
                l = &a;
        }
        if (!l || l->pendingSeekMs < 0)
            return;
        player->setPosition(l->pendingSeekMs);
        l->pendingSeekMs = -1;
        if (m_playing)
            player->play();
        else
            player->pause();
    });
    return lane;
}

QImage TimelinePlayer::stillImage(const QString &path)
{
    auto it = m_stills.find(path);
    if (it != m_stills.end())
        return *it;
    QImageReader r(path);
    r.setAutoTransform(true);
    QImage img = r.read();
    if (m_stills.size() > 32)
        m_stills.clear();
    m_stills.insert(path, img);
    return img;
}

void TimelinePlayer::play()
{
    if (m_playing)
        return;
    const TimeUs dur = m_project->data().duration();
    if (dur <= 0)
        return;
    if (m_pos >= dur)
        m_pos = 0;
    m_playing = true;
    m_clockBase = m_pos;
    m_clock.start();
    m_timer.start();
    sync(true);
    emit playingChanged(true);
}

void TimelinePlayer::pause()
{
    if (!m_playing)
        return;
    m_playing = false;
    m_timer.stop();
    m_video.player->pause();
    for (auto &a : m_audio)
        a.player->pause();
    sync(true);
    emit playingChanged(false);
}

void TimelinePlayer::togglePlay()
{
    if (m_playing)
        pause();
    else
        play();
}

void TimelinePlayer::seek(TimeUs t)
{
    m_pos = std::clamp<TimeUs>(t, 0, std::max<TimeUs>(0, m_project->data().duration()));
    if (m_playing) {
        m_clockBase = m_pos;
        m_clock.restart();
    }
    sync(true);
    emit positionChanged(m_pos);
}

void TimelinePlayer::stepFrames(int frames)
{
    const double fps = m_project->data().sequence.frameRate;
    pause();
    seek(snapToFrame(m_pos, fps) + TimeUs(std::llround(frames * kUsPerSecond / fps)));
}

void TimelinePlayer::tick()
{
    m_pos = m_clockBase + m_clock.nsecsElapsed() / 1000;
    const TimeUs dur = m_project->data().duration();
    if (m_pos >= dur) {
        m_pos = dur;
        pause();
        emit positionChanged(m_pos);
        return;
    }
    sync(false);
    emit positionChanged(m_pos);
}

void TimelinePlayer::sync(bool forceSeek)
{
    m_view->setCanvasSize(QSize(m_project->data().sequence.width, m_project->data().sequence.height));
    syncVideo(forceSeek);
    syncAudio(forceSeek);
}

void TimelinePlayer::stopLane(Lane &lane)
{
    if (lane.source.isEmpty())
        return;
    lane.player->stop();
    lane.player->setSource(QUrl());
    lane.source.clear();
    lane.pendingSeekMs = -1;
}

void TimelinePlayer::driveLane(Lane &lane, const QString &path, qint64 srcMs, bool forceSeek)
{
    if (lane.source != path) {
        lane.source = path;
        lane.pendingSeekMs = srcMs;
        lane.player->setSource(QUrl::fromLocalFile(path));
        return;
    }
    if (lane.pendingSeekMs >= 0) {
        lane.pendingSeekMs = srcMs; // still loading; update the target
        return;
    }
    const qint64 drift = std::abs(lane.player->position() - srcMs);
    if (forceSeek || drift > kDriftToleranceMs)
        lane.player->setPosition(srcMs);
    if (m_playing && lane.player->playbackState() != QMediaPlayer::PlayingState)
        lane.player->play();
    else if (!m_playing && lane.player->playbackState() == QMediaPlayer::PlayingState)
        lane.player->pause();
}

void TimelinePlayer::syncVideo(bool forceSeek)
{
    const ProjectData &d = m_project->data();
    const Clip *clip = timeline::topVideoClipAt(d, m_pos);
    const MediaItem *media = clip ? d.mediaById(clip->mediaId) : nullptr;
    if (!clip || !media) {
        stopLane(m_video);
        m_view->showBlack();
        m_view->setAdjust(ColorAdjust(), 1.0);
        m_view->setPlaceholderText(d.duration() > 0 ? QString()
                                                    : tr("Drag clips from the Media Bin onto the timeline"));
        return;
    }
    m_view->setPlaceholderText(QString());
    m_view->setAdjust(clip->color, timeline::fadeEnvelope(*clip, m_pos));
    if (media->isStill()) {
        stopLane(m_video);
        m_view->setStillImage(stillImage(media->path));
        return;
    }
    driveLane(m_video, media->path, usToMs(clip->in + (m_pos - clip->start)), forceSeek);
}

void TimelinePlayer::syncAudio(bool forceSeek)
{
    const ProjectData &d = m_project->data();
    const QVector<int> audioTracks = d.audioTrackIndices();
    while (m_audio.size() < audioTracks.size())
        m_audio.append(makeLane(true));
    while (m_audio.size() > audioTracks.size()) {
        Lane l = m_audio.takeLast();
        l.player->stop();
        l.player->deleteLater();
        l.output->deleteLater();
    }

    for (int i = 0; i < audioTracks.size(); ++i) {
        Lane &lane = m_audio[i];
        const int ti = audioTracks[i];
        const Track &track = d.tracks[ti];
        const int ci = timeline::clipIndexAt(track, m_pos);
        const Clip *clip = ci >= 0 ? &track.clips[ci] : nullptr;
        const MediaItem *media = clip ? d.mediaById(clip->mediaId) : nullptr;
        if (!clip || !media || !media->hasAudio || !timeline::isTrackAudible(d, ti)) {
            stopLane(lane);
            continue;
        }
        const double vol = track.volume * clip->gain * timeline::fadeEnvelope(*clip, m_pos);
        lane.output->setVolume(float(std::clamp(vol, 0.0, 1.0)));
        if (!m_playing) {
            // Silent while paused, but keep the source loaded for a fast start.
            if (lane.source != media->path)
                driveLane(lane, media->path, usToMs(clip->in + (m_pos - clip->start)), true);
            else if (lane.player->playbackState() == QMediaPlayer::PlayingState)
                lane.player->pause();
            continue;
        }
        driveLane(lane, media->path, usToMs(clip->in + (m_pos - clip->start)), forceSeek);
    }
}

} // namespace spotlight
