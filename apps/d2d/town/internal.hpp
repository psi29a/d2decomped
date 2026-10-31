// SPDX-License-Identifier: GPL-3.0-or-later
// What the town/ files share: no part of the client's API.
#pragma once

#include "town.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace d2d::client {

// Where the automap's files go: beside the save (none with saving off).
std::filesystem::path automap_dir(const World& world);

// Local co-op's Resurrect (docs/design/local-coop.md): a gold aura round a
// body being raised (town/view.cpp).
void draw_raise_aura(std::vector<std::uint8_t>& framebuffer, int centre_x, int centre_y, float raised, std::uint32_t now_ms);

}  // namespace d2d::client
