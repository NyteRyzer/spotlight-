# spotlight_ui: the three studios and the unified main window.
add_library(spotlight_ui STATIC
    theme.h theme.cpp
    actions.h actions.cpp
    mediabin.h mediabin.cpp
    sourcemonitor.h sourcemonitor.cpp
    programmonitor.h programmonitor.cpp
    timelinewidget.h timelinewidget.cpp
    inspector.h inspector.cpp
    edittools.h edittools.cpp
    commandpalette.h commandpalette.cpp
    markerspanel.h markerspanel.cpp
    recorddialog.h recorddialog.cpp
    autoclip.h autoclip.cpp
    videostudio.h videostudio.cpp
    audiostudio.h audiostudio.cpp
    designcanvas.h designcanvas.cpp
    designstudio.h designstudio.cpp
    exportdialog.h exportdialog.cpp
    shortcutsdialog.h shortcutsdialog.cpp
    settingsdialog.h settingsdialog.cpp
    recoverydialog.h recoverydialog.cpp
    mainwindow.h mainwindow.cpp
    ../../resources/resources.qrc
)

target_include_directories(spotlight_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(spotlight_ui PUBLIC spotlight_core spotlight_media Qt6::Widgets Qt6::Multimedia)
