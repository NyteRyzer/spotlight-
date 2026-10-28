#include "core/exporter.h"
#include "core/beats.h"
#include "core/highlights.h"
#include "core/looks.h"
#include "core/titles.h"
#include "core/timelineops.h"
#include "testutil.h"

#include <QtTest>

using namespace spotlight;
using namespace testutil;

class TestExporter : public QObject {
    Q_OBJECT

    static QString filterGraph(const QStringList &args)
    {
        const int i = args.indexOf("-filter_complex");
        return i >= 0 ? args.value(i + 1) : QString();
    }

private slots:
    void emptyTimelineFails()
    {
        QString err;
        const QStringList args = Exporter::buildArguments(ProjectData::createDefault(),
                                                          builtInExportPresets().first(), "/tmp/out.mp4", &err);
        QVERIFY(args.isEmpty());
        QVERIFY(!err.isEmpty());
    }

    void presetsAreSane()
    {
        const auto presets = builtInExportPresets();
        QVERIFY(presets.size() >= 8);
        for (const auto &p : presets) {
            QVERIFY(!p.name.isEmpty());
            QVERIFY(p.builtIn);
            if (!p.isAudioOnly())
                QVERIFY(p.width > 0 && p.height > 0 && p.frameRate > 0);
        }
    }

    void buildsVideoGraph()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S);
        const QUuid id = timeline::placeMedia(d, m, 0, 2 * S, S, 5 * S);
        d.clipById(id)->color.brightness = 0.1;
        d.tracks[2].fx.compressorEnabled = true;
        d.tracks[2].fx.dialogueCleanup = true;
        d.tracks[2].clips[0].fadeIn = S;

        QString err;
        const QStringList args = Exporter::buildArguments(d, builtInExportPresets().first(), "/tmp/o.mp4", &err);
        QVERIFY2(!args.isEmpty(), qPrintable(err));
        const QString g = filterGraph(args);
        QVERIFY(g.contains("color=c=black")); // 2 s gap at the start
        QVERIFY(g.contains("concat=n=2:v=1:a=0")); // gap + clip on V1
        QVERIFY(g.contains("overlay=format=auto:eof_action=pass"));
        QVERIFY(g.contains("[vout]"));
        QVERIFY(g.contains("eq=brightness=0.100000"));
        QVERIFY(g.contains("adelay=2000|2000"));
        QVERIFY(g.contains("afade=t=in"));
        QVERIFY(g.contains("acompressor="));
        QVERIFY(g.contains("afftdn"));
        QVERIFY(g.contains("[aout]"));
        QVERIFY(args.contains("[vout]"));
        QVERIFY(args.contains("libx264"));
        // Input seek to the clip's in-point.
        const int ss = args.indexOf("-ss");
        QCOMPARE(args.value(ss + 1), QStringLiteral("1.000000"));
        QCOMPARE(args.last(), QStringLiteral("/tmp/o.mp4"));
    }

    void loudnessNormalization()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addAudio(d, 10 * S);
        timeline::placeMedia(d, m, 2, 0);
        ExportPreset youtube = builtInExportPresets().first();
        QCOMPARE(youtube.loudnessLufs, -14.0);
        QString err;
        QVERIFY(filterGraph(Exporter::buildArguments(d, youtube, "/tmp/o.mp4", &err)).contains("loudnorm=I=-14"));
        youtube.loudnessLufs = 0;
        QVERIFY(!filterGraph(Exporter::buildArguments(d, youtube, "/tmp/o.mp4", &err)).contains("loudnorm"));
    }

    void mutedTracksAreSkipped()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addAudio(d, 10 * S);
        timeline::placeMedia(d, m, 2, 0);
        d.tracks[2].muted = true;
        QString err;
        ExportPreset mp3;
        for (const auto &p : builtInExportPresets()) {
            if (p.container == "mp3")
                mp3 = p;
        }
        const QStringList args = Exporter::buildArguments(d, mp3, "/tmp/o.mp3", &err);
        QVERIFY(!args.isEmpty());
        QVERIFY(filterGraph(args).contains("anullsrc"));
        QVERIFY(!args.contains("[vout]"));
        QVERIFY(!args.contains("-i")); // no inputs needed
    }

    void reportsMissingMedia()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid used = addVideo(d, 3 * S, false, "/no/such/used.mp4");
        addVideo(d, 3 * S, false, "/no/such/unused.mp4"); // in the bin only
        timeline::placeMedia(d, used, 0, 0);
        const QStringList missing = Exporter::missingMedia(d);
        QCOMPARE(missing.size(), 1);

        Exporter ex;
        QString err;
        QVERIFY(!ex.start(d, builtInExportPresets().first(), "/tmp/never.mp4", &err));
        if (!Exporter::findFfmpeg().isEmpty())
            QVERIFY2(err.contains("Relink"), qPrintable(err));
    }

    // End-to-end render with real FFmpeg, using generated test media.
    void rendersWithFfmpeg()
    {
        const QString ffmpeg = Exporter::findFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg not installed", "");

        QTemporaryDir tmp;
        const QString src = tmp.path() + "/src.mp4";
        QProcess gen;
        gen.start(ffmpeg, {"-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc=size=320x240:rate=25:duration=3", "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=3", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac",
                           "-shortest", src});
        QVERIFY(gen.waitForFinished(60000));
        QCOMPARE(gen.exitCode(), 0);

        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 3 * S, true, src);
        timeline::placeMedia(d, m, 0, 0, 0, 2 * S);
        timeline::placeMedia(d, m, 0, 3 * S, S, 3 * S); // leaves a 1 s gap

        ExportPreset p = builtInExportPresets().first();
        p.width = 320;
        p.height = 180;
        p.speedPreset = "ultrafast";

        Exporter ex;
        QSignalSpy done(&ex, &Exporter::finished);
        QString err;
        const QString out = tmp.path() + "/out.mp4";
        QVERIFY2(ex.start(d, p, out, &err), qPrintable(err));
        QVERIFY(done.wait(120000));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QVERIFY(QFileInfo(out).size() > 1000);
    }



    // ---- 50-feature pack: export --------------------------------------------
    void newVideoEffectsRender()
    {
        const QString ffmpeg = Exporter::findFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg not installed", "");
        QTemporaryDir tmp;
        const QString red = tmp.path() + "/red.mp4", tall = tmp.path() + "/tall.mp4", src = tmp.path() + "/src.mp4";
        QVERIFY(makeMedia(ffmpeg, red, "color=c=red:s=320x180:r=25", 3));
        QVERIFY(makeMedia(ffmpeg, tall, "color=c=0x00ff00:s=180x320:r=25", 3));
        QVERIFY(makeMedia(ffmpeg, src, "testsrc2=s=320x180:r=25", 3));

        // 1) Invert + cinema bars on red: cyan picture with black bars.
        ProjectData d = ProjectData::createDefault();
        const QUuid mr = addFile(d, red, 3 * S, {320, 180});
        const QUuid a = timeline::placeMedia(d, mr, 0, 0, 0, 3 * S);
        d.clipById(a)->fx.invert = true;
        d.clipById(a)->fx.cinemaBars = true;
        QString log;
        QVERIFY2(render(d, smallPreset(), tmp.path() + "/inv.mp4", &log), qPrintable(log));
        const QColor mid = pixelAt(ffmpeg, tmp.path() + "/inv.mp4", 1, 160, 90, 320, 180);
        QVERIFY2(mid.red() < 60 && mid.green() > 200 && mid.blue() > 200, qPrintable(mid.name()));
        const QColor top = pixelAt(ffmpeg, tmp.path() + "/inv.mp4", 1, 160, 5, 320, 180);
        QVERIFY2(top.red() < 30 && top.green() < 30 && top.blue() < 30, qPrintable(top.name()));

        // 2) Blur fill: a vertical green clip fills the sides with its blurred copy.
        ProjectData v = ProjectData::createDefault();
        const QUuid mt = addFile(v, tall, 3 * S, {180, 320});
        const QUuid b = timeline::placeMedia(v, mt, 0, 0, 0, 3 * S);
        v.clipById(b)->fx.blurFill = true;
        v.clipById(b)->fx.border = 0.02;
        QVERIFY2(render(v, smallPreset(), tmp.path() + "/fill.mp4", &log), qPrintable(log));
        const QColor side = pixelAt(ffmpeg, tmp.path() + "/fill.mp4", 1, 20, 90, 320, 180);
        QVERIFY2(side.green() > 120 && side.red() < 80, qPrintable(side.name())); // not black
        const QColor edge = pixelAt(ffmpeg, tmp.path() + "/fill.mp4", 1, 160 - 51 - 1, 90, 320, 180);
        QVERIFY2(edge.red() > 200 && edge.green() > 200 && edge.blue() > 200, qPrintable(edge.name())); // white border

        // 3) Everything else at once renders (sharpen, grain, pixelate, hue,
        //    gamma, entrance animation, new audio effects).
        ProjectData e = ProjectData::createDefault();
        const QUuid ms = addFile(e, src, 3 * S, {320, 180});
        const QUuid c = timeline::placeMedia(e, ms, 0, 0, 0, 3 * S);
        Clip *cc = e.clipById(c);
        cc->fx.sharpen = 0.5;
        cc->fx.grain = 0.5;
        cc->fx.pixelate = 0.3;
        cc->color.hue = 90;
        cc->color.gamma = 1.4;
        cc->motion = Motion::SlideInLeft;
        const QUuid t2 = timeline::placeMedia(e, ms, 1, 0, 0, 2 * S);
        e.clipById(t2)->motion = Motion::PopIn;
        e.clipById(t2)->transform.scale = 0.4;
        AudioFx &fx = e.tracks[e.audioTrackIndices()[0]].fx;
        fx.telephone = fx.chorus = fx.mono = true;
        fx.lowpassHz = 5000;
        fx.highpassHz = 120;
        QVERIFY2(render(e, smallPreset(), tmp.path() + "/all.mp4", &log), qPrintable(log));
        QVERIFY(std::abs(durationOf(tmp.path() + "/all.mp4") - 3.0) < 0.15);
        // The slide-in starts off-screen: at t=0 the left of V1 shows black.
        const QColor start = pixelAt(ffmpeg, tmp.path() + "/all.mp4", 0, 40, 150, 320, 180);
        QVERIFY2(start.red() < 40 && start.green() < 40 && start.blue() < 40, qPrintable(start.name()));
    }

    void gifRangeAndLoudness()
    {
        const QString ffmpeg = Exporter::findFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg not installed", "");
        QTemporaryDir tmp;
        const QString src = tmp.path() + "/src.mp4";
        QVERIFY(makeMedia(ffmpeg, src, "testsrc2=s=320x180:r=25", 6));
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addFile(d, src, 6 * S, {320, 180});
        timeline::placeMedia(d, m, 0, 0, 0, 6 * S);

        // GIF of the In/Out range (2 s .. 4 s).
        ExportPreset gif;
        for (const auto &p : builtInExportPresets()) {
            if (p.container == "gif")
                gif = p;
        }
        QCOMPARE(gif.container, QStringLiteral("gif"));
        gif.width = 160;
        gif.height = 90;
        QString log;
        const QString out = tmp.path() + "/clip.gif";
        QVERIFY2(render(timeline::rangeProject(d, 2 * S, 4 * S), gif, out, &log), qPrintable(log));
        QProcess probe;
        probe.start(Exporter::findFfprobe(), {"-v", "error", "-show_entries", "stream=codec_name,width",
                                             "-of", "csv=p=0", out});
        probe.waitForFinished(30000);
        QCOMPARE(QString::fromUtf8(probe.readAllStandardOutput()).trimmed(), QStringLiteral("gif,160"));
        QVERIFY(std::abs(durationOf(out) - 2.0) < 0.3);

        // Loudness of the mix: a 330 Hz sine at moderate level.
        QString err;
        const QStringList args = Exporter::loudnessArguments(d, &err);
        QVERIFY2(!args.isEmpty(), qPrintable(err));
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(ffmpeg, args);
        QVERIFY(p.waitForFinished(60000));
        const auto l = Exporter::parseLoudness(QString::fromUtf8(p.readAll()));
        QVERIFY(l.ok);
        QVERIFY2(l.integrated < -5 && l.integrated > -40, qPrintable(QString::number(l.integrated)));
        QVERIFY(l.truePeak < 0.5);
    }

    void detectsTempo()
    {
        const QString ffmpeg = Exporter::findFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg not installed", "");
        QTemporaryDir tmp;
        const QString song = tmp.path() + "/song.wav";
        // A 128 BPM kick pattern starting at 0.25 s over a quiet pad.
        QProcess gen;
        gen.start(ffmpeg, {"-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "aevalsrc='0.05*sin(2*PI*220*t)+0.9*sin(2*PI*60*t)*exp(-25*mod(t-0.25+10*60/128\\,60/128))"
                           "*gte(t\\,0.25)':s=44100:d=20",
                           song});
        QVERIFY(gen.waitForFinished(60000));
        QCOMPARE(gen.exitCode(), 0);
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(ffmpeg, beats::analysisArgs(song, 0, 20 * S));
        QVERIFY(p.waitForFinished(60000));
        const auto env = beats::parseEnvelope(QString::fromUtf8(p.readAll()));
        QVERIFY(env.size() > 1900);
        const auto tempo = beats::estimateTempo(env);
        QVERIFY2(std::abs(tempo.bpm - 128) < 1.5, qPrintable(QString::number(tempo.bpm)));
        // The grid lands on a kick (0.25 s + k * 60/128).
        const double period = 60.0 / 128;
        const double phase = std::fmod(tempo.firstBeat - 0.25 + 10 * period, period);
        QVERIFY2(phase < 0.05 || phase > period - 0.05, qPrintable(QString::number(tempo.firstBeat)));
    }

    // ---- Auto-Clip ----------------------------------------------------------
    void highlightsFromSignals()
    {
        highlights::Signals s;
        s.duration = 600;
        s.loudnessDb = QVector<double>(600, -32.0);
        for (int i = 0; i < 600; ++i)
            s.loudnessDb[i] += (i * 7919 % 13) / 6.0; // speech-like wobble
        for (int i = 120; i < 124; ++i)
            s.loudnessDb[i] = -8; // big reaction
        for (int i = 400; i < 403; ++i)
            s.loudnessDb[i] = -12; // smaller one
        for (int i = 280; i < 290; ++i)
            s.sceneCuts << i + 0.5 << i + 0.9; // montage, normal volume
        highlights::Options o;
        o.clipSeconds = 30;
        o.maxClips = 3;
        const auto hs = highlights::findHighlights(s, o);
        QCOMPARE(hs.size(), 3);
        // Strongest first, each clip contains its moment with the build-up before it.
        QVERIFY(hs[0].start < 120 * S && hs[0].end > 123 * S);
        QCOMPARE(hs[0].reason, QStringLiteral("Big reaction"));
        QVERIFY(hs[0].score >= hs[1].score && hs[1].score >= hs[2].score);
        bool foundSecond = false, foundMontage = false;
        for (const auto &h : hs) {
            QVERIFY(std::abs(usToSeconds(h.duration()) - 30) <= 5);
            foundSecond |= h.start < 400 * S && h.end > 402 * S;
            foundMontage |= h.start < 285 * S && h.end > 285 * S && h.reason == "Fast action";
        }
        QVERIFY(foundSecond);
        QVERIFY(foundMontage);
        // Clips never overlap.
        for (int i = 0; i < hs.size(); ++i)
            for (int j = i + 1; j < hs.size(); ++j)
                QVERIFY(hs[i].end <= hs[j].start || hs[j].end <= hs[i].start);
        // A flat video has no highlights.
        highlights::Signals flat;
        flat.duration = 300;
        flat.loudnessDb = QVector<double>(300, -30.0);
        QVERIFY(highlights::findHighlights(flat, o).isEmpty());
    }

    void highlightsFromRealVod()
    {
        const QString ffmpeg = Exporter::findFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg not installed", "");
        QTemporaryDir tmp;
        const QString vod = tmp.path() + "/vod.mp4";
        // 3 minutes of quiet chatter with two loud moments (at 50 s and 140 s).
        QProcess gen;
        gen.start(ffmpeg, {"-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=s=320x180:r=15:d=180", "-f", "lavfi", "-i",
                           "aevalsrc='0.03*sin(2*PI*220*t)*(1+0.5*sin(2*PI*3*t))*(1+30*between(t,50,53))"
                           "+0.03*sin(2*PI*330*t)*(1+20*between(t,140,142))':s=44100:d=180",
                           "-shortest", "-c:v", "libx264", "-preset", "ultrafast", "-c:a", "aac", vod});
        QVERIFY(gen.waitForFinished(120000));
        QCOMPARE(gen.exitCode(), 0);
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(ffmpeg, highlights::analysisArgs(vod, true, true));
        QVERIFY(p.waitForFinished(120000));
        QCOMPARE(p.exitCode(), 0);
        const auto sig = highlights::parseAnalysis(QString::fromUtf8(p.readAll()), 180);
        QVERIFY(sig.loudnessDb.size() >= 179);
        highlights::Options o;
        o.clipSeconds = 20;
        o.maxClips = 2;
        const auto hs = highlights::findHighlights(sig, o);
        QCOMPARE(hs.size(), 2);
        QVERIFY(hs[0].start <= 50 * S && hs[0].end >= 53 * S);
        QVERIFY(hs[1].start <= 140 * S && hs[1].end >= 142 * S);
    }

    // ---- Pro features ---------------------------------------------------
    static bool makeMedia(const QString &ffmpeg, const QString &out, const QString &video, double seconds)
    {
        QProcess gen;
        gen.start(ffmpeg, {"-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", video, "-f", "lavfi",
                           "-i", QString("sine=frequency=330:duration=%1").arg(seconds), "-t", QString::number(seconds),
                           "-c:v", "libx264", "-pix_fmt", "yuv444p", "-crf", "1", "-c:a", "aac", out});
        return gen.waitForFinished(60000) && gen.exitCode() == 0;
    }

    static QUuid addFile(ProjectData &d, const QString &path, TimeUs dur, QSize res)
    {
        const QUuid id = addVideo(d, dur, true, path);
        for (auto &m : d.media) {
            if (m.id == id)
                m.resolution = res;
        }
        return id;
    }

    // RGB of one pixel of the rendered output at time t.
    static QColor pixelAt(const QString &ffmpeg, const QString &file, double t, int x, int y, int W, int H)
    {
        QProcess p;
        p.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-ss", QString::number(t), "-i", file, "-frames:v", "1",
                         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"});
        p.waitForFinished(60000);
        const QByteArray raw = p.readAllStandardOutput();
        if (raw.size() < W * H * 3)
            return QColor();
        const int i = (y * W + x) * 3;
        return QColor(quint8(raw[i]), quint8(raw[i + 1]), quint8(raw[i + 2]));
    }

    static double durationOf(const QString &file)
    {
        QProcess p;
        p.start(Exporter::findFfprobe(), {"-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", file});
        p.waitForFinished(30000);
        return p.readAllStandardOutput().trimmed().toDouble();
    }

    static bool render(const ProjectData &d, ExportPreset p, const QString &out, QString *log)
    {
        Exporter ex;
        QSignalSpy done(&ex, &Exporter::finished);
        QString err;
        if (!ex.start(d, p, out, &err)) {
            *log = err;
            return false;
        }
        if (!done.wait(240000))
            return false;
        *log = done.first().at(1).toString();
        return done.first().at(0).toBool();
    }

    static ExportPreset smallPreset()
    {
        ExportPreset p = builtInExportPresets().first();
        p.width = 320;
        p.height = 180;
        p.frameRate = 25;
        p.speedPreset = "ultrafast";
        p.crf = 10;
        p.loudnessLufs = 0;
        return p;
    }

    // Compositing (PIP + chroma key over V1), a cross dissolve on V1 using
    // media handles, and opacity — checked on actual output pixels.
    void composesTracksAndTransitions()
    {
        const QString ffmpeg = Exporter::findFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg not installed", "");
        QTemporaryDir tmp;
        const QString red = tmp.path() + "/red.mp4", blue = tmp.path() + "/blue.mp4", gs = tmp.path() + "/gs.mp4";
        QVERIFY(makeMedia(ffmpeg, red, "color=c=red:s=320x180:r=25", 4));
        QVERIFY(makeMedia(ffmpeg, blue, "color=c=blue:s=320x180:r=25", 4));
        QVERIFY(makeMedia(ffmpeg, gs, "color=c=0x00ff00:s=320x180:r=25,drawbox=x=120:y=50:w=80:h=80:c=white:t=fill", 4));

        ProjectData d = ProjectData::createDefault();
        const QUuid mr = addFile(d, red, 4 * S, {320, 180});
        const QUuid mb = addFile(d, blue, 4 * S, {320, 180});
        const QUuid mg = addFile(d, gs, 4 * S, {320, 180});
        timeline::placeMedia(d, mr, 0, 0, 0, 2 * S);
        const QUuid b = timeline::placeMedia(d, mb, 0, 2 * S, 0, 2 * S);
        d.clipById(b)->transition = {QStringLiteral("fade"), S};
        const QUuid g = timeline::placeMedia(d, mg, 1, 0, 0, 4 * S);
        Clip *gc = d.clipById(g);
        gc->key.enabled = true;
        gc->transform.scale = 0.5;
        gc->transform.x = 0.25; // right half

        QString log;
        const QString out = tmp.path() + "/out.mp4";
        QVERIFY2(render(d, smallPreset(), out, &log), qPrintable(log));
        QVERIFY(std::abs(durationOf(out) - 4.0) < 0.15);

        // Left side: V1 only. Red, then mid-dissolve purple, then blue.
        const QColor c0 = pixelAt(ffmpeg, out, 0.5, 40, 90, 320, 180);
        QVERIFY2(c0.red() > 200 && c0.blue() < 60, qPrintable(c0.name()));
        const QColor c1 = pixelAt(ffmpeg, out, 2.5, 40, 90, 320, 180);
        QVERIFY2(c1.red() > 70 && c1.red() < 190 && c1.blue() > 70 && c1.blue() < 190, qPrintable(c1.name()));
        const QColor c2 = pixelAt(ffmpeg, out, 3.5, 40, 90, 320, 180);
        QVERIFY2(c2.blue() > 200 && c2.red() < 60, qPrintable(c2.name()));
        // Right half centre: the keyed clip's white square (scaled to 40x40 at x=240).
        const QColor w = pixelAt(ffmpeg, out, 0.5, 240, 90, 320, 180);
        QVERIFY2(w.red() > 220 && w.green() > 220 && w.blue() > 220, qPrintable(w.name()));
        // Inside the PIP but outside the square: green keyed out, red shows.
        const QColor k = pixelAt(ffmpeg, out, 0.5, 190, 60, 320, 180);
        QVERIFY2(k.red() > 200 && k.green() < 80, qPrintable(k.name()));
    }

    void speedLooksMotionAndTitles()
    {
        const QString ffmpeg = Exporter::findFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg not installed", "");
        QTemporaryDir tmp;
        const QString src = tmp.path() + "/src.mp4";
        QVERIFY(makeMedia(ffmpeg, src, "testsrc2=s=320x240:r=25", 4));

        ProjectData d = ProjectData::createDefault();
        const QUuid m = addFile(d, src, 4 * S, {320, 240});
        const QUuid id = timeline::placeMedia(d, m, 0, 0, 0, 4 * S);
        QVERIFY(timeline::setClipSpeed(d, id, 2.0));
        QCOMPARE(d.clipById(id)->duration(), 2 * S);
        Clip *c = d.clipById(id);
        c->color = lookPresets()[4].adjust; // Cinematic: eq + matrix + vignette
        c->color.blur = 0.2;
        c->motion = Motion::ZoomIn;
        c->transform.rotation = 10;
        c->transform.flipH = true;
        c->transform.cropLeft = 0.1;
        c->transform.opacity = 0.8;
        c->fadeIn = S / 2;
        c->stabilize = true;

        MediaItem title;
        QString err;
        QVERIFY2(titles::renderTitleMedia(titles::makeTitle(1, QSize(320, 180), "Name", "Role"), "Lower third",
                                          &title, &err),
                 qPrintable(err));
        d.media.append(title);
        timeline::placeMedia(d, title.id, 1, 0, 0, 2 * S);

        QString log;
        const QString out = tmp.path() + "/out.mp4";
        QVERIFY2(render(d, smallPreset(), out, &log), qPrintable(log));
        QVERIFY2(std::abs(durationOf(out) - 2.0) < 0.15, qPrintable(QString::number(durationOf(out))));
        QFile::remove(title.path);
    }

    void audioEffectsAndDucking()
    {
        const QString ffmpeg = Exporter::findFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg not installed", "");
        QTemporaryDir tmp;
        const QString src = tmp.path() + "/src.mp4";
        QVERIFY(makeMedia(ffmpeg, src, "color=c=gray:s=160x90:r=25", 4));

        ProjectData d = ProjectData::createDefault();
        const QUuid m = addAudio(d, 4 * S, src);
        const QVector<int> audio = d.audioTrackIndices();
        timeline::placeMedia(d, m, audio[0], 0, 0, 4 * S);
        timeline::placeMedia(d, m, audio[1], S, 0, 2 * S);
        Track &music = d.tracks[audio[0]];
        music.fx.duckUnderVoice = true;
        music.fx.reverbEnabled = true;
        music.fx.echoEnabled = true;
        music.fx.limiterEnabled = true;
        Track &voice = d.tracks[audio[1]];
        voice.voice = true;
        voice.fx.gateEnabled = true;
        voice.fx.deEsser = true;
        voice.fx.pitchSemitones = 4;
        voice.fx.eqEnabled = true;
        voice.fx.compressorEnabled = true;

        QCOMPARE(voiceRanges(d, audio[0]).size(), 1);
        QVERIFY(duckGain(voiceRanges(d, audio[0]), -12, 2 * S) < 0.3);
        QCOMPARE(duckGain(voiceRanges(d, audio[0]), -12, 0), 1.0);

        QString log;
        ExportPreset p = smallPreset();
        const QString out = tmp.path() + "/out.mp4";
        QVERIFY2(render(d, p, out, &log), qPrintable(log));
        QVERIFY(std::abs(durationOf(out) - 4.0) < 0.15);
    }
};

QTEST_MAIN(TestExporter)
#include "test_exporter.moc"
