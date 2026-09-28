// The item cursor: a left click on an open panel's grid, a body slot or a
// belt box picks up the item there, or puts the held one down.
#pragma once

#include "scene.hpp"

namespace d2d::client {

// Which panels are open, for hit-testing.
struct OpenPanels { bool inv = false, stash = false, cube = false, belt_popup = false, expansion = true; };

// A left click at (mx, my) with the panels in `open`: picks up or puts
// down. Returns whether the click landed on an item spot (so it doesn't
// also walk or toggle the belt).
// A click's item action (the client's side): the command for the World
// (protocol.hpp), and whether the click was the cursor's at all.
struct CursorClick { bool consumed = false; std::optional<Command> cmd; };
CursorClick item_cursor_command(const Scene& scene, const std::vector<d2d::d2s::Item>& items, const std::optional<d2d::d2s::Item>& held,
                                int save_cls, const OpenPanels& open, int mouse_x, int mouse_y);

// The held item, centred on the cursor (D2 hides the hand while holding).
void draw_held(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Item& item, int mouse_x, int mouse_y);

}  // namespace d2d::client
