# Header-only game rules (store, prices, grid placement) over excel tables.
add_library(rules INTERFACE)
target_include_directories(rules INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rules INTERFACE d2s)
target_compile_features(rules INTERFACE cxx_std_26)
