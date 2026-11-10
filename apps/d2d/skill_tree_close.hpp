// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace d2d::client {
struct SkillTreeClose {
    int left, bottom;
    [[nodiscard]] bool contains(int pixel_x, int pixel_y) const {
        return pixel_x >= left && pixel_x <= left + 34 && pixel_y >= bottom - 34 && pixel_y <= bottom;
    }
};
// FUN_004ab530 / FUN_004ab5f0; class/tab table at 0x724ce4.
// Oracle: tools/emu/skill_tree_close.py (all 7 classes, 3 tabs, held/released).
[[nodiscard]] inline SkillTreeClose skill_tree_close(int character_class, int tab, int right = 720, int bottom = 540) {
    constexpr std::array<std::array<int, 3>, 7> offsets{{
        {-149, -220, -305}, {-305, -305, -149}, {-305, -149, -305},
        {-305, -220, -305}, {-149, -305, -149}, {-149, -149, -149}, {-220, -149, -305}
    }};
    return {right + offsets[std::size_t(std::clamp(character_class, 0, 6))][std::size_t(std::clamp(tab, 1, 3) - 1)], bottom - 63};
}
} // namespace d2d::client
