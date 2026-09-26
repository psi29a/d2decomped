add_library(palette INTERFACE)
target_include_directories(palette INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(palette INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
