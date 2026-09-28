// The item cursor: a left click on an open panel's grid, a body slot or a
// belt box picks up the item there, or puts the held one down.
#pragma once

#include "protocol.hpp"
#include "store.hpp"
#include "inventory.hpp"

namespace d2d::app {

// Which panels are open, for hit-testing.
struct OpenPanels { bool inv = false, stash = false, cube = false, belt_popup = false, expansion = true; };


// A left click at (mx, my) with the panels in `open`: picks up or puts
// down. Returns whether the click landed on an item spot (so it doesn't
// also walk or toggle the belt).
// A click's item action (the client's side): the command for the World
// (protocol.hpp), and whether the click was the cursor's at all.
struct CursorClick { bool consumed = false; std::optional<Command> cmd; };
CursorClick item_cursor_command(const Scene& s, const std::vector<d2d::d2s::Item>& items, const std::optional<d2d::d2s::Item>& held,
                                int save_cls, const OpenPanels& open, int mx, int my);


// The held item, centred on the cursor (D2 hides the hand while holding).
void draw_held(std::vector<std::uint8_t>& fb, const Scene& s, const d2d::d2s::Item& it, int mx, int my);

}  // namespace d2d::app
