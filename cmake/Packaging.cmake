# Installers:
#   macOS : SPOTLIGHT!.app bundle with Qt deployed (macdeployqt) inside a .dmg
#   Linux : .deb and .tar.gz via CPack; AppImage via packaging/linux/build-appimage.sh
set(CPACK_PACKAGE_NAME "spotlight")
set(CPACK_PACKAGE_VENDOR "SPOTLIGHT!")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "All-in-one video, audio and design studio")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "spotlight@users.noreply.github.com")
set(CPACK_RESOURCE_FILE_README ${PROJECT_SOURCE_DIR}/README.md)
set(CPACK_STRIP_FILES ON)

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "SPOTLIGHT!")
    set(CPACK_PACKAGE_FILE_NAME "SPOTLIGHT-${PROJECT_VERSION}-macOS-${CMAKE_SYSTEM_PROCESSOR}")

    # Bundle Qt frameworks + plugins (incl. the FFmpeg multimedia backend).
    find_program(MACDEPLOYQT macdeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(MACDEPLOYQT)
        install(CODE "
            execute_process(COMMAND \"${MACDEPLOYQT}\"
                \"\${CMAKE_INSTALL_PREFIX}/SPOTLIGHT.app\" -always-overwrite
                COMMAND_ERROR_IS_FATAL ANY)
        ")
    endif()

    # Optionally ship an ffmpeg binary for exporting (Exporter looks in
    # Contents/MacOS first): cmake -DSPOTLIGHT_BUNDLE_FFMPEG=/path/to/ffmpeg
    if(SPOTLIGHT_BUNDLE_FFMPEG)
        install(PROGRAMS ${SPOTLIGHT_BUNDLE_FFMPEG} DESTINATION SPOTLIGHT.app/Contents/MacOS)
    endif()
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_PACKAGE_FILE_NAME "spotlight-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "ffmpeg, qt6-qpa-plugins")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "gstreamer1.0-libav")
endif()

include(CPack)
