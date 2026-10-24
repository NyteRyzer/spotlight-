#include "ui/timelinewidget.h"

#include "core/project.h"
#include "core/looks.h"
#include "core/timelineops.h"
#include "media/waveformcache.h"
#include "ui/theme.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyle>
#include <QUrl>

#include <cmath>

namespace spotlight {

namespace {
constexpr int kHeaderWidth = 150;
constexpr int kRulerHeight = 28;
constexpr int kEdgeGrab = 6;
constexpr int kSnapPixels = 8;
const char *kMediaMime = "application/x-spotlight-media";

// Ruler tick spacing (seconds) that keeps labels ~80px apart.
double tickStep(double pxPerSecond)
{
    static const double steps[] = {1.0 / 30, 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600};
    for (double s : steps) {
        if (s * pxPerSecond >= 80)
            return s;
    }
    return 7200;
}
} // namespace

TimelineWidget::TimelineWidget(Project *project, Mode mode, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_mode(mode)
    , m_hbar(new QScrollBar(Qt::Horizontal, this))
    , m_vbar(new QScrollBar(Qt::Vertical, this))
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumHeight(140);
    if (mode == Mode::AudioOnly)
        m_pxPerSecond = 60.0;

    connect(m_hbar, &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
    connect(m_vbar, &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
    connect(project, &Project::changed, this, [this] {
        // Drop selections that no longer exist (undo, delete, ...).
        QVector<QUuid> kept;
        for (const auto &id : m_selection) {
            if (m_project->data().clipById(id))
                kept.append(id);
        }
        if (kept != m_selection) {
            m_selection = kept;
            emit selectionChanged();
        }
        if (m_currentTrack >= m_project->data().tracks.size())
            m_currentTrack = -1;
        updateScrollBars();
        update();
    });
    connect(WaveformCache::instance(), &WaveformCache::ready, this, qOverload<>(&QWidget::update));
    updateScrollBars();
}

const ProjectData &TimelineWidget::shown() const
{
    return m_previewActive ? m_preview : m_project->data();
}

QVector<TimelineWidget::Row> TimelineWidget::rows() const
{
    const ProjectData &d = shown();
    QVector<Row> r;
    int y = kRulerHeight - m_vbar->value();
    if (m_mode == Mode::All) {
        const QVector<int> videos = d.videoTrackIndices();
        for (int i = videos.size() - 1; i >= 0; --i) {
            r.append({videos[i], y, 58});
            y += 58;
        }
    }
    const int audioHeight = m_mode == Mode::AudioOnly ? 96 : 58;
    for (int ti : d.audioTrackIndices()) {
        r.append({ti, y, audioHeight});
        y += audioHeight;
    }
    return r;
}

int TimelineWidget::rowAt(int y) const
{
    for (const Row &r : rows()) {
        if (y >= r.y && y < r.y + r.height)
            return r.trackIndex;
    }
    return -1;
}

QRect TimelineWidget::contentRect() const
{
    return QRect(kHeaderWidth, kRulerHeight, width() - kHeaderWidth - m_vbar->width(),
                 height() - kRulerHeight - m_hbar->height());
}

int TimelineWidget::xForTime(TimeUs t) const
{
    return kHeaderWidth + int(std::lround(usToSeconds(t) * m_pxPerSecond)) - m_hbar->value();
}

TimeUs TimelineWidget::timeForX(int x) const
{
    return std::max<TimeUs>(0, secondsToUs(double(x - kHeaderWidth + m_hbar->value()) / m_pxPerSecond));
}

QRect TimelineWidget::clipRect(const Row &row, const Clip &c) const
{
    const int x1 = xForTime(c.start);
    const int x2 = std::max(x1 + 2, xForTime(c.end()));
    return QRect(x1, row.y + 3, x2 - x1, row.height - 6);
}

void TimelineWidget::updateScrollBars()
{
    const int extentPx = int(usToSeconds(m_project->data().duration() + 30 * kUsPerSecond) * m_pxPerSecond);
    const int viewW = std::max(1, width() - kHeaderWidth - m_vbar->width());
    m_hbar->setRange(0, std::max(0, extentPx - viewW));
    m_hbar->setPageStep(viewW);
    m_hbar->setSingleStep(20);

    int contentH = 0;
    for (const Row &r : rows())
        contentH += r.height;
    const int viewH = std::max(1, height() - kRulerHeight - m_hbar->height());
    m_vbar->setRange(0, std::max(0, contentH - viewH));
    m_vbar->setPageStep(viewH);
    m_vbar->setSingleStep(20);
}

void TimelineWidget::resizeEvent(QResizeEvent *)
{
    const int sb = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    m_hbar->setGeometry(kHeaderWidth, height() - sb, width() - kHeaderWidth - sb, sb);
    m_vbar->setGeometry(width() - sb, kRulerHeight, sb, height() - kRulerHeight - sb);
    updateScrollBars();
}

void TimelineWidget::setPlayhead(TimeUs t)
{
    if (t == m_playhead)
        return;
    m_playhead = t;
    ensurePlayheadVisible();
    update();
}

void TimelineWidget::ensurePlayheadVisible()
{
    const QRect c = contentRect();
    const int x = xForTime(m_playhead);
    if (x > c.right() - 20)
        m_hbar->setValue(m_hbar->value() + (x - c.left()) - c.width() / 4);
    else if (x < c.left())
        m_hbar->setValue(std::max(0, m_hbar->value() - (c.left() - x) - c.width() / 4));
}

void TimelineWidget::setSelection(const QVector<QUuid> &ids)
{
    if (ids == m_selection)
        return;
    m_selection = ids;
    if (!ids.isEmpty()) {
        int ti = -1;
        if (m_project->data().clipById(ids.first(), &ti) && ti != m_currentTrack) {
            m_currentTrack = ti;
            emit currentTrackChanged(ti);
        }
    }
    emit selectionChanged();
    update();
}

void TimelineWidget::selectAll()
{
    QVector<QUuid> ids;
    for (const Row &r : rows()) {
        for (const Clip &c : m_project->data().tracks[r.trackIndex].clips)
            ids.append(c.id);
    }
    setSelection(ids);
}

void TimelineWidget::zoomIn()
{
    const int px = xForTime(m_playhead);
    m_pxPerSecond = std::min(m_pxPerSecond * 1.5, 2000.0);
    updateScrollBars();
    m_hbar->setValue(m_hbar->value() + xForTime(m_playhead) - px);
    update();
}

void TimelineWidget::zoomOut()
{
    const int px = xForTime(m_playhead);
    m_pxPerSecond = std::max(m_pxPerSecond / 1.5, 0.5);
    updateScrollBars();
    m_hbar->setValue(m_hbar->value() + xForTime(m_playhead) - px);
    update();
}

void TimelineWidget::zoomToFit()
{
    const double secs = std::max(5.0, usToSeconds(m_project->data().duration()) * 1.05);
    m_pxPerSecond = std::clamp(contentRect().width() / secs, 0.5, 2000.0);
    updateScrollBars();
    m_hbar->setValue(0);
    update();
}

void TimelineWidget::splitAtPlayhead()
{
    const TimeUs t = m_playhead;
    // Split only the selected clips if any are under the playhead, else all.
    QVector<QUuid> targets;
    for (const auto &id : m_selection) {
        const Clip *c = m_project->data().clipById(id);
        if (c && c->contains(t))
            targets.append(id);
    }
    m_project->edit(tr("Split"), [&](ProjectData &d) {
        if (targets.isEmpty())
            timeline::splitAll(d, t);
        else
            timeline::splitClips(d, targets, t);
    });
}

void TimelineWidget::deleteSelection(bool ripple)
{
    if (m_selection.isEmpty())
        return;
    const QVector<QUuid> ids = m_selection;
    m_project->edit(ripple ? tr("Ripple Delete") : tr("Delete"),
                    [&](ProjectData &d) { timeline::removeClips(d, ids, ripple); });
}

TimeUs TimelineWidget::snap(TimeUs t, const QSet<QUuid> &exclude) const
{
    if (!m_snapping)
        return t;
    const TimeUs threshold = secondsToUs(kSnapPixels / m_pxPerSecond);
    QVector<TimeUs> points{m_playhead};
    for (const Marker &m : m_project->data().markers)
        points.append(m.time);
    return timeline::snapTime(m_project->data(), t, threshold, exclude, points);
}

// ---------------------------------------------------------------------------
// Painting

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 20, 24));

