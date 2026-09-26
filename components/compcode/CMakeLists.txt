# Header-only rebuild of D2's composite component table (appearance byte -> DCC code).
add_library(compcode INTERFACE)
target_include_directories(compcode INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(compcode INTERFACE txt)
target_compile_features(compcode INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
