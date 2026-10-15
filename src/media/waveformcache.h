#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QVector>

class QProcess;
class QAudioDecoder;

namespace spotlight {

// Computes and caches audio peak data for drawing waveforms.
//
// Peaks are stored at kPeaksPerSecond buckets (0..1 absolute amplitude).
// Decoding uses the ffmpeg CLI when available (fast, every format), falling
// back to QAudioDecoder otherwise. Files are decoded one at a time in the
// background; `ready` is emitted when a file's peaks become available.
class WaveformCache : public QObject {
    Q_OBJECT
public:
    static constexpr int kPeaksPerSecond = 100;

    static WaveformCache *instance();

    // Returns cached peaks, or nullptr and schedules decoding.
    const QVector<float> *peaks(const QString &path);

signals:
    void ready(const QString &path);

private:
    explicit WaveformCache(QObject *parent = nullptr);
    void next();
    void finish(const QVector<float> &peaks);
    void decodeWithFfmpeg(const QString &ffmpeg);
    void decodeWithQt();

    QHash<QString, QVector<float>> m_cache;
    QSet<QString> m_failed;
    QQueue<QString> m_queue;
    QString m_current;
    QVector<float> m_work;
    QProcess *m_process = nullptr;
    QAudioDecoder *m_decoder = nullptr;
};

} // namespace spotlight
