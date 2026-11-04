// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// The mini-panel's rules (apps/d2d/mini_panel.hpp): d2d.cfg's mini_panel
// keeps it shut into the next game as game.exe's "Mini Panel" (FUN_0047f130
// / FUN_004567f0).
#include "mini_panel.hpp"

#include <cassert>

using namespace d2d::client;

int main() {
    assert(mini_panel::opens(""));                                    // no value yet: open
    assert(mini_panel::opens(mini_panel::saved(true)));
    assert(!mini_panel::opens(mini_panel::saved(false)));              // shut as the menu opened: shut next game
    assert(!mini_panel::opens("2"));                                   // any nonzero
    return 0;
}
