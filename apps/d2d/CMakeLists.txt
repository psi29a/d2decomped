# SPDX-License-Identifier: GPL-3.0-or-later
# d2d — the game binary. SDL3 windowing + streaming framebuffer texture;
# devctl + screenshot still wired to the same in-memory buffer.
find_package(SDL3 CONFIG REQUIRED)
# Sound: OpenAL (openal-soft), like ../thirdeye. cmake/FindOpenAL.cmake.
find_package(OpenAL REQUIRED)
# Cinematics: FFmpeg decodes Bink (video.hpp); sound and music are WAVs,
# which SDL decodes. A distro's FFmpeg brings every codec and ~100 MB of
# codec libraries to ship, so by default a static Bink-only FFmpeg (LGPL)
# is built from source: ~40 s, ~1 MB in d2d. -DD2D_BUNDLED_FFMPEG=OFF uses
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
    include(ExternalProject)
    set(ffmpeg_prefix "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg")
    set(ffmpeg_archives "")
    foreach(lib avformat avcodec swscale swresample avutil)   # link order
        list(APPEND ffmpeg_archives "${ffmpeg_prefix}/lib/lib${lib}.a")
    endforeach()
    cmake_host_system_information(RESULT cores QUERY NUMBER_OF_LOGICAL_CORES)
    # No autodetect: nothing of the system's (zlib, VideoToolbox, VAAPI...)
    # creeps in. No protocols: video.hpp reads through its own AVIOContext.
    ExternalProject_Add(ffmpeg_build
        URL "https://ffmpeg.org/releases/ffmpeg-${D2D_FFMPEG_VERSION}.tar.xz"
        URL_HASH "SHA256=${D2D_FFMPEG_SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        INSTALL_DIR "${ffmpeg_prefix}"
        CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>
            --disable-everything --disable-autodetect --disable-programs --disable-doc
            --disable-network --disable-avdevice --disable-avfilter
            --enable-static --disable-shared --enable-pic
            --disable-x86asm   # no nasm needed; Bink at 640x480 is cheap in C
            --enable-demuxer=bink --enable-decoder=bink,binkaudio_rdft,binkaudio_dct
        BUILD_COMMAND make -j${cores}
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${ffmpeg_archives})
    file(MAKE_DIRECTORY "${ffmpeg_prefix}/include")   # exists before the build fills it
    find_package(Threads REQUIRED)
    add_library(ffmpeg INTERFACE)
    add_dependencies(ffmpeg ffmpeg_build)
    target_include_directories(ffmpeg SYSTEM INTERFACE "${ffmpeg_prefix}/include")
    target_link_libraries(ffmpeg INTERFACE ${ffmpeg_archives} Threads::Threads $<$<PLATFORM_ID:Linux>:m>)
    set(ffmpeg_target ffmpeg)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
    set(ffmpeg_target PkgConfig::FFMPEG)
endif()

add_executable(d2d
    main.cpp common.cpp cursor.cpp frontend.cpp gamemenu.cpp ingame.cpp items.cpp load.cpp panels.cpp
    netgame.cpp scene.cpp skilltree.cpp store.cpp town.cpp ui.cpp window.cpp world_view.cpp)
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