    const QVector<Row> rs = rows();
    p.save();
    p.setClipRect(QRect(0, kRulerHeight, width() - m_vbar->width(), height() - kRulerHeight - m_hbar->height()));
    for (const Row &r : rs)
        paintRow(p, r);

    // Drop indicator for media dragged from the bin.
    if (m_dropRow >= 0 && m_dropTime >= 0) {
        for (const Row &r : rs) {
            if (r.trackIndex == m_dropRow) {
                const int x = xForTime(m_dropTime);
                p.fillRect(QRect(x, r.y + 2, 3, r.height - 4), theme::accent);
            }
        }
    }
    if (m_snapLine >= 0) {
        const int x = xForTime(m_snapLine);
        p.setPen(QPen(theme::accent, 1, Qt::DashLine));
        p.drawLine(x, kRulerHeight, x, height());
    }
    p.restore();

    paintRuler(p);

    // Marker guide lines through the lanes.
    p.setPen(QPen(QColor(255, 196, 0, 70), 1, Qt::DashLine));
    for (const Marker &m : shown().markers) {
        const int x = xForTime(m.time);
        if (x >= kHeaderWidth && x <= width() - m_vbar->width())
            p.drawLine(x, kRulerHeight, x, height() - m_hbar->height());
    }

    // Playhead.
    const int px = xForTime(m_playhead);
    if (px >= kHeaderWidth) {
        p.setPen(QPen(theme::playhead, 1.5));
        p.drawLine(px, kRulerHeight - 8, px, height() - m_hbar->height());
        QPainterPath tri;
        tri.moveTo(px - 6, kRulerHeight - 12);
        tri.lineTo(px + 6, kRulerHeight - 12);
        tri.lineTo(px, kRulerHeight - 4);
        tri.closeSubpath();
        p.fillPath(tri, theme::playhead);
    }

