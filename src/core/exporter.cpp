#include "core/exporter.h"

#include "core/timelineops.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace spotlight {

namespace {

QString num(double v)
{
    return QString::number(v, 'f', 6);
}

QString secs(TimeUs t)
{
    return num(usToSeconds(t));
}

double dbToLinear(double db)
{
    return std::pow(10.0, db / 20.0);
}

struct GraphBuilder {
    QStringList inputs;  // ffmpeg input arguments
    QStringList filters; // filter_complex chains
    int inputCount = 0;

    int addInput(const MediaItem &m, TimeUs in, TimeUs duration, double fps)
    {
        if (m.isStill()) {
            inputs << QStringLiteral("-loop") << QStringLiteral("1") << QStringLiteral("-framerate") << num(fps)
                   << QStringLiteral("-t") << secs(duration) << QStringLiteral("-i") << m.path;
        } else {
            inputs << QStringLiteral("-ss") << secs(in) << QStringLiteral("-t") << secs(duration)
                   << QStringLiteral("-i") << m.path;
        }
        return inputCount++;
    }
};

} // namespace

Exporter::Exporter(QObject *parent)
    : QObject(parent)
{
}

Exporter::~Exporter()
{
    cancel();
}

QString Exporter::extensionFor(const ExportPreset &preset)
{
    return preset.container.isEmpty() ? QStringLiteral("mp4") : preset.container;
}

