#pragma once

#include "core/design.h"

#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QTransform>

class QPainter;

namespace spotlight {

// Renders Design Studio documents. The on-screen canvas and the PNG/JPG
// export use the same code, so what you see is exactly what you export.
class DesignRenderer {
public:
    // Natural (unscaled, unrotated) size of a layer's content at the origin.
    QRectF localRect(const DesignLayer &layer);

    // Maps layer-local coordinates to canvas coordinates.
    QTransform layerTransform(const DesignLayer &layer);

    // Polygon of the layer's box on the canvas, for hit testing and outlines.
    QPolygonF canvasPolygon(const DesignLayer &layer);

    void renderLayer(QPainter &p, const DesignLayer &layer);
    void render(QPainter &p, const DesignDocument &doc);
    QImage renderToImage(const DesignDocument &doc);

    // Topmost visible layer at a canvas position, or a null id.
    QUuid layerAt(const DesignDocument &doc, const QPointF &canvasPos);

    void clearCache() { m_images.clear(); m_adjusted.clear(); }

private:
    QImage image(const QString &path);
    QImage adjustedImage(const DesignLayer &layer);
    QPainterPath textPath(const DesignLayer &layer);

    QHash<QString, QImage> m_images;
    QHash<QString, QImage> m_adjusted; // key includes adjustment values
};

} // namespace spotlight