    // Corner.
    p.fillRect(QRect(0, 0, kHeaderWidth, kRulerHeight), QColor(26, 26, 30));
    p.setPen(theme::textDim);
    p.drawText(QRect(8, 0, kHeaderWidth - 8, kRulerHeight), Qt::AlignVCenter,
               formatTimecode(m_playhead, m_project->data().sequence.frameRate));
}

void TimelineWidget::paintRuler(QPainter &p)
{
    const QRect ruler(kHeaderWidth, 0, width() - kHeaderWidth, kRulerHeight);
    p.fillRect(ruler, QColor(28, 28, 33));
    p.setPen(QColor(50, 50, 58));
    p.drawLine(ruler.bottomLeft(), ruler.bottomRight());

    const double step = tickStep(m_pxPerSecond);
    const double firstSec = std::floor(usToSeconds(timeForX(kHeaderWidth)) / step) * step;
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p.setFont(f);
    for (double s = firstSec;; s += step) {
        const int x = xForTime(secondsToUs(s));
        if (x > width())
            break;
        if (x < kHeaderWidth)
            continue;
        p.setPen(QColor(90, 90, 100));
        p.drawLine(x, kRulerHeight - 10, x, kRulerHeight);
        const int sub = xForTime(secondsToUs(s + step / 2));
        p.drawLine(sub, kRulerHeight - 5, sub, kRulerHeight);
        p.setPen(theme::textDim);
        p.drawText(x + 3, 13, formatDuration(secondsToUs(s)));
    }

    // Markers: a gold flag on the ruler with the marker's name.
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    for (const Marker &m : shown().markers) {
        const int x = xForTime(m.time);
        if (x < kHeaderWidth - 6 || x > width())
            continue;
        QPainterPath flag;
        flag.moveTo(x, kRulerHeight - 1);
        flag.lineTo(x - 5, kRulerHeight - 8);
        flag.lineTo(x - 5, kRulerHeight - 15);
        flag.lineTo(x + 5, kRulerHeight - 15);
        flag.lineTo(x + 5, kRulerHeight - 8);
        flag.closeSubpath();
        p.fillPath(flag, theme::accent);
        p.setPen(theme::accent);
        QFont mf = p.font();
        mf.setBold(true);
        p.setFont(mf);
        p.drawText(x + 8, kRulerHeight - 5, m.name);
    }
    p.restore();
}

QVector<QPair<QRect, TimelineWidget::HeaderButton>> TimelineWidget::headerButtons(const Row &row) const
{
    const Track &t = m_project->data().tracks[row.trackIndex];
    QVector<QPair<QRect, HeaderButton>> r;
    const int size = 20;
    const int y = row.y + (row.height - size) / 2;
    int x = kHeaderWidth - 8 - size;
    r.append({QRect(x, y, size, size), HeaderButton::Lock});
    x -= size + 4;
    if (t.isAudio()) {
        r.append({QRect(x, y, size, size), HeaderButton::Solo});
        x -= size + 4;
    }
    r.append({QRect(x, y, size, size), HeaderButton::Mute});
    return r;
}

