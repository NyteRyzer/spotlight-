#include "core/designrender.h"
#include "core/exporter.h"
#include "core/project.h"
#include "core/timelineops.h"
#include "ui/designstudio.h"
#include "ui/mainwindow.h"
#include "ui/mediabin.h"
#include "ui/theme.h"
#include "ui/videostudio.h"

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

    void mainWindowWorkflow()
    {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));

        auto *video = w.findChild<VideoStudio *>();
        auto *design = w.findChild<DesignStudio *>();
        auto *project = w.findChild<Project *>();
        QVERIFY(video && design && project);

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
