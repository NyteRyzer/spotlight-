#pragma once

#include "core/exportpreset.h"
#include "core/projectdata.h"

#include <QObject>
#include <QProcess>
#include <QStringList>

namespace spotlight {

struct GraphBuilder;

// Renders the timeline to a file using FFmpeg.
//
// The timeline is turned into an FFmpeg filter graph: each visible video track
// becomes a full-frame stream (clips fitted, colour-graded, keyed, cropped,
// transformed and animated; transparent gaps; xfade transitions) and the
// tracks are overlaid bottom to top over black; audio clips are delayed to their timeline position, mixed per
// track, run through the track's effects (cleanup, gate, de-esser, EQ,
// compressor, pitch, echo, reverb, limiter), volume, ducking and pan, then
// mixed to the master bus.
class Exporter : public QObject {
    Q_OBJECT
public:
    explicit Exporter(QObject *parent = nullptr);
    ~Exporter() override;

    // Builds the full FFmpeg command line (without the program name).
    // Returns an empty list and sets *error if the project can't be exported.
    static QStringList buildArguments(const ProjectData &project, const ExportPreset &preset,
                                      const QString &outputPath, QString *error);

    // Locates an ffmpeg binary: bundled next to the app first, then PATH and
    // common install locations (GUI apps on macOS don't inherit the shell PATH).
    static QString findFfmpeg();
    static QString findFfprobe();
    // Shared lookup used by both (name without extension).
    static QString findTool(const QString &name);

    // Media files used on the timeline that no longer exist on disk.
    static QStringList missingMedia(const ProjectData &project);

    // Default file extension for a preset's container.
    static QString extensionFor(const ExportPreset &preset);

    bool start(const ProjectData &project, const ExportPreset &preset, const QString &outputPath, QString *error);
    void cancel();
    bool isRunning() const;

signals:
    void progress(double fraction); // 0..1
    void finished(bool success, const QString &message);

private:
    void readProgress();
    // Appends one video track's filter chains; returns its output label.
    static QString buildVideoTrack(GraphBuilder &g, const ProjectData &d, const Track &t, int trackIndex,
                                   const QSize &frame, double fps);

    QProcess *m_process = nullptr;
    qint64 m_totalUs = 0;
    QString m_output;
    QString m_log;
    bool m_cancelled = false;
};

} // namespace spotlight
