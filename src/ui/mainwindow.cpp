#include "ui/mainwindow.h"

#include "core/autosave.h"
#include "core/project.h"
#include "core/timelineops.h"
#include "core/titles.h"
#include "media/timelineplayer.h"
#include "media/videoview.h"
#include "ui/actions.h"
#include "ui/audiostudio.h"
#include "ui/designstudio.h"
#include "ui/autoclip.h"
#include "ui/edittools.h"
#include "ui/sourcemonitor.h"
#include "ui/inspector.h"
#include "ui/exportdialog.h"
#include "ui/mediabin.h"
#include "ui/programmonitor.h"
#include "ui/recoverydialog.h"
#include "ui/settingsdialog.h"
#include "ui/shortcutsdialog.h"
#include "ui/timelinewidget.h"
#include "ui/videostudio.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
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
    m_tools = new EditTools(m_project, m_video->programMonitor()->player(), m_video->mediaBin(), this);
    m_tools->setTimelineProvider([this] { return activeTimeline(); });
    connect(m_tools, &EditTools::status, this, [this](const QString &m) { statusBar()->showMessage(m, 8000); });
    m_autoClip = new AutoClipper(m_project, m_video->mediaBin(), this);
    connect(m_autoClip, &AutoClipper::status, this, [this](const QString &m) { statusBar()->showMessage(m, 12000); });
    connect(m_video->mediaBin(), &MediaBin::autoClipRequested, this, &MainWindow::openAutoClip);
    connect(m_video->inspector(), &ClipInspector::freezeFrameRequested, m_tools, &EditTools::freezeFrame);
    connect(m_video->inspector(), &ClipInspector::reverseRequested, m_tools, &EditTools::reverseClip);
    connect(m_video->inspector(), &ClipInspector::editTitleRequested, m_tools, &EditTools::editTitle);
    connect(m_audio->inspector(), &ClipInspector::reverseRequested, m_tools, &EditTools::reverseClip);
    connect(m_audio, &AudioStudio::recordRequested, m_tools, &EditTools::recordVoiceover);
    auto extender = [this](QMenu *menu, const QUuid &clipId) {
        const ProjectData &d = m_project->data();
        int ti = -1;
        const Clip *c = d.clipById(clipId, &ti);
        const MediaItem *m = c ? d.mediaById(c->mediaId) : nullptr;
        if (!c || !m)
            return;
        menu->addSeparator();
        if (d.tracks[ti].isVideo() && !m->isStill())
            menu->addAction(tr("Freeze Frame at Playhead"), m_tools, &EditTools::freezeFrame);
        if (!m->isStill())
            menu->addAction(tr("Reverse"), m_tools, &EditTools::reverseClip);
        if (m->hasAudio)
            menu->addAction(tr("Remove Silences..."), m_tools, &EditTools::removeSilences);
        if (d.tracks[ti].isVideo() && m->hasVideo && !m->isStill())
            menu->addAction(tr("Split at Scene Changes"), m_tools, &EditTools::detectScenes);
        if (m->isTitle())
            menu->addAction(tr("Edit Title Text..."), m_tools, &EditTools::editTitle);
    };
    m_video->timeline()->setClipMenuExtender(extender);
    m_audio->timeline()->setClipMenuExtender(extender);

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
    connect(m_design, &DesignStudio::placeOnTimelineRequested, this, &MainWindow::placeDesignOnTimeline);

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
    // Let focused input controls keep keys that are also global shortcuts.
    qApp->installEventFilter(this);
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
    auto *exportFrameAct = makeAction(this, QStringLiteral("file.exportframe"), F, tr("Export &Frame as Image..."),
                                      QKeySequence(tr("Ctrl+Shift+E")));
    connect(exportFrameAct, &QAction::triggered, this, &MainWindow::exportFrame);
    file->addAction(exportFrameAct);
    file->addSeparator();
    auto *relinkAct = makeAction(this, QStringLiteral("file.relink"), F, tr("Re&link Missing Media..."));
    connect(relinkAct, &QAction::triggered, this, &MainWindow::relinkMedia);
    file->addAction(relinkAct);
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
    auto *copyAct = makeAction(this, QStringLiteral("edit.copy"), E, tr("&Copy"), QKeySequence::Copy);
    auto *pasteAct = makeAction(this, QStringLiteral("edit.paste"), E, tr("&Paste"), QKeySequence::Paste);
    auto *dupAct = makeAction(this, QStringLiteral("edit.duplicate"), E, tr("D&uplicate"), QKeySequence(tr("Ctrl+D")));
    connect(copyAct, &QAction::triggered, this, &MainWindow::copySelection);
    connect(pasteAct, &QAction::triggered, this, &MainWindow::pasteClipboard);
    connect(dupAct, &QAction::triggered, this, &MainWindow::duplicateSelection);
    edit->addAction(copyAct);
    edit->addAction(pasteAct);
    edit->addAction(dupAct);
    edit->addSeparator();
    edit->addAction(keys);
    connect(del, &QAction::triggered, this, [this] {
        // Delete acts on whatever panel has focus: the media bin removes
        // media, otherwise the current studio's selection is deleted.
        QWidget *focus = QApplication::focusWidget();
        if (focus && m_video->mediaBin()->isAncestorOf(focus))
            m_video->mediaBin()->removeSelected();
        else if (m_stack->currentIndex() == Design)
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
    add(QStringLiteral("timeline.insert"), tr("Insert Source Clip at Playhead"), QKeySequence(Qt::Key_Comma),
        [this] { m_tools->insertAtPlayhead(); });
    add(QStringLiteral("timeline.freeze"), tr("Freeze Frame"), QKeySequence(tr("Shift+F")), [this] { m_tools->freezeFrame(); });
    add(QStringLiteral("timeline.reverse"), tr("Reverse Clip"), QKeySequence(), [this] { m_tools->reverseClip(); });
    add(QStringLiteral("timeline.closegaps"), tr("Close Gaps on Track"), QKeySequence(), [this] { m_tools->closeGaps(); });
    tl->addSeparator();
    add(QStringLiteral("timeline.silences"), tr("Remove Silences (Jump Cuts)..."), QKeySequence(),
        [this] { m_tools->removeSilences(); });
    add(QStringLiteral("timeline.scenes"), tr("Scene Edit Detection"), QKeySequence(), [this] { m_tools->detectScenes(); });
    add(QStringLiteral("timeline.autoclip"), tr("Auto-Clip Highlights..."), QKeySequence(tr("Ctrl+Shift+H")), [this] {
        // The selected bin item, else the media of the selected / playhead clip.
        QUuid media = m_video->mediaBin()->currentMedia();
        if (media.isNull()) {
            const auto sel = activeTimeline()->selection();
            if (const Clip *c = sel.isEmpty() ? nullptr : m_project->data().clipById(sel.first()))
                media = c->mediaId;
        }
        if (media.isNull()) {
            statusBar()->showMessage(tr("Select a video in the Media Bin to find its highlights."), 6000);
            return;
        }
        openAutoClip(media);
    });
    add(QStringLiteral("timeline.beats"), tr("Add Beat Markers..."), QKeySequence(), [this] { m_tools->addBeatMarkers(); });
    add(QStringLiteral("timeline.reframe"), tr("Auto Reframe (Vertical / Square)..."), QKeySequence(),
        [this] { m_tools->autoReframe(); });
    add(QStringLiteral("timeline.voiceover"), tr("Record Audio..."), QKeySequence(tr("Ctrl+R")),
        [this] { m_tools->recordVoiceover(); });
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
    add(QStringLiteral("timeline.marker"), tr("Add / Remove Marker"), QKeySequence(Qt::Key_M), [this, player] {
        const TimeUs t = player->position();
        const TimeUs frame = TimeUs(kUsPerSecond / std::max(1.0, m_project->data().sequence.frameRate));
        bool added = false;
        m_project->edit(tr("Marker"), [&](ProjectData &d) { added = timeline::toggleMarker(d, t, frame); });
        statusBar()->showMessage(added ? tr("Marker added at %1").arg(formatTimecode(t, m_project->data().sequence.frameRate))
                                       : tr("Marker removed"),
                                 3000);
    });
    add(QStringLiteral("timeline.nextmarker"), tr("Next Marker"), QKeySequence(tr("Shift+M")), [this] { jumpToMarker(true); });
    add(QStringLiteral("timeline.prevmarker"), tr("Previous Marker"), QKeySequence(tr("Alt+M")), [this] { jumpToMarker(false); });
    add(QStringLiteral("timeline.clearmarkers"), tr("Clear All Markers"), QKeySequence(), [this] {
        m_project->edit(tr("Clear Markers"), [](ProjectData &d) { d.markers.clear(); });
    });
    tl->addSeparator();
    add(QStringLiteral("timeline.addvideotrack"), tr("Add Video Track"), QKeySequence(), [this] {
        m_project->edit(tr("Add Track"), [](ProjectData &d) { d.addTrack(TrackType::Video); });
    });
    add(QStringLiteral("timeline.addaudiotrack"), tr("Add Audio Track"), QKeySequence(), [this] {
        m_project->edit(tr("Add Track"), [](ProjectData &d) { d.addTrack(TrackType::Audio); });
    });

    // ---- Titles ----
    QMenu *titlesMenu = menuBar()->addMenu(tr("T&itles"));
    const QString TT = tr("Titles");
    auto *addTitleAct = makeAction(this, QStringLiteral("titles.add"), TT, tr("Add Title..."), QKeySequence(tr("Ctrl+Alt+T")));
    connect(addTitleAct, &QAction::triggered, this, [this] {
        setWorkspace(Video);
        m_tools->addTitle(0);
    });
    titlesMenu->addAction(addTitleAct);
    QMenu *styles = titlesMenu->addMenu(tr("Add Title Style"));
    const QStringList presetNames = titles::presetNames();
    for (int i = 0; i < presetNames.size(); ++i) {
        styles->addAction(presetNames[i], this, [this, i] {
            setWorkspace(Video);
            m_tools->addTitle(i);
        });
    }
    auto *editTitleAct = makeAction(this, QStringLiteral("titles.edit"), TT, tr("Edit Title Text..."));
    connect(editTitleAct, &QAction::triggered, m_tools, &EditTools::editTitle);
    titlesMenu->addAction(editTitleAct);
    titlesMenu->addSeparator();
    auto *importSrt = makeAction(this, QStringLiteral("titles.importsrt"), TT, tr("Import Captions (SRT)..."));
    connect(importSrt, &QAction::triggered, m_tools, &EditTools::importCaptions);
    titlesMenu->addAction(importSrt);
    auto *exportSrt = makeAction(this, QStringLiteral("titles.exportsrt"), TT, tr("Export Captions (SRT)..."));
    connect(exportSrt, &QAction::triggered, m_tools, &EditTools::exportCaptions);
    titlesMenu->addAction(exportSrt);

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
    addD(QStringLiteral("design.duplicate"), tr("Duplicate Layer"), QKeySequence(),
         [this] { m_design->duplicateSelected(); });
    addD(QStringLiteral("design.forward"), tr("Bring Forward"), QKeySequence(tr("Ctrl+]")), [this] { m_design->moveSelected(1); });
    addD(QStringLiteral("design.backward"), tr("Send Backward"), QKeySequence(tr("Ctrl+[")),
         [this] { m_design->moveSelected(-1); });
    design->addSeparator();
    auto *capture = makeAction(this, QStringLiteral("design.captureframe"), D, tr("Capture Program Frame to Design"),
                               QKeySequence(tr("Ctrl+Shift+F")));
    connect(capture, &QAction::triggered, m_design, &DesignStudio::requestVideoFrame);
    design->addAction(capture);
    auto *toTimeline = makeAction(this, QStringLiteral("design.totimeline"), D, tr("Place Design on Timeline"),
                                  QKeySequence(tr("Ctrl+Shift+T")));
    connect(toTimeline, &QAction::triggered, this, &MainWindow::placeDesignOnTimeline);
    design->addAction(toTimeline);
    QMenu *templates = design->addMenu(tr("Templates"));
    const QStringList templateNames = DesignStudio::templateNames();
    for (int i = 0; i < templateNames.size(); ++i) {
        templates->addAction(templateNames[i], this, [this, i] {
            setWorkspace(Design);
            m_design->applyTemplate(i);
        });
    }

    // ---- View ----
    QMenu *view = menuBar()->addMenu(tr("&View"));
    auto *guides = makeAction(this, QStringLiteral("view.guides"), tr("View"), tr("Show Guides"), QKeySequence(Qt::Key_G));
    guides->setCheckable(true);
    guides->setToolTip(tr("Rule-of-thirds, safe areas and the YouTube timestamp zone"));
    connect(guides, &QAction::toggled, this, [this](bool on) {
        m_design->setGuidesVisible(on);
        m_video->programMonitor()->view()->setGuidesVisible(on);
    });
    view->addAction(guides);

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

void MainWindow::openAutoClip(const QUuid &mediaId)
{
    // Modeless, so a previewed clip can be played in the Source monitor
    // while the list stays open.
    auto *dlg = new AutoClipDialog(m_project, m_autoClip, mediaId, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &AutoClipDialog::status, this, [this](const QString &m) { statusBar()->showMessage(m, 8000); });
    connect(dlg, &AutoClipDialog::previewRequested, this, [this](const QUuid &id, TimeUs in, TimeUs out) {
        if (const MediaItem *m = m_project->data().mediaById(id)) {
            setWorkspace(Video);
            m_video->sourceMonitor()->loadRange(*m, in, out);
            statusBar()->showMessage(tr("Previewing in the Source monitor (in/out set) — press Space to play."), 8000);
        }
    });
    dlg->show();
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
    titles::restoreMissingTitles(d.media);
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
    const QString dir = captureDirectory();
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

QString MainWindow::captureDirectory() const
{
    // Captures live next to the project when it has been saved, so they
    // travel with it; otherwise in the app data folder.
    QString dir;
    if (!m_project->filePath().isEmpty()) {
        const QFileInfo fi(m_project->filePath());
        dir = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(" Captures");
    } else {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/captures");
    }
    QDir().mkpath(dir);
    return dir;
}

void MainWindow::exportFrame()
{
    const QImage frame = m_video->programMonitor()->view()->currentFrame();
    if (frame.isNull()) {
        statusBar()->showMessage(tr("Nothing in the Program Monitor to export — move the playhead over a clip."), 6000);
        return;
    }
    QSettings s;
    const QString dir = s.value(QStringLiteral("paths/designExport"),
                                QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                            .toString();
    const double fps = m_project->data().sequence.frameRate;
    QString name = QStringLiteral("%1 %2.png")
                       .arg(m_project->displayName(),
                            formatTimecode(m_video->programMonitor()->player()->position(), fps).replace(':', '-'));
    QString path = QFileDialog::getSaveFileName(this, tr("Export Frame"), QDir(dir).filePath(name),
                                                tr("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".png");
    s.setValue(QStringLiteral("paths/designExport"), QFileInfo(path).absolutePath());
    const bool jpeg = path.endsWith(QLatin1String(".jpg"), Qt::CaseInsensitive)
        || path.endsWith(QLatin1String(".jpeg"), Qt::CaseInsensitive);
    if ((jpeg ? frame.convertToFormat(QImage::Format_RGB32) : frame).save(path, nullptr, jpeg ? 95 : -1))
        statusBar()->showMessage(tr("Saved frame to %1 (%2×%3)").arg(path).arg(frame.width()).arg(frame.height()), 6000);
    else
        QMessageBox::warning(this, tr("Export Frame"), tr("Could not write %1").arg(path));
}

void MainWindow::relinkMedia()
{
    int missing = 0;
    for (const auto &m : m_project->data().media)
        missing += QFileInfo::exists(m.path) ? 0 : 1;
    if (missing == 0) {
        QMessageBox::information(this, tr("Relink Media"), tr("All media files in this project are where they should be."));
        return;
    }
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Find %n missing file(s) in folder", nullptr, missing),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    if (folder.isEmpty())
        return;
    int found = 0;
    m_project->edit(tr("Relink Media"), [&](ProjectData &d) { found = timeline::relinkMissingMedia(d, folder); });
    m_video->programMonitor()->player()->seek(m_video->programMonitor()->player()->position());
    QMessageBox::information(this, tr("Relink Media"),
                             tr("Relinked %1 of %2 missing file(s).").arg(found).arg(missing));
}

void MainWindow::placeDesignOnTimeline()
{
    const QImage img = m_design->renderImage();
    const QString path = captureDirectory() + QStringLiteral("/design_%1.png")
                                                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    if (!img.save(path)) {
        statusBar()->showMessage(tr("Could not save the design to %1").arg(path), 6000);
        return;
    }
    MediaItem item;
    item.id = QUuid::createUuid();
    item.path = path;
    item.name = tr("Design %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
    item.kind = MediaKind::Image;
    item.resolution = img.size();
    item.hasVideo = true;
    const TimeUs at = m_video->programMonitor()->player()->position();
    QUuid placed;
    m_project->edit(tr("Place Design on Timeline"), [&](ProjectData &d) {
        d.media.append(item);
        const QVector<int> videos = d.videoTrackIndices();
        placed = timeline::placeMedia(d, item.id, videos.isEmpty() ? -1 : videos.last(), at);
    });
    setWorkspace(Video);
    if (!placed.isNull()) {
        m_video->timeline()->setSelection({placed});
        statusBar()->showMessage(tr("Design placed on the top video track at %1 — drag its edges to change how long it shows")
                                     .arg(formatTimecode(at, m_project->data().sequence.frameRate)),
                                 8000);
    }
}

void MainWindow::copySelection()
{
    if (m_stack->currentIndex() == Design) {
        DesignLayer l;
        if (m_design->copySelectedLayer(&l))
            m_layerClipboard = l;
        return;
    }
    const QVector<QUuid> sel = activeTimeline()->selection();
    if (sel.isEmpty())
        return;
    m_clipClipboard = timeline::copyClips(m_project->data(), sel);
    statusBar()->showMessage(tr("Copied %n clip(s)", nullptr, int(m_clipClipboard.size())), 3000);
}

void MainWindow::pasteClipboard()
{
    if (m_stack->currentIndex() == Design) {
        if (m_layerClipboard)
            m_design->pasteLayer(*m_layerClipboard);
        return;
    }
    if (m_clipClipboard.isEmpty())
        return;
    TimelinePlayer *player = m_video->programMonitor()->player();
    QVector<QUuid> created;
    m_project->edit(tr("Paste"), [&](ProjectData &d) { created = timeline::pasteClips(d, m_clipClipboard, player->position()); });
    if (!created.isEmpty()) {
        activeTimeline()->setSelection(created);
        player->seek(timeline::selectionEnd(m_project->data(), created));
    }
}

void MainWindow::duplicateSelection()
{
    if (m_stack->currentIndex() == Design) {
        m_design->duplicateSelected();
        return;
    }
    const QVector<QUuid> sel = activeTimeline()->selection();
    if (sel.isEmpty())
        return;
    const auto entries = timeline::copyClips(m_project->data(), sel);
    const TimeUs end = timeline::selectionEnd(m_project->data(), sel);
    QVector<QUuid> created;
    m_project->edit(tr("Duplicate"), [&](ProjectData &d) { created = timeline::pasteClips(d, entries, end); });
    if (!created.isEmpty())
        activeTimeline()->setSelection(created);
}

void MainWindow::jumpToMarker(bool next)
{
    TimelinePlayer *player = m_video->programMonitor()->player();
    const TimeUs t = next ? timeline::nextMarker(m_project->data(), player->position())
                          : timeline::previousMarker(m_project->data(), player->position());
    if (t >= 0)
        player->seek(t);
    else
        statusBar()->showMessage(next ? tr("No marker after the playhead") : tr("No marker before the playhead"), 2500);
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

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Single-key shortcuts (Space, arrows, S, N, Delete...) must not steal
    // keys from the control the user is typing in or adjusting. Accepting a
    // ShortcutOverride hands the key press to the focused widget instead.
    if (event->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers mods = ke->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
        QWidget *w = qobject_cast<QWidget *>(watched);
        if (w && w == QApplication::focusWidget() && mods == Qt::NoModifier) {
            const int k = ke->key();
            const bool navKey = k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_Up || k == Qt::Key_Down
                || k == Qt::Key_Home || k == Qt::Key_End || k == Qt::Key_PageUp || k == Qt::Key_PageDown;
            const bool textEntry = qobject_cast<QLineEdit *>(w) || qobject_cast<QTextEdit *>(w)
                || qobject_cast<QPlainTextEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w)
                || qobject_cast<QKeySequenceEdit *>(w)
                || (qobject_cast<QComboBox *>(w) && static_cast<QComboBox *>(w)->isEditable());
            const bool navWidget = qobject_cast<QAbstractSlider *>(w) || qobject_cast<QAbstractItemView *>(w);
            if (textEntry || (navWidget && navKey)) {
                event->accept();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
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
