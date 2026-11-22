# SPDX-License-Identifier: GPL-3.0-or-later
# The D2 string-table (.tbl) parser (header-only) and game.exe's second
# UTF-8 reader (unicode.cpp).
add_library(tbl STATIC unicode.cpp)
target_include_directories(tbl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tbl PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
