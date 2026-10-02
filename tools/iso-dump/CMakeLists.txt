# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(iso-dump iso-dump.cpp)
target_link_libraries(iso-dump PRIVATE iso9660)
