# SPDX-License-Identifier: GPL-3.0-or-later
# game.exe one-dash switches, translated before CLI11 sees argv.
add_library(switches STATIC switches.cpp)
target_include_directories(switches PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(switches PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
d2d_check_headers(switches_headers)
