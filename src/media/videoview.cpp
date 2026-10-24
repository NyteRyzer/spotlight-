#include "media/videoview.h"

#include "core/imageops.h"
#include "core/looks.h"

#include <QPainter>
#include <QPainterPath>
#include <QVideoFrame>
#include <QVideoSink>

#include <cmath>

namespace spotlight {

VideoView::VideoView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(160, 90);
    setAttribute(Qt::WA_OpaquePaintEvent);
    sinkFor(0);
}

QVideoSink *VideoView::sinkFor(int slot) const
{
    auto it = m_sinks.find(slot);
    if (it != m_sinks.end())
        return *it;
    auto *self = const_cast<VideoView *>(this);
    auto *sink = new QVideoSink(self);
    connect(sink, &QVideoSink::videoFrameChanged, self, [self, slot](const QVideoFrame &f) { self->onFrame(slot, f); });
    m_sinks.insert(slot, sink);
    return sink;
}

void VideoView::setCanvasSize(const QSize &size)
{
    if (size == m_canvas)
        return;
    m_canvas = size;
    update();
}

void VideoView::onFrame(int slot, const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    const QImage img = frame.toImage();
    if (img.isNull())
        return;
    m_frames[slot] = img;
    ++m_serial[slot];
    update();
}

void VideoView::setStillImage(const QImage &img)
{
    m_compositing = false;
    m_frames[0] = img;
    ++m_serial[0];
    update();
}

void VideoView::showBlack()
{
    m_frames.remove(0);
    update();
}

void VideoView::setPlaceholderText(const QString &text)
{
    if (text == m_placeholder)
        return;
    m_placeholder = text;
    update();
}

void VideoView::setTracks(const QVector<ViewTrack> &tracks)
{
    m_compositing = true;
    m_tracks = tracks;
    update();
}

QRect VideoView::canvasRect() const
{
    const QImage f = m_frames.value(0);
    QSize c = m_canvas.isEmpty() ? (f.isNull() ? QSize(16, 9) : f.size()) : m_canvas;
    c.scale(size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - c.width()) / 2, (height() - c.height()) / 2), c);
}

QImage VideoView::frameFor(const ViewLayer &l) const
{
    if (!l.still.isNull())
        return l.still;
    return m_frames.value(l.slot);
}

QImage VideoView::processed(const ViewLayer &l, const QSize &target, bool cache) const
{
    const QImage src = frameFor(l);
    if (src.isNull())
        return src;
    const Clip &c = l.clip;
    const QString id = l.still.isNull() ? QStringLiteral("s%1").arg(l.slot) : QStringLiteral("i%1").arg(l.still.cacheKey());
    QString key;
    if (cache) {
        key = QStringLiteral("%1|%2|%3x%4|%5|%6,%7,%8,%9")
                  .arg(id)
                  .arg(l.still.isNull() ? m_serial.value(l.slot) : 0)
                  .arg(target.width())
                  .arg(target.height())
                  .arg(int(c.transform.flipH) | int(c.transform.flipV) << 1 | int(c.key.enabled) << 2)
                  .arg(c.color.brightness)
                  .arg(c.color.contrast)
                  .arg(c.color.saturation)
                  .arg(QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8")
                           .arg(c.color.temperature)
                           .arg(c.color.tint)
                           .arg(int(c.color.mono))
                           .arg(c.color.vignette)
                           .arg(c.color.blur)
                           .arg(c.key.color.rgb())
                           .arg(c.key.similarity)
                           .arg(c.key.blend));
        auto it = m_cache.constFind(id);
        if (it != m_cache.constEnd() && it->key == key)
            return it->image;
    }

    // Same order as the exporter: fit, flip, eq, matrix, blur, vignette, key.
    QImage img = src.size() == target ? src : src.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (c.transform.flipH || c.transform.flipV)
        img = img.mirrored(c.transform.flipH, c.transform.flipV);
    img = adjustImage(img, c.color.brightness, c.color.contrast, c.color.saturation);
    if (c.color.hasMatrix())
        img = applyColorMatrix(img, colorMatrix(c.color));
    if (c.color.blur > 0)
        img = blurImage(img, blurSigma(c.color.blur, img.height()));
    if (c.color.vignette > 0)
        img = applyVignette(img, vignetteAngle(c.color.vignette));
    if (c.key.enabled)
        img = chromaKey(img, c.key.color, c.key.similarity, c.key.blend);
    if (cache)
        m_cache.insert(id, {key, img});
    return img;
}

