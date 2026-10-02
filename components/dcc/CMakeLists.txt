# SPDX-License-Identifier: GPL-3.0-or-later
add_library(dcc INTERFACE)
target_include_directories(dcc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(dcc INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
