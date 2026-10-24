#include "ui/mediabin.h"

#include "core/project.h"
#include "core/timelineops.h"
#include "media/mediaprobe.h"
#include "ui/theme.h"

#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace spotlight {

namespace {

const char *kMediaMime = "application/x-spotlight-media";

class BinList : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QStringList mimeTypes() const override { return {QString::fromLatin1(kMediaMime)}; }
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override
    {
        QStringList ids;
        for (auto *i : items)
            ids << i->data(Qt::UserRole).toString();
        auto *md = new QMimeData;
        md->setData(kMediaMime, ids.join(QLatin1Char('\n')).toUtf8());
        return md;
    }
};

QIcon makeIcon(const QImage &thumb, const MediaItem &m)
{
    QImage canvas(160, 90, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(QColor(16, 16, 20));
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!thumb.isNull()) {
        QSize s = thumb.size();
        s.scale(canvas.size(), Qt::KeepAspectRatio);
        p.drawImage(QRect(QPoint((160 - s.width()) / 2, (90 - s.height()) / 2), s), thumb);
    } else {
        const QColor c = m.kind == MediaKind::Audio ? theme::audioClip : theme::videoClip;
        p.fillRect(canvas.rect(), c.darker(200));
        p.setPen(c.lighter(150));
        QFont f;
        f.setPixelSize(30);
        f.setBold(true);
        p.setFont(f);
        p.drawText(canvas.rect(), Qt::AlignCenter, m.kind == MediaKind::Audio ? QStringLiteral("♫") : QStringLiteral("▶"));
    }
    if (!QFileInfo::exists(m.path)) {
        p.fillRect(QRect(0, 70, 160, 20), QColor(200, 40, 40));
        p.setPen(Qt::white);
        p.drawText(QRect(0, 70, 160, 20), Qt::AlignCenter, QObject::tr("MISSING"));
    } else if (!m.isStill()) {
        const QString d = formatDuration(m.duration);
        QFont f;
        f.setPixelSize(11);
        p.setFont(f);
        const QRect badge(160 - 8 - p.fontMetrics().horizontalAdvance(d) - 8, 90 - 20, p.fontMetrics().horizontalAdvance(d) + 8, 15);
        p.fillRect(badge, QColor(0, 0, 0, 180));
        p.setPen(Qt::white);
        p.drawText(badge, Qt::AlignCenter, d);
    }
    p.end();
    return QIcon(QPixmap::fromImage(canvas));
}

} // namespace

MediaBin::MediaBin(Project *project, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_list(new BinList(this))
    , m_hint(new QLabel(this))
    , m_probe(new MediaProbe(this))
    , m_thumbProbe(new MediaProbe(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto *bar = new QHBoxLayout;
    auto *importBtn = new QToolButton(this);
    importBtn->setText(tr("＋ Import"));
    importBtn->setToolTip(tr("Import video, audio and image files"));
    connect(importBtn, &QToolButton::clicked, this, &MediaBin::importDialog);
    auto *removeBtn = new QToolButton(this);
    removeBtn->setText(tr("Remove"));
    connect(removeBtn, &QToolButton::clicked, this, &MediaBin::removeSelected);
    bar->addWidget(importBtn);
    bar->addWidget(removeBtn);
    bar->addStretch();
    layout->addLayout(bar);

    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(160, 90));
    m_list->setGridSize(QSize(176, 128));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setUniformItemSizes(true);
    layout->addWidget(m_list, 1);

    m_hint->setText(tr("Import media or drop files here.\nThen drag clips onto the timeline."));
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setStyleSheet(QStringLiteral("color: #8a8a96;"));
    layout->addWidget(m_hint);

    setAcceptDrops(true);

    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *it = m_list->itemAt(pos);
        if (!it)
            return;
        m_list->setCurrentItem(it);
        const QUuid id = QUuid::fromString(it->data(Qt::UserRole).toString());
        const MediaItem *m = m_project->data().mediaById(id);
        QMenu menu(this);
        menu.addAction(tr("Open in Source Monitor"), this, [this, id] { emit openInSource(id); });
        if (m && !m->isStill() && m->duration > 10 * kUsPerSecond) {
            QAction *a = menu.addAction(m->highlightsAnalyzed && !m->highlights.isEmpty()
                                            ? tr("Auto-Clip: Review %n Highlight(s)…", nullptr, m->highlights.size())
                                            : tr("Auto-Clip: Find Highlights…"),
                                        this, [this, id] { emit autoClipRequested(id); });
            a->setEnabled(!m_analyzing.contains(id));
        }
        menu.addSeparator();
        menu.addAction(tr("Remove from Project"), this, &MediaBin::removeSelected);
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *i) { emit openInSource(QUuid::fromString(i->data(Qt::UserRole).toString())); });
    connect(m_probe, &MediaProbe::probed, this, &MediaBin::onProbed);
    connect(m_probe, &MediaProbe::failed, this, [this](const QString &path, const QString &err) {
        emit status(tr("Could not import %1: %2").arg(QFileInfo(path).fileName(), err));
    });
    connect(m_thumbProbe, &MediaProbe::probed, this, [this](const MediaItem &item, const QImage &thumb) {
        if (!thumb.isNull()) {
            m_thumbs.insert(item.path, thumb);
            rebuild();
            emit thumbnailsChanged();
        }
    });
    connect(project, &Project::changed, this, &MediaBin::rebuild);
    rebuild();
}