void TimelineWidget::paintRow(QPainter &p, const Row &row)
{
    const ProjectData &d = shown();
    const Track &t = d.tracks[row.trackIndex];
    const QRect lane(kHeaderWidth, row.y, width() - kHeaderWidth, row.height);
    p.fillRect(lane, row.trackIndex % 2 ? QColor(26, 26, 30) : QColor(29, 29, 34));
    p.setPen(QColor(38, 38, 44));
    p.drawLine(lane.bottomLeft(), lane.bottomRight());

    for (const Clip &c : t.clips) {
        const QRect r = clipRect(row, c);
        if (r.right() < kHeaderWidth || r.left() > width())
            continue;
        paintClip(p, row, c);
    }
    if (t.locked) {
        p.fillRect(lane, QBrush(QColor(0, 0, 0, 90), Qt::BDiagPattern));
    }

    // Header.
    const QRect header(0, row.y, kHeaderWidth, row.height);
    p.fillRect(header, row.trackIndex == m_currentTrack ? QColor(44, 42, 34) : QColor(34, 34, 40));
    p.fillRect(QRect(0, row.y, 4, row.height), t.isVideo() ? theme::videoClip : theme::audioClip);
    p.setPen(theme::text);
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    p.drawText(header.adjusted(12, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, t.name);
    p.setFont(font());

    for (const auto &[rect, kind] : headerButtons(row)) {
        bool on = false;
        QString label;
        QColor onColor = theme::accent;
        switch (kind) {
        case HeaderButton::Mute:
            on = t.muted;
            label = t.isVideo() ? QStringLiteral("V") : QStringLiteral("M");
            if (t.isVideo())
                on = !t.muted; // eye: lit when visible
            onColor = t.isVideo() ? QColor(90, 90, 104) : QColor(230, 80, 80);
            break;
        case HeaderButton::Solo:
            on = t.solo;
            label = QStringLiteral("S");
            onColor = QColor(80, 180, 255);
            break;
        case HeaderButton::Lock:
            on = t.locked;
            label = QStringLiteral("L");
            break;
        default:
            break;
        }
        p.setPen(Qt::NoPen);
        p.setBrush(on ? onColor : QColor(52, 52, 60));
        p.drawRoundedRect(rect, 4, 4);
        p.setPen(on ? Qt::black : theme::textDim);
        if (kind == HeaderButton::Mute && t.isVideo())
            p.setPen(on ? Qt::white : theme::textDim);
        p.drawText(rect, Qt::AlignCenter, label);
    }
}

void TimelineWidget::paintClip(QPainter &p, const Row &row, const Clip &c)
{
    const ProjectData &d = shown();
    const MediaItem *m = d.mediaById(c.mediaId);
    const Track &t = d.tracks[row.trackIndex];
    const QRect r = clipRect(row, c);
    const bool selected = m_selection.contains(c.id);

    QColor base = t.isAudio() ? theme::audioClip
                              : (m && m->isTitle() ? theme::titleClip
                                                   : (m && m->isStill() ? theme::imageClip : theme::videoClip));
    if (t.muted)
        base = base.darker(180);
    p.setPen(Qt::NoPen);
    p.setBrush(base.darker(130));
    p.drawRoundedRect(r, 4, 4);

    p.save();
    p.setClipRect(r.adjusted(1, 1, -1, -1), Qt::IntersectClip);
    // Title strip.
    const QRect title(r.left(), r.top(), r.width(), 16);
    p.fillRect(title, base);

    if (m && t.isVideo() && m_thumbs) {
        const QImage thumb = m_thumbs(m->path);
        if (!thumb.isNull()) {
            const QRect body(r.left(), r.top() + 16, r.width(), r.height() - 16);
            const QImage scaled = thumb.scaledToHeight(std::max(1, body.height()), Qt::SmoothTransformation);
            for (int x = body.left(); x < body.right(); x += scaled.width() + 1)
                p.drawImage(QPoint(x, body.top()), scaled);
            if (!c.color.isIdentity())
                p.fillRect(body, QColor(255, 196, 0, 30));
        }
    }

    // Transition into this clip: a crossed band at its head.
    if (t.isVideo()) {
        const Transition tr = timeline::effectiveTransition(t, c);
        if (!tr.isNone()) {
            const QRect band(r.left(), r.top() + 16, std::max(6, xForTime(c.start + tr.duration) - r.left()),
                             r.height() - 16);
            p.fillRect(band, QColor(255, 196, 0, 70));
            p.setPen(QPen(QColor(255, 220, 120, 220), 1));
            p.drawLine(band.topLeft(), band.bottomRight());
            p.drawLine(band.bottomLeft(), band.topRight());
        }
    }
    if (m && t.isAudio())
        paintWaveform(p, QRect(r.left(), r.top() + 16, r.width(), r.height() - 16), c, *m);

    // Fades.
    if (c.fadeIn > 0 || c.fadeOut > 0) {
        p.setPen(QPen(QColor(255, 255, 255, 180), 1));
        p.setBrush(QColor(0, 0, 0, 90));
        if (c.fadeIn > 0) {
            const int fx = xForTime(c.start + c.fadeIn);
            QPolygon poly({QPoint(r.left(), r.bottom()), QPoint(fx, r.top() + 16), QPoint(r.left(), r.top() + 16)});
            p.drawPolygon(poly);
        }
        if (c.fadeOut > 0) {
            const int fx = xForTime(c.end() - c.fadeOut);
            QPolygon poly({QPoint(r.right(), r.bottom()), QPoint(fx, r.top() + 16), QPoint(r.right(), r.top() + 16)});
            p.drawPolygon(poly);
        }
    }

    p.setPen(Qt::white);
    QString label = m ? m->name : tr("(missing media)");
    if (!c.linkId.isNull())
        label = QStringLiteral("⇄ ") + label;
    // Badges for what's applied to the clip.
    QStringList badges;
    if (!qFuzzyCompare(c.speed, 1.0))
        badges << QStringLiteral("%1×").arg(c.speed, 0, 'g', 3);
    if (t.isVideo()) {
        if (!c.transform.isIdentity())
            badges << QStringLiteral("◱");
        if (c.motion != Motion::None)
            badges << QStringLiteral("⤢");
        if (c.key.enabled)
            badges << QStringLiteral("▣");
        if (c.stabilize)
            badges << QStringLiteral("≋");
    }
    if (!badges.isEmpty())
        label = badges.join(QLatin1Char(' ')) + QStringLiteral("  ") + label;
    p.drawText(title.adjusted(5, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(label, Qt::ElideRight, title.width() - 9));
    p.restore();

    if (selected) {
        p.setPen(QPen(theme::accent, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
    }
}

void TimelineWidget::paintWaveform(QPainter &p, const QRect &r, const Clip &c, const MediaItem &m)
{
    const QVector<float> *peaks = WaveformCache::instance()->peaks(m.path);
    const int mid = r.center().y();
    if (!peaks) {
        p.setPen(QColor(255, 255, 255, 60));
        p.drawLine(r.left(), mid, r.right(), mid);
        return;
    }
    const double gain = c.gain;
    const int half = r.height() / 2 - 1;
    p.setPen(QColor(200, 255, 225, 200));
    const int x0 = std::max(r.left(), kHeaderWidth);
    const int x1 = std::min(r.right(), width());
    for (int x = x0; x < x1; ++x) {
        const TimeUs t0 = c.sourceAt(timeForX(x));
        const TimeUs t1 = c.sourceAt(timeForX(x + 1));
        qsizetype b0 = qsizetype(t0 * WaveformCache::kPeaksPerSecond / kUsPerSecond);
        qsizetype b1 = std::max(b0 + 1, qsizetype(t1 * WaveformCache::kPeaksPerSecond / kUsPerSecond));
        float v = 0;
        for (qsizetype b = std::max<qsizetype>(0, b0); b < std::min(b1, peaks->size()); ++b)
            v = std::max(v, (*peaks)[b]);
        const double env = timeline::fadeEnvelope(c, timeForX(x));
        const int h = int(std::min(1.0, v * gain * env) * half);
        p.drawLine(x, mid - h, x, mid + h);
    }
}

// ---------------------------------------------------------------------------
// Interaction

bool TimelineWidget::hitClip(const QPoint &pos, QUuid *clipId, int *trackIndex, Drag *edge) const
{
    if (pos.x() < kHeaderWidth || pos.y() < kRulerHeight)
        return false;
    for (const Row &r : rows()) {
        if (pos.y() < r.y || pos.y() >= r.y + r.height)
            continue;
        const Track &t = m_project->data().tracks[r.trackIndex];
        for (const Clip &c : t.clips) {
            const QRect cr = clipRect(r, c);
            if (!cr.adjusted(-2, 0, 2, 0).contains(pos))
                continue;
            *clipId = c.id;
            *trackIndex = r.trackIndex;
            if (pos.x() - cr.left() <= kEdgeGrab && cr.width() > kEdgeGrab * 3)
                *edge = Drag::TrimStart;
            else if (cr.right() - pos.x() <= kEdgeGrab && cr.width() > kEdgeGrab * 3)
                *edge = Drag::TrimEnd;
            else
                *edge = Drag::Move;
            return true;
        }
    }
    return false;
}

TimelineWidget::HeaderButton TimelineWidget::hitHeaderButton(const QPoint &pos, int *trackIndex) const
{
    if (pos.x() >= kHeaderWidth)
        return HeaderButton::None;
    for (const Row &r : rows()) {
        if (pos.y() < r.y || pos.y() >= r.y + r.height)
            continue;
        *trackIndex = r.trackIndex;
        for (const auto &[rect, kind] : headerButtons(r)) {
            if (rect.contains(pos))
                return kind;
        }
        return HeaderButton::None;
    }
    *trackIndex = -1;
    return HeaderButton::None;
}

void TimelineWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_pressPos = e->pos();
    m_dragMoved = false;

    if (e->pos().y() < kRulerHeight && e->pos().x() >= kHeaderWidth) {
        m_drag = Drag::Scrub;
        emit seekRequested(timeForX(e->pos().x()));
        return;
    }

    int ti = -1;
    const HeaderButton hb = hitHeaderButton(e->pos(), &ti);
    if (e->pos().x() < kHeaderWidth) {
        if (ti >= 0 && hb != HeaderButton::None) {
            const QString name = m_project->data().tracks[ti].name;
            m_project->edit(tr("Toggle %1").arg(name), [&](ProjectData &d) {
                Track &t = d.tracks[ti];
                if (hb == HeaderButton::Mute)
                    t.muted = !t.muted;
                else if (hb == HeaderButton::Solo)
                    t.solo = !t.solo;
                else if (hb == HeaderButton::Lock)
                    t.locked = !t.locked;
            });
        }
        if (ti >= 0 && ti != m_currentTrack) {
            m_currentTrack = ti;
            emit currentTrackChanged(ti);
            update();
        }
        return;
    }

    QUuid id;
    Drag edge = Drag::None;
    if (!hitClip(e->pos(), &id, &ti, &edge)) {
        if (!(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)))
            setSelection({});
        // Clicking empty lane space moves the playhead there.
        if (e->pos().y() >= kRulerHeight) {
            m_drag = Drag::Scrub;
            emit seekRequested(timeForX(e->pos().x()));
        }
        return;
    }

    QVector<QUuid> sel = m_selection;
    const QVector<QUuid> group = timeline::groupOf(m_project->data(), id);
    if (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) {
        if (sel.contains(id)) {
            for (const auto &g : group)
                sel.removeAll(g);
        } else {
            sel += group;
        }
    } else if (!sel.contains(id)) {
        sel = group;
    }
    setSelection(sel);
    if (ti != m_currentTrack) {
        m_currentTrack = ti;
        emit currentTrackChanged(ti);
    }

    if (m_project->data().tracks[ti].locked)
        return;
    const Clip *c = m_project->data().clipById(id);
    m_drag = edge;
    m_dragClip = id;
    m_dragOrigStart = c->start;
    m_dragOrigEnd = c->end();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag == Drag::None) {
        QUuid id;
        int ti;
        Drag edge = Drag::None;
        if (hitClip(e->pos(), &id, &ti, &edge) && (edge == Drag::TrimStart || edge == Drag::TrimEnd))
            setCursor(Qt::SizeHorCursor);
        else
            unsetCursor();
        return;
    }
    if (m_drag == Drag::Scrub) {
        emit seekRequested(timeForX(e->pos().x()));
        return;
    }
    if (!m_dragMoved && (e->pos() - m_pressPos).manhattanLength() < 4)
        return;
    m_dragMoved = true;
    applyDragPreview(e->pos());
}

void TimelineWidget::applyDragPreview(const QPoint &pos)
{
    m_preview = m_project->data();
    m_previewActive = true;
    const TimeUs dt = timeForX(pos.x()) - timeForX(m_pressPos.x());
    const QSet<QUuid> exclude(m_selection.begin(), m_selection.end());
    m_snapLine = -1;

    if (m_drag == Drag::Move) {
        TimeUs start = std::max<TimeUs>(0, m_dragOrigStart + dt);
        const TimeUs len = m_dragOrigEnd - m_dragOrigStart;
        const TimeUs s1 = snap(start, exclude);
        const TimeUs s2 = snap(start + len, exclude) - len;
        if (s1 != start && (s2 == start || std::abs(s1 - start) <= std::abs(s2 - start))) {
            m_snapLine = s1;
            start = s1;
        } else if (s2 != start) {
            m_snapLine = s2 + len;
            start = s2;
        }
        int track = rowAt(pos.y());
        int origTrack = -1;
        m_preview.clipById(m_dragClip, &origTrack);
        if (track < 0 || m_preview.tracks[track].type != m_preview.tracks[origTrack].type)
            track = origTrack;
        timeline::moveClip(m_preview, m_dragClip, start, track);
    } else if (m_drag == Drag::TrimStart) {
        TimeUs t = snap(m_dragOrigStart + dt, exclude);
        if (t != m_dragOrigStart + dt)
            m_snapLine = t;
        timeline::trimClipStart(m_preview, m_dragClip, t);
    } else if (m_drag == Drag::TrimEnd) {
        TimeUs t = snap(m_dragOrigEnd + dt, exclude);
        if (t != m_dragOrigEnd + dt)
            m_snapLine = t;
        timeline::trimClipEnd(m_preview, m_dragClip, t);
    }
    update();
}

void TimelineWidget::commitDrag()
{
    if (!m_previewActive)
        return;
    const ProjectData result = m_preview;
    m_previewActive = false;
    m_snapLine = -1;
    const QString text = m_drag == Drag::Move ? tr("Move Clip") : tr("Trim Clip");
    // The preview already holds the edited state; commit it as one step.
    m_project->edit(text, [&](ProjectData &d) { d.tracks = result.tracks; });
    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_dragMoved && (m_drag == Drag::Move || m_drag == Drag::TrimStart || m_drag == Drag::TrimEnd))
        commitDrag();
    m_drag = Drag::None;
    m_previewActive = false;
    m_dragMoved = false;
    update();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    QUuid id;
    int ti;
    Drag edge;
    if (hitClip(e->pos(), &id, &ti, &edge))
        emit seekRequested(m_project->data().clipById(id)->start);
}

