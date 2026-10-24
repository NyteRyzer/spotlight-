#pragma once

#include "core/types.h"

#include <QObject>
#include <QPair>
#include <QProcess>
#include <QStringList>
#include <QVector>

// Media analysis with FFmpeg: silence detection (for "remove silences" /
// jump cuts) and scene-change detection (for "split at scene cuts").
namespace spotlight::analysis {

using Range = QPair<TimeUs, TimeUs>;

// Arguments for scanning the source range [in, in + duration) of a file.
QStringList silenceArgs(const QString &path, TimeUs in, TimeUs duration, double thresholdDb, TimeUs minSilence);
QStringList sceneArgs(const QString &path, TimeUs in, TimeUs duration, double threshold);

// Parses FFmpeg's log. Times are relative to the scanned range start.
// A silence still open at the end of the log runs to `duration`.
QVector<Range> parseSilences(const QString &log, TimeUs duration);
QVector<TimeUs> parseScenes(const QString &log);

// Shrinks each silence by `padding` on both sides (so cuts don't clip word
// edges) and drops those that become shorter than `minLength`.
QVector<Range> padRanges(const QVector<Range> &ranges, TimeUs padding, TimeUs minLength);

// Runs one FFmpeg analysis in the background.
class Job : public QObject {
    Q_OBJECT
public:
    explicit Job(QObject *parent = nullptr);
    ~Job() override;
    bool start(const QStringList &args, QString *error);
    void cancel();

signals:
    void finished(bool ok, const QString &log);

private:
    QProcess *m_process = nullptr;
    QString m_log;
};

} // namespace spotlight::analysis
