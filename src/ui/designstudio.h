#pragma once

#include "core/design.h"

#include <QMainWindow>

class QListWidget;
class QComboBox;
class QStackedWidget;
class QDoubleSpinBox;
class QSpinBox;
class QSlider;
class QPlainTextEdit;
class QFontComboBox;
class QCheckBox;
class QPushButton;
class QLabel;

namespace spotlight {

class Project;
class DesignCanvas;

// Design Studio: layered graphics for thumbnails, posters and channel art.
class DesignStudio : public QMainWindow {
    Q_OBJECT
public:
    explicit DesignStudio(Project *project, QWidget *parent = nullptr);

    // Adds an image layer scaled to fit the canvas (used for captured frames).
    void addImageLayer(const QString &path, const QString &name, bool fitCanvas = true);

public slots:
    void addText();
    void addImageDialog();
    void addShape(spotlight::ShapeKind kind);
    void deleteSelected();
    void moveSelected(int delta); // +1 = forward, -1 = backward
    void duplicateSelected();
    void exportImage();
    // Replaces text/shape layers with a ready-made thumbnail layout; image
    // layers (e.g. a captured video frame) are kept underneath.
    void applyTemplate(int index);
    void setGuidesVisible(bool visible);

public:
    static QStringList templateNames();
    // The design rendered at full canvas resolution.
    QImage renderImage();
    // Layer clipboard support (Edit → Copy / Paste in the Design Studio).
    bool copySelectedLayer(DesignLayer *out) const;
    void pasteLayer(const DesignLayer &layer);

signals:
    void requestVideoFrame();
    void placeOnTimelineRequested();
    void status(const QString &message);

private:
    void rebuildLayers();
    void refreshProperties();
    void editLayer(const QString &what, const std::function<void(DesignLayer &)> &fn);
    QPushButton *colorButton(const QString &what, std::function<QColor(const DesignLayer &)> get,
                             std::function<void(DesignLayer &, const QColor &)> set);

    Project *m_project;
    DesignCanvas *m_canvas;
    QListWidget *m_layers;
    QComboBox *m_preset;
    QPushButton *m_bgButton;

    // Properties
    QWidget *m_props;
    QLabel *m_propsTitle;
    QSlider *m_opacity;
    QDoubleSpinBox *m_scale;
    QDoubleSpinBox *m_rotation;
    QStackedWidget *m_typePages;
    QSlider *m_imgBrightness;
    QSlider *m_imgContrast;
    QSlider *m_imgSaturation;
    QPlainTextEdit *m_text;
    QFontComboBox *m_font;
    QSpinBox *m_fontSize;
    QCheckBox *m_bold;
    QDoubleSpinBox *m_outline;
    QCheckBox *m_shadow;
    QComboBox *m_shapeKind;
    QSpinBox *m_shapeW;
    QSpinBox *m_shapeH;
    QDoubleSpinBox *m_radius;
    QCheckBox *m_gradient;
    QDoubleSpinBox *m_gradientAngle;
    QVector<std::function<void()>> m_colorRefreshers;
    bool m_updating = false;
};

} // namespace spotlight
