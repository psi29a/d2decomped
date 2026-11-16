// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// FUN_00661110 (Chat/Chat.cpp): an overhead stays min(length, 254) * 8 + 125
// frames. 254 and above are 2157. A shrine's 4-digit id is 157.
// overhead_string: a monster's or object's text is atol'd, 1..0xfffe
// (FUN_004a0d20); a player's needs 0xff 0xff and one of FUN_004a0b30's
// ranges.
#include "overhead.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <optional>

int main() try {
    assert(d2d::client::overhead_frames(1) == 133);
    assert(d2d::client::overhead_frames(4) == 157);
    assert(d2d::client::overhead_frames(254) == 2157);
    assert(d2d::client::overhead_frames(255) == 2157);
    assert(d2d::client::overhead_frames(300) == 2157);

    using d2d::client::overhead_string;
    assert(overhead_string(2, "3684") == 3684);         // a shrine's ShrMsg1
    assert(overhead_string(1, " 12abc") == 12);         // atol: spaces, then up to the first non-digit
    assert(overhead_string(1, "0065534") == 65534);
    assert(!overhead_string(1, "65535"));
    assert(!overhead_string(1, "0"));
    assert(!overhead_string(2, "-5"));
    assert(!overhead_string(2, "hello"));
    assert(!overhead_string(0, "3684"));                // a player's number is drawn as text
    assert(overhead_string(0, "\xff\xff" "1069") == 1069);  // 0x42d
    assert(!overhead_string(0, "\xff\xff" "1068"));
    assert(overhead_string(0, "\xff\xff" "1295") == 1295);  // 0x50f
    assert(!overhead_string(0, "\xff\xff" "1296"));
    assert(overhead_string(0, "\xff\xff" "10916") == 10916);  // 0x2aa4
    assert(overhead_string(0, "\xff\xff" "21894") == 21894);  // 0x5586
    assert(overhead_string(0, "\xff\xff" "22037") == 22037);  // 0x5615
    assert(!overhead_string(0, "\xff\xff" "22038"));
    std::puts("overhead stay checks passed");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
