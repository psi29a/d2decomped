# SPDX-License-Identifier: GPL-3.0-or-later
# d2d — the game binary. SDL3 windowing + streaming framebuffer texture;
# devctl + screenshot still wired to the same in-memory buffer.
find_package(SDL3 CONFIG REQUIRED)
# Sound: OpenAL (openal-soft), like ../thirdeye. cmake/FindOpenAL.cmake.
find_package(OpenAL REQUIRED)
# Cinematics: ffmpeg decodes Bink (binkvideo / binkaudio_rdft). video.hpp.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)

add_executable(d2d
    main.cpp common.cpp cursor.cpp frontend.cpp gamemenu.cpp ingame.cpp items.cpp load.cpp panels.cpp
    netgame.cpp scene.cpp skilltree.cpp store.cpp town.cpp ui.cpp window.cpp world_view.cpp)
target_link_libraries(d2d PRIVATE
    game net mpq d2s rules drlg txt userdir cof compcode dc6 dcc ds1 dt1 font palette tbl devctl screenshot
    SDL3::SDL3 CLI11::CLI11 ${OPENAL_LIBRARY} PkgConfig::FFMPEG)
target_include_directories(d2d SYSTEM PRIVATE ${OPENAL_INCLUDE_DIR})
d2d_check_headers(d2d_headers LINK
    game net mpq d2s rules drlg txt userdir cof compcode dc6 dcc ds1 dt1 font palette tbl devctl screenshot
    SDL3::SDL3 CLI11::CLI11 ${OPENAL_LIBRARY} PkgConfig::FFMPEG)
if(TARGET d2d_headers)
    target_include_directories(d2d_headers SYSTEM PRIVATE ${OPENAL_INCLUDE_DIR})
endif()
