# SPDX-License-Identifier: GPL-3.0-or-later
# d2d — the game binary. SDL3 windowing + streaming framebuffer texture;
# devctl + screenshot still wired to the same in-memory buffer.
find_package(SDL3 CONFIG REQUIRED)
# Sound: OpenAL (openal-soft), like ../thirdeye. cmake/FindOpenAL.cmake.
find_package(OpenAL REQUIRED)

# Crash reports: Sentry Native with its Crashpad handler, built here from
# source, or (the Flatpak) prebuilt as its own module, which flatpak-builder
# then caches between builds; that module installs the licenses itself.
set(D2D_SENTRY_PREFIX "" CACHE PATH
    "Use the static crashpad Sentry Native installed here instead of building it (Flatpak)")
if(D2D_SENTRY_PREFIX)
    if(NOT D2D_FLATPAK)
        message(FATAL_ERROR "D2D_SENTRY_PREFIX is for the Flatpak: the packages ship crashpad_handler from this build")
    endif()
    find_package(sentry CONFIG REQUIRED PATHS "${D2D_SENTRY_PREFIX}" NO_DEFAULT_PATH)
    add_library(sentry ALIAS sentry::sentry)
else()
    set(SENTRY_BACKEND crashpad CACHE STRING "Sentry crash-reporting backend" FORCE)
    set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "Build Sentry Native statically" FORCE)
    set(SENTRY_BUILD_TESTS OFF CACHE BOOL "Build Sentry Native tests" FORCE)
    set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "Build Sentry Native examples" FORCE)
    set(SENTRY_BUILD_BENCHMARKS OFF CACHE BOOL "Build Sentry Native benchmarks" FORCE)
    set(SENTRY_ENABLE_INSTALL OFF CACHE BOOL "Install Sentry Native" FORCE)
    set(SENTRY_SCREENSHOT none CACHE STRING "Do not capture crash screenshots" FORCE)
    FetchContent_Declare(
        sentry_native
        GIT_REPOSITORY https://github.com/getsentry/sentry-native.git
        GIT_TAG        0.17.3
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES external/crashpad external/third_party/lss
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(sentry_native)

    # The project treats its own warnings as errors. Do not let that policy turn
    # warnings in the separately maintained Sentry/Crashpad sources into failures.
    function(d2d_allow_vendor_warnings directory)
        get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target IN LISTS targets)
            get_target_property(target_type ${target} TYPE)
            if(NOT target_type STREQUAL "INTERFACE_LIBRARY")
                if(MSVC)
                    target_compile_options(${target} PRIVATE
                        "$<$<COMPILE_LANGUAGE:C,CXX>:/WX->")
                else()
                    target_compile_options(${target} PRIVATE
                        "$<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error>")
                endif()
            endif()
        endforeach()
        get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
        foreach(subdirectory IN LISTS subdirectories)
            d2d_allow_vendor_warnings("${subdirectory}")
        endforeach()
    endfunction()
    d2d_allow_vendor_warnings("${sentry_native_BINARY_DIR}")
    install(FILES "${sentry_native_SOURCE_DIR}/LICENSE"
        DESTINATION share/licenses/sentry-native)
    install(FILES "${sentry_native_SOURCE_DIR}/external/crashpad/LICENSE"
        DESTINATION share/licenses/crashpad)
    install(FILES "${sentry_native_SOURCE_DIR}/external/crashpad/third_party/mini_chromium/mini_chromium/LICENSE"
        DESTINATION share/licenses/mini-chromium)
    install(FILES "${sentry_native_SOURCE_DIR}/external/crashpad/third_party/lss/lss/LICENSE"
        DESTINATION share/licenses/crashpad-lss)
    install(FILES "${sentry_native_SOURCE_DIR}/external/third_party/lss/LICENSE"
        DESTINATION share/licenses/sentry-lss)
    install(FILES "${sentry_native_SOURCE_DIR}/external/crashpad/third_party/zlib/zlib/LICENSE"
        DESTINATION share/licenses/crashpad-zlib)
    install(FILES "${sentry_native_SOURCE_DIR}/external/crashpad/third_party/cpp-httplib/cpp-httplib/LICENSE"
        DESTINATION share/licenses/cpp-httplib)
    install(FILES "${sentry_native_SOURCE_DIR}/external/crashpad/third_party/getopt/LICENSE"
        DESTINATION share/licenses/crashpad-getopt)
    install(FILES "${sentry_native_SOURCE_DIR}/external/crashpad/third_party/mpack/LICENSE"
        DESTINATION share/licenses/crashpad-mpack)
    install(FILES "${sentry_native_SOURCE_DIR}/vendor/libunwind/COPYING"
        DESTINATION share/licenses/sentry-libunwind)
