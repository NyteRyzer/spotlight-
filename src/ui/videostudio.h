#pragma once

#include <QMainWindow>

namespace spotlight {

class Project;
class MediaBin;
class SourceMonitor;
class ProgramMonitor;
class TimelineWidget;
class ClipInspector;

// Video Studio: media bin, source + program monitors, inspector, timeline.
// Each studio is its own QMainWindow so its dock layout can be customised and
// saved independently.
class VideoStudio : public QMainWindow {
    Q_OBJECT
public:
    VideoStudio(Project *project, QWidget *parent = nullptr);

    MediaBin *mediaBin() const { return m_bin; }
    SourceMonitor *sourceMonitor() const { return m_source; }
    ProgramMonitor *programMonitor() const { return m_program; }
    TimelineWidget *timeline() const { return m_timeline; }
    ClipInspector *inspector() const { return m_inspector; }

signals:
    void captureFrame(const QImage &frame);
    void status(const QString &message);

private:
    Project *m_project;
    MediaBin *m_bin;
    SourceMonitor *m_source;
    ProgramMonitor *m_program;
    TimelineWidget *m_timeline;
    ClipInspector *m_inspector;
};

} // namespace spotlight
