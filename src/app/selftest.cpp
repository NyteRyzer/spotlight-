#include "selftest.h"

#include "core/designrender.h"
#include "core/exporter.h"
#include "core/exportpreset.h"
#include "core/project.h"
#include "core/timelineops.h"
#include "core/titles.h"
#include "media/mediaprobe.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QAudioDevice>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QMenuBar>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermissions>
#endif

#include <cstdio>
#include <functional>

namespace spotlight {

namespace {

struct Report {
    QStringList lines;
    int failures = 0;

    void add(bool ok, const QString &name, const QString &detail = QString())
    {
        const QString line = QStringLiteral("%1  %2%3")
                                 .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), name,
                                      detail.isEmpty() ? QString() : QStringLiteral(" — ") + detail);
        lines << line;
        if (!ok)
            ++failures;
        std::fprintf(stdout, "%s\n", qPrintable(line));
        std::fflush(stdout);
    }
    void info(const QString &text)
    {
        lines << QStringLiteral("INFO  ") + text;
        std::fprintf(stdout, "INFO  %s\n", qPrintable(text));
        std::fflush(stdout);
    }
};

bool waitUntil(const std::function<bool()> &cond, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return true;
}

int run(const QString &program, const QStringList &args, QString *out = nullptr, int timeoutMs = 120000)
{
    QProcess p;
    // Only stdout: diagnostics on stderr (e.g. DYLD_PRINT_LIBRARIES when the
    // CI audits library loads) must not end up in parsed values.
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        return -1;
    }
    if (out)
        *out = QString::fromUtf8(p.readAllStandardOutput());
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

double fileDuration(const QString &ffprobe, const QString &path)
{
    QString out;
    run(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                  QStringLiteral("format=duration"), QStringLiteral("-of"), QStringLiteral("csv=p=0"), path},
        &out);
    return out.trimmed().toDouble();
}

} // namespace

