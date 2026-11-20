// SPDX-License-Identifier: GPL-3.0-or-later
// A monster's own gear, MonEquip.txt (FUN_005d6b60): which rows it wears
// from, which of a row's items, and what the gear does to its stats.
// docs/research/re/skills.md "Summons", tools/emu/mon_equip.py.
#pragma once

#include "combat.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

// FUN_005d6b60(owner, unit, skill, level, ilvl, oninit): from the
// monster's first row (MonStats +0x2a, `first`; none when negative), which
// must be oninit when `oninit` is set (a monster as it's made,
// FUN_00573cb0), rows whose level is above `level` skipped, then each
// following row of that monster: its items up to the first slot outside
// 1..10 counted (3 at most), one picked on the unit seed (FUN_00573b20 is
// called only when its body slot is empty). Quality outside 0..7 is 0.
// `filled` / `owner`: body slots already worn (bit n for slot n). Every
// item the caller makes fills its slot; one that names an item does here.
std::vector<MonGear> monster_gear(std::span<const MonEquipRow> rows, int first, int monster, int level, bool oninit, std::uint32_t filled,
                                  std::optional<std::uint32_t> owner, Rng& own);

// The item level a pet's gear is made at (FUN_005c4470, 0x5c497c): the
// summoning skill's calc2 when it isn't 0 (the Valkyrie's), else
// min(max(3 x its level, 1), the owner's level).
int summon_gear_level(int calc2, int skill_level, int owner_level);

// What worn gear does to a monster: its items' stat lists join the unit's,
// as a hireling's do (merc-ai.md "Its gear"). `sum`: the items' props but
// their own enhanced defence and damage (16, 17, 18: op 13, the item's own);
// `defense`: their own defence after their enhanced defence (stat 31);
// the weapon in a hand (FUN_00535bc0 finds a monster's in its inventory)
// and its own damage after its enhanced damage (op 13 stats 17 / 18 only
// touch the item they're on), two-handed (Items +0x11c, FUN_006289c0).
struct GearSum {
    StatSum sum{};
    std::int64_t defense = 0, min = 0, max = 0;
    bool weapon = false, two_handed = false;
};
GearSum gear_sum(const Tables& tables, std::span<const d2d::d2s::Item> items);

// A blow's physical range with that gear (FUN_0057b420): with a two-handed
// weapon stats 23 / 24, which only the weapon fills (the mode's damage,
// FUN_005a4f50, is in 21 / 22 and goes unread); else 21 / 22, the mode's
// damage plus a one-handed weapon's and the items' flat damage.
std::pair<int, int> gear_damage(const GearSum& gear, int mode_min, int mode_max);

}   // namespace d2d::rules
