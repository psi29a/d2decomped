# Networking with game.exe (docs/design/net-join-plan.md). d2gs is the
# sans-IO codec: bytes in, packets out, no sockets. net adds the join
# session, the TCP connection and the net log.
add_subdirectory(d2gs)

add_library(net STATIC join.cpp tcp.cpp net_log.cpp)
target_include_directories(net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(net PUBLIC d2gs)
target_compile_features(net PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
if (WIN32)
    target_link_libraries(net PRIVATE ws2_32)
endif()
d2d_check_headers(net_headers LINK net)
