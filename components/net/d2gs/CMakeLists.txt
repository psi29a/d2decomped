# The D2GS wire codec, sans-IO (docs/design/net-join-plan.md M1, M2): the
# tables read from the user's game.exe, Huffman, frames, the S->C
# splitter and the join / system packets. Headers are reached as
# <d2gs/...>.
add_library(d2gs STATIC exe_tables.cpp huffman.cpp frame.cpp split.cpp)
target_include_directories(d2gs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(d2gs PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
d2d_check_headers(d2gs_headers LINK d2gs)
