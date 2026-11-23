// SPDX-License-Identifier: GPL-3.0-or-later
// Drinking potions: from the belt or the bag, what each gives and its rate.
// The tables these read are rules.hpp's.
#pragma once

#include "rules.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::rules {

// A healing / mana potion's amount in 256ths (FUN_005be3f0): calc1 << 8,
// the class's bonus (life FUN_0062a5d0: Amazon, Paladin, Assassin x1.5,
// Barbarian x2; mana FUN_0062a620: Amazon, Paladin, Assassin x1.5,
// Sorceress, Necromancer, Druid x2; `cls` -1, a non-player such as the
// hireling: life x2, mana x1), then doubled when rand(vitality / energy) /
// 2 beats rand(100), the drinker's seed (unit +0x20).
template <class T> T potion_bonus(T amount, int cls, bool life) {
    if (cls < 0) return life ? amount * 2 : amount;
    if (cls == d2d::d2s::kAmazon || cls == d2d::d2s::kPaladin || cls == d2d::d2s::kAssassin) return amount + (amount >> 1);
    return (life ? cls == d2d::d2s::kBarbarian : cls == d2d::d2s::kSorceress || cls == d2d::d2s::kNecromancer || cls == d2d::d2s::kDruid) ? amount * 2 : amount;
}

// Drinks the potion at the bottom of belt column `col` (box col, 0..3): the item goes, those stacked above it
// (boxes col + 4, + 8, + 12) drop a row.
std::string drink_at(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::vector<d2d::d2s::Item>::iterator potion);

std::string drink_item(const Tables& tables, std::vector<d2d::d2s::Item>& items, int id);

std::string drink_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, int col);

// Find Potion's potion.
std::string find_potion(int level_id, int difficulty, int roll, int mana_chance, int rejuv_chance);

int potion_amount(int calc, int cls, bool life, int stat, Rng& rng);

// The potion's state (healthpot / manapot) gives it over `len` frames on top of what's left of the last one: per
// frame (old x left + amount) / (left + len), for left + len frames.
int potion_rate(int old_rate, int left, int amount, int len);

}  // namespace d2d::rules
