#pragma once

#include <QImage>

namespace spotlight {

// Brightness (-1..1, additive), contrast (0..2, around mid grey) and
// saturation (0..3) — the same model as FFmpeg's eq filter, so previews match
// exports. Returns the image unchanged when the settings are neutral.
QImage adjustImage(const QImage &src, double brightness, double contrast, double saturation);

} // namespace spotlight
