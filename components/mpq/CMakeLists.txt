# StormLib: the system's when there is one (Debian / Ubuntu libstorm-dev,
# Homebrew stormlib, vcpkg stormlib), else built from source. Every call
# d2d makes (SFileOpenArchive, SFileOpenFileEx, SFileReadFile, ...) is in
# StormLib 9.22, Debian stable's. -DD2D_BUNDLED_STORMLIB=ON forces the
# source build. The packages ship no CMake config or pkg-config file
# (Debian: StormLib.h and libstorm.so only), so it's found by name.
option(D2D_BUNDLED_STORMLIB "Build StormLib from source instead of using the system's" OFF)
set(STORMLIB_TAG "v9.30" CACHE STRING "StormLib git tag (source build)")

if(NOT D2D_BUNDLED_STORMLIB)
    find_path(STORMLIB_INCLUDE_DIR StormLib.h)
    find_library(STORMLIB_LIBRARY NAMES storm StormLib)
endif()

if(STORMLIB_INCLUDE_DIR AND STORMLIB_LIBRARY)
    message(STATUS "StormLib: ${STORMLIB_LIBRARY}")
    add_library(storm UNKNOWN IMPORTED)
    set_target_properties(storm PROPERTIES
        IMPORTED_LOCATION "${STORMLIB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${STORMLIB_INCLUDE_DIR}")
    # A static libstorm needs its compressors at link time; harmless for
    # a shared one.
    find_package(ZLIB REQUIRED)
    find_package(BZip2)
    target_link_libraries(storm INTERFACE ZLIB::ZLIB $<$<TARGET_EXISTS:BZip2::BZip2>:BZip2::BZip2>)
else()
    message(STATUS "StormLib: building ${STORMLIB_TAG} from source")
    include(FetchContent)
    FetchContent_Declare(
        stormlib
        GIT_REPOSITORY https://github.com/ladislav-zezula/StormLib.git
        GIT_TAG        ${STORMLIB_TAG}
        GIT_SHALLOW    TRUE
    )
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    # System zlib (and bzip2 where found) rather than StormLib's bundled
    # copies: the screenshot component links zlib too, and on Windows the two
    # crc32s collide at link time (LNK2005).
    set(STORM_USE_BUNDLED_LIBRARIES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(stormlib)
    # Third-party: its warnings aren't ours to fix. MSVC: drop the project's
    # /W4 rather than add /w over it (D9025 on every file).
    if(MSVC)
        get_target_property(_storm_opts storm COMPILE_OPTIONS)
        if(_storm_opts)
            list(REMOVE_ITEM _storm_opts /W4)
            set_target_properties(storm PROPERTIES COMPILE_OPTIONS "${_storm_opts}")
        endif()
        target_compile_options(storm PRIVATE /W0)
    else()
        target_compile_options(storm PRIVATE -w)
    endif()
    # Optimised in every build: a Debug d2d spent ~1 s of its start in StormLib
    # at -O0 (listfiles, decompression). (MSVC's Debug /RTC1 refuses /O2.)
    target_compile_options(storm PRIVATE $<$<AND:$<CONFIG:Debug>,$<NOT:$<C_COMPILER_ID:MSVC>>>:-O2>)
    set(STORMLIB_INCLUDE_DIR ${stormlib_SOURCE_DIR}/src)
endif()

add_library(mpq INTERFACE)
target_link_libraries(mpq INTERFACE storm)
target_include_directories(mpq INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${STORMLIB_INCLUDE_DIR})
target_compile_features(mpq INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
