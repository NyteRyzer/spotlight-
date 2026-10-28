#pragma once

#include "core/designrender.h"

#include <QUuid>
#include <QVector>
#include <QLineF>
#include <QWidget>

namespace spotlight {

class Project;

// Interactive Design Studio canvas: renders the document (with the same
// renderer used for export), selects layers, moves them by dragging and
// scales them with the corner handle. Image files can be dropped onto it.
class DesignCanvas : public QWidget {
    Q_OBJECT
public:
    explicit DesignCanvas(Project *project, QWidget *parent = nullptr);

    QUuid selectedLayer() const { return m_selected; }
    void setSelectedLayer(const QUuid &id);
    DesignRenderer *renderer() { return &m_renderer; }
    // Rule-of-thirds lines, centre lines and the YouTube timestamp zone.
    void setGuidesVisible(bool on)
    {
        m_guides = on;
        update();
    }
    // Brush tool: strokes paint onto the selected Drawing layer (a new
    // full-canvas Drawing layer is created if none is selected).
    void setBrush(bool enabled, const QColor &color, double width, bool eraser);
    bool brushEnabled() const { return m_brush; }

    // View zoom (1 = fit the canvas in the window) and pan. Ctrl+wheel
    // zooms at the cursor, the wheel / middle-drag / Space+drag pans.
    double zoom() const { return m_zoom; }
    void setZoom(double zoom, const QPointF &anchor = QPointF(-1, -1));
    void zoomIn() { setZoom(m_zoom * 1.25); }
    void zoomOut() { setZoom(m_zoom / 1.25); }
    void zoomToFit();
    // Snap moved layers to the canvas edges and centre and to other layers.
    void setSnapping(bool on) { m_snapping = on; }
    // Moves the selected layer by (dx, dy) canvas pixels (arrow keys).
    void nudgeSelected(double dx, double dy);

signals:
    void selectionChanged(const QUuid &layerId);
    void editTextRequested(const QUuid &layerId);
    void imagesDropped(const QStringList &paths, const QPointF &canvasPos);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    bool event(QEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    enum class Drag { None, Move, Scale, Paint, Pan };

    const DesignDocument &doc() const;
    QTransform viewTransform() const; // canvas -> widget
    double fitScale() const;
    // Snaps a moving layer's box (canvas coords) and records guide lines.
    QPointF snapMove(const QRectF &box, const QUuid &movingId);
    QPointF toCanvas(const QPointF &widgetPos) const;
    QRectF handleRect(const DesignLayer &l) const; // in widget coords

    Project *m_project;
    DesignRenderer m_renderer;
    QUuid m_selected;
    Drag m_drag = Drag::None;
    QPointF m_pressCanvas;
    DesignLayer m_orig;
    bool m_previewActive = false;
    bool m_guides = false;
    DesignDocument m_preview;
    bool m_brush = false;
    QColor m_brushColor = Qt::white;
    double m_brushWidth = 12.0;
    bool m_eraser = false;
    double m_zoom = 1.0;
    QPointF m_pan;          // widget pixels
    QPointF m_panStart;
    QPointF m_panOrig;
    bool m_spaceHeld = false;
    bool m_snapping = true;
    QVector<QLineF> m_snapLines; // canvas coords, shown while dragging
};

} // namespace spotlight
