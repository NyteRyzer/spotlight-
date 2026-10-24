#include "core/exporter.h"
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
