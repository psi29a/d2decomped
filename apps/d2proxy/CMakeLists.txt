# SPDX-License-Identifier: GPL-3.0-or-later
# d2proxy: a network debug proxy between a client and a game.exe host.
add_executable(d2proxy main.cpp)
target_link_libraries(d2proxy PRIVATE net log CLI11::CLI11)
