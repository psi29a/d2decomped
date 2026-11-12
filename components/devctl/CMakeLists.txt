# SPDX-License-Identifier: GPL-3.0-or-later
# Local-dev-only Unix-domain-socket control channel (AF_UNIX: POSIX, and
# Winsock on Windows 10 1803+). Zero cost when unused (nothing binds, no
# threads spawned). See docs/control_channel.md.
add_library(devctl STATIC devctl.cpp)
target_include_directories(devctl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(devctl PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
if(WIN32)
    target_link_libraries(devctl PRIVATE ws2_32)
endif()
