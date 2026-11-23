# SPDX-License-Identifier: GPL-3.0-or-later
# Level generation (game.exe's DRLG): act layout, outdoors, mazes, room
# tiles. The bodies are in the .cpp files: ~250 files include these headers.
add_library(drlg STATIC
    # One a line, in name order: a new file goes where it sorts.
    drlg.cpp
    maze.cpp
    outdoor.cpp
    outdoor_data.cpp
    room_tiles.cpp
    tile_pick.cpp
    units.cpp)
target_include_directories(drlg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(drlg PUBLIC rules ds1 txt)
target_compile_features(drlg PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
