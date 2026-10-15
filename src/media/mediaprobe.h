#pragma once

#include "core/media.h"

#include <QImage>
#include <QObject>
#include <QQueue>
#include <QTimer>

class QMediaPlayer;
class QVideoSink;

namespace spotlight {

// Reads duration, streams and resolution of media files and grabs a
// thumbnail. Files are processed one at a time in the order queued.
class MediaProbe : public QObject {
    Q_OBJECT
public:
    explicit MediaProbe(QObject *parent = nullptr);

    void probe(const QString &path);
    bool isBusy() const { return !m_current.isEmpty() || !m_queue.isEmpty(); }

    static bool isImageFile(const QString &path);
    // File dialog filter covering supported media.
    static QString fileFilter();

signals:
    void probed(const spotlight::MediaItem &item, const QImage &thumbnail);
    void failed(const QString &path, const QString &error);

private:
    void next();
    void finish();
    void fail(const QString &error);

    QQueue<QString> m_queue;
    QString m_current;
    MediaItem m_item;
    QImage m_thumb;
    bool m_seeking = false;
    QMediaPlayer *m_player = nullptr;
    QVideoSink *m_sink = nullptr;
    QTimer m_timeout;
};

} // namespace spotlight
