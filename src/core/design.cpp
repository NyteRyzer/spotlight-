#include "core/design.h"

#include <QJsonArray>

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
    }
    return QStringLiteral("image");
}

LayerType layerTypeFromString(const QString &s)
{
    if (s == QLatin1String("text"))
        return LayerType::Text;
    if (s == QLatin1String("shape"))
        return LayerType::Shape;
    return LayerType::Image;
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
    switch (type) {
    case LayerType::Image:
        o["imagePath"] = imagePath;
        o["brightness"] = brightness;
        o["contrast"] = contrast;
        o["saturation"] = saturation;
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
        break;
    case LayerType::Shape:
        o["shape"] = shape == ShapeKind::Ellipse ? QStringLiteral("ellipse") : QStringLiteral("rectangle");
        o["width"] = shapeSize.width();
        o["height"] = shapeSize.height();
        o["fillColor"] = colorToString(fillColor);
        o["cornerRadius"] = cornerRadius;
        o["gradient"] = gradient;
        o["fillColor2"] = colorToString(fillColor2);
        o["gradientAngle"] = gradientAngle;
        break;
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
    l.imagePath = o["imagePath"].toString();
    l.brightness = o["brightness"].toDouble(0.0);
    l.contrast = o["contrast"].toDouble(1.0);
    l.saturation = o["saturation"].toDouble(1.0);
    l.text = o["text"].toString();
    l.fontFamily = o["fontFamily"].toString();
    l.fontSize = o["fontSize"].toInt(96);
    l.bold = o["bold"].toBool(true);
    l.textColor = colorFromValue(o["textColor"], Qt::white);
    l.outlineColor = colorFromValue(o["outlineColor"], Qt::black);
    l.outlineWidth = o["outlineWidth"].toDouble(6.0);
    l.shadow = o["shadow"].toBool(true);
    l.shape = o["shape"].toString() == QLatin1String("ellipse") ? ShapeKind::Ellipse : ShapeKind::Rectangle;
    l.shapeSize = QSize(o["width"].toInt(400), o["height"].toInt(200));
    l.fillColor = colorFromValue(o["fillColor"], QColor(255, 196, 0));
    l.cornerRadius = o["cornerRadius"].toDouble();
    l.gradient = o["gradient"].toBool();
    l.fillColor2 = colorFromValue(o["fillColor2"], QColor(255, 80, 60));
    l.gradientAngle = o["gradientAngle"].toDouble(90.0);
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
