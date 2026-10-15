# spotlight_media: decoding, preview playback and media analysis built on
# Qt Multimedia (FFmpeg backend) plus the ffmpeg command line tool.
add_library(spotlight_media STATIC
    mediaprobe.h mediaprobe.cpp
    videoview.h videoview.cpp
    timelineplayer.h timelineplayer.cpp
    waveformcache.h waveformcache.cpp
)

target_include_directories(spotlight_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(spotlight_media PUBLIC spotlight_core Qt6::Widgets Qt6::Multimedia)
