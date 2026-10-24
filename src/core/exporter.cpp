#include "core/exporter.h"

#include "core/looks.h"
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

} // namespace

struct GraphBuilder {
    QStringList inputs;  // ffmpeg input arguments
    QStringList filters; // filter_complex chains
    int inputCount = 0;
    int pieceCount = 0;

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

namespace {


// atempo only accepts 0.5..2 per instance (older FFmpeg); chain as needed.
QStringList atempoChain(double speed)
{
    QStringList r;
    speed = std::clamp(speed, 0.1, 8.0);
    while (speed > 2.0) {
        r << QStringLiteral("atempo=2.0");
        speed /= 2.0;
    }
    while (speed < 0.5) {
        r << QStringLiteral("atempo=0.5");
        speed /= 0.5;
    }
    if (!qFuzzyCompare(speed, 1.0))
        r << QStringLiteral("atempo=%1").arg(num(speed));
    return r;
}

QStringList audioFxChain(const AudioFx &fx, int sr)
{
    QStringList chain;
    if (fx.dialogueCleanup)
        chain << QStringLiteral("highpass=f=80") << QStringLiteral("afftdn=nf=-25");
    if (fx.gateEnabled) {
        const double thr = std::clamp(dbToLinear(fx.gateThresholdDb), 0.0001, 1.0);
        chain << QStringLiteral("agate=threshold=%1:ratio=8:attack=5:release=150:range=0.03").arg(num(thr));
    }
    if (fx.deEsser)
        chain << QStringLiteral("deesser=i=0.5:m=0.5:f=0.5");
    if (fx.eqEnabled) {
        chain << QStringLiteral("bass=g=%1:f=100").arg(num(fx.lowGainDb))
              << QStringLiteral("equalizer=f=1000:t=q:w=1:g=%1").arg(num(fx.midGainDb))
              << QStringLiteral("treble=g=%1:f=8000").arg(num(fx.highGainDb));
    }
    if (fx.compressorEnabled) {
        const double thr = std::clamp(dbToLinear(fx.thresholdDb), 0.000976563, 1.0);
        chain << QStringLiteral("acompressor=threshold=%1:ratio=%2:attack=20:release=250:makeup=1")
                     .arg(num(thr), num(std::clamp(fx.ratio, 1.0, 20.0)));
    }
    if (!qFuzzyIsNull(fx.pitchSemitones)) {
        // Resample to shift pitch, then time-stretch back to the original
        // length: a pitch change that keeps the timing.
        const double f = std::pow(2.0, std::clamp(fx.pitchSemitones, -12.0, 12.0) / 12.0);
        chain << QStringLiteral("asetrate=%1").arg(qRound(sr * f)) << QStringLiteral("aresample=%1").arg(sr)
              << atempoChain(1.0 / f);
    }
    if (fx.echoEnabled) {
        const double d = std::clamp(fx.echoDelayMs, 20.0, 2000.0);
        const double fb = std::clamp(fx.echoFeedback, 0.0, 0.9);
        chain << QStringLiteral("aecho=0.9:0.9:%1|%2|%3:%4|%5|%6")
                     .arg(num(d), num(2 * d), num(3 * d), num(fb), num(fb * fb), num(fb * fb * fb));
    }
    if (fx.reverbEnabled) {
        // A small multi-tap echo network approximates room reflections.
        const double size = 0.6 + std::clamp(fx.reverbSize, 0.0, 1.0) * 1.4;
        const double mix = std::clamp(fx.reverbMix, 0.0, 1.0);
        const double taps[] = {43, 67, 97, 131, 173, 229, 293};
        const double decays[] = {0.5, 0.42, 0.35, 0.29, 0.23, 0.18, 0.14};
        QStringList ds, gs;
        for (int i = 0; i < 7; ++i) {
            ds << num(taps[i] * size);
            gs << num(std::max(0.001, decays[i] * mix * (0.7 + 0.3 * fx.reverbSize)));
        }
        chain << QStringLiteral("aecho=0.9:0.9:%1:%2").arg(ds.join(QLatin1Char('|')), gs.join(QLatin1Char('|')));
    }
    if (fx.limiterEnabled) {
        const double lim = std::clamp(dbToLinear(fx.limiterCeilingDb), 0.0625, 1.0);
        chain << QStringLiteral("alimiter=limit=%1:attack=5:release=50:level=disabled").arg(num(lim));
    }
    return chain;
}

// FFmpeg expression reproducing duckGain() (see looks.h) over time t.
QString duckExpression(const QVector<TimeRange> &ranges, double amountDb)
{
    if (ranges.isEmpty())
        return {};
    const double g = std::pow(10.0, std::min(0.0, amountDb) / 20.0);
    const QString r = secs(kDuckRamp);
    QStringList terms;
    for (const auto &x : ranges) {
        terms << QStringLiteral("clip(min((t-%1)/%2\\,(%3-t)/%2)\\,0\\,1)")
                     .arg(secs(x.start - kDuckRamp), r, secs(x.end + kDuckRamp));
    }
    return QStringLiteral("1-%1*min(1\\,%2)").arg(num(1.0 - g), terms.join(QLatin1Char('+')));
}

QString hexColor(const QColor &c)
{
    return QStringLiteral("0x%1").arg(c.rgb() & 0xffffff, 6, 16, QLatin1Char('0'));
}

// FFmpeg expressions for a Ken Burns move (mirrors motionAt()).
struct MotionExpr {
    QString zoom = QStringLiteral("1");
    QString dx = QStringLiteral("0");
    QString dy = QStringLiteral("0");
};

MotionExpr motionExpr(Motion m, TimeUs duration)
{
    const QString p = QStringLiteral("min(max(t/%1\\,0)\\,1)").arg(secs(std::max<TimeUs>(duration, 1)));
    const QString a = num(kMotionAmount);
    const QString half = num(kMotionAmount / 2.0);
    const QString za = num(1.0 + kMotionAmount);
    MotionExpr e;
    switch (m) {
    case Motion::None:
        break;
    case Motion::ZoomIn:
        e.zoom = QStringLiteral("(1+%1*%2)").arg(a, p);
        break;
    case Motion::ZoomOut:
        e.zoom = QStringLiteral("(1+%1*(1-%2))").arg(a, p);
        break;
    case Motion::PanLeft:
        e.zoom = za;
        e.dx = QStringLiteral("(%1*(1-2*%2))").arg(half, p);
        break;
    case Motion::PanRight:
        e.zoom = za;
        e.dx = QStringLiteral("(-%1*(1-2*%2))").arg(half, p);
        break;
    case Motion::PanUp:
        e.zoom = za;
        e.dy = QStringLiteral("(%1*(1-2*%2))").arg(half, p);
        break;
    case Motion::PanDown:
        e.zoom = za;
        e.dy = QStringLiteral("(-%1*(1-2*%2))").arg(half, p);
        break;
    }
    return e;
}

int even(double v)
{
    return std::max(2, int(std::lround(v / 2.0)) * 2);
}

// Renders one clip as a full-frame, transparent-background piece lasting
// clip duration + `tail` (extra time used by a transition into the next
// clip). Returns the output label.
QString buildClipPiece(GraphBuilder &g, const ProjectData &d, const Clip &c, const MediaItem &m, const QSize &frame,
                       double fps, TimeUs tail, bool fadeInAllowed, bool fadeOutAllowed, int pieceIndex)
{
    const int W = frame.width();
    const int H = frame.height();
    const TimeUs dur = c.duration();
    const TimeUs pieceDur = dur + tail;

    // Real media past the out-point covers as much of the tail as possible;
    // the rest holds the last frame.
    TimeUs handle = 0;
    if (tail > 0 && !m.isStill() && m.duration > c.out)
        handle = std::min(tail, c.toTimeline(m.duration - c.out));
    const TimeUs hold = tail - handle;

    const int idx = m.isStill() ? g.addInput(m, 0, pieceDur, fps)
                                : g.addInput(m, c.in, c.sourceDuration() + c.toSource(handle), fps);
    QStringList ch;
    ch << QStringLiteral("[%1:v]setpts=(PTS-STARTPTS)%2")
              .arg(idx)
              .arg(qFuzzyCompare(c.speed, 1.0) || m.isStill() ? QString() : QStringLiteral("/") + num(c.speed));
    if (c.stabilize && !m.isStill())
        ch << QStringLiteral("deshake");
    const QSize fit = fitSize(m.resolution, frame);
    ch << QStringLiteral("scale=%1:%2").arg(fit.width()).arg(fit.height()) << QStringLiteral("setsar=1")
       << QStringLiteral("fps=%1").arg(num(fps));
    if (hold > 0)
        ch << QStringLiteral("tpad=stop_mode=clone:stop_duration=%1").arg(secs(hold));
    ch << QStringLiteral("trim=duration=%1").arg(secs(pieceDur)) << QStringLiteral("setpts=PTS-STARTPTS");

    const Transform &tf = c.transform;
    if (tf.flipH)
        ch << QStringLiteral("hflip");
    if (tf.flipV)
        ch << QStringLiteral("vflip");

    const ColorAdjust &col = c.color;
    if (!col.isEqIdentity()) {
        ch << QStringLiteral("eq=brightness=%1:contrast=%2:saturation=%3")
                  .arg(num(col.brightness), num(col.contrast), num(col.saturation));
    }
    if (col.hasMatrix()) {
        const ColorMatrix mx = colorMatrix(col);
        ch << QStringLiteral("colorchannelmixer=rr=%1:rg=%2:rb=%3:gr=%4:gg=%5:gb=%6:br=%7:bg=%8:bb=%9")
                  .arg(num(mx[0]), num(mx[1]), num(mx[2]), num(mx[3]), num(mx[4]), num(mx[5]), num(mx[6]),
                       num(mx[7]), num(mx[8]));
    }
    if (col.blur > 0)
        ch << QStringLiteral("gblur=sigma=%1").arg(num(std::max(0.1, blurSigma(col.blur, fit.height()))));
    if (col.vignette > 0)
        ch << QStringLiteral("vignette=angle=%1").arg(num(vignetteAngle(col.vignette)));

    ch << QStringLiteral("format=yuva420p");
    if (c.key.enabled) {
        ch << QStringLiteral("chromakey=color=%1:similarity=%2:blend=%3")
                  .arg(hexColor(c.key.color), num(std::clamp(c.key.similarity, 0.01, 1.0)),
                       num(std::clamp(c.key.blend, 0.0, 1.0)));
    }

    // Crop (transparent where cropped away; the remaining picture stays put).
    double cl = std::clamp(tf.cropLeft, 0.0, 0.95), cr = std::clamp(tf.cropRight, 0.0, 0.95);
    double ct = std::clamp(tf.cropTop, 0.0, 0.95), cb = std::clamp(tf.cropBottom, 0.0, 0.95);
    if (cl + cr > 0.95)
        cr = 0.95 - cl;
    if (ct + cb > 0.95)
        cb = 0.95 - ct;
    int pw = fit.width(), ph = fit.height();
    if (tf.hasCrop()) {
        pw = even(fit.width() * (1.0 - cl - cr));
        ph = even(fit.height() * (1.0 - ct - cb));
        ch << QStringLiteral("crop=%1:%2:%3:%4")
                  .arg(pw)
                  .arg(ph)
                  .arg(int(fit.width() * cl))
                  .arg(int(fit.height() * ct));
    }
    if (tf.opacity < 1.0)
        ch << QStringLiteral("colorchannelmixer=aa=%1").arg(num(std::clamp(tf.opacity, 0.0, 1.0)));
    const bool rotated = !qFuzzyIsNull(std::fmod(tf.rotation, 360.0));
    if (rotated) {
        const double rad = tf.rotation * M_PI / 180.0;
        ch << QStringLiteral("rotate=a=%1:c=none:ow='rotw(%1)':oh='roth(%1)'").arg(num(rad));
    }

    const MotionExpr mo = motionExpr(c.motion, dur);
    const double s = std::clamp(tf.scale, 0.05, 5.0);
    const bool animated = c.motion != Motion::None;
    if (animated) {
        ch << QStringLiteral("scale=w='trunc(iw*%1*%2/2)*2':h='trunc(ih*%1*%2/2)*2':eval=frame")
                  .arg(num(s), mo.zoom);
    } else if (!qFuzzyCompare(s, 1.0)) {
        ch << QStringLiteral("scale=w='trunc(iw*%1/2)*2':h='trunc(ih*%1/2)*2'").arg(num(s));
    }

    // Fades work on alpha: over lower tracks they cross-fade, on V1 they dip
    // to the black base.
    if (fadeInAllowed && c.fadeIn > 0)
        ch << QStringLiteral("fade=t=in:st=0:d=%1:alpha=1").arg(secs(std::min(c.fadeIn, dur)));
    if (fadeOutAllowed && c.fadeOut > 0) {
        const TimeUs fo = std::min(c.fadeOut, dur);
        ch << QStringLiteral("fade=t=out:st=%1:d=%2:alpha=1").arg(secs(dur - fo), secs(fo));
    }
    const QString pic = QStringLiteral("[p%1]").arg(pieceIndex);
    g.filters << ch.join(QLatin1Char(',')) + pic;

    // Centre of the (possibly cropped) picture relative to the frame centre.
    const double rad = tf.rotation * M_PI / 180.0;
    const double ox0 = (cl - cr) / 2.0 * fit.width() * s;
    const double oy0 = (ct - cb) / 2.0 * fit.height() * s;
    const double ox = ox0 * std::cos(rad) - oy0 * std::sin(rad) + tf.x * W;
    const double oy = ox0 * std::sin(rad) + oy0 * std::cos(rad) + tf.y * H;
    const QString xExpr = QStringLiteral("(W-w)/2+%1+%2*%3").arg(num(ox), mo.dx, num(fit.width() * s));
    const QString yExpr = QStringLiteral("(H-h)/2+%1+%2*%3").arg(num(oy), mo.dy, num(fit.height() * s));

    const QString out = QStringLiteral("[q%1]").arg(pieceIndex);
    g.filters << QStringLiteral("color=c=black@0:s=%1x%2:r=%3:d=%4,format=yuva420p[bg%5]")
                     .arg(W)
                     .arg(H)
                     .arg(num(fps), secs(pieceDur))
                     .arg(pieceIndex);
    g.filters << QStringLiteral("[bg%1]%2overlay=x='%3':y='%4':format=auto:eof_action=pass,settb=AVTB%5")
                     .arg(pieceIndex)
                     .arg(pic, xExpr, yExpr, out);
    Q_UNUSED(d)
    return out;
}

} // namespace

QString Exporter::buildVideoTrack(GraphBuilder &g, const ProjectData &d, const Track &t, int trackIndex,
                                  const QSize &frame, double fps)
{
    struct Piece {
        QString label;
        TimeUs start = 0;
        Transition in; // xfade from the previous piece
    };
    QVector<Piece> pieces;
    TimeUs cursor = 0;
    for (int i = 0; i < t.clips.size(); ++i) {
        const Clip &c = t.clips[i];
        const MediaItem *m = d.mediaById(c.mediaId);
        if (c.start > cursor) {
            const QString gap = QStringLiteral("[gap%1]").arg(g.pieceCount);
            g.filters << QStringLiteral("color=c=black@0:s=%1x%2:r=%3:d=%4,format=yuva420p,settb=AVTB%5")
                             .arg(frame.width())
                             .arg(frame.height())
                             .arg(num(fps), secs(c.start - cursor), gap);
            ++g.pieceCount;
            pieces.append({gap, cursor, {}});
        }
        const Transition in = timeline::effectiveTransition(t, c);
        const Clip *next = i + 1 < t.clips.size() ? &t.clips[i + 1] : nullptr;
        const Transition nextIn = next ? timeline::effectiveTransition(t, *next) : Transition();
        const TimeUs tail = nextIn.isNone() ? 0 : nextIn.duration;
        QString label;
        if (m) {
            label = buildClipPiece(g, d, c, *m, frame, fps, tail, in.isNone(), nextIn.isNone(), g.pieceCount++);
        } else {
            label = QStringLiteral("[gap%1]").arg(g.pieceCount++);
            g.filters << QStringLiteral("color=c=black@0:s=%1x%2:r=%3:d=%4,format=yuva420p,settb=AVTB%5")
                             .arg(frame.width())
                             .arg(frame.height())
                             .arg(num(fps), secs(c.duration() + tail), label);
        }
        pieces.append({label, c.start, in});
        cursor = c.end();
    }

    QString acc = pieces.first().label;
    for (int i = 1; i < pieces.size(); ++i) {
        const Piece &p = pieces[i];
        const QString out = QStringLiteral("[tr%1_%2]").arg(trackIndex).arg(i);
        if (p.in.isNone()) {
            g.filters << acc + p.label + QStringLiteral("concat=n=2:v=1:a=0,settb=AVTB") + out;
        } else {
            g.filters << acc + p.label
                    + QStringLiteral("xfade=transition=%1:duration=%2:offset=%3")
                          .arg(p.in.type, secs(p.in.duration), secs(p.start))
                    + out;
        }
        acc = out;
    }
    return acc;
}

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
        // Every visible video track is rendered to a full-frame stream with
        // transparent gaps, then the tracks are stacked bottom (V1) to top
        // over black, so upper tracks act as overlays (titles, PIP, keyed
        // footage) exactly like the preview compositor.
        g.filters << QStringLiteral("color=c=black:s=%1:r=%2:d=%3,format=yuva420p,settb=AVTB[base]")
                         .arg(size, num(fps), secs(total));
        QString acc = QStringLiteral("[base]");
        int layer = 0;
        for (int ti : d.videoTrackIndices()) {
            const Track &t = d.tracks[ti];
            if (t.muted || t.clips.isEmpty())
                continue;
            const QString trackOut = buildVideoTrack(g, d, t, ti, QSize(W, H), fps);
            const QString next = QStringLiteral("[vl%1]").arg(layer++);
            g.filters << acc + trackOut + QStringLiteral("overlay=format=auto:eof_action=pass") + next;
            acc = next;
        }
        g.filters << acc + QStringLiteral("format=yuv420p[vout]");
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
            const int idx = g.addInput(*m, c.in, c.sourceDuration(), fps);
            const QString label = QStringLiteral("[c%1]").arg(idx);
            const qint64 delayMs = usToMs(c.start);
            QStringList chain;
            chain << QStringLiteral("[%1:a]aresample=%2").arg(idx).arg(sr)
                  << QStringLiteral("aformat=sample_fmts=fltp:channel_layouts=stereo")
                  << QStringLiteral("atrim=duration=%1").arg(secs(c.sourceDuration()))
                  << QStringLiteral("asetpts=PTS-STARTPTS");
            if (!qFuzzyCompare(c.speed, 1.0))
                chain << atempoChain(c.speed);
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
        chain << audioFxChain(t.fx, sr);
        if (!qFuzzyCompare(t.volume, 1.0))
            chain << QStringLiteral("volume=%1").arg(num(t.volume));
        if (t.fx.duckUnderVoice) {
            const QString expr = duckExpression(voiceRanges(d, ti), t.fx.duckAmountDb);
            if (!expr.isEmpty())
                chain << QStringLiteral("volume='%1':eval=frame").arg(expr);
        }
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
