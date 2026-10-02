# SPDX-License-Identifier: GPL-3.0-or-later
add_library(dt1 INTERFACE)
target_include_directories(dt1 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(dt1 INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
