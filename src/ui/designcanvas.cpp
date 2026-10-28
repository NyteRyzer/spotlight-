#include "ui/designcanvas.h"

#include "core/project.h"
#include "ui/theme.h"

#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QUrl>

#include <array>
#include <cmath>

namespace spotlight {

DesignCanvas::DesignCanvas(Project *project, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setMinimumSize(320, 200);
    setFocusPolicy(Qt::StrongFocus);
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

double DesignCanvas::fitScale() const
{
    const QSizeF c = doc().canvasSize;
    const double margin = 24;
    return std::max(0.01, std::min((width() - 2 * margin) / c.width(), (height() - 2 * margin) / c.height()));
}

QTransform DesignCanvas::viewTransform() const
{
    const QSizeF c = doc().canvasSize;
    const double s = fitScale() * m_zoom;
    QTransform t;
    t.translate((width() - c.width() * s) / 2 + m_pan.x(), (height() - c.height() * s) / 2 + m_pan.y());
    t.scale(s, s);
    return t;
}

void DesignCanvas::setZoom(double zoom, const QPointF &anchor)
{
    zoom = std::clamp(zoom, 0.1, 32.0);
    // Keep the canvas point under `anchor` (default: the widget centre) still.
    const QPointF a = anchor.x() < 0 ? QPointF(width() / 2.0, height() / 2.0) : anchor;
    const QPointF canvasPt = toCanvas(a);
    m_zoom = zoom;
    const QPointF now = viewTransform().map(canvasPt);
    m_pan += a - now;
    update();
}

void DesignCanvas::zoomToFit()
{
    m_zoom = 1.0;
    m_pan = QPointF();
    update();
}

void DesignCanvas::nudgeSelected(double dx, double dy)
{
    const DesignLayer *l = m_project->data().design.layer(m_selected);
    if (!l || l->locked)
        return;
    const QUuid id = m_selected;
    // Repeated key presses merge into one undo step.
    m_project->edit(
        tr("Nudge Layer"),
        [&](ProjectData &d) {
            if (DesignLayer *t = d.design.layer(id))
                t->position += QPointF(dx, dy);
        },
        QStringLiteral("nudge") + id.toString());
}

QPointF DesignCanvas::snapMove(const QRectF &box, const QUuid &movingId)
{
    m_snapLines.clear();
    if (!m_snapping)
        return {};
    const QSizeF c = doc().canvasSize;
    // Candidate lines: canvas edges and centre, other layers' edges and centres.
    QVector<double> xs{0, c.width() / 2, c.width()}, ys{0, c.height() / 2, c.height()};
    for (const DesignLayer &o : doc().layers) {
        if (o.id == movingId || !o.visible)
            continue;
        const QRectF r = m_renderer.canvasPolygon(o).boundingRect();
        xs << r.left() << r.center().x() << r.right();
        ys << r.top() << r.center().y() << r.bottom();
    }
    const double tol = 8 / viewTransform().m11();
    auto best = [&](const QVector<double> &lines, const std::array<double, 3> &edges, double *line) {
        double shift = 0, dist = tol;
        for (double l : lines) {
            for (double e : edges) {
                if (std::abs(l - e) < dist) {
                    dist = std::abs(l - e);
                    shift = l - e;
                    *line = l;
                }
            }
        }
        return shift;
    };
    double lx = -1, ly = -1;
    const double dx = best(xs, {box.left(), box.center().x(), box.right()}, &lx);
    const double dy = best(ys, {box.top(), box.center().y(), box.bottom()}, &ly);
    if (lx >= 0)
        m_snapLines << QLineF(lx, 0, lx, c.height());
    if (ly >= 0)
        m_snapLines << QLineF(0, ly, c.width(), ly);
    return {dx, dy};
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

    // Render through an offscreen buffer at display resolution so blend
    // modes composite the same way as in the exported image.
    {
        const qreal dpr = devicePixelRatioF();
        const QSize px = (canvasRect.size() * dpr).toSize().expandedTo(QSize(1, 1));
        QImage buf(px, QImage::Format_ARGB32_Premultiplied);
        buf.fill(Qt::transparent);
        QPainter bp(&buf);
        bp.setRenderHint(QPainter::Antialiasing);
        bp.scale(px.width() / double(doc().canvasSize.width()), px.height() / double(doc().canvasSize.height()));
        m_renderer.render(bp, doc());
        bp.end();
        buf.setDevicePixelRatio(dpr);
        p.drawImage(canvasRect.topLeft(), buf);
    }

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

    if (!m_snapLines.isEmpty()) {
        p.save();
        p.setTransform(vt);
        p.setPen(QPen(QColor(255, 60, 200), 0));
        for (const QLineF &l : m_snapLines)
            p.drawLine(l);
        p.restore();
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
               QStringLiteral("%1 × %2   %3%").arg(doc().canvasSize.width()).arg(doc().canvasSize.height())
                   .arg(int(std::lround(fitScale() * m_zoom * 100))));
}

void DesignCanvas::setBrush(bool enabled, const QColor &color, double width, bool eraser)
{
    m_brush = enabled;
    m_brushColor = color;
    m_brushWidth = width;
    m_eraser = eraser;
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void DesignCanvas::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && m_spaceHeld)) {
        m_drag = Drag::Pan;
        m_panStart = e->position();
        m_panOrig = m_pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    const QPointF cpos = toCanvas(e->position());
    m_pressCanvas = cpos;

    if (m_brush) {
        m_preview = m_project->data().design;
        m_previewActive = true;
        DesignLayer *target = m_preview.layer(m_selected);
        if (!target || target->type != LayerType::Drawing || target->locked) {
            DesignLayer l;
            l.id = QUuid::createUuid();
            l.type = LayerType::Drawing;
            l.name = tr("Drawing");
            l.shapeSize = m_preview.canvasSize;
            l.outlineWidth = 0;
            m_preview.layers.append(l);
            target = &m_preview.layers.last();
            m_selected = l.id;
            emit selectionChanged(m_selected);
        }
        BrushStroke st;
        st.color = m_brushColor;
        st.width = m_brushWidth / std::max(0.01, target->scale);
        st.eraser = m_eraser;
        st.points.append(m_renderer.layerTransform(*target).inverted().map(cpos));
        target->strokes.append(st);
        m_orig = *target;
        m_drag = Drag::Paint;
        update();
        return;
    }

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
    if (m_drag == Drag::Pan) {
        m_pan = m_panOrig + (e->position() - m_panStart);
        update();
        return;
    }
    if (m_drag == Drag::Paint) {
        if (DesignLayer *l = m_preview.layer(m_orig.id); l && !l->strokes.isEmpty()) {
            const QPointF pt = m_renderer.layerTransform(*l).inverted().map(toCanvas(e->position()));
            if (l->strokes.last().points.isEmpty() || QLineF(l->strokes.last().points.last(), pt).length() > 1.5)
                l->strokes.last().points.append(pt);
            update();
        }
        return;
    }
    if (m_brush)
        return;
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
        const QPointF delta = cpos - m_pressCanvas;
        l->position = m_orig.position + delta;
        // Snap the box to the canvas and other layers (hold Alt to move freely).
        if (!(e->modifiers() & Qt::AltModifier)) {
            const QRectF box = m_renderer.canvasPolygon(*l).boundingRect();
            l->position += snapMove(box, l->id);
        } else {
            m_snapLines.clear();
        }
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
    m_snapLines.clear();
    if (m_drag == Drag::Pan) {
        m_drag = Drag::None;
        setCursor(m_spaceHeld ? Qt::OpenHandCursor : (m_brush ? Qt::CrossCursor : Qt::ArrowCursor));
        return;
    }
    if (m_previewActive) {
        const DesignDocument result = m_preview;
        m_previewActive = false;
        m_project->edit(m_drag == Drag::Paint ? tr("Brush Stroke")
                                              : (m_drag == Drag::Scale ? tr("Scale Layer") : tr("Move Layer")),
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

void DesignCanvas::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const double steps = e->angleDelta().y() / 120.0;
        setZoom(m_zoom * std::pow(1.15, steps), e->position());
    } else {
        // Two-finger scroll / wheel pans (Shift: horizontal on a mouse wheel).
        QPointF d = e->pixelDelta().isNull() ? QPointF(e->angleDelta()) / 2.0 : QPointF(e->pixelDelta());
        if (e->modifiers() & Qt::ShiftModifier && d.x() == 0)
            d = QPointF(d.y(), 0);
        m_pan += d;
        update();
    }
    e->accept();
}

bool DesignCanvas::event(QEvent *e)
{
    // Arrow keys nudge the selected layer instead of moving the playhead.
    if (e->type() == QEvent::ShortcutOverride && !m_selected.isNull()) {
        auto *ke = static_cast<QKeyEvent *>(e);
        const int k = ke->key();
        if ((k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_Up || k == Qt::Key_Down)
            && !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            e->accept();
            return true;
        }
    }
    return QWidget::event(e);
}

void DesignCanvas::keyPressEvent(QKeyEvent *e)
{
    const double step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    switch (e->key()) {
    case Qt::Key_Left: nudgeSelected(-step, 0); return;
    case Qt::Key_Right: nudgeSelected(step, 0); return;
    case Qt::Key_Up: nudgeSelected(0, -step); return;
    case Qt::Key_Down: nudgeSelected(0, step); return;
    case Qt::Key_Space:
        if (!e->isAutoRepeat()) {
            m_spaceHeld = true;
            setCursor(Qt::OpenHandCursor);
        }
        return;
    default:
        QWidget::keyPressEvent(e);
    }
}

void DesignCanvas::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceHeld = false;
        setCursor(m_brush ? Qt::CrossCursor : Qt::ArrowCursor);
        return;
    }
    QWidget::keyReleaseEvent(e);
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
