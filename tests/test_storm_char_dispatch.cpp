// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Enter with the chat box and the hire list both up (rules::enter_takers,
// FUN_00426f90's dispatch to every WM_CHAR handler) against stock 1.14d's,
// run by tools/emu/storm_char_dispatch.py: who ran, in which order.
#include <hire_list.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    struct Case { int chat_registered, menu_registered, chat_first, chat_ran, menu_ran, first; };
    const Case cases[] = {
#include "storm_char_dispatch_cases.inc"
    };
    for (const auto& row : cases) {
        // A list drawn twice with row 0 selected: it hires whenever its menu is up.
        const auto takers = d2d::rules::enter_takers(row.chat_registered == 1, 2, 0);
        const bool hired = takers.hire_row == 0;
        if ((row.menu_registered == 1 && hired != (row.menu_ran == 1)) || takers.chat != (row.chat_ran == 1))
            std::fprintf(stderr, "FAIL: chat %d menu %d: d2d hire %d chat %d, game.exe menu %d chat %d\n", row.chat_registered, row.menu_registered, hired, takers.chat, row.menu_ran, row.chat_ran);
        assert(takers.chat == (row.chat_ran == 1));
        assert(row.menu_registered == 0 || hired == (row.menu_ran == 1));
    }
    std::printf("OK: %zu registrations, every WM_CHAR handler runs as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_storm_char_dispatch: %s\n", error.what());
    return 1;
}
