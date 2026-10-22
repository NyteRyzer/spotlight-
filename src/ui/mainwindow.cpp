#include "ui/mainwindow.h"

#include "core/autosave.h"
#include "core/project.h"
#include "core/timelineops.h"
#include "media/timelineplayer.h"
#include "media/videoview.h"
#include "ui/actions.h"
#include "ui/audiostudio.h"
#include "ui/designstudio.h"
#include "ui/exportdialog.h"
#include "ui/mediabin.h"
#include "ui/programmonitor.h"
#include "ui/recoverydialog.h"
#include "ui/settingsdialog.h"
#include "ui/shortcutsdialog.h"
#include "ui/timelinewidget.h"
#include "ui/videostudio.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QLineEdit>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>

namespace spotlight {

namespace {
const QString kProjectFilter = QStringLiteral("SPOTLIGHT! Project (*.spotlight)");
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_project(new Project(this))
    , m_autosave(new AutosaveManager(m_project, AutosaveManager::defaultDirectory(), this))
    , m_tabs(new QTabBar(this))
    , m_stack(new QStackedWidget(this))
    , m_autosaveLabel(new QLabel(this))
{
    setObjectName(QStringLiteral("SpotlightMainWindow"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/spotlight.png")));

    m_video = new VideoStudio(m_project, m_stack);
    m_audio = new AudioStudio(m_project, m_video->programMonitor()->player(), m_video->mediaBin(), m_stack);
    m_design = new DesignStudio(m_project, m_stack);
    m_stack->addWidget(m_video);
    m_stack->addWidget(m_audio);
    m_stack->addWidget(m_design);

    // Top bar: logo + workspace switcher.
    auto *central = new QWidget(this);
    auto *cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    auto *top = new QWidget(central);
    top->setObjectName(QStringLiteral("topBar"));
    auto *tl = new QHBoxLayout(top);
    tl->setContentsMargins(0, 0, 12, 0);
    auto *logo = new QLabel(QStringLiteral("SPOTLIGHT!"), top);
    logo->setObjectName(QStringLiteral("logo"));
    tl->addWidget(logo);
    m_tabs->setObjectName(QStringLiteral("workspaceTabs"));
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->addTab(tr("Video Studio"));
    m_tabs->addTab(tr("Audio Studio"));
    m_tabs->addTab(tr("Design Studio"));
    tl->addWidget(m_tabs);
    tl->addStretch();
    cl->addWidget(top);
    cl->addWidget(m_stack, 1);
    setCentralWidget(central);
    connect(m_tabs, &QTabBar::currentChanged, m_stack, &QStackedWidget::setCurrentIndex);

    statusBar()->addPermanentWidget(m_autosaveLabel);
    connect(m_video, &VideoStudio::status, this, [this](const QString &m) { statusBar()->showMessage(m, 6000); });
    connect(m_design, &DesignStudio::status, this, [this](const QString &m) { statusBar()->showMessage(m, 6000); });
    connect(m_video, &VideoStudio::captureFrame, this, &MainWindow::captureFrame);
    connect(m_design, &DesignStudio::requestVideoFrame, this, [this] {
        const QImage f = m_video->programMonitor()->view()->currentFrame();
        if (f.isNull())
            statusBar()->showMessage(tr("Nothing in the Program Monitor to capture — move the playhead over a clip."),
                                     6000);
        else
            captureFrame(f);
    });
    connect(m_autosave, &AutosaveManager::autosaved, this, [this] {
        m_autosaveLabel->setText(tr("Autosaved %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm"))));
    });
    connect(m_project, &Project::modifiedChanged, this, &MainWindow::updateTitle);
    connect(m_project, &Project::filePathChanged, this, &MainWindow::updateTitle);

    QSettings s;
    m_autosave->setEnabled(s.value(QStringLiteral("autosave/enabled"), true).toBool());
    m_autosave->setIntervalSeconds(s.value(QStringLiteral("autosave/interval"), 120).toInt());

    buildMenus();
    for (int i = 0; i < 3; ++i)
        m_defaultLayouts[i] = studio(i)->saveState();
    restoreLayouts();
    updateTitle();
}

QMainWindow *MainWindow::studio(int index) const
{
    switch (index) {
    case Audio: return m_audio;
    case Design: return m_design;
    default: return m_video;
    }
}

TimelineWidget *MainWindow::activeTimeline() const
{
    return m_stack->currentIndex() == Audio ? m_audio->timeline() : m_video->timeline();
}

void MainWindow::setWorkspace(int index)
{
    m_tabs->setCurrentIndex(index);
}

void MainWindow::buildMenus()
{
    TimelinePlayer *player = m_video->programMonitor()->player();
    const QString F = tr("File"), E = tr("Edit"), T = tr("Timeline"), W = tr("Window");

    // ---- File ----
    QMenu *file = menuBar()->addMenu(tr("&File"));
    auto *newAct = makeAction(this, QStringLiteral("file.new"), F, tr("&New Project"), QKeySequence::New);
    auto *openAct = makeAction(this, QStringLiteral("file.open"), F, tr("&Open Project..."), QKeySequence::Open);
    auto *saveAct = makeAction(this, QStringLiteral("file.save"), F, tr("&Save"), QKeySequence::Save);
    auto *saveAsAct = makeAction(this, QStringLiteral("file.saveas"), F, tr("Save &As..."), QKeySequence::SaveAs);
    auto *importAct = makeAction(this, QStringLiteral("file.import"), F, tr("&Import Media..."), QKeySequence(tr("Ctrl+I")));
    auto *exportAct = makeAction(this, QStringLiteral("file.export"), F, tr("&Export Video/Audio..."), QKeySequence(tr("Ctrl+M")));
    auto *exportImgAct = makeAction(this, QStringLiteral("file.exportimage"), F, tr("Export Design &Image..."),
                                    QKeySequence(tr("Ctrl+Shift+M")));
    auto *settingsAct = makeAction(this, QStringLiteral("file.settings"), F, tr("Se&ttings..."), QKeySequence::Preferences);
    settingsAct->setMenuRole(QAction::PreferencesRole);
    auto *quitAct = makeAction(this, QStringLiteral("file.quit"), F, tr("&Quit"), QKeySequence::Quit);
    quitAct->setMenuRole(QAction::QuitRole);
    file->addAction(newAct);
    file->addAction(openAct);
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    file->addSeparator();
    file->addAction(saveAct);
    file->addAction(saveAsAct);
    file->addSeparator();
    file->addAction(importAct);
    file->addAction(exportAct);
    file->addAction(exportImgAct);
    file->addSeparator();
    file->addAction(settingsAct);
    file->addSeparator();
    file->addAction(quitAct);
    connect(newAct, &QAction::triggered, this, &MainWindow::newProject);
    connect(openAct, &QAction::triggered, this, &MainWindow::openDialog);
    connect(saveAct, &QAction::triggered, this, &MainWindow::save);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::saveAs);
    connect(importAct, &QAction::triggered, m_video->mediaBin(), &MediaBin::importDialog);
    connect(exportAct, &QAction::triggered, this, [this, player] {
        player->pause();
        ExportDialog(m_project, this).exec();
    });
    connect(exportImgAct, &QAction::triggered, m_design, &DesignStudio::exportImage);
    connect(settingsAct, &QAction::triggered, this, [this] { SettingsDialog(m_project, m_autosave, this).exec(); });
    connect(quitAct, &QAction::triggered, this, &QWidget::close);
    rebuildRecentMenu();

