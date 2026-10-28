#include "media/timelineplayer.h"

#include "core/looks.h"
#include "core/project.h"
#include "core/timelineops.h"
#include "media/videoview.h"

#include <QAudioOutput>
#include <QImageReader>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoSink>

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

TimelinePlayer::Lane *TimelinePlayer::laneFor(QMediaPlayer *player)
{
    for (auto &l : m_video) {
        if (l.player == player)
            return &l;
    }
    for (auto &l : m_audio) {
        if (l.player == player)
            return &l;
    }
    return nullptr;
}

TimelinePlayer::Lane TimelinePlayer::makeLane(bool withAudio, int slot)
{
    Lane lane;
    lane.player = new QMediaPlayer(this);
    lane.slot = slot;
    if (withAudio) {
        lane.output = new QAudioOutput(this);
        lane.player->setAudioOutput(lane.output);
    }
    if (slot >= 0) {
        QVideoSink *sink = m_view->sinkFor(slot);
        lane.player->setVideoSink(sink);
        QMediaPlayer *player = lane.player;
        connect(sink, &QVideoSink::videoFrameChanged, this, [this, player] {
            Lane *l = laneFor(player);
            if (l && l->frameRequested && !m_playing) {
                l->frameRequested = false;
                player->pause();
            }
        });
    }
    // Seeking is only reliable once the media is loaded; apply it then.
    QMediaPlayer *player = lane.player;
    // Queued: seeking from inside the backend's own status notification is
    // ignored by the FFmpeg backend, leaving the monitor on a stale frame.
    connect(
        player, &QMediaPlayer::mediaStatusChanged, this,
        [this, player](QMediaPlayer::MediaStatus s) {
        if (s != QMediaPlayer::LoadedMedia && s != QMediaPlayer::BufferedMedia)
            return;
        Lane *l = laneFor(player);
        if (!l || l->pendingSeekMs < 0)
            return;
        const qint64 target = l->pendingSeekMs;
        l->pendingSeekMs = -1;
        // Leave the Stopped state before seeking: some backends ignore a
        // seek issued while stopped, leaving a paused monitor on frame 0.
        if (m_playing)
            player->play();
        else
            player->pause();
        player->setPosition(target);
        if (!m_playing && l->slot >= 0)
            requestPausedFrame(*l);
        // Safety net: if the seek still didn't stick, retry once the
        // pipeline has settled.
        QTimer::singleShot(150, player, [player, target] {
            if (std::abs(player->position() - target) > 200)
                player->setPosition(target);
        });
        },
        Qt::QueuedConnection);
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

void TimelinePlayer::requestPausedFrame(Lane &lane)
{
    lane.frameRequested = true;
    lane.player->play(); // no audio output: silent
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
    for (auto &v : m_video)
        v.player->pause();
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
    lane.frameRequested = false;
}

void TimelinePlayer::driveLane(Lane &lane, const QString &path, qint64 srcMs, bool forceSeek, double rate)
{
    if (!qFuzzyCompare(lane.player->playbackRate(), rate))
        lane.player->setPlaybackRate(rate);
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
    if (forceSeek || drift > kDriftToleranceMs * std::max(1.0, rate)) {
        lane.player->setPosition(srcMs);
        if (!m_playing && lane.slot >= 0) {
            requestPausedFrame(lane);
            return;
        }
    }
    if (m_playing && lane.player->playbackState() != QMediaPlayer::PlayingState)
        lane.player->play();
    else if (!m_playing && lane.player->playbackState() == QMediaPlayer::PlayingState && !lane.frameRequested)
        lane.player->pause();
}

bool TimelinePlayer::prepareLayer(ViewLayer &layer, Lane &lane, const Clip &clip, TimeUs at, bool forceSeek)
{
    const ProjectData &d = m_project->data();
    const MediaItem *media = d.mediaById(clip.mediaId);
    if (!media)
        return false;
    layer.clip = clip;
    layer.slot = lane.slot;
    layer.progress = clip.duration() > 0 ? double(at - clip.start) / double(clip.duration()) : 0.0;
    layer.elapsed = usToSeconds(at - clip.start);
    if (media->isStill()) {
        stopLane(lane);
        layer.still = stillImage(media->path);
        return true;
    }
    // Past the out-point (outgoing clip of a transition) use media handles,
    // holding the last frame where the media ends.
    TimeUs src = clip.sourceAt(at);
    if (media->duration > 0)
        src = std::min(src, media->duration - 40 * kUsPerMs);
    driveLane(lane, media->path, usToMs(std::max<TimeUs>(0, src)), forceSeek, clip.speed);
    return true;
}

void TimelinePlayer::syncVideo(bool forceSeek)
{
    const ProjectData &d = m_project->data();
    const QVector<int> videos = d.videoTrackIndices();
    while (m_video.size() < videos.size() * 2)
        m_video.append(makeLane(false, m_video.size()));
    while (m_video.size() > videos.size() * 2) {
        Lane l = m_video.takeLast();
        l.player->stop();
        l.player->deleteLater();
    }

    QVector<ViewTrack> tracks;
    for (int vi = 0; vi < videos.size(); ++vi) {
        const Track &track = d.tracks[videos[vi]];
        Lane &main = m_video[vi * 2];
        Lane &aux = m_video[vi * 2 + 1];
        int ci = track.muted ? -1 : timeline::clipIndexAt(track, m_pos);
        if (ci >= 0 && !track.clips[ci].enabled)
            ci = -1;
        if (ci < 0) {
            stopLane(main);
            stopLane(aux);
            continue;
        }
        const Clip &clip = track.clips[ci];
        ViewTrack vt;
        if (!prepareLayer(vt.layer, main, clip, m_pos, forceSeek)) {
            stopLane(main);
            stopLane(aux);
            continue;
        }
        vt.layer.alpha = timeline::videoAlpha(track, clip, m_pos);
        const Transition tr = timeline::effectiveTransition(track, clip);
        const Clip *prev = tr.isNone() ? nullptr : timeline::previousAdjacent(track, clip);
        if (prev && !prev->enabled)
            prev = nullptr;
        if (prev && m_pos < clip.start + tr.duration && prepareLayer(vt.outgoing, aux, *prev, m_pos, forceSeek)) {
            vt.inTransition = true;
            vt.transition = tr.type;
            vt.transitionProgress = double(m_pos - clip.start) / double(tr.duration);
            vt.outgoing.alpha = 1.0;
        } else {
            stopLane(aux);
        }
        tracks.append(vt);
    }
    m_view->setTracks(tracks);
    m_view->setPlaceholderText(d.duration() > 0 ? QString() : tr("Drag clips from the Media Bin onto the timeline"));
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
        const Clip *clip = ci >= 0 && track.clips[ci].enabled ? &track.clips[ci] : nullptr;
        const MediaItem *media = clip ? d.mediaById(clip->mediaId) : nullptr;
        if (!clip || !media || !media->hasAudio || !timeline::isTrackAudible(d, ti)) {
            stopLane(lane);
            continue;
        }
        double vol = track.volume * clip->gain * timeline::fadeEnvelope(*clip, m_pos);
        if (track.fx.duckUnderVoice)
            vol *= duckGain(voiceRanges(d, ti), track.fx.duckAmountDb, m_pos);
        lane.output->setVolume(float(std::clamp(vol, 0.0, 1.0)));
        if (!m_playing) {
            // Silent while paused, but keep the source loaded for a fast start.
            if (lane.source != media->path)
                driveLane(lane, media->path, usToMs(clip->sourceAt(m_pos)), true, clip->speed);
            else if (lane.player->playbackState() == QMediaPlayer::PlayingState)
                lane.player->pause();
            continue;
        }
        driveLane(lane, media->path, usToMs(clip->sourceAt(m_pos)), forceSeek, clip->speed);
    }
}

} // namespace spotlight
