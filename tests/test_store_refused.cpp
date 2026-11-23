// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A refused buy, sale or repair (FUN_004b53f0): the store panel goes to mode
// 7 under the box (FUN_00487d30) and Continue puts it back to the plain
// store, mode 1 (LAB_004b4cf0 -> FUN_00487d40), so a button toggled on is
// off again: rules::Store::refused.
#include <vendor.hpp>

#include <cassert>
#include <exception>
#include <iostream>

int main() try {
    for (const int mode : { 0, 1, 2, 3 }) {
        d2d::rules::Store store;
        store.npc = 4;
        store.tab = 2;
        store.mode = mode;
        store.pressed[1] = true;
        store.refused();
        assert(store.mode == 0);
        assert(!store.pressed[0] && !store.pressed[1] && !store.pressed[2] && !store.pressed[3]);
        assert(store.npc == 4 && store.tab == 2);   // still open, on its tab (0x7c0c6b = 3)
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
