# SPDX-License-Identifier: GPL-3.0-or-later
# Off macOS the binaries sit flat in the package root, and on Linux their
# DT_RPATH (below) finds the bundled Qt for its plugins too, so Qt's deploy
# needn't give each plugin an rpath (that wants patchelf). Qt's deploy reads
# both where Qt is first found: the launcher.
if(NOT APPLE)
    set(CMAKE_INSTALL_BINDIR ".")
endif()
set(QT_DEPLOY_FORCE_ADJUST_RPATHS OFF)
add_subdirectory(launcher)
add_subdirectory(d2d)
if(NOT D2D_FLATPAK)
    add_subdirectory(d2proxy)
endif()

function(d2d_remove_crashpad_build_artifacts)
    install(CODE "
        set(prefix \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
        foreach(libdir lib lib64)
            set(libdir \"\${prefix}/\${libdir}\")
            file(REMOVE_RECURSE \"\${libdir}/cmake/sentry\")
            file(GLOB archives
                \"\${libdir}/libcrashpad_*.a\"
                \"\${libdir}/libmini_chromium.a\"
                \"\${libdir}/crashpad_*.lib\"
                \"\${libdir}/mini_chromium.lib\")
            foreach(archive IN LISTS archives)
                file(REMOVE \"\${archive}\")
            endforeach()
        endforeach()")
endfunction()

if(D2D_FLATPAK)
    # Flatpak supplies Qt and the desktop integration through its runtime.
    # Install the app entry point and engine side by side so the launcher can
    # continue resolving d2d from its own executable directory.
    set(D2D_FLATPAK_APP_ID "io.github.psi29a.d2decomped" CACHE STRING
        "Flatpak application ID (provisional until confirmed for Flathub)")
    set(_flatpak_desktop "${CMAKE_CURRENT_BINARY_DIR}/${D2D_FLATPAK_APP_ID}.desktop")
    set(_flatpak_metainfo "${CMAKE_CURRENT_BINARY_DIR}/${D2D_FLATPAK_APP_ID}.metainfo.xml")
    configure_file("${PROJECT_SOURCE_DIR}/assets/flatpak/desktop.in" "${_flatpak_desktop}" @ONLY)
    configure_file("${PROJECT_SOURCE_DIR}/assets/flatpak/metainfo.xml.in" "${_flatpak_metainfo}" @ONLY)

    install(TARGETS d2d-launcher d2d RUNTIME DESTINATION bin)
    if(NOT D2D_SENTRY_PREFIX)   # a prebuilt Sentry put its handler in bin itself
        install(TARGETS crashpad_handler RUNTIME DESTINATION bin)
    endif()
    install(FILES "${_flatpak_desktop}" DESTINATION share/applications)
    install(FILES "${_flatpak_metainfo}" DESTINATION share/metainfo)
    install(FILES "${PROJECT_SOURCE_DIR}/assets/icon/d2d.png"
        DESTINATION "share/icons/hicolor/256x256/apps"
        RENAME "${D2D_FLATPAK_APP_ID}.png")
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
        DESTINATION "share/licenses/${D2D_FLATPAK_APP_ID}")
    d2d_remove_crashpad_build_artifacts()
    return()
endif()

# --- packaging: `cmake --build build --target package` -----------------------
# The launcher is what a user runs; d2d sits next to it (engineBinaryPath
# looks beside the launcher), with every library both need:
#   macOS   D2D.app (the launcher), d2d in Contents/MacOS, in a .dmg
#   Linux   d2d-launcher, d2d, lib/, plugins/, qt.conf in a .tar.gz
#   Windows d2d-launcher.exe, d2d.exe, the DLLs, Qt's plugin dirs in a .zip
# The dev tools (d2proxy, mpq-cat, drlg-dump) ride along beside d2d, on the
# same bundled libraries.
# d2d alone in a Diablo II folder (as game.exe) works too, but isn't this.
find_package(Qt6 6.5 COMPONENTS Core REQUIRED)
string(REGEX REPLACE "^v" "" D2D_VERSION "${D2D_VERSION}")   # tag v0.1.0 -> 0.1.0
set(tools d2proxy)
foreach(tool mpq-cat drlg-dump)
    if(TARGET ${tool})   # -DD2D_BUILD_TOOLS=OFF
        list(APPEND tools ${tool})
    endif()
endforeach()
set(beside_d2d d2d crashpad_handler ${tools})
install(TARGETS d2d-launcher BUNDLE DESTINATION . RUNTIME DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/README.md" DESTINATION .)

if(APPLE)
    set_target_properties(d2d-launcher PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "D2D"   # the menu bar's name
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${D2D_VERSION}")
    # Some Homebrew dylibs load their siblings by @rpath (brotli, webp);
    # macdeployqt puts them all in Frameworks.
    set_target_properties(d2d-launcher ${beside_d2d} PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
    # Finder's name. Only the bundle: an executable D2D would be d2d's file
    # on a case-insensitive disk.
    install(CODE "
        set(dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
        file(REMOVE_RECURSE \"\${dir}/D2D.app\")
        file(RENAME \"\${dir}/d2d-launcher.app\" \"\${dir}/D2D.app\")")
    install(TARGETS ${beside_d2d} RUNTIME DESTINATION D2D.app/Contents/MacOS)
    set(deploy_args "EXECUTABLE D2D.app ADDITIONAL_EXECUTABLES")
    foreach(target IN LISTS beside_d2d)
        string(APPEND deploy_args " \"D2D.app/Contents/MacOS/$<TARGET_FILE_NAME:${target}>\"")
    endforeach()
elseif(WIN32)
    # windeployqt brings Qt for the launcher; d2d's DLLs (SDL3, OpenAL32,
    # FFmpeg, zlib, StormLib), which cover the tools', are the ones staged
    # beside it (CI does that). CMake 4.3+ warns per DLL until told to
    # normalize slashes; the System32 regex matches either.
    if(POLICY CMP0207)
        cmake_policy(SET CMP0207 NEW)
    endif()
    install(TARGETS ${beside_d2d} RUNTIME_DEPENDENCY_SET d2d_dlls RUNTIME DESTINATION .)
    install(RUNTIME_DEPENDENCY_SET d2d_dlls DESTINATION .
        PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
        POST_EXCLUDE_REGEXES "[Ss]ystem32"
        DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/d2d")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
    include(InstallRequiredSystemLibraries)   # vcruntime140.dll, msvcp140.dll
    set(deploy_args "EXECUTABLE \"$<TARGET_FILE_NAME:d2d-launcher>\"")
else()
    set(beside_d2d_files "")
    foreach(target IN LISTS beside_d2d)
        string(APPEND beside_d2d_files " \"$<TARGET_FILE_NAME:${target}>\"")
    endforeach()
    # DT_RPATH, not RUNPATH: it also serves the bundled libraries' own
    # dependencies (libavcodec's codecs, Qt's xcb libs).
    set_target_properties(d2d-launcher ${beside_d2d} PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
    foreach(target d2d-launcher ${beside_d2d})
        target_link_options(${target} PRIVATE "LINKER:--disable-new-dtags")
    endforeach()
    install(TARGETS ${beside_d2d} RUNTIME DESTINATION .)
    # Qt's default skips everything in the system lib dirs, i.e. all of it on
    # a distro. Ship all but what every desktop has and must match the
    # system: glibc, GL, X11 / Wayland, the sound servers.
    set(deploy_args "EXECUTABLE \"$<TARGET_FILE_NAME:d2d-launcher>\"
        ADDITIONAL_EXECUTABLES ${beside_d2d_files}
        POST_EXCLUDE_REGEXES
            \"/ld-linux\" \"/lib(c|m|dl|rt|pthread|resolv|util)[.]so\"
            \"/lib(GL|GLX|GLdispatch|EGL|OpenGL|drm|gbm|vulkan)[.]so\"
            \"/lib(X11|X11-xcb|xcb|wayland-[a-z]+)[.]so\"
            \"/lib(asound|pulse|pulse-simple|jack|pipewire-[0-9.]+)[.]so\"
            \"/lib(dbus-1|udev|systemd|fontconfig|freetype|expat)[.]so\"")
endif()

# Qt's deploy tool: macdeployqt (also d2d's dylibs), windeployqt, or
# Qt's generic one on Linux (also d2d's .so files).
qt_generate_deploy_script(TARGET d2d-launcher OUTPUT_SCRIPT deploy_script CONTENT "
qt_deploy_runtime_dependencies(${deploy_args}
    LIB_DIR lib PLUGINS_DIR plugins
    GENERATE_QT_CONF NO_TRANSLATIONS NO_COMPILER_RUNTIME)")
install(SCRIPT "${deploy_script}")
d2d_remove_crashpad_build_artifacts()

if(APPLE)
    # macdeployqt copies every plugin Homebrew has; the launcher needs only
    # cocoa, the mac style and Apple's TLS / network info. Gone: other
    # modules' (pdf, svg, virtual keyboard, which brought QtQuick and QtQml,
    # 10 MB), image formats (its images are PNG, built into QtGui), glib
    # network info, OpenSSL TLS, and the dylibs only those wanted. Then
    # re-sign ad hoc, as
    # macdeployqt rewrote install names (as ../thirdeye): Gatekeeper then
    # offers "Open anyway" instead of "damaged".
    install(CODE "
        set(app \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/D2D.app\")
        foreach(dir imageformats iconengines platforminputcontexts)
            file(REMOVE_RECURSE \"\${app}/Contents/PlugIns/\${dir}\")
        endforeach()
        file(REMOVE \"\${app}/Contents/PlugIns/networkinformation/libqglib.dylib\"
            \"\${app}/Contents/PlugIns/tls/libqopensslbackend.dylib\"
            \"\${app}/Contents/PlugIns/tls/libqcertonlybackend.dylib\")
        foreach(lib gio-2.0.0 gmodule-2.0.0 gobject-2.0.0 jasper.7 jpeg.8 lcms2.2
                lzma.5 mng.2 sharpyuv.0 tiff.6 webp.7 webpdemux.2 webpmux.3)
            file(REMOVE \"\${app}/Contents/Frameworks/lib\${lib}.dylib\")
        endforeach()
        foreach(module Quick Qml QmlMeta QmlModels QmlWorkerScript OpenGL)
            file(REMOVE_RECURSE \"\${app}/Contents/Frameworks/Qt\${module}.framework\")
        endforeach()
        execute_process(COMMAND codesign --force --deep --sign - \"\${app}\" COMMAND_ERROR_IS_FATAL ANY)")
elseif(UNIX)
    # A bundled library's own RUNPATH (libproxy's points into /usr/lib) hides
    # the binaries' DT_RPATH from its dependencies; drop it.
    install(CODE "
        file(GLOB libs \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/lib/*.so*\")
        foreach(lib IN LISTS libs)
            if(NOT IS_SYMLINK \"\${lib}\")
                file(RPATH_REMOVE FILE \"\${lib}\")
            endif()
        endforeach()")
endif()

set(CPACK_PACKAGE_NAME "d2d")
set(CPACK_PACKAGE_VENDOR "D2Decomp")
set(CPACK_PACKAGE_VERSION "${D2D_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "d2d-${D2D_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
