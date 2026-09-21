# Header-only D2 string-table (.tbl) parser.
add_library(tbl INTERFACE)
target_include_directories(tbl INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tbl INTERFACE cxx_std_26)
