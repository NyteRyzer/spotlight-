#include "ui/markerspanel.h"

#include "core/project.h"
#include "core/timelineops.h"
#include "media/timelineplayer.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace spotlight {

MarkersPanel::MarkersPanel(Project *project, TimelinePlayer *player, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_player(player)
    , m_table(new QTableWidget(this))
    , m_delete(new QPushButton(tr("Delete"), this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    m_table->setObjectName(QStringLiteral("markersTable"));
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("Time"), tr("Name")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(m_table, 1);

    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add at Playhead"), this);
    auto *chapters = new QPushButton(tr("YouTube Chapters…"), this);
    buttons->addWidget(add);
    buttons->addWidget(m_delete);
    buttons->addStretch();
    buttons->addWidget(chapters);
    layout->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, [this] {
        const TimeUs t = m_player->position();
        for (const Marker &m : m_project->data().markers) {
            if (m.time == t)
                return; // already one here
        }
        m_project->edit(tr("Add Marker"), [&](ProjectData &d) { timeline::toggleMarker(d, t, 0); });
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        QSet<QUuid> ids;
        for (const auto &r : m_table->selectionModel()->selectedRows())
            ids.insert(QUuid::fromString(m_table->item(r.row(), 0)->data(Qt::UserRole).toString()));
        if (ids.isEmpty())
            return;
        m_project->edit(tr("Delete Markers"), [&](ProjectData &d) {
            d.markers.erase(std::remove_if(d.markers.begin(), d.markers.end(),
                                           [&](const Marker &m) { return ids.contains(m.id); }),
                            d.markers.end());
        });
    });
    connect(chapters, &QPushButton::clicked, this, &MarkersPanel::chaptersRequested);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) {
        m_player->seek(m_table->item(row, 0)->data(Qt::UserRole + 1).toLongLong());
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_updating || item->column() != 1)
            return;
        const QUuid id = QUuid::fromString(m_table->item(item->row(), 0)->data(Qt::UserRole).toString());
        const QString name = item->text().trimmed();
        m_project->edit(tr("Rename Marker"), [&](ProjectData &d) {
            for (Marker &m : d.markers) {
                if (m.id == id)
                    m.name = name;
            }
        });
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            [this] { m_delete->setEnabled(!m_table->selectionModel()->selectedRows().isEmpty()); });
    connect(project, &Project::changed, this, &MarkersPanel::rebuild);
    rebuild();
}

void MarkersPanel::rebuild()
{
    const ProjectData &d = m_project->data();
    QString sig = QString::number(d.sequence.frameRate);
    for (const Marker &m : d.markers)
        sig += m.id.toString() + QString::number(m.time) + m.name;
    if (sig == m_shown)
        return;
    m_shown = sig;
    m_updating = true;
    m_table->setRowCount(int(d.markers.size()));
    for (int i = 0; i < d.markers.size(); ++i) {
        const Marker &m = d.markers[i];
        auto *time = new QTableWidgetItem(formatTimecode(m.time, d.sequence.frameRate));
        time->setFlags(time->flags() & ~Qt::ItemIsEditable);
        time->setData(Qt::UserRole, m.id.toString());
        time->setData(Qt::UserRole + 1, qlonglong(m.time));
        m_table->setItem(i, 0, time);
        m_table->setItem(i, 1, new QTableWidgetItem(m.name));
    }
    m_delete->setEnabled(!m_table->selectionModel()->selectedRows().isEmpty());
    m_updating = false;
}

} // namespace spotlight
