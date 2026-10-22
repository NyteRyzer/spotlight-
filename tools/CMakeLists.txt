# Developer tools (not shipped). Enable with -DSPOTLIGHT_BUILD_TOOLS=ON.

# Builds a demo project and captures marketing screenshots of each studio:
#   xvfb-run -s "-screen 0 1920x1200x24" build/tools/spotlight-screenshots docs/screenshots
add_executable(spotlight-screenshots screenshots.cpp)
target_link_libraries(spotlight-screenshots PRIVATE spotlight_ui)
