// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "input/touch/input.hpp"

namespace d2d::client::touch {

enum class BagDrop { None, Inventory, Sell };
// A shop's bag drag may place in inventory or sell; an outside release never drops on the ground.
[[nodiscard]] BagDrop bag_drop(Box inventory, Box vendor, float x, float y);

} // namespace d2d::client::touch