void TimelineWidget::wheelEvent(QWheelEvent *e)
{
    const QPoint delta = e->angleDelta();
    if (e->modifiers() & Qt::ControlModifier) {
        if (delta.y() > 0)
            zoomIn();
        else if (delta.y() < 0)
            zoomOut();
    } else if (e->modifiers() & Qt::ShiftModifier || std::abs(delta.x()) > std::abs(delta.y())) {
        const int d = delta.x() != 0 ? delta.x() : delta.y();
        m_hbar->setValue(m_hbar->value() - d);
    } else {
        m_vbar->setValue(m_vbar->value() - delta.y() / 2);
    }
    e->accept();
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent *e)
{
    QUuid id;
    int ti;
    Drag edge;
    QMenu menu(this);
    if (hitClip(e->pos(), &id, &ti, &edge)) {
        if (!m_selection.contains(id))
            setSelection(timeline::groupOf(m_project->data(), id));
        menu.addAction(tr("Split at Playhead"), this, &TimelineWidget::splitAtPlayhead);
        menu.addAction(tr("Delete"), this, [this] { deleteSelection(false); });
        menu.addAction(tr("Ripple Delete"), this, [this] { deleteSelection(true); });
        menu.addSeparator();
        QMenu *speed = menu.addMenu(tr("Speed"));
        for (double v : {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0}) {
            speed->addAction(QStringLiteral("%1×").arg(v), this, [this, id, v] {
                m_project->edit(tr("Change Speed"), [&](ProjectData &d) { timeline::setClipSpeed(d, id, v); });
            });
        }
        if (m_project->data().tracks[ti].isVideo()) {
            QMenu *trans = menu.addMenu(tr("Transition In"));
            trans->addAction(tr("None"), this, [this, id] {
                m_project->edit(tr("Transition"), [&](ProjectData &d) {
                    if (Clip *cl = d.clipById(id))
                        cl->transition = Transition();
                });
            });
            for (const auto &tt : transitionTypes()) {
                trans->addAction(tt.name, this, [this, id, type = tt.id] {
                    m_project->edit(tr("Transition"), [&](ProjectData &d) {
                        if (Clip *cl = d.clipById(id))
                            cl->transition = {type, std::min<TimeUs>(kUsPerSecond, cl->duration() / 2)};
                    });
                });
            }
            QMenu *looks = menu.addMenu(tr("Look"));
            for (const auto &lp : lookPresets()) {
                looks->addAction(lp.name, this, [this, id, a = lp.adjust] {
                    m_project->edit(tr("Look"), [&](ProjectData &d) {
                        if (Clip *cl = d.clipById(id))
                            cl->color = a;
                    });
                });
            }
        }
        if (m_menuExtender)
            m_menuExtender(&menu, id);
        menu.addSeparator();
        const Clip *c = m_project->data().clipById(id);
        if (c && !c->linkId.isNull()) {
            menu.addAction(tr("Unlink Audio/Video"), this, [this, link = c->linkId] {
                m_project->edit(tr("Unlink"), [&](ProjectData &d) {
                    for (auto &t : d.tracks) {
                        for (auto &cl : t.clips) {
                            if (cl.linkId == link)
                                cl.linkId = QUuid();
                        }
                    }
                });
            });
        } else if (m_selection.size() == 2) {
            menu.addAction(tr("Link Clips"), this, [this] {
                const QUuid link = QUuid::createUuid();
                const QVector<QUuid> sel = m_selection;
                m_project->edit(tr("Link"), [&](ProjectData &d) {
                    for (const auto &sid : sel) {
                        if (Clip *cl = d.clipById(sid))
                            cl->linkId = link;
                    }
                });
            });
        }
    } else {
        const int row = rowAt(e->pos().y());
        menu.addAction(tr("Add Video Track"), this,
                       [this] { m_project->edit(tr("Add Track"), [](ProjectData &d) { d.addTrack(TrackType::Video); }); });
        menu.addAction(tr("Add Audio Track"), this,
                       [this] { m_project->edit(tr("Add Track"), [](ProjectData &d) { d.addTrack(TrackType::Audio); }); });
        if (row >= 0 && m_project->data().tracks[row].clips.isEmpty()) {
            menu.addAction(tr("Delete Empty Track \"%1\"").arg(m_project->data().tracks[row].name), this, [this, row] {
                m_project->edit(tr("Delete Track"), [&](ProjectData &d) { d.tracks.removeAt(row); });
            });
        }
    }
    menu.exec(e->globalPos());
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(kMediaMime) || e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent *e)
{
    if (!e->mimeData()->hasFormat(kMediaMime)) {
        e->acceptProposedAction();
        return;
    }
    const QPoint pos = e->position().toPoint();
    m_dropRow = rowAt(pos.y());
    m_dropTime = snap(timeForX(pos.x()), {});
    e->acceptProposedAction();
    update();
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropRow = -1;
    m_dropTime = -1;
    update();
}

