// SPDX-License-Identifier: GPL-3.0-or-later
// A monster's own gear, MonEquip.txt (FUN_005d6b60): which rows it wears
// from and which of a row's items.
// docs/research/re/skills.md "Summons", tools/emu/mon_equip.py.
#pragma once

#include "rules.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::rules {

// One MonEquip.txt row as game.exe compiles it (0x1c bytes, FUN_00659e60):
// +0 the MonStats row, +2 level, +4 oninit, +8 three item codes ("    " is
// none, or the owner's item in that slot), +0x14 three BodyLocs slots,
// +0x17 three qualities.
struct MonEquipRow {
    int monster = -1, level = 0;
    bool oninit = false;
    std::array<std::string, 3> item{ "    ", "    ", "    " };
    std::array<int, 3> loc{}, quality{};
};
inline constexpr std::string_view kOwnersItem = "    ";

// One item FUN_005d6b60 makes: its row and slot, the body slot, its quality
// (0..7). `owners`: the slot's code is "    " and the owner has an item
// there, which is made instead.
struct MonGear {
    std::size_t row = 0, slot = 0;
    int loc = 0, quality = 0;
    bool owners = false;
};

// FUN_005d6b60(owner, unit, skill, level, ilvl, oninit): from the monster's
// first row (MonStats +0x2a, `first`; none when negative), which must be
// oninit when `oninit` is set (a monster as it's made, FUN_00573cb0), rows
// whose level is above `level` skipped, then each following row of that
// monster: its items up to the first slot outside 1..10 counted (3 at
// most), one picked on the unit seed (FUN_00573b20 is called only when its
// body slot is empty). Quality outside 0..7 is 0. A "    " code with an
// owner takes the owner's item in that slot (skipped when it has none);
// without one it goes to FUN_00573b20 as "    ", which makes nothing.
// `filled` / `owner`: body slots already worn (bit n for slot n). Every
// item the caller makes fills its slot; one that names an item does here.
// A unit without an inventory (+0x60) gets nothing: the caller's check.
inline std::vector<MonGear> monster_gear(std::span<const MonEquipRow> rows, int first, int monster, int level, bool oninit, std::uint32_t filled,
                                         std::optional<std::uint32_t> owner, Rng& own) {
    std::vector<MonGear> out;
    if (first < 0 || std::size_t(first) >= rows.size()) return out;
    std::size_t row = std::size_t(first);
    if (oninit && !rows[row].oninit) return out;
    while (row < rows.size() && rows[row].monster == monster && rows[row].level > level) ++row;
    for (; row < rows.size() && rows[row].monster == monster; ++row) {
        const auto& equip = rows[row];
        int count = 0;
        while (count < 3 && equip.loc[std::size_t(count)] > 0 && equip.loc[std::size_t(count)] < 11) ++count;
        if (count == 0) continue;
        const auto slot = std::size_t(own(count));
        const int loc = equip.loc[slot];
        if (filled & (1u << loc)) continue;
        const bool owners = equip.item[slot] == kOwnersItem && owner;
        if (owners && !(*owner & (1u << loc))) continue;
        const int quality = equip.quality[slot] < 0 || equip.quality[slot] > 7 ? 0 : equip.quality[slot];
        out.push_back({ row, slot, loc, quality, owners });
        if (owners || equip.item[slot] != kOwnersItem) filled |= 1u << loc;
    }
    return out;
}

}   // namespace d2d::rules
