# SPDX-License-Identifier: GPL-3.0-or-later
# Logging (log.hpp; the file, log_file.hpp), for the game, the apps and the
# tools. The clock and the file are in log.cpp, out of the header most of
# the tree includes.
add_library(log STATIC log.cpp)
target_include_directories(log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(log PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
