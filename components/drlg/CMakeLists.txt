# SPDX-License-Identifier: GPL-3.0-or-later
# Header-only level generation (game.exe's DRLG): act layout, outdoors.
add_library(drlg INTERFACE)
target_include_directories(drlg INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(drlg INTERFACE rules ds1 txt)
target_compile_features(drlg INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
