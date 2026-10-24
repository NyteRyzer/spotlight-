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

    void clearCache()
    {
        m_images.clear();
        m_adjusted.clear();
        m_composed.clear();
    }

    // Path of a shape layer's outline in local coordinates.
    static QPainterPath shapePath(const DesignLayer &layer);

private:
    struct TextLayout {
        QPainterPath path;
        QVector<QRectF> boxes; // per-line background boxes
        QRectF bounds;         // everything, including outline and boxes
    };
    QImage image(const QString &path);
    // The image layer's pixels after crop, adjustments, filters, background
    // removal and fade mask.
    QImage adjustedImage(const DesignLayer &layer);
    TextLayout textLayout(const DesignLayer &layer);
    QImage drawingImage(const DesignLayer &layer);
    // Draws the layer's own content (no effects) in local coordinates.
    void paintContent(QPainter &p, const DesignLayer &layer);
    // Whether the layer must be composited off-screen (effects/blending).
    static bool needsComposite(const DesignLayer &layer);
    // Content + glow/shadow/stroke rendered at the layer's scale, with
    // `padding` transparent pixels around it.
    QImage composedImage(const DesignLayer &layer, int *padding);

    QHash<QString, QImage> m_images;
    QHash<QString, QImage> m_adjusted; // key includes adjustment values
    QHash<QString, QPair<QImage, int>> m_composed;
};

} // namespace spotlight
