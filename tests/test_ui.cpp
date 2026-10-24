#include "core/designrender.h"
#include "core/exporter.h"
#include "core/project.h"
#include "core/timelineops.h"
#include "ui/designstudio.h"
#include "ui/mainwindow.h"
#include "ui/mediabin.h"
#include "media/timelineplayer.h"
#include "media/videoview.h"
#include "ui/inspector.h"
#include "ui/edittools.h"
#include "ui/recorddialog.h"
#include <QCheckBox>
#include <QLabel>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QPushButton>
#include <QTabWidget>
#include "ui/programmonitor.h"
#include "ui/theme.h"
#include "ui/timelinewidget.h"
#include "ui/videostudio.h"

#include <QAction>
#include <QDockWidget>
#include <QtTest>

using namespace spotlight;

class TestUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("QT_MEDIA_BACKEND", "ffmpeg"); // same backend the app selects in main()
        Q_INIT_RESOURCE(resources);
        theme::apply(*qApp);
    }

    void designRenderMatchesCanvasSize()
    {
        DesignDocument doc;
        doc.canvasSize = QSize(640, 360);
        doc.background = Qt::red;
        DesignLayer text;
        text.id = QUuid::createUuid();
        text.type = LayerType::Text;
        text.text = "HELLO";
        text.fontSize = 60;
        text.position = QPointF(10, 10);
        doc.layers.append(text);
        DesignRenderer r;
        const QImage img = r.renderToImage(doc);
        QCOMPARE(img.size(), QSize(640, 360));
        QCOMPARE(QColor(img.pixel(639, 359)), QColor(Qt::red));
        QCOMPARE(r.layerAt(doc, QPointF(20, 30)), text.id);
        QVERIFY(r.layerAt(doc, QPointF(630, 350)).isNull());
    }


    // The program monitor composites tracks like the exporter: PIP scale and
    // offset, chroma key and transitions, checked on the rendered frame.
    void previewComposites()
    {
        VideoView view;
        view.setCanvasSize(QSize(320, 180));
        QImage red(320, 180, QImage::Format_RGB32);
        red.fill(Qt::red);
        QImage blue(320, 180, QImage::Format_RGB32);
        blue.fill(Qt::blue);
        QImage green(320, 180, QImage::Format_RGB32);
        green.fill(QColor(0, 255, 0));
        for (int y = 50; y < 130; ++y)
            for (int x = 120; x < 200; ++x)
                green.setPixel(x, y, qRgb(255, 255, 255));

        ViewTrack base;
        base.layer.still = red;
        ViewTrack pip;
        pip.layer.still = green;
        pip.layer.clip.key.enabled = true;
        pip.layer.clip.transform.scale = 0.5;
        pip.layer.clip.transform.x = 0.25;
        view.setTracks({base, pip});
        QImage f = view.currentFrame();
        QCOMPARE(f.size(), QSize(320, 180));
        QCOMPARE(QColor(f.pixel(40, 90)), QColor(Qt::red));
        const QColor w = f.pixel(240, 90);
        QVERIFY2(w.red() > 240 && w.green() > 240 && w.blue() > 240, qPrintable(w.name()));
        const QColor k = f.pixel(190, 60);
        QVERIFY2(k.red() > 240 && k.green() < 20, qPrintable(k.name())); // keyed out: red shows

        ViewTrack t;
        t.layer.still = blue;
        t.outgoing.still = red;
        t.inTransition = true;
        t.transition = "fade";
        t.transitionProgress = 0.5;
        view.setTracks({t});
        const QColor mid = view.currentFrame().pixel(160, 90);
        QVERIFY2(mid.red() > 100 && mid.red() < 160 && mid.blue() > 100 && mid.blue() < 160, qPrintable(mid.name()));
        t.transition = "wipeleft";
        t.transitionProgress = 0.25; // incoming (blue) revealed from the right
        view.setTracks({t});
        f = view.currentFrame();
        QCOMPARE(QColor(f.pixel(20, 90)), QColor(Qt::red));
        QCOMPARE(QColor(f.pixel(300, 90)), QColor(Qt::blue));

        // Looks: black & white makes the red base grey.
        base.layer.clip.color.mono = MonoMode::BlackWhite;
        view.setTracks({base});
        const QColor g = view.currentFrame().pixel(40, 90);
        QVERIFY(std::abs(g.red() - g.green()) < 3 && std::abs(g.green() - g.blue()) < 3);
    }

    void designEffectsRender()
    {
        DesignDocument doc;
        doc.canvasSize = QSize(400, 200);
        doc.background = Qt::white;
        DesignLayer shape;
        shape.id = QUuid::createUuid();
        shape.type = LayerType::Shape;
        shape.shape = ShapeKind::Star;
        shape.shapeSize = QSize(100, 100);
        shape.position = QPointF(20, 50);
        shape.fillColor = Qt::red;
        shape.blend = BlendMode::Multiply;
        shape.dropShadow = true;
        doc.layers.append(shape);
        DesignLayer text;
        text.id = QUuid::createUuid();
        text.type = LayerType::Text;
        text.text = "GLOW";
        text.fontSize = 60;
        text.position = QPointF(200, 60);
        text.glow = true;
        text.glowColor = Qt::blue;
        text.textBox = true;
        doc.layers.append(text);
        DesignLayer drawing;
        drawing.id = QUuid::createUuid();
        drawing.type = LayerType::Drawing;
        drawing.shapeSize = doc.canvasSize;
        drawing.strokes.append({Qt::green, 10, false, {QPointF(0, 195), QPointF(399, 195)}});
        doc.layers.append(drawing);
        DesignRenderer r;
        const QImage img = r.renderToImage(doc);
        QCOMPARE(QColor(img.pixel(70, 100)), QColor(Qt::red)); // star centre: multiply red on white
        QCOMPARE(QColor(img.pixel(200, 195)).green(), 255);   // brush stroke
        QVERIFY(QColor(img.pixel(200, 195)).red() < 10);

        // Background removal + stroke on an image layer.
        QTemporaryDir tmp;
        QImage photo(100, 100, QImage::Format_RGB32);
        photo.fill(QColor(0, 200, 0));
        for (int y = 30; y < 70; ++y)
            for (int x = 30; x < 70; ++x)
                photo.setPixel(x, y, qRgb(200, 20, 20));
        const QString path = tmp.path() + "/p.png";
        QVERIFY(photo.save(path));
        DesignDocument d2;
        d2.canvasSize = QSize(100, 100);
        d2.background = Qt::black;
        DesignLayer img2;
        img2.id = QUuid::createUuid();
        img2.type = LayerType::Image;
        img2.imagePath = path;
        img2.removeBg = true;
        img2.strokeWidth = 4;
        img2.strokeColor = Qt::white;
        d2.layers.append(img2);
        const QImage out = r.renderToImage(d2);
        QCOMPARE(QColor(out.pixel(5, 5)), QColor(Qt::black));    // green backdrop removed
        QCOMPARE(QColor(out.pixel(50, 50)), QColor(200, 20, 20)); // subject kept
        QCOMPARE(QColor(out.pixel(27, 50)), QColor(Qt::white));   // stroke around the cut-out
    }


    // Records a real take from the default input (skipped without one).
    void recordsAudioTakes()
    {
        // Device lists can fill in asynchronously after startup.
        QMediaDevices devices;
        for (int i = 0; i < 30 && QMediaDevices::audioInputs().isEmpty(); ++i)
            QTest::qWait(100);
        if (QMediaDevices::audioInputs().isEmpty())
            QSKIP("no audio input device", "");
        Project project;
        TimelinePlayer player(&project, new VideoView);
        RecordDialog dlg(&project, &player, QUuid());
        for (auto *cb : dlg.findChildren<QCheckBox *>()) {
            if (cb->text().contains("count-in") || cb->text().contains("Play the timeline"))
                cb->setChecked(false);
        }
        QPushButton *rec = nullptr;
        for (auto *b : dlg.findChildren<QPushButton *>()) {
            if (b->text().contains("Record"))
                rec = b;
        }
        QVERIFY(rec && rec->isEnabled());
        dlg.show();
        // Wait until the level meter shows signal from the input.
        auto hasSignal = [&] {
            for (auto *l : dlg.findChildren<QLabel *>()) {
                if (l->text().endsWith(" dB") && !l->text().startsWith(QChar(0x2212)))
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(), 10000);
        for (int take = 1; take <= 2; ++take) {
            rec->click();
            QTest::qWait(1500);
            const QString shots = qEnvironmentVariable("SPOTLIGHT_SCREENSHOT_DIR");
            if (!shots.isEmpty() && take == 2)
                dlg.grab().save(shots + "/record-dialog.png");
            rec->click();
            QTRY_COMPARE_WITH_TIMEOUT(dlg.takesRecorded(), take, 15000);
        }
        // Two takes on two new tracks, each ~1.5 s long, with real audio.
        int clips = 0;
        for (const auto &t : project.data().tracks) {
            if (t.isAudio() && t.name.startsWith("Rec")) {
                QCOMPARE(t.clips.size(), 1);
                QVERIFY(t.voice);
                QVERIFY2(t.clips[0].duration() > kUsPerSecond, qPrintable(QString::number(t.clips[0].duration())));
                ++clips;
            }
        }
        QCOMPARE(clips, 2);
        QVERIFY(project.data().media.last().hasAudio);
        QVERIFY(QFileInfo(project.data().media.last().path).size() > 10000);
        static_cast<QDialog &>(dlg).reject();
    }

    void mainWindowWorkflow()
    {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));

        auto *video = w.findChild<VideoStudio *>();
        auto *design = w.findChild<DesignStudio *>();
        auto *project = w.findChild<Project *>();
        QVERIFY(video && design && project);
        QVERIFY2(QFile::exists(QStringLiteral(":/icons/spotlight.png")), "app icon resource missing");

        // Import a generated clip if ffmpeg is available.
        const QString ffmpeg = Exporter::findFfmpeg();
        QTemporaryDir tmp;
        if (!ffmpeg.isEmpty()) {
            const QString src = tmp.path() + "/clip.mp4";
            QProcess gen;
            gen.start(ffmpeg, {"-y", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc=size=320x240:rate=25:duration=2",
                               "-f", "lavfi", "-i", "sine=duration=2", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                               "-shortest", src});
            QVERIFY(gen.waitForFinished(60000));
            video->mediaBin()->importFiles({src});
            QTRY_COMPARE_WITH_TIMEOUT(project->data().media.size(), 1, 15000);
            QVERIFY(project->data().media[0].duration > 0);
            const QUuid mid = project->data().media[0].id;
            project->edit("Add", [&](ProjectData &d) { timeline::placeMedia(d, mid, 0, 0); });
            QVERIFY(project->data().duration() > 0);

            // FFmpeg-backed tools on the selected clip.
            auto *tools = w.findChild<EditTools *>();
            QVERIFY(tools);
            const QUuid clipId = project->data().tracks[0].clips.first().id;
            video->timeline()->setSelection({clipId});
            video->programMonitor()->player()->seek(kUsPerSecond);
            const TimeUs before = project->data().duration();
            tools->freezeFrame();
            QCOMPARE(project->data().duration(), before + 2 * kUsPerSecond);
            QCOMPARE(project->data().media.last().kind, MediaKind::Image);
            QVERIFY(QFileInfo::exists(project->data().media.last().path));
            video->timeline()->setSelection({clipId});
            const int mediaCount = project->data().media.size();
            tools->reverseClip();
            QCOMPARE(project->data().media.size(), mediaCount + 1);
            QVERIFY(project->data().media.last().name.contains("reversed"));
            QCOMPARE(project->data().clipById(clipId)->mediaId, project->data().media.last().id);
            tools->detectScenes(); // testsrc has no cuts: nothing changes, no crash
            project->undoStack()->undo(); // reverse
            project->undoStack()->undo(); // freeze
            QCOMPARE(project->data().duration(), before);
        }

        // Design studio: text layer + undo.
        w.setWorkspace(MainWindow::Design);
        design->addText();
        QCOMPARE(project->data().design.layers.size(), 1);
        project->undoStack()->undo();
        QCOMPARE(project->data().design.layers.size(), 0);
        project->undoStack()->redo();

        // Frame capture lands in the design as a bottom layer.
        QImage frame(320, 180, QImage::Format_RGB32);
        frame.fill(Qt::blue);
        QMetaObject::invokeMethod(video, "captureFrame", Q_ARG(QImage, frame));
        QCOMPARE(project->data().design.layers.size(), 2);
        QCOMPARE(project->data().design.layers.first().type, LayerType::Image);

        // Templates keep image layers and add their own text/shape layers.
        design->applyTemplate(0);
        QCOMPARE(project->data().design.layers.first().type, LayerType::Image);
        QVERIFY(project->data().design.layers.size() >= 4);
        int images = 0;
        for (const auto &l : project->data().design.layers)
            images += l.type == LayerType::Image ? 1 : 0;
        QCOMPARE(images, 1);

        // The finished design can be placed on the timeline as a graphic.
        const int mediaBefore = project->data().media.size();
        emit design->placeOnTimelineRequested();
        QCOMPARE(project->data().media.size(), mediaBefore + 1);
        QVERIFY(project->data().media.last().isStill());
        QVERIFY(QFileInfo::exists(project->data().media.last().path));

        // Menu actions by their shortcut-registry ids.
        auto trigger = [&](const QString &text) {
            for (QAction *a : w.findChildren<QAction *>()) {
                if (a->text().remove('&') == text) {
                    a->trigger();
                    return true;
                }
            }
            return false;
        };
        w.setWorkspace(MainWindow::Video);
        QVERIFY(trigger("Add / Remove Marker"));
        QCOMPARE(project->data().markers.size(), 1);
        QVERIFY(trigger("Show Guides"));

        // Copy / paste / duplicate the selected clip(s) on the timeline.
        auto *timelineWidget = video->timeline();
        const int clipsBefore = project->data().tracks[1].clips.size() + project->data().tracks[0].clips.size();
        QVERIFY(!timelineWidget->selection().isEmpty()); // the placed design is selected
        QVERIFY(trigger("Duplicate"));
        const int clipsAfter = project->data().tracks[1].clips.size() + project->data().tracks[0].clips.size();
        QCOMPARE(clipsAfter, clipsBefore + 1);
        QVERIFY(trigger("Copy"));
        // Paste is an overwrite edit at the playhead; paste into empty space.
        video->programMonitor()->player()->seek(project->data().duration());
        QVERIFY(trigger("Paste"));
        QVERIFY(project->data().tracks[1].clips.size() + project->data().tracks[0].clips.size() > clipsAfter);

        // Pro tools are reachable from the menus.
        for (const QString &name : {QStringLiteral("Freeze Frame"), QStringLiteral("Reverse Clip"),
                                    QStringLiteral("Remove Silences (Jump Cuts)..."),
                                    QStringLiteral("Scene Edit Detection"), QStringLiteral("Add Beat Markers..."),
                                    QStringLiteral("Record Audio..."), QStringLiteral("Add Title..."),
                                    QStringLiteral("Import Captions (SRT)..."), QStringLiteral("Close Gaps on Track"),
                                    QStringLiteral("Insert Source Clip at Playhead")}) {
            bool found = false;
            for (QAction *a : w.findChildren<QAction *>())
                found |= a->text().remove('&') == name;
            QVERIFY2(found, qPrintable(name));
        }
        // The inspector shows Video / Color / Effects tabs for a video clip.
        auto *inspector = video->inspector();
        for (const auto &t : project->data().tracks) {
            if (t.isVideo() && !t.clips.isEmpty()) {
                inspector->setClip(t.clips.first().id);
                break;
            }
        }
        auto *tabs = inspector->findChild<QTabWidget *>();
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 3);

        const QString shots = qEnvironmentVariable("SPOTLIGHT_SCREENSHOT_DIR");
        for (int i = 0; i < 3; ++i) {
            w.setWorkspace(i);
            QTest::qWait(300);
            if (!shots.isEmpty())
                w.grab().save(QStringLiteral("%1/workspace%2.png").arg(shots).arg(i));
        }
        // Every dock of the visible studio must be laid out inside the window.
        for (auto *d : design->findChildren<QDockWidget *>())
            QVERIFY2(design->rect().intersects(d->geometry()), qPrintable(d->objectName()));
        QVERIFY(project->isModified());
        project->markSaved(); // avoid the save prompt on close
    }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
