# SPDX-License-Identifier: GPL-3.0-or-later
# Logging (log.hpp): header only, for the game, the apps and the tools.
add_library(log INTERFACE)
target_include_directories(log INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(log INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
