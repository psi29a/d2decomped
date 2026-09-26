add_library(ds1 INTERFACE)
target_include_directories(ds1 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(ds1 INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
