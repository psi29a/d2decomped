# SPDX-License-Identifier: GPL-3.0-or-later
# Header-only per-user config/save dirs + key=value .cfg reader, matching
# thirdeye's components/files layout.
add_library(userdir INTERFACE)
target_include_directories(userdir INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(userdir INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
if (WIN32)
    target_link_libraries(userdir INTERFACE shell32 ole32)
endif()
