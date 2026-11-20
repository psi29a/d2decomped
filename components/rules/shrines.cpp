// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of shrines.hpp's functions; their notes are in the header.
#include "shrines.hpp"

#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace d2d::rules {

int roll_shrine(const std::vector<ShrineRow>& rows, int parm0, int level_id, Rng& rgn) {
    Rng& obj = rgn;
    if (rows.size() < 2) return 0;
    auto low = [&](int id) { return level_id < rows[std::size_t(id)].level_min; };
    int id = 0;
    if (parm0 == 0) {
        for (int tries = 8; tries > 0; --tries) if (id = obj(int(rows.size()) - 1) + 1; !low(id)) break;
    } else {
        int cls = parm0 == 1 ? 2 : parm0 == 2 ? 3 : obj.next() % 10 == 0 ? 1 : 4;
        std::vector<int> list;
        for (std::size_t row = 0; row < rows.size(); ++row) if (rows[row].effectclass == cls) list.push_back(int(row));
        if (list.empty()) return 0;
        for (int tries = 8; tries > 0; --tries) if (id = std::max(list[std::size_t(rgn(int(list.size())))], 1); !low(id)) break;
    }
    return id == 5 ? 3 : id == 4 ? 2 : id == 16 ? 18 : id;
}

int preset_shrine(int id, Rng& unit_seed) {
    static constexpr std::array<std::pair<int, int>, 6> kRanges{ { { 2, 6 }, { 7, 7 }, { 8, 11 }, { 12, 12 }, { 1, 5 }, { 14, 14 } } };
    if (id < 574 || id > 579) return -1;
    const auto [low, high] = kRanges[std::size_t(id - 574)];
    const int kind = low + unit_seed(high - low);
    return kind == 4 || kind == 5 ? 2 : kind;
}

std::vector<std::pair<int, int>> shrine_boost(const ShrineRow& shrine, int attack_rating) {
    switch (shrine.code) {
    case 6:  return { { 171, shrine.arg0 } };                  // skill_armor_percent
    case 7:  return { { 19, attack_rating * shrine.arg0 / 100 }, { 25, shrine.arg1 } };
    case 8:  return { { 39, shrine.arg0 } };                   // fireresist
    case 9:  return { { 43, shrine.arg0 } };                   // coldresist
    case 10: return { { 41, shrine.arg0 } };                   // lightresist
    case 11: return { { 45, shrine.arg0 } };                   // poisonresist
    case 12: return { { 127, 2 } };                            // item_allskills
    case 13: return { { 27, shrine.arg0 } };                   // manarecoverybonus
    case 14: return { { 28, 1000 } };                     // staminarecoverybonus
    case 15: return { { 85, shrine.arg0 } };                   // item_addexperience
    default: return {};
    }
}

void shrine_recharge(const ShrineRow& shrine, std::int64_t& life, std::int64_t max_life, std::int64_t& mana, std::int64_t max_mana) {
    switch (shrine.code) {
    case 1: life = max_life; mana = max_mana; break;
    case 2: life = max_life; break;
    case 3: mana = max_mana; break;
    case 4: { const auto moved = life * shrine.arg0 / 100; life -= moved; mana += moved * shrine.arg1 / 100; break; }
    case 5: { const auto moved = mana * shrine.arg0 / 100; mana -= moved; life += moved * shrine.arg1 / 100; break; }
    default: break;
    }
}

std::string gem_shrine(const Tables& tables, std::vector<d2s::Item>& items, Rng& seed) {
    for (auto& item : items) {
        if (item.location != d2d::d2s::item_location::kStored || item.panel != d2d::d2s::item_panel::kInventory) continue;
        const auto found = tables.item_base.find(item.code);
        if (found == tables.item_base.end() || found->second.better_gem.empty() || found->second.better_gem == "non") continue;
        item.code = found->second.better_gem;
        return {};
    }
    static constexpr const char* kChipped[6] = { "gcw", "gcr", "gcg", "gcb", "gcy", "gcv" };
    return kChipped[seed(6)];
}