    // ---- Edit ----
    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    auto *undo = makeAction(this, QStringLiteral("edit.undo"), E, tr("&Undo"), QKeySequence::Undo);
    auto *redo = makeAction(this, QStringLiteral("edit.redo"), E, tr("&Redo"), QKeySequence::Redo);
    connect(undo, &QAction::triggered, m_project->undoStack(), &QUndoStack::undo);
    connect(redo, &QAction::triggered, m_project->undoStack(), &QUndoStack::redo);
    connect(m_project->undoStack(), &QUndoStack::canUndoChanged, undo, &QAction::setEnabled);
    connect(m_project->undoStack(), &QUndoStack::canRedoChanged, redo, &QAction::setEnabled);
    connect(m_project->undoStack(), &QUndoStack::undoTextChanged, undo,
            [undo](const QString &t) { undo->setText(t.isEmpty() ? tr("&Undo") : tr("&Undo %1").arg(t)); });
    connect(m_project->undoStack(), &QUndoStack::redoTextChanged, redo,
            [redo](const QString &t) { redo->setText(t.isEmpty() ? tr("&Redo") : tr("&Redo %1").arg(t)); });
    undo->setEnabled(false);
    redo->setEnabled(false);
    auto *del = makeAction(this, QStringLiteral("edit.delete"), E, tr("&Delete"), QKeySequence::Delete);
    auto *ripple = makeAction(this, QStringLiteral("edit.rippledelete"), E, tr("Ripple Delete"),
                              QKeySequence(tr("Shift+Del")));
    auto *selAll = makeAction(this, QStringLiteral("edit.selectall"), E, tr("Select &All"), QKeySequence::SelectAll);
    auto *keys = makeAction(this, QStringLiteral("edit.shortcuts"), E, tr("&Keyboard Shortcuts..."),
                            QKeySequence(tr("Ctrl+Alt+K")));
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    edit->addAction(del);
    edit->addAction(ripple);
    edit->addAction(selAll);
    edit->addSeparator();
    edit->addAction(keys);
    connect(del, &QAction::triggered, this, [this] {
        if (m_stack->currentIndex() == Design)
            m_design->deleteSelected();
        else
            activeTimeline()->deleteSelection(false);
    });
    connect(ripple, &QAction::triggered, this, [this] {
        if (m_stack->currentIndex() != Design)
            activeTimeline()->deleteSelection(true);
    });
    connect(selAll, &QAction::triggered, this, [this] {
        if (m_stack->currentIndex() != Design)
            activeTimeline()->selectAll();
    });
    connect(keys, &QAction::triggered, this, [this] { ShortcutsDialog(this).exec(); });

