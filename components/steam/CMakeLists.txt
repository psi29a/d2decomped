# SPDX-License-Identifier: GPL-3.0-or-later
# Header-only reader/writer for Steam's shortcuts.vdf (the launcher's
# "Add to Steam"). No deps.
add_library(steam_shortcuts INTERFACE)
target_include_directories(steam_shortcuts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(steam_shortcuts INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