int runSelfTest(const QString &outDirArg)
{
    // Never touch the user's settings, autosaves or generated titles.
    QStandardPaths::setTestModeEnabled(true);
    const QString outDir = QDir(outDirArg).absolutePath();
    QDir().mkpath(outDir);
    Report r;
    r.info(QStringLiteral("SPOTLIGHT! %1 self-test, Qt %2, %3")
               .arg(QStringLiteral(SPOTLIGHT_VERSION), QString::fromLatin1(qVersion()),
                    QSysInfo::prettyProductName() + QLatin1Char(' ') + QSysInfo::currentCpuArchitecture()));
    r.info(QStringLiteral("App: %1").arg(QCoreApplication::applicationFilePath()));

    // 1. FFmpeg tools: present, runnable, and bundled when running from a package.
    const QString ffmpeg = Exporter::findFfmpeg();
    const QString ffprobe = Exporter::findFfprobe();
    const QString appDir = QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath();
    const bool inBundle = appDir.contains(QLatin1String(".app/Contents/MacOS"))
        || !qEnvironmentVariableIsEmpty("APPIMAGE");
    auto bundled = [&](const QString &tool) {
        if (!inBundle)
            return true;
        QString root = appDir;
        if (root.endsWith(QLatin1String("/MacOS")))
            root = QFileInfo(root + QStringLiteral("/..")).canonicalFilePath();
        else if (!qEnvironmentVariableIsEmpty("APPDIR"))
            root = QFileInfo(qEnvironmentVariable("APPDIR")).canonicalFilePath();
        return QFileInfo(tool).canonicalFilePath().startsWith(root);
    };
    r.add(!ffmpeg.isEmpty() && bundled(ffmpeg), QStringLiteral("ffmpeg found"), ffmpeg);
    r.add(!ffprobe.isEmpty() && bundled(ffprobe), QStringLiteral("ffprobe found"), ffprobe);
    QString ver;
    r.add(!ffmpeg.isEmpty() && run(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-version")}, &ver) == 0,
          QStringLiteral("ffmpeg runs"), ver.trimmed().section(QLatin1Char('\n'), 0, 0));
    if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
        r.add(false, QStringLiteral("remaining checks"), QStringLiteral("skipped: FFmpeg missing"));
        return 1;
    }

    // 2. Generated test media (video + audio).
    const QString clipA = outDir + QStringLiteral("/a.mp4");
    const QString clipB = outDir + QStringLiteral("/b.mp4");
    const QString gen = QStringLiteral("-y -hide_banner -loglevel error -f lavfi -i %1 -f lavfi -i sine=frequency=330:duration=3 "
                                       "-t 3 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest");
    auto makeClip = [&](const QString &src, const QString &out) {
        QStringList args = gen.arg(src).split(QLatin1Char(' '));
        args << out;
        return run(ffmpeg, args) == 0 && QFileInfo(out).size() > 1000;
    };
    r.add(makeClip(QStringLiteral("testsrc2=s=640x360:r=30"), clipA)
              && makeClip(QStringLiteral("color=c=0x2060ff:s=640x360:r=30"), clipB),
          QStringLiteral("encode H.264/AAC test media"));

    // 3. Probing (ffprobe path used by the media bin).
    QVector<MediaItem> probed;
    for (const QString &path : {clipA, clipB}) {
        MediaProbe probe;
        bool done = false;
        QObject::connect(&probe, &MediaProbe::probed, &probe, [&](const MediaItem &m, const QImage &) {
            probed.append(m);
            done = true;
        });
        QObject::connect(&probe, &MediaProbe::failed, &probe, [&](const QString &, const QString &e) {
            r.info(QStringLiteral("probe error: %1").arg(e));
            done = true;
        });
        probe.probe(path);
        waitUntil([&] { return done; }, 30000);
    }
    const bool probeOk = probed.size() == 2 && probed[0].duration > 2 * kUsPerSecond && probed[0].hasVideo
        && probed[0].hasAudio && probed[0].resolution == QSize(640, 360);
    r.add(probeOk, QStringLiteral("probe media"),
          probed.isEmpty() ? QString()
                           : QStringLiteral("%1×%2, %3")
                                 .arg(probed[0].resolution.width())
                                 .arg(probed[0].resolution.height())
                                 .arg(formatDuration(probed[0].duration)));
    if (!probeOk)
        return 1;

    // 4. Qt Multimedia decodes video (the preview path).
    {
        QMediaPlayer player;
        QVideoSink sink;
        player.setVideoSink(&sink);
        QImage frame;
        QObject::connect(&sink, &QVideoSink::videoFrameChanged, &sink, [&](const QVideoFrame &f) {
            if (frame.isNull() && f.isValid())
                frame = f.toImage();
        });
        player.setSource(QUrl::fromLocalFile(clipA));
        player.play();
        const bool ok = waitUntil([&] { return !frame.isNull(); }, 20000);
        player.stop();
        r.add(ok, QStringLiteral("Qt Multimedia playback"),
              ok ? QStringLiteral("%1×%2 frame").arg(frame.width()).arg(frame.height())
                 : QStringLiteral("no frame (multimedia plugin missing?)"));
    }

    // 5. Titles and design rendering (fonts, effects).
    MediaItem title;
    QString err;
    const bool titleOk = titles::renderTitleMedia(titles::makeTitle(1, QSize(640, 360), QStringLiteral("Self Test"),
                                                                    QStringLiteral("SPOTLIGHT!")),
                                                  QStringLiteral("Title"), &title, &err);
    int opaque = 0;
    if (titleOk) {
        const QImage img(title.path);
        for (int y = 0; y < img.height(); y += 4)
            for (int x = 0; x < img.width(); x += 4)
                opaque += qAlpha(img.pixel(x, y)) > 200 ? 1 : 0;
    }
    r.add(titleOk && opaque > 50, QStringLiteral("render title"), titleOk ? QString() : err);
    {
        DesignDocument doc;
        doc.canvasSize = QSize(1280, 720);
        DesignLayer text;
        text.id = QUuid::createUuid();
        text.type = LayerType::Text;
        text.text = QStringLiteral("SELF TEST");
        text.fontSize = 120;
        text.position = QPointF(100, 250);
        text.glow = true;
        text.strokeWidth = 4;
        text.blend = BlendMode::Screen;
        doc.layers.append(text);
        DesignRenderer dr;
        const QImage img = dr.renderToImage(doc);
        const bool ok = !img.isNull() && img.save(outDir + QStringLiteral("/design.png"));
        r.add(ok, QStringLiteral("render design"));
    }

    // 6. Export: two tracks, a transition, a title overlay, looks, audio FX.
    {
        ProjectData d = ProjectData::createDefault();
        for (auto m : probed)
            d.media.append(m);
        d.media.append(title);
        const QUuid a = timeline::placeMedia(d, probed[0].id, 0, 0, 0, 3 * kUsPerSecond);
        const QUuid b = timeline::placeMedia(d, probed[1].id, 0, 3 * kUsPerSecond, 0, 3 * kUsPerSecond);
        d.clipById(b)->transition = {QStringLiteral("fade"), kUsPerSecond};
        d.clipById(a)->color.temperature = 0.4;
        d.clipById(a)->motion = Motion::ZoomIn;
        timeline::placeMedia(d, title.id, 1, kUsPerSecond, 0, 3 * kUsPerSecond);
        const int a1 = d.audioTrackIndices().value(0);
        d.tracks[a1].fx.reverbEnabled = true;
        d.tracks[a1].fx.pitchSemitones = 2;
        d.tracks[a1].fx.limiterEnabled = true;
        ExportPreset preset = builtInExportPresets().first();
        preset.width = 640;
        preset.height = 360;
        preset.speedPreset = QStringLiteral("ultrafast");
        const QString out = outDir + QStringLiteral("/export.mp4");
        Exporter ex;
        bool done = false, ok = false;
        QString msg;
        QObject::connect(&ex, &Exporter::finished, &ex, [&](bool success, const QString &m) {
            ok = success;
            msg = m;
            done = true;
        });
        QString startErr;
        if (!ex.start(d, preset, out, &startErr)) {
            r.add(false, QStringLiteral("export video"), startErr);
        } else {
            waitUntil([&] { return done; }, 240000);
            const double dur = fileDuration(ffprobe, out);
            r.add(ok && std::abs(dur - 6.0) < 0.3, QStringLiteral("export video"),
                  ok ? QStringLiteral("%1 s, %2 KB").arg(dur, 0, 'f', 2).arg(QFileInfo(out).size() / 1024)
                     : msg.right(600));
        }
    }

    // 7. Audio devices and microphone permission (informational: CI
    //    machines have no microphone and can't grant access).
    r.info(QStringLiteral("audio inputs: %1, outputs: %2")
               .arg(QMediaDevices::audioInputs().size())
               .arg(QMediaDevices::audioOutputs().size()));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0) && QT_CONFIG(permissions)
    const auto st = qApp->checkPermission(QMicrophonePermission{});
    r.info(QStringLiteral("microphone permission: %1")
               .arg(st == Qt::PermissionStatus::Granted
                        ? QStringLiteral("granted")
                        : (st == Qt::PermissionStatus::Denied ? QStringLiteral("denied")
                                                              : QStringLiteral("not asked yet"))));
#endif

    // 8. The main window builds and shows (all three studios, menus, docks).
    {
        MainWindow w;
        w.resize(1280, 800);
        w.show();
        waitUntil([] { return false; }, 1500);
        const bool ok = w.isVisible() && w.menuBar() && w.menuBar()->actions().size() >= 6;
        r.add(ok, QStringLiteral("main window"));
        w.close();
    }

    QFile::remove(title.path);
    r.info(r.failures == 0 ? QStringLiteral("ALL CHECKS PASSED")
                           : QStringLiteral("%1 CHECK(S) FAILED").arg(r.failures));
    QFile f(outDir + QStringLiteral("/self-test.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(r.lines.join(QLatin1Char('\n')).toUtf8() + '\n');
    return r.failures == 0 ? 0 : 1;
}

} // namespace spotlight
