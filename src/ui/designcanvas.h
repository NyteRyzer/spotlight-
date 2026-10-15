#pragma once

#include "core/designrender.h"

#include <QUuid>
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
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    enum class Drag { None, Move, Scale };

    const DesignDocument &doc() const;
    QTransform viewTransform() const; // canvas -> widget
    QPointF toCanvas(const QPointF &widgetPos) const;
    QRectF handleRect(const DesignLayer &l) const; // in widget coords

    Project *m_project;
    DesignRenderer m_renderer;
    QUuid m_selected;
    Drag m_drag = Drag::None;
    QPointF m_pressCanvas;
    DesignLayer m_orig;
    bool m_previewActive = false;
    DesignDocument m_preview;
};

} // namespace spotlight