QStringList Exporter::buildArguments(const ProjectData &d, const ExportPreset &preset, const QString &outputPath,
                                     QString *error)
{
    const TimeUs total = d.duration();
    if (total <= 0) {
        if (error)
            *error = QStringLiteral("The timeline is empty. Add some clips before exporting.");
        return {};
    }

    const int W = preset.width & ~1; // encoders need even dimensions
    const int H = preset.height & ~1;
    const double fps = preset.frameRate > 0 ? preset.frameRate : d.sequence.frameRate;
    const int sr = preset.sampleRate > 0 ? preset.sampleRate : 48000;
    const QString size = QStringLiteral("%1x%2").arg(W).arg(H);

    GraphBuilder g;

    // ---- Video -----------------------------------------------------------
    if (!preset.isAudioOnly()) {
        const QVector<timeline::VideoSegment> segs = timeline::videoSegments(d);
        QStringList labels;
        for (int i = 0; i < segs.size(); ++i) {
            const auto &s = segs[i];
            const QString label = QStringLiteral("[v%1]").arg(i);
            labels << label;
            const Clip *c = s.clipId.isNull() ? nullptr : d.clipById(s.clipId);
            const MediaItem *m = c ? d.mediaById(c->mediaId) : nullptr;
            if (!c || !m) {
                g.filters << QStringLiteral("color=c=black:s=%1:r=%2:d=%3,format=yuv420p,setsar=1%4")
                                 .arg(size, num(fps), secs(s.duration()), label);
                continue;
            }
            const TimeUs srcIn = m->isStill() ? 0 : c->in + (s.start - c->start);
            const int idx = g.addInput(*m, srcIn, s.duration(), fps);
            QStringList chain;
            chain << QStringLiteral("[%1:v]scale=%2:%3:force_original_aspect_ratio=decrease").arg(idx).arg(W).arg(H)
                  << QStringLiteral("pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black").arg(W).arg(H)
                  << QStringLiteral("setsar=1") << QStringLiteral("fps=%1").arg(num(fps))
                  << QStringLiteral("trim=duration=%1").arg(secs(s.duration()))
                  << QStringLiteral("setpts=PTS-STARTPTS");
            if (!c->color.isIdentity()) {
                chain << QStringLiteral("eq=brightness=%1:contrast=%2:saturation=%3")
                             .arg(num(c->color.brightness), num(c->color.contrast), num(c->color.saturation));
            }
            if (c->fadeIn > 0 && s.start == c->start)
                chain << QStringLiteral("fade=t=in:st=0:d=%1").arg(secs(std::min(c->fadeIn, s.duration())));
            if (c->fadeOut > 0 && s.end == c->end()) {
                const TimeUs fo = std::min(c->fadeOut, s.duration());
                chain << QStringLiteral("fade=t=out:st=%1:d=%2").arg(secs(s.duration() - fo), secs(fo));
            }
            chain << QStringLiteral("format=yuv420p");
            g.filters << chain.join(QLatin1Char(',')) + label;
        }
        g.filters << labels.join(QString()) + QStringLiteral("concat=n=%1:v=1:a=0[vout]").arg(segs.size());
    }

    // ---- Audio -----------------------------------------------------------
    QStringList trackLabels;
    for (int ti = 0; ti < d.tracks.size(); ++ti) {
        if (!timeline::isTrackAudible(d, ti))
            continue;
        const Track &t = d.tracks[ti];
        QStringList clipLabels;
        for (const Clip &c : t.clips) {
            const MediaItem *m = d.mediaById(c.mediaId);
            if (!m || !m->hasAudio)
                continue;
            const int idx = g.addInput(*m, c.in, c.duration(), fps);
            const QString label = QStringLiteral("[c%1]").arg(idx);
            const qint64 delayMs = usToMs(c.start);
            QStringList chain;
            chain << QStringLiteral("[%1:a]aresample=%2").arg(idx).arg(sr)
                  << QStringLiteral("aformat=sample_fmts=fltp:channel_layouts=stereo")
                  << QStringLiteral("atrim=duration=%1").arg(secs(c.duration()))
                  << QStringLiteral("asetpts=PTS-STARTPTS");
            if (!qFuzzyCompare(c.gain, 1.0))
                chain << QStringLiteral("volume=%1").arg(num(c.gain));
            if (c.fadeIn > 0)
                chain << QStringLiteral("afade=t=in:st=0:d=%1").arg(secs(c.fadeIn));
            if (c.fadeOut > 0)
                chain << QStringLiteral("afade=t=out:st=%1:d=%2").arg(secs(c.duration() - c.fadeOut), secs(c.fadeOut));
            chain << QStringLiteral("adelay=%1|%1").arg(delayMs);
            g.filters << chain.join(QLatin1Char(',')) + label;
            clipLabels << label;
        }
        if (clipLabels.isEmpty())
            continue;

        const QString trackLabel = QStringLiteral("[t%1]").arg(ti);
        QStringList chain;
        if (clipLabels.size() == 1)
            chain << clipLabels.first() + QStringLiteral("anull");
        else
            chain << clipLabels.join(QString())
                         + QStringLiteral("amix=inputs=%1:duration=longest:normalize=0").arg(clipLabels.size());
        if (t.fx.dialogueCleanup)
            chain << QStringLiteral("highpass=f=80") << QStringLiteral("afftdn=nf=-25");
        if (t.fx.eqEnabled) {
            chain << QStringLiteral("bass=g=%1:f=100").arg(num(t.fx.lowGainDb))
                  << QStringLiteral("equalizer=f=1000:t=q:w=1:g=%1").arg(num(t.fx.midGainDb))
                  << QStringLiteral("treble=g=%1:f=8000").arg(num(t.fx.highGainDb));
        }
        if (t.fx.compressorEnabled) {
            const double thr = std::clamp(dbToLinear(t.fx.thresholdDb), 0.000976563, 1.0);
            chain << QStringLiteral("acompressor=threshold=%1:ratio=%2:attack=20:release=250:makeup=1")
                         .arg(num(thr), num(std::clamp(t.fx.ratio, 1.0, 20.0)));
        }
        if (!qFuzzyCompare(t.volume, 1.0))
            chain << QStringLiteral("volume=%1").arg(num(t.volume));
        if (!qFuzzyIsNull(t.pan)) {
            const double l = std::min(1.0, 1.0 - t.pan);
            const double r = std::min(1.0, 1.0 + t.pan);
            chain << QStringLiteral("pan=stereo|c0=%1*c0|c1=%2*c1").arg(num(l), num(r));
        }
        g.filters << chain.join(QLatin1Char(',')) + trackLabel;
        trackLabels << trackLabel;
    }

    if (trackLabels.isEmpty()) {
        g.filters << QStringLiteral("anullsrc=r=%1:cl=stereo,atrim=duration=%2[aout]").arg(sr).arg(secs(total));
    } else {
        QString mix = trackLabels.size() == 1
            ? trackLabels.first() + QStringLiteral("anull")
            : trackLabels.join(QString())
                + QStringLiteral("amix=inputs=%1:duration=longest:normalize=0").arg(trackLabels.size());
        // Loudness normalization (EBU R128) to the platform target, with a
        // -1.5 dBTP ceiling so the encoder never clips.
        if (preset.loudnessLufs < 0)
            mix += QStringLiteral(",loudnorm=I=%1:TP=-1.5:LRA=11").arg(num(preset.loudnessLufs));
        g.filters << mix + QStringLiteral(",apad,atrim=duration=%1[aout]").arg(secs(total));
    }

    // ---- Command line ----------------------------------------------------
    QStringList args;
    args << QStringLiteral("-y") << QStringLiteral("-hide_banner") << QStringLiteral("-nostdin")
         << QStringLiteral("-progress") << QStringLiteral("pipe:1") << QStringLiteral("-nostats");
    args << g.inputs;
    args << QStringLiteral("-filter_complex") << g.filters.join(QLatin1Char(';'));
    if (!preset.isAudioOnly())
        args << QStringLiteral("-map") << QStringLiteral("[vout]");
    args << QStringLiteral("-map") << QStringLiteral("[aout]");

    if (!preset.isAudioOnly()) {
        args << QStringLiteral("-c:v") << preset.videoCodec;
        if (preset.videoCodec == QLatin1String("libvpx-vp9")) {
            args << QStringLiteral("-b:v") << QStringLiteral("0") << QStringLiteral("-crf")
                 << QString::number(preset.crf) << QStringLiteral("-row-mt") << QStringLiteral("1");
        } else {
            args << QStringLiteral("-crf") << QString::number(preset.crf);
            if (!preset.speedPreset.isEmpty())
                args << QStringLiteral("-preset") << preset.speedPreset;
        }
        if (preset.videoCodec == QLatin1String("libx265")
            && (preset.container == QLatin1String("mp4") || preset.container == QLatin1String("mov")))
            args << QStringLiteral("-tag:v") << QStringLiteral("hvc1");
        args << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p") << QStringLiteral("-r") << num(fps);
    }

    args << QStringLiteral("-c:a") << preset.audioCodec;
    if (preset.audioBitrateKbps > 0 && !preset.audioCodec.startsWith(QLatin1String("pcm_")))
        args << QStringLiteral("-b:a") << QStringLiteral("%1k").arg(preset.audioBitrateKbps);
    args << QStringLiteral("-ar") << QString::number(sr);

    if (preset.container == QLatin1String("mp4") || preset.container == QLatin1String("mov")
        || preset.container == QLatin1String("m4a"))
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    args << QStringLiteral("-t") << secs(total);
    args << outputPath;
    return args;
}

