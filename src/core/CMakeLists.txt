# spotlight_core: the document model, timeline editing, persistence and export
# command generation. No widgets and no multimedia playback live here, so this
# library is fully unit-testable.
add_library(spotlight_core STATIC
    types.h types.cpp
    media.h
    timeline.h
    design.h design.cpp
    projectdata.h projectdata.cpp
    timelineops.h timelineops.cpp
    project.h project.cpp
    autosave.h autosave.cpp
    exportpreset.h exportpreset.cpp
    exporter.h exporter.cpp
    shortcuts.h shortcuts.cpp
    imageops.h imageops.cpp
    designrender.h designrender.cpp
)

target_include_directories(spotlight_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(spotlight_core PUBLIC Qt6::Core Qt6::Gui)
target_compile_definitions(spotlight_core PUBLIC
    SPOTLIGHT_VERSION="${PROJECT_VERSION}")
