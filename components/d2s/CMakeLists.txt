# Header-only D2 character-save (.d2s) header parser.
add_library(d2s INTERFACE)
target_include_directories(d2s INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(d2s INTERFACE cxx_std_26)
