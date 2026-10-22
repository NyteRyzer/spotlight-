#include "ui/theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

namespace spotlight::theme {

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
#ifndef Q_OS_MACOS
    // A crisp UI font where available (macOS keeps its own system font).
    if (QFontDatabase::families().contains(QStringLiteral("Inter"))) {
        QFont f(QStringLiteral("Inter"));
        f.setPointSizeF(app.font().pointSizeF());
        app.setFont(f);
    }
#endif

    QPalette p;
    p.setColor(QPalette::Window, QColor(24, 24, 28));
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, QColor(18, 18, 21));
    p.setColor(QPalette::AlternateBase, panelAlt);
    p.setColor(QPalette::ToolTipBase, QColor(45, 45, 52));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(42, 42, 48));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::PlaceholderText, textDim);
    p.setColor(QPalette::Disabled, QPalette::Text, textDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, textDim);
    p.setColor(QPalette::Disabled, QPalette::WindowText, textDim);
    app.setPalette(p);

    app.setStyleSheet(QStringLiteral(R"(
        QMainWindow::separator { background: #18181c; width: 4px; height: 4px; }
        QDockWidget::title { background: #222228; padding: 5px 8px; font-weight: 600; color: #c8c8d0; }
        QToolBar { background: #1e1e23; border: none; spacing: 4px; padding: 3px; }
        QToolButton { padding: 4px 8px; border-radius: 4px; }
        QToolButton:hover { background: #33333b; }
        QToolButton:checked { background: #3d3a2a; color: #ffc400; }
        QPushButton { padding: 5px 12px; border-radius: 4px; background: #2e2e35; border: 1px solid #3a3a42; }
        QPushButton:hover { background: #393941; }
        QPushButton:default, QPushButton#primary { background: #ffc400; color: #111; border: none; font-weight: 600; }
        QPushButton#primary:hover { background: #ffd23f; }
        QTabBar#workspaceTabs::tab { padding: 8px 22px; font-weight: 600; color: #9a9aa6; background: transparent;
                                     border: none; border-bottom: 3px solid transparent; }
        QTabBar#workspaceTabs::tab:selected { color: #ffffff; border-bottom: 3px solid #ffc400; }
        QTabBar#workspaceTabs::tab:hover { color: #e0e0e8; }
        QWidget#topBar { background: #141417; border-bottom: 1px solid #2a2a30; }
        QLabel#logo { color: #ffc400; font-size: 18px; font-weight: 900; letter-spacing: 2px; padding: 0 14px; }
        QListWidget { border: none; }
        QGroupBox { border: 1px solid #33333a; border-radius: 6px; margin-top: 14px; padding-top: 6px; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #b8b8c2; }
        QSlider::groove:horizontal { height: 4px; background: #3a3a42; border-radius: 2px; }
        QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px; background: #ffc400; }
        QSlider::groove:vertical { width: 4px; background: #3a3a42; border-radius: 2px; }
        QSlider::handle:vertical { height: 12px; margin: 0 -5px; border-radius: 6px; background: #ffc400; }
        QStatusBar { background: #141417; color: #9a9aa6; }
    )"));
}

} // namespace spotlight::theme
