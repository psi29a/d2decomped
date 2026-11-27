// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "input/touch/input.hpp"

namespace d2d::client::touch {

enum class BagDrop { None, Inventory, Sell };
// A shop's bag drag may place in inventory or sell; an outside release never drops on the ground.
[[nodiscard]] BagDrop bag_drop(Box inventory, Box vendor, float x, float y);

struct TradeDropPreview { Box box; bool dropping = false, inside = false; };
// Hit-test the legacy pointer before translating the highlighted box for drawing.
[[nodiscard]] TradeDropPreview trade_drop_preview(Box legacy_box, float pointer_x, float pointer_y);

} // namespace d2d::client::touch