endif()

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
set(D2D_FFMPEG_SOURCE_DIR "" CACHE PATH "Use this prepared FFmpeg source tree instead of downloading it")
# The Flatpak builds the same Bink-only FFmpeg as its own module (cached
# between builds) and links it from /app: its link order, not pkg-config's.
set(D2D_FFMPEG_PREFIX "" CACHE PATH "Use the Bink-only static FFmpeg already installed here")
if(D2D_BUNDLED_FFMPEG)
    if(D2D_FLATPAK AND NOT D2D_FFMPEG_PREFIX AND
       (NOT D2D_FFMPEG_SOURCE_DIR OR NOT EXISTS "${D2D_FFMPEG_SOURCE_DIR}/configure"))
        message(FATAL_ERROR
            "Flatpak builds must provide FFmpeg locally with -DD2D_FFMPEG_SOURCE_DIR=<source dir>")
    endif()
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
    if(D2D_FFMPEG_PREFIX)
        set(ffmpeg_prefix "${D2D_FFMPEG_PREFIX}")
    else()
        set(ffmpeg_prefix "${CMAKE_BINARY_DIR}/ffmpeg-${ffmpeg_key}")
    endif()
    set(ffmpeg_build_dir "${ffmpeg_prefix}/build")
    if(D2D_FFMPEG_SOURCE_DIR)
        set(ffmpeg_source_dir "${D2D_FFMPEG_SOURCE_DIR}")
        set(ffmpeg_source_args
            SOURCE_DIR "${ffmpeg_source_dir}"
            DOWNLOAD_COMMAND ""
            UPDATE_COMMAND ""
            PATCH_COMMAND "")
    else()
        set(ffmpeg_source_dir "${ffmpeg_prefix}/src")
        set(ffmpeg_source_args
            SOURCE_DIR "${ffmpeg_source_dir}"
            URL "https://ffmpeg.org/releases/ffmpeg-${D2D_FFMPEG_VERSION}.tar.xz"
            URL_HASH "SHA256=${D2D_FFMPEG_SHA256}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    endif()
    set(ffmpeg_archives "")
    foreach(lib avformat avcodec swscale swresample avutil)   # link order
        list(APPEND ffmpeg_archives "${ffmpeg_prefix}/lib/lib${lib}.a")
    endforeach()
    add_library(ffmpeg INTERFACE)
    if(D2D_FFMPEG_PREFIX)
        foreach(archive IN LISTS ffmpeg_archives)
            if(NOT EXISTS "${archive}")
                message(FATAL_ERROR "D2D_FFMPEG_PREFIX: no ${archive}")
            endif()
        endforeach()
    elseif(NOT EXISTS "${ffmpeg_prefix}/built")
        include(ExternalProject)
        cmake_host_system_information(RESULT cores QUERY NUMBER_OF_LOGICAL_CORES)
        ExternalProject_Add(ffmpeg_build
            ${ffmpeg_source_args}
            BINARY_DIR "${ffmpeg_build_dir}"
            CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${ffmpeg_prefix} ${ffmpeg_options}
            BUILD_COMMAND make -j${cores}
            INSTALL_COMMAND make install COMMAND ${CMAKE_COMMAND} -E touch "${ffmpeg_prefix}/built"
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
set(icon_bmp "${PROJECT_SOURCE_DIR}/assets/icon/d2d.bmp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${icon_bmp}")
file(READ "${icon_bmp}" icon_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," icon_bytes "${icon_hex}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/d2d_icon.hpp" CONTENT
    "#pragma once\n// generated from assets/icon/d2d.bmp\ninline constexpr unsigned char kIconBmp[] = {${icon_bytes}};\n")

# The log's preamble (main.cpp): git hash, branch, exact tag ("null" off
# one) and build date, restamped on each commit / checkout (the launcher's
# CONFIGURE_DEPENDS on .git/logs/HEAD). A CI checkout is a detached HEAD:
# the branch comes from GitHub's env then.
# ponytail: the date is configure time (SOURCE_DATE_EPOCH honoured), not
# each rebuild's; a build-time stamp would relink d2d every build.
find_package(Git QUIET)
set(D2D_GIT_HASH "unknown")
set(D2D_GIT_BRANCH "unknown")
set(D2D_GIT_TAG "null")
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE git_hash OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE git_branch OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE git_tag OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(git_branch STREQUAL "HEAD")
        if(DEFINED ENV{GITHUB_HEAD_REF} AND NOT "$ENV{GITHUB_HEAD_REF}" STREQUAL "")
            set(git_branch "$ENV{GITHUB_HEAD_REF}")
        elseif("$ENV{GITHUB_REF_TYPE}" STREQUAL "branch")   # a tag's job keeps "HEAD"
            set(git_branch "$ENV{GITHUB_REF_NAME}")
        endif()
    endif()
    if(git_hash)
        set(D2D_GIT_HASH "${git_hash}")
    endif()
    if(git_branch)
        set(D2D_GIT_BRANCH "${git_branch}")
    endif()
    if(git_tag)
        set(D2D_GIT_TAG "${git_tag}")
    endif()
endif()
string(TIMESTAMP D2D_BUILD_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)
# @VAR@ substituted with ESCAPE_QUOTES: a ref name may hold a quote.
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/build_info.hpp" CONTENT
    "#pragma once\n// generated by apps/d2d/CMakeLists.txt\nnamespace d2d::build {\ninline constexpr auto kVersion = \"@D2D_VERSION@\";\ninline constexpr auto kGitHash = \"@D2D_GIT_HASH@\";\ninline constexpr auto kGitBranch = \"@D2D_GIT_BRANCH@\";\ninline constexpr auto kGitTag = \"@D2D_GIT_TAG@\";\ninline constexpr auto kDate = \"@D2D_BUILD_DATE@\";\n}\n"
    @ONLY ESCAPE_QUOTES)

add_executable(d2d
    main.cpp common.cpp controls.cpp cursor.cpp frontend.cpp gamemenu.cpp gamepad.cpp help.cpp hireling.cpp lobby.cpp party.cpp ingame.cpp items.cpp load.cpp panels.cpp
    netgame.cpp roster.cpp scene.cpp skilltree.cpp store.cpp town.cpp tile_blit.cpp ui.cpp window.cpp world_view.cpp
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/assets/icon/d2d.rc>)
target_include_directories(d2d PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
# The version reaches main.cpp through build_info.hpp, not a define: a
# define changes every commit and misses ccache for all of d2d's sources.
target_compile_definitions(d2d PRIVATE D2D_WITH_SENTRY)
target_link_libraries(d2d PRIVATE
    ${ffmpeg_target}   # first: its headers before a system FFmpeg's in /opt/homebrew/include
    game net mpq d2s rules drlg txt userdir cof compcode dc6 dcc ds1 dt1 font palette tbl devctl screenshot switches
    SDL3::SDL3 CLI11::CLI11 ${OPENAL_LIBRARY} sentry)
target_include_directories(d2d SYSTEM PRIVATE ${OPENAL_INCLUDE_DIR})
# start_sentry looks for the handler beside d2d; the packages put it there,
# this does for the build tree (sentry_init fails without it).
if(TARGET crashpad_handler)
    add_dependencies(d2d crashpad_handler)
    add_custom_command(TARGET d2d POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:crashpad_handler>" "$<TARGET_FILE_DIR:d2d>")
endif()
d2d_check_headers(d2d_headers LINK
    ${ffmpeg_target}   # first: its headers before a system FFmpeg's in /opt/homebrew/include
    game net mpq d2s rules drlg txt userdir cof compcode dc6 dcc ds1 dt1 font palette tbl devctl screenshot switches
    SDL3::SDL3 CLI11::CLI11 ${OPENAL_LIBRARY})
if(TARGET d2d_headers)
    target_include_directories(d2d_headers SYSTEM PRIVATE ${OPENAL_INCLUDE_DIR})
endif()
