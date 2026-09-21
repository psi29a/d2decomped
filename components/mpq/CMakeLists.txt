include(FetchContent)

set(STORMLIB_TAG "v9.30" CACHE STRING "StormLib git tag")

FetchContent_Declare(
    stormlib
    GIT_REPOSITORY https://github.com/ladislav-zezula/StormLib.git
    GIT_TAG        ${STORMLIB_TAG}
    GIT_SHALLOW    TRUE
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(STORM_USE_BUNDLED_LIBRARIES ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(stormlib)

add_library(mpq INTERFACE)
target_link_libraries(mpq INTERFACE storm)
target_include_directories(mpq INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${stormlib_SOURCE_DIR}/src)
target_compile_features(mpq INTERFACE cxx_std_26)
