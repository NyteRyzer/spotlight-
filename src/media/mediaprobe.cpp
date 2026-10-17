#include "media/mediaprobe.h"

#include "core/exporter.h"

#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QUrl>
#include <QUuid>
#include <QVideoFrame>
#include <QVideoSink>

#include <cmath>

namespace spotlight {

namespace {
constexpr int kThumbWidth = 320;

// "30000/1001" -> 29.97
double parseRate(const QString &r)
{
    const QStringList parts = r.split(QLatin1Char('/'));
    if (parts.size() == 2 && parts[1].toDouble() > 0)
        return parts[0].toDouble() / parts[1].toDouble();
    return r.toDouble();
}
} // namespace

MediaProbe::MediaProbe(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_process) {
            // ffprobe/ffmpeg hung: keep what we have, or fall back to Qt.
            m_process->disconnect(this);
            m_process->kill();
            m_process->deleteLater();
            m_process = nullptr;
            if (m_item.duration > 0) {
                finish();
                return;
            }
            probeWithQt();
            return;
        }
        // Metadata may be complete even if no frame arrived in time.
        readPlayerInfo();
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

    const QString ffprobe = Exporter::findFfprobe();
    if (!ffprobe.isEmpty())
        probeWithFfprobe(ffprobe);
    else
        probeWithQt();
}

void MediaProbe::probeWithFfprobe(const QString &ffprobe)
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QByteArray out = m_process->readAllStandardOutput();
        m_process->deleteLater();
        m_process = nullptr;
        if (status != QProcess::NormalExit || code != 0) {
            probeWithQt(); // let Qt try before giving up
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(out).object();
        double seconds = root[QStringLiteral("format")].toObject()[QStringLiteral("duration")].toString().toDouble();
        for (const auto &v : root[QStringLiteral("streams")].toArray()) {
            const QJsonObject st = v.toObject();
            const QString type = st[QStringLiteral("codec_type")].toString();
            if (type == QLatin1String("audio")) {
                m_item.hasAudio = true;
            } else if (type == QLatin1String("video")) {
                // Embedded cover art in music files is not a video stream.
                if (st[QStringLiteral("disposition")].toObject()[QStringLiteral("attached_pic")].toInt() == 1)
                    continue;
                if (m_item.hasVideo)
                    continue;
                m_item.hasVideo = true;
                int w = st[QStringLiteral("width")].toInt();
                int h = st[QStringLiteral("height")].toInt();
                // Phones store portrait video as rotated landscape.
                int rotation = st[QStringLiteral("tags")].toObject()[QStringLiteral("rotate")].toString().toInt();
                for (const auto &sd : st[QStringLiteral("side_data_list")].toArray()) {
                    if (sd.toObject().contains(QStringLiteral("rotation")))
                        rotation = sd.toObject()[QStringLiteral("rotation")].toInt();
                }
                if (std::abs(rotation) % 180 == 90)
                    std::swap(w, h);
                m_item.resolution = QSize(w, h);
                m_item.frameRate = parseRate(st[QStringLiteral("avg_frame_rate")].toString());
                if (m_item.frameRate <= 0 || m_item.frameRate > 1000)
                    m_item.frameRate = parseRate(st[QStringLiteral("r_frame_rate")].toString());
            }
            if (seconds <= 0)
                seconds = st[QStringLiteral("duration")].toString().toDouble();
        }
        if (!m_item.hasVideo && !m_item.hasAudio) {
            fail(QStringLiteral("No audio or video streams found."));
            return;
        }
        m_item.duration = secondsToUs(seconds);
        if (m_item.duration <= 0) {
            fail(QStringLiteral("Could not read the media duration."));
            return;
        }
        m_item.kind = m_item.hasVideo ? MediaKind::Video : MediaKind::Audio;
        if (m_item.hasVideo)
            grabThumbnailWithFfmpeg();
        else
            finish();
    });
    m_timeout.start(15000);
    m_process->start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-print_format"),
                               QStringLiteral("json"), QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
                               m_item.path});
}

void MediaProbe::grabThumbnailWithFfmpeg()
{
    const QString ffmpeg = Exporter::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        finish();
        return;
    }
    // A frame a little way in, to skip black openings.
    const double at = std::min(1.0, usToSeconds(m_item.duration) / 10.0);
    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        const QImage img = QImage::fromData(m_process->readAllStandardOutput(), "PNG");
        m_process->deleteLater();
        m_process = nullptr;
        if (!img.isNull())
            m_thumb = img;
        finish();
    });
    m_process->start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
                              QStringLiteral("-ss"), QString::number(at, 'f', 3), QStringLiteral("-i"), m_item.path,
                              QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-vf"),
                              QStringLiteral("scale=%1:-2").arg(kThumbWidth), QStringLiteral("-f"),
                              QStringLiteral("image2pipe"), QStringLiteral("-c:v"), QStringLiteral("png"),
                              QStringLiteral("-")});
}

void MediaProbe::readPlayerInfo()
{
    if (!m_player)
        return;
    if (m_player->duration() > 0)
        m_item.duration = msToUs(m_player->duration());
    m_item.hasVideo = m_item.hasVideo || m_player->hasVideo();
    m_item.hasAudio = m_item.hasAudio || m_player->hasAudio();
    const QMediaMetaData md = m_player->metaData();
    if (md.value(QMediaMetaData::Resolution).toSize().isValid())
        m_item.resolution = md.value(QMediaMetaData::Resolution).toSize();
    if (md.value(QMediaMetaData::VideoFrameRate).toDouble() > 0)
        m_item.frameRate = md.value(QMediaMetaData::VideoFrameRate).toDouble();
    m_item.kind = m_item.hasVideo ? MediaKind::Video : MediaKind::Audio;
}

void MediaProbe::probeWithQt()
{
    m_item.duration = 0;
    m_item.hasVideo = m_item.hasAudio = false;
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_sink = new QVideoSink(this);
        m_player->setVideoSink(m_sink);
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
            if (m_current.isEmpty() || m_seeking)
                return;
            if (s == QMediaPlayer::LoadedMedia) {
                readPlayerInfo();
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
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
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