void MediaBin::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MediaBin::dropEvent(QDropEvent *e)
{
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls()) {
        if (u.isLocalFile())
            paths << u.toLocalFile();
    }
    importFiles(paths);
    e->acceptProposedAction();
}

void MediaBin::setAnalyzing(const QUuid &mediaId, bool on)
{
    if (on)
        m_analyzing.insert(mediaId);
    else
        m_analyzing.remove(mediaId);
    rebuild();
}

QUuid MediaBin::currentMedia() const
{
    auto *i = m_list->currentItem();
    return i ? QUuid::fromString(i->data(Qt::UserRole).toString()) : QUuid();
}

void MediaBin::importDialog()
{
    QSettings s;
    const QString dir = s.value(QStringLiteral("paths/import"),
                                QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                            .toString();
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Media"), dir, MediaProbe::fileFilter());
    if (files.isEmpty())
        return;
    s.setValue(QStringLiteral("paths/import"), QFileInfo(files.first()).absolutePath());
    importFiles(files);
}

void MediaBin::importFiles(const QStringList &paths)
{
    for (const QString &p : paths) {
        const QString abs = QFileInfo(p).absoluteFilePath();
        bool exists = false;
        for (const auto &m : m_project->data().media)
            exists = exists || m.path == abs;
        if (exists) {
            emit status(tr("%1 is already in the project").arg(QFileInfo(p).fileName()));
            continue;
        }
        emit status(tr("Importing %1…").arg(QFileInfo(p).fileName()));
        m_probe->probe(abs);
    }
}

void MediaBin::onProbed(const MediaItem &item, const QImage &thumb)
{
    if (!thumb.isNull())
        m_thumbs.insert(item.path, thumb);
    m_project->edit(tr("Import %1").arg(item.name), [&](ProjectData &d) { d.media.append(item); });
    emit status(tr("Imported %1").arg(item.name));
    emit thumbnailsChanged();
}

void MediaBin::removeSelected()
{
    const auto items = m_list->selectedItems();
    if (items.isEmpty())
        return;
    QVector<QUuid> ids;
    int usedClips = 0;
    for (auto *i : items) {
        const QUuid id = QUuid::fromString(i->data(Qt::UserRole).toString());
        ids.append(id);
        for (const auto &t : m_project->data().tracks) {
            for (const auto &c : t.clips)
                usedClips += c.mediaId == id ? 1 : 0;
        }
    }
    if (usedClips > 0
        && QMessageBox::question(this, tr("Remove Media"),
                                 tr("The selected media is used by %n clip(s) on the timeline. Remove it and its clips?",
                                    nullptr, usedClips))
            != QMessageBox::Yes)
        return;
    m_project->edit(tr("Remove Media"), [&](ProjectData &d) {
        for (const auto &id : ids)
            timeline::removeMedia(d, id);
    });
}

void MediaBin::rebuild()
{
    // The project changes on every edit anywhere (timeline drags, fader
    // moves...); only rebuild when what the bin shows actually differs, so
    // selection and scroll position survive.
    QString signature;
    for (const MediaItem &m : m_project->data().media) {
        signature += m.id.toString() + m.path + m.name + QString::number(m.duration)
            + (QFileInfo::exists(m.path) ? QLatin1Char('1') : QLatin1Char('0'))
            + (m_thumbs.contains(m.path) ? QLatin1Char('t') : QLatin1Char('-'))
            + QString::number(m.highlightsAnalyzed ? m.highlights.size() : -1)
            + (m_analyzing.contains(m.id) ? QLatin1Char('a') : QLatin1Char('-'));
    }
    if (signature == m_shownSignature && m_list->count() == m_project->data().media.size())
        return;
    m_shownSignature = signature;

    const QUuid current = currentMedia();
    m_list->clear();
    for (const MediaItem &m : m_project->data().media) {
        if (!m_thumbs.contains(m.path) && !m_thumbRequested.contains(m.path) && QFileInfo::exists(m.path)) {
            m_thumbRequested.insert(m.path);
            m_thumbProbe->probe(m.path);
        }
        QString label = m.name;
        if (m_analyzing.contains(m.id))
            label = tr("✂ finding clips…  %1").arg(m.name);
        else if (m.highlightsAnalyzed && !m.highlights.isEmpty())
            label = tr("✂ %1  %2").arg(m.highlights.size()).arg(m.name);
        auto *item = new QListWidgetItem(makeIcon(m_thumbs.value(m.path), m), label, m_list);
        item->setData(Qt::UserRole, m.id.toString(QUuid::WithoutBraces));
        QString tip = QStringLiteral("%1\n%2").arg(m.name, m.path);
        if (!m.resolution.isEmpty())
            tip += QStringLiteral("\n%1×%2").arg(m.resolution.width()).arg(m.resolution.height());
        if (m.frameRate > 0)
            tip += QStringLiteral(" @ %1 fps").arg(m.frameRate, 0, 'f', 2);
        if (!m.isStill())
            tip += QStringLiteral("\n%1").arg(formatDuration(m.duration));
        if (m.highlightsAnalyzed)
            tip += m.highlights.isEmpty() ? tr("\nAuto-Clip: no standout moments found")
                                          : tr("\nAuto-Clip: %n highlight(s) — right-click → Auto-Clip to review",
                                               nullptr, m.highlights.size());
        item->setToolTip(tip);
        if (m.id == current)
            m_list->setCurrentItem(item);
    }
    m_hint->setVisible(m_project->data().media.isEmpty());
}

} // namespace spotlight
