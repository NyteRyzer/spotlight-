#include "core/designrender.h"

#include "core/imageops.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImageReader>
#include <QJsonDocument>
#include <QLinearGradient>
#include <QPainter>
#include <QtMath>

#include <cmath>

namespace spotlight {

namespace {

QPainter::CompositionMode compositionMode(BlendMode m)
{
    switch (m) {
    case BlendMode::Normal: return QPainter::CompositionMode_SourceOver;
    case BlendMode::Multiply: return QPainter::CompositionMode_Multiply;
    case BlendMode::Screen: return QPainter::CompositionMode_Screen;
    case BlendMode::Overlay: return QPainter::CompositionMode_Overlay;
    case BlendMode::Darken: return QPainter::CompositionMode_Darken;
    case BlendMode::Lighten: return QPainter::CompositionMode_Lighten;
    case BlendMode::ColorDodge: return QPainter::CompositionMode_ColorDodge;
    case BlendMode::ColorBurn: return QPainter::CompositionMode_ColorBurn;
    case BlendMode::HardLight: return QPainter::CompositionMode_HardLight;
    case BlendMode::SoftLight: return QPainter::CompositionMode_SoftLight;
    case BlendMode::Difference: return QPainter::CompositionMode_Difference;
    case BlendMode::Exclusion: return QPainter::CompositionMode_Exclusion;
    }
    return QPainter::CompositionMode_SourceOver;
}

} // namespace

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
    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
                            .arg(l.imagePath)
                            .arg(l.brightness)
                            .arg(l.contrast)
                            .arg(l.saturation)
                            .arg(l.hue)
                            .arg(l.blur)
                            .arg(int(l.grayscale) | int(l.invert) << 1 | int(l.removeBg) << 2)
                            .arg(QStringLiteral("%1,%2,%3,%4,%5").arg(l.removeBgTolerance).arg(l.cropLeft).arg(l.cropTop)
                                     .arg(l.cropRight).arg(l.cropBottom))
                            .arg(QStringLiteral("%1,%2,%3,%4,%5")
                                     .arg(l.fadeDirection)
                                     .arg(l.fadeAmount)
                                     .arg(int(l.tint))
                                     .arg(l.tintColor.rgba())
                                     .arg(l.tintAmount));
    auto it = m_adjusted.constFind(key);
    if (it != m_adjusted.constEnd())
        return *it;
    QImage img = image(l.imagePath);
    if (!img.isNull()) {
        if (l.cropLeft > 0 || l.cropTop > 0 || l.cropRight > 0 || l.cropBottom > 0) {
            const double cl = std::clamp(l.cropLeft, 0.0, 0.95), ct = std::clamp(l.cropTop, 0.0, 0.95);
            const double cr = std::clamp(l.cropRight, 0.0, 0.95 - cl), cb = std::clamp(l.cropBottom, 0.0, 0.95 - ct);
            const QRect r(int(img.width() * cl), int(img.height() * ct),
                          std::max(1, int(img.width() * (1 - cl - cr))), std::max(1, int(img.height() * (1 - ct - cb))));
            img = img.copy(r);
        }
        if (l.removeBg)
            img = removeBackground(img, l.removeBgTolerance);
        img = adjustImage(img, l.brightness, l.contrast, l.grayscale ? 0.0 : l.saturation);
        img = hueRotate(img, l.hue);
        if (l.invert)
            img = invertImage(img);
        if (l.blur > 0)
            img = blurImage(img, l.blur);
        if (l.tint && l.tintAmount > 0) {
            // Colour overlay that keeps the image's light and shade.
            QImage mono = adjustImage(img, 0.0, 1.0, 0.0).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QPainter tp(&mono);
            tp.setCompositionMode(QPainter::CompositionMode_Multiply);
            tp.fillRect(mono.rect(), l.tintColor);
            tp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            tp.drawImage(0, 0, img.convertToFormat(QImage::Format_ARGB32_Premultiplied));
            tp.end();
            QImage blended = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QPainter bp(&blended);
            bp.setOpacity(std::clamp(l.tintAmount, 0.0, 1.0));
            bp.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            bp.drawImage(0, 0, mono);
            bp.end();
            img = blended;
        }
        if (l.fadeAmount > 0)
            img = fadeMask(img, l.fadeDirection, l.fadeAmount);
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    if (m_adjusted.size() > 64)
        m_adjusted.clear();
    m_adjusted.insert(key, img);
    return img;
}

DesignRenderer::TextLayout DesignRenderer::textLayout(const DesignLayer &l)
{
    // Without an explicit family, use the platform's UI font (exists everywhere).
    QFont font = l.fontFamily.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::GeneralFont) : QFont(l.fontFamily);
    font.setPixelSize(std::max(4, l.fontSize));
    font.setBold(l.bold);
    font.setItalic(l.italic);
    if (!qFuzzyIsNull(l.letterSpacing))
        font.setLetterSpacing(QFont::AbsoluteSpacing, l.letterSpacing / 100.0 * l.fontSize);
    const QFontMetricsF fm(font);
    const QStringList lines = l.text.split(QLatin1Char('\n'));
    const qreal lineStep = fm.lineSpacing() * std::clamp(l.lineHeight, 0.5, 3.0);

