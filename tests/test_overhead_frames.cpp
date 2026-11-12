// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// FUN_00661110 (Chat/Chat.cpp): an overhead stays min(length, 254) * 8 + 125
// frames. 254 and above are 2157. A shrine's 4-digit id is 157.
#include "overhead.hpp"

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    assert(d2d::client::overhead_frames(1) == 133);
    assert(d2d::client::overhead_frames(4) == 157);
    assert(d2d::client::overhead_frames(254) == 2157);
    assert(d2d::client::overhead_frames(255) == 2157);
    assert(d2d::client::overhead_frames(300) == 2157);
    std::puts("overhead stay checks passed");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