QString Exporter::findFfmpeg()
{
    return findTool(QStringLiteral("ffmpeg"));
}

QString Exporter::findFfprobe()
{
    return findTool(QStringLiteral("ffprobe"));
}

QString Exporter::findTool(const QString &name)
{
#ifdef Q_OS_WIN
    const QString exe = name + QStringLiteral(".exe");
#else
    const QString exe = name;
#endif
    // Bundled copies first: next to the binary (Linux AppImage, macOS
    // Contents/MacOS), then Contents/Resources and lib/spotlight.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList bundled{appDir + QLatin1Char('/') + exe, appDir + QStringLiteral("/../Resources/") + exe,
                              appDir + QStringLiteral("/../lib/spotlight/") + exe};
    for (const auto &p : bundled) {
        if (QFileInfo(p).isExecutable())
            return QFileInfo(p).canonicalFilePath();
    }
    const QString onPath = QStandardPaths::findExecutable(exe);
    if (!onPath.isEmpty())
        return onPath;
    // GUI apps on macOS don't inherit the shell PATH.
    return QStandardPaths::findExecutable(exe, {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"),
                                                QStringLiteral("/opt/local/bin"), QStringLiteral("/usr/bin"),
                                                QStringLiteral("/snap/bin")});
}

QStringList Exporter::missingMedia(const ProjectData &d)
{
    QSet<QUuid> used;
    for (const auto &t : d.tracks) {
        for (const auto &c : t.clips)
            used.insert(c.mediaId);
    }
    QStringList missing;
    for (const auto &m : d.media) {
        if (used.contains(m.id) && !QFileInfo::exists(m.path))
            missing << m.name;
    }
    return missing;
}

