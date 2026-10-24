#pragma once

#include <QString>

namespace spotlight {

// Verifies an installed / packaged copy of the app end to end, without any
// user interaction: bundled FFmpeg tools, media probing, Qt Multimedia
// decoding, title and design rendering (fonts), a real export with
// compositing, transitions and audio effects, and the main window.
// Writes a report to stdout and <outDir>/self-test.txt; returns 0 when
// every check passes. Used by CI on the shipped .dmg / AppImage.
int runSelfTest(const QString &outDir);

} // namespace spotlight
