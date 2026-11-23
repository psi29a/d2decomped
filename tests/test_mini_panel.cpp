// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// The mini-panel's rules (apps/d2d/panels/mini_panel.hpp): d2d.cfg's mini_panel
// keeps it shut into the next game as game.exe's "Mini Panel" (FUN_0047f130
// / FUN_004567f0); Messages shuts the belt and toggles the Message Log
// (0x47ed08). The bar's layout in both forms, single player's 7 buttons and
// a multiplayer game's 8 with Party (FUN_0047f0c0), against FUN_0047f710 /
// FUN_0047e8b0 run in game.exe (tools/emu/minipanel_layout.py).
#include "panels/mini_panel.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <iterator>

using namespace d2d::client;

namespace {

struct Layout {
    int width, height;
    bool party, left, right;
    int bar_x, bar_y, count;
    std::array<int, 8> button_xs;
    int button_y;
    std::array<int, 8> actions, frames;
};

constexpr Layout kLayouts[]{
#include "minipanel_layout_cases.inc"
};

// d2d's bar against each game.exe row; the number of rows that differ.
int layout_differences() {
    int differ = 0;
    for (const auto& want : kLayouts) {
        const auto actions = mini_panel::buttons(want.party);
        bool same = int(actions.size()) == want.count
                    && mini_panel::bar_x(want.width, want.left, want.right, want.party) == want.bar_x
                    && mini_panel::bar_y(want.height) == want.bar_y && mini_panel::button_y(want.height) == want.button_y;
        const int first = mini_panel::first_button_x(want.width, want.left, want.right, want.party);
        for (std::size_t i = 0; same && i < actions.size(); ++i)
            same = first + 0x15 * int(i) == want.button_xs[i] && actions[i] == want.actions[i] && 2 * actions[i] == want.frames[i];
        if (!same) {
            std::fprintf(stderr, "layout %dx%d party %d left %d right %d differs\n", want.width, want.height, int(want.party),
                         int(want.left), int(want.right));
            ++differ;
        }
    }
    return differ;
}

}  // namespace

int main() try {
    static_assert(std::size(kLayouts) == 12);
    if (layout_differences() != 0) return 1;
    assert(mini_panel::opens(""));                                    // no value yet: open
    assert(mini_panel::opens(mini_panel::saved(true)));
    assert(!mini_panel::opens(mini_panel::saved(false)));              // shut as the menu opened: shut next game
    assert(!mini_panel::opens("2"));                                   // any nonzero
    bool belt_open = true, message_log = false;
    mini_panel::messages(belt_open, message_log);
    assert(!belt_open && message_log);
    mini_panel::messages(belt_open, message_log);
    assert(!belt_open && !message_log);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_mini_panel: %s\n", error.what());
    return 1;
}
