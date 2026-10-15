#include "core/exporter.h"
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
        QVERIFY(g.contains("concat=n=2:v=1:a=0[vout]"));
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

    // End-to-end render with real FFmpeg, using generated test media.
    void rendersWithFfmpeg()
    {
        const QString ffmpeg = Exporter::findFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg not installed");

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
};

QTEST_GUILESS_MAIN(TestExporter)
#include "test_exporter.moc"