void TimelineWidget::dropEvent(QDropEvent *e)
{
    const QMimeData *md = e->mimeData();
    if (md->hasFormat(kMediaMime)) {
        const QPoint pos = e->position().toPoint();
        const int track = rowAt(pos.y());
        TimeUs t = snap(timeForX(pos.x()), {});
        const QStringList ids = QString::fromUtf8(md->data(kMediaMime)).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVector<QUuid> placed;
        m_project->edit(tr("Add Clip"), [&](ProjectData &d) {
            for (const QString &s : ids) {
                const QUuid mid = QUuid::fromString(s);
                const QUuid cid = timeline::placeMedia(d, mid, track, t);
                if (!cid.isNull()) {
                    placed.append(cid);
                    t = d.clipById(cid)->end();
                }
            }
        });
        if (!placed.isEmpty())
            setSelection(timeline::groupOf(m_project->data(), placed.first()));
        e->acceptProposedAction();
    } else if (md->hasUrls()) {
        QStringList paths;
        for (const QUrl &u : md->urls()) {
            if (u.isLocalFile())
                paths << u.toLocalFile();
        }
        if (!paths.isEmpty())
            emit filesDropped(paths);
        e->acceptProposedAction();
    }
    m_dropRow = -1;
    m_dropTime = -1;
    update();
}

} // namespace spotlight
