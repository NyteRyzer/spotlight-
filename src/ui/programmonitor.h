#pragma once

#include "core/types.h"

#include <QWidget>

class QLabel;
class QToolButton;

namespace spotlight {

class Project;
class TimelinePlayer;
class VideoView;

// Shows the edited sequence and owns the timeline transport controls.
class ProgramMonitor : public QWidget {
    Q_OBJECT
public:
    ProgramMonitor(Project *project, QWidget *parent = nullptr);

    TimelinePlayer *player() const { return m_player; }
    VideoView *view() const { return m_view; }

signals:
    void captureFrame(const QImage &frame);

private:
    void updateTime();

    Project *m_project;
    VideoView *m_view;
    TimelinePlayer *m_player;
    QLabel *m_time;
    QToolButton *m_playBtn;
};

} // namespace spotlight
