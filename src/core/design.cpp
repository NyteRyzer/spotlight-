#include "core/design.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace spotlight {

namespace {

QString colorToString(const QColor &c)
{
    return c.name(QColor::HexArgb);
}

QColor colorFromValue(const QJsonValue &v, const QColor &fallback)
{
    const QColor c(v.toString());
    return c.isValid() ? c : fallback;
}

} // namespace

QString layerTypeToString(LayerType t)
{
    switch (t) {
    case LayerType::Image: return QStringLiteral("image");
    case LayerType::Text: return QStringLiteral("text");
    case LayerType::Shape: return QStringLiteral("shape");
    case LayerType::Drawing: return QStringLiteral("drawing");
    }
    return QStringLiteral("image");
}

LayerType layerTypeFromString(const QString &s)
{
    if (s == QLatin1String("text"))
        return LayerType::Text;
    if (s == QLatin1String("shape"))
        return LayerType::Shape;
    if (s == QLatin1String("drawing"))
        return LayerType::Drawing;
    return LayerType::Image;
}

QStringList blendModeNames()
{
    return {QStringLiteral("Normal"),      QStringLiteral("Multiply"),    QStringLiteral("Screen"),
            QStringLiteral("Overlay"),     QStringLiteral("Darken"),      QStringLiteral("Lighten"),
            QStringLiteral("Color Dodge"), QStringLiteral("Color Burn"),  QStringLiteral("Hard Light"),
            QStringLiteral("Soft Light"),  QStringLiteral("Difference"),  QStringLiteral("Exclusion")};
}

QStringList shapeKindNames()
{
    return {QStringLiteral("Rectangle"), QStringLiteral("Ellipse"), QStringLiteral("Triangle"),
            QStringLiteral("Star"),      QStringLiteral("Arrow"),   QStringLiteral("Line"),
            QStringLiteral("Speech Bubble"), QStringLiteral("Burst")};
}

namespace {
const char *const kShapeIds[] = {"rectangle", "ellipse", "triangle", "star", "arrow", "line", "speech", "burst"};
}

QJsonObject DesignLayer::toJson() const
{
    QJsonObject o;
    o["id"] = id.toString(QUuid::WithoutBraces);
    o["type"] = layerTypeToString(type);
    o["name"] = name;
    o["visible"] = visible;
    o["opacity"] = opacity;
    o["x"] = position.x();
    o["y"] = position.y();
    o["scale"] = scale;
    o["rotation"] = rotation;
    if (blend != BlendMode::Normal)
        o["blend"] = int(blend);
    if (flipH)
        o["flipH"] = true;
    if (flipV)
        o["flipV"] = true;
    if (locked)
        o["locked"] = true;
    if (dropShadow) {
        o["dropShadow"] = true;
        o["shadowColor"] = colorToString(shadowColor);
        o["shadowBlur"] = shadowBlur;
        o["shadowDistance"] = shadowDistance;
        o["shadowAngle"] = shadowAngle;
    }
    if (glow) {
        o["glow"] = true;
        o["glowColor"] = colorToString(glowColor);
        o["glowSize"] = glowSize;
    }
    if (strokeWidth > 0) {
        o["strokeWidth"] = strokeWidth;
        o["strokeColor"] = colorToString(strokeColor);
    }
    switch (type) {
    case LayerType::Image:
        o["imagePath"] = imagePath;
        o["brightness"] = brightness;
        o["contrast"] = contrast;
        o["saturation"] = saturation;
        o["hue"] = hue;
        o["blur"] = blur;
        o["grayscale"] = grayscale;
        o["invert"] = invert;
        o["removeBg"] = removeBg;
        o["removeBgTolerance"] = removeBgTolerance;
        o["cropL"] = cropLeft;
        o["cropT"] = cropTop;
        o["cropR"] = cropRight;
        o["cropB"] = cropBottom;
        o["fadeDirection"] = fadeDirection;
        o["fadeAmount"] = fadeAmount;
        o["tint"] = tint;
        o["tintColor"] = colorToString(tintColor);
        o["tintAmount"] = tintAmount;
        break;
    case LayerType::Text:
        o["text"] = text;
        o["fontFamily"] = fontFamily;
        o["fontSize"] = fontSize;
        o["bold"] = bold;
        o["textColor"] = colorToString(textColor);
        o["outlineColor"] = colorToString(outlineColor);
        o["outlineWidth"] = outlineWidth;
        o["shadow"] = shadow;
        o["italic"] = italic;
        o["letterSpacing"] = letterSpacing;
        o["align"] = align;
        o["lineHeight"] = lineHeight;
        o["textGradient"] = textGradient;
        o["textColor2"] = colorToString(textColor2);
        o["textGradientAngle"] = textGradientAngle;
        o["textBox"] = textBox;
        o["textBoxColor"] = colorToString(textBoxColor);
        o["textBoxPadding"] = textBoxPadding;
        break;
    case LayerType::Shape:
        o["shape"] = QString::fromLatin1(kShapeIds[int(shape)]);
        o["width"] = shapeSize.width();
        o["height"] = shapeSize.height();
        o["fillColor"] = colorToString(fillColor);
        o["cornerRadius"] = cornerRadius;
        o["gradient"] = gradient;
        o["fillColor2"] = colorToString(fillColor2);
        o["gradientAngle"] = gradientAngle;
        break;
    case LayerType::Drawing: {
        o["width"] = shapeSize.width();
        o["height"] = shapeSize.height();
        QJsonArray arr;
        for (const auto &st : strokes) {
            QJsonObject so;
            so["color"] = colorToString(st.color);
            so["width"] = st.width;
            if (st.eraser)
                so["eraser"] = true;
            QJsonArray pts;
            for (const auto &pt : st.points) {
                pts.append(std::round(pt.x() * 10) / 10);
                pts.append(std::round(pt.y() * 10) / 10);
            }
            so["points"] = pts;
            arr.append(so);
        }
        o["strokes"] = arr;
        break;
    }
    }
    return o;
}

