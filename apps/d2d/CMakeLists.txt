# SPDX-License-Identifier: GPL-3.0-or-later
# d2d — the game binary. SDL3 windowing + streaming framebuffer texture;
# devctl + screenshot still wired to the same in-memory buffer.
find_package(SDL3 CONFIG REQUIRED)
# Sound: OpenAL (openal-soft), like ../thirdeye. cmake/FindOpenAL.cmake.
find_package(OpenAL REQUIRED)
# Cinematics: FFmpeg decodes Bink (video.hpp); sound and music are WAVs,
# which SDL decodes. A distro's FFmpeg brings every codec and ~100 MB of
# codec libraries to ship, so by default a static Bink-only FFmpeg (LGPL)
# is built from source: ~25 s once, under 1 MB in d2d. -DD2D_BUNDLED_FFMPEG=OFF uses
# the system's through pkg-config. Windows: off for now, its configure
# wants MSYS2; CI ships BtbN's DLLs there.
if(WIN32)
    set(bundled_ffmpeg_default OFF)
else()
    set(bundled_ffmpeg_default ON)
endif()
option(D2D_BUNDLED_FFMPEG "Build a Bink-only FFmpeg from source" ${bundled_ffmpeg_default})
set(D2D_FFMPEG_VERSION "9.0.2" CACHE STRING "FFmpeg release (source build)")
set(D2D_FFMPEG_SHA256 "8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"
    CACHE STRING "SHA-256 of ffmpeg-<D2D_FFMPEG_VERSION>.tar.xz")
if(D2D_BUNDLED_FFMPEG)
    # Only what Bink needs. No autodetect: nothing of the system's (zlib,
    # VideoToolbox, VAAPI...) creeps in. No protocols: video.hpp reads through
    # its own AVIOContext. No threads: Bink decodes on one. Not off: the safe
    # bitstream reader (bounds checks on the file's data). The intro decodes
    # bit for bit as with FFmpeg's defaults (every pixel and sample hashed).
    set(ffmpeg_options
        --disable-everything --disable-autodetect --disable-programs --disable-doc
        --disable-network --disable-avdevice --disable-avfilter
        --disable-faan --disable-iamf --disable-swscale-alpha
        --disable-pthreads --disable-runtime-cpudetect --disable-debug
        --enable-static --disable-shared --enable-pic
        --disable-x86asm   # no nasm needed; Bink at 640x480 is cheap in C
        --enable-demuxer=bink --enable-decoder=bink,binkaudio_rdft,binkaudio_dct)
    # Named for its version and options, and built only when that build isn't
    # already there: CI caches build/ffmpeg-* (most of the ~25 s is configure).
    string(SHA1 ffmpeg_key "${D2D_FFMPEG_VERSION};${ffmpeg_options}")
    string(SUBSTRING "${ffmpeg_key}" 0 12 ffmpeg_key)
    set(ffmpeg_prefix "${CMAKE_BINARY_DIR}/ffmpeg-${ffmpeg_key}")
    set(ffmpeg_archives "")
    foreach(lib avformat avcodec swscale swresample avutil)   # link order
        list(APPEND ffmpeg_archives "${ffmpeg_prefix}/lib/lib${lib}.a")
    endforeach()
    add_library(ffmpeg INTERFACE)
    if(NOT EXISTS "${ffmpeg_prefix}/built")
        include(ExternalProject)
        cmake_host_system_information(RESULT cores QUERY NUMBER_OF_LOGICAL_CORES)
        ExternalProject_Add(ffmpeg_build
            URL "https://ffmpeg.org/releases/ffmpeg-${D2D_FFMPEG_VERSION}.tar.xz"
            URL_HASH "SHA256=${D2D_FFMPEG_SHA256}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            INSTALL_DIR "${ffmpeg_prefix}"
            CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> ${ffmpeg_options}
            BUILD_COMMAND make -j${cores}
            INSTALL_COMMAND make install COMMAND ${CMAKE_COMMAND} -E touch <INSTALL_DIR>/built
            BUILD_BYPRODUCTS ${ffmpeg_archives})
        add_dependencies(ffmpeg ffmpeg_build)
        file(MAKE_DIRECTORY "${ffmpeg_prefix}/include")   # exists before the build fills it
    endif()
    target_include_directories(ffmpeg SYSTEM INTERFACE "${ffmpeg_prefix}/include")
    target_link_libraries(ffmpeg INTERFACE ${ffmpeg_archives} $<$<PLATFORM_ID:Linux>:m>)
    set(ffmpeg_target ffmpeg)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
    set(ffmpeg_target PkgConfig::FFMPEG)
endif()

# The window/dock icon, as bytes SDL_LoadBMP_IO reads (any SDL3; PNG needs 3.4).
set(icon_bmp "${PROJECT_SOURCE_DIR}/apps/icon/d2d.bmp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${icon_bmp}")
file(READ "${icon_bmp}" icon_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," icon_bytes "${icon_hex}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/d2d_icon.hpp" CONTENT
    "#pragma once\n// generated from apps/icon/d2d.bmp\ninline constexpr unsigned char kIconBmp[] = {${icon_bytes}};\n")

add_executable(d2d
    main.cpp common.cpp cursor.cpp frontend.cpp gamemenu.cpp ingame.cpp items.cpp load.cpp panels.cpp
    netgame.cpp scene.cpp skilltree.cpp store.cpp town.cpp ui.cpp window.cpp world_view.cpp
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/apps/icon/d2d.rc>)
target_include_directories(d2d PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(d2d PRIVATE
    ${ffmpeg_target}   # first: its headers before a system FFmpeg's in /opt/homebrew/include
    game net mpq d2s rules drlg txt userdir cof compcode dc6 dcc ds1 dt1 font palette tbl devctl screenshot
    SDL3::SDL3 CLI11::CLI11 ${OPENAL_LIBRARY})
target_include_directories(d2d SYSTEM PRIVATE ${OPENAL_INCLUDE_DIR})
d2d_check_headers(d2d_headers LINK
    ${ffmpeg_target}   # first: its headers before a system FFmpeg's in /opt/homebrew/include
    game net mpq d2s rules drlg txt userdir cof compcode dc6 dcc ds1 dt1 font palette tbl devctl screenshot
    SDL3::SDL3 CLI11::CLI11 ${OPENAL_LIBRARY})
if(TARGET d2d_headers)
    target_include_directories(d2d_headers SYSTEM PRIVATE ${OPENAL_INCLUDE_DIR})
endif()
