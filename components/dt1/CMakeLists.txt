add_library(dt1 INTERFACE)
target_include_directories(dt1 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(dt1 INTERFACE cxx_std_26)