DesignLayer DesignLayer::fromJson(const QJsonObject &o)
{
    DesignLayer l;
    l.id = QUuid::fromString(o["id"].toString());
    if (l.id.isNull())
        l.id = QUuid::createUuid();
    l.type = layerTypeFromString(o["type"].toString());
    l.name = o["name"].toString();
    l.visible = o["visible"].toBool(true);
    l.opacity = o["opacity"].toDouble(1.0);
    l.position = QPointF(o["x"].toDouble(), o["y"].toDouble());
    l.scale = o["scale"].toDouble(1.0);
    l.rotation = o["rotation"].toDouble();
    l.blend = BlendMode(std::clamp(o["blend"].toInt(0), 0, int(BlendMode::Exclusion)));
    l.flipH = o["flipH"].toBool();
    l.flipV = o["flipV"].toBool();
    l.locked = o["locked"].toBool();
    l.dropShadow = o["dropShadow"].toBool();
    l.shadowColor = colorFromValue(o["shadowColor"], QColor(0, 0, 0, 180));
    l.shadowBlur = o["shadowBlur"].toDouble(12.0);
    l.shadowDistance = o["shadowDistance"].toDouble(10.0);
    l.shadowAngle = o["shadowAngle"].toDouble(45.0);
    l.glow = o["glow"].toBool();
    l.glowColor = colorFromValue(o["glowColor"], QColor(255, 220, 60));
    l.glowSize = o["glowSize"].toDouble(18.0);
    l.strokeWidth = o["strokeWidth"].toDouble(0.0);
    l.strokeColor = colorFromValue(o["strokeColor"], Qt::white);
    l.imagePath = o["imagePath"].toString();
    l.brightness = o["brightness"].toDouble(0.0);
    l.contrast = o["contrast"].toDouble(1.0);
    l.saturation = o["saturation"].toDouble(1.0);
    l.hue = o["hue"].toDouble(0.0);
    l.blur = o["blur"].toDouble(0.0);
    l.grayscale = o["grayscale"].toBool();
    l.invert = o["invert"].toBool();
    l.removeBg = o["removeBg"].toBool();
    l.removeBgTolerance = o["removeBgTolerance"].toDouble(0.12);
    l.cropLeft = o["cropL"].toDouble(0.0);
    l.cropTop = o["cropT"].toDouble(0.0);
    l.cropRight = o["cropR"].toDouble(0.0);
    l.cropBottom = o["cropB"].toDouble(0.0);
    l.fadeDirection = o["fadeDirection"].toInt(0);
    l.fadeAmount = o["fadeAmount"].toDouble(0.0);
    l.tint = o["tint"].toBool();
    l.tintColor = colorFromValue(o["tintColor"], QColor(255, 120, 0));
    l.tintAmount = o["tintAmount"].toDouble(0.5);
    l.text = o["text"].toString();
    l.fontFamily = o["fontFamily"].toString();
    l.fontSize = o["fontSize"].toInt(96);
    l.bold = o["bold"].toBool(true);
    l.textColor = colorFromValue(o["textColor"], Qt::white);
    l.outlineColor = colorFromValue(o["outlineColor"], Qt::black);
    l.outlineWidth = o["outlineWidth"].toDouble(6.0);
    l.shadow = o["shadow"].toBool(true);
    l.italic = o["italic"].toBool();
    l.letterSpacing = o["letterSpacing"].toDouble(0.0);
    l.align = std::clamp(o["align"].toInt(0), 0, 2);
    l.lineHeight = o["lineHeight"].toDouble(1.0);
    l.textGradient = o["textGradient"].toBool();
    l.textColor2 = colorFromValue(o["textColor2"], QColor(255, 120, 0));
    l.textGradientAngle = o["textGradientAngle"].toDouble(90.0);
    l.textBox = o["textBox"].toBool();
    l.textBoxColor = colorFromValue(o["textBoxColor"], QColor(0, 0, 0, 170));
    l.textBoxPadding = o["textBoxPadding"].toDouble(0.3);
    l.shape = ShapeKind::Rectangle;
    for (int i = 0; i < int(std::size(kShapeIds)); ++i) {
        if (o["shape"].toString() == QLatin1String(kShapeIds[i]))
            l.shape = ShapeKind(i);
    }
    l.shapeSize = QSize(o["width"].toInt(400), o["height"].toInt(200));
    l.fillColor = colorFromValue(o["fillColor"], QColor(255, 196, 0));
    l.cornerRadius = o["cornerRadius"].toDouble();
    l.gradient = o["gradient"].toBool();
    l.fillColor2 = colorFromValue(o["fillColor2"], QColor(255, 80, 60));
    l.gradientAngle = o["gradientAngle"].toDouble(90.0);
    for (const auto &sv : o["strokes"].toArray()) {
        const QJsonObject so = sv.toObject();
        BrushStroke st;
        st.color = colorFromValue(so["color"], Qt::white);
        st.width = so["width"].toDouble(12.0);
        st.eraser = so["eraser"].toBool();
        const QJsonArray pts = so["points"].toArray();
        for (int i = 0; i + 1 < pts.size(); i += 2)
            st.points.append(QPointF(pts[i].toDouble(), pts[i + 1].toDouble()));
        l.strokes.append(st);
    }
    return l;
}

