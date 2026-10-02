# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(drlg-dump drlg-dump.cpp)
target_link_libraries(drlg-dump PRIVATE drlg mpq rules game)
