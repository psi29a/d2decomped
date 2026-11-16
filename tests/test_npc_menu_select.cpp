// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: dcc font game
// test-includes: apps/d2d
// The NPC window's selected line (+0x44): -1 at creation (FUN_004b7eb0), the
// first choosable line once the lines are in (FUN_004b85f0 -> FUN_004b82f0), a
// mouse move sets the line under it or -1 (FUN_004b7200), FUN_004b82f0 refuses a
// header or an index past the lines. The cases of FUN_004b82f0 itself are in
// test_shop_action.cpp.
#include "ui.hpp"

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    d2d::client::NpcMenuState menu;
    menu.npc = 0;
    menu.x = 75;
    menu.y = 105;
    menu.box_width = 100;
    menu.lines.push_back({ "head", 0x15, 0, 0, true });
    menu.lines.push_back({ "talk", 15 });
    menu.lines.push_back({ "cancel", 15 });
    assert(menu.selected == -1);
    menu.select_first();
    assert(menu.selected == 1);                              // the header is not choosable: the first line after it
    const int mid_x = menu.x + menu.box_width / 2;
    const int cancel_base = menu.y + 0x15 + 15 + 15;
    menu.track_mouse(5, 5);                                  // the first sight only records the mouse
    assert(menu.selected == 1);
    menu.track_mouse(5, 5);
    assert(menu.selected == 1);                              // no move, no change
    menu.track_mouse(mid_x, cancel_base - 2);
    assert(menu.selected == 2);
    menu.track_mouse(5, 5);                                  // off every line
    assert(menu.selected == -1);
    int before = menu.selected;
    assert(!menu.select_line(0) && menu.selected == before); // a header halts game.exe (0x50f)
    assert(!menu.select_line(3) && menu.selected == before); // past the lines (0x4ed)
    assert(menu.select_line(2) && menu.selected == 2);
    assert(menu.select_line(-1) && menu.selected == -1);
    d2d::client::NpcMenuState empty;
    empty.select_first();
    assert(empty.selected == -1);
    std::puts("ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_npc_menu_select: %s\n", error.what());
    return 1;
}
