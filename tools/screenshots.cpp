// Builds a realistic demo project from generated media and saves screenshots
// of each studio for the README and website.
//
//   xvfb-run -s "-screen 0 1920x1200x24" build/tools/spotlight-screenshots docs/screenshots
//
// Needs ffmpeg on PATH (used to synthesise the demo footage and audio).

#include "core/designrender.h"
#include "core/exporter.h"
#include "core/project.h"
#include "core/looks.h"
#include "core/timelineops.h"
#include "core/titles.h"
#include "media/timelineplayer.h"
#include "media/videoview.h"
#include "ui/audiostudio.h"
#include "ui/designcanvas.h"
#include "ui/designstudio.h"
#include "ui/inspector.h"
#include "ui/mainwindow.h"
#include "ui/mediabin.h"
#include "ui/programmonitor.h"
#include "ui/sourcemonitor.h"
#include "ui/theme.h"
#include "ui/timelinewidget.h"
#include "ui/videostudio.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QStatusBar>
#include <QTabWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <functional>

using namespace spotlight;

namespace {

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

bool waitFor(const std::function<bool()> &cond, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > timeoutMs)
            return false;
        pump(50);
    }
    return true;
}

bool ffmpeg(const QStringList &args)
{
    QProcess p;
    p.start(Exporter::findFfmpeg(), QStringList{"-y", "-hide_banner", "-loglevel", "error"} + args);
    p.waitForFinished(300000);
    if (p.exitCode() != 0)
        qWarning("ffmpeg failed: %s", p.readAllStandardError().constData());
    return p.exitCode() == 0;
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    QApplication::setOrganizationName(QStringLiteral("SpotlightScreenshots"));
    QApplication::setApplicationName(QStringLiteral("SPOTLIGHT"));
    QStandardPaths::setTestModeEnabled(true); // never touch real settings
    QApplication app(argc, argv);
    Q_INIT_RESOURCE(resources);
    theme::apply(app);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/spotlight.png")));

    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("screenshots");
    QDir().mkpath(outDir);
    if (Exporter::findFfmpeg().isEmpty()) {
        qWarning("ffmpeg not found");
        return 1;
    }

    // ---- Demo media ------------------------------------------------------
    QTemporaryDir tmp;
    const QString media = tmp.path() + QStringLiteral("/Footage");
    QDir().mkpath(media);
    const QString sunset = media + "/sunset-drive.mp4";
    const QString fractal = media + "/fractal-zoom.mp4";
    const QString city = media + "/city-lights.mp4";
    const QString music = media + "/lofi-beat.wav";
    const QString voice = media + "/voiceover.wav";
    const QString logo = media + "/channel-logo.png";
    const QString whoosh = media + "/whoosh.wav";
    const QString ambience = media + "/room-tone.wav";
    const bool ok = ffmpeg({"-f", "lavfi", "-i",
                            "gradients=s=1280x720:r=30:d=8:speed=0.015:c0=0xff6a00:c1=0xee0979:c2=0x2b1055:c3=0xffc400:"
                            "nb_colors=4:type=linear",
                            "-c:v", "libx264", "-pix_fmt", "yuv420p", sunset})
        && ffmpeg({"-f", "lavfi", "-i", "mandelbrot=s=1280x720:rate=30:start_scale=2.5:end_scale=0.4:inner=mincol", "-t",
                   "8", "-c:v", "libx264", "-pix_fmt", "yuv420p", fractal})
        && ffmpeg({"-f", "lavfi", "-i",
                   "life=s=320x180:mold=10:r=30:ratio=0.45:death_color=#1a0b3a:life_color=#ffc400,"
                   "scale=1280:720:flags=neighbor",
                   "-t", "8", "-c:v", "libx264", "-pix_fmt", "yuv420p", city})
        && ffmpeg({"-f", "lavfi", "-i",
                   "aevalsrc='0.35*sin(2*PI*110*t)*(0.6+0.4*sin(2*PI*2*t))+0.25*sin(2*PI*165*t)*abs(sin(2*PI*0.5*t))"
                   "+0.2*sin(2*PI*220*t)*exp(-6*mod(t,0.5))':s=48000:d=26",
                   "-ac", "2", music})
        && ffmpeg({"-f", "lavfi", "-i",
                   "aevalsrc='0.7*sin(2*PI*180*t+3*sin(2*PI*5*t))*abs(sin(2*PI*1.3*t))*abs(sin(2*PI*0.21*t))':"
                   "s=48000:d=14",
                   "-ac", "2", voice})
        && ffmpeg({"-f", "lavfi", "-i", "color=c=0x141417:s=512x512", "-frames:v", "1", logo})
        && ffmpeg({"-f", "lavfi", "-i", "anoisesrc=d=1.6:c=pink:a=0.9", "-af",
                   "afade=t=in:d=0.9:curve=exp,afade=t=out:st=0.9:d=0.7,highpass=f=400", "-ac", "2", whoosh})
        && ffmpeg({"-f", "lavfi", "-i", "anoisesrc=d=22:c=brown:a=0.25", "-af",
                   "volume='0.6+0.4*sin(2*PI*0.15*t)':eval=frame,lowpass=f=900", "-ac", "2", ambience});
    if (!ok)
        return 1;

    // ---- Window + project -------------------------------------------------
    MainWindow w;
    w.resize(1680, 1000);
    w.show();
    pump(300);
    auto *project = w.findChild<Project *>();
    auto *video = w.findChild<VideoStudio *>();
    auto *audio = w.findChild<AudioStudio *>();
    auto *design = w.findChild<DesignStudio *>();
    TimelinePlayer *player = video->programMonitor()->player();

    video->mediaBin()->importFiles({sunset, fractal, city, music, voice, whoosh, ambience});
    if (!waitFor([&] { return project->data().media.size() == 7; }, 60000)) {
        qWarning("import timed out");
        return 1;
    }
    pump(1500); // thumbnails

    auto mediaId = [&](const QString &path) {
        for (const auto &m : project->data().media) {
            if (m.path == QFileInfo(path).absoluteFilePath())
                return m.id;
        }
        return QUuid();
    };
    // Titles are rendered images; make them before the edit.
    const QSize frame(1920, 1080);
    MediaItem lowerThird, subscribe;
    titles::renderTitleMedia(titles::makeTitle(1, frame, QStringLiteral("Alex Rivera"),
                                               QStringLiteral("Creator · made with SPOTLIGHT!")),
                             QStringLiteral("Title – Alex Rivera"), &lowerThird, nullptr);
    titles::renderTitleMedia(titles::makeTitle(3, frame, QStringLiteral("SUBSCRIBE")), QStringLiteral("Title – SUBSCRIBE"),
                             &subscribe, nullptr);
    project->edit("Build demo timeline", [&](ProjectData &d) {
        d.name = QStringLiteral("My Channel Trailer");
        d.media.append(lowerThird);
        d.media.append(subscribe);
        const QUuid v1a = timeline::placeMedia(d, mediaId(sunset), 0, 0, 0, 7 * kUsPerSecond);
        const QUuid v1b = timeline::placeMedia(d, mediaId(fractal), 0, 7 * kUsPerSecond);
        const QUuid v1c = timeline::placeMedia(d, mediaId(city), 0, 15 * kUsPerSecond, kUsPerSecond, 8 * kUsPerSecond);
        d.clipById(v1a)->fadeIn = kUsPerSecond;
        d.clipById(v1a)->motion = Motion::ZoomIn;
        d.clipById(v1b)->color = lookPresets()[4].adjust; // Cinematic
        d.clipById(v1b)->color.saturation = 1.25;
        d.clipById(v1b)->transition = {QStringLiteral("fade"), kUsPerSecond};
        d.clipById(v1c)->transition = {QStringLiteral("circleopen"), kUsPerSecond};
        d.clipById(v1c)->color = lookPresets()[2].adjust; // Cool
        // Picture-in-picture on V2, titles above it.
        const QUuid pip = timeline::placeMedia(d, mediaId(city), 1, 9 * kUsPerSecond, 0, 5 * kUsPerSecond);
        Clip *pc = d.clipById(pip);
        pc->transform.scale = 0.3;
        pc->transform.x = 0.31;
        pc->transform.y = -0.28;
        pc->fadeIn = pc->fadeOut = 300 * kUsPerSecond / 1000;
        pc->fx.border = 0.012;
        pc->name = QStringLiteral("Facecam");
        pc->colorLabel = 7;
        d.clipById(v1a)->colorLabel = 2;
        d.clipById(v1c)->colorLabel = 5;
        const int v3 = d.addTrack(TrackType::Video, QStringLiteral("V3"));
        const QUuid lt = timeline::placeMedia(d, lowerThird.id, v3, 8 * kUsPerSecond, 0, 6 * kUsPerSecond);
        d.clipById(lt)->fadeIn = d.clipById(lt)->fadeOut = 300 * kUsPerSecond / 1000;
        const QUuid sub = timeline::placeMedia(d, subscribe.id, v3, 17 * kUsPerSecond, 0, 4 * kUsPerSecond);
        d.clipById(sub)->fadeIn = 300 * kUsPerSecond / 1000;
        const int a1 = d.audioTrackIndices().value(0);
        const int a2 = d.audioTrackIndices().value(1);
        const QUuid vo = timeline::placeMedia(d, mediaId(voice), a1, 2 * kUsPerSecond);
        d.clipById(vo)->gain = 1.2;
        const QUuid bed = timeline::placeMedia(d, mediaId(music), a2, 0, 0, 22 * kUsPerSecond);
        d.clipById(bed)->fadeIn = 2 * kUsPerSecond;
        d.clipById(bed)->fadeOut = 3 * kUsPerSecond;
        d.clipById(bed)->gain = 0.6;
        const int a3 = d.addTrack(TrackType::Audio, QStringLiteral("SFX"));
        const int a4 = d.addTrack(TrackType::Audio, QStringLiteral("Ambience"));
        for (TimeUs t : {TimeUs(6300000), TimeUs(14300000)})
            timeline::placeMedia(d, mediaId(whoosh), a3, t);
        const QUuid amb = timeline::placeMedia(d, mediaId(ambience), a4, 0, 0, 22 * kUsPerSecond);
        d.clipById(amb)->fadeIn = kUsPerSecond;
        d.clipById(amb)->fadeOut = 2 * kUsPerSecond;
        d.tracks[a4].volume = 0.45;
        d.tracks[a3].pan = -0.3;
        d.tracks[a1].name = QStringLiteral("Voice");
        d.tracks[a2].name = QStringLiteral("Music");
        d.tracks[a1].fx.dialogueCleanup = true;
        d.tracks[a1].fx.eqEnabled = true;
        d.tracks[a1].fx.lowGainDb = -3;
        d.tracks[a1].fx.midGainDb = 2;
        d.tracks[a1].fx.highGainDb = 4;
        d.tracks[a1].fx.compressorEnabled = true;
        d.tracks[a1].fx.thresholdDb = -20;
        d.tracks[a1].fx.ratio = 4;
        d.tracks[a2].volume = 0.7;
        d.tracks[a2].pan = 0.1;
        d.tracks[a1].voice = true;
        d.tracks[a1].fx.gateEnabled = true;
        d.tracks[a1].fx.deEsser = true;
        d.tracks[a1].fx.limiterEnabled = true;
        d.tracks[a2].fx.duckUnderVoice = true;
        d.tracks[a2].fx.duckAmountDb = -14;
        timeline::toggleMarker(d, 0, 0, QStringLiteral("Intro"));
        timeline::toggleMarker(d, 7 * kUsPerSecond, 0, QStringLiteral("The Build"));
        timeline::toggleMarker(d, 15 * kUsPerSecond, 0, QStringLiteral("Outro"));
        d.inPoint = 7 * kUsPerSecond;
        d.outPoint = 15 * kUsPerSecond;
    });

    // ---- Design: capture a frame, apply a template, write our own copy ----
    player->seek(10 * kUsPerSecond + 200000);
    waitFor([&] { return !video->programMonitor()->view()->currentFrame().isNull(); }, 15000);
    pump(1500);
    QMetaObject::invokeMethod(video, "captureFrame", Q_ARG(QImage, video->programMonitor()->view()->currentFrame()));
    design->applyTemplate(0);
    project->edit("Thumbnail copy", [&](ProjectData &d) {
        for (auto &l : d.design.layers) {
            if (l.type == LayerType::Text && l.text == QLatin1String("NO WAY!")) {
                l.text = l.name = QStringLiteral("I BUILT IT!");
                l.glow = true;
                l.glowColor = QColor(255, 196, 0, 200);
                l.glowSize = 22;
            }
            else if (l.type == LayerType::Text && l.text.startsWith(QLatin1String("I tried")))
                l.text = l.name = QStringLiteral("one app for video, audio & thumbnails");
        }
    });
    // Re-anchor the edited text to the right edge after its width changed.
    project->edit("Align", [&](ProjectData &d) {
        DesignRenderer r;
        for (auto &l : d.design.layers) {
            if (l.type == LayerType::Text && l.name != QLatin1String("NEW")) {
                const QRectF lr = r.localRect(l);
                if (l.position.x() > d.design.canvasSize.width() * 0.3)
                    l.position.setX(d.design.canvasSize.width() - 48 - lr.width());
            }
        }
    });
    QDir().mkpath(outDir);
    design->renderImage().save(outDir + "/thumbnail.png");

    // ---- Video Studio ------------------------------------------------------
    w.setWorkspace(MainWindow::Video);
    if (const MediaItem *src = project->data().mediaById(mediaId(sunset)))
        video->sourceMonitor()->load(*src);
    video->timeline()->zoomToFit();
    player->seek(11 * kUsPerSecond + 300000);
    pump(3000);
    for (const auto &c : project->data().tracks[0].clips) {
        if (c.start == 7 * kUsPerSecond)
            video->timeline()->setSelection(timeline::groupOf(project->data(), c.id));
    }
    if (auto *tabs = video->inspector()->findChild<QTabWidget *>())
        tabs->setCurrentIndex(1); // Color: looks
    pump(1200);
    w.statusBar()->clearMessage();
    w.grab().save(outDir + "/video-studio.png");

    // ---- Audio Studio ------------------------------------------------------
    w.setWorkspace(MainWindow::Audio);
    audio->timeline()->zoomToFit();
    if (auto *fx = audio->findChild<TrackFxPanel *>()) {
        for (const auto &t : project->data().tracks) {
            if (t.voice)
                fx->setTrack(t.id);
        }
    }
    pump(2500); // waveforms
    audio->timeline()->setSelection({});
    pump(500);
    w.statusBar()->clearMessage();
    w.grab().save(outDir + "/audio-studio.png");

    // ---- Design Studio -----------------------------------------------------
    w.setWorkspace(MainWindow::Design);
    design->setGuidesVisible(false);
    pump(300);
    auto *canvas = design->findChild<DesignCanvas *>();
    for (const auto &l : project->data().design.layers) {
        if (l.type == LayerType::Text && l.text.startsWith(QLatin1String("I BUILT")))
            canvas->setSelectedLayer(l.id);
    }
    pump(1200);
    w.statusBar()->clearMessage();
    w.grab().save(outDir + "/design-studio.png");

    QFile::remove(lowerThird.path);
    QFile::remove(subscribe.path);
    project->markSaved();
    qInfo("Screenshots written to %s", qPrintable(QDir(outDir).absolutePath()));
    return 0;
}
