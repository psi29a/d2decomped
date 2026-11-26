// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "input/loot_radial.hpp"
#include "input/touch/input.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2d::client {

struct LootChoice { int id = -1; std::string name; std::array<std::uint8_t, 3> rgb{}; };

struct LootChooser {
    bool open = false, touch = false, ready = false;
    std::vector<LootChoice> choices;
    loot_radial::Model radial{ std::span<const int>{} };
    std::size_t scroll = 0;
    std::int64_t finger = -1;
    float start_x = 0, start_y = 0;
    int row = -1;
    bool moved = false;

    void clear() { *this = {}; }
    void start(std::vector<LootChoice> items, bool use_touch);
    [[nodiscard]] static touch::Box list_box();
    [[nodiscard]] int touch_event(std::int64_t id, touch::Phase phase, float x, float y);
};

} // namespace d2d::client
