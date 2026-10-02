# SPDX-License-Identifier: GPL-3.0-or-later
# Header-only reader for D2's tab-separated excel tables (data\global\excel\*.txt).
add_library(txt INTERFACE)
target_include_directories(txt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(txt INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
