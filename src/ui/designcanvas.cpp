#include "ui/designcanvas.h"

#include "core/project.h"
#include "ui/theme.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QUrl>

#include <cmath>

namespace spotlight {

DesignCanvas::DesignCanvas(Project *project, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setMinimumSize(320, 200);
    connect(project, &Project::changed, this, [this] {
        if (!m_selected.isNull() && !doc().layer(m_selected)) {
            m_selected = QUuid();
            emit selectionChanged(m_selected);
        }
        update();
    });
}

const DesignDocument &DesignCanvas::doc() const
{
    return m_previewActive ? m_preview : m_project->data().design;
}

void DesignCanvas::setSelectedLayer(const QUuid &id)
{
    if (id == m_selected)
        return;
    m_selected = id;
    emit selectionChanged(id);
    update();
}

QTransform DesignCanvas::viewTransform() const
{
    const QSizeF c = doc().canvasSize;
    const double margin = 24;
    const double s = std::min((width() - 2 * margin) / c.width(), (height() - 2 * margin) / c.height());
    QTransform t;
    t.translate((width() - c.width() * s) / 2, (height() - c.height() * s) / 2);
    t.scale(s, s);
    return t;
}

QPointF DesignCanvas::toCanvas(const QPointF &widgetPos) const
{
    return viewTransform().inverted().map(widgetPos);
}

QRectF DesignCanvas::handleRect(const DesignLayer &l) const
{
    auto *self = const_cast<DesignCanvas *>(this);
    const QPolygonF poly = viewTransform().map(self->m_renderer.canvasPolygon(l));
    const QPointF corner = poly.size() >= 3 ? poly[2] : QPointF();
    return QRectF(corner - QPointF(6, 6), QSizeF(12, 12));
}

void DesignCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(17, 17, 20));
    const QTransform vt = viewTransform();
    const QRectF canvasRect = vt.mapRect(QRectF(QPointF(0, 0), QSizeF(doc().canvasSize)));
    p.fillRect(canvasRect.translated(4, 6), QColor(0, 0, 0, 120));

    p.save();
    p.setTransform(vt);
    p.setClipRect(QRectF(QPointF(0, 0), QSizeF(doc().canvasSize)));
    m_renderer.render(p, doc());
    p.restore();

    if (m_guides) {
        const double cw = doc().canvasSize.width();
        const double ch = doc().canvasSize.height();
        p.save();
        p.setTransform(vt);
        QPen thirds(QColor(255, 255, 255, 110), 0, Qt::DashLine);
        p.setPen(thirds);
        for (int i = 1; i < 3; ++i) {
            p.drawLine(QLineF(cw * i / 3, 0, cw * i / 3, ch));
            p.drawLine(QLineF(0, ch * i / 3, cw, ch * i / 3));
        }
        p.setPen(QPen(QColor(255, 196, 0, 120), 0, Qt::DotLine));
        p.drawLine(QLineF(cw / 2, 0, cw / 2, ch));
        p.drawLine(QLineF(0, ch / 2, cw, ch / 2));
        // YouTube overlays the video length in the bottom-right corner of
        // thumbnails; keep important content out of that box.
        const QRectF stamp(cw * 0.84, ch * 0.86, cw * 0.14, ch * 0.10);
        p.fillRect(stamp, QColor(230, 40, 50, 70));
        p.setPen(QPen(QColor(230, 40, 50, 200), 0));
        p.drawRect(stamp);
        p.restore();
        p.setPen(QColor(255, 200, 200));
        p.drawText(vt.mapRect(stamp), Qt::AlignCenter, tr("timestamp"));
    }

    // Selection outline + scale handle.
    if (const DesignLayer *l = doc().layer(m_selected)) {
        const QPolygonF poly = vt.map(m_renderer.canvasPolygon(*l));
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(theme::accent, 1.5, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(poly);
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(theme::accent);
        p.drawRect(handleRect(*l));
    }

    p.setPen(theme::textDim);
    p.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignBottom | Qt::AlignRight,
               QStringLiteral("%1 × %2").arg(doc().canvasSize.width()).arg(doc().canvasSize.height()));
}

void DesignCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QPointF cpos = toCanvas(e->position());
    m_pressCanvas = cpos;

    if (const DesignLayer *sel = doc().layer(m_selected)) {
        if (handleRect(*sel).contains(e->position())) {
            m_drag = Drag::Scale;
            m_orig = *sel;
            return;
        }
    }
    const QUuid hit = m_renderer.layerAt(doc(), cpos);
    setSelectedLayer(hit);
    if (!hit.isNull()) {
        m_drag = Drag::Move;
        m_orig = *doc().layer(hit);
    }
}

void DesignCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag == Drag::None) {
        const DesignLayer *sel = doc().layer(m_selected);
        if (sel && handleRect(*sel).contains(e->position()))
            setCursor(Qt::SizeFDiagCursor);
        else if (!m_renderer.layerAt(doc(), toCanvas(e->position())).isNull())
            setCursor(Qt::SizeAllCursor);
        else
            unsetCursor();
        return;
    }
    if (!m_previewActive) {
        m_preview = m_project->data().design;
        m_previewActive = true;
    }
    DesignLayer *l = m_preview.layer(m_orig.id);
    if (!l)
        return;
    const QPointF cpos = toCanvas(e->position());
    if (m_drag == Drag::Move) {
        QPointF pos = m_orig.position + (cpos - m_pressCanvas);
        // Snap the layer's centre to the canvas centre lines.
        const QRectF r = m_renderer.localRect(m_orig);
        const QPointF half(r.width() * m_orig.scale / 2, r.height() * m_orig.scale / 2);
        const QPointF centre = pos + half;
        const QSizeF c = m_preview.canvasSize;
        const double tol = 10 / viewTransform().m11();
        if (std::abs(centre.x() - c.width() / 2) < tol)
            pos.setX(c.width() / 2 - half.x());
        if (std::abs(centre.y() - c.height() / 2) < tol)
            pos.setY(c.height() / 2 - half.y());
        l->position = pos;
    } else {
        // Uniform scale around the layer's centre, by distance from it.
        const QRectF r = m_renderer.localRect(m_orig);
        const QPointF centre = m_orig.position + QPointF(r.width() * m_orig.scale / 2, r.height() * m_orig.scale / 2);
        const double d0 = std::hypot(m_pressCanvas.x() - centre.x(), m_pressCanvas.y() - centre.y());
        const double d1 = std::hypot(cpos.x() - centre.x(), cpos.y() - centre.y());
        if (d0 > 1) {
            const double s = std::clamp(m_orig.scale * d1 / d0, 0.02, 50.0);
            l->scale = s;
            l->position = centre - QPointF(r.width() * s / 2, r.height() * s / 2);
        }
    }
    update();
}

void DesignCanvas::mouseReleaseEvent(QMouseEvent *)
{
    if (m_previewActive) {
        const DesignDocument result = m_preview;
        m_previewActive = false;
        m_project->edit(m_drag == Drag::Scale ? tr("Scale Layer") : tr("Move Layer"),
                        [&](ProjectData &d) { d.design = result; });
    }
    m_drag = Drag::None;
    update();
}

void DesignCanvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    const QUuid hit = m_renderer.layerAt(doc(), toCanvas(e->position()));
    const DesignLayer *l = doc().layer(hit);
    if (l && l->type == LayerType::Text)
        emit editTextRequested(hit);
}

void DesignCanvas::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void DesignCanvas::dropEvent(QDropEvent *e)
{
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls()) {
        if (u.isLocalFile())
            paths << u.toLocalFile();
    }
    if (!paths.isEmpty())
        emit imagesDropped(paths, toCanvas(e->position()));
    e->acceptProposedAction();
}

} // namespace spotlight
