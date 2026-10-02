# SPDX-License-Identifier: GPL-3.0-or-later
# Header-only D2 character-save (.d2s) header parser.
add_library(d2s INTERFACE)
target_include_directories(d2s INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(d2s INTERFACE txt)
target_compile_features(d2s INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