QVector<CanvasPreset> canvasPresets()
{
    return {
        {QStringLiteral("YouTube Thumbnail (1280×720)"), QSize(1280, 720)},
        {QStringLiteral("YouTube Channel Art (2560×1440)"), QSize(2560, 1440)},
        {QStringLiteral("Full HD (1920×1080)"), QSize(1920, 1080)},
        {QStringLiteral("Square Post (1080×1080)"), QSize(1080, 1080)},
        {QStringLiteral("Portrait Post (1080×1350)"), QSize(1080, 1350)},
        {QStringLiteral("Story / Shorts Cover (1080×1920)"), QSize(1080, 1920)},
        {QStringLiteral("Poster (2480×3508, A4 @300dpi)"), QSize(2480, 3508)},
    };
}

int DesignDocument::indexOf(const QUuid &id) const
{
    for (int i = 0; i < layers.size(); ++i) {
        if (layers[i].id == id)
            return i;
    }
    return -1;
}

DesignLayer *DesignDocument::layer(const QUuid &id)
{
    const int i = indexOf(id);
    return i >= 0 ? &layers[i] : nullptr;
}

const DesignLayer *DesignDocument::layer(const QUuid &id) const
{
    const int i = indexOf(id);
    return i >= 0 ? &layers[i] : nullptr;
}

QJsonObject DesignDocument::toJson() const
{
    QJsonObject o;
    o["width"] = canvasSize.width();
    o["height"] = canvasSize.height();
    o["background"] = colorToString(background);
    QJsonArray arr;
    for (const auto &l : layers)
        arr.append(l.toJson());
    o["layers"] = arr;
    return o;
}

DesignDocument DesignDocument::fromJson(const QJsonObject &o)
{
    DesignDocument d;
    d.canvasSize = QSize(o["width"].toInt(1280), o["height"].toInt(720));
    if (d.canvasSize.isEmpty())
        d.canvasSize = QSize(1280, 720);
    d.background = colorFromValue(o["background"], QColor(24, 24, 28));
    for (const auto &v : o["layers"].toArray())
        d.layers.append(DesignLayer::fromJson(v.toObject()));
    return d;
}

} // namespace spotlight