void VideoView::drawLayer(QPainter &p, const ViewLayer &l, const QSize &canvas, double scale, bool forDisplay) const
{
    const QImage src = frameFor(l);
    if (src.isNull())
        return;
    const Clip &c = l.clip;
    const Transform &tf = c.transform;
    const double W = canvas.width(), H = canvas.height();
    const QSize fit = fitSize(src.size(), canvas);
    // Process at the size the picture will actually be drawn.
    const double drawScale = scale * std::clamp(tf.scale, 0.05, 5.0) * (c.motion != Motion::None ? 1.0 + kMotionAmount : 1.0);
    QSize target(std::max(2, int(fit.width() * std::min(1.0, drawScale))),
                 std::max(2, int(fit.height() * std::min(1.0, drawScale))));
    if (!forDisplay)
        target = fit;
    const QImage img = processed(l, target, forDisplay);
    if (img.isNull())
        return;

    double cl = std::clamp(tf.cropLeft, 0.0, 0.95), cr = std::clamp(tf.cropRight, 0.0, 0.95);
    double ct = std::clamp(tf.cropTop, 0.0, 0.95), cb = std::clamp(tf.cropBottom, 0.0, 0.95);
    if (cl + cr > 0.95)
        cr = 0.95 - cl;
    if (ct + cb > 0.95)
        cb = 0.95 - ct;
    const double s = std::clamp(tf.scale, 0.05, 5.0);
    const MotionState mo = motionAt(c.motion, l.progress);
    const double rad = tf.rotation * M_PI / 180.0;
    const double ox0 = (cl - cr) / 2.0 * fit.width() * s;
    const double oy0 = (ct - cb) / 2.0 * fit.height() * s;
    const double ox = ox0 * std::cos(rad) - oy0 * std::sin(rad) + tf.x * W + mo.dx * fit.width() * s;
    const double oy = ox0 * std::sin(rad) + oy0 * std::cos(rad) + tf.y * H + mo.dy * fit.height() * s;
    const double pw = fit.width() * (1 - cl - cr), ph = fit.height() * (1 - ct - cb);

    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(p.opacity() * std::clamp(tf.opacity, 0.0, 1.0) * std::clamp(l.alpha, 0.0, 1.0));
    p.scale(scale, scale);
    p.translate(W / 2 + ox, H / 2 + oy);
    p.rotate(tf.rotation);
    p.scale(s * mo.zoom, s * mo.zoom);
    const double kx = double(img.width()) / fit.width(), ky = double(img.height()) / fit.height();
    const QRectF srcRect(cl * fit.width() * kx, ct * fit.height() * ky, pw * kx, ph * ky);
    p.drawImage(QRectF(-pw / 2, -ph / 2, pw, ph), img, srcRect);
    p.restore();
}

