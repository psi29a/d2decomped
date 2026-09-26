cmake_minimum_required(VERSION 3.28)
project(D2Decomp LANGUAGES CXX)

# MSVC: CMake has no C++26 flag for it; 23 maps to /std:c++latest, the
# newest dialect MSVC offers.
if(MSVC)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# windows.h (via StormLib, SDL) otherwise #defines min/max and, through
# the RPC headers, small; near/far it defines regardless, so the code
# avoids those names.
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Structs here lean on default member initializers and are aggregate-
# initialized with only their leading fields ({ x, y, ... }); -Wextra's
# missing-field-initializers flags exactly that idiom. getenv is fine:
# MSVC's _s variants aren't portable.
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

# Our find modules (FindOpenAL: Homebrew openal-soft first, as ../thirdeye).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# CLI argument parsing — CLI11 (header-only), matches thirdeye.
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.6.2
)
FetchContent_MakeAvailable(cli11)

include(CTest)
enable_testing()

option(D2D_BUILD_APPS  "Build end-user apps under apps/"  ON)
option(D2D_BUILD_TOOLS "Build helper tools under tools/"  ON)
option(D2D_BUILD_TESTS "Build tests under tests/"         ON)

# Components (shared libs) must configure before apps/tests that link them.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/components/CMakeLists.txt")
    add_subdirectory(components)
endif()
if(D2D_BUILD_APPS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/CMakeLists.txt")
    add_subdirectory(apps)
endif()
if(D2D_BUILD_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
    add_subdirectory(tools)
endif()
if(D2D_BUILD_TESTS AND BUILD_TESTING AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
