#pragma once

#include <QColor>

class QApplication;

namespace spotlight::theme {

// Applies SPOTLIGHT!'s dark studio theme (Fusion style + palette + QSS).
void apply(QApplication &app);

// Brand and timeline colours shared by custom-painted widgets.
inline const QColor accent{255, 196, 0};        // spotlight gold
inline const QColor accentDark{204, 150, 0};
inline const QColor videoClip{58, 110, 196};
inline const QColor audioClip{46, 160, 110};
inline const QColor imageClip{150, 92, 196};
inline const QColor titleClip{214, 120, 40};
inline const QColor playhead{255, 70, 70};
inline const QColor panel{30, 30, 34};
inline const QColor panelAlt{38, 38, 44};
inline const QColor text{225, 225, 232};
inline const QColor textDim{140, 140, 152};

} // namespace spotlight::theme
