#pragma once

#include <QColor>
#include <QImage>

#include <array>

namespace spotlight {

// Brightness (-1..1, additive), contrast (0..2, around mid grey) and
// saturation (0..3) — the same model as FFmpeg's eq filter, so previews match
// exports. Returns the image unchanged when the settings are neutral.
QImage adjustImage(const QImage &src, double brightness, double contrast, double saturation);

// out = M * (r, g, b) per pixel (FFmpeg colorchannelmixer semantics).
QImage applyColorMatrix(const QImage &src, const std::array<double, 9> &m);

// Darkens towards the corners like FFmpeg's vignette filter (angle in radians).
QImage applyVignette(const QImage &src, double angle);

// Approximate Gaussian blur (three box passes) with the given sigma in pixels.
QImage blurImage(const QImage &src, double sigma);

// Makes pixels close to `key` transparent, using FFmpeg chromakey's distance
// in the UV plane so preview and export key the same pixels.
QImage chromaKey(const QImage &src, const QColor &key, double similarity, double blend);

// Rotates hue by `degrees` (YIQ rotation, preserves luma).
QImage hueRotate(const QImage &src, double degrees);

QImage invertImage(const QImage &src);

// FFmpeg eq gamma: v' = v^(1/gamma) per channel.
QImage applyGamma(const QImage &src, double gamma);

// Unsharp mask: src + amount * (src - blur(src)), like FFmpeg unsharp.
QImage sharpenImage(const QImage &src, double amount);

// Mosaic with square blocks of `block` pixels (area down, nearest up).
QImage pixelateImage(const QImage &src, int block);

// Uniform per-pixel noise of +-strength/2 (FFmpeg noise alls), seeded so a
// still frame doesn't shimmer in the preview.
QImage addGrain(const QImage &src, double strength, quint32 seed);

// "Remove background": flood-fills from the image border, turning connected
// pixels within `tolerance` (0..1) of the border colours transparent, with a
// soft one-pixel edge. Works best on solid or studio backdrops.
QImage removeBackground(const QImage &src, double tolerance);

// Fades the image to transparent towards one edge. direction: 0 = bottom,
// 1 = top, 2 = right, 3 = left; amount (0..1) is the faded fraction.
QImage fadeMask(const QImage &src, int direction, double amount);

} // namespace spotlight

namespace spotlight {

// Solid `color` wherever the source has alpha, keeping the source's alpha
// (multiplied by the colour's alpha). Used for shadows and glows.
QImage tintedMask(const QImage &src, const QColor &color);

// A `radius`-pixel outline around the opaque parts of `src` (round,
// anti-aliased; via a chamfer distance transform). Same size as src, so
// callers must leave `radius` pixels of transparent padding.
QImage alphaOutline(const QImage &src, double radius, const QColor &color);

} // namespace spotlight
