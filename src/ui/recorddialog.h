#pragma once

#include "core/types.h"

#include <QDialog>
#include <QUuid>

class QAudioSource;
class QFile;
class QCheckBox;
class QComboBox;
class QIODevice;
class QLabel;
class QPushButton;
class QTimer;

namespace spotlight {

class Project;
class TimelinePlayer;

// Input level meter: peak bar (green / yellow / red) with a peak-hold line.
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget *parent = nullptr);
    void setLevel(double peak); // linear 0..1
    QSize sizeHint() const override { return {320, 22}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    double m_level = 0; // smoothed, dBFS mapped to 0..1
    double m_hold = 0;
    int m_holdFrames = 0;
};

// Records audio from a microphone or audio interface onto the timeline.
//
// The input is captured with QAudioSource and written straight to a WAV
// file (lossless, and independent of the multimedia backend's encoders);
// the same stream drives the level meter. Offers a count-in and
// play-along, and records any number of takes: each starts at the playhead
// and lands on the chosen audio track (or a new track per take).
class RecordDialog : public QDialog {
    Q_OBJECT
public:
    RecordDialog(Project *project, TimelinePlayer *player, const QUuid &preferredTrack, QWidget *parent = nullptr);
    ~RecordDialog() override;

    int takesRecorded() const { return m_takes; }

signals:
    void status(const QString &message);

protected:
    void reject() override;

private:
    void startMeter();
    void stopMeter();
    void readMeter();
    void toggleRecord();
    void beginRecording();
    void stopRecording();
    void finishTake(const QString &path);
    void writeWavHeader(quint32 dataBytes);
    void refreshTracks();
    QString nextTakePath() const;

    Project *m_project;
    TimelinePlayer *m_player;
    QComboBox *m_device;
    QComboBox *m_track;
    QCheckBox *m_countIn;
    QCheckBox *m_playAlong;
    QCheckBox *m_voice;
    LevelMeter *m_meter;
    QLabel *m_peakLabel;
    QLabel *m_time;
    QLabel *m_info;
    QPushButton *m_record;
    QTimer *m_tick;
    QTimer *m_countdown;
    int m_countLeft = 0;

    QAudioSource *m_source = nullptr;
    QIODevice *m_sourceIo = nullptr;
    QFile *m_file = nullptr; // open while recording
    qint64 m_dataBytes = 0;

    TimeUs m_startAt = 0;
    int m_takes = 0;
    QUuid m_preferredTrack;
};

} // namespace spotlight