    // ---- Timeline ----
    QMenu *tl = menuBar()->addMenu(tr("&Timeline"));
    auto add = [&](const QString &id, const QString &text, const QKeySequence &key, auto fn) {
        auto *a = makeAction(this, id, T, text, key);
        connect(a, &QAction::triggered, this, fn);
        tl->addAction(a);
        return a;
    };
    add(QStringLiteral("timeline.play"), tr("Play / Pause"), QKeySequence(Qt::Key_Space), [player] { player->togglePlay(); });
    add(QStringLiteral("timeline.start"), tr("Go to Start"), QKeySequence(Qt::Key_Home), [player] { player->seek(0); });
    add(QStringLiteral("timeline.end"), tr("Go to End"), QKeySequence(Qt::Key_End),
        [this, player] { player->seek(m_project->data().duration()); });
    add(QStringLiteral("timeline.prevframe"), tr("Previous Frame"), QKeySequence(Qt::Key_Left),
        [player] { player->stepFrames(-1); });
    add(QStringLiteral("timeline.nextframe"), tr("Next Frame"), QKeySequence(Qt::Key_Right),
        [player] { player->stepFrames(1); });
    tl->addSeparator();
    add(QStringLiteral("timeline.split"), tr("Split at Playhead"), QKeySequence(Qt::Key_S),
        [this] { activeTimeline()->splitAtPlayhead(); });
    add(QStringLiteral("timeline.addclip"), tr("Add Source Clip to Timeline"), QKeySequence(Qt::Key_Period), [this] {
        const QUuid id = m_video->mediaBin()->currentMedia();
        if (id.isNull())
            return;
        TimelinePlayer *p = m_video->programMonitor()->player();
        m_project->edit(tr("Add Clip"), [&](ProjectData &d) { timeline::placeMedia(d, id, -1, p->position()); });
    });
    tl->addSeparator();
    add(QStringLiteral("timeline.zoomin"), tr("Zoom In"), QKeySequence(Qt::Key_Equal), [this] { activeTimeline()->zoomIn(); });
    add(QStringLiteral("timeline.zoomout"), tr("Zoom Out"), QKeySequence(Qt::Key_Minus),
        [this] { activeTimeline()->zoomOut(); });
    add(QStringLiteral("timeline.zoomfit"), tr("Zoom to Fit"), QKeySequence(Qt::Key_Backslash),
        [this] { activeTimeline()->zoomToFit(); });
    auto *snap = makeAction(this, QStringLiteral("timeline.snapping"), T, tr("Snapping"), QKeySequence(Qt::Key_N));
    snap->setCheckable(true);
    snap->setChecked(true);
    connect(snap, &QAction::toggled, this, [this](bool on) {
        m_video->timeline()->setSnapping(on);
        m_audio->timeline()->setSnapping(on);
    });
    tl->addAction(snap);
    tl->addSeparator();
    add(QStringLiteral("timeline.addvideotrack"), tr("Add Video Track"), QKeySequence(), [this] {
        m_project->edit(tr("Add Track"), [](ProjectData &d) { d.addTrack(TrackType::Video); });
    });
    add(QStringLiteral("timeline.addaudiotrack"), tr("Add Audio Track"), QKeySequence(), [this] {
        m_project->edit(tr("Add Track"), [](ProjectData &d) { d.addTrack(TrackType::Audio); });
    });

