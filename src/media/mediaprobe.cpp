#include "media/mediaprobe.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QUrl>
#include <QUuid>
#include <QVideoFrame>
#include <QVideoSink>

namespace spotlight {

namespace {
constexpr int kThumbWidth = 320;
}

MediaProbe::MediaProbe(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        // Metadata may be complete even if no frame arrived in time.
        if (m_item.duration > 0 || m_item.hasAudio || m_item.hasVideo)
            finish();
        else
            fail(QStringLiteral("Timed out reading media."));
    });
}

bool MediaProbe::isImageFile(const QString &path)
{
    const QByteArray suffix = QFileInfo(path).suffix().toLower().toLatin1();
    return !suffix.isEmpty() && QImageReader::supportedImageFormats().contains(suffix)
        && suffix != "gif"; // animated GIFs are better treated as video
}

QString MediaProbe::fileFilter()
{
    return QStringLiteral(
        "All Media (*.mp4 *.mov *.m4v *.mkv *.webm *.avi *.mts *.m2ts *.mpg *.mpeg *.wmv *.flv *.gif "
        "*.mp3 *.wav *.aac *.m4a *.flac *.ogg *.opus *.aif *.aiff *.wma "
        "*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;"
        "Video (*.mp4 *.mov *.m4v *.mkv *.webm *.avi *.mts *.m2ts *.mpg *.mpeg *.wmv *.flv *.gif);;"
        "Audio (*.mp3 *.wav *.aac *.m4a *.flac *.ogg *.opus *.aif *.aiff *.wma);;"
        "Images (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;"
        "All Files (*)");
}

void MediaProbe::probe(const QString &path)
{
    m_queue.enqueue(path);
    if (m_current.isEmpty())
        next();
}

void MediaProbe::next()
{
    m_current.clear();
    if (m_queue.isEmpty())
        return;
    m_current = m_queue.dequeue();
    m_item = MediaItem();
    m_item.id = QUuid::createUuid();
    m_item.path = QFileInfo(m_current).absoluteFilePath();
    m_item.name = QFileInfo(m_current).fileName();
    m_thumb = QImage();
    m_seeking = false;

    if (!QFileInfo::exists(m_current)) {
        fail(QStringLiteral("File not found."));
        return;
    }

    if (isImageFile(m_current)) {
        QImageReader reader(m_current);
        reader.setAutoTransform(true);
        const QImage img = reader.read();
        if (img.isNull()) {
            fail(reader.errorString());
            return;
        }
        m_item.kind = MediaKind::Image;
        m_item.resolution = img.size();
        m_item.hasVideo = true;
        m_thumb = img.scaledToWidth(kThumbWidth, Qt::SmoothTransformation);
        finish();
        return;
    }

    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_sink = new QVideoSink(this);
        m_player->setVideoSink(m_sink);
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
            if (m_current.isEmpty() || m_seeking)
                return;
            if (s == QMediaPlayer::LoadedMedia) {
                m_item.duration = m_player->duration() * 1000;
                m_item.hasVideo = m_player->hasVideo();
                m_item.hasAudio = m_player->hasAudio();
                const QMediaMetaData md = m_player->metaData();
                m_item.resolution = md.value(QMediaMetaData::Resolution).toSize();
                m_item.frameRate = md.value(QMediaMetaData::VideoFrameRate).toDouble();
                m_item.kind = m_item.hasVideo ? MediaKind::Video : MediaKind::Audio;
                if (!m_item.hasVideo && !m_item.hasAudio) {
                    fail(QStringLiteral("No audio or video streams found."));
                    return;
                }
                if (!m_item.hasVideo) {
                    finish();
                    return;
                }
                // Grab a frame a little way in, to skip black openings.
                m_seeking = true;
                const qint64 pos = std::min<qint64>(1000, m_player->duration() / 10);
                m_player->setPosition(pos);
                m_player->play();
            } else if (s == QMediaPlayer::InvalidMedia) {
                fail(m_player->errorString().isEmpty() ? QStringLiteral("Unsupported or corrupt file.")
                                                       : m_player->errorString());
            }
        });
        connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            if (m_current.isEmpty() || !m_seeking || !frame.isValid())
                return;
            const QImage img = frame.toImage();
            if (img.isNull())
                return;
            m_thumb = img.scaledToWidth(kThumbWidth, Qt::SmoothTransformation);
            if (!m_item.resolution.isValid() || m_item.resolution.isEmpty())
                m_item.resolution = frame.size();
            finish();
        });
    }
    m_timeout.start(8000);
    m_player->setSource(QUrl::fromLocalFile(m_item.path));
}

void MediaProbe::finish()
{
    m_timeout.stop();
    if (m_player) {
        m_seeking = false;
        m_player->stop();
        m_player->setSource(QUrl());
    }
    const MediaItem item = m_item;
    const QImage thumb = m_thumb;
    m_current.clear();
    emit probed(item, thumb);
    QTimer::singleShot(0, this, &MediaProbe::next);
}

void MediaProbe::fail(const QString &error)
{
    m_timeout.stop();
    const QString path = m_current;
    if (m_player) {
        m_seeking = false;
        m_player->stop();
        m_player->setSource(QUrl());
    }
    m_current.clear();
    emit failed(path, error);
    QTimer::singleShot(0, this, &MediaProbe::next);
}

} // namespace spotlight
