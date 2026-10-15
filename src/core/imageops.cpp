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
