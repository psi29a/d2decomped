include(FetchContent)

set(STORMLIB_TAG "v9.30" CACHE STRING "StormLib git tag")

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
# Third-party: its warnings aren't ours to fix.
target_compile_options(storm PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)

add_library(mpq INTERFACE)
target_link_libraries(mpq INTERFACE storm)
target_include_directories(mpq INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${stormlib_SOURCE_DIR}/src)
target_compile_features(mpq INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
