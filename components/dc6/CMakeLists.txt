# SPDX-License-Identifier: GPL-3.0-or-later
add_library(dc6 INTERFACE)
target_include_directories(dc6 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(dc6 INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
