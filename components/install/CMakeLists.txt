# SPDX-License-Identifier: GPL-3.0-or-later
# Finding the user's classic Diablo II in place (registry, default folders,
# macOS apps, Wine prefixes) and refusing D2R. Plain C++, no Qt, no
# StormLib. docs/design/install-detect.md. The target is install_detect:
# CMake reserves "install".
add_library(install_detect STATIC install.cpp)
target_include_directories(install_detect PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(install_detect PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