    // ---- Design ----
    QMenu *design = menuBar()->addMenu(tr("&Design"));
    const QString D = tr("Design");
    auto addD = [&](const QString &id, const QString &text, const QKeySequence &key, auto fn) {
        auto *a = makeAction(this, id, D, text, key);
        connect(a, &QAction::triggered, this, [this, fn] {
            setWorkspace(Design);
            fn();
        });
        design->addAction(a);
    };
    addD(QStringLiteral("design.text"), tr("Add Text"), QKeySequence(tr("Ctrl+T")), [this] { m_design->addText(); });
    addD(QStringLiteral("design.image"), tr("Add Image..."), QKeySequence(), [this] { m_design->addImageDialog(); });
    addD(QStringLiteral("design.rect"), tr("Add Rectangle"), QKeySequence(), [this] { m_design->addShape(ShapeKind::Rectangle); });
    addD(QStringLiteral("design.ellipse"), tr("Add Ellipse"), QKeySequence(), [this] { m_design->addShape(ShapeKind::Ellipse); });
    addD(QStringLiteral("design.duplicate"), tr("Duplicate Layer"), QKeySequence(tr("Ctrl+D")),
         [this] { m_design->duplicateSelected(); });
    addD(QStringLiteral("design.forward"), tr("Bring Forward"), QKeySequence(tr("Ctrl+]")), [this] { m_design->moveSelected(1); });
    addD(QStringLiteral("design.backward"), tr("Send Backward"), QKeySequence(tr("Ctrl+[")),
         [this] { m_design->moveSelected(-1); });
    design->addSeparator();
    auto *capture = makeAction(this, QStringLiteral("design.captureframe"), D, tr("Capture Program Frame to Design"),
                               QKeySequence(tr("Ctrl+Shift+F")));
    connect(capture, &QAction::triggered, m_design, &DesignStudio::requestVideoFrame);
    design->addAction(capture);

    // ---- Window ----
    QMenu *win = menuBar()->addMenu(tr("&Window"));
    const QStringList names{tr("Video Studio"), tr("Audio Studio"), tr("Design Studio")};
    for (int i = 0; i < 3; ++i) {
        auto *a = makeAction(this, QStringLiteral("window.workspace%1").arg(i + 1), W, names[i],
                             QKeySequence(tr("Ctrl+%1").arg(i + 1)));
        connect(a, &QAction::triggered, this, [this, i] { setWorkspace(i); });
        win->addAction(a);
    }
    win->addSeparator();
    auto *saveLayout = makeAction(this, QStringLiteral("window.savelayout"), W, tr("Save Layout As..."));
    connect(saveLayout, &QAction::triggered, this, &MainWindow::saveNamedLayout);
    win->addAction(saveLayout);
    m_layoutMenu = win->addMenu(tr("Load Layout"));
    connect(m_tabs, &QTabBar::currentChanged, this, &MainWindow::rebuildLayoutMenu);
    rebuildLayoutMenu();
    auto *resetLayout = makeAction(this, QStringLiteral("window.resetlayout"), W, tr("Reset Layout"));
    connect(resetLayout, &QAction::triggered, this, [this] {
        const int i = m_stack->currentIndex();
        studio(i)->restoreState(m_defaultLayouts[i]);
        for (auto *dock : studio(i)->findChildren<QDockWidget *>())
            dock->show();
    });
    win->addAction(resetLayout);