std::string chest_tc(int act, int difficulty, int alvl, int lo_alvl, int hi_alvl) {
    const int third = (std::abs(hi_alvl - lo_alvl) + 1) / 3;
    const int cls = alvl < lo_alvl + third ? 0 : alvl < lo_alvl + 2 * third ? 1 : 2;
    static constexpr const char* kDifficultySuffix[3] = { "", " (N)", " (H)" };
    return std::format("Act {}{} Chest {}", std::clamp(act, 0, 4) + 1, kDifficultySuffix[std::clamp(difficulty, 0, 2)], char('A' + cls));
}

ChestInit roll_chest(int mlvl1, bool lockable, Rng& seed) {
    ChestInit chest;
    chest.trap = roll_trap(mlvl1, seed);
    if (lockable && seed(100) < mlvl1 / 2 + 8) chest.locked = true;
    chest.seed = seed.next() % 0xfffe + 1;
    return chest;
}

int door_mode(int mode, bool occupied, bool blocker, bool key) {
    if (mode == 0 || (mode == 6 && key)) return 2;
    if (mode != 2 && mode != 5) return -1;
    return !occupied && !blocker ? 0 : blocker ? 4 : mode == 5 ? -1 : 5;
}

bool in_doorway(int pattern, int unit_x, int unit_y, int door_x, int door_y, int size_x, int size_y) {
    if (pattern <= 0 || pattern > 5) return false;
    const int left = door_x - size_x / 2, top = door_y - size_y / 2;
    const auto inside = [&](int at_x, int at_y) { return at_x >= left && at_y >= top && at_x < left + size_x && at_y < top + size_y; };
    if (pattern == 2 || pattern == 4) {
        for (int row = -1; row <= 1; ++row)
            for (int column = -1; column <= 1; ++column)
                if (inside(unit_x + column, unit_y + row)) return true;
        return false;
    }
    return inside(unit_x, unit_y) || inside(unit_x - 1, unit_y) || inside(unit_x + 1, unit_y) || inside(unit_x, unit_y - 1) || inside(unit_x, unit_y + 1);
}

std::string stand_item(const Tables& tables, bool weapon, int ilvl, Rng& room) {
    ilvl = std::max(ilvl, 1);
    const int act = ilvl < 40 ? 0 : ilvl < 75 ? 1 : ilvl < 103 ? 2 : ilvl < 109 ? 3 : ilvl < 137 ? 4 : 0;
    std::string code;
    for (int tries = 0; tries < (weapon ? 6 : 1); ++tries) {
        std::vector<const std::string*> pool;
        for (const auto& base_code : tables.stand_bases[weapon ? 1 : 0]) {
            const auto& base = tables.item_base.at(base_code);
            if (base.level > ilvl || (base.rarity > act && room(base.rarity - act) != 0)) continue;
            pool.push_back(&base_code);
        }
        if (pool.empty()) return {};
        code = *pool[std::size_t(room(int(pool.size())))];
        if (!weapon || (tables.item_base.at(code).bitfield1 & 2)) break;
    }
    return code;
}

bool well_drink(std::int64_t& life, std::int64_t max_life, std::int64_t& mana, std::int64_t max_mana, std::int64_t& stamina, std::int64_t max_stamina) {
    bool drank = false;
    for (auto [value, max] : { std::pair{ &life, max_life }, { &mana, max_mana }, { &stamina, max_stamina } })
        if (*value < max) { *value = std::min(*value + (128 * max >> 8), max); drank = true; }
    return drank;
}

int trap_on_level(int trap, int level_id) {
    if ((trap == 8 && level_id > 0x4a) || (trap == 3 && level_id < 0x28 && level_id != 0x19) || ((trap == 1 || trap == 4) && level_id < 0x28)) return 2;
    return trap;
}

int object_blast(std::int64_t life, int level, int dexterity, int defense, Rng& seed) {
    const int low = std::max(int(life >> 5), 1), high = std::max(int(life >> 3), low + 1);
    const int roll = std::uint8_t(seed(level >> 2) + level);
    const int chance = std::max((roll - (dexterity >> 1) * 5 - level) * 2 - defense + 125, 65);
    if (int(seed.next() % 100) >= chance) return 0;
    return low + seed(high - low + 256);
}

int trap_undead(const std::vector<int>& region, int act) {
    const int chance = act == 1 ? 96 : 5;
    for (const int id : region) {
        if (id >= chance && id < chance + 5) return chance;
        for (const int first : { 0, 170, 274, 379, 383, 387 }) if (id >= first && id < first + 4) return first;
    }
    return act == 0 ? -1 : 234;
}

}