    QVector<QPainterPath> linePaths;
    qreal maxW = 0;
    for (const QString &line : lines) {
        QPainterPath lp;
        lp.addText(QPointF(0, 0), font, line);
        linePaths.append(lp);
        maxW = std::max(maxW, fm.horizontalAdvance(line));
    }
    TextLayout t;
    const qreal boxPad = l.textBox ? l.textBoxPadding * l.fontSize : 0;
    qreal y = fm.ascent();
    for (int i = 0; i < lines.size(); ++i) {
        const qreal w = fm.horizontalAdvance(lines[i]);
        const qreal x = l.align == 1 ? (maxW - w) / 2 : (l.align == 2 ? maxW - w : 0);
        t.path.addPath(linePaths[i].translated(x, y));
        if (l.textBox && !lines[i].trimmed().isEmpty())
            t.boxes.append(QRectF(x - boxPad, y - fm.ascent() - boxPad * 0.5, w + 2 * boxPad,
                                  fm.ascent() + fm.descent() + boxPad));
        y += lineStep;
    }
    // Normalise so everything starts at the origin, leaving room for the outline.
    const qreal pad = l.outlineWidth;
    QRectF b = t.path.boundingRect().adjusted(-pad, -pad, pad, pad);
    for (const auto &bx : t.boxes)
        b = b.united(bx);
    const QPointF shift(-b.left(), -b.top());
    t.path.translate(shift);
    for (auto &bx : t.boxes)
        bx.translate(shift);
    t.bounds = QRectF(0, 0, std::max<qreal>(10, b.width()), std::max<qreal>(10, b.height()));
    return t;
}

