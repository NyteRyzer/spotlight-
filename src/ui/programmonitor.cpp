#include "ui/programmonitor.h"

#include "core/project.h"
#include "media/timelineplayer.h"
#include "media/videoview.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace spotlight {

ProgramMonitor::ProgramMonitor(Project *project, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_view(new VideoView(this))
    , m_player(new TimelinePlayer(project, m_view, this))
    , m_time(new QLabel(this))
    , m_playBtn(new QToolButton(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_view, 1);

    auto *bar = new QHBoxLayout;
    auto button = [this, bar](const QString &text, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        bar->addWidget(b);
        return b;
    };
    auto *start = button(QStringLiteral("⏮"), tr("Go to start (Home)"));
    auto *back = button(QStringLiteral("◀|"), tr("Previous frame (Left)"));
    bar->addWidget(m_playBtn);
    m_playBtn->setText(QStringLiteral("▶"));
    m_playBtn->setToolTip(tr("Play / Pause (Space)"));
    auto *fwd = button(QStringLiteral("|▶"), tr("Next frame (Right)"));
    auto *end = button(QStringLiteral("⏭"), tr("Go to end (End)"));
    bar->addSpacing(12);
    bar->addWidget(m_time);
    bar->addStretch();
    auto *grab = button(tr("Capture Frame"), tr("Capture the current frame and open it in the Design Studio"));
    layout->addLayout(bar);

    connect(start, &QToolButton::clicked, this, [this] { m_player->seek(0); });
    connect(end, &QToolButton::clicked, this, [this] { m_player->seek(m_project->data().duration()); });
    connect(back, &QToolButton::clicked, this, [this] { m_player->stepFrames(-1); });
    connect(fwd, &QToolButton::clicked, this, [this] { m_player->stepFrames(1); });
    connect(m_playBtn, &QToolButton::clicked, m_player, &TimelinePlayer::togglePlay);
    connect(grab, &QToolButton::clicked, this, [this] {
        const QImage f = m_view->currentFrame();
        if (!f.isNull())
            emit captureFrame(f);
    });
    connect(m_player, &TimelinePlayer::playingChanged, this,
            [this](bool playing) { m_playBtn->setText(playing ? QStringLiteral("❚❚") : QStringLiteral("▶")); });
    connect(m_player, &TimelinePlayer::positionChanged, this, &ProgramMonitor::updateTime);
    connect(project, &Project::changed, this, &ProgramMonitor::updateTime);
    updateTime();
}

void ProgramMonitor::updateTime()
{
    const double fps = m_project->data().sequence.frameRate;
    m_time->setText(QStringLiteral("%1 / %2").arg(formatTimecode(m_player->position(), fps),
                                                  formatTimecode(m_project->data().duration(), fps)));
}

} // namespace spotlight
