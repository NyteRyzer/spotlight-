#include "core/imageops.h"

#include <QtGlobal>

#include <algorithm>
#include <array>

namespace spotlight {

QImage adjustImage(const QImage &src, double brightness, double contrast, double saturation)
{
    if (src.isNull())
        return src;
    if (qFuzzyIsNull(brightness) && qFuzzyCompare(contrast, 1.0) && qFuzzyCompare(saturation, 1.0))
        return src;

    QImage img = src.convertToFormat(src.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);

    std::array<int, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        const double v = (i / 255.0 - 0.5) * contrast + 0.5 + brightness;
        lut[i] = std::clamp(int(v * 255.0 + 0.5), 0, 255);
    }
    const int sat = int(saturation * 256.0);
    const bool doSat = !qFuzzyCompare(saturation, 1.0);

    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = line[x];
            int r = lut[qRed(p)];
            int g = lut[qGreen(p)];
            int b = lut[qBlue(p)];
            if (doSat) {
                const int l = (r * 77 + g * 150 + b * 29) >> 8;
                r = std::clamp(l + (((r - l) * sat) >> 8), 0, 255);
                g = std::clamp(l + (((g - l) * sat) >> 8), 0, 255);
                b = std::clamp(l + (((b - l) * sat) >> 8), 0, 255);
            }
            line[x] = qRgba(r, g, b, qAlpha(p));
        }
    }
    return img;
}

} // namespace spotlight

#include <QHash>
#include <QVector>

#include <cmath>
#include <deque>

namespace spotlight {

namespace {

QImage argb(const QImage &src)
{
    return src.convertToFormat(QImage::Format_ARGB32);
}

// One horizontal box-blur pass of radius r over premultiplied ARGB, written
// transposed so the next call blurs the other axis.
QImage boxPassTransposed(const QImage &src, int r)
{
    const int w = src.width(), h = src.height();
    QImage dst(h, w, QImage::Format_ARGB32_Premultiplied);
    const int div = 2 * r + 1;
    for (int y = 0; y < h; ++y) {
        const auto *in = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        int sa = 0, sr = 0, sg = 0, sb = 0;
        for (int i = -r; i <= r; ++i) {
            const QRgb p = in[std::clamp(i, 0, w - 1)];
            sa += qAlpha(p), sr += qRed(p), sg += qGreen(p), sb += qBlue(p);
        }
        for (int x = 0; x < w; ++x) {
            reinterpret_cast<QRgb *>(dst.scanLine(x))[y] = qRgba(sr / div, sg / div, sb / div, sa / div);
            const QRgb add = in[std::min(x + r + 1, w - 1)];
            const QRgb sub = in[std::max(x - r, 0)];
            sa += qAlpha(add) - qAlpha(sub);
            sr += qRed(add) - qRed(sub);
            sg += qGreen(add) - qGreen(sub);
            sb += qBlue(add) - qBlue(sub);
        }
    }
    return dst;
}

} // namespace

QImage applyColorMatrix(const QImage &src, const std::array<double, 9> &m)
{
    if (src.isNull())
        return src;
    QImage img = argb(src);
    int k[9];
    for (int i = 0; i < 9; ++i)
        k[i] = int(std::lround(m[i] * 1024.0));
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = line[x];
            const int r = qRed(p), g = qGreen(p), b = qBlue(p);
            line[x] = qRgba(std::clamp((k[0] * r + k[1] * g + k[2] * b) >> 10, 0, 255),
                            std::clamp((k[3] * r + k[4] * g + k[5] * b) >> 10, 0, 255),
                            std::clamp((k[6] * r + k[7] * g + k[8] * b) >> 10, 0, 255), qAlpha(p));
        }
    }
    return img;
}

QImage applyVignette(const QImage &src, double angle)
{
    if (src.isNull() || angle <= 0)
        return src;
    QImage img = argb(src);
    const double x0 = img.width() / 2.0, y0 = img.height() / 2.0;
    const double dmax = std::hypot(x0, y0);
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const double dn = std::hypot(x - x0, y - y0) / dmax;
            const double c = std::cos(angle * dn);
            const double f = dn > 1 ? 0 : c * c * c * c;
            const QRgb p = line[x];
            line[x] = qRgba(int(qRed(p) * f), int(qGreen(p) * f), int(qBlue(p) * f), qAlpha(p));
        }
    }
    return img;
}

QImage blurImage(const QImage &src, double sigma)
{
    if (src.isNull() || sigma < 0.3)
        return src;
    // Three box passes of width w approximate a Gaussian when
    // w = sqrt(12 sigma^2 / 3 + 1).
    const int r = std::max(1, int(std::lround((std::sqrt(4.0 * sigma * sigma + 1.0) - 1.0) / 2.0)));
    QImage img = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int pass = 0; pass < 3; ++pass)
        img = boxPassTransposed(boxPassTransposed(img, r), r);
    return img.convertToFormat(src.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
}

