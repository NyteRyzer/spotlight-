#pragma once

#include "core/timeline.h"

#include <QImage>
#include <QSize>
#include <QWidget>

class QVideoSink;
class QVideoFrame;

namespace spotlight {

// Displays video frames (from a QVideoSink) or a still image letterboxed into
// a canvas of the sequence's aspect ratio. Applies the clip's colour
// adjustment and fade so the preview matches the export, and keeps the last
// full-resolution frame for "Capture Frame".
class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget *parent = nullptr);

    QVideoSink *videoSink() const { return m_sink; }

    // Aspect of the output canvas (e.g. 1920x1080). Empty = fit the frame.
    void setCanvasSize(const QSize &size);

    void setStillImage(const QImage &img);
    void showBlack();
    void setAdjust(const ColorAdjust &adjust, double fadeLevel);
    void setPlaceholderText(const QString &text);

    // Last displayed frame at source resolution (null if nothing shown).
    QImage currentFrame() const { return m_frame; }

    QSize sizeHint() const override { return {640, 360}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void onFrame(const QVideoFrame &frame);
    QRect canvasRect() const;
    QRect frameRect(const QRect &canvas) const;

    QVideoSink *m_sink;
    QImage m_frame;
    QImage m_display; // scaled + adjusted, cached
    bool m_dirty = true;
    QSize m_canvas;
    ColorAdjust m_adjust;
    double m_fade = 1.0;
    QString m_placeholder;
};

} // namespace spotlight
