#include "core/designrender.h"

#include "core/imageops.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QtMath>

#include <cmath>

namespace spotlight {

QImage DesignRenderer::image(const QString &path)
{
    auto it = m_images.constFind(path);
    if (it != m_images.constEnd())
        return *it;
    QImageReader r(path);
    r.setAutoTransform(true);
    QImage img = r.read();
    if (!img.isNull())
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_images.insert(path, img);
    return img;
}

QImage DesignRenderer::adjustedImage(const DesignLayer &l)
{
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(l.imagePath).arg(l.brightness).arg(l.contrast).arg(l.saturation);
    auto it = m_adjusted.constFind(key);
    if (it != m_adjusted.constEnd())
        return *it;
    QImage img = adjustImage(image(l.imagePath), l.brightness, l.contrast, l.saturation);
    if (m_adjusted.size() > 64)
        m_adjusted.clear();
    m_adjusted.insert(key, img);
    return img;
}

QPainterPath DesignRenderer::textPath(const DesignLayer &l)
{
    // Without an explicit family, use the platform's UI font (exists everywhere).
    QFont font = l.fontFamily.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::GeneralFont) : QFont(l.fontFamily);
    font.setPixelSize(std::max(4, l.fontSize));
    font.setBold(l.bold);
    const QFontMetricsF fm(font);
    QPainterPath path;
    const QStringList lines = l.text.split(QLatin1Char('\n'));
    qreal y = fm.ascent();
    for (const QString &line : lines) {
        path.addText(QPointF(0, y), font, line);
        y += fm.lineSpacing();
    }
    // Normalise to start at the origin, leaving room for the outline.
    const qreal pad = l.outlineWidth;
    const QRectF b = path.boundingRect();
    path.translate(-b.left() + pad, -b.top() + pad);
    return path;
}

QRectF DesignRenderer::localRect(const DesignLayer &l)
{
    switch (l.type) {
    case LayerType::Image: {
        const QImage img = image(l.imagePath);
        return QRectF(QPointF(0, 0), img.isNull() ? QSizeF(400, 300) : QSizeF(img.size()));
    }
    case LayerType::Text: {
        const QRectF b = textPath(l).boundingRect();
        const qreal pad = l.outlineWidth;
        return QRectF(0, 0, std::max<qreal>(10, b.right() + pad), std::max<qreal>(10, b.bottom() + pad));
    }
    case LayerType::Shape:
        return QRectF(QPointF(0, 0), QSizeF(l.shapeSize));
    }
    return {};
}

QTransform DesignRenderer::layerTransform(const DesignLayer &l)
{
    const QRectF r = localRect(l);
    const qreal s = l.scale;
    QTransform t;
    t.translate(l.position.x() + r.width() * s / 2, l.position.y() + r.height() * s / 2);
    t.rotate(l.rotation);
    t.scale(s, s);
    t.translate(-r.width() / 2, -r.height() / 2);
    return t;
}

QPolygonF DesignRenderer::canvasPolygon(const DesignLayer &l)
{
    return layerTransform(l).map(QPolygonF(localRect(l)));
}

void DesignRenderer::renderLayer(QPainter &p, const DesignLayer &l)
{
    if (!l.visible || l.opacity <= 0)
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(l.opacity);
    p.setTransform(layerTransform(l), true);

    switch (l.type) {
    case LayerType::Image: {
        const QImage img = adjustedImage(l);
        if (img.isNull()) {
            p.fillRect(localRect(l), QColor(80, 80, 90));
            p.setPen(Qt::white);
            p.drawText(localRect(l), Qt::AlignCenter, QStringLiteral("Missing image"));
        } else {
            p.drawImage(QPointF(0, 0), img);
        }
        break;
    }
    case LayerType::Text: {
        const QPainterPath path = textPath(l);
        if (l.shadow) {
            const qreal off = std::max(2.0, l.fontSize / 18.0);
            p.fillPath(path.translated(off, off), QColor(0, 0, 0, 150));
            if (l.outlineWidth > 0)
                p.strokePath(path.translated(off, off),
                             QPen(QColor(0, 0, 0, 150), l.outlineWidth * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        if (l.outlineWidth > 0)
            p.strokePath(path, QPen(l.outlineColor, l.outlineWidth * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.fillPath(path, l.textColor);
        break;
    }
    case LayerType::Shape: {
        p.setPen(Qt::NoPen);
        const QRectF r = localRect(l);
        if (l.gradient) {
            // Gradient line through the centre at gradientAngle, spanning the
            // shape's extent in that direction.
            const double a = qDegreesToRadians(l.gradientAngle);
            const QPointF dir(std::cos(a), std::sin(a));
            const double half = (std::abs(dir.x()) * r.width() + std::abs(dir.y()) * r.height()) / 2;
            QLinearGradient g(r.center() - dir * half, r.center() + dir * half);
            g.setColorAt(0, l.fillColor);
            g.setColorAt(1, l.fillColor2);
            p.setBrush(g);
        } else {
            p.setBrush(l.fillColor);
        }
        if (l.shape == ShapeKind::Ellipse)
            p.drawEllipse(r);
        else
            p.drawRoundedRect(r, l.cornerRadius, l.cornerRadius);
        break;
    }
    }
    p.restore();
}

void DesignRenderer::render(QPainter &p, const DesignDocument &doc)
{
    p.fillRect(QRect(QPoint(0, 0), doc.canvasSize), doc.background);
    for (const auto &l : doc.layers)
        renderLayer(p, l);
}

QImage DesignRenderer::renderToImage(const DesignDocument &doc)
{
    QImage img(doc.canvasSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    render(p, doc);
    p.end();
    return img;
}

QUuid DesignRenderer::layerAt(const DesignDocument &doc, const QPointF &pos)
{
    for (int i = doc.layers.size() - 1; i >= 0; --i) {
        const auto &l = doc.layers[i];
        if (l.visible && canvasPolygon(l).containsPoint(pos, Qt::OddEvenFill))
            return l.id;
    }
    return {};
}

} // namespace spotlight
