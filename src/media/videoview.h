#pragma once

#include "core/timeline.h"

#include <QHash>
#include <QImage>
#include <QSize>
#include <QVector>
#include <QWidget>

class QVideoSink;
class QVideoFrame;
class QPainter;

namespace spotlight {

// One clip's picture as shown by the compositor.
struct ViewLayer {
    int slot = -1;        // frames come from sinkFor(slot) unless `still` is set
    QImage still;         // stills and titles
    Clip clip;            // look, transform, key, motion
    double progress = 0;  // 0..1 through the clip (Ken Burns motion)
    double elapsed = 1e9; // seconds since the clip started (entrance motion)
    double alpha = 1.0;   // fade envelope
};

// A video track at the current time: its clip and, during a transition, the
// outgoing clip it is transitioning from.
struct ViewTrack {
    ViewLayer layer;
    bool inTransition = false;
    ViewLayer outgoing;
    QString transition;          // xfade id
    double transitionProgress = 0; // 0 = all outgoing, 1 = all incoming
};

// Displays video. In simple mode (Source Monitor) it shows one sink's frames
// or a still letterboxed into the canvas. In compositing mode (Program
// Monitor) it stacks the given tracks bottom to top over black, applying each
// clip's look, chroma key, crop, transform, motion, fade and transitions with
// the same geometry as the exporter, so the preview matches the export.
class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget *parent = nullptr);

    // Simple-mode sink (slot 0).
    QVideoSink *videoSink() const { return sinkFor(0); }
    // Sink feeding a compositor slot (created on demand).
    QVideoSink *sinkFor(int slot) const;

    // Size of the output canvas (e.g. 1920x1080). Empty = fit the frame.
    void setCanvasSize(const QSize &size);

    // Simple mode.
    void setStillImage(const QImage &img);
    void showBlack();
    void setPlaceholderText(const QString &text);

    // Compositing mode.
    void setTracks(const QVector<ViewTrack> &tracks);

    // Rule-of-thirds plus title-safe (90%) and action-safe (95%) frames.
    void setGuidesVisible(bool on)
    {
        m_guides = on;
        update();
    }

    // What the view shows, rendered at the canvas size (full resolution):
    // the composite in compositing mode, the last frame in simple mode.
    QImage currentFrame() const;

    QSize sizeHint() const override { return {640, 360}; }

    // Draws the tracks onto a painter whose coordinate system is the output
    // canvas in pixels scaled by `scale` (used for display and capture).
    void composite(QPainter &p, const QSize &canvas, double scale, bool forDisplay) const;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void onFrame(int slot, const QVideoFrame &frame);
    QRect canvasRect() const;
    QImage frameFor(const ViewLayer &l) const;
    // The layer's picture after flip, colour and key, at roughly `target`
    // pixels (cached for display).
    QImage processed(const ViewLayer &l, const QSize &target, bool cache) const;
    // Blurred, darkened copy covering the canvas (clip "blur fill").
    QImage blurFillBackground(const ViewLayer &l, const QSize &px, bool cache) const;
    void drawLayer(QPainter &p, const ViewLayer &l, const QSize &canvas, double scale, bool forDisplay) const;
    void drawTransition(QPainter &p, const ViewTrack &t, const QSize &canvas, double scale, bool forDisplay) const;

    mutable QHash<int, QVideoSink *> m_sinks;
    QHash<int, QImage> m_frames;
    QHash<int, quint64> m_serial;
    QSize m_canvas;
    QString m_placeholder;
    bool m_guides = false;
    bool m_compositing = false;
    QVector<ViewTrack> m_tracks;

    struct CacheEntry {
        QString key;
        QImage image;
    };
    mutable QHash<QString, CacheEntry> m_cache; // per layer identity
};

} // namespace spotlight