    // ---- Help ----
    QMenu *help = menuBar()->addMenu(tr("&Help"));
    auto *about = help->addAction(tr("About SPOTLIGHT!"));
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About SPOTLIGHT!"),
                           tr("<h2>SPOTLIGHT! %1</h2>"
                              "<p>An all-in-one creative studio: edit video, mix audio and design thumbnails in "
                              "one project.</p>"
                              "<p>Built with Qt %2 and FFmpeg.</p>")
                               .arg(QStringLiteral(SPOTLIGHT_VERSION), QString::fromLatin1(qVersion())));
    });
}

void MainWindow::updateTitle()
{
    setWindowTitle(QStringLiteral("%1%2 — SPOTLIGHT!").arg(m_project->displayName(),
                                                           m_project->isModified() ? QStringLiteral(" •") : QString()));
}

bool MainWindow::maybeSave()
{
    if (!m_project->isModified())
        return true;
    const auto r = QMessageBox::warning(this, tr("Unsaved Changes"),
                                        tr("Save changes to \"%1\" before closing?").arg(m_project->displayName()),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel)
        return false;
    if (r == QMessageBox::Save)
        return save();
    m_autosave->discardCurrent();
    return true;
}

bool MainWindow::save()
{
    if (m_project->filePath().isEmpty())
        return saveAs();
    QString err;
    if (!m_project->save(&err)) {
        QMessageBox::critical(this, tr("Save Failed"), err);
        return false;
    }
    statusBar()->showMessage(tr("Saved %1").arg(m_project->filePath()), 4000);
    return true;
}

bool MainWindow::saveAs()
{
    QSettings s;
    const QString dir = s.value(QStringLiteral("paths/project"),
                                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                            .toString();
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project"),
                                                QDir(dir).filePath(m_project->displayName() + QStringLiteral(".spotlight")),
                                                kProjectFilter);
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QLatin1String(".spotlight")))
        path += QStringLiteral(".spotlight");
    QString err;
    if (!m_project->saveAs(path, &err)) {
        QMessageBox::critical(this, tr("Save Failed"), err);
        return false;
    }
    s.setValue(QStringLiteral("paths/project"), QFileInfo(path).absolutePath());
    addRecent(path);
    statusBar()->showMessage(tr("Saved %1").arg(path), 4000);
    return true;
}

void MainWindow::newProject()
{
    if (!maybeSave())
        return;
    m_video->programMonitor()->player()->pause();
    m_project->reset(ProjectData::createDefault());
    m_video->programMonitor()->player()->seek(0);
}

void MainWindow::openDialog()
{
    if (!maybeSave())
        return;
    const QString dir = QSettings().value(QStringLiteral("paths/project"),
                                          QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                            .toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), dir, kProjectFilter);
    if (!path.isEmpty())
        openProject(path);
}

bool MainWindow::openProject(const QString &path)
{
    m_video->programMonitor()->player()->pause();
    QString err;
    if (!m_project->open(path, &err)) {
        QMessageBox::critical(this, tr("Cannot Open Project"), tr("%1\n\n%2").arg(path, err));
        return false;
    }
    QSettings().setValue(QStringLiteral("paths/project"), QFileInfo(path).absolutePath());
    addRecent(path);
    m_video->programMonitor()->player()->seek(0);
    int missing = 0;
    for (const auto &m : m_project->data().media)
        missing += QFileInfo::exists(m.path) ? 0 : 1;
    if (missing > 0)
        statusBar()->showMessage(tr("%n media file(s) could not be found.", nullptr, missing), 10000);
    return true;
}

