#include "media/videoview.h"

#include "core/imageops.h"

#include <QPainter>
#include <QVideoFrame>
#include <QVideoSink>

namespace spotlight {

VideoView::VideoView(QWidget *parent)
    : QWidget(parent)
    , m_sink(new QVideoSink(this))
{
    setMinimumSize(160, 90);
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(m_sink, &QVideoSink::videoFrameChanged, this, &VideoView::onFrame);
}

void VideoView::setCanvasSize(const QSize &size)
{
    m_canvas = size;
    m_dirty = true;
    update();
}

void VideoView::onFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    const QImage img = frame.toImage();
    if (img.isNull())
        return;
    m_frame = img;
    m_dirty = true;
    update();
}

void VideoView::setStillImage(const QImage &img)
{
    m_frame = img;
    m_dirty = true;
    update();
}

void VideoView::showBlack()
{
    if (m_frame.isNull())
        return;
    m_frame = QImage();
    m_dirty = true;
    update();
}

void VideoView::setAdjust(const ColorAdjust &adjust, double fadeLevel)
{
    if (adjust == m_adjust && qFuzzyCompare(1 + fadeLevel, 1 + m_fade))
        return;
    if (!(adjust == m_adjust))
        m_dirty = true;
    m_adjust = adjust;
    m_fade = fadeLevel;
    update();
}

void VideoView::setPlaceholderText(const QString &text)
{
    m_placeholder = text;
    update();
}

void VideoView::resizeEvent(QResizeEvent *)
{
    m_dirty = true;
}

QRect VideoView::canvasRect() const
{
    QSize c = m_canvas.isEmpty() ? (m_frame.isNull() ? QSize(16, 9) : m_frame.size()) : m_canvas;
    c.scale(size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - c.width()) / 2, (height() - c.height()) / 2), c);
}

QRect VideoView::frameRect(const QRect &canvas) const
{
    QSize f = m_frame.size();
    f.scale(canvas.size(), Qt::KeepAspectRatio);
    return QRect(QPoint(canvas.x() + (canvas.width() - f.width()) / 2, canvas.y() + (canvas.height() - f.height()) / 2),
                 f);
}

void VideoView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(14, 14, 16));
    const QRect canvas = canvasRect();
    p.fillRect(canvas, Qt::black);

    if (!m_frame.isNull()) {
        const QRect fr = frameRect(canvas);
        if (m_dirty || m_display.size() != fr.size()) {
            // Scale first so colour adjustment only touches displayed pixels.
            const QImage scaled = m_frame.scaled(fr.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            m_display = adjustImage(scaled, m_adjust.brightness, m_adjust.contrast, m_adjust.saturation);
            m_dirty = false;
        }
        p.drawImage(fr.topLeft(), m_display);
        if (m_fade < 1.0)
            p.fillRect(canvas, QColor(0, 0, 0, int((1.0 - m_fade) * 255)));
    } else if (!m_placeholder.isEmpty()) {
        p.setPen(QColor(140, 140, 150));
        p.drawText(canvas, Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
    }

    if (m_guides) {
        p.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DashLine));
        for (int i = 1; i < 3; ++i) {
            const int x = canvas.left() + canvas.width() * i / 3;
            const int y = canvas.top() + canvas.height() * i / 3;
            p.drawLine(x, canvas.top(), x, canvas.bottom());
            p.drawLine(canvas.left(), y, canvas.right(), y);
        }
        auto inset = [&](double f) {
            const int dx = int(canvas.width() * (1 - f) / 2);
            const int dy = int(canvas.height() * (1 - f) / 2);
            return canvas.adjusted(dx, dy, -dx, -dy);
        };
        p.setPen(QPen(QColor(255, 196, 0, 150), 1));
        p.drawRect(inset(0.95)); // action safe
        p.setPen(QPen(QColor(255, 196, 0, 150), 1, Qt::DashLine));
        p.drawRect(inset(0.90)); // title safe
    }
}

} // namespace spotlight
