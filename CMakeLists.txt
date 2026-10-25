# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.28)
project(D2Decomp LANGUAGES C CXX) # Bundled StormLib contains both C and C++ sources.

# MSVC: CMake has no C++26 flag for it; 23 maps to /std:c++latest, the
# newest dialect MSVC offers.
# -DCMAKE_CXX_STANDARD=23 overrides it (the Coverity scan: its 2024.12
# front end reads -std=c++26 as C++17).
if(DEFINED CMAKE_CXX_STANDARD)
elseif(MSVC)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No C++ modules here. Scanning for them (CMake's default at C++20+) puts
# -fmodules-ts & co. on GCC's compile lines, which ccache won't cache.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The one version stamp: the launcher, d2d, Sentry and the package all read
# D2D_VERSION. git describe, else a VERSION file (CI's flatpak builds a git
# archive, no .git), else unknown.
find_package(Git QUIET)
set(D2D_VERSION "0.0.0-unknown")
if(EXISTS "${PROJECT_SOURCE_DIR}/VERSION")
    file(STRINGS "${PROJECT_SOURCE_DIR}/VERSION" D2D_VERSION LIMIT_COUNT 1)
endif()
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE git_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(git_describe)
        set(D2D_VERSION "${git_describe}")
    endif()
    # Re-stamp on each commit/checkout, not only on a fresh configure.
    if(EXISTS "${PROJECT_SOURCE_DIR}/.git/logs/HEAD")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/.git/logs/HEAD")
    endif()
endif()
string(REGEX REPLACE "-[0-9]+-g" "-g" D2D_VERSION "${D2D_VERSION}")   # drop commits-since-tag
message(STATUS "D2D version: ${D2D_VERSION}")

# windows.h (via StormLib, SDL) otherwise #defines min/max and, through
# the RPC headers, small; near/far it defines regardless, so the code
# avoids those names.
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
# The near/far guard: off Windows, cmake/no_near_far.hpp makes either name a
# compile error ("'near' is a windows.h macro"), so it fails here and not
# only in the MSVC CI.
if(NOT WIN32)
    add_compile_options("SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/cmake/no_near_far.hpp")
endif()

# Structs here lean on default member initializers and are aggregate-
# initialized with only their leading fields ({ x, y, ... }); -Wextra's
# missing-field-initializers flags exactly that idiom. getenv is fine:
# MSVC's _s variants aren't portable.
# Warnings are errors (D2D_WERROR, on by default): switch it off to build
# with a newer compiler that finds new ones.
option(D2D_WERROR "Treat compiler warnings as errors" ON)
if(MSVC)
    # /bigobj: d2d's translation units are large (each includes the app's
    # headers), past COFF's default section limit (C1128).
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C,CXX>:/W4>"
        "$<$<COMPILE_LANGUAGE:CXX>:/permissive->"
        "$<$<COMPILE_LANGUAGE:C,CXX>:/bigobj>")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    if(D2D_WERROR)
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/WX>")
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
    if(D2D_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

# Our find modules (FindOpenAL: Homebrew openal-soft first, as ../thirdeye).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(HeaderCheck)

option(D2D_FLATPAK "Install apps using Flatpak's runtime and filesystem layout" OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# CLI argument parsing — CLI11 (header-only), matches thirdeye.
if(D2D_FLATPAK AND
   (NOT FETCHCONTENT_SOURCE_DIR_CLI11 OR
    NOT EXISTS "${FETCHCONTENT_SOURCE_DIR_CLI11}/CMakeLists.txt"))
    message(FATAL_ERROR
        "Flatpak builds must provide CLI11 locally with -DFETCHCONTENT_SOURCE_DIR_CLI11=<source dir>")
endif()
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.6.2
)
FetchContent_MakeAvailable(cli11)

include(CTest)
enable_testing()

option(D2D_BUILD_APPS  "Build end-user apps under apps/"  ON)
if(D2D_FLATPAK)
    set(_d2d_tools_default OFF)
    set(_d2d_tests_default OFF)
else()
    set(_d2d_tools_default ON)
    set(_d2d_tests_default ON)
endif()
option(D2D_BUILD_TOOLS "Build helper tools under tools/"  ${_d2d_tools_default})
option(D2D_BUILD_TESTS "Build tests under tests/"         ${_d2d_tests_default})

# Components (shared libs) must configure before apps/tests that link them.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/components/CMakeLists.txt")
    add_subdirectory(components)
endif()
# Tools before apps: the package (apps/CMakeLists.txt) ships some of them.
if(D2D_BUILD_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
    add_subdirectory(tools)
endif()
if(D2D_BUILD_APPS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/CMakeLists.txt")
    add_subdirectory(apps)
endif()
if(D2D_BUILD_TESTS AND BUILD_TESTING AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
