add_library(cof INTERFACE)
target_include_directories(cof INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cof INTERFACE cxx_std_26)
