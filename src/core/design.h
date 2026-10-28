#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

namespace spotlight {

enum class LayerType { Image, Text, Shape, Drawing };
enum class ShapeKind { Rectangle, Ellipse, Triangle, Star, Arrow, Line, SpeechBubble, Burst };

// Photoshop-style layer blend modes (mapped to QPainter composition modes).
enum class BlendMode {
    Normal, Multiply, Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn, HardLight, SoftLight, Difference,
    Exclusion
};
QStringList blendModeNames();
QStringList shapeKindNames();

// One freehand brush stroke of a Drawing layer, in layer coordinates.
struct BrushStroke {
    QColor color = Qt::white;
    double width = 12.0;
    bool eraser = false;
    QVector<QPointF> points;
};

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
    BlendMode blend = BlendMode::Normal;
    bool flipH = false;
    bool flipV = false;
    bool locked = false;   // can't be selected/moved on the canvas

    // Effects (any layer type)
    bool dropShadow = false;
    QColor shadowColor = QColor(0, 0, 0, 180);
    double shadowBlur = 12.0;      // px
    double shadowDistance = 10.0;  // px
    double shadowAngle = 45.0;     // degrees, light from the top-left
    bool glow = false;
    QColor glowColor = QColor(255, 220, 60);
    double glowSize = 18.0;        // px
    double strokeWidth = 0.0;      // image/shape/drawing outline along the alpha edge
    QColor strokeColor = Qt::white;

    // Image
    QString imagePath;
    double brightness = 0.0; // -1 .. 1
    double contrast = 1.0;   //  0 .. 2
    double saturation = 1.0; //  0 .. 3
    double hue = 0.0;        // degrees
    double blur = 0.0;       // px
    bool grayscale = false;
    bool invert = false;
    bool removeBg = false;   // flood-fill backdrop removal
    double removeBgTolerance = 0.12;
    double cropLeft = 0.0, cropTop = 0.0, cropRight = 0.0, cropBottom = 0.0; // fractions
    int fadeDirection = 0;   // see fadeMask(): 0 bottom, 1 top, 2 right, 3 left
    double fadeAmount = 0.0; // 0 = no fade mask
    bool tint = false;       // colour overlay (duotone-style)
    QColor tintColor = QColor(255, 120, 0);
    double tintAmount = 0.5; // 0 .. 1

    // Text
    QString text;
    QString fontFamily;
    int fontSize = 96;
    bool bold = true;
    QColor textColor = Qt::white;
    QColor outlineColor = Qt::black;
    double outlineWidth = 6.0;
    bool shadow = true;      // hard offset shadow (classic thumbnail look)
    bool italic = false;
    double letterSpacing = 0.0; // percent of the font size
    int align = 0;              // 0 left, 1 centre, 2 right (multi-line text)
    double lineHeight = 1.0;    // multiple of the font's line spacing
    bool textGradient = false;  // gradient fill from textColor to textColor2
    QColor textColor2 = QColor(255, 120, 0);
    double textGradientAngle = 90.0; // degrees, 90 = top→bottom
    bool textBox = false;       // filled box behind the text (caption style)
    QColor textBoxColor = QColor(0, 0, 0, 170);
    double textBoxPadding = 0.3; // fraction of the font size

    // Shape
    ShapeKind shape = ShapeKind::Rectangle;
    QSize shapeSize = QSize(400, 200);
    QColor fillColor = QColor(255, 196, 0);
    double cornerRadius = 0.0;
    bool gradient = false;              // linear gradient fill
    QColor fillColor2 = QColor(255, 80, 60);
    double gradientAngle = 90.0;        // degrees, 0 = left→right, 90 = top→bottom

    // Drawing (brush) — shapeSize holds the drawing's bounds
    QVector<BrushStroke> strokes;

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