QPainterPath DesignRenderer::shapePath(const DesignLayer &l)
{
    const QRectF r(QPointF(0, 0), QSizeF(l.shapeSize));
    QPainterPath path;
    switch (l.shape) {
    case ShapeKind::Rectangle:
        path.addRoundedRect(r, l.cornerRadius, l.cornerRadius);
        break;
    case ShapeKind::Ellipse:
        path.addEllipse(r);
        break;
    case ShapeKind::Triangle:
        path.moveTo(r.center().x(), r.top());
        path.lineTo(r.bottomRight());
        path.lineTo(r.bottomLeft());
        path.closeSubpath();
        break;
    case ShapeKind::Star: {
        const QPointF c = r.center();
        const double rx = r.width() / 2, ry = r.height() / 2;
        for (int i = 0; i < 10; ++i) {
            const double a = -M_PI / 2 + i * M_PI / 5;
            const double k = i % 2 == 0 ? 1.0 : 0.42;
            const QPointF pt(c.x() + std::cos(a) * rx * k, c.y() + std::sin(a) * ry * k);
            if (i == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
        path.closeSubpath();
        break;
    }
    case ShapeKind::Arrow: {
        const double w = r.width(), h = r.height();
        const double head = std::min(w * 0.45, h * 1.2);
        const double shaft = h * 0.38;
        path.moveTo(0, h / 2 - shaft / 2);
        path.lineTo(w - head, h / 2 - shaft / 2);
        path.lineTo(w - head, 0);
        path.lineTo(w, h / 2);
        path.lineTo(w - head, h);
        path.lineTo(w - head, h / 2 + shaft / 2);
        path.lineTo(0, h / 2 + shaft / 2);
        path.closeSubpath();
        break;
    }
    case ShapeKind::Line:
        path.addRoundedRect(r, r.height() / 2, r.height() / 2);
        break;
    case ShapeKind::SpeechBubble: {
        // Rounded body over the top 78 %, tail at the lower left.
        const QRectF body(r.left(), r.top(), r.width(), r.height() * 0.78);
        const double rad = std::min(body.width(), body.height()) * 0.22;
        path.addRoundedRect(body, rad, rad);
        QPainterPath tail;
        tail.moveTo(r.left() + r.width() * 0.18, body.bottom() - 2);
        tail.lineTo(r.left() + r.width() * 0.10, r.bottom());
        tail.lineTo(r.left() + r.width() * 0.38, body.bottom() - 2);
        tail.closeSubpath();
        path = path.united(tail);
        break;
    }
    case ShapeKind::Burst: {
        // A 14-point "sale sticker" starburst.
        const QPointF c = r.center();
        const double rx = r.width() / 2, ry = r.height() / 2;
        const int points = 14;
        for (int i = 0; i < points * 2; ++i) {
            const double a = -M_PI / 2 + i * M_PI / points;
            const double k = i % 2 == 0 ? 1.0 : 0.78;
            const QPointF pt(c.x() + std::cos(a) * rx * k, c.y() + std::sin(a) * ry * k);
            if (i == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
        path.closeSubpath();
        break;
    }
    }
    return path;
}

QImage DesignRenderer::drawingImage(const DesignLayer &l)
{
    QImage img(l.shapeSize.expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    for (const auto &st : l.strokes) {
        if (st.points.isEmpty())
            continue;
        p.setCompositionMode(st.eraser ? QPainter::CompositionMode_Clear : QPainter::CompositionMode_SourceOver);
        QPen pen(st.color, st.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        if (st.points.size() == 1) {
            p.drawPoint(st.points.first());
            continue;
        }
        QPainterPath path(st.points.first());
        for (int i = 1; i < st.points.size(); ++i)
            path.lineTo(st.points[i]);
        p.drawPath(path);
    }
    return img;
}

QRectF DesignRenderer::localRect(const DesignLayer &l)
{
    switch (l.type) {
    case LayerType::Image: {
        const QImage img = adjustedImage(l);
        return QRectF(QPointF(0, 0), img.isNull() ? QSizeF(400, 300) : QSizeF(img.size()));
    }
    case LayerType::Text:
        return textLayout(l).bounds;
    case LayerType::Shape:
    case LayerType::Drawing:
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
    t.scale(s * (l.flipH ? -1 : 1), s * (l.flipV ? -1 : 1));
    t.translate(-r.width() / 2, -r.height() / 2);
    return t;
}

void DesignRenderer::resizeDocument(DesignDocument &doc, const QSize &size)
{
    const QSizeF old = doc.canvasSize;
    if (old.isEmpty() || size.isEmpty() || QSize(old.toSize()) == size) {
        doc.canvasSize = size;
        return;
    }
    const double fit = std::min(size.width() / old.width(), size.height() / old.height());
    const double cover = std::max(size.width() / old.width(), size.height() / old.height());
    const QPointF oldCentre(old.width() / 2, old.height() / 2);
    const QPointF newCentre(size.width() / 2.0, size.height() / 2.0);
    const QRectF canvas(QPointF(0, 0), old);
    for (DesignLayer &l : doc.layers) {
        const QRectF bounds = canvasPolygon(l).boundingRect();
        const QRectF covered = bounds.intersected(canvas);
        const bool background = covered.width() * covered.height() >= 0.9 * old.width() * old.height();
        const double f = background ? cover : fit;
        const QRectF r = localRect(l);
        const QPointF centre = l.position + QPointF(r.width() * l.scale / 2, r.height() * l.scale / 2);
        const QPointF moved = newCentre + (centre - oldCentre) * f;
        l.scale *= f;
        l.position = moved - QPointF(r.width() * l.scale / 2, r.height() * l.scale / 2);
    }
    doc.canvasSize = size;
}

QPolygonF DesignRenderer::canvasPolygon(const DesignLayer &l)
{
    return layerTransform(l).map(QPolygonF(localRect(l)));
}

void DesignRenderer::paintContent(QPainter &p, const DesignLayer &l)
{
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
        const TextLayout t = textLayout(l);
        if (l.textBox) {
            p.setPen(Qt::NoPen);
            p.setBrush(l.textBoxColor);
            const qreal rad = l.fontSize * 0.12;
            for (const auto &bx : t.boxes)
                p.drawRoundedRect(bx, rad, rad);
        }
        const QPainterPath &path = t.path;
        if (l.shadow) {
            const qreal off = std::max(2.0, l.fontSize / 18.0);
            p.fillPath(path.translated(off, off), QColor(0, 0, 0, 150));
            if (l.outlineWidth > 0)
                p.strokePath(path.translated(off, off),
                             QPen(QColor(0, 0, 0, 150), l.outlineWidth * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        if (l.outlineWidth > 0)
            p.strokePath(path, QPen(l.outlineColor, l.outlineWidth * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (l.textGradient) {
            const QRectF r = path.boundingRect();
            const double a = qDegreesToRadians(l.textGradientAngle);
            const QPointF dir(std::cos(a), std::sin(a));
            const double half = (std::abs(dir.x()) * r.width() + std::abs(dir.y()) * r.height()) / 2;
            QLinearGradient g(r.center() - dir * half, r.center() + dir * half);
            g.setColorAt(0, l.textColor);
            g.setColorAt(1, l.textColor2);
            p.fillPath(path, g);
        } else {
            p.fillPath(path, l.textColor);
        }
        break;
    }
    case LayerType::Shape: {
        const QRectF r = localRect(l);
        const QPainterPath path = shapePath(l);
        if (l.strokeWidth > 0)
            p.strokePath(path, QPen(l.strokeColor, l.strokeWidth * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (l.gradient) {
            // Gradient line through the centre at gradientAngle, spanning the
            // shape's extent in that direction.
            const double a = qDegreesToRadians(l.gradientAngle);
            const QPointF dir(std::cos(a), std::sin(a));
            const double half = (std::abs(dir.x()) * r.width() + std::abs(dir.y()) * r.height()) / 2;
            QLinearGradient g(r.center() - dir * half, r.center() + dir * half);
            g.setColorAt(0, l.fillColor);
            g.setColorAt(1, l.fillColor2);
            p.fillPath(path, g);
        } else {
            p.fillPath(path, l.fillColor);
        }
        break;
    }
    case LayerType::Drawing:
        p.drawImage(QPointF(0, 0), drawingImage(l));
        break;
    }
}

bool DesignRenderer::needsComposite(const DesignLayer &l)
{
    return l.dropShadow || l.glow || l.blend != BlendMode::Normal
        || (l.strokeWidth > 0 && (l.type == LayerType::Image || l.type == LayerType::Drawing));
}

QImage DesignRenderer::composedImage(const DesignLayer &l, int *padding)
{
    // Position, rotation and opacity are applied when drawing, so they are
    // not part of the cache key: dragging a layer stays fast.
    QJsonObject o = l.toJson();
    for (const char *k : {"x", "y", "rotation", "opacity", "visible", "name", "locked", "blend"})
        o.remove(QLatin1String(k));
    const QString key = QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    auto it = m_composed.constFind(key);
    if (it != m_composed.constEnd()) {
        *padding = it->second;
        return it->first;
    }

    const QRectF r = localRect(l);
    const double s = std::max(0.01, l.scale);
    const int stroke = (l.type == LayerType::Image || l.type == LayerType::Drawing) ? int(std::ceil(l.strokeWidth)) : 0;
    int pad = stroke + 2;
    if (l.glow)
        pad = std::max(pad, int(l.glowSize * 2.5) + stroke + 2);
    if (l.dropShadow)
        pad = std::max(pad, int(l.shadowBlur * 2.5 + l.shadowDistance) + stroke + 2);
    if (l.type == LayerType::Text || l.type == LayerType::Shape)
        pad += int(std::ceil(std::max(l.outlineWidth, l.strokeWidth) * s));
    const QSize contentSize(std::max(1, int(std::ceil(r.width() * s))), std::max(1, int(std::ceil(r.height() * s))));
    const QSize full = contentSize + QSize(2 * pad, 2 * pad);

    QImage content(full, QImage::Format_ARGB32_Premultiplied);
    content.fill(Qt::transparent);
    {
        QPainter cp(&content);
        cp.setRenderHint(QPainter::Antialiasing);
        cp.setRenderHint(QPainter::SmoothPixmapTransform);
        cp.translate(pad, pad);
        cp.scale(s, s);
        paintContent(cp, l);
    }
    if (stroke > 0) {
        const QImage outline = alphaOutline(content, l.strokeWidth, l.strokeColor);
        QImage merged = outline.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter mp(&merged);
        mp.drawImage(0, 0, content);
        mp.end();
        content = merged;
    }

    QImage out(full, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    if (l.glow) {
        QImage g = blurImage(tintedMask(content, l.glowColor), l.glowSize / 2.0);
        // Two passes make the glow read as a glow rather than a faint haze.
        p.drawImage(0, 0, g);
        p.drawImage(0, 0, g);
    }
    if (l.dropShadow) {
        const double a = qDegreesToRadians(l.shadowAngle);
        const QPointF off(std::cos(a) * l.shadowDistance, std::sin(a) * l.shadowDistance);
        p.drawImage(off, blurImage(tintedMask(content, l.shadowColor), l.shadowBlur / 2.0));
    }
    p.drawImage(0, 0, content);
    p.end();

    if (m_composed.size() > 48)
        m_composed.clear();
    m_composed.insert(key, {out, pad});
    *padding = pad;
    return out;
}

void DesignRenderer::renderLayer(QPainter &p, const DesignLayer &l)
{
    if (!l.visible || l.opacity <= 0)
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(l.opacity);

    if (needsComposite(l)) {
        int pad = 0;
        const QImage img = composedImage(l, &pad);
        const QRectF r = localRect(l);
        const qreal s = l.scale;
        QTransform t;
        t.translate(l.position.x() + r.width() * s / 2, l.position.y() + r.height() * s / 2);
        t.rotate(l.rotation);
        t.scale(l.flipH ? -1 : 1, l.flipV ? -1 : 1);
        t.translate(-r.width() * s / 2 - pad, -r.height() * s / 2 - pad);
        p.setTransform(t, true);
        p.setCompositionMode(compositionMode(l.blend));
        p.drawImage(QPointF(0, 0), img);
    } else {
        p.setTransform(layerTransform(l), true);
        paintContent(p, l);
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
        if (l.visible && !l.locked && canvasPolygon(l).containsPoint(pos, Qt::OddEvenFill))
            return l.id;
    }
    return {};
}

} // namespace spotlight
