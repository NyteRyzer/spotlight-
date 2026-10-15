#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QUuid>
#include <QVector>

namespace spotlight {

enum class LayerType { Image, Text, Shape };
enum class ShapeKind { Rectangle, Ellipse };

// One layer of a Design Studio document. Layers are stored bottom-to-top.
struct DesignLayer {
    QUuid id;
    LayerType type = LayerType::Image;
    QString name;
    bool visible = true;
    double opacity = 1.0;
    QPointF position;     // top-left in canvas pixels
    double scale = 1.0;
    double rotation = 0.0; // degrees

    // Image
    QString imagePath;
    double brightness = 0.0; // -1 .. 1
    double contrast = 1.0;   //  0 .. 2
    double saturation = 1.0; //  0 .. 3

    // Text
    QString text;
    QString fontFamily;
    int fontSize = 96;
    bool bold = true;
    QColor textColor = Qt::white;
    QColor outlineColor = Qt::black;
    double outlineWidth = 6.0;
    bool shadow = true;

    // Shape
    ShapeKind shape = ShapeKind::Rectangle;
    QSize shapeSize = QSize(400, 200);
    QColor fillColor = QColor(255, 196, 0);
    double cornerRadius = 0.0;

    QJsonObject toJson() const;
    static DesignLayer fromJson(const QJsonObject &o);
};

// Canvas size presets for common creator graphics.
struct CanvasPreset {
    QString name;
    QSize size;
};
QVector<CanvasPreset> canvasPresets();

// A graphic (thumbnail, poster, channel art) designed in the Design Studio.
struct DesignDocument {
    QSize canvasSize = QSize(1280, 720); // YouTube thumbnail
    QColor background = QColor(24, 24, 28);
    QVector<DesignLayer> layers;

    int indexOf(const QUuid &id) const;
    DesignLayer *layer(const QUuid &id);
    const DesignLayer *layer(const QUuid &id) const;

    QJsonObject toJson() const;
    static DesignDocument fromJson(const QJsonObject &o);
};

QString layerTypeToString(LayerType t);
LayerType layerTypeFromString(const QString &s);

} // namespace spotlight
