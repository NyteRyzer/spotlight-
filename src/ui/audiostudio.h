#pragma once

#include <QMainWindow>

#include <functional>
#include <QUuid>
#include <QVector>

class QHBoxLayout;
class QLabel;
class QToolButton;
class QSlider;
class QCheckBox;
class QGroupBox;
class QFormLayout;

namespace spotlight {

class Project;
struct Track;
class TimelinePlayer;
class TimelineWidget;
class ClipInspector;
class MediaBin;

// One channel strip in the mixer.
class ChannelStrip : public QWidget {
    Q_OBJECT
public:
    ChannelStrip(Project *project, const QUuid &trackId, QWidget *parent = nullptr);
    void refresh();
    QUuid trackId() const { return m_trackId; }

signals:
    void selectTrack(const QUuid &trackId);

private:
    void edit(const QString &what, const std::function<void(Track &)> &fn);

    Project *m_project;
    QUuid m_trackId;
    QLabel *m_name;
    QLabel *m_db;
    QSlider *m_volume;
    QSlider *m_pan;
    QToolButton *m_mute;
    QToolButton *m_solo;
    QToolButton *m_fx;
    bool m_updating = false;
};

// The selected audio track's effect chain: cleanup (denoise, gate,
// de-esser), EQ, compressor, voice changer, reverb/echo, limiter, plus voice
// track / auto-ducking and one-click presets.
class TrackFxPanel : public QWidget {
    Q_OBJECT
public:
    TrackFxPanel(Project *project, QWidget *parent = nullptr);
    void setTrack(const QUuid &trackId);

private:
    void refresh();
    void editFx(const QString &key, const std::function<void(Track &)> &fn);
    QSlider *slider(QFormLayout *form, const QString &label, int min, int max, double scale, const QString &suffix,
                    std::function<double(const Track &)> get, std::function<void(Track &, double)> set);
    QCheckBox *check(QFormLayout *form, const QString &label, std::function<bool(const Track &)> get,
                     std::function<void(Track &, bool)> set);

    Project *m_project;
    QUuid m_trackId;
    QLabel *m_title;
    QWidget *m_body;
    QVector<std::function<void(const Track &)>> m_refreshers;
    bool m_updating = false;
};

// Audio Studio: waveform timeline of audio tracks, mixer and track effects.
class AudioStudio : public QMainWindow {
    Q_OBJECT
public:
    AudioStudio(Project *project, TimelinePlayer *player, MediaBin *bin, QWidget *parent = nullptr);

    TimelineWidget *timeline() const { return m_timeline; }
    ClipInspector *inspector() const { return m_inspector; }

signals:
    void recordRequested();

private:
    void rebuildMixer();

    Project *m_project;
    TimelinePlayer *m_player;
    TimelineWidget *m_timeline;
    ClipInspector *m_inspector;
    TrackFxPanel *m_fx;
    QWidget *m_mixer;
    QHBoxLayout *m_mixerLayout;
    QVector<ChannelStrip *> m_strips;
    QLabel *m_time;
    QToolButton *m_playBtn;
};

} // namespace spotlight