QImage chromaKey(const QImage &src, const QColor &key, double similarity, double blend)
{
    if (src.isNull())
        return src;
    QImage img = argb(src);
    auto uv = [](int r, int g, int b, double *u, double *v) {
        // BT.601 limited-range chroma, as FFmpeg's RGB->YUV conversion.
        *u = -0.148 * r - 0.291 * g + 0.439 * b + 128.0;
        *v = 0.439 * r - 0.368 * g - 0.071 * b + 128.0;
    };
    double ku, kv;
    uv(key.red(), key.green(), key.blue(), &ku, &kv);
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = line[x];
            double u, v;
            uv(qRed(p), qGreen(p), qBlue(p), &u, &v);
            const double du = u - ku, dv = v - kv;
            const double diff = std::sqrt((du * du + dv * dv) / (255.0 * 255.0 * 2.0));
            double a;
            if (blend > 0.0001)
                a = std::clamp((diff - similarity) / blend, 0.0, 1.0);
            else
                a = diff > similarity ? 1.0 : 0.0;
            line[x] = qRgba(qRed(p), qGreen(p), qBlue(p), int(qAlpha(p) * a));
        }
    }
    return img;
}

QImage hueRotate(const QImage &src, double degrees)
{
    if (src.isNull() || qFuzzyIsNull(std::fmod(degrees, 360.0)))
        return src;
    const double a = degrees * M_PI / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    // Rotation about the luma axis (standard hue-rotate matrix).
    const std::array<double, 9> m{0.213 + c * 0.787 - s * 0.213, 0.715 - c * 0.715 - s * 0.715,
                                  0.072 - c * 0.072 + s * 0.928, 0.213 - c * 0.213 + s * 0.143,
                                  0.715 + c * 0.285 + s * 0.140, 0.072 - c * 0.072 - s * 0.283,
                                  0.213 - c * 0.213 - s * 0.787, 0.715 - c * 0.715 + s * 0.715,
                                  0.072 + c * 0.928 + s * 0.072};
    return applyColorMatrix(src, m);
}

QImage invertImage(const QImage &src)
{
    if (src.isNull())
        return src;
    QImage img = argb(src);
    img.invertPixels(QImage::InvertRgb);
    return img;
}

QImage applyGamma(const QImage &src, double gamma)
{
    if (src.isNull() || qFuzzyCompare(gamma, 1.0))
        return src;
    QImage img = argb(src);
    std::array<int, 256> lut{};
    const double inv = 1.0 / std::clamp(gamma, 0.1, 10.0);
    for (int i = 0; i < 256; ++i)
        lut[i] = std::clamp(int(std::pow(i / 255.0, inv) * 255.0 + 0.5), 0, 255);
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
            line[x] = qRgba(lut[qRed(line[x])], lut[qGreen(line[x])], lut[qBlue(line[x])], qAlpha(line[x]));
    }
    return img;
}

QImage sharpenImage(const QImage &src, double amount)
{
    if (src.isNull() || amount <= 0)
        return src;
    QImage img = argb(src);
    const QImage soft = argb(blurImage(img, 1.2));
    const int k = int(amount * 256.0);
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        const auto *bl = reinterpret_cast<const QRgb *>(soft.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            auto ch = [&](int v, int b) { return std::clamp(v + (((v - b) * k) >> 8), 0, 255); };
            const QRgb p = line[x], b = bl[x];
            line[x] = qRgba(ch(qRed(p), qRed(b)), ch(qGreen(p), qGreen(b)), ch(qBlue(p), qBlue(b)), qAlpha(p));
        }
    }
    return img;
}