void VideoView::drawTransition(QPainter &p, const ViewTrack &t, const QSize &canvas, double scale,
                               bool forDisplay) const
{
    const QSize px(std::max(1, int(std::ceil(canvas.width() * scale))),
                   std::max(1, int(std::ceil(canvas.height() * scale))));
    auto renderOne = [&](const ViewLayer &l) {
        QImage img(px, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter ip(&img);
        drawLayer(ip, l, canvas, scale, forDisplay);
        return img;
    };
    const QImage a = renderOne(t.outgoing);
    const QImage b = renderOne(t.layer);
    const double pr = std::clamp(t.transitionProgress, 0.0, 1.0);
    const double w = px.width(), h = px.height();
    const QString &id = t.transition;

    p.save();
    auto clipped = [&](const QPainterPath &region, const QImage &under, const QImage &over) {
        p.drawImage(0, 0, under);
        p.save();
        p.setClipPath(region);
        p.drawImage(0, 0, over);
        p.restore();
    };
    if (id == QLatin1String("fadeblack") || id == QLatin1String("fadewhite")) {
        const QColor tint = id == QLatin1String("fadeblack") ? Qt::black : Qt::white;
        const bool first = pr < 0.5;
        p.drawImage(0, 0, first ? a : b);
        QColor c = tint;
        c.setAlphaF(first ? pr * 2 : (1 - pr) * 2);
        p.fillRect(QRectF(0, 0, w, h), c);
    } else if (id.startsWith(QLatin1String("wipe"))) {
        QRectF r;
        if (id == QLatin1String("wipeleft"))
            r = QRectF(w * (1 - pr), 0, w * pr + 1, h);
        else if (id == QLatin1String("wiperight"))
            r = QRectF(0, 0, w * pr, h);
        else if (id == QLatin1String("wipeup"))
            r = QRectF(0, h * (1 - pr), w, h * pr + 1);
        else
            r = QRectF(0, 0, w, h * pr);
        QPainterPath path;
        path.addRect(r);
        clipped(path, a, b);
    } else if (id.startsWith(QLatin1String("slide"))) {
        QPointF da, db;
        if (id == QLatin1String("slideleft")) {
            da = QPointF(-w * pr, 0);
            db = QPointF(w * (1 - pr), 0);
        } else if (id == QLatin1String("slideright")) {
            da = QPointF(w * pr, 0);
            db = QPointF(-w * (1 - pr), 0);
        } else if (id == QLatin1String("slideup")) {
            da = QPointF(0, -h * pr);
            db = QPointF(0, h * (1 - pr));
        } else {
            da = QPointF(0, h * pr);
            db = QPointF(0, -h * (1 - pr));
        }
        p.drawImage(da, a);
        p.drawImage(db, b);
    } else if (id == QLatin1String("circleopen") || id == QLatin1String("circleclose")) {
        const double maxR = std::hypot(w, h) / 2;
        QPainterPath path;
        const bool open = id == QLatin1String("circleopen");
        const double r = open ? pr * maxR : (1 - pr) * maxR;
        path.addEllipse(QPointF(w / 2, h / 2), r, r);
        if (open)
            clipped(path, a, b);
        else
            clipped(path, b, a);
    } else if (id == QLatin1String("radial")) {
        QPainterPath path;
        const double R = std::hypot(w, h);
        path.moveTo(w / 2, h / 2);
        path.arcTo(QRectF(w / 2 - R, h / 2 - R, 2 * R, 2 * R), 90, -360 * pr);
        path.closeSubpath();
        clipped(path, a, b);
    } else if (id == QLatin1String("pixelize")) {
        const double q = 1.0 - std::abs(2 * pr - 1);
        const int block = 1 + int(q * 48 * scale);
        QImage mix(px, QImage::Format_ARGB32_Premultiplied);
        mix.fill(Qt::transparent);
        {
            QPainter mp(&mix);
            mp.drawImage(0, 0, a);
            mp.setOpacity(pr);
            mp.drawImage(0, 0, b);
        }
        if (block > 1) {
            const QImage small =
                mix.scaled(std::max(1, px.width() / block), std::max(1, px.height() / block), Qt::IgnoreAspectRatio,
                           Qt::FastTransformation);
            mix = small.scaled(px, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        }
        p.drawImage(0, 0, mix);
    } else if (id == QLatin1String("zoomin")) {
        p.drawImage(0, 0, b);
        p.save();
        p.setOpacity(1 - pr);
        p.translate(w / 2, h / 2);
        p.scale(1 + pr, 1 + pr);
        p.translate(-w / 2, -h / 2);
        p.drawImage(0, 0, a);
        p.restore();
    } else { // fade / dissolve
        p.drawImage(0, 0, a);
        p.setOpacity(pr);
        p.drawImage(0, 0, b);
    }
    p.restore();
}

void VideoView::composite(QPainter &p, const QSize &canvas, double scale, bool forDisplay) const
{
    for (const ViewTrack &t : m_tracks) {
        if (t.inTransition) {
            drawTransition(p, t, canvas, scale, forDisplay);
        } else {
            drawLayer(p, t.layer, canvas, scale, forDisplay);
        }
    }
}

QImage VideoView::currentFrame() const
{
    if (!m_compositing)
        return m_frames.value(0);
    if (m_tracks.isEmpty() || m_canvas.isEmpty())
        return {};
    QImage img(m_canvas, QImage::Format_RGB32);
    img.fill(Qt::black);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    composite(p, m_canvas, 1.0, false);
    p.end();
    return img;
}

void VideoView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(14, 14, 16));
    const QRect canvas = canvasRect();
    p.fillRect(canvas, Qt::black);

    if (m_compositing && !m_tracks.isEmpty() && !m_canvas.isEmpty()) {
        p.save();
        p.setClipRect(canvas);
        p.translate(canvas.topLeft());
        composite(p, m_canvas, double(canvas.width()) / m_canvas.width(), true);
        p.restore();
    } else if (!m_compositing && !m_frames.value(0).isNull()) {
        const QImage f = m_frames.value(0);
        QSize fs = f.size();
        fs.scale(canvas.size(), Qt::KeepAspectRatio);
        const QRect fr(QPoint(canvas.x() + (canvas.width() - fs.width()) / 2,
                              canvas.y() + (canvas.height() - fs.height()) / 2),
                       fs);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(fr, f);
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
