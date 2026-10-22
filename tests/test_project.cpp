#include "core/autosave.h"
#include "core/project.h"
#include "core/timelineops.h"
#include "testutil.h"

#include <QtTest>

using namespace spotlight;
using namespace testutil;

class TestProject : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void jsonRoundTrip()
    {
        ProjectData d = ProjectData::createDefault();
        d.name = "Roundtrip";
        d.sequence.frameRate = 60;
        const QUuid m = addVideo(d, 8 * S);
        const QUuid id = timeline::placeMedia(d, m, 0, S);
        d.clipById(id)->color.saturation = 1.5;
        d.clipById(id)->fadeIn = S / 2;
        d.tracks[2].fx.eqEnabled = true;
        d.tracks[2].fx.lowGainDb = 4;
        d.tracks[2].pan = -0.25;

        DesignLayer text;
        text.id = QUuid::createUuid();
        text.type = LayerType::Text;
        text.text = "WOW!";
        text.textColor = QColor(255, 0, 0, 200);
        d.design.layers.append(text);

        DesignLayer shape;
        shape.id = QUuid::createUuid();
        shape.type = LayerType::Shape;
        shape.gradient = true;
        shape.gradientAngle = 45;
        d.design.layers.append(shape);
        timeline::toggleMarker(d, 3 * S, 0, "Intro");

        ProjectData back;
        QString err;
        QVERIFY2(ProjectData::fromJson(d.toJson(), &back, &err), qPrintable(err));
        QCOMPARE(back.markers.size(), 1);
        QCOMPARE(back.markers[0].name, QStringLiteral("Intro"));
        QVERIFY(back.design.layers[1].gradient);
        QCOMPARE(back.design.layers[1].gradientAngle, 45.0);
        QCOMPARE(back.toJson(), d.toJson());
        QCOMPARE(back.design.layers[0].textColor, QColor(255, 0, 0, 200));
        QCOMPARE(back.clipById(id)->color.saturation, 1.5);
    }

    void rejectsForeignFiles()
    {
        ProjectData out;
        QString err;
        QVERIFY(!ProjectData::fromJson(QJsonObject{{"hello", 1}}, &out, &err));
        QVERIFY(!err.isEmpty());
        QJsonObject future{{"format", "spotlight-project"}, {"version", 999}};
        QVERIFY(!ProjectData::fromJson(future, &out, &err));
    }

    void saveLoadAndRelink()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QDir(tmp.path()).mkpath("proj/media");
        const QString mediaPath = tmp.path() + "/proj/media/a.mp4";
        QFile f(mediaPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();

        ProjectData d = ProjectData::createDefault();
        addVideo(d, 3 * S, true, mediaPath);
        const QString file = tmp.path() + "/proj/test.spotlight";
        QString err;
        QVERIFY2(saveProjectFile(d, file, &err), qPrintable(err));

        // Move the whole folder; media should be found via its relative path.
        QVERIFY(QDir(tmp.path()).rename("proj", "moved"));
        ProjectData loaded;
        QVERIFY2(loadProjectFile(tmp.path() + "/moved/test.spotlight", &loaded, &err), qPrintable(err));
        QCOMPARE(loaded.media[0].path, tmp.path() + "/moved/media/a.mp4");
    }

    void undoRedo()
    {
        Project p;
        QSignalSpy changed(&p, &Project::changed);
        QVERIFY(!p.isModified());
        QUuid media;
        QVERIFY(p.edit("Import", [&](ProjectData &d) { media = addVideo(d, 4 * S); }));
        QVERIFY(p.edit("Place", [&](ProjectData &d) { timeline::placeMedia(d, media, 0, 0); }));
        QVERIFY(p.isModified());
        QCOMPARE(p.data().duration(), 4 * S);
        QVERIFY(!p.edit("No-op", [](ProjectData &) {}));

        p.undoStack()->undo();
        QCOMPARE(p.data().duration(), 0);
        QCOMPARE(p.data().media.size(), 1);
        p.undoStack()->redo();
        QCOMPARE(p.data().duration(), 4 * S);
        QVERIFY(changed.count() >= 4);
    }

    void saveMarksClean()
    {
        QTemporaryDir tmp;
        Project p;
        p.edit("Rename", [](ProjectData &d) { d.sequence.width = 1280; });
        QVERIFY(p.isModified());
        QString err;
        QVERIFY(p.saveAs(tmp.path() + "/My Video.spotlight", &err));
        QVERIFY(!p.isModified());
        QCOMPARE(p.displayName(), QStringLiteral("My Video"));

        Project q;
        QVERIFY(q.open(p.filePath(), &err));
        QCOMPARE(q.data().sequence.width, 1280);
        QVERIFY(!q.isModified());
    }

    void autosaveAndRecovery()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.path() + "/autosave";
        QUuid projectId;
        {
            Project p;
            AutosaveManager as(&p, dir);
            QVERIFY(!as.autosaveNow()); // nothing to save yet
            p.edit("Change", [](ProjectData &d) { d.sequence.frameRate = 24; });
            QVERIFY(as.autosaveNow());
            QVERIFY(!as.autosaveNow()); // unchanged since last autosave
            projectId = p.data().id;
            // Simulate a crash: the manager goes away without discarding.
        }

        Project fresh;
        AutosaveManager as(&fresh, dir);
        const QVector<RecoveryEntry> entries = as.findRecoverable();
        QCOMPARE(entries.size(), 1);
        ProjectData recovered;
        QString err;
        QVERIFY(loadProjectFile(entries[0].autosavePath, &recovered, &err));
        QCOMPARE(recovered.id, projectId);
        QCOMPARE(recovered.sequence.frameRate, 24.0);

        AutosaveManager::discard(entries[0]);
        QVERIFY(as.findRecoverable().isEmpty());
    }

    void savingDiscardsAutosave()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.path() + "/autosave";
        Project p;
        AutosaveManager as(&p, dir);
        p.edit("Change", [](ProjectData &d) { d.sequence.frameRate = 25; });
        QVERIFY(as.autosaveNow());
        QCOMPARE(as.findRecoverable().size(), 1);
        QString err;
        QVERIFY(p.saveAs(tmp.path() + "/x.spotlight", &err));
        QVERIFY(as.findRecoverable().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestProject)
#include "test_project.moc"
