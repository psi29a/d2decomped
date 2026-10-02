# SPDX-License-Identifier: GPL-3.0-or-later
# Header-only ISO 9660 + Joliet reader. Consumed by launcher and by
# tests/test_iso9660.
add_library(iso9660 INTERFACE)
target_include_directories(iso9660 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(iso9660 INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
