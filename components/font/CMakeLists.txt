add_library(font INTERFACE)
target_include_directories(font INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(font INTERFACE dc6 palette)
target_compile_features(font INTERFACE cxx_std_26)