bool Exporter::start(const ProjectData &project, const ExportPreset &preset, const QString &outputPath,
                     QString *error)
{
    if (isRunning()) {
        if (error)
            *error = QStringLiteral("An export is already running.");
        return false;
    }
    const QString ffmpeg = findFfmpeg();
    if (ffmpeg.isEmpty()) {
        if (error)
            *error = QStringLiteral("FFmpeg was not found. Install it (macOS: \"brew install ffmpeg\", "
                                    "Linux: your package manager) and try again.");
        return false;
    }
    const QStringList missing = missingMedia(project);
    if (!missing.isEmpty()) {
        if (error)
            *error = QStringLiteral("Some media used on the timeline can't be found:\n\n%1\n\n"
                                    "Use File → Relink Missing Media to locate it.")
                         .arg(missing.join(QLatin1Char('\n')));
        return false;
    }
    const QStringList args = buildArguments(project, preset, outputPath, error);
    if (args.isEmpty())
        return false;

    m_totalUs = project.duration();
    m_output = outputPath;
    m_log.clear();
    m_cancelled = false;

    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Exporter::readProgress);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_log += QString::fromUtf8(m_process->readAllStandardError());
        if (m_log.size() > 20000)
            m_log = m_log.right(20000);
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = !m_cancelled && status == QProcess::NormalExit && code == 0;
        QString msg;
        if (ok) {
            msg = QStringLiteral("Exported to %1").arg(m_output);
        } else if (m_cancelled) {
            msg = QStringLiteral("Export cancelled.");
            QFile::remove(m_output);
        } else {
            msg = QStringLiteral("FFmpeg failed (exit code %1).\n\n%2").arg(code).arg(m_log.right(4000));
        }
        m_process->deleteLater();
        m_process = nullptr;
        if (ok)
            emit progress(1.0);
        emit finished(ok, msg);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_process->deleteLater();
            m_process = nullptr;
            emit finished(false, QStringLiteral("Could not start FFmpeg."));
        }
    });
    m_process->start(ffmpeg, args);
    return true;
}

void Exporter::readProgress()
{
    while (m_process && m_process->canReadLine()) {
        const QByteArray line = m_process->readLine().trimmed();
        if (line.startsWith("out_time_us=") || line.startsWith("out_time_ms=")) {
            // Despite its name, out_time_ms is also in microseconds.
            const qint64 us = line.mid(line.indexOf('=') + 1).toLongLong();
            if (m_totalUs > 0 && us >= 0)
                emit progress(std::clamp(double(us) / double(m_totalUs), 0.0, 1.0));
        }
    }
}

void Exporter::cancel()
{
    if (!m_process)
        return;
    m_cancelled = true;
    m_process->kill();
    m_process->waitForFinished(3000);
}

bool Exporter::isRunning() const
{
    return m_process != nullptr;
}

} // namespace spotlight