QImage pixelateImage(const QImage &src, int block)
{
    if (src.isNull() || block < 2)
        return src;
    const QImage small = src.scaled(std::max(1, src.width() / block), std::max(1, src.height() / block),
                                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return small.scaled(src.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

QImage addGrain(const QImage &src, double strength, quint32 seed)
{
    if (src.isNull() || strength <= 0)
        return src;
    QImage img = argb(src);
    const int range = std::max(1, int(strength));
    quint32 state = seed | 1u;
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            const int n = int(state % quint32(range + 1)) - range / 2;
            const QRgb p = line[x];
            line[x] = qRgba(std::clamp(qRed(p) + n, 0, 255), std::clamp(qGreen(p) + n, 0, 255),
                            std::clamp(qBlue(p) + n, 0, 255), qAlpha(p));
        }
    }
    return img;
}

QImage removeBackground(const QImage &src, double tolerance)
{
    if (src.isNull())
        return src;
    QImage img = argb(src);
    const int w = img.width(), h = img.height();
    const int tol = int(std::clamp(tolerance, 0.0, 1.0) * 441.0); // max RGB distance ~441
    QVector<quint8> state(w * h, 0); // 0 = unvisited, 1 = background
    std::deque<int> queue;
    auto near = [&](QRgb a, QRgb b) {
        const int dr = qRed(a) - qRed(b), dg = qGreen(a) - qGreen(b), db = qBlue(a) - qBlue(b);
        return dr * dr + dg * dg + db * db <= tol * tol;
    };
    auto pixel = [&](int i) { return reinterpret_cast<const QRgb *>(img.constScanLine(i / w))[i % w]; };
    // The backdrop colour is the most common (quantised) border colour; only
    // border pixels near it seed the fill, so a subject touching the edge of
    // the frame is not eaten.
    QHash<int, int> votes;
    QHash<int, QRgb> sample;
    auto vote = [&](int x, int y) {
        const QRgb p = pixel(y * w + x);
        const int q = ((qRed(p) >> 4) << 8) | ((qGreen(p) >> 4) << 4) | (qBlue(p) >> 4);
        ++votes[q];
        sample.insert(q, p);
    };
    for (int x = 0; x < w; ++x) {
        vote(x, 0);
        vote(x, h - 1);
    }
    for (int y = 0; y < h; ++y) {
        vote(0, y);
        vote(w - 1, y);
    }
    int best = -1, bestVotes = 0;
    for (auto it = votes.cbegin(); it != votes.cend(); ++it) {
        if (it.value() > bestVotes) {
            best = it.key();
            bestVotes = it.value();
        }
    }
    const QRgb ref = sample.value(best);
    auto seed = [&](int x, int y) {
        const int i = y * w + x;
        if (!state[i] && near(pixel(i), ref)) {
            state[i] = 1;
            queue.push_back(i);
        }
    };
    for (int x = 0; x < w; ++x) {
        seed(x, 0);
        seed(x, h - 1);
    }
    for (int y = 0; y < h; ++y) {
        seed(0, y);
        seed(w - 1, y);
    }
    while (!queue.empty()) {
        const int i = queue.front();
        queue.pop_front();
        const int x = i % w, y = i / w;
        const int nb[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
        for (const auto &n : nb) {
            if (n[0] < 0 || n[1] < 0 || n[0] >= w || n[1] >= h)
                continue;
            const int j = n[1] * w + n[0];
            if (state[j] || !near(pixel(j), ref))
                continue;
            state[j] = 1;
            queue.push_back(j);
        }
    }
    QImage out = img;
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            if (state[i]) {
                line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), 0);
                continue;
            }
            // Soften the edge: half alpha next to removed pixels.
            const bool edge = (x > 0 && state[i - 1]) || (x + 1 < w && state[i + 1]) || (y > 0 && state[i - w])
                || (y + 1 < h && state[i + w]);
            if (edge)
                line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), qAlpha(line[x]) / 2);
        }
    }
    return out;
}

QImage fadeMask(const QImage &src, int direction, double amount)
{
    if (src.isNull() || amount <= 0)
        return src;
    QImage img = argb(src);
    const int w = img.width(), h = img.height();
    amount = std::clamp(amount, 0.0, 1.0);
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            double pos; // 0 at the solid edge, 1 at the faded edge
            switch (direction) {
            case 1: pos = 1.0 - double(y) / std::max(1, h - 1); break;
            case 2: pos = double(x) / std::max(1, w - 1); break;
            case 3: pos = 1.0 - double(x) / std::max(1, w - 1); break;
            default: pos = double(y) / std::max(1, h - 1); break;
            }
            const double f = std::clamp((1.0 - pos) / amount, 0.0, 1.0);
            line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), int(qAlpha(line[x]) * f));
        }
    }
    return img;
}

} // namespace spotlight

namespace spotlight {

QImage tintedMask(const QImage &src, const QColor &color)
{
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int ca = color.alpha();
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
            line[x] = qRgba(color.red(), color.green(), color.blue(), qAlpha(line[x]) * ca / 255);
    }
    return img;
}

QImage alphaOutline(const QImage &src, double radius, const QColor &color)
{
    const QImage a = src.convertToFormat(QImage::Format_ARGB32);
    const int w = a.width(), h = a.height();
    QImage out(w, h, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    if (w == 0 || h == 0 || radius <= 0)
        return out;
    // Two-pass 3-4 chamfer distance (in 1/3 px units) to the nearest opaque pixel.
    const int inf = 1 << 28;
    QVector<int> dist(w * h);
    for (int y = 0; y < h; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        for (int x = 0; x < w; ++x)
            dist[y * w + x] = qAlpha(line[x]) >= 96 ? 0 : inf;
    }
    auto relax = [&](int i, int j, int cost) {
        if (dist[j] + cost < dist[i])
            dist[i] = dist[j] + cost;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            if (x > 0)
                relax(i, i - 1, 3);
            if (y > 0) {
                relax(i, i - w, 3);
                if (x > 0)
                    relax(i, i - w - 1, 4);
                if (x + 1 < w)
                    relax(i, i - w + 1, 4);
            }
        }
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            const int i = y * w + x;
            if (x + 1 < w)
                relax(i, i + 1, 3);
            if (y + 1 < h) {
                relax(i, i + w, 3);
                if (x + 1 < w)
                    relax(i, i + w + 1, 4);
                if (x > 0)
                    relax(i, i + w - 1, 4);
            }
        }
    }
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const double d = dist[y * w + x] / 3.0;
            const double cov = std::clamp(radius - d + 0.5, 0.0, 1.0);
            line[x] = qRgba(color.red(), color.green(), color.blue(), int(cov * color.alpha()));
        }
    }
    return out;
}

} // namespace spotlight