void MainWindow::addRecent(const QString &path)
{
    QSettings s;
    QStringList list = s.value(QStringLiteral("recentProjects")).toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 10)
        list.removeLast();
    s.setValue(QStringLiteral("recentProjects"), list);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList list = QSettings().value(QStringLiteral("recentProjects")).toStringList();
    for (const QString &p : list) {
        m_recentMenu->addAction(QFileInfo(p).fileName(), this, [this, p] {
            if (maybeSave())
                openProject(p);
        })->setToolTip(p);
    }
    m_recentMenu->setEnabled(!list.isEmpty());
}

void MainWindow::offerRecovery()
{
    const QVector<RecoveryEntry> entries = m_autosave->findRecoverable();
    if (entries.isEmpty())
        return;
    RecoveryDialog dlg(entries, this);
    if (dlg.exec() != QDialog::Accepted || !dlg.chosen())
        return;
    const RecoveryEntry e = *dlg.chosen();
    ProjectData d;
    QString err;
    if (!loadProjectFile(e.autosavePath, &d, &err)) {
        QMessageBox::critical(this, tr("Recovery Failed"), err);
        return;
    }
    m_project->reset(d, e.originalPath);
    // The recovered state differs from what's on disk: keep it flagged and
    // autosaved until the user saves.
    m_project->markModified();
    m_autosave->autosaveNow();
    statusBar()->showMessage(tr("Recovered \"%1\". Save it to keep the changes.").arg(e.projectName), 10000);
}

void MainWindow::captureFrame(const QImage &frame)
{
    // Captures live next to the project when it has been saved, so they travel
    // with it; otherwise in the app data folder.
    QString dir;
    if (!m_project->filePath().isEmpty()) {
        const QFileInfo fi(m_project->filePath());
        dir = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(" Captures");
    } else {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/captures");
    }
    QDir().mkpath(dir);
    const QString name = QStringLiteral("frame_%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    const QString path = dir + QLatin1Char('/') + name;
    if (!frame.save(path)) {
        statusBar()->showMessage(tr("Could not save captured frame to %1").arg(path), 6000);
        return;
    }
    m_design->addImageLayer(path, tr("Video frame"), true);
    setWorkspace(Design);
    statusBar()->showMessage(tr("Frame captured to %1 and added to the design").arg(path), 6000);
}

void MainWindow::saveLayouts()
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/workspace"), m_stack->currentIndex());
    for (int i = 0; i < 3; ++i)
        s.setValue(QStringLiteral("layout/%1").arg(studio(i)->objectName()), studio(i)->saveState());
}

void MainWindow::restoreLayouts()
{
    QSettings s;
    if (!restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray()))
        resize(1500, 920);
    for (int i = 0; i < 3; ++i)
        studio(i)->restoreState(s.value(QStringLiteral("layout/%1").arg(studio(i)->objectName())).toByteArray());
    setWorkspace(s.value(QStringLiteral("window/workspace"), 0).toInt());
}

void MainWindow::saveNamedLayout()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save Layout"),
                                               tr("Name for this %1 layout:").arg(m_tabs->tabText(m_stack->currentIndex())),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    QMainWindow *w = studio(m_stack->currentIndex());
    QSettings s;
    s.beginGroup(QStringLiteral("namedLayouts/%1").arg(w->objectName()));
    s.setValue(name.trimmed(), w->saveState());
    s.endGroup();
    rebuildLayoutMenu();
}

void MainWindow::rebuildLayoutMenu()
{
    if (!m_layoutMenu)
        return;
    m_layoutMenu->clear();
    QMainWindow *w = studio(m_stack->currentIndex());
    QSettings s;
    s.beginGroup(QStringLiteral("namedLayouts/%1").arg(w->objectName()));
    const QStringList names = s.childKeys();
    for (const QString &n : names) {
        const QByteArray state = s.value(n).toByteArray();
        m_layoutMenu->addAction(n, this, [w, state] { w->restoreState(state); });
    }
    s.endGroup();
    m_layoutMenu->setEnabled(!names.isEmpty());
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    m_video->programMonitor()->player()->pause();
    m_autosave->discardCurrent();
    saveLayouts();
    e->accept();
}

} // namespace spotlight
