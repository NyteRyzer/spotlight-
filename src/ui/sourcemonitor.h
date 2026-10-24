#pragma once

#include "core/media.h"

#include <QWidget>

class QMediaPlayer;
class QAudioOutput;
class QSlider;
class QLabel;
class QToolButton;

namespace spotlight {

class VideoView;

// Previews a single media item from the bin, with in/out marks for
// three-point editing ("Add to Timeline" places the marked range).
class SourceMonitor : public QWidget {
    Q_OBJECT
public:
    explicit SourceMonitor(QWidget *parent = nullptr);

    void load(const MediaItem &item);
    // Loads the item with in/out marks set to [in, out) and parks the
    // playhead on the in-point (used to preview Auto-Clip highlights).
    void loadRange(const MediaItem &item, TimeUs in, TimeUs out);
    QUuid mediaId() const { return m_item.id; }
    QImage currentFrame() const;

public slots:
    void togglePlay();
    void markIn();
    void markOut();

signals:
    void addToTimeline(const QUuid &mediaId, spotlight::TimeUs in, spotlight::TimeUs out);
    void captureFrame(const QImage &frame);

private:
    void updateLabels();

    MediaItem m_item;
    VideoView *m_view;
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QSlider *m_slider;
    QLabel *m_time;
    QLabel *m_marks;
    QToolButton *m_playBtn;
    TimeUs m_in = 0;
    TimeUs m_out = -1;
    qint64 m_pendingSeekMs = -1;
};

} // namespace spotlight
