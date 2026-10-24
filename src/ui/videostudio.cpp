#include "ui/videostudio.h"

#include "core/project.h"
#include "core/timelineops.h"
#include "media/timelineplayer.h"
#include "ui/inspector.h"
#include "ui/mediabin.h"
#include "ui/programmonitor.h"
#include "ui/sourcemonitor.h"
#include "ui/timelinewidget.h"

#include <QDockWidget>

namespace spotlight {

namespace {
QDockWidget *dock(QMainWindow *w, const QString &title, const QString &name, QWidget *content)
{
    auto *d = new QDockWidget(title, w);
    d->setObjectName(name); // needed for saveState()/restoreState()
    d->setWidget(content);
    d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                   | QDockWidget::DockWidgetClosable);
    return d;
}
} // namespace

VideoStudio::VideoStudio(Project *project, QWidget *parent)
    : QMainWindow(parent)
    , m_project(project)
    , m_bin(new MediaBin(project, this))
    , m_source(new SourceMonitor(this))
    , m_program(new ProgramMonitor(project, this))
    , m_timeline(new TimelineWidget(project, TimelineWidget::Mode::All, this))
    , m_inspector(new ClipInspector(project, this))
{
    setObjectName(QStringLiteral("VideoStudio"));
    setWindowFlags(Qt::Widget);
    setDockNestingEnabled(true);
    setCentralWidget(m_program);
    m_timeline->setMinimumHeight(220);

    auto *sourceDock = dock(this, tr("Source"), QStringLiteral("video.source"), m_source);
    auto *binDock = dock(this, tr("Media Bin"), QStringLiteral("video.bin"), m_bin);
    auto *inspectorDock = dock(this, tr("Inspector"), QStringLiteral("video.inspector"), m_inspector);
    auto *timelineDock = dock(this, tr("Timeline"), QStringLiteral("video.timeline"), m_timeline);

    addDockWidget(Qt::LeftDockWidgetArea, sourceDock);
    addDockWidget(Qt::LeftDockWidgetArea, binDock);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock);
    resizeDocks({sourceDock, inspectorDock}, {500, 330}, Qt::Horizontal);
    resizeDocks({timelineDock}, {300}, Qt::Vertical);

    m_timeline->setThumbnailProvider([this](const QString &path) { return m_bin->thumbnail(path); });
    connect(m_bin, &MediaBin::thumbnailsChanged, m_timeline, qOverload<>(&QWidget::update));
    connect(m_bin, &MediaBin::status, this, &VideoStudio::status);
    connect(m_bin, &MediaBin::openInSource, this, [this, sourceDock](const QUuid &id) {
        if (const MediaItem *m = m_project->data().mediaById(id)) {
            m_source->load(*m);
            sourceDock->show();
            sourceDock->raise();
        }
    });
    connect(m_timeline, &TimelineWidget::filesDropped, m_bin, &MediaBin::importFiles);

    TimelinePlayer *player = m_program->player();
    connect(m_timeline, &TimelineWidget::seekRequested, player, &TimelinePlayer::seek);
    connect(player, &TimelinePlayer::positionChanged, m_timeline, &TimelineWidget::setPlayhead);
    connect(m_timeline, &TimelineWidget::selectionChanged, this, [this] {
        const auto sel = m_timeline->selection();
        m_inspector->setClip(sel.isEmpty() ? QUuid() : sel.first());
    });

    connect(m_source, &SourceMonitor::addToTimeline, this, [this, player](const QUuid &id, TimeUs in, TimeUs out) {
        QUuid placed;
        m_project->edit(tr("Add Clip"), [&](ProjectData &d) {
            int track = m_timeline->currentTrack();
            placed = timeline::placeMedia(d, id, track, player->position(), in, out);
        });
        if (!placed.isNull()) {
            m_timeline->setSelection(timeline::groupOf(m_project->data(), placed));
            player->seek(m_project->data().clipById(placed)->end());
        }
    });
    connect(m_source, &SourceMonitor::captureFrame, this, &VideoStudio::captureFrame);
    connect(m_program, &ProgramMonitor::captureFrame, this, &VideoStudio::captureFrame);
}

} // namespace spotlight
