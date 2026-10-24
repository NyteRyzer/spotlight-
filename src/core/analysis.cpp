#include "core/analysis.h"

#include "core/exporter.h"

#include <QRegularExpression>

#include <algorithm>

namespace spotlight::analysis {

namespace {

QString secs(TimeUs t)
{
    return QString::number(usToSeconds(t), 'f', 6);
}

QStringList inputArgs(const QString &path, TimeUs in, TimeUs duration)
{
    return {QStringLiteral("-hide_banner"), QStringLiteral("-nostats"), QStringLiteral("-nostdin"),
            QStringLiteral("-ss"), secs(in), QStringLiteral("-t"), secs(duration), QStringLiteral("-i"), path};
}

} // namespace

QStringList silenceArgs(const QString &path, TimeUs in, TimeUs duration, double thresholdDb, TimeUs minSilence)
{
    return inputArgs(path, in, duration)
        << QStringLiteral("-vn") << QStringLiteral("-af")
        << QStringLiteral("silencedetect=noise=%1dB:d=%2").arg(thresholdDb).arg(secs(minSilence))
        << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");
}

QStringList sceneArgs(const QString &path, TimeUs in, TimeUs duration, double threshold)
{
    // Analyse a small copy of the picture: much faster, same cuts.
    return inputArgs(path, in, duration)
        << QStringLiteral("-an") << QStringLiteral("-vf")
        << QStringLiteral("scale=320:-2,scdet=threshold=%1").arg(threshold) << QStringLiteral("-f")
        << QStringLiteral("null") << QStringLiteral("-");
}

QVector<Range> parseSilences(const QString &log, TimeUs duration)
{
    static const QRegularExpression startRe(QStringLiteral("silence_start:\\s*(-?[0-9.]+)"));
    static const QRegularExpression endRe(QStringLiteral("silence_end:\\s*(-?[0-9.]+)"));
    QVector<Range> out;
    TimeUs open = -1;
    for (const QString &line : log.split(QLatin1Char('\n'))) {
        const auto s = startRe.match(line);
        if (s.hasMatch())
            open = std::max<TimeUs>(0, secondsToUs(s.captured(1).toDouble()));
        const auto e = endRe.match(line);
        if (e.hasMatch() && open >= 0) {
            const TimeUs end = std::min(duration, secondsToUs(e.captured(1).toDouble()));
            if (end > open)
                out.append({open, end});
            open = -1;
        }
    }
    if (open >= 0 && open < duration)
        out.append({open, duration});
    return out;
}

QVector<TimeUs> parseScenes(const QString &log)
{
    static const QRegularExpression re(QStringLiteral("lavfi\\.scd\\.time:\\s*([0-9.]+)"));
    QVector<TimeUs> out;
    auto it = re.globalMatch(log);
    while (it.hasNext()) {
        const TimeUs t = secondsToUs(it.next().captured(1).toDouble());
        if (t > 0 && (out.isEmpty() || t - out.last() > kMinClipDuration))
            out.append(t);
    }
    return out;
}

QVector<Range> padRanges(const QVector<Range> &ranges, TimeUs padding, TimeUs minLength)
{
    QVector<Range> out;
    for (const auto &r : ranges) {
        // Keep the very start / end of the scanned range unpadded.
        const TimeUs a = r.first == 0 ? 0 : r.first + padding;
        const TimeUs b = r.second - padding;
        if (b - a >= minLength)
            out.append({a, b});
    }
    return out;
}

Job::Job(QObject *parent)
    : QObject(parent)
{
}

Job::~Job()
{
    cancel();
}

bool Job::start(const QStringList &args, QString *error)
{
    const QString ffmpeg = Exporter::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        if (error)
            *error = QStringLiteral("FFmpeg was not found.");
        return false;
    }
    m_log.clear();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this] {
        const QString chunk = QString::fromUtf8(m_process->readAll());
        m_log += chunk;
        const qsizetype i = chunk.lastIndexOf(QStringLiteral("out_time_us="));
        if (i >= 0) {
            const qint64 us = chunk.mid(i + 12, chunk.indexOf(QLatin1Char('\n'), i) - i - 12).toLongLong();
            if (us > 0)
                emit progress(us);
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        m_log += QString::fromUtf8(m_process->readAll());
        m_process->deleteLater();
        m_process = nullptr;
        emit finished(st == QProcess::NormalExit && code == 0, m_log);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && m_process) {
            m_process->deleteLater();
            m_process = nullptr;
            emit finished(false, QStringLiteral("Could not start FFmpeg."));
        }
    });
    m_process->start(ffmpeg, args);
    return true;
}

void Job::cancel()
{
    if (!m_process)
        return;
    m_process->disconnect(this);
    m_process->kill();
    m_process->waitForFinished(2000);
    m_process->deleteLater();
    m_process = nullptr;
}

} // namespace spotlight::analysis
