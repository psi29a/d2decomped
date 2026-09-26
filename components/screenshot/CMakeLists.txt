# PNG framebuffer dump for local dev screenshots. Uses system zlib for the
# IDAT stream — bundled StormLib pulls in the same library, so no extra deps.
find_package(ZLIB REQUIRED)

add_library(screenshot STATIC screenshot.cpp)
target_include_directories(screenshot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(screenshot PUBLIC ZLIB::ZLIB)
target_compile_features(screenshot PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
