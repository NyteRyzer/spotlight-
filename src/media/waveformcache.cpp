#include "media/waveformcache.h"

#include "core/exporter.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QProcess>
#include <QTimer>
#include <QUrl>

#include <cmath>

namespace spotlight {

namespace {
// ffmpeg decodes to mono 16-bit at this rate; each peak bucket covers
// kDecodeRate / kPeaksPerSecond samples.
constexpr int kDecodeRate = 4000;
} // namespace

WaveformCache *WaveformCache::instance()
{
    static WaveformCache *s = new WaveformCache;
    return s;
}

WaveformCache::WaveformCache(QObject *parent)
    : QObject(parent)
{
}

const QVector<float> *WaveformCache::peaks(const QString &path)
{
    auto it = m_cache.constFind(path);
    if (it != m_cache.constEnd())
        return &it.value();
    if (!m_failed.contains(path) && path != m_current && !m_queue.contains(path)) {
        m_queue.enqueue(path);
        if (m_current.isEmpty())
            QTimer::singleShot(0, this, &WaveformCache::next);
    }
    return nullptr;
}

void WaveformCache::next()
{
    if (!m_current.isEmpty() || m_queue.isEmpty())
        return;
    m_current = m_queue.dequeue();
    m_work.clear();
    const QString ffmpeg = Exporter::findFfmpeg();
    if (!ffmpeg.isEmpty())
        decodeWithFfmpeg(ffmpeg);
    else
        decodeWithQt();
}

void WaveformCache::finish(const QVector<float> &peaks)
{
    const QString path = m_current;
    if (peaks.isEmpty())
        m_failed.insert(path);
    else
        m_cache.insert(path, peaks);
    m_current.clear();
    if (!peaks.isEmpty())
        emit ready(path);
    QTimer::singleShot(0, this, &WaveformCache::next);
}

void WaveformCache::decodeWithFfmpeg(const QString &ffmpeg)
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const QByteArray raw = m_process->readAllStandardOutput();
        m_process->deleteLater();
        m_process = nullptr;
        QVector<float> peaks;
        if (code == 0) {
            const auto *samples = reinterpret_cast<const qint16 *>(raw.constData());
            const qsizetype n = raw.size() / 2;
            const int perBucket = kDecodeRate / kPeaksPerSecond;
            peaks.reserve(n / perBucket + 1);
            for (qsizetype i = 0; i < n; i += perBucket) {
                int peak = 0;
                for (qsizetype j = i; j < std::min(n, i + perBucket); ++j)
                    peak = std::max(peak, std::abs(int(samples[j])));
                peaks.append(float(peak) / 32768.0f);
            }
        }
        finish(peaks);
    });
    m_process->start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                              QStringLiteral("-nostdin"), QStringLiteral("-i"), m_current, QStringLiteral("-vn"),
                              QStringLiteral("-ac"), QStringLiteral("1"), QStringLiteral("-ar"),
                              QString::number(kDecodeRate), QStringLiteral("-f"), QStringLiteral("s16le"),
                              QStringLiteral("-")});
}

void WaveformCache::decodeWithQt()
{
    if (!m_decoder) {
        m_decoder = new QAudioDecoder(this);
        connect(m_decoder, &QAudioDecoder::bufferReady, this, [this] {
            const QAudioBuffer buf = m_decoder->read();
            const QAudioFormat fmt = buf.format();
            const int channels = std::max(1, fmt.channelCount());
            const qint64 startUs = buf.startTime();
            const int frames = buf.frameCount();
            const double rate = fmt.sampleRate() > 0 ? fmt.sampleRate() : 48000;
            for (int f = 0; f < frames; ++f) {
                float v = 0;
                for (int c = 0; c < channels; ++c) {
                    const int idx = f * channels + c;
                    float s = 0;
                    switch (fmt.sampleFormat()) {
                    case QAudioFormat::Int16: s = buf.constData<qint16>()[idx] / 32768.0f; break;
                    case QAudioFormat::Int32: s = float(buf.constData<qint32>()[idx] / 2147483648.0); break;
                    case QAudioFormat::Float: s = buf.constData<float>()[idx]; break;
                    case QAudioFormat::UInt8: s = (buf.constData<quint8>()[idx] - 128) / 128.0f; break;
                    default: break;
                    }
                    v = std::max(v, std::abs(s));
                }
                const qint64 bucket = (startUs + qint64(f / rate * 1e6)) * kPeaksPerSecond / 1000000;
                if (bucket < 0 || bucket > 100000000)
                    continue;
                if (m_work.size() <= bucket)
                    m_work.resize(bucket + 1);
                m_work[bucket] = std::max(m_work[bucket], v);
            }
        });
        connect(m_decoder, &QAudioDecoder::finished, this, [this] { finish(m_work); });
        connect(m_decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this,
                [this](QAudioDecoder::Error) {
                    m_decoder->stop();
                    finish({});
                });
    }
    m_decoder->setSource(QUrl::fromLocalFile(m_current));
    m_decoder->start();
}

} // namespace spotlight
