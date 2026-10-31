// SPDX-License-Identifier: GPL-3.0-or-later
// Monsters: MonStats / MonStats2 / MonLvl rows, which monsters a level
// spawns and where (game.exe's monster region and room population), and a
// spawned monster's stats. docs/research/re/monsters.md. Fighting is in
// combat.hpp, what they drop in drops.hpp.
#pragma once

#include "level_ids.hpp"
#include "montypes.hpp"
#include "rules.hpp"
#include "town_npcs.hpp"
#include "uniques.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::rules {

// A type's component sets (FUN_005bdb20): each layer's pick among
// MonStats2's HDv..S8v. The first rolls every layer with a choice; the
// others are the first with one or two layers rerolled, up to 3 tries not
// to repeat a set. 1 set when no layer has a choice, 2 when the only one
// has 2, else 3 (MonStats2 +0x25 as 1 << bits; checked on all 609 rows).
using Components = std::array<std::uint8_t, 16>;
inline std::vector<Components> roll_components(const Components& choices, Rng& rng) {
    std::vector<int> varied;
    for (int layer = 0; layer < 16; ++layer) if (choices[std::size_t(layer)] > 1) varied.push_back(layer);
    const std::size_t sets_max = varied.empty() ? 1 : varied.size() == 1 && choices[std::size_t(varied[0])] == 2 ? 2 : 3;
    Components first{};
    for (std::size_t layer = 0; layer < 16; ++layer) first[layer] = std::uint8_t(choices[layer] < 2 ? 0 : rng(choices[layer]));
    std::vector<Components> sets{ first };
    if (sets_max == 1) return sets;
    int layer_a = varied[0], layer_b = varied[0];
    if (varied.size() > 1) {
        const int count = int(varied.size());
        const int pick = rng(count);
        layer_a = varied[std::size_t(pick)];
        varied[std::size_t(pick)] = varied.back();
        layer_b = varied[std::size_t(rng(count - 1))];
    }
    while (sets.size() < sets_max) {
        Components next = first;
        for (int tries = 3;;) {
            next[std::size_t(layer_a)] = std::uint8_t(rng(choices[std::size_t(layer_a)]));
            if (layer_b != layer_a) next[std::size_t(layer_b)] = std::uint8_t(rng(choices[std::size_t(layer_b)]));
            bool repeat = false;
            for (const auto& set : sets) if (set == next) { --tries; repeat = true; }
            if (!repeat || tries == 0) break;
        }
        sets.push_back(next);
    }
    return sets;
}

// A monster's look as it's made (FUN_00573cb0 → FUN_005739d0), the first
// roll of its unit seed: one of its type's region sets (rand(count)); a
// type the level's region lacks rolls every layer with a choice instead
// (a layer of one choice still takes a roll).
inline Components monster_look(const std::vector<Components>* sets, const Components& choices, Rng& unit_seed) {
    if (sets && !sets->empty()) return (*sets)[std::size_t(unit_seed(int(sets->size())))];
    Components look{};
    for (std::size_t layer = 0; layer < 16; ++layer) look[layer] = std::uint8_t(choices[layer] < 1 ? 0 : unit_seed(choices[layer]));
    return look;
}

// FUN_00573cb0, after the look: the Abyss and Oblivion Knights (MonStats
// rows 0x137 / 0x138) roll rand(4) on the unit seed, their variant
// (monster data +0xe, the Oblivion Knight's +0xf too), the row
// DoomKnightMissile adds to its missile (FUN_005cdfb0). -1: none rolled.
inline int monster_variant(int row, Rng& unit_seed) { return row == 0x137 || row == 0x138 ? unit_seed(4) : -1; }

// The level's monster region (FUN_005475e0): up to NumMon (at most 13)
// types drawn without replacement from the difficulty's list, each kept
// with its rarity and component sets when MonStats isSpawn. With
// rangedspawn the first draw retries up to 20 times for a rangedtype.
// game.exe makes every level's region at game start on one seed
// (FUN_005479c0, levels 1 up): pass the same `seed` level after level.
struct Region {
    std::vector<std::pair<int, int>> types;     // (MonStats row, rarity)
    std::vector<std::vector<Components>> components;   // per type
    int total = 0;                              // rarity sum
};
inline Region monster_region(const Monsters& monsters, const LevelMon& level_mon, int difficulty, Rng& seed) {
    Region region;
    auto list = difficulty == 0 ? level_mon.mon : level_mon.nmon;
    auto type_at = [&](int row) { return row >= 0 && std::size_t(row) < monsters.types.size() ? &monsters.types[std::size_t(row)] : nullptr; };
    const int picks = std::min<int>(std::min(level_mon.num_mon, 13), int(list.size()));
    for (int i = 0; i < picks && !list.empty(); ++i) {
        int pick = seed(int(list.size()));
        for (int retry = 0; i == 0 && level_mon.ranged_first && retry < 20; ++retry) {
            if (const auto* type = type_at(list[std::size_t(pick)]); type && type->ranged) break;
            pick = seed(int(list.size()));
        }
        const int row = list[std::size_t(pick)];
        list.erase(list.begin() + pick);
        if (const auto* type = type_at(row); type && type->spawnable) {
            region.types.emplace_back(row, type->rarity);
            region.total += type->rarity;
        }
    }
    for (const auto& [row, rarity] : region.types) region.components.push_back(roll_components(monsters.types[std::size_t(row)].choices, seed));
    return region;
}

// A spawned monster, in level-relative subtiles; `leader` is the index
// of its group's first monster (itself for a leader).
struct Spawn {
    int type = -1, x = 0, y = 0, leader = -1, super = -1;     // super: SuperUniques row
    Boss boss = Boss::none;                                   // champion / unique / its minion
    std::vector<int> mods;                                    // MonUMod ids
    int name_seed = 0;                                        // a unique's (rndname)
    // Its unit seed (unit +0x20, {seed, 666}): a step of the game seed as
    // the unit is made (FUN_00555230 → FUN_00552df0), what its look rolls.
    std::uint32_t seed = 0;
    std::vector<std::pair<int, int>> path;                    // its preset's map AI points (FUN_00555910), subtiles
    bool dead = false;                                        // made in mode 12 (FUN_0054e600's dead MonPlace codes)
    int cage = -1;                                            // a5q2's prisoner: its cage's slot (FUN_00588600)
};

// A level's population so far (monster region +4 rooms done, +0xc rooms
// in the level, +0x2c8 uniques made) and what its uniques roll with.
struct Population {
    int rooms_done = 0, rooms_total = 0, uniques = 0;
    int umin = 0, umax = 0;                                   // Levels MonUMin / MonUMax for the difficulty
    int difficulty = 0;
    const UMods* umods = nullptr;
};

// A room to populate: its rect in subtiles, its seed, its areas
// (FUN_0061ad50: x, y, width, height subtiles) and the one room_spot draws in (-1: the room).
struct SpawnRoom { int x = 0, y = 0, width = 0, height = 0; Rng seed; std::vector<std::array<int, 4>> areas; int area = -1; };

// Random object groups per room (FUN_00552610, objects.md "Random object
// groups per room"). Runs before the room's monsters (FUN_0054ec90), on
// the room1 seed. Per slot i = 0..7: one seed step for the roll, and
// (when ObjGrp[i] != 0 and roll <= ObjPrb[i]) a second step to pick the
// objgroup entry. The picked object's placement (the table at 0x731d00's
// PopulateFns) draws the object seed instead, not the room seed — so
// stepping the room seed here is what monster population needs.
// `rooms_populated_before` and `rooms_total` are the objrgn per-level
// counter and target (FUN_00552560 / FUN_00552400): the density throttle
// forces the roll to 100 (a guaranteed miss) once more than 75 % of the
// level has been populated. Approximates game.exe's model of the target
// (level room count via FUN_00642be0) and skips the objgroup +0x167 gate
// (it reads objgroup ids as if they were objects.txt rows — a likely
// game.exe bug). The seed still steps once per slot regardless.
// Not built here: calling the PopulateFn to actually place the object;
// that needs the object seed and unit maker.
struct ObjectGroupPick {
    int object_id = 0;
    std::uint8_t density = 0;
};
inline std::vector<ObjectGroupPick> place_object_groups(const LevelMon& level_mon,
                                                        const std::vector<ObjGroup>& obj_groups,
                                                        Rng& room_seed,
                                                        int rooms_populated_before = 0,
                                                        int rooms_total = 0,
                                                        const std::vector<std::uint8_t>* subclass = nullptr) {   // objects.txt SubClass by Id: only those throttle
    std::vector<ObjectGroupPick> picks;
    const bool throttle = rooms_total > 0 && rooms_populated_before * 128 / rooms_total > 96;
    for (int i = 0; i < 8; ++i) {
        int roll = int(room_seed.next() % 100);
        const std::uint8_t group_id = level_mon.obj_group[std::size_t(i)];
        if (throttle && (!subclass || (group_id < subclass->size() && (*subclass)[group_id]))) roll = 100;
        const std::uint8_t prob     = level_mon.obj_prob[std::size_t(i)];
        if (group_id == 0 || roll > prob) continue;
        if (std::size_t(group_id) >= obj_groups.size()) continue;
        const int weight_roll = int(room_seed.next() % 100);
        const auto& group = obj_groups[std::size_t(group_id)];
        int accumulated = 0;
        for (int j = 0; j < 8 && group.id[std::size_t(j)] != 0; ++j) {
            accumulated += group.weight[std::size_t(j)];
            if (weight_roll < accumulated) {
                picks.push_back({ group.id[std::size_t(j)], group.density[std::size_t(j)] });
                break;
            }
        }
    }
    return picks;
}

namespace monster_detail {

// FUN_005b2a00's placement: rings of 3 subtiles around (x, y) out to
// 3 * radius (radius < 0: the point itself), each walked from a random
// point on it round the square; the first spot in the room that the
// monster fits wins. `fits(x, y)`: the collision test (FUN_0064d9b0).
template <class Fits>
bool place(SpawnRoom& room, int x, int y, int radius, Fits&& fits, int& out_x, int& out_y) {
    const int last = radius < 0 ? 0 : radius * 3;
    for (int ring = radius < 0 ? 0 : 3; ring <= last; ring += 3) {
        const bool even = (room.seed.next() & 1) == 0;
        int dx, dy;
        if (even) { dx = room.seed(ring); dy = ring; } else { dx = ring; dy = room.seed(ring); }
        int step_x = even ? 1 : 0, step_y = even ? 0 : 1;
        if (room.seed.next() & 1) dx = -dx;
        if (room.seed.next() & 1) dy = -dy;
        int spot_x = x + dx, spot_y = y + dy;
        const int left = x - ring, right = x + ring, top = y - ring, bottom = y + ring;
        for (int steps = ring ? ring * 8 : 1; steps > 0; --steps) {
            if (spot_x == left && spot_y == top) { step_x = 1; step_y = 0; }
            if (spot_x == right) {
                if (spot_y == top) { step_x = 0; step_y = 1; }
                if (spot_y == bottom) { step_x = -1; step_y = 0; }
            }
            if (spot_x == left) {
                if (spot_y == bottom) { step_x = 0; step_y = -1; }
                if (spot_y == top && spot_x == right && spot_y == bottom) { step_x = 0; step_y = 0; }
            }
            spot_x += step_x; spot_y += step_y;
            if (spot_x >= room.x && spot_y >= room.y && spot_x < room.x + room.width && spot_y < room.y + room.height && fits(spot_x, spot_y)) {
                out_x = spot_x; out_y = spot_y;
                return true;
            }
        }
    }
    return false;
}

}  // namespace monster_detail

// A type by rarity from the region (FUN_005bde80) on `seed`.
inline int pick_type(const Region& reg, Rng& seed) {
    int pick = seed(reg.total) + 1;
    std::size_t index = 0;
    for (; index < reg.types.size(); ++index) { pick -= reg.types[index].second; if (pick < 1) break; }
    return reg.types[std::min(index, reg.types.size() - 1)].first;
}

// FUN_0054dc40: a spot in the room (the rect shrunk by one subtile at the
// top left), 20 tries, not by an entrance, where a monster fits.
template <class Fits, class Near>
bool room_spot(SpawnRoom& room, Fits&& fits, Near&& near_entrance, int& spot_x, int& spot_y) {
    const auto rect = room.area >= 0 ? room.areas[std::size_t(room.area)] : std::array<int, 4>{ room.x, room.y, room.width, room.height };   // FUN_0054dac0
    const int room_x = rect[0] + 1, room_y = rect[1] + 1, room_width = rect[0] + rect[2] - room_x, room_height = rect[1] + rect[3] - room_y;
    for (int tries = 0; tries < 20; ++tries) {
        const int x = room.seed(room_width) + room_x, y = room.seed(room_height) + room_y;
        if (near_entrance(x, y)) continue;
        int found_x, found_y;
        if (monster_detail::place(room, x, y, -1, fits, found_x, found_y)) { spot_x = x; spot_y = y; return true; }
    }
    return false;
}

// A monster's stats at `level` (FUN_00573cb0 with FUN_006538a0): MonLvl's
// row for the level (at most its last; the L- columns in an expansion
// game) x MonStats' percentage / 100, or the MonStats value itself for a
// noRatio row; life min + rand(max - min + 1) on the unit's seed.
// Elemental attacks: MonLvl damage x El1..3 MinD / MaxD % the same way
// (FUN_005a4f50), the length as written. tools/emu/monstats.py checks
// every row and difficulty against game.exe.
// With `players` in the game (FUN_00535790's count, FUN_00573930) life
// and experience grow by player_life_pct / player_exp_pct of the roll
// (FUN_00483360: truncated), life capped at 0x7fffff (FUN_00573cb0).
// ponytail: monster data +0x4c (unidentified) skips the bonus in game.exe;
// d2d's pets never take it; Nightmare / Hell take the area's level (FUN_0061dca0) for a ratio row,
// not Level(N) / Level(H), which waits for those difficulties.
struct MonStats {
    int level = 1, hit_points = 1, armor_class = 0, to_hit = 0, a1_min = 0, a1_max = 0, a2_min = 0, a2_max = 0, exp = 0;
    int s1_min = 0, s1_max = 0;
    int to_hit2 = 0, to_hit_s1 = 0;            // A2TH's, S1TH's: the hit's mode's (FUN_005a4f50 flags 0x10 / 0x20)
    struct El { int type = -1, pct = 0, min = 0, max = 0, dur = 0; std::string_view mode; };
    std::array<El, 3> elements{};
    // Its own poison stats 57 / 58 / 59 (MonUMod poison hit, FUN_005a1e00),
    // on every hit it lands (FUN_0057b7d0): per tick in 256ths, ticks.
    int poison_min = 0, poison_max = 0, poison_len = 0;
};
// The max damage a monster's last swing left in its stats (FUN_005a4f50,
// at each attack mode: A2's for A2, S1's for SC / S1, A1's otherwise; each
// element of that mode as its own stat, poison x 10), as Diablo's score
// sums it (FUN_005e8530: maxdamage, fire / lightning / magic / cold max,
// poison max >> 8). 0 before any swing.
// ponytail: an element counts whatever its chance (FUN_005a4f50 rolls one
// under 100 %); the hireling / damage-% scaling (+0x70, stat 100) is left out.
inline int swing_max_damage(const MonStats& stats, std::string_view swing_mode) {
    if (swing_mode.empty()) return 0;
    int total = swing_mode == "A2" ? stats.a2_max : swing_mode == "SC" || swing_mode == "S1" ? stats.s1_max : stats.a1_max;
    for (const auto& element : stats.elements)
        if (element.type >= 0 && element.mode == swing_mode) total += element.type == 3 ? element.max * 10 >> 8 : element.max;
    return total;
}

// The player-count bonus in % (FUN_005738f0 life, table 0x6e1590;
// FUN_00573910 experience, 0x6e15b4): 50 a player past the first up to 8,
// then (n - 2) x 50 and (5n + 130) x 2.
inline int player_life_pct(int players) {
    static constexpr std::array<int, 9> kPct{ 0, 0, 50, 100, 150, 200, 250, 300, 350 };
    return players < 9 ? kPct[std::size_t(std::max(players, 0))] : (players - 2) * 50;
}
inline int player_exp_pct(int players) {
    static constexpr std::array<int, 9> kPct{ 0, 0, 50, 100, 150, 200, 250, 300, 350 };
    return players < 9 ? kPct[std::size_t(std::max(players, 0))] : (players * 5 + 130) * 2;
}

// What a monster's MonProp row gave it (the end of FUN_00573cb0): each
// slot of its difficulty in order, one with a chance first stepping the
// unit seed and applying when its low % 100 is below it; applying is the
// item property path (FUN_0065fd70 -> apply_mod), whose min..max draw is
// on the unit seed too (FUN_0065e9e0, a monster's +0x20). The stats as
// they act on a monster:
// - ias / fcr: its attack (A1, A2) and cast (SC) rates (FUN_00623f50 ->
//   FUN_00621740: 120 x v / (120 + v), anim_rate_pct);
// - crush / knock (domeleedamage / domissiledamage events 16 / 7) and
//   thorns (damagedinmelee 6): the item events FUN_0055b800 hooks up, as
//   FUN_00573cb0 sets it as the monster's stat callback before the row;
// - stupidity (event 9, FUN_005bfaa0) blinds only a monster it hits, and
//   the one row with it, druidhawk, is the Druid's Raven; fade (stat 181)
//   is how the client draws it; fire mastery reaches only fire missiles
//   the monster owns itself (FUN_0064b860 -> FUN_00644c90), and the
//   Council and baalhighpriest fire only through their Hydra's heads
//   (FUN_005ca910, the heads owning the missiles): kept, none of them acts.
struct MonProps { int ias = 0, fcr = 0, fhr = 0, crush = 0, knock = 0, thorns = 0, stupidity = 0, fire_mastery = 0, fade = 0; };
inline MonProps monster_props(const Tables& tables, const MonType& type, int difficulty, Rng& unit_seed) {
    MonProps props;
    for (const auto& slot : type.props[std::size_t(std::clamp(difficulty, 0, 2))]) {
        if (slot.chance != 0 && int(unit_seed.next() % 100) >= slot.chance) continue;
        std::vector<d2d::d2s::ItemProp> stats;
        apply_mod(tables, { slot.code, slot.param, slot.min, slot.max }, stats, unit_seed);
        for (const auto& stat : stats) {
            using namespace d2d::d2s;
            switch (stat.stat) {
                case kFasterAttackRate: props.ias += stat.value; break;
                case kFasterCastRate: props.fcr += stat.value; break;
                case kFasterGetHitRate: props.fhr += stat.value; break;
                case kCrushingBlow: props.crush += stat.value; break;
                case kKnockback: props.knock += stat.value; break;
                case kAttackerTakesDamage: props.thorns += stat.value; break;
                case kStupidity: props.stupidity += stat.value; break;
                case kPassiveFireMastery: props.fire_mastery += stat.value; break;
                case kFade: props.fade += stat.value; break;
                default: break;
            }
        }
    }
    return props;
}

// FUN_006538a0's values before monster_stats' roll, at the type's own
// Level: life min / max, A1 min / max (the skill tooltips' summon lines,
// game::skill_text).
struct MonRanges { int min_hp = 0, max_hp = 0, a1_min = 0, a1_max = 0; };
inline MonRanges monster_ranges(const Monsters& monsters, int type, int difficulty) {
    if (type < 0 || std::size_t(type) >= monsters.types.size() || monsters.lvl.empty()) return {};
    const auto& type_info = monsters.types[std::size_t(type)];
    const auto index = std::size_t(std::clamp(difficulty, 0, 2));
    const auto& level_row = monsters.lvl[std::min<std::size_t>(std::size_t(std::max(type_info.level[index], 0)), monsters.lvl.size() - 1)];
    const auto& per_difficulty = type_info.diff[index];
    auto pct = [&](int base, int percent) { return type_info.no_ratio ? percent : base * percent / 100; };
    return { pct(level_row.hit_points[index], per_difficulty.min_hp), pct(level_row.hit_points[index], per_difficulty.max_hp),
             pct(level_row.damage[index], per_difficulty.a1_min), pct(level_row.damage[index], per_difficulty.a1_max) };
}

inline MonStats monster_stats(const Monsters& monsters, int type, int difficulty, Rng& rng, int level_add = 0, int players = 1) {
    MonStats stats;
    if (type < 0 || std::size_t(type) >= monsters.types.size()) return stats;
    const auto& type_info = monsters.types[std::size_t(type)];
    const int difficulty_index = std::clamp(difficulty, 0, 2);
    stats.level = std::max(type_info.level[std::size_t(difficulty_index)] + level_add, 1);
    if (monsters.lvl.empty()) return stats;
    const auto& level_row = monsters.lvl[std::min<std::size_t>(std::size_t(stats.level), monsters.lvl.size() - 1)];
    const auto& per_difficulty = type_info.diff[std::size_t(difficulty_index)];
    auto pct = [&](int base, int percent) { return type_info.no_ratio ? percent : base * percent / 100; };
    const int hit_points = level_row.hit_points[std::size_t(difficulty_index)];
    stats.hit_points  = std::max(rng.range(pct(hit_points, per_difficulty.min_hp), pct(hit_points, per_difficulty.max_hp)), 1);
    stats.hit_points = int(std::min<std::int64_t>(stats.hit_points + std::int64_t(stats.hit_points) * player_life_pct(players) / 100, 0x7fffff));
    stats.armor_class  = pct(level_row.armor_class[std::size_t(difficulty_index)], per_difficulty.armor_class);
    stats.to_hit  = pct(level_row.to_hit[std::size_t(difficulty_index)], per_difficulty.a1_th);
    stats.to_hit2 = pct(level_row.to_hit[std::size_t(difficulty_index)], per_difficulty.a2_th);
    stats.to_hit_s1 = pct(level_row.to_hit[std::size_t(difficulty_index)], per_difficulty.s1_th);
    stats.a1_min = pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.a1_min);
    stats.a1_max = std::max(pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.a1_max), stats.a1_min);
    stats.a2_min = pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.a2_min);
    stats.a2_max = std::max(pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.a2_max), stats.a2_min);
    stats.s1_min = pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.s1_min);
    stats.s1_max = std::max(pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.s1_max), stats.s1_min);
    stats.exp = pct(level_row.experience[std::size_t(difficulty_index)], per_difficulty.exp);
    stats.exp += int(std::int64_t(stats.exp) * player_exp_pct(players) / 100);
    for (std::size_t element = 0; element < 3; ++element) {
        const auto& element_info = per_difficulty.elements[element];
        if (type_info.el_type[element] < 0 || element_info.max <= 0) continue;
        stats.elements[element] = { type_info.el_type[element], element_info.pct ? element_info.pct : 100, pct(level_row.damage[std::size_t(difficulty_index)], element_info.min),
                    std::max(pct(level_row.damage[std::size_t(difficulty_index)], element_info.max), pct(level_row.damage[std::size_t(difficulty_index)], element_info.min)), element_info.dur, type_info.el_mode[element] };
    }
    return stats;
}

// The AI's distance to a unit, subtiles (FUN_005dc530): half of the
// smaller delta plus twice the larger.
inline int ai_distance(int dx, int dy) {
    dx = std::abs(dx); dy = std::abs(dy);
    return dy < dx ? (dy + dx * 2) / 2 : (dx + dy * 2) / 2;
}

// A unit's distance to a point less `less` on each axis
// (FUN_005dc480 / FUN_005dc380 / FUN_006417f0): (max x 2 + min) / 2.
inline int point_apart(int dx, int dy, int less) {
    dx = std::max(std::abs(dx) - less, 0); dy = std::max(std::abs(dy) - less, 0);
    return (dx <= dy ? dx + dy * 2 : dy + dx * 2) / 2;
}

// What Diablo's target score reads off a player (FUN_005e8530, stats by
// FUN_00625480): damageresist 0x24, magicresist 0x25, fire / lightning /
// cold resist 0x27 / 0x29 / 0x2b; maxdamage 0x16 and the fire / lightning /
// magic / cold max 0x31 / 0x33 / 0x35 / 0x37 plus poison max 0x3a >> 8; its
// left and right skills (FUN_00620190 / FUN_006201d0, -1 none) at their
// levels (FUN_006442a0) and Skills.txt attackrank; state 0xb on it.
struct DiabloPick {
    int damage_resist = 0, magic_resist = 0, fire_resist = 0, light_resist = 0, cold_resist = 0;
    int max_damage = 0;                             // the six max damages summed (poison's >> 8)
    int left_skill = -1, left_level = 0, left_rank = 0, right_skill = -1, right_level = 0, right_rank = 0;
    bool state_b = false;
};

// Diablo's score of a player (FUN_005e8530, C division throughout): the
// resists ((dr + mr x 2) x 4 + fire + lightning + cold in reach) / 15; the
// reach (FUN_00622c40) 100 else a clear line (FUN_005dd290: FUN_00622aa0
// mask 4) 75, plus 100 when Diablo is past 0x55 from his point and the
// player inside it (FUN_005dc480), both x 5; (the hands' level x attackrank
// / 4, state 0xb 200, the max damages / 2) x 2; 300 under 20 % life; all
// / 22, at least 1.
inline int diablo_score(const DiabloPick& pick, int life_pct, bool reach, bool line_clear, bool point_bonus) {
    const int resists = ((pick.damage_resist + pick.magic_resist * 2) * 4 + (reach ? pick.cold_resist : 0) + pick.light_resist + pick.fire_resist) / 15;
    const int closeness = (reach ? 100 : line_clear ? 75 : 0) + (point_bonus ? 100 : 0);
    const int ranked = (pick.left_level * pick.left_rank + pick.right_level * pick.right_rank) / 4;
    const int score = ((life_pct < 20 ? 100 : 0) * 3 + resists + closeness * 5 + (ranked + (pick.state_b ? 200 : 0) + pick.max_damage / 2) * 2) / 22;
    return score == 0 ? 1 : score;
}

inline constexpr std::array<std::array<int, 8>, 8> kNear{ {
    { -1, -1, -1, 0, 2, 4, 6, 8 }, { -1, -1, 0, 1, 2, 4, 6, 8 }, { -1, 0, 0, 2, 3, 5, 7, 8 }, { 0, 1, 2, 2, 4, 5, 7, 8 },
    { 2, 2, 3, 4, 5, 6, 7, 9 }, { 4, 4, 5, 5, 6, 7, 8, 9 }, { 6, 6, 7, 7, 7, 8, 10, 10 }, { 8, 8, 8, 8, 9, 9, 10, 11 } } };
// Distance between two units of sizes `size_a` / `size_b` (MonStats2
// SizeX; a player's 2), subtiles apart (FUN_00641530): close up, the
// 8x8 table at 0x6eb180 (one less when either is size 3), else the deltas
// less their half sizes, the smaller plus twice the larger.
inline int unit_distance(int dx, int dy, int size_a, int size_b) {
    dx = std::abs(dx); dy = std::abs(dy);
    if (dx < 8 && dy < 8 && size_a < 4 && size_b < 4) {
        int nearby = kNear[std::size_t(dy)][std::size_t(dx)];
        if (nearby < 0) return 0;
        if (size_a == 3 || size_b == 3) nearby = std::max(nearby - 1, 0);
        return size_a < 2 || size_b < 2 ? nearby + 1 : nearby;
    }
    const int half = size_a / 2 + size_b / 2, across = std::max(dx - half, 0), down = std::max(dy - half, 0);
    return down < across ? down + across * 2 : across + down * 2;
}

// The plain distance (FUN_006416d0, the client's object call): the deltas in
// subtiles less both half sizes (FUN_00620510: a player 2, an object its
// objects.txt SizeX), then the smaller plus twice the larger, halved.
inline int unit_range(int dx, int dy, int size_a, int size_b) {
    const int half = size_a / 2 + size_b / 2, across = std::max(std::abs(dx) - half, 0), down = std::max(std::abs(dy) - half, 0);
    return (across <= down ? across + down * 2 : down + across * 2) / 2;
}

// A monster's door (FUN_005b0f50 → FUN_005dd0b0 mode 8, FUN_005dcd50):
// of the closed IsDoor objects about, `spots` their offsets from it in
// subtiles, the nearest by squared distance (FUN_005b0bd0) under 9, the
// first on a tie. Its index, or -1.
inline int door_pick(std::span<const std::pair<int, int>> spots) {
    int pick = -1, best = 9;
    for (std::size_t i = 0; i < spots.size(); ++i)
        if (const int squared = spots[i].first * spots[i].first + spots[i].second * spots[i].second; squared < best) { pick = int(i); best = squared; }
    return pick;
}

// Can a unit of `size` (MonStats2 SizeX) operate an object of SizeX /
// SizeY `size_x` / `size_y`, `dx`, `dy` subtiles from it (FUN_00623660)?
// Touching (unit_distance 0 with the object's SizeX), else within 2 of the
// object's rect (its spot less half its size), the corners 1 in for a unit
// under size 3; a sizeless object within 1 of its spot.
inline bool object_reach(int dx, int dy, int size, int size_x, int size_y) {
    if (unit_distance(dx, dy, size, size_x) == 0) return true;
    const int left = -(size_x / 2), top = -(size_y / 2);
    if (size_x < 1 || size_y < 1) return std::abs(dx - left) <= 1 && std::abs(dy - top) <= 1;
    if (dx < left - 2 || dx > left + size_x + 2 || dy < top - 2 || dy > top + size_y + 2) return false;
    if (size > 2 || (dy >= top - 1 && dy <= top + size_y + 1)) return true;
    return dx >= left - 1 && dx <= left + size_x + 1;
}

// Is the line between two units cut (FUN_00622aa0 -> FUN_00622920 ->
// FUN_0064e260)? Subtiles, sizes clamped to 2 (a player's 2, a monster's
// MonStats2 SizeX). Units whose deltas sum under their sizes always see
// each other; else each end steps its size toward the other along the
// longer axis (both on a diagonal), then every subtile of the line between,
// ends included, goes to `blocked` (the AI asks collision mask 4).
// tools/emu/sight.py checks it against game.exe.
template <class Blocked>
bool sight_blocked(int from_x, int from_y, int size1, int to_x, int to_y, int size2, Blocked&& blocked) {
    size1 = std::min(size1, 2); size2 = std::min(size2, 2);
    const int across = std::abs(to_x - from_x), down = std::abs(to_y - from_y);
    if (across + down < size1 + size2) return false;
    if (down <= across) { if (from_x < to_x) { from_x += size1; to_x -= size2; } else { from_x -= size1; to_x += size2; } }
    if (across <= down) { if (from_y < to_y) { from_y += size1; to_y -= size2; } else { from_y -= size1; to_y += size2; } }
    const int dx = std::abs(to_x - from_x), dy = std::abs(to_y - from_y), step_x = to_x >= from_x ? 1 : -1, step_y = to_y >= from_y ? 1 : -1;
    for (int k = 0; k <= std::max(dx, dy); ++k) {
        const int off_x = dx >= dy ? k : k * dx / dy, off_y = dx >= dy ? (dx ? k * dy / dx : 0) : k;
        if (blocked(from_x + step_x * off_x, from_y + step_y * off_y)) return true;
    }
    return false;
}

inline constexpr std::array<std::array<int, 2>, 8> kDir{ { { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } } };   // DAT_006f1798
inline constexpr std::array<std::array<int, 3>, 25> kTry{ { { 5, 4, 6 }, { 4, 5, 6 }, { 4, 3, 5 }, { 4, 3, 2 }, { 3, 4, 2 }, { 6, 5, 4 }, { 5, 4, 6 },
    { 4, 3, 5 }, { 3, 4, 2 }, { 2, 3, 4 }, { 6, 7, 5 }, { 6, 7, 5 }, { 6, 7, 5 }, { 2, 1, 3 }, { 2, 1, 3 }, { 6, 7, 0 }, { 7, 0, 6 }, { 0, 1, 7 },
    { 1, 0, 2 }, { 2, 1, 0 }, { 7, 0, 6 }, { 0, 7, 6 }, { 0, 1, 7 }, { 0, 1, 2 }, { 1, 0, 2 } } };   // DAT_006f1518
// A move's path toward subtile (to_x, to_y), the toward pather of path
// types 2 / 5 / 6 / 0xd (FUN_00679c80; a monster's walk and run are 0xd,
// `steps` path +0x91: 5, FUN_005a63f0). The straight line (FUN_00679720, a
// Bresenham whose short-axis step is tested only past a remainder) is the
// whole path when clear; else it stops short at the last free subtile, which
// is the path when it's within `near` of the end (FUN_00679380: the
// size-1 unit_distance; 1 at a player or monster, FUN_006498a0, else 0).
// Else from there up to `steps` 8-way steps toward the end, each the first
// free of three directions (DAT_006f1518 by FUN_00678c10's 5x5 index,
// FUN_006793e0; a third of 0xff isn't tried), stopping at the end, a wall or
// a step back; a point at each turn (the subtile before it, none at the
// start) and the last one unless that step turned. The points are subtiles;
// `blocked` asks the unit's collision (FUN_0064d910).
// tools/emu/moves.py checks it against game.exe.
template <class Blocked>
std::vector<std::pair<int, int>> toward_path(int x, int y, int to_x, int to_y, int steps, int nearby, Blocked&& blocked) {
    using P = std::pair<int, int>;
    auto sign = [](int value) { return value >= 0 ? 1 : -1; };
    P end{ to_x, to_y };
    const bool clear = [&] {                                      // FUN_00679720: `end` cut to the last free subtile
        const int dx = to_x - x, dy = to_y - y, long_x = std::abs(dx) + 1, long_y = std::abs(dy) + 1;
        int at_x = x, at_y = y;
        auto cut = [&](P last) { end = last; return false; };
        if (long_y == long_x) {
            for (;;) {
                const P last{ at_x, at_y };
                if (at_x == to_x) return true;
                at_y += sign(dy); at_x += sign(dx);
                if (blocked(at_x, at_y)) return cut(last);
            }
        }
        const bool by_x = long_y < long_x;                        // the long axis
        int& major = by_x ? at_x : at_y, & minor = by_x ? at_y : at_x;
        const int major_end = by_x ? to_x : to_y, major_step = sign(by_x ? dx : dy), minor_step = sign(by_x ? dy : dx);
        const int major_len = by_x ? long_x : long_y, minor_len = by_x ? long_y : long_x;
        if (major == major_end) return true;
        for (int err = minor_len;;) {
            const P last{ at_x, at_y };
            major += major_step;
            if (blocked(at_x, at_y)) return cut(last);
            if ((err += minor_len) >= major_len) {
                minor += minor_step; err -= major_len;
                if (err > 0 && blocked(at_x, at_y)) return cut(last);
            }
            if (major == major_end) return true;
        }
    }();
    if (clear || unit_distance(end.first - to_x, end.second - to_y, 1, 1) <= nearby) return { end };
    std::vector<P> points;
    P here{ x, y };
    if (end != here) { points.push_back(end); here = end; }
    int prev = 0xff, taken = 0;
    bool turned = false;
    while (taken < steps && here != P{ to_x, to_y }) {
        turned = false;
        const int dx = to_x - here.first, dy = to_y - here.second, adx = std::abs(dx), ady = std::abs(dy);   // FUN_00678c10
        int aim_x = dx, aim_y = dy;
        if (adx >= ady * 2) aim_y = dy < 0 ? -1 : dy & 1;
        else if (adx * 2 <= ady) aim_x = dx < 0 ? -1 : dx & 1;
        const auto& tries = kTry[std::size_t(std::clamp(aim_x, -2, 2) * 5 + 12 + std::clamp(aim_y, -2, 2))];
        int dir = -1;
        for (std::size_t pick = 0; pick < 3 && dir < 0; ++pick)
            if (tries[pick] != 0xff && !blocked(here.first + kDir[std::size_t(tries[pick])][0], here.second + kDir[std::size_t(tries[pick])][1])) dir = tries[pick];
        if (dir < 0 || ((dir - 4) & 7) == prev) { turned = false; break; }
        const P from = here;
        here = { here.first + kDir[std::size_t(dir)][0], here.second + kDir[std::size_t(dir)][1] };
        if (dir != prev) {
            if (from != P{ x, y }) points.push_back(from);
            turned = true;
        }
        ++taken;
        prev = dir;
    }
    if (!turned && taken) points.push_back(here);
    return points;
}

// The search pather (FUN_0067b850, path type 1; type 7's close leg): an A*
// over subtiles from (x, y) to (to_x, to_y) in a pool of 200 nodes. Costs:
// a straight step 2, a diagonal 3 (FUN_0067b200); the estimate is 2 x the
// long axis + the short one (FUN_0067adc0). The open list is kept sorted by
// cost (FUN_0067aed0: a node goes before the first that costs as much, so
// ties pop newest first), and a cheaper way to a node on it changes its
// cost without moving it. A cheaper way to a closed node passes down its
// children (up to 8 links a node, FUN_0067afe0, a stack). Neighbours go in
// the order (-1,-1) (-1,+1) (+1,-1) (+1,+1) (-1,0) (0,-1) (+1,0) (0,+1)
// (FUN_0067b440). It stops at the end, with the open list empty or the
// pool spent, and keeps the node nearest the end (the estimate; at a tie,
// one more than 5 further along). The points (FUN_0067b690) are that
// node and each node where the way turns, start first, the start left
// out; none past 77 or with no step. With a unit to walk to
// (`to_unit`), nothing when all eight subtiles 2 off the end are blocked
// (FUN_0067b740).
template <class Blocked>
std::vector<std::pair<int, int>> search_path(int x, int y, int to_x, int to_y, bool to_unit, Blocked&& blocked) {
    using P = std::pair<int, int>;
    if (to_unit && std::ranges::all_of(std::array<P, 8>{ { { -2, -2 }, { -2, 2 }, { 2, -2 }, { 2, 2 }, { -2, 0 }, { 0, -2 }, { 2, 0 }, { 0, 2 } } },
                                       [&](P off) { return blocked(to_x + off.first, to_y + off.second); }))
        return {};
    struct Node { P at; int cost = 0, estimate = 0, walked = 0; int parent = -1; std::array<int, 8> children{ -1, -1, -1, -1, -1, -1, -1, -1 }; bool closed = false; };
    std::vector<Node> pool;
    pool.reserve(200);
    std::vector<int> open;                                        // sorted by cost, front first
    auto estimate = [&](P spot) {
        const int across = std::abs(spot.first - to_x), down = std::abs(spot.second - to_y);
        return down <= across ? down + across * 2 : across + down * 2;
    };
    auto push_open = [&](int index) {
        const auto before = std::ranges::find_if(open, [&](int other) { return pool[std::size_t(index)].cost <= pool[std::size_t(other)].cost; });
        open.insert(before, index);
    };
    auto step_cost = [](P from, P dest) { return from.first == dest.first || from.second == dest.second ? 2 : 3; };
    auto link = [](Node& node, int child) {
        if (const auto free = std::ranges::find(node.children, -1); free != node.children.end()) *free = child;
    };
    pool.push_back({ .at = { x, y }, .cost = estimate({ x, y }), .estimate = estimate({ x, y }) });
    push_open(0);
    auto relax = [&](int from, P spot) {                            // FUN_0067b200: false when the pool is spent
        const int walked = pool[std::size_t(from)].walked + step_cost(pool[std::size_t(from)].at, spot);
        int index = -1;                                           // FUN_0067ae00 / FUN_0067ae50: open or closed
        for (std::size_t slot = 0; slot < pool.size() && index < 0; ++slot)
            if (pool[slot].at == spot) index = int(slot);
        if (index < 0) {
            if (pool.size() == 200) return false;
            pool.push_back({ .at = spot, .estimate = estimate(spot), .walked = walked, .parent = from });
            pool.back().cost = pool.back().estimate + walked;
            const int added = int(pool.size()) - 1;
            push_open(added);
            link(pool[std::size_t(from)], added);
            return true;
        }
        link(pool[std::size_t(from)], index);
        auto& node = pool[std::size_t(index)];
        if (walked >= node.walked) return true;
        node.parent = from; node.walked = walked; node.cost = node.estimate + walked;
        if (!node.closed) return true;
        std::vector<int> stack{ index };                          // FUN_0067afe0
        while (!stack.empty()) {
            const int top = stack.back();
            stack.pop_back();
            for (const int child : pool[std::size_t(top)].children) {
                if (child < 0) break;
                auto& next = pool[std::size_t(child)];
                const int through = pool[std::size_t(top)].walked + step_cost(pool[std::size_t(top)].at, next.at);
                if (through < next.walked) {
                    next.parent = top; next.walked = through; next.cost = next.estimate + through;
                    stack.push_back(child);
                }
            }
        }
        return true;
    };
    int best = -1;
    while (!open.empty()) {
        const int index = open.front();                           // FUN_0067af40: to the closed list
        open.erase(open.begin());
        pool[std::size_t(index)].closed = true;
        const auto& node = pool[std::size_t(index)];
        if (best < 0 || node.estimate < pool[std::size_t(best)].estimate
            || (node.estimate == pool[std::size_t(best)].estimate && pool[std::size_t(best)].walked + 5 < node.walked))
            best = index;
        if (node.estimate == 0) break;
        const P from = node.at;
        bool spent = false;
        for (const P& off : { P{ -1, -1 }, P{ -1, 1 }, P{ 1, -1 }, P{ 1, 1 }, P{ -1, 0 }, P{ 0, -1 }, P{ 1, 0 }, P{ 0, 1 } }) {
            const P next{ from.first + off.first, from.second + off.second };
            if (!blocked(next.first, next.second) && !relax(index, next)) { spent = true; break; }
        }
        if (spent) break;
    }
    std::vector<P> points;                                        // FUN_0067b690, end first
    P last_step{ -2, -2 };
    for (int index = best; index >= 0 && points.size() <= 77; index = pool[std::size_t(index)].parent) {
        const auto& node = pool[std::size_t(index)];
        if (node.parent < 0) break;
        const P step{ node.at.first - pool[std::size_t(node.parent)].at.first, node.at.second - pool[std::size_t(node.parent)].at.second };
        if (step != last_step) { points.push_back(node.at); last_step = step; }
    }
    if (points.empty() || points.size() > 77) return {};
    std::ranges::reverse(points);
    return points;
}

// The wall pather (FUN_0067c2d0, path type 0xf: a monster's walk when the
// toward pather finds nothing, FUN_005a6290). A line (FUN_0067b9f0, a
// Bresenham on the long axis) of `steps` (+0x91; 0x28 at least with a unit
// to walk to) less one subtiles at most, else nothing; nothing either with
// two subtiles or fewer. Walking it, a blocked subtile sends two tracers
// round the obstacle from the one before (FUN_0067bdf0), taking turns: one
// keeps a wall on its right (turning left after each step, right when
// blocked), the other on its left, starting 45° off the line
// (DAT_006f1e18 +1 / +7, of 16 directions from north clockwise,
// DAT_006f1d98). A step tries its way and up to three turns
// (FUN_0067bbf0). A tracer back on the line further on is spliced in
// (FUN_0067bd80; the subtile after isn't tested); one that steps onto the
// other's last-but-one spot ends the path before the obstacle. A tracer
// that reaches the other's spot goes on alone until they part. They stop
// stuck or past `steps` less the line less the walked; then, unless over
// 0x50 were left, the path ends with the tracer nearer the end, if nearer
// than the start (a tracer with no step counts as (1, 0) or (0, 0)). The
// points (FUN_0067c1e0) are each turn and the last; a first turn on both
// axes isn't one.
template <class Blocked>
std::vector<std::pair<int, int>> wall_path(int x, int y, int to_x, int to_y, int steps, bool to_unit, Blocked&& blocked) {
    using P = std::pair<int, int>;
    static constexpr std::array<P, 8> kVec{ { { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 } } };
    static constexpr std::array<int, 9> kFirst{ 7, 0, 1, 6, -1, 2, 5, 4, 3 };              // by (dy + 1) * 3 + dx + 1
    static constexpr std::array<int, 8> kLeftAfter{ 6, 6, 0, 0, 2, 2, 4, 4 }, kLeftBlocked{ 2, 2, 4, 4, 6, 6, 0, 0 };
    static constexpr std::array<int, 8> kRightAfter{ 2, 2, 4, 4, 6, 6, 0, 0 }, kRightBlocked{ 6, 0, 0, 2, 2, 4, 4, 6 };
    if (to_unit && steps < 0x28) steps = 0x28;
    const int dx = to_x - x, dy = to_y - y, step_x = dx >= 0 ? 1 : -1, step_y = dy >= 0 ? 1 : -1;
    const int across = std::abs(dx), down = std::abs(dy);
    std::vector<P> line;
    int heading = 0;                                              // 0 -y, 1 +x, 2 +y, 3 -x: the long axis
    if (across < down || across == 0) {
        heading = step_y > 0 ? 2 : 0;
        if (down > 0 && down <= steps - 1)
            for (int walked = 1, err = 0, at_x = x; walked <= down; ++walked) {
                if ((err += across) >= down) { err -= down; at_x += step_x; }
                line.emplace_back(at_x, y + step_y * walked);
            }
    } else {
        heading = step_x > 0 ? 1 : 3;
        if (across <= steps - 1)
            for (int walked = 1, err = 0, at_y = y; walked <= across; ++walked) {
                if ((err += down) >= across) { err -= across; at_y += step_y; }
                line.emplace_back(x + step_x * walked, at_y);
            }
    }
    int count = int(line.size());
    if (count <= 2) return {};
    struct Tracer {
        int dir; P cur; P next{}; int meet = 0; bool done = false; std::vector<P> points;
        const std::array<int, 8>* after; const std::array<int, 8>* turn;
    };
    auto distance2 = [&](P spot) { return (spot.first - to_x) * (spot.first - to_x) + (spot.second - to_y) * (spot.second - to_y); };
    // FUN_0067bdf0: false ends the path at `index`; true goes on from `index`.
    auto detour = [&](P prev, int& index, int budget) {
        const P first = line[std::size_t(index)];
        const int base = kFirst[std::size_t((first.second - prev.second + 1) * 3 + first.first - prev.first + 1)];
        Tracer left{ .dir = base + 1, .cur = prev, .after = &kLeftAfter, .turn = &kLeftBlocked };
        Tracer right{ .dir = base + 7, .cur = prev, .after = &kRightAfter, .turn = &kRightBlocked };
        Tracer* cur = &left;
        Tracer* other = &right;
        while (true) {
            if (!cur->done) {
                bool free = false;
                for (int turn = 0; turn < 4 && !free; ++turn) {          // FUN_0067bbf0
                    if (turn) cur->dir = (*cur->turn)[std::size_t(cur->dir & 7)];
                    const P vec = kVec[std::size_t(cur->dir & 7)];
                    cur->next = { cur->cur.first + vec.first, cur->cur.second + vec.second };
                    free = !blocked(cur->next.first, cur->next.second);
                }
                if (!free) {
                    cur->done = true;
                } else {
                    const P next = cur->next;
                    if (!cur->points.empty()) {
                        const std::array<int, 4> ahead_by{ prev.second - next.second, next.first - prev.first, next.second - prev.second, prev.first - next.first };
                        const int ahead = ahead_by[std::size_t(heading)], rejoin = ahead - 1 + index;
                        if (ahead > 0 && rejoin < count && line[std::size_t(rejoin)] == next) {       // FUN_0067bd80
                            cur->points.push_back(next);
                            std::vector<P> spliced(line.begin(), line.begin() + index);
                            spliced.insert(spliced.end(), cur->points.begin(), cur->points.end());
                            spliced.insert(spliced.end(), line.begin() + rejoin + 1, line.begin() + count);
                            line = std::move(spliced);
                            index += int(cur->points.size());
                            count = int(line.size());
                            return true;
                        }
                    }
                    if (other->points.size() > 1) {
                        if (cur->meet != 0) {
                            if (next == other->points[std::size_t(cur->meet - 2)]) return false;   // round in a ring
                            cur->meet = 0;
                        }
                        if (next == other->cur) cur->meet = int(other->points.size());
                    }
                    cur->points.push_back(next);
                    cur->cur = next;
                    cur->dir = (*cur->after)[std::size_t(cur->dir & 7)];
                    if (budget - count - 1 <= int(cur->points.size())) cur->done = true;
                }
            }
            if (cur->meet == 0) std::swap(cur, other);
            if (left.done || right.done) break;
        }
        if (budget > 0x50) return false;
        auto last = [](const Tracer& tracer) { return tracer.points.empty() ? P{ tracer.done ? 1 : 0, 0 } : tracer.points.back(); };
        const int to_left = distance2(last(left)), to_right = distance2(last(right)), from_start = distance2({ x, y });
        const Tracer& best = to_left < to_right ? left : right;
        if (from_start < std::min(to_left, to_right) || (to_left >= to_right && from_start < to_right)) return false;
        line.resize(std::size_t(index));
        line.insert(line.end(), best.points.begin(), best.points.end());
        index = count = int(line.size());
        return true;
    };
    P prev{ x, y };
    for (int index = 0; index < count; ++index) {
        if (blocked(line[std::size_t(index)].first, line[std::size_t(index)].second) && !detour(prev, index, steps - index)) {
            count = index;
            break;
        }
        if (index < int(line.size())) prev = line[std::size_t(index)];
    }
    line.resize(std::size_t(count));
    if (line.size() <= 1) return line;                            // FUN_0067c1e0
    std::vector<P> points;
    int run = 0, last_dx = line[0].first - x, last_dy = line[0].second - y;
    for (std::size_t at = 0; at + 1 < line.size(); ++at) {
        const int step_dx = line[at + 1].first - line[at].first, step_dy = line[at + 1].second - line[at].second;
        if (step_dx == last_dx && step_dy == last_dy) { ++run; last_dx = step_dx; }
        else if (run < 1 && last_dx != step_dx && last_dy != step_dy) { run = 1; last_dx = -2; }
        else { points.push_back(line[at]); run = 0; last_dx = step_dx; }
        last_dy = step_dy;
    }
    points.push_back(line.back());
    return points;
}

// A monster's walk (FUN_005a7c20 -> FUN_005a63f0 -> FUN_005a6290; town
// NPCs, Cain, the merc and pets walk as monsters): the toward pather (type
// 0xd: 5 steps, near 1 at a unit, FUN_006498a0); with no point left, the
// wall pather (type 0xf: 5 steps, 0x28 at a unit). Leading points on its
// own subtile are dropped as the path is made (FUN_00649970 ->
// FUN_0064fe40). None at its own subtile or over 100 off. `steps`: the
// path's (+0x91; a think's pace can set it, FUN_005a6260).
template <class Blocked>
std::vector<std::pair<int, int>> monster_path(int x, int y, int to_x, int to_y, bool to_unit, Blocked&& blocked, int steps = 5) {
    if ((to_x == x && to_y == y) || std::abs(to_x - x) > 100 || std::abs(to_y - y) > 100) return {};
    auto off_own = [&](std::vector<std::pair<int, int>> points) {
        const auto own = std::ranges::find_if(points, [&](const std::pair<int, int>& point) { return point != std::pair(x, y); });
        points.erase(points.begin(), own);
        return points;
    };
    if (auto toward = off_own(toward_path(x, y, to_x, to_y, steps, to_unit ? 1 : 0, blocked)); !toward.empty()) return toward;
    return off_own(wall_path(x, y, to_x, to_y, steps, to_unit, blocked));
}

// A player's path (type 7, FUN_00679ed0; +0x91 steps 0x49, FUN_00649d00):
// the toward pather; taken when its end is within `nearby` of the target
// and isn't the start. Else, with the target under 18 subtiles off (dx^2 +
// dy^2 < 325), the search pather when that finds a way; else the toward
// path.
// `steps`: the path's +0x91 (a player's 0x49; a merc given type 7, its own).
template <class Blocked>
std::vector<std::pair<int, int>> player_path(int x, int y, int to_x, int to_y, int nearby, bool to_unit, Blocked&& blocked, int steps = 0x49) {
    auto toward = toward_path(x, y, to_x, to_y, steps, nearby, blocked);
    if (!toward.empty() && unit_distance(toward.back().first - to_x, toward.back().second - to_y, 1, 1) <= nearby
        && toward.back() != std::pair(x, y))
        return toward;
    const int dx = x - to_x, dy = y - to_y;
    if (dx * dx + dy * dy < 0x145)
        if (auto searched = search_path(x, y, to_x, to_y, to_unit, blocked); !searched.empty()) return searched;
    return toward;
}

// A chase's check each frame of its move (FUN_00650840 -> FUN_006503f0, a
// path of type 2 / 0xd / 0xf): 0 stop when its target is within `stop`
// (+0x93: FUN_00649070, 0 for a walk or run) by unit_distance; 2 re-path
// when the target, a player or monster (`mover`), is over 5 subtiles on
// either axis from where it stood when pathed (`moved`: SP2 +0x14 less its
// spot now), or when the path has run out (`idx` +0x24 at `count` +0x28)
// short of its end (SP3 +0x18); else 1. With no target only the last test
// counts. A monster's re-path (FUN_00650350) is refused (0) when the budget
// (+0x94, 0x14 at each mode start: FUN_005a7c20 -> FUN_006490e0) is 0; else
// the points reached come off it (FUN_00649140, floor 0).
// tools/emu/moves.py checks it against game.exe.
inline int chase_check(bool target, int distance, int stop, bool mover, int moved_x, int moved_y, int idx, int count, bool at_end, int& budget) {
    const bool ran_out = idx >= count && !at_end;
    if (target && distance <= stop) return 0;
    if (target ? !((mover && (std::abs(moved_x) > 5 || std::abs(moved_y) > 5)) || ran_out) : !ran_out) return 1;
    if (budget == 0) return 0;
    budget = std::max(budget - idx, 0);
    return 2;
}

// A hostile monster's target search over the player lists (FUN_005dd7f0,
// game +0x10f8 lists 0..7). Each list is a player (`pet` false) and then
// its pets (FUN_005b1900 puts them after it: the merc, summons). `away` is
// a player in another act, in no room, or in a town room (FUN_0061ab00).
// An away player and its pets are skipped. Otherwise `nearest` takes the
// player's distance (FUN_005dc530). If that distance is 0x37 or more, the
// player and its pets are skipped too. A dead player (FUN_005541b0) counts
// as 0x7fffffff, but its pets are still tried. Each one tried is taken when
// its distance is under the best so far and, when `need_sight`, it isn't
// `blocked` (FUN_00622aa0 mask 4). Pets don't count for `nearest`.
// Then the monster lists (`list` 8: good monsters, 9: neutral; they come
// after the players): same act (`away`: another act), sight, no 0x37 or
// death test. List 8 is tried as a player is. List 9's nearest is taken
// from its own 0x7fffffff only when nothing else was found (FUN_005dd510:
// with a best it keeps it unless that's under 6 away with no path to it).
// ponytail: FUN_005dd510's path test (FUN_00649970) always finds a path.
// tools/emu/search.py checks this against game.exe.
struct SearchFoe { int distance = 0; bool pet = false, away = false, dead = false, blocked = false; int list = 0; };
struct SearchPick { int target = -1, best = 0, nearest = 0x7fffffff; };
inline SearchPick search_pick(std::span<const SearchFoe> foes, int best, bool need_sight) {
    SearchPick pick{ -1, best };
    bool skip = true;
    int nine = -1, nine_best = 0x7fffffff;
    for (std::size_t i = 0; i < foes.size(); ++i) {
        const auto& foe = foes[i];
        int distance = foe.distance;
        if (foe.list) {
            if (foe.away || (need_sight && foe.blocked)) continue;
            if (foe.list == 8 && distance < pick.best) { pick.target = int(i); pick.best = distance; }
            if (foe.list == 9 && distance < nine_best) { nine = int(i); nine_best = distance; }
            continue;
        }
        if (!foe.pet) {
            skip = foe.away;
            if (!skip) pick.nearest = std::min(pick.nearest, distance);
            skip = skip || distance >= 0x37;
            if (foe.dead) distance = 0x7fffffff;
        }
        if (skip || distance >= pick.best || (need_sight && foe.blocked)) continue;
        pick.target = int(i); pick.best = distance;
    }
    if (nine >= 0 && pick.target < 0) { pick.target = nine; pick.best = nine_best; }
    return pick;
}

// FUN_00650d70: friends by alignment (stat 0xac: 0 evil, 1 neutral, 2 good).
// Evil befriends evil, good good, neutral nobody. FUN_00554200's enemies
// are the rest, owners first (a pet is its player's: good).
inline bool friends(int mine, int theirs) { return mine == 0 ? theirs == 0 : mine == 2 && theirs == 2; }

// FUN_005dc380: ai_distance less the candidate's size on each axis (FUN_00620510).
inline int near_distance(int dx, int dy, int size) {
    return ai_distance(std::max(std::abs(dx) - size, 0), std::max(std::abs(dy) - size, 0));
}

// Confuse's skill-set target (FUN_005dd610 kind 3) searches as a random
// alignment, one draw of the unit's seed: neutral turns good or evil, evil
// and good swap on a 1.
inline int confuse_align(int align, bool draw) { return align == 1 ? (draw ? 2 : 0) : draw ? 2 - align : align; }

// A neutral or good monster's search, and Confuse's: FUN_005dd0b0 mode 5
// (FUN_005dcf70 over the units of the rooms near it, not town ones;
// FUN_005dca70 each). `distance` is near_distance, `threat` FUN_005dc920
// (a player's 14, a monster's MonStats threat). A live enemy within 0x23
// is a primary when its threat is 2 or more, else a secondary, each the
// nearest in sight (FUN_00622aa0 from it to the searcher). An evil searcher
// that meets a monster friend (or itself) not dying, aware (AI flag 8), in
// its area (FUN_0061b130) needs no sight from then on (`waking`). `skip`:
// in a room it doesn't search.
struct NearFoe { int distance = 0, align = 2, threat = 14; bool self = false, monster = false, dead = false, skip = false, blocked = false, waking = false; };
struct NearPick { int target = -1, best = 0x7fffffff, second = -1, second_best = 0x7fffffff; };
inline NearPick search_near(std::span<const NearFoe> foes, int align, bool need_sight) {
    NearPick pick;
    for (std::size_t i = 0; i < foes.size(); ++i) {
        const auto& foe = foes[i];
        if (foe.skip) continue;
        if (!foe.self && !foe.dead && !friends(align, foe.align)) {
            const bool first = foe.threat >= 2;
            if (foe.distance > 0x23 || foe.distance >= (first ? pick.best : pick.second_best) || (need_sight && foe.blocked)) continue;
            (first ? pick.target : pick.second) = int(i);
            (first ? pick.best : pick.second_best) = foe.distance;
        } else if (foe.monster && align == 0 && need_sight && foe.waking) {
            need_sight = false;
        }
    }
    return pick;
}

// The second-target search of a merc (and of the Vampire): FUN_005ddc30 ->
// FUN_005dd0b0 mode 6 (FUN_005dcbd0 over the near rooms' units). An enemy
// (FUN_005dc970: player or monster, neither dying, the candidate not in a
// town room, FUN_00554200's foe) under 0x31 off (near_distance), in sight
// (FUN_00622aa0 mask 4, always tested): threat 2 or more a primary, else a
// secondary, each the nearest, the first found at a tie. FUN_005dc970
// passes over a monster in state 0x92 (invis) within melee of the searcher
// (a player in it 80 % of the time unless in melee: the merc's foes are all
// monsters). The picks (-1 none) and their distances. FUN_005dd510 then
// takes the primary, else the secondary (merc_target).
struct SightFoe { int distance = 0, threat = 0; bool enemy = false, blocked = false, invisible = false, in_melee = false; };
struct SightPick { int first = -1, first_best = 0x7fffffff, second = -1, second_best = 0x7fffffff; };
inline SightPick search_sight(std::span<const SightFoe> foes) {
    SightPick pick;
    for (std::size_t i = 0; i < foes.size(); ++i) {
        const auto& foe = foes[i];
        if (!foe.enemy || (foe.invisible && foe.in_melee) || foe.distance >= 0x31) continue;
        const bool primary = foe.threat >= 2;
        if (foe.distance >= (primary ? pick.first_best : pick.second_best) || foe.blocked) continue;
        (primary ? pick.first : pick.second) = int(i);
        (primary ? pick.first_best : pick.second_best) = foe.distance;
    }
    return pick;
}

// FUN_005dd510's mode 7 (FUN_005dcc60): with no path to the primary, the
// nearest other enemy of threat 2 or more under 0x31 off in sight; -1 none.
inline int search_threat(std::span<const SightFoe> foes, int skip, int& distance) {
    int best = -1;
    distance = 0x7fffffff;
    for (std::size_t i = 0; i < foes.size(); ++i) {
        const auto& foe = foes[i];
        if (int(i) == skip || !foe.enemy || (foe.invisible && foe.in_melee) || foe.distance >= 0x31 || foe.threat < 2 || foe.distance >= distance || foe.blocked) continue;
        best = int(i); distance = foe.distance;
    }
    return best;
}

// FUN_005dd510 for a monster searcher: no secondary, the primary; no
// primary, the secondary; the secondary under 6 off and no path to the
// primary (`path_to_primary`: a type 2 path, the toward pather), mode 7's
// pick when within 0x13, else the secondary; else the primary.
template <class PathTo>
int sight_choice(std::span<const SightFoe> foes, const SightPick& pick, PathTo&& path_to_primary, int& distance) {
    if (pick.second < 0 || pick.first < 0) {
        distance = pick.first >= 0 ? pick.first_best : pick.second_best;
        return pick.first >= 0 ? pick.first : pick.second;
    }
    if (pick.second_best < 6 && !path_to_primary(pick.first)) {
        int other_best = 0;
        if (const int other = search_threat(foes, pick.first, other_best); other >= 0 && other_best <= 0x13) { distance = other_best; return other; }
        distance = pick.second_best;
        return pick.second;
    }
    distance = pick.first_best;
    return pick.first;
}

// A unit's direction 0..63 from (x, y) to (tx, ty), subtiles (FUN_0064fdc0
// -> FUN_0064fc60): the smaller delta over the larger in 128ths picks an
// eighth of a quadrant (the table at 0x6eb7e0), folded into its octant.
// 0 faces +x+y (screen south), counting clockwise on screen.
inline int direction64(int x, int y, int target_x, int target_y) {
    static constexpr std::array<int, 7> kStep{ 13, 26, 39, 53, 68, 85, 105 };
    const int across = std::abs(target_x - x), down = std::abs(target_y - y);
    const bool steep = across <= down;
    const int major = steep ? down : across, minor = steep ? across : down;
    // 16.16 deltas, 127 x the smaller in 32 bits (it wraps past 258 subtiles).
    const int eighth = major == 0 ? 0 : std::clamp(int(std::uint32_t(minor) * 0x10000u * 0x7fu) / int(std::uint32_t(major) * 0x10000u), 0, 127);
    int dir = int(std::ranges::count_if(kStep, [&](int step) { return step <= eighth; }));
    if (!steep) dir = (-dir - 1) & 0xf;
    if (target_y < y) dir = (-dir - 1) & 0x1f;
    return target_x < x ? (dir + 8) & 0x3f : (((-dir - 1) & 0x3f) + 8) & 0x3f;
}

// Andariel's think (MonAI 34 "Andariel", FUN_005f5830; the driver
// FUN_005b1740 calls it once she has a target). aip1..4 by difficulty:
// in melee, aip1 % AndrialSpray (skill 1) else a swing (A1); at range,
// aip2 % stand 5 frames, else aip3 % a skill — aip4 % (a second draw)
// the spray, else AndyPoisonBolt (skill 2) — else walk at the target.
// One rand(100) on her seed each, as drawn here; her MonStats row has
// both skills, so their >= 0 tests always pass.
enum class AndarielAct : std::uint8_t { spray, melee, idle, bolt, walk };
inline AndarielAct andariel_think(bool in_melee, const std::array<int, 8>& aip, Rng& rng) {
    if (in_melee) return rng(100) < aip[0] ? AndarielAct::spray : AndarielAct::melee;
    if (rng(100) < aip[1]) return AndarielAct::idle;
    if (rng(100) < aip[2]) return rng(100) < aip[3] ? AndarielAct::spray : AndarielAct::bolt;
    return AndarielAct::walk;
}

inline constexpr std::array<int, 8> kRing{ 29, 28, 27, 26, 25, 24, 31, 30 };
inline constexpr std::array<std::array<int, 9>, 8> kSweep{ {
    { 27, 14, 15, 3, 99, 7, 21, 22, 31 }, { 26, 12, 13, 2, 99, 6, 19, 20, 30 }, { 25, 10, 11, 1, 99, 5, 17, 18, 29 }, { 24, 8, 9, 0, 99, 4, 15, 16, 28 },
    { 31, 22, 23, 7, 99, 3, 13, 14, 27 }, { 30, 20, 7, 6, 99, 2, 1, 12, 26 }, { 29, 18, 19, 5, 99, 1, 9, 10, 25 }, { 28, 16, 17, 4, 99, 0, 23, 8, 24 } } };
// Where Andariel's spray missile on SC frame `frame` is aimed, subtiles
// from her, facing `dir64` (FUN_005cb580): the radius-3 ring point that
// way (0x6e3188 into the DIR32 ring table FUN_0063e7e0), then frames 4..12
// sweep across it (0x6e3140, 99 = no offset; frame 8 aims at the ring point).
inline std::pair<int, int> andariel_spray_aim(int dir64, int frame) {
    static constexpr std::array<int, 32> kDx{ 0, -1, -1, -1, 0, 1, 1, 1, 0, -1, -2, -2, -2, -2, -2, -1, 0, 1, 2, 2, 2, 2, 2, 1, 0, -3, -3, -3, 0, 3, 3, 3 };
    static constexpr std::array<int, 32> kDy{ -1, -1, 0, 1, 1, 1, 0, -1, -2, -2, -2, -1, 0, 1, 2, 2, 2, 2, 2, 1, 0, -1, -2, -2, -3, -3, 0, 3, 3, 3, 0, -3 };
    const auto octant = std::size_t(((dir64 + 4) >> 3) & 7);
    int x = kDx[std::size_t(kRing[octant])], y = kDy[std::size_t(kRing[octant])];
    if (const int sweep = kSweep[octant][std::size_t(std::clamp(frame - 4, 0, 8))]; sweep != 99) { x += kDx[std::size_t(sweep)]; y += kDy[std::size_t(sweep)]; }
    return { x, y };
}

// The per-type MonAI thinks (the MonAI table 0x73ca18 + AI * 16: {target
// search, init, think, ...}; FUN_005b15d0), each ending in one AI helper:
//   idle n       FUN_005de080: stand n frames
//   a1 / a2 / s2 FUN_005ddf90(4 / 5 / 9): attack the target (the Fallen's S2: a taunt)
//   skill n      FUN_005dead0: Skill(n+1) in Sk(n+1)mode at the target
//   walk n       FUN_005dec80 / FUN_005ded40, flags n: walk at the target
//                (flags 2: can't set off, rand(100) < 70 wander 4, else stand 10)
//   run          FUN_005ded20 / FUN_005defb0: run at it
//   approach     FUN_005def80: walk at it
//   keep n x     FUN_005de6d0: walk to x subtiles off it, n at most
//   circle n x   FUN_005df7d0: one seed step (x: its low byte >= 0x80), walk round it
//   wander x y   FUN_005de200: walk to (x, y) subtiles off, four seed steps
//   none         already on its way (a back-off, FUN_005defe0 / FUN_005df140)
//   die          FUN_005ddfc0(0): into DT, flagged 0x20000 (a nest done laying)
//   around n x y FUN_005df680: walk to (x, y) subtiles off the target, four seed steps
//   home         FUN_005dede0: walk back to its spawn point (Blood Raven's command 10)
//   skill n x y  as skill, at (x, y) subtiles off the target when set (Blood Raven's raise)
//   point x y    FUN_005dead0 with no unit: Skill1 in Sk1mode at (x, y) subtiles (the Countess's map AI)
// A think's rand(100)s are one step each of the monster's seed (+0x20),
// drawn here in game.exe's order; a skill test with no skill (-1) draws
// nothing.
// direct: FUN_005deca0, a walk at the unit with no path (the frozen horror's, the blood lord's).
enum class MonAct : std::uint8_t { idle, a1, a2, s2, skill, walk, run, approach, keep, circle, wander, none, die, around, home, point, untraced, s3, s4, seq, fly, gone, s1, sc, mode_at, move_to, spawn, direct, warp };
// Whom an act is at: the think's target, no unit, the second target
// (FUN_005ddc30), the search's find (FUN_005dd0b0: kin, a corpse), the hurt
// one it heals, itself.
enum class ThinkUnit : std::uint8_t { target, none, second, kin, aid, self };
// `wait`: FUN_005de0f0 after the act; `event`: FUN_005de130 (its next think
// that many frames on); `spot`: a skill at the target's spot, not the unit.
struct Think {
    MonAct act = MonAct::idle;
    int n = 0, x = 0, y = 0;
    ThinkUnit unit = ThinkUnit::target;
    int wait = 0, event = 0;
    bool spot = false;
};
// What a think does besides its act: a state on / off (FUN_00639db0), an
// overlay (FUN_00621e40), stat 0x4a set (FUN_00627260), its group sent
// charging (FUN_0058f730), the leader's command done (FUN_0058ed10), the
// Summoner's quest opened (FUN_0059c330), its aura (Skill n) started
// (FUN_005701b0), a Vulture landing (FUN_005f2fc0) or taking off
// (FUN_005f2eb0), the AI toggled (FUN_005b0e00: id the special AI, value 1
// keeping the third scratch word), its skill-set target cleared
// (FUN_00573120).
struct ThinkEffect {
    // The wisps': set_word (FUN_0058ec00: id = its find's index x 4 + the
    // word 1..3), bless (a stat on the player it's at: id the stat).
    // Izual's first think: a4q1's greeting (FUN_005b4390).
    // Diablo's kept point set (mark: id x, value y; FUN_0058ef40 kind 10).
    // Act 5's: the siege beast takes a rider (FUN_005e17e0: the imp's AI word
    // +0x14 its id), a minion takes its command's unit as target (FUN_006416d0).
    // Baal's: a Vile Effigy made (clone, FUN_005fc860).
    enum Kind : std::uint8_t { state, overlay, stat, rally, command_done, summoner_open, aura, land, take_off, toggle, unset, set_word, bless, greet, mark, mount, retarget, clone } kind;
    int id = 0, value = 0;
};
struct ThinkIn {
    std::array<int, 8> aip{};                   // aip1..8 for its difficulty
    bool in_melee = false, got_hit = false;     // got hit: the last mode it left was GH (FUN_005dd2b0)
    int dist = 0;                               // AI distance to the target, subtiles
    int difficulty = 0, level = 0, life_pct = 100;
    std::array<bool, 3> skill{};                // Skill1..3 set
    int* state = nullptr;                       // the AI's scratch word (AI control +0x14)
    // The Fallen's: it leads its group (FUN_0058f0d0), its command (1:
    // charge; FUN_0058ee80, 0 none), a unit dying (DT) within 15 subtiles;
    // a Shaman's corpse to raise; set when it sends its group command 1
    // (FUN_0058f730).
    bool leader = false, dying = false, corpse = false;
    int* command = nullptr;
    bool* rally = nullptr;
    // A nest's: the game frame, its second scratch word (+0x18: laid so
    // far), whether the spot its young come out on is free (FUN_005fd350).
    int frame = 0;
    int* state2 = nullptr;
    bool spot_free = true;
    int home_dist = 0;                          // Blood Raven's: AI distance to her spawn point (FUN_005dc480)
    int off_x = 0, off_y = 0;                   // the target, subtiles off it
    // The velocity % its next mode gets (FUN_005de190 -> FUN_005a6260, set
    // when not 0: monster data +0x2c ctx +0x1c; FUN_005a63f0 takes and
    // clears it as that mode starts, stat 0x43 velocitypercent through
    // FUN_005a6380); the AI's third scratch word (+0x1c); its target's life
    // % (FUN_00621f20); in Spider Lay's state (0x16, FUN_00639df0);
    // MonStats Velocity / Run; aidel (a spot move that can't set off
    // thinks then, FUN_005a73e0).
    int* pace = nullptr;
    int* state3 = nullptr;
    int target_life_pct = 100;
    bool laying = false;
    int velocity = 0, run = 0, aidel = 15;
    // Act 2's: Skill4 / Skill5 set; stat 0x4a (its own); the target's fire /
    // cold resist (0x27 / 0x2b); the second target (FUN_005ddc30) and its AI
    // distance; the search's find (FUN_005dd0b0: the nearest kin or other
    // monster, its squared distance; the Greater Mummy's hurt one (its corpse:
    // `corpse`),
    // the count of those in reach); a skill usable at its target
    // (FUN_005fd470); its aura (the right skill) on; it leads its group
    // (FUN_0058f0d0); the effects; the pace's steps.
    std::array<bool, 2> skill45{};
    int regen = 0, target_fire_res = 0, target_cold_res = 0;
    bool second = false, kin = false, aid = false, usable = true, aura_on = true;
    int second_dist = 0x7fffffff, kin_dist = 0, in_reach = 0;
    ThinkUnit* away_from = nullptr;             // whom `away` backs off from (the second target's back-offs)
    std::vector<ThinkEffect>* effects = nullptr;
    int* steps = nullptr;
    // The Vulture's: its spot and the target's (subtiles); the target in its
    // room (FUN_00620bb0); it can land (FUN_005f2fc0: not down already, room
    // to stand); a weak one about (FUN_005f30f0: within 11, at most aip3 %
    // life); in a group (FUN_0058f0d0); the free spot nearest its circling
    // point (state2, state3) by the target's room, else by its own
    // (FUN_0064e7b0).
    int self_x = 0, self_y = 0, target_x = 0, target_y = 0;
    bool same_room = true, can_land = false, victim = false, grouped = false;
    std::array<std::optional<std::pair<int, int>>, 2> free_spot{};
    // The curse thinks' (special AI 10 / 11): it can swing (an A1 mode,
    // FUN_0046c140 bit 4, and not interact, MonStats flag 9), it has an RN
    // mode (bit 15), Terror's state (0x38) is on it. The other thinks': the
    // BatDemon in S4 (unit +0x10 mode 11); the SandMaggot's AI command 14
    // +0x18 (FUN_0058efa0).
    bool swings = true, runs = false, afraid = true, in_s4 = false;
    int* cast_word = nullptr;
    // Act 3's: its next think event's frame (FUN_005415a0 type 1, 0 none);
    // its group leader's mode (unit +0x10; the tentacles' head: 12 DD, 14
    // S4), with `grouped`; its command's unit found (FUN_00552f60).
    // Skill1's level (FUN_006442a0, base + bonus); state 0xc (the fetish
    // shaman's Inferno) on it.
    // Its own spot clear of 0xc01 (FUN_0064cb30).
    // A target found (search types 2 / 5 think without one). The zealots'
    // spare: the target a player, or a pet whose owner is (FUN_0058f090),
    // in act 3 (FUN_006427f0), with quest 0x15 bit 0 in the monster's
    // difficulty (FUN_0065c310).
    int next_event = 0, leader_mode = 0, skill_level = 1;
    // The line to the target clear of mask 4 (FUN_005dd290).
    // MonStats MissS1's Missiles.txt Range (FUN_0046ace0 +0x96; -1: none).
    bool command_unit = false, state_c = false, spot_clear = true, target = true, spared = false, line_clear = true;
    bool target_still = false;                  // the target a player or monster in mode 1 (NU: BaalTaunt's)
    int s1_range = -1;
    // Mephisto's: Skill6..8 set; his chooser's finds within 32 (FUN_005f7700:
    // the weakest unit, a player nearest).
    std::array<bool, 3> skill678{};
    bool weak = false, near_player = false;
    // The wisps': the wisps within 32 (FUN_0065a950, class 0x76, itself
    // too); its target a player not out of reach (FUN_005541b0) for the
    // dance's blessing.
    int wisps = 0;
    bool bless_ok = false;
    // Act 4's: a state its skill's record names (skill +0x80 / +0x82) on its
    // unit (the Abyss Knight's Bone Armor on itself, the Oblivion Knight's curse
    // and the finger mage's 0x54 on the target); the vile mother's young within
    // 25 (FUN_005dd0b0 search type 0xc) and a spot found to lay one
    // (FUN_005f9e30); the search's find's spot and unit id (the Regurgitator's
    // corpse, the Oblivion Knight's knight).
    bool skill_state = false, lay_ok = false;
    int young = 0, kin_x = 0, kin_y = 0, kin_id = 0;
    // Diablo's: his target pick's score and the players it looked at
    // (FUN_005e8f20), his kept point (the leader word, FUN_0058eef0: kind 10,
    // set to his own spot the first time), stuck where he stands
    // (FUN_0064d910 mask 0x40), the target in reach (FUN_00622c40), its left
    // and right skills (-1 none; FUN_00620190 / FUN_006201d0, at
    // left_level / right_level) and its state 0xb, a player; the difficulty's MonsterSkillBonus
    // (FUN_00573930); in the Chaos Sanctuary (FUN_0061a1b0: 108; the kin is
    // the target's leader found by FUN_005353f0); its lightning resist (0x29).
    int score = 0, players_looked = 0, word_x = 0, word_y = 0, left_skill = -1, right_skill = -1, skill_bonus = 0, target_light_res = 0;
    int left_level = 0, right_level = 0;            // the hands' levels (FUN_006442a0, each its own)
    bool has_word = false, stuck = false, reach = false, target_state_b = false, target_player = false, in_chaos = false;
    int self_size = 2, target_size = 2;             // FUN_00620510: the units' sizes (the distances take them off)
    // Act 5's: three lines to the target clear of 0x805 (FUN_005dc640);
    // Skill1's Skills.txt +0x158 (FUN_004e6c70, its reach); the siege
    // beast's rider (FUN_00552fd0), it lies dead (FUN_0063ea40: mode 0 or
    // 12), its state 0x69 test (FUN_006259b0), an imp within aip1 to take on
    // (FUN_005dd0b0, FUN_005e1720); the target in state 0x20 (FUN_00625760),
    // its FUN_00625d10 / FUN_00625d60 values, its own life % (FUN_00621f20
    // on itself); its command's last frame (+0x14) and its unit busy
    // (FUN_005541b0).
    bool lines_clear = true, rider = false, self_dead = false, state69 = false, imp_near = false, target_state20 = false, command_busy = false;
    int skill1_range = 0, target_max_life = 0, target_max_mana = 0, self_life_pct = 100, command_until = 0;
    // The Overseer's: the one that last hit it (FUN_005de830: a player not
    // dead, a monster a pet); its search (FUN_005e26d0): a minion within 24
    // under 50 % life, one within 20 not under FUN_005a0180, how many
    // minions (the MinionSpawner's FUN_005e2b70 count too: `young`).
    bool attacker = false, hurt_minion = false, free_minion = false;
    // The Imp's: MonStats imp1..imp4's aip1..8 (its think reads theirs).
    // A riding imp's state 0x8f (FUN_00639df0; `rider` is its mount).
    std::array<std::array<int, 8>, 4> imp_aip{};
    bool riding = false;
    // The catapult spotter's: its catapult gone (FUN_005edf70 finds none
    // behind it); a free spot at its strike's point (FUN_0064e7e0, mask
    // 0x805). The generic spawner's: its spawn type unset (AI control +0x3c
    // -1) and none found for it (FUN_005e6020); room for its young
    // (FUN_005fd350).
    bool catapult_gone = false, strike_spot = true, spawn_unset = false, spawn_found = true, spawn_room = true;
    // The putrid defiler's: a unit within 25 its search takes (FUN_005efa50:
    // FUN_005d2140's test), in reach (FUN_00622c40: `reach`).
    bool defile = false;
    // The Ancients' (FUN_005ef1a0 by class: 0 Talic 0x21c, 1 Madawc, 2
    // Korlic): asleep (a5q5's record +0x11 unset, FUN_0058cf90); Talic's
    // state 0x36; a player picked (FUN_005e8f20, as Diablo's) when there's no
    // target.
    int ancient = 0;
    bool asleep = false, state36 = false, picked = false;
    // The arrow traps': the level's trap kind (FUN_005fb650: 1 the poison
    // ball, else the spike). The desert turret's: the line to each of the
    // 8 points it may cast at clear (FUN_005fd470 skill 0xcb: FUN_00645910,
    // mask 0x805), by kTurretStep; `usable` the line to its target.
    int trap_kind = 0;
    std::array<bool, 8> ray_clear{};
};

// A monster's skill level (FUN_00573cb0): Sk*lvl + DifficultyLevels
// MonsterSkillBonus (+0x10 via FUN_00573930).
inline int monster_skill_level(int sk_lvl, int difficulty) {
    static constexpr std::array<int, 3> kMonsterSkillBonus{ 0, 3, 7 };
    return sk_lvl + kMonsterSkillBonus[std::size_t(std::clamp(difficulty, 0, 2))];
}

inline bool traced_ai(std::string_view ai_name) {
    static constexpr std::array<std::string_view, 107> kTraced{ "Skeleton", "Zombie", "Bighead", "BloodHawk", "Brute", "Wraith", "Goatman",
                                                               "CorruptRogue", "QuillRat", "CorruptArcher", "CorruptLancer", "SkeletonBow", "Fallen", "FallenShaman", "FoulCrowNest",
                                                               "BloodRaven", "SkeletonMage", "GargoyleTrap", "Arach", "Vampire", "Fetish", "Griswold", "Smith",
                                                               "Duriel", "Swarm", "PinHead", "Mummy", "SandLeaper", "ClawViper", "PantherWoman", "PantherJavelin",
                                                               "Scarab", "BatDemon", "Baboon", "SandRaider", "GreaterMummy", "SandMaggot", "Summoner", "Vulture",
                                                               "MaggotEgg", "MaggotLarva", "DarkWanderer", "SandMaggotQueen",
                                                               "Mosquito", "ThornHulk", "ZakarumZealot", "ZakarumPriest", "FrogDemon", "Tentacle", "TentacleHead",
                                                               "FetishShaman", "HighPriest", "Hydra", "FetishBlowgun", "Mephisto", "WillOWisp",
                                                               "VileMother", "VileDog", "FingerMage", "Regurgitator", "DoomKnight", "AbyssKnight", "OblivionKnight", "Megademon", "Izual", "Diablo", "TrappedSoul", "BaalCrab", "BaalCrabClone",
                                                               "ReanimatedHorde", "SiegeBeast", "Minion", "SuicideMinion", "Succubus", "SuccubusWitch", "Overseer",
                                                               "MinionSpawner", "Imp", "Catapult", "FrozenHorror", "BloodLord", "CatapultSpotter", "NpcBarb",
                                                               "GenericSpawner", "DeathMauler", "PutridDefiler", "BaalTentacle", "BaalMinion", "Ancient", "Nihlathak", "ClawViperEx", "Wussie", "BaalThrone", "BaalToStairs", "BaalTaunt", "UberIzual", "UberMephisto", "UberDiablo", "UberBaal", "SiegeTower",
                                                               "HellMeteor", "Sarcophagus", "Spirit", "ArcaneTower", "Trap-RightArrow", "Trap-LeftArrow", "DesertTurret" };
    return std::ranges::contains(kTraced, ai_name);
}

// FUN_005de200(radius): one side radius off, the other rand(radius), on the seed's low
// bit; the next two low bits negate x, then y.
inline Think think_wander(Rng& rng, int radius) {
    Think out{ MonAct::wander, radius, radius, radius };
    if ((rng.next() & 1) == 0) out.x = rng(radius); else out.y = rng(radius);
    if (rng.next() & 1) out.x = -out.x;
    if (rng.next() & 1) out.y = -out.y;
    return out;
}
inline Think think_circle(Rng& rng, int count) { return { MonAct::circle, count, (rng.next() & 0xff) >= 0x80 ? 1 : 0 }; }
// FUN_005dec80 flags 2 when the walk can't set off.
inline Think walk_failed(Rng& rng) { return rng(100) < 70 ? think_wander(rng, 4) : Think{ MonAct::idle, 10 }; }

// The Gargoyle Trap's shot (srvdofunc 93, FUN_005cc050), in subtiles from
// the trap at (x, y): the point square on at its target (on the axis it's
// nearer the target, its own spot moved up to 4 toward the target's; on
// the other, the target's), then the missile (FUN_0059fa30 flags 3) from
// a sixth of the way there less a subtile on both axes, flying along that
// same offset. {from_x, from_y, offset_x, offset_y}.
inline std::array<int, 4> gargoyle_shot(int x, int y, int target_x, int target_y) {
    auto toward = [](int from, int target) {
        for (int step = 0; step < 4; ++step) from += (from < target) - (from > target);
        return from;
    };
    const bool down = std::abs(target_x - x) < std::abs(target_y - y);
    const int off_x = (down ? toward(x, target_x) : target_x) - x, off_y = (down ? target_y : toward(y, target_y)) - y;
    return { x + off_x / 6 - 1, y + off_y / 6 - 1, off_x, off_y };
}

// The desert turret's casting steps (0x6e33a0: subtiles, x aip5) and its
// step's turn toward the target's eighth (0x6e33e0: [eighth][step]).
inline constexpr std::array<std::array<int, 2>, 8> kTurretStep{ { { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { 1, 0 } } };
inline constexpr std::array<std::array<int, 8>, 8> kTurretTurn{ { { 0, 0, 1, 2, 5, 6, 7, 0 }, { 1, 1, 1, 2, 3, 6, 7, 0 }, { 1, 2, 2, 2, 3, 4, 7, 0 }, { 1, 2, 3, 3, 3, 4, 5, 6 },
                                                                  { 1, 2, 3, 4, 4, 4, 5, 6 }, { 7, 2, 3, 4, 5, 5, 5, 6 }, { 7, 0, 3, 4, 5, 6, 6, 6 }, { 7, 0, 1, 4, 5, 6, 7, 7 } } };

// The desert turret's fan (srvdofunc 105, FUN_005cd6a0): `count` missiles
// from (x, y) at the point (to_x, to_y), side by side 2 subtiles apart
// across the way (by the offset's signs), centred on it. Each starts the
// offset / 6 out on both axes from the x offset (bugs.md), and flies the
// offset on (FUN_0059fa30 flags 3). None when the point is its spot.
// {from x, from y, offset x, offset y} each.
inline std::vector<std::array<int, 4>> turret_fan(int x, int y, int to_x, int to_y, int count) {
    const int dx = to_x - x, dy = to_y - y;
    if (dx == 0 && dy == 0) return {};
    int side_x = 0, side_y = 0;
    if (dx < 0 && dy < 0) { side_x = 2; side_y = -2; }
    else if (dx == 0) side_x = dy < 0 ? 2 : -2;
    else if (dx > 0) { side_y = 2; side_x = dy < 0 ? 2 : dy > 0 ? -2 : 0; }
    else if (dy > 0) { side_x = -2; side_y = -2; }
    else if (dy == 0) side_y = -2;
    std::vector<std::array<int, 4>> out;
    int off_x = dx - count / 2 * side_x, off_y = dy - count / 2 * side_y;
    for (int k = 0; k < count; ++k, off_x += side_x, off_y += side_y) out.push_back({ x + off_x / 6, y + off_x / 6, off_x, off_y });
    return out;
}

// `away(n, run)` backs off n subtiles from the target (FUN_005defe0 walking,
// FUN_005df140 running) and says whether it set off.
template <class Away>
Think mon_think(std::string_view ai_name, const ThinkIn& input, Rng& rng, Away&& away) {
    const auto& aip = input.aip;
    auto roll = [&] { return rng(100); };
    auto a1_or_a2 = [&](int chance) { return Think{ roll() < chance ? MonAct::a1 : MonAct::a2 }; };
    const Think idle2{ MonAct::idle, aip[1] }, walk{ MonAct::walk, 7 };
    // An act at the second target (FUN_005ddc30), a back-off from it.
    auto at_second = [](Think out) { out.unit = ThinkUnit::second; return out; };
    auto away_second = [&](int subtiles, bool running) {
        if (input.away_from) *input.away_from = ThinkUnit::second;
        const bool set_off = away(subtiles, running);
        if (input.away_from) *input.away_from = ThinkUnit::target;
        return set_off;
    };
    // Skeleton (2, FUN_005efcf0; hellbovine too).
    if (ai_name == "Skeleton") {
        if (!input.in_melee) { if (roll() < aip[0]) return walk; }
        else if (roll() < aip[2]) return a1_or_a2(aip[3]);
        return idle2;
    }
    // Zombie (3, FUN_005efe20): runs at a foe it's hit by, or aip1 % one
    // within aip2; else wanders 3, bar in the Burial Grounds.
    if (ai_name == "Zombie") {
        if (input.in_melee) return a1_or_a2(aip[3]);
        if (!input.got_hit && !(input.dist < aip[1] && roll() < aip[0]) && input.level != level_ids::kBurialGrounds) return think_wander(rng, 3);
        return { MonAct::run };
    }
    // Bighead (4, FUN_005eff50): above aip1 % life it closes in (aip3 % a
    // shot within 15); below, it backs off under 3 (pace 50), walks in past
    // 15, else aip4 % shoots, aip2 % circles, else stands 10. A shot (at
    // the target) needs a second target (FUN_005ddc30), its distance unread.
    if (ai_name == "Bighead") {
        if (!input.in_melee && input.got_hit) return { MonAct::a2 };
        if (input.life_pct >= aip[0]) {
            if (input.in_melee) return { MonAct::a1 };
            if (input.dist < 15 && input.second && roll() < aip[2]) return { MonAct::a2 };
            return walk;
        }
        if (input.dist < 3) { *input.pace = 50; return away(5, false) ? Think{ MonAct::none } : Think{ MonAct::a2 }; }
        if (input.dist > 15) return { MonAct::approach, 6 };
        if (input.second && roll() < aip[3]) return { MonAct::a2 };
        if (roll() >= aip[1]) return { MonAct::idle, 10 };
        return think_circle(rng, 3);
    }
    // BloodHawk (5, FUN_005f00e0): aip1 % a charge (state 1: a swing when
    // it lands in melee), else wanders (aip2 %: 4, else 3) past 3 subtiles;
    // in melee aip3 % a swing; else backs off 4, or swings.
    if (ai_name == "BloodHawk") {
        const bool charged = *input.state == 1;
        *input.state = 0;
        if (charged && input.in_melee) return { MonAct::a1 };
        if (!input.in_melee) {
            if (roll() < aip[0]) { *input.state = 1; return { MonAct::walk, 0 }; }
            if (input.dist > 3) return think_wander(rng, roll() < aip[1] ? 4 : 3);
        } else if (roll() < aip[2]) return { MonAct::a1 };
        return away(4, false) ? Think{ MonAct::none } : Think{ MonAct::a1 };
    }
    // Brute (7, FUN_005efb80): walks in without a draw; in melee aip3 % a
    // swing (aip4 % A1), else aip3 % circles 4, else stands 15.
    if (ai_name == "Brute") {
        if (!input.in_melee) return walk;
        if (roll() < aip[2]) return a1_or_a2(aip[3]);
        if (roll() < aip[2]) return think_circle(rng, 4);
        return { MonAct::idle, 15 };
    }
    // Wraith (9, FUN_005f0a20) and Goatman (12, FUN_005f12a0): aip1 % close
    // in (the wraith drifts up to 12 subtiles), in melee aip3 % A1, else
    // stand aip2.
    if (ai_name == "Wraith" || ai_name == "Goatman") {
        if (!input.in_melee) { if (roll() < aip[0]) return ai_name == "Wraith" ? Think{ MonAct::keep, 12, 0 } : walk; }
        else if (roll() < aip[2]) return { MonAct::a1 };
        return idle2;
    }
    // CorruptRogue (10, FUN_005f0b00): runs at a target past 20 - 3 x
    // difficulty (FUN_00573930's difficulty); in melee aip3 % A1, else
    // aip1 % close in (aip5 % running).
    if (ai_name == "CorruptRogue") {
        if (input.dist > 20 - 3 * input.difficulty) return { MonAct::run };
        if (input.in_melee) return roll() < aip[2] ? Think{ MonAct::a1 } : idle2;
        if (roll() >= aip[0]) return idle2;
        return roll() < aip[4] ? Think{ MonAct::run } : walk;
    }
    // QuillRat (14, FUN_005f1140): A1 in melee, spikes (A2) when hit;
    // wanders (aip4, at least 3) at aip1 or more; else aip2 % a back-off
    // of aip4 first, then the spikes.
    // ponytail: the leader's command (FUN_0058ee80) to shoot isn't sent.
    if (ai_name == "QuillRat") {
        if (input.in_melee) return { MonAct::a1 };
        if (input.got_hit) return { MonAct::a2 };
        const int roam = std::max(aip[3], 3);
        if (input.dist >= aip[0]) return think_wander(rng, roam);
        if (roll() >= aip[1]) {
            if (away(aip[3] & 0xff, false)) return { MonAct::none };
            if (input.dist > 3) return think_wander(rng, roam);
        }
        return { MonAct::a2 };
    }
    // CorruptArcher (35, FUN_005f5a20): at its second target (FUN_005ddc30,
    // its distance); none, it circles 3 or (50 %) stands aip3. It shoots a
    // foe it's hit by; under 6, aip4 % runs 12 off (pace 100); past aip8,
    // aip1 % walks in to aip8 (pace 10); past aip5 runs in aip5 (pace 100);
    // else aip2 % a shot (Skill2 aip6 %, Skill3 aip7 %, else Skill1 or A1).
    if (ai_name == "CorruptArcher") {
        if (!input.second) return roll() > 49 ? Think{ MonAct::idle, aip[2] } : think_circle(rng, 3);
        const int dist = input.second_dist;
        if (!input.in_melee && input.got_hit) return at_second({ MonAct::a1 });
        if (dist < 6 && roll() < aip[3]) { *input.pace = 100; if (away_second(12, true)) return { MonAct::none }; }
        if (aip[7] > 0 && aip[7] < dist && roll() < aip[0]) { *input.pace = 10; return at_second({ MonAct::approach, aip[7] }); }
        if (aip[4] < dist) { *input.pace = 100; return at_second({ MonAct::run, aip[4] }); }
        if (roll() >= aip[1]) return { MonAct::idle, aip[2] };
        if (input.skill[1] && roll() < aip[5]) return at_second({ MonAct::skill, 1 });
        if (input.skill[2] && roll() < aip[6]) return at_second({ MonAct::skill, 2 });
        return at_second(input.skill[0] ? Think{ MonAct::skill, 0 } : Think{ MonAct::a1 });
    }
    // CorruptLancer (36, FUN_005f5d50): runs in past aip5 (state 1: it
    // strikes on arrival); at range aip1 % closes in (aip4 % running);
    // in melee aip2 % (or on arrival) Skill1..3 at aip6..8 %, else A1.
    if (ai_name == "CorruptLancer") {
        if (aip[4] < input.dist) { *input.state = 1; return { MonAct::run }; }
        if (!input.in_melee) {
            if (roll() >= aip[0]) return { MonAct::idle, aip[2] };
            return roll() >= aip[3] ? Think{ MonAct::approach, 3 } : Think{ MonAct::run };
        }
        if (*input.state == 0 && roll() >= aip[1]) return { MonAct::idle, aip[2] };
        *input.state = 0;
        for (int skill = 0; skill < 3; ++skill)
            if (input.skill[std::size_t(skill)] && roll() < aip[std::size_t(5 + skill)]) return { MonAct::skill, skill };
        return { MonAct::a1 };
    }
    // SkeletonBow (37, FUN_005f6070): shoots a foe it's hit by; past 19,
    // aip3 % keeps aip5 off (aip4 at most a go), else stands 20; else aip1
    // % shoots, 20 % circles 3, else stands aip2.
    if (ai_name == "SkeletonBow") {
        if (input.got_hit) return { MonAct::a1 };
        if (input.dist > 19) return roll() < aip[2] ? Think{ MonAct::keep, aip[3], aip[4] } : Think{ MonAct::idle, 20 };
        if (roll() < aip[0]) return { MonAct::a1 };
        if (roll() < 20) return think_circle(rng, 3);
        return idle2;
    }
    // Fallen (6, FUN_005f02c0): with a unit dying near, it backs off 12 from
    // its target, dropping its command (a seed step: 1 in 20 a scream).
    // Charging (command 1) it walks in, then aip3 % swings (aip4 % A1),
    // else stands 5. Otherwise: hit, it walks in; a leader within 15 aip1 %
    // taunts (S2) and sets its group charging; aip2 or nearer it walks in,
    // else 30 % wanders 3; in melee aip3 % (or on a scare, state 1) swings,
    // else 30 % taunts, else stands 10.
    // ponytail: the stand-10 outside NU is left out (our moves re-think
    // mid-way, game.exe's at their end); commands other than 1 never come.
    if (ai_name == "Fallen") {
        if (input.dying) {
            *input.state = 1; *input.command = 0;
            if (away(12, false)) { (void)rng.next(); return { MonAct::none }; }
        }
        if (*input.command == 1) {
            if (!input.in_melee) return { MonAct::walk, 0 };
            if (roll() >= aip[2]) return { MonAct::idle, 5 };
            return a1_or_a2(aip[3]);
        }
        if (!input.in_melee && input.got_hit) return { MonAct::walk, 0 };
        if (input.dist < 15 && input.leader && roll() < aip[0]) { *input.rally = true; return { MonAct::s2 }; }
        if (!input.in_melee) {
            if (input.dist <= aip[1]) return walk;
            return roll() < 30 ? think_wander(rng, 3) : Think{ MonAct::idle, 10 };
        }
        if (*input.state != 0 || roll() < aip[2]) { *input.state = 0; return a1_or_a2(aip[3]); }
        return roll() < 30 ? Think{ MonAct::s2 } : Think{ MonAct::idle, 10 };
    }
    // FallenShaman (13, FUN_005f1440): in melee aip3 % A1; aip1 % sets its
    // group charging; with a corpse (FUN_005dd0b0) aip1 % raises it
    // (Skill1); within aip5, aip2 % a fire bolt (Skill2), rolled again for
    // its second target search (FUN_005ddc30); else aip3 % circles 3, else
    // stands 10.
    // ponytail: the second target search taken as its target.
    if (ai_name == "FallenShaman") {
        if (input.in_melee && roll() < aip[2]) return { MonAct::a1 };
        if (roll() < aip[0]) *input.rally = true;
        if (input.corpse && roll() < aip[0] && input.skill[0]) return { MonAct::skill, 0 };
        for (int search = 0; search < 2; ++search)
            if (input.dist < aip[4] && roll() < aip[1]) return { MonAct::skill, 1 };
        if (roll() >= aip[2]) return { MonAct::idle, 10 };
        return think_circle(rng, 3);
    }
    // FoulCrowNest (43, FUN_005f6650; init FUN_005f6630 keeps the frame in
    // the state): past 20 stands 25; once it's laid aip3 it collapses;
    // every aip1 frames it lays (Skill1, Nest) where there's room; else
    // stands 20 + a seed step % 10.
    if (ai_name == "FoulCrowNest") {
        if (input.dist > 20) return { MonAct::idle, 25 };
        if (*input.state2 >= aip[2]) return { MonAct::die };
        if (input.skill[0] && std::abs(input.frame - *input.state) >= aip[0]) {
            *input.state = input.frame;
            if (input.spot_free) { ++*input.state2; return { MonAct::skill, 0 }; }
        }
        return { MonAct::idle, int(rng.next() % 10) + 20 };
    }
    // BloodRaven (59, FUN_005e6320; init FUN_005e6300 clears the flag):
    // past 45 stands 5; 50 or more from home (her command 10, set at her
    // first think) she heads back until within 5 (the flag: *command);
    // past 20 she closes to a spot half as far off (12 at least); every
    // think adds 3 to the state, and out of melee, with fewer than 8 + 2 *
    // difficulty raised (state2), rand(100) < state raises a zombie (Nest)
    // 5..19 off the target; past 5, 5 % moves 12 about the target, else
    // not hit 80 % strikes ((difficulty + 4) * 10 % Quick Strike, else A1),
    // else circles 4; within 5, 30 % backs off to 12 running, else A1.
    // ponytail: the walks home / about the target and the circle always
    // set off; the second target search (FUN_005ddc30) is the target.
    if (ai_name == "BloodRaven") {
        if (input.dist > 45) return { MonAct::idle, 5 };
        if (input.home_dist >= 50) *input.command = 1;
        if (*input.command && input.home_dist > 5) return { MonAct::home };
        *input.command = 0;
        auto around = [&](int radius) { auto out = think_wander(rng, radius); out.act = MonAct::around; return out; };
        if (input.dist > 20) return around(std::max(input.dist >> 1, 12));
        *input.state += 3;
        if (input.skill[0] && !input.in_melee && *input.state2 < input.difficulty * 2 + 8 && roll() < *input.state) {
            const int reach = rng(15) + 5;
            int x = reach, y = reach;
            if (rng.next() & 1) y = rng(reach); else x = rng(reach);
            if (rng.next() & 1) x = -x;
            if (rng.next() & 1) y = -y;
            ++*input.state2; *input.state = 0;
            return { MonAct::skill, 0, x, y };
        }
        if (input.dist > 5) {
            if (roll() < 5) return around(12);
            if (!input.got_hit && roll() < 80) return input.skill[1] && roll() < (input.difficulty + 4) * 10 ? Think{ MonAct::skill, 1 } : Think{ MonAct::a1 };
            return think_circle(rng, 4);             // FUN_005df7d0(4, 1): its walk flagged 4
        }
        if (roll() < 30 && away(12 - input.dist, true)) return { MonAct::none };
        return { MonAct::a1 };
    }
    // SkeletonMage (FUN_005f96c0): with a second target (FUN_005ddc30) by
    // its distance: past aip2, aip3 % closes to aip2 (pace 10); within
    // aip4, aip5 % backs off 5 from it (pace 25; blocked: A1 at the
    // target); within aip6, aip1 % shoots it (A1, MissA1). Then, the
    // distance 0x7fffffff with none, past aip2 aip3 % closes in on the
    // target; else aip7 % circles 4, else stands aip8.
    if (ai_name == "SkeletonMage") {
        const int dist = input.second ? input.second_dist : 0x7fffffff;
        if (input.second) {
            if (aip[1] < dist && roll() < aip[2]) { *input.pace = 10; return at_second({ MonAct::approach, aip[1] }); }
            if (dist <= aip[3] && roll() < aip[4]) { *input.pace = 25; return away_second(5, false) ? Think{ MonAct::none } : Think{ MonAct::a1 }; }
            if (dist < aip[5] && roll() < aip[0]) return at_second({ MonAct::a1 });
        }
        if (aip[1] < dist && roll() < aip[2]) { *input.pace = 10; return { MonAct::approach, aip[1] }; }
        if (roll() >= aip[6]) return { MonAct::idle, aip[7] };
        return think_circle(rng, 4);
    }
    // GargoyleTrap (FUN_005f9490): after a shot it stands its scratch word
    // (aip3) frames; else, facing the target square on, within 5 subtiles
    // of its row or column and nearer than aip1, aip2 % shoots Skill1;
    // else it stands aip4.
    if (ai_name == "GargoyleTrap") {
        if (*input.state > 0) { const int frames = *input.state; *input.state = 0; return { MonAct::idle, frames }; }
        if ((std::abs(input.off_x) <= 5 || std::abs(input.off_y) <= 5) && input.skill[0] && input.dist < aip[0] && roll() < aip[1]) {
            *input.state = aip[2];
            return { MonAct::skill, 0 };
        }
        return { MonAct::idle, aip[3] };
    }
    // Arach (26, FUN_005f4510): hurt (state 1), over 75 % life it's done:
    // aip3 % charges (state 2, walk 0), else circles 6; else in melee aip1
    // - 25 % A1; aip4 or more off, not hit, circles 12; else backs off 4.
    // Out of melee: hit or charging (state2) it charges; every 21st think
    // (state3) aip3 % a charge; else 20 % wanders 6, else stands 15. In
    // melee aip1 % A1; at aip5 % life or more aip2 % circles 4, else
    // stands 15; under, hurt: it lays (Skill1, Sk1mode, at no unit) out of
    // Spider Lay's state, else backs off 8. A back-off (FUN_005defe0 flags
    // 0) that can't set off thinks aidel on.
    if (ai_name == "Arach") {
        const Think charge{ MonAct::walk, 0 }, stand{ MonAct::idle, 15 }, stuck{ MonAct::idle, input.aidel };
        if (*input.state == 1) {
            *input.state2 = 0;
            if (input.life_pct > 75) {
                *input.state = 0;
                if (roll() < aip[2]) { *input.state = 2; return charge; }
                return think_circle(rng, 6);
            }
            if (input.in_melee && aip[0] > 25 && roll() < aip[0] - 25) return { MonAct::a1 };
            if (aip[3] <= input.dist && !input.got_hit) { *input.state = 0; return think_circle(rng, 12); }
            return away(4, false) ? Think{ MonAct::none } : stuck;
        }
        if (!input.in_melee) {
            if (input.got_hit || *input.state2 == 1) { *input.state2 = 1; return charge; }
            if (++*input.state3 > 20) *input.state3 = 0;
            *input.state2 = 0;
            if (*input.state3 == 1 && roll() < aip[2]) { *input.state2 = 1; return charge; }
            return roll() < 20 ? think_wander(rng, 6) : stand;
        }
        *input.state = 2;
        if (roll() < aip[0]) return { MonAct::a1 };
        if (aip[4] <= input.life_pct) return roll() < aip[1] ? think_circle(rng, 4) : stand;
        *input.state = 1;
        if (input.skill[0] && !input.laying) return { MonAct::skill, 0 };
        return away(8, false) ? Think{ MonAct::none } : stuck;
    }
    // Vampire (28, FUN_005f4a70): aip5 bit 0 lets it shoot (Skill1 or
    // Skill4, even odds, at its second target within 20), bit 1 cast a
    // firewall (Skill2), bit 2 a meteor (Skill3), aip4 % each, 11 thinks
    // apart (state3). Hit, it's on (state 1), keeping the hitter's distance
    // under 30 (state2); in melee 30 % with bit 0 a shot, else A1. Fleeing
    // (state 2): over 74 % life it walks in; under 14 or within state2 it
    // backs off 8, the pace Run / Velocity (0..120 % on); past aip3, or
    // past aip2 %, it stands 15; else a cast, a shot, or circles 4. Else
    // under 33 % life it flees, backing off 8; in melee aip1 % A1 (bit 0:
    // 70 %, else a shot), else 33 % circles 4, else stands 10; aip3 or more
    // off it walks in on, else stands 15; then aip2 % a cast or a shot (25
    // % circles 4 first) or walks in; else past 20 walks in, under 9 50 %
    // backs off 8, else 50 % circles 4, else stands 10. A shot but the
    // hit one in melee (at the target) needs a second target (FUN_005ddc30)
    // and goes at it, by its distance; a back-off's pace stays when blocked.
    if (ai_name == "Vampire") {
        const bool shoots = aip[4] & 1, at_hand = shoots && input.second;
        const int second_dist = input.second ? input.second_dist : 0x7fffffff;
        auto shot = [&](ThinkUnit unit) { Think out{ MonAct::skill, roll() < 50 ? 0 : 3 }; out.unit = unit; return out; };
        auto cast = [&] {                                   // FUN_005dead0(Sk2mode / Sk3mode, at the target)
            for (const int skill : { 1, 2 })
                if ((aip[4] & (skill << 1)) && *input.state3 < 1 && roll() < aip[3]) { *input.state3 = 11; return skill; }
            return 0;
        };
        if (*input.state3 > 0) --*input.state3;
        if (input.got_hit) {
            if (*input.state == 0) *input.state = 1;
            if (input.dist < 30 && *input.state2 < input.dist) *input.state2 = input.dist;
            if (input.in_melee) return roll() > 30 || !shoots ? Think{ MonAct::a1 } : shot(ThinkUnit::target);
        }
        if (*input.state == 2) {
            if (input.life_pct > 74) { *input.state = 1; return walk; }
            if (input.dist < 14 || input.dist <= *input.state2) {
                *input.pace = input.velocity > 0 ? std::clamp(input.run * 100 / input.velocity - 100, 0, 120) : 0;
                if (away(8, false)) return { MonAct::none };       // flags 4: no aidel think when it can't
            }
            if (aip[2] <= input.dist || roll() >= aip[1]) return { MonAct::idle, 15 };
            if (const int skill = cast()) return { MonAct::skill, skill };
            return !at_hand || second_dist > 20 ? think_circle(rng, 4) : shot(ThinkUnit::second);
        }
        if (input.life_pct < 33) {
            *input.state = 2;
            if (away(8, false)) return { MonAct::none };
        }
        if (input.in_melee) {
            *input.state = 1;
            if (roll() < aip[0]) {
                if (!at_hand || roll() > 30) return { MonAct::a1 };
                if (second_dist < 21) return shot(ThinkUnit::second);
            }
            return roll() < 33 ? think_circle(rng, 4) : Think{ MonAct::idle, 10 };
        }
        if (aip[2] <= input.dist) return *input.state != 1 ? Think{ MonAct::idle, 15 } : walk;
        *input.state = 1;
        if (roll() >= aip[1]) {
            if (input.dist > 20) return walk;
            if (input.dist < 9 && roll() < 50) return away(8, false) ? Think{ MonAct::none } : Think{ MonAct::idle, input.aidel };
            return roll() < 50 ? think_circle(rng, 4) : Think{ MonAct::idle, 10 };
        }
        if (const int skill = cast()) return { MonAct::skill, skill };
        if (!at_hand || second_dist > 20) return walk;
        return roll() > 74 ? think_circle(rng, 4) : shot(ThinkUnit::second);
    }
    // Fetish (30, FUN_005f53e0): at rest (state 0) in melee it squares up
    // (state 1); squared up, after aip3 thinks (state2) with its target
    // over aip4 % life it backs off 14 (state 2); in melee aip1 % A1, else
    // stands aip2. Backing off: past 12, after two thinks it rests, 20 %
    // circling 4, else standing 10; within, it backs off again, or rests
    // and stands 10. Else it walks in. Its walks and back-offs go at pace
    // 50; a back-off (flags 4) that can't set off thinks again at once.
    // ponytail: its leader's commands (FUN_0058ee80: 1 and 0xe walk it at
    // a unit) never come, none of Act 1's thinks sending them; the pathers
    // (2, 0xd) as path_to's.
    if (ai_name == "Fetish") {
        auto swing = [&] { return roll() >= aip[0] ? Think{ MonAct::idle, aip[1] } : Think{ MonAct::a1 }; };
        if (*input.state == 0) {
            if (input.in_melee) { *input.state2 = 0; *input.state = 1; return swing(); }
        } else if (*input.state == 1) {
            if (aip[2] < ++*input.state2 && aip[3] < input.target_life_pct) {
                *input.state = 2; *input.state2 = 0;
                *input.pace = 50;
                if (!away(14, false)) *input.pace = 0;
                return { MonAct::none };
            }
            if (input.in_melee) return swing();
        } else {
            if (*input.state == 2) {
                if (input.dist > 12) {
                    if (++*input.state2 > 1) *input.state = *input.state2 = 0;
                    return roll() < 20 ? think_circle(rng, 4) : Think{ MonAct::idle, 10 };
                }
                *input.pace = 50;
                if (away(14, false)) return { MonAct::none };
                *input.pace = 0;
                *input.state = *input.state2 = 0;
            }
            return { MonAct::idle, 10 };
        }
        *input.pace = 50;
        return walk;
    }
    // Griswold (90, FUN_005e5ac0): in melee 80 % A1, else 50 % walks in;
    // else stands 10.
    if (ai_name == "Griswold") {
        if (input.in_melee) { if (roll() < 80) return { MonAct::a1 }; }
        else if (roll() < 50) return walk;
        return { MonAct::idle, 10 };
    }
    // Smith (98, FUN_005e3890): A1 in melee, else walks in, the faster the
    // more he's hurt (pace: half the life % he's lost).
    if (ai_name == "Smith") {
        if (input.in_melee) return { MonAct::a1 };
        *input.pace = (100 - std::clamp(input.life_pct, 0, 100)) >> 1;
        return walk;
    }
    // Act 2's (docs/research/re/monster-ai.md "Act 2's thinks"; tools/emu/mon_ais.py).
    auto effect = [&](ThinkEffect::Kind kind, int id = 0, int value = 0) { if (input.effects) input.effects->push_back({ kind, id, value }); };
    auto set_pace = [&](int pct, int steps) { if (pct) *input.pace = pct; if (steps && input.steps) *input.steps = steps; };
    auto skill_set = [&](int slot) { return slot < 3 ? input.skill[std::size_t(slot)] : input.skill45[std::size_t(slot - 3)]; };
    auto cast = [&](int slot, ThinkUnit unit = ThinkUnit::target) { Think out{ MonAct::skill, slot }; out.unit = unit; return out; };
    auto aimed = [](Think out, ThinkUnit unit) { out.unit = unit; return out; };
    const Think walk0{ MonAct::walk, 0 };
    // Duriel (44, FUN_005f67b0): his aura (Skill4) on if it's off; out of
    // melee aip5 % Skill1 (his charge), else walks in; in melee aip2 %
    // Skill3, aip3 % Skill2, else aip4 % A2, else A1.
    if (ai_name == "Duriel") {
        if (skill_set(3) && !input.aura_on) effect(ThinkEffect::aura, 3);
        if (!input.in_melee) {
            if (input.skill[0] && roll() < aip[4]) return cast(0);
            return walk;
        }
        if (input.skill[2] && roll() < aip[1]) return cast(2);
        if (input.skill[1] && roll() < aip[2]) return cast(1);
        return roll() < aip[3] ? Think{ MonAct::a2 } : Think{ MonAct::a1 };
    }
    // Swarm (19, FUN_005f2460): out of melee aip1 % walks in; in melee aip3
    // % A1; else stands aip2.
    if (ai_name == "Swarm") {
        if (!input.in_melee) { if (roll() < aip[0]) return walk; }
        else if (roll() < aip[2]) return { MonAct::a1 };
        return idle2;
    }
    // PinHead (39, FUN_005f6340): out of melee (state 0) aip3 % walks in,
    // else stands aip4; in melee, after its first swing (state 1) aip1 %
    // goes on, else stands aip2; then aip5 % Skill1, aip6 % Skill2, else A1.
    if (ai_name == "PinHead") {
        if (!input.in_melee) {
            *input.state = 0;
            return roll() >= aip[2] ? Think{ MonAct::idle, aip[3] } : walk;
        }
        if (*input.state != 0 && roll() >= aip[0]) return idle2;
        *input.state = 1;
        if (input.skill[0] && roll() < aip[4]) return cast(0);
        if (input.skill[1] && roll() < aip[5]) return cast(1);
        return { MonAct::a1 };
    }
    // Mummy (21, FUN_005f2850): hit out of melee it walks in; within aip1,
    // in melee aip3 % swings (aip4 % A1), else walks in (the stand aip5 it
    // asks first is overtaken); past aip1 aip2 % wanders 3, else stands aip5.
    if (ai_name == "Mummy") {
        if (input.got_hit && !input.in_melee) return walk;
        if (input.dist <= aip[0]) {
            if (input.in_melee && roll() < aip[2]) return a1_or_a2(aip[3]);
            return walk;
        }
        if (roll() < aip[1]) return think_wander(rng, 3);
        return { MonAct::idle, aip[4] };
    }
    // SandLeaper (17, FUN_005f20a0): within 5, aip1 % its leap (Skill1) if
    // it can; out of melee past 10 it bounds about the target 5 (pace 75),
    // else aip3 % walks in, aip4 % circles 4; in melee aip2 % A2; else
    // stands 10.
    if (ai_name == "SandLeaper") {
        if (input.skill[0] && input.dist < 5 && roll() < aip[0] && input.usable) return cast(0);
        if (!input.in_melee) {
            if (input.dist > 10) { set_pace(75, 0); auto out = think_wander(rng, 5); out.act = MonAct::around; return out; }
            if (roll() < aip[2]) return walk;
            if (roll() < aip[3]) return think_circle(rng, 4);
        } else if (roll() < aip[1]) return { MonAct::a2 };
        return { MonAct::idle, 10 };
    }
    // ClawViper (16, FUN_005f1b60): after its charge (state) its aip6 state
    // goes off (2: 0x5b, else 0x5a); out of melee within aip2, aip1 % the
    // charge (Skill1) if it can, its state on; else 50 % walks in, else
    // stands aip5; in melee aip3 % swings (aip4 % A1), else stands aip5.
    if (ai_name == "ClawViper") {
        const int state_id = aip[5] == 2 ? 0x5b : 0x5a;
        if (*input.state != 0 && aip[5] != 0) effect(ThinkEffect::state, state_id, 0);
        const Think stand{ MonAct::idle, aip[4] };
        if (!input.in_melee) {
            if (input.skill[0] && input.dist < aip[1] && roll() < aip[0] && input.usable) {
                if (aip[5] != 0) effect(ThinkEffect::state, state_id, 1);
                *input.state = 1;
                return cast(0);
            }
            return roll() < 50 ? walk : stand;
        }
        if (roll() >= aip[2]) return stand;
        return a1_or_a2(aip[3]);
    }
    // PantherWoman (18, FUN_005f22b0): in melee aip2 % A1, else stands
    // aip4; aip1 % runs in (pace 75); its nearest kin past aip3 it walks to
    // (pace 75); 25 % circles 3; else stands aip4.
    if (ai_name == "PantherWoman") {
        const Think stand{ MonAct::idle, aip[3] };
        if (input.in_melee) return roll() < aip[1] ? Think{ MonAct::a1 } : stand;
        if (roll() < aip[0]) { set_pace(75, 0); return { MonAct::run, 7 }; }
        if (input.kin && aip[2] * aip[2] < input.kin_dist) { set_pace(75, 0); return aimed(walk, ThinkUnit::kin); }
        if (roll() < 25) return think_circle(rng, 3);
        return stand;
    }
    // PantherJavelin (95, FUN_005e1080): at its second target's distance
    // (none: the most there is): under 8 aip4 % backs off 16; past aip6 - 6
    // aip1 % moves 4 about the target; with none or at aip6 or more it walks
    // to its kin past aip3, else stands aip5; else aip2 % throws (A1) at it.
    if (ai_name == "PantherJavelin") {
        const int reach = input.second ? input.second_dist : 0x7fffffff;
        if (reach < 8 && roll() < aip[3]) { (void)away(16, false); return { MonAct::none }; }
        if (aip[5] - 6 < reach && roll() < aip[0]) { auto out = think_wander(rng, 4); out.act = MonAct::around; return out; }
        if (!input.second || aip[5] <= reach) {
            if (input.kin && aip[2] < input.kin_dist) return aimed(walk, ThinkUnit::kin);
        } else if (roll() < aip[1]) return aimed({ MonAct::a1 }, ThinkUnit::second);
        return { MonAct::idle, aip[4] };
    }
    // Scarab (20, FUN_005f2540): its leader within 19, itself leading, aip5
    // % sends the group charging (and charges). Charging (command 1): in
    // melee Skill1 (the command done), else walks in (pace 100). Else out of
    // melee once it's circled (state) it walks in (4 steps), 90 % done
    // circling; else circles 0. In melee aip1 % on: aip4 % Skill1, aip2 %
    // A1, else A2; else stands aip3.
    if (ai_name == "Scarab") {
        int command = *input.command;
        if (command == 0 && input.dist <= 19 && input.leader && roll() < aip[4]) { *input.rally = true; effect(ThinkEffect::rally); command = 1; }
        if (command == 1) {
            if (input.in_melee && input.skill[0]) { effect(ThinkEffect::command_done); return cast(0); }
            set_pace(100, 0);
            return walk0;
        }
        if (!input.in_melee) {
            if (*input.state != 0) {
                set_pace(0, 4);
                if (roll() > 10) *input.state = 0;
                return walk;
            }
            *input.state = 1;
            return think_circle(rng, 0);
        }
        if (roll() >= aip[0]) return { MonAct::idle, aip[2] };
        if (input.skill[0] && roll() < aip[3]) return cast(0);
        return roll() >= aip[1] ? Think{ MonAct::a2 } : Think{ MonAct::a1 };
    }
    // BatDemon (29, FUN_005f5040): state 0 / 4: it rises (S3, wait 8),
    // state 1, its regen up aip5 / 8 (state3); state 1: after two drops
    // (state2) it lands (S2, wait 6; state 3, the regen back) unless out of
    // melee past 6, not hit, past 13 or under 51 % life, when it drops
    // again (S4, wait 10 then 15); state 2 (fleeing): under aip1 % life it
    // backs off 15 (state 4), 33 % walks in (state 3), 15 % wanders 6, else
    // stands 10; state 3: the same back-off; out of melee walks in; dropped,
    // A2; hit aip2 % backs off 12 (state 2); aip3 %: aip4 % A2, else A1;
    // else stands 10; other states within 15 walk in (state 3), else state 4
    // and stand 15.
    // Its other think: other_think.
    if (ai_name == "BatDemon") {
        const int life = input.life_pct;
        auto mode = [&](MonAct act, int wait) { Think out{ act }; out.unit = ThinkUnit::none; out.wait = wait; return out; };
        if (*input.state == 0 || *input.state == 4) {
            *input.state = 1; *input.state2 = 0;
            if (input.regen != 0) {
                *input.state3 = aip[4] * input.regen / 8;
                effect(ThinkEffect::stat, 0x4a, *input.state3 + input.regen);
            } else *input.state3 = 0;
            return mode(MonAct::s3, 8);
        }
        if (*input.state == 1) {
            if (*input.state2 > 1 && !(!input.in_melee && input.dist > 6 && !input.got_hit && (input.dist > 13 || life < 51))) {
                if (*input.state3 != 0) effect(ThinkEffect::stat, 0x4a, input.regen - *input.state3);
                *input.state = 3;
                return mode(MonAct::s2, 6);
            }
            const int wait = *input.state2 == 0 ? 10 : 15;
            ++*input.state2;
            return mode(MonAct::s4, wait);
        }
        if (*input.state == 2) {
            if (life < aip[0] && away(15, false)) { *input.state = 4; return { MonAct::none }; }
            if (roll() < 33) { *input.state = 3; return walk0; }
            if (roll() < 15) return think_wander(rng, 6);
            return { MonAct::idle, 10 };
        }
        if (*input.state == 3) {
            if (life < aip[0] && away(15, false)) { *input.state = 4; return { MonAct::none }; }
            if (!input.in_melee) return walk0;
            if (*input.state2 > 0) { *input.state2 = 0; return { MonAct::a2 }; }
            if (input.got_hit && roll() < aip[1] && away(12, false)) { *input.state = 2; return { MonAct::none }; }
            if (roll() < aip[2]) return roll() < aip[3] ? Think{ MonAct::a2 } : Think{ MonAct::a1 };
            return { MonAct::idle, 10 };
        }
        if (input.dist < 15) { *input.state = 3; return walk0; }
        *input.state = 4;
        return { MonAct::idle, 15 };
    }
    // Baboon (11, FUN_005f0cd0): its pace Run / Velocity (0..120 % on). At
    // rest (state 0): out of melee it runs in; hit under aip1 % life, 50 %
    // it flees rand(5) + 2 thinks (state), its regen up aip5 / 8 (state3),
    // backing off 15; hit after a swing (state2) 20 % circles 3; after a
    // swing aip3 % on, else aip2 % circles 3 and stands 15; else aip4 % A1,
    // else A2 (state2 1). Fleeing: a think off (state2 0); done or over 75 %
    // life its regen back; out of melee over 75 % it's done and runs in; in
    // melee 33 % swings (aip4 % A1); past 23 not hit 33 % circles 4 and it
    // stands 20; else backs off 15, or out of melee wanders 5, in it swings.
    if (ai_name == "Baboon") {
        const int life = input.life_pct;
        int pct = input.velocity < 1 ? 0 : input.run * 100 / input.velocity;
        pct = pct < 100 ? 0 : std::min(pct - 100, 120);
        auto swing = [&] { return roll() < aip[3] ? Think{ MonAct::a1 } : Think{ MonAct::a2 }; };
        if (*input.state == 0) {
            if (!input.in_melee) return { MonAct::run, 7 };
            if (input.got_hit) {
                if (life < aip[0] && roll() < 50) {
                    *input.state = rng(5) + 2;
                    if (input.regen != 0) {
                        *input.state3 = aip[4] * input.regen / 8;
                        effect(ThinkEffect::stat, 0x4a, *input.state3 + input.regen);
                    } else *input.state3 = 0;
                    set_pace(pct, 0);
                    (void)away(15, false);
                    return { MonAct::none };
                }
                if (*input.state2 != 0 && roll() < 20) { *input.state2 = 0; return think_circle(rng, 3); }
            }
            if (*input.state2 != 0 && roll() >= aip[2]) {
                if (roll() < aip[1]) { (void)think_circle(rng, 3); *input.state2 = 0; }
                return { MonAct::idle, 15 };
            }
            *input.state2 = 1;
            return swing();
        }
        --*input.state;
        *input.state2 = 0;
        if (*input.state == 0 || life > 75) effect(ThinkEffect::stat, 0x4a, input.regen - *input.state3);
        if (!input.in_melee) {
            if (life > 75) { *input.state = 0; set_pace(pct, 0); return { MonAct::run, 7 }; }
        } else if (roll() < 33) return swing();
        if (input.dist > 23 && !input.got_hit) {
            if (roll() < 33) (void)think_circle(rng, 4);
            return { MonAct::idle, 20 };
        }
        set_pace(pct, 0);
        if (away(15, false)) return { MonAct::none };
        if (!input.in_melee) return think_wander(rng, 5);
        return swing();
    }
    // SandRaider (8, FUN_005f0700): its thinks count up (state; at 0 its
    // states 0x5a / 0x5b off, state2 0); at aip5 it shows its overlay
    // (aip6 1: 0x96, else 0x2e) and stands aidel + 1; past it, its state on
    // (aip6 1: 0x5a, else 0x5b; state2 1). Under aip1 % life, seven times
    // (state3), it walks to its nearest other monster. Out of melee past 4,
    // not yet on, aip2 % circles 0; out of melee it walks in (not on: aip4
    // %); in melee, on, Skill1 (the count starts over); aip3 % swings (aip7
    // or more A1, else A2); else stands 15, the count over past aip5 +
    // max(24 - aip5, 6).
    if (ai_name == "SandRaider") {
        if (*input.state == 0) { effect(ThinkEffect::state, 0x5a, 0); effect(ThinkEffect::state, 0x5b, 0); *input.state2 = 0; }
        ++*input.state;
        if (*input.state == aip[4]) {
            effect(ThinkEffect::overlay, aip[5] == 1 ? 0x96 : 0x2e);
            return { MonAct::idle, input.aidel + 1 };
        }
        if (aip[4] < *input.state) { effect(ThinkEffect::state, aip[5] == 1 ? 0x5a : 0x5b, 1); *input.state2 = 1; }
        if (*input.state3 < 7 && input.life_pct < aip[0]) {
            if (input.kin) return aimed(walk0, ThinkUnit::kin);
            ++*input.state3;
        }
        auto rest = [&] {
            const int count = std::max(24 - aip[4], 6);
            if (aip[4] + count < *input.state) *input.state = *input.state2 = 0;
            return Think{ MonAct::idle, 15 };
        };
        if (input.dist > 4 && *input.state2 == 0 && roll() < aip[1]) return think_circle(rng, 0);
        if (!input.in_melee) {
            if (*input.state2 == 0 && roll() >= aip[3]) return rest();
            return walk0;
        }
        if (*input.state2 == 1 && input.skill[0]) { *input.state = *input.state2 = 0; return cast(0); }
        if (roll() < aip[2]) return rng(100) >= aip[6] ? Think{ MonAct::a1 } : Think{ MonAct::a2 };
        return rest();
    }
    // GreaterMummy (22, FUN_005f2b10): in melee aip1 % A1; within 5 aip1 %
    // A2; aip3 % heals a hurt one in reach (Skill2); aip2 % raises a corpse
    // (Skill1's sequence) if it can; aip4 % Skill3 at its second target;
    // with none in reach it closes in (pace 50, 3); else 50 % circles 3,
    // else stands 6.
    // Radament's wider reach (aip5 + 10) and the corpse / hurt pick are
    // the search's (ai.cpp).
    if (ai_name == "GreaterMummy") {
        if (input.in_melee && roll() < aip[0]) return { MonAct::a1 };
        if (input.dist < 5 && roll() < aip[0]) return { MonAct::a2 };
        if (input.skill[1] && input.aid && roll() < aip[2]) return cast(1, ThinkUnit::aid);
        if (input.corpse && roll() < aip[1] && input.usable) return aimed({ MonAct::seq, 0 }, ThinkUnit::kin);
        if (input.skill[2] && roll() < aip[3] && input.second) return cast(2, ThinkUnit::second);
        if (input.in_reach < 1) { set_pace(50, 0); return { MonAct::approach, 3 }; }
        if (roll() > 49) return { MonAct::idle, 6 };
        return think_circle(rng, 3);
    }
    // SandMaggot (15, search type 4; FUN_005f1800): state 3 (buried, its
    // skill ready: state2 the frame) it lays (Skill1, event 25; ready aip5
    // on; state 1), else waits 20. Under 25 % life with its second target
    // within 7, ready, 20 % it burrows (Skill2, event 30; state 3). In
    // melee aip4 % A1; its second target within 15 aip2 % A2 at it; 20 %
    // circles 6; under aip3 eggs laid (state3) aip1 %: not circled (state
    // 2) it circles 6 (state 2), else Skill3 (an egg, event 20; state 1);
    // else waits 12.
    // Its other think: other_think.
    if (ai_name == "SandMaggot") {
        const bool ready = *input.state2 < input.frame;
        const int second_dist = input.second ? input.second_dist : 0x7fffffff;
        auto with_event = [](Think out, int frames) { out.event = frames; return out; };
        if (*input.state == 3) {
            if (ready && input.skill[0]) { *input.state2 = aip[4] + input.frame; *input.state = 1; return with_event(cast(0), 25); }
            return with_event({ MonAct::none }, 20);
        }
        if (input.life_pct < 25 && input.skill[1] && second_dist < 7 && ready && rng(100) < 20) {
            *input.state2 = aip[4] + input.frame; *input.state = 3;
            return with_event(cast(1), 30);
        }
        if (input.in_melee && roll() < aip[3]) return { MonAct::a1 };
        if (input.second && second_dist < 15 && rng(100) < aip[1]) return aimed({ MonAct::a2 }, ThinkUnit::second);
        if (roll() < 20) return think_circle(rng, 6);
        if (*input.state3 < aip[2] && rng(100) < aip[0]) {
            if (*input.state != 2 || !input.skill[2]) { *input.state = 2; return think_circle(rng, 6); }
            ++*input.state3; *input.state = 1;
            return with_event(cast(2), 20);
        }
        return with_event({ MonAct::none }, 12);
    }
    // Summoner (53, FUN_005f85c0): its first think opens his quest (state
    // 1). Within 5 aip6 % he backs off 6. aip1 % he casts, else wanders 4:
    // aip2 % Skill5; the element the target resists less (cold when its
    // cold resist is no more than its fire), turned about past aip3 %:
    // Skill2 (aip7 reach, ready at state2: aip4 frames on) or Skill1 at his
    // second target (aip8 reach); Skill4 every aip5 frames (state3); Skill3
    // at the second (aip8); the other element's Skill2; else Skill5. Each
    // at the target's spot.
    if (ai_name == "Summoner") {
        if (*input.state == 0) { effect(ThinkEffect::summoner_open); *input.state = 1; }
        if (input.dist < 5 && roll() < aip[5]) (void)away(6, false);
        bool cold = input.target_cold_res <= input.target_fire_res;
        auto spot = [&](int slot, ThinkUnit unit = ThinkUnit::target) { auto out = cast(slot, unit); out.spot = true; return out; };
        if (roll() < aip[0]) {
            if (skill_set(4) && roll() < aip[1]) return spot(4);
            if (aip[2] < roll()) cold = !cold;
            auto bolt = [&] { *input.state2 = aip[3] + input.frame; return spot(1); };
            const bool bolt_ready = input.skill[1] && *input.state2 < input.frame && input.dist < aip[6];
            if (cold) {
                if (bolt_ready) return bolt();
                if (input.skill[0] && input.second && input.dist < aip[7]) return spot(0, ThinkUnit::second);
            }
            if (skill_set(3) && *input.state3 < input.frame) { *input.state3 = aip[4] + input.frame; return spot(3); }
            if (input.skill[2] && input.second && input.dist < aip[7]) return spot(2, ThinkUnit::second);
            if (!cold && bolt_ready) return bolt();
            if (skill_set(4)) return spot(4);
        }
        return think_wander(rng, 4);
    }
    // Vulture (23, FUN_005f3170; state: its thinks aloft, -1 landed). Far
    // (over 12) in another room: down, it keeps 9 off (2 a go); aloft it
    // lands (S2, 2 a go to 3 off; -1). Down, not in a group, far, 60 % it
    // takes off for rand(8) + 24 thinks (S1 8 to 8 off). Aloft (2 or more)
    // with no weak one about, past 5 or 85 %: it flies (S1) to a point
    // rand(2r) about the target (r = thinks + 8) pushed off itself until
    // it's clamp(2r, 12, 36) away, kept in state2 / 3 (then re-placed on a
    // free spot), a think less; else (or 1 left) it lands to 3 off (-1),
    // or can't (8 more) and fights on. In melee aip1 % A1, else waits
    // aip2; landed or aip5 %: aip4 % circles 6, else keeps 9 off; down.
    // Each waits 12.
    if (ai_name == "Vulture") {
        auto wait = [](Think out, int frames) { out.wait = frames; return out; };
        auto keep = [&](ThinkUnit unit, int mode, int most, int want) { Think out{ MonAct::keep, most, want, mode }; out.unit = unit; return out; };
        auto land = [&] { if (!input.can_land) return false; effect(ThinkEffect::land); return true; };
        const int off_x = input.self_x - input.target_x, off_y = input.self_y - input.target_y;
        const bool distant = off_x * off_x + off_y * off_y > 0x90;
        int& aloft = *input.state;
        if (!input.same_room && distant) {
            if (aloft < 1) return keep(ThinkUnit::target, 2, 9, 0);
            if (land()) { aloft = -1; return wait(keep(ThinkUnit::self, 9, 2, 3), 12); }
        }
        if (aloft == 0 && !input.grouped && distant && rng(100) < 60) {
            effect(ThinkEffect::take_off);
            aloft = int(rng.next() & 7) + 24;
            return wait(keep(ThinkUnit::target, 8, 8, 8), 12);
        }
        Think fallback{ MonAct::none };
        bool landing = aloft == 1;
        if (aloft >= 2) {
            if (!input.victim && (input.dist > 5 || rng(100) > 14)) {
                effect(ThinkEffect::take_off);
                auto gap = [&](int x, int y) { return npc_distance(input.self_x, input.self_y, x, y); };
                int& spot_x = *input.state2;
                int& spot_y = *input.state3;
                if (spot_x == 0 || spot_y == 0 || gap(spot_x, spot_y) < 2) {
                    const int reach = aloft + 8;
                    int x = rng(reach * 2) + input.target_x - reach;
                    int y = rng(reach * 2) + input.target_y - reach;
                    const int need = std::clamp(reach * 2, 12, 36);
                    while (gap(x, y) < need) {
                        x += (x > input.self_x) - (x < input.self_x);
                        y += (y > input.self_y) - (y < input.self_y);
                        if (x == input.self_x && y == input.self_y) { ++x; ++y; }
                    }
                    spot_x = x; spot_y = y;
                } else if (input.free_spot[0]) std::tie(spot_x, spot_y) = *input.free_spot[0];
                else if (input.free_spot[1]) std::tie(spot_x, spot_y) = *input.free_spot[1];
                --aloft;
                Think fly{ MonAct::fly, 8, spot_x, spot_y };
                fly.unit = ThinkUnit::none;
                return wait(fly, 12);
            }
            aloft = 1;
            landing = true;
        }
        if (landing) {
            if (land()) { aloft = -1; return wait(keep(ThinkUnit::target, 9, 2, 3), 12); }
            aloft = 8;
            fallback.wait = 12;
        }
        if (input.in_melee) return roll() >= aip[0] ? wait(fallback, aip[1]) : wait({ MonAct::a1 }, fallback.wait);
        if (aloft != -1 && rng(100) >= aip[4]) return wait(fallback, aip[1]);
        const Think out = rng(100) < aip[3] ? think_circle(rng, 6) : keep(ThinkUnit::target, 2, 9, 0);
        aloft = 0;
        return wait(out, 12);
    }
    // SandMaggotQueen (66, FUN_005f9cf0; Coldworm): rested (state3) she
    // stands aip2 x 25; under aip1 young made (state) she plays S1 where she
    // stands (FUN_005de440, mode 8 at her own spot; state2 1), then her young
    // comes up (spawn 1: ai.cpp, counted there), each a wait of aip2 and a
    // rest after. Done, she does nothing.
    if (ai_name == "SandMaggotQueen") {
        if (*input.state3) { *input.state3 = 0; return { MonAct::idle, aip[1] * 25 }; }
        if (*input.state2 == 0 && *input.state >= aip[0]) return { MonAct::none };
        const int phase = *input.state2 != 0;
        Think out{ MonAct::spawn, phase };
        out.wait = aip[1];
        *input.state3 = phase;
        *input.state2 = 1 - phase;
        return out;
    }
    // MaggotEgg (40, FUN_005f6530): unhatched, aip2 % it hatches (Skill1,
    // wait aip1; state 1); hatched it dies; else stands aip1.
    if (ai_name == "MaggotEgg") {
        if (*input.state == 1) { Think out{ MonAct::die }; return out; }
        if (input.skill[0] && roll() < aip[1]) { *input.state = 1; Think out = cast(0); out.wait = aip[0]; return out; }
        return { MonAct::idle, aip[0] };
    }
    // MaggotLarva (38, FUN_005f6220): out of melee aip3 % walks in (flags
    // 1), else stands aip4; in melee, rested (state 0), aip1 % A1 (state
    // 1); else stands aip2.
    if (ai_name == "MaggotLarva") {
        if (!input.in_melee) {
            *input.state = 0;
            return roll() < aip[2] ? Think{ MonAct::walk, 1 } : Think{ MonAct::idle, aip[3] };
        }
        if (*input.state == 0 && roll() < aip[0]) { *input.state = 1; return { MonAct::a1 }; }
        *input.state = 0;
        return { MonAct::idle, aip[1] };
    }
    // Act 3's (docs/research/re/monster-ai.md "Act 3's thinks"). A circle
    // (FUN_005df7d0) says whether it set off, as `away` does.
    auto circle = [&](int count, int flag, bool& set_off) { auto out = think_circle(rng, count); out.y = flag; set_off = away(count, false); return out; };
    auto aimed_at = [](Think out, ThinkUnit unit) { out.unit = unit; return out; };
    // Mosquito (24, FUN_005f3730): in melee, aip3 % (or before its first
    // swing, state2 0) it swings: aip4 % Skill1, else A1; else 80 % stands
    // 15, 20 % flies off (state 2). Off: walks in (state 0); state 1 drifts
    // (wander 4) for aip5 thinks; state 2 backs off 10, then drifts.
    if (ai_name == "Mosquito") {
        if (input.in_melee) {
            if (roll() < aip[2] || *input.state2 == 0) {
                ++*input.state2;
                if (input.skill[0] && roll() < aip[3]) return cast(0);
                return { MonAct::a1 };
            }
            if (roll() > 20) return { MonAct::idle, 15 };
            *input.state = 2; *input.state2 = 0;
        }
        switch (*input.state) {
        case 0: set_pace(100, 0); return walk0;
        case 1: {
            set_pace(50, 0);
            const auto out = think_wander(rng, 4);
            if (++*input.state2 > aip[4]) *input.state = *input.state2 = 0;
            return out;
        }
        case 2:
            set_pace(100, 0);
            away(10, false);
            *input.state = 1; *input.state2 = 0;
            return { MonAct::none };
        default:
            *input.state = *input.state2 = 0;
            return { MonAct::idle, 10 };
        }
    }
    // ThornHulk (27, FUN_005f4850): out of melee it runs in. In melee, its
    // frenzy (Skill1) on (state: thinks left) it keeps casting; else aip1
    // % it fights: with no cooldown (state2) aip4 % frenzies (state aip6),
    // else aip2 % A1, else A2; else aip3 % circles 4, else stands 15. A
    // cast waits aip5 past its next think event, if one's due.
    if (ai_name == "ThornHulk") {
        if (!input.in_melee) { *input.state = 0; return { MonAct::run, 7 }; }
        auto frenzy = [&] {
            auto out = cast(0);
            if (input.frame < input.next_event) out.wait = aip[4] - input.frame + input.next_event;
            return out;
        };
        if (*input.state < 1) {
            if (roll() >= aip[0]) {
                if (roll() >= aip[2]) return { MonAct::idle, 15 };
                bool set_off = false;
                return circle(4, 0, set_off);
            }
            if (*input.state2 < 1 && roll() < aip[3]) { *input.state = aip[5]; return frenzy(); }
            --*input.state2;
            return { roll() >= aip[1] ? MonAct::a1 : MonAct::a2 };
        }
        const auto out = frenzy();
        if (--*input.state < 1) *input.state2 = 3;
        return out;
    }
    // Tentacle (56, FUN_005f8f80) and TentacleHead (57, FUN_005f9270): the
    // head's tentacles die with it (no group leader), or 40 % once it's
    // dying (DD). Skill1 (rise) first, then on timers (state2: frames,
    // state3: 1 up, 2 under): up, Skill2 (submerge, at itself) after aip4 ×
    // 25 frames when the target's in melee or within aip6 (a tentacle 5 %
    // anyway, its head not in S4); under, Skill1 again after aip3 × 25
    // past aip6 off, or out of melee aip2 % (a tentacle: 50 % with its head
    // in S4; the head waits 20, not 8). Up, not due: wait aip5. Else in
    // melee aip1 % A1 (the head: at its second target), else stands aip5.
    if (ai_name == "Tentacle" || ai_name == "TentacleHead") {
        const bool head = ai_name == "TentacleHead";
        const auto target = input.target ? ThinkUnit::target : ThinkUnit::none;
        if (!head && !input.grouped) return { MonAct::die };
        if (!head && input.leader_mode == 12 && roll() < 40) return { MonAct::die };
        auto rise = [&](int wait) {
            auto out = cast(0, target); out.wait = wait;
            *input.state2 = aip[2] * 25 + input.frame; *input.state3 = 1;
            return out;
        };
        if (input.skill[0]) {
            if (*input.state3 == 0) return rise(8);
            if (*input.state3 == 2 && *input.state2 < input.frame
                && (aip[5] < input.dist || (!input.in_melee && roll() < aip[1]) || (!head && input.leader_mode == 14 && roll() < 50)))
                return rise(head ? 20 : 8);
        }
        if (input.skill[1] && *input.state3 == 1) {
            if (*input.state2 < input.frame && (input.in_melee || input.dist < aip[5] || (!head && input.leader_mode != 14 && roll() < 5))) {
                *input.state2 = aip[3] * 25 + input.frame; *input.state3 = 2;
                return cast(1, ThinkUnit::self);
            }
        }
        if (*input.state3 == 1) return { MonAct::none, 0, 0, 0, ThinkUnit::target, aip[4] };
        if (head) {
            const auto second = input.second ? ThinkUnit::second : ThinkUnit::none;
            return roll() >= aip[0] ? Think{ MonAct::idle, aip[4] } : aimed_at({ MonAct::a1 }, second);
        }
        if (input.in_melee && roll() < aip[0]) return aimed_at({ MonAct::a1 }, target);
        return { MonAct::idle, aip[4] };
    }
    // FetishBlowgun (96, FUN_005e1250): its group's command first (1: A1 at
    // the unit, 14: a walk to it, at 50 %), done either way. Past aip1 out
    // of melee it closes about the target (6, at 50 %); in melee or under 6
    // off, aip2 % it breaks off (state 2). State 0: shoots its second
    // target (none: circles 4) for rand(3) + 3 thinks, then state 1: circles
    // 6 (none: stands 10). State 2: under 13 off it backs off 14 (can't: A1
    // at the second target); else 20 % circles 4, or stands 10.
    if (ai_name == "FetishBlowgun") {
        if (*input.command) {
            if ((*input.command == 1 || *input.command == 14) && input.command_unit) {
                *input.state = *input.state2 = 0;
                effect(ThinkEffect::command_done);
                if (*input.command == 1) return aimed_at({ MonAct::a1 }, ThinkUnit::kin);
                set_pace(50, 0);
                return aimed_at(walk0, ThinkUnit::kin);
            }
            effect(ThinkEffect::command_done);
        }
        if (!input.in_melee && aip[0] < input.dist) {
            set_pace(50, 0);
            auto out = think_wander(rng, 6); out.act = MonAct::around;
            return out;
        }
        if ((input.in_melee || input.dist < 6) && roll() < aip[1]) *input.state = 2;
        const auto second = input.second ? ThinkUnit::second : ThinkUnit::none;
        bool set_off = false;
        switch (*input.state) {
        case 0: {
            ++*input.state2;
            if (rng(3) + 3 < *input.state2) { *input.state = 1; *input.state2 = 0; }
            if (input.second) return aimed_at({ MonAct::a1 }, ThinkUnit::second);
            return circle(4, 0, set_off);
        }
        case 1: {
            *input.state = *input.state2 = 0;
            set_pace(50, 0);
            const auto out = circle(6, 1, set_off);
            return set_off ? out : Think{ MonAct::idle, 10 };
        }
        case 2:
            if (input.dist < 13) {
                set_pace(50, 0);
                if (away(14, false)) return { MonAct::none };
                *input.state = *input.state2 = 0;
                return aimed_at({ MonAct::a1 }, second);
            }
            if (roll() <= 19) {
                const auto out = circle(4, 1, set_off);
                if (set_off) return out;
            }
            *input.state = *input.state2 = 0;
            return { MonAct::idle, 10 };
        default: return { MonAct::idle, 10 };
        }
    }
    // FetishShaman (65, FUN_005f9a80): Inferno (Skill1) when the target's
    // AI distance is under Skill1's level (FUN_006442a0, at least 1) and
    // its state (0xc) is off, its group told to attack (command 1); else
    // that state off. A dead fetish within aip5 (FUN_005f99a0, squared;
    // the nearest): aip1 % with Skill3 usable, the group sent there
    // (command 14) and Skill3's sequence on it when its squared distance is
    // at most aip3 (game.exe compares it unsquared), else about it 10.
    // Else aip4 % circles 4, else stands 10.
    if (ai_name == "FetishShaman") {
        const int reach = input.skill[0] ? std::max(input.skill_level, 1) : 1;
        if (input.skill[0] && input.dist < reach && !input.state_c) { effect(ThinkEffect::rally, 1); return cast(0); }
        if (input.state_c) effect(ThinkEffect::state, 0xc, 0);
        if (input.corpse && input.kin_dist <= aip[4] * aip[4] && roll() < aip[0] && input.usable) {
            effect(ThinkEffect::rally, 14);
            if (input.kin_dist <= aip[2]) return aimed_at({ MonAct::seq, 2 }, ThinkUnit::kin);
            auto out = think_wander(rng, 10); out.act = MonAct::around; out.unit = ThinkUnit::kin;
            return out;
        }
        if (roll() >= aip[3]) return { MonAct::idle, 10 };
        bool set_off = false;
        return circle(4, 0, set_off);
    }
    // FrogDemon (52, FUN_005f8260; search type 5): state3 0 at first, 1
    // under, 2 up. First: past 12 off Skill1 (dive, wait 8) and under;
    // else up by Skill2 if it can land (FUN_005f2fc0), else stands 12 (up
    // if its spot is clear of 0xc01). Under: Skill2 (surface, at itself)
    // when it can land within aip8, or within 19 after 65 dives (state2);
    // else stays under (take-off, wait 24, state2 + 1). Up: no target waits
    // 32; out of melee, past aip6 aip4 % circles 3, else approaches 4;
    // within it aip5 % A2, else aip4 % circles 4, else waits aip7. In melee
    // aip2 % A2, else aip1 % A1, aip3 % circles 3, else waits aip7. Taking
    // off or landing waits 12 (the Vulture's).
    if (ai_name == "FrogDemon") {
        int& phase = *input.state3;
        auto surface = [&](Think out) { out.act = MonAct::skill; out.n = 1; out.unit = ThinkUnit::self; phase = 2; return out; };
        auto waits = [](int frames) { Think out{ MonAct::none }; out.wait = frames; return out; };
        bool set_off = false;
        if (phase == 0) {
            if (input.skill[0] && input.dist > 12) {
                effect(ThinkEffect::take_off);
                phase = 1;
                auto out = cast(0, input.target ? ThinkUnit::target : ThinkUnit::none); out.wait = 12;
                return out;
            }
            if (input.skill[1] && input.can_land) { effect(ThinkEffect::land); return surface(waits(12)); }
            if (input.spot_clear) phase = 2;
            return { MonAct::idle, 12 };
        }
        if (phase == 1) {
            if (input.skill[1] && input.can_land && (input.dist < aip[7] || (input.dist <= 19 && *input.state2 >= 65))) {
                effect(ThinkEffect::land);
                return surface(waits(12));
            }
            effect(ThinkEffect::take_off);
            ++*input.state2; phase = 1;
            return waits(24);
        }
        if (!input.target) return waits(32);
        if (!input.in_melee) {
            if (aip[5] <= input.dist) return roll() >= aip[3] ? Think{ MonAct::approach, 4 } : circle(3, 0, set_off);
            if (roll() < aip[4]) return { MonAct::a2 };
            if (roll() < aip[3]) return circle(4, 0, set_off);
            return waits(aip[6]);
        }
        if (roll() < aip[1]) return { MonAct::a2 };
        if (roll() < aip[0]) return { MonAct::a1 };
        if (roll() < aip[2]) return circle(3, 0, set_off);
        return waits(aip[6]);
    }
    // ZakarumZealot (48, FUN_005f6e60): its pace Run / Velocity (0..120 %
    // on). One it spares runs off 8 (can't: wanders 6). Hit, the first time
    // (state2 0) under aip3 % life, it runs off 8 (can't: state2 5); then
    // on its spot's 0x40 (FUN_0064cb30) it circles 4 in melee, else walks
    // in. State2 counts down. Out of melee aip4 % runs in (mode RN), else
    // walks in. In melee, after a swing (state 1) aip1 % keeps at it; else
    // 80 % stands 10, 20 % circles 4. A swing: aip2 % A2, else A1.
    if (ai_name == "ZakarumZealot") {
        int pct = 0;
        if (input.velocity > 0) pct = std::min(std::max(input.run * 100 / input.velocity - 100, 0), 120);
        bool set_off = false;
        if (input.spared) {
            set_pace(pct, 0);
            if (away(8, true)) return { MonAct::none };
            return think_wander(rng, 6);
        }
        if (input.got_hit) {
            if (*input.state2 == 0 && input.life_pct < aip[2]) {
                *input.state = 0;
                set_pace(pct, 0);
                if (away(8, true)) return { MonAct::none };
                *input.state2 = 5;
            }
            if (!input.spot_clear) return input.in_melee ? circle(4, 0, set_off) : walk;
        }
        if (*input.state2 != 0) --*input.state2;
        if (!input.in_melee) {
            *input.state = 0;
            if (roll() >= aip[3]) return walk;
            set_pace(pct, 0);
            return { MonAct::run, 0 };
        }
        if (*input.state != 0 && roll() >= aip[0]) {
            *input.state = 0;
            if (roll() < 80) return { MonAct::idle, 10 };
            return circle(4, 0, set_off);
        }
        *input.state = 1;
        return { roll() < aip[1] ? MonAct::a2 : MonAct::a1 };
    }
    // ZakarumPriest (49, FUN_005f72d0): in melee, or hit, under 33 % life
    // with Skill3 (its teleport) off cooldown (state: frame, aip5 × 4 on)
    // to a point past the target (twice as far, four times in melee) if
    // usable; in melee then aip1 % A1. Skill1 (its heal) 25 % at the
    // weakest zakarum (MonStats 0xeb / 0xee) under 61 % within aip6
    // (FUN_005f7240, squared). With no line to the target, aip1 % its
    // Skill4 (off cooldown, state2: aip5 on) aip2 %; with one, aip4 % the
    // same, then Skill2 (state3: 20 on) aip3 %. Else 30 % circles 4, else
    // stands 20.
    if (ai_name == "ZakarumPriest") {
        if (input.in_melee || input.got_hit) {
            if (skill_set(2) && input.life_pct < 33 && *input.state < input.frame) {
                *input.state = input.frame + aip[4] * 4;
                const int shift = input.in_melee ? 2 : 0;
                const int point_x = input.target_x + ((input.target_x - input.self_x) << shift);
                const int point_y = input.target_y + ((input.target_y - input.self_y) << shift);
                if (input.usable) return { MonAct::skill, 2, point_x, point_y, ThinkUnit::none };
            }
            if (input.in_melee && roll() < aip[0]) return { MonAct::a1 };
        }
        if (input.skill[0] && input.aid && input.kin_dist <= aip[5] * aip[5] && roll() < 25) return cast(0, ThinkUnit::aid);
        auto skill4 = [&] { *input.state2 = aip[4] + input.frame; return cast(3); };
        const bool ready4 = skill_set(3) && *input.state2 < input.frame;
        if (!input.line_clear) {
            if (roll() < aip[0] && ready4 && roll() < aip[1]) return skill4();
        } else if (roll() < aip[3]) {
            if (ready4 && roll() < aip[1]) return skill4();
            if (input.skill[1] && *input.state3 < input.frame && roll() < aip[2]) { *input.state3 = input.frame + 20; return cast(1); }
        }
        bool set_off = false;
        if (roll() < 30) return circle(4, 0, set_off);
        return { MonAct::idle, 20 };
    }
    // HighPriest (85, FUN_005e0490; the Council): state 0 picks. In melee,
    // aip1 % A1 (state 1), else backs off 6. Off cooldown (state2), aip2 %
    // Skill2 at the weakest ally under 76 % life within 50 (FUN_005e0430;
    // aip3 on); within aip8 aip4 % Skill1 at the target and one of the four
    // corners 5 off it (FUN_00472210, 0x6e337c; 100 on). With an S1
    // missile (MissS1) aip5 % within its Range - 2: S1. Else 80 % circles 3
    // within aip8, approaches 6 past it. Else (state 1): out of melee, under
    // 6 aip7 % S1; aip6 % stands 10 (state 0); else 30 % wanders 12, 70 %
    // walks in. In melee aip7 % S1, else aip6 % backs off 6 (state 0), else
    // 90 % A1, 10 % stands 10.
    if (ai_name == "HighPriest") {
        const Think shoot{ MonAct::s1 };
        bool set_off = false;
        if (*input.state == 0) {
            if (input.in_melee) {
                if (roll() >= aip[0]) { away(6, false); return { MonAct::none }; }
                *input.state = 1;
                return { MonAct::a1 };
            }
            if (input.skill[1] && *input.state2 < input.frame && roll() < aip[1] && input.aid && input.kin_dist <= 2500) {
                *input.state2 = aip[2] + input.frame;
                return cast(1, ThinkUnit::aid);
            }
            if (input.skill[0] && *input.state2 < input.frame && input.dist < aip[7] && roll() < aip[3]) {
                static constexpr std::array<std::pair<int, int>, 4> kCorners{ { { -5, -5 }, { 5, -5 }, { 5, 5 }, { -5, 5 } } };
                const auto [corner_x, corner_y] = kCorners[rng.next() & 3];
                *input.state2 = input.frame + 100;
                return { MonAct::skill, 0, input.target_x + corner_x, input.target_y + corner_y };
            }
            if (aip[4] > 0 && input.s1_range >= 0 && input.dist < input.s1_range - 2 && roll() < aip[4]) return shoot;
            if (roll() < 80) return input.dist <= aip[7] ? circle(3, 0, set_off) : Think{ MonAct::approach, 6 };
        }
        *input.state = 1;
        if (!input.in_melee) {
            if (input.dist < 6 && roll() < aip[6]) return shoot;
            if (roll() < aip[5]) { *input.state = 0; return { MonAct::idle, 10 }; }
            if (roll() > 69) return think_wander(rng, 12);
            return walk;
        }
        if (roll() < aip[6]) return shoot;
        if (roll() < aip[5]) { away(6, false); *input.state = 0; return { MonAct::none }; }
        if (roll() > 89) return { MonAct::idle, 10 };
        return { MonAct::a1 };
    }
    // Mephisto (50, FUN_005f78b0): hurt = (100 - life %) / 5. Picks a case
    // (state3 keeps it): past 20 off 4; one under way goes on (past 4: the
    // fallback); under 21 % life within 5, 40 %: 1; under hurt + 50 %: 2
    // (state rand(3) + 3 casts); else the fallback. In melee, past aip1 %:
    // 3, else 2. Fallback: 35 % stands 10; else within 6, 35 %, walks in (4
    // steps); else circles 4. 0: stands 5. 1: runs off 8 (can't: in melee
    // Skill3, else 2). 2: state counts its casts down; every other one
    // circles 3 (can't: wanders 12). Else a cast at his chooser's unit
    // (FUN_005f77c0: aip2 % the weakest within 32, else with a player
    // nearest aip3 % the weakest again (game.exe returns the wrong find,
    // bugs.md #21), else the target), by the skills in a row from Skill1 (n;
    // none: wanders 6): past Normal with no line to it or past 30 off
    // Skill6; under 15 off, past Normal, rand < 100 / n Skill5; under 2 ×
    // 100 / n Skill4, 3 × Skill2, else Skill1. Then under 50 - hurt % the
    // count restarts (state2 0). 3: under hurt + 80 %, under 80 - hurt % A1,
    // else Skill3; else circles 3 (can't: wanders 12). 4: about the target 6.
    // ponytail: about the target always sets off, so its fallback
    // (FUN_00540e60, keep 12 / 6) isn't taken.
    if (ai_name == "Mephisto") {
        const int hurt = std::max((100 - input.life_pct) / 5, 0);
        int& phase = *input.state3;
        bool set_off = false;
        auto fallback = [&]() -> Think {
            if (roll() > 64) { phase = 0; return { MonAct::idle, 10 }; }
            const bool walks = roll() > 64;
            phase = 0;
            if (walks && input.dist < 6) { set_pace(0, 4); return walk; }
            return circle(4, 0, set_off);
        };
        int pick = 0;
        if (!input.in_melee) {
            if (input.dist > 20) pick = 4;
            else if (phase != 0) { if (unsigned(phase) > 4) return fallback(); pick = phase; }
            else if (input.life_pct < 21 && input.dist < 5 && roll() < 40) pick = 1;
            else if (roll() >= hurt + 50) return fallback();
            else { pick = 2; *input.state = int(rng.next() % 3) + 3; }
        } else if (roll() > aip[0]) pick = 3;
        else { pick = 2; *input.state = int(rng.next() % 3) + 3; }
        phase = pick;
        auto circle_or_wander = [&] { auto out = circle(3, 1, set_off); return set_off ? out : think_wander(rng, 12); };
        switch (pick) {
        case 0: phase = 4; return { MonAct::idle, 5 };
        case 1:
            phase = 0;
            set_pace(50, 0);
            if (away(8, false)) return { MonAct::none };
            if (input.in_melee) return cast(2);
            [[fallthrough]];
        case 2: {
            if (--*input.state == 0) phase = 0;
            if (*input.state2 == 0) { *input.state2 = 2; return circle_or_wander(); }
            ++*input.state2;
            auto chosen = ThinkUnit::target;
            if (input.weak && roll() < aip[1]) chosen = ThinkUnit::second;
            else if (input.near_player && roll() < aip[2]) chosen = input.weak ? ThinkUnit::second : ThinkUnit::none;
            int count = 0;
            for (int slot = 0; slot < 8 && (slot < 5 ? skill_set(slot) : input.skill678[std::size_t(slot - 5)]); ++slot) ++count;
            Think out;
            if (count <= 0) out = think_wander(rng, 6);
            else {
                const int share = 100 / count, rolled = roll();
                const bool blocked = input.difficulty > 0 && chosen != ThinkUnit::none && !input.line_clear;
                if (input.difficulty > 0 && (blocked || input.dist > 30)) out = cast(5, chosen);
                else if (input.dist < 15 && rolled < share && input.difficulty > 0) out = cast(4, chosen);
                else if (rolled < share * 2) out = cast(3, chosen);
                else if (rolled < share * 3) out = cast(1, chosen);
                else out = cast(0, chosen);
            }
            if (roll() < 50 - hurt) *input.state2 = 0;
            return out;
        }
        case 3:
            phase = 0;
            if (roll() < hurt + 80) return roll() < 80 - hurt ? Think{ MonAct::a1 } : cast(2);
            set_pace(50, 0);
            return circle_or_wander();
        default: {
            phase = 0;
            set_pace(50, 0);
            auto out = think_wander(rng, 6); out.act = MonAct::around;
            return out;
        }
        }
    }
    // WillOWisp (25, FUN_005f39b0). With a target past the cooldown (state3,
    // a frame) within 32 (squared 0x400) and state under 4, rand(1000) <=
    // difficulty + 2: state 4. 4: the wisps within 32 (itself too); four
    // dance (word 1 6) and five (5), each word 2 0 and word 3 its place + 1,
    // all standing to the next 337th frame; else state 2, stands 8. Dancing
    // (6..12 a step, 5 the fives' one): to the target + its step's point
    // (0x6e.. tables on the stack, by word 3 + state x 4) a walk (mode 2,
    // FUN_005de490), 34 % a wander 4 instead, while past 3 off; there, on
    // each 67th frame, up to 3 casts (SC, FUN_005ddfc0 mode 7) at the target
    // + the step's other point, then the next step (past 12, or the fives':
    // state 2, cooldown 1800 frames), standing to the next 337th frame; a
    // player it's at gets stat 0x50 (magic find) (difficulty + 1) x 50 for
    // 0x1a5e00 frames each step. 1: S1 (in melee state 3; else aip1 %,
    // state 2), else walks in (aip3 %) or wanders 6. Else: out of melee SC
    // (state 2 or aip1 %), in melee A1 (state 3 or aip2 %), state 0; else
    // aip3 % walks in, wanders 4 (state 1, state2 3).
    if (ai_name == "WillOWisp") {
        static constexpr std::array<std::pair<int, int>, 53> kSteps{ { {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, { 0, 0 },
            { -10, -3 }, { 1, -5 }, { 6, 0 }, { 4, 10 }, { -9, -2 }, { -6, -11 }, { -2, -5 }, { 6, 1 }, { 0, 13 }, { -12, 0 }, { 7, 5 }, { -3, -11 }, { -15, -3 }, { -6, -3 },
            { -4, -5 }, { -6, -13 }, { -12, -2 }, { -6, 2 }, { -5, 5 }, { 1, 11 }, { -13, -6 }, { -8, -11 }, { -7, 0 }, { 1, 8 }, { -5, -8 }, { -8, 1 }, { 1, -6 }, { 6, 9 } } };
        static constexpr std::array<std::pair<int, int>, 53> kCasts{ { {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, { 6, 9 },
            { 1, -5 }, { 6, 0 }, { 4, 10 }, { -10, -3 }, { 1, 8 }, { -9, -2 }, { -5, 4 }, { 1, 8 }, { -12, 0 }, { 7, 5 }, { -3, -11 }, { 11, -2 }, { 7, 9 }, { -15, -3 },
            { -6, -13 }, { 9, 7 }, { -5, -7 }, { -5, -7 }, { 9, 0 }, { -12, -2 }, { -8, -11 }, { -2, -6 }, { -2, -6 }, { -13, -6 }, { -8, 1 }, { 1, -6 }, { 6, 9 }, { 0, 0 } } };
        static constexpr std::array<std::pair<int, int>, 6> kFiveSteps{ { { -5, 0 }, { -5, -5 }, { 3, -5 }, { 6, 3 }, { 3, 6 }, { -5, 3 } } };
        static constexpr std::array<std::pair<int, int>, 6> kFiveCasts{ { { 0, 0 }, { 6, 3 }, { 3, 6 }, { -5, 3 }, { -5, -5 }, { 3, -5 } } };
        int& state = *input.state;
        int& count = *input.state2;
        int& word3 = *input.state3;
        const int frame = input.frame;
        auto stand_to = [&](int period) { return Think{ MonAct::idle, period - frame % period }; };
        const int off_x = input.target_x - input.self_x, off_y = input.target_y - input.self_y;
        if (input.target && frame > word3 && off_x * off_x + off_y * off_y < 0x400 && state < 4 && rng(1000) <= input.difficulty + 2) state = 4;
        if (state == 4) {
            if (input.wisps == 4 || input.wisps == 5) {
                for (int found = 0; found < input.wisps; ++found) {
                    effect(ThinkEffect::set_word, found * 4 + 1, input.wisps == 4 ? 6 : 5);
                    effect(ThinkEffect::set_word, found * 4 + 2, 0);
                    effect(ThinkEffect::set_word, found * 4 + 3, found + 1);
                }
                return stand_to(337);
            }
            state = 2;
            return { MonAct::idle, 8 };
        }
        if (state >= 5) {
            const bool five = state == 5;
            if (word3 > (five ? 5 : 4)) { state = 2; return { MonAct::idle, 8 }; }
            const std::size_t step = five ? std::size_t(word3) : std::size_t(word3 + state * 4);
            if (step >= kSteps.size()) { state = 2; return { MonAct::idle, 8 }; }
            const auto [to_x, to_y] = five ? kFiveSteps[step] : kSteps[step];
            const int point_x = input.target_x + to_x, point_y = input.target_y + to_y;
            const int across = std::abs(input.self_x - point_x), down = std::abs(input.self_y - point_y);
            if ((down < across ? down + across * 2 : across + down * 2) / 2 > 3) {   // FUN_005dc5c0
                if (rng(100) < 34) return think_wander(rng, 4);
                return { MonAct::move_to, 2, point_x, point_y, ThinkUnit::none };
            }
            if (frame % 67 != 0) return stand_to(67);
            if (count < 3) {
                const auto [cast_x, cast_y] = five ? kFiveCasts[step] : kCasts[step];
                ++count;
                if (cast_x == 0 && cast_y == 0) return stand_to(five ? 337 : 67);
                return { MonAct::mode_at, 7, input.target_x + cast_x, input.target_y + cast_y, ThinkUnit::none };
            }
            if (five) { state = 2; word3 = frame + 1800; return stand_to(337); }
            ++state; count = 0;
            if (state - 6 > 6) { state = 2; word3 = frame + 1800; }
            if (input.bless_ok) effect(ThinkEffect::bless, 0x50, (input.difficulty + 1) * 50);
            return stand_to(337);
        }
        if (state == 1) {
            if (count <= 0) {
                if (input.in_melee) { state = 3; return { MonAct::s1, 0, 0, 0, ThinkUnit::none }; }
                if (roll() < aip[0]) { state = 2; return { MonAct::s1, 0, 0, 0, ThinkUnit::none }; }
            }
            --count;
            state = 1;
            return roll() < aip[2] ? walk0 : think_wander(rng, 6);
        }
        if (!input.in_melee) {
            if (state == 2 || roll() < aip[0]) { state = 0; return { MonAct::sc }; }
        } else if (state == 3 || roll() < aip[1]) { state = 0; return { MonAct::a1 }; }
        state = 1; count = 3;
        return roll() < aip[2] ? walk0 : think_wander(rng, 4);
    }
    // Hydra (86, FUN_005e9e60; Mephisto's): past its end frame (state) it's
    // gone (FUN_005ddfc0); a target within 25, 60 % Skill1; else stands 10.
    if (ai_name == "Hydra") {
        if (*input.state < input.frame) return { MonAct::gone };
        if (input.target && input.dist < 25 && roll() < 60) return cast(0);
        return { MonAct::idle, 10 };
    }
    // ---- Act 4's (monster-ai.md "Act 4's MonAIs"; tools/emu/mon_ais.py).
    auto squared = [](int from_x, int from_y, int to_x, int to_y) { return (from_x - to_x) * (from_x - to_x) + (from_y - to_y) * (from_y - to_y); };   // FUN_005b0bd0
    bool set_off_unused = false;                        // FUN_005df7d0's answer: Act 4's thinks return either way
    // VileMother (68, FUN_005fa010): under aip1 laid (state), aip3 %, with
    // under aip2 young within 25, it lays one toward the target (FUN_005f9e30:
    // Skill1 at the first of eight spots that fits). In melee aip4 % A1, else
    // stands aip7; out of it, still laying, aip5 % on: aip6 % circles 4, else
    // stands 15; else walks in.
    if (ai_name == "VileMother") {
        if (*input.state < aip[0] && roll() < aip[2] && input.young < aip[1] && input.lay_ok) { ++*input.state; return { MonAct::spawn, 2 }; }
        if (input.in_melee) return roll() < aip[3] ? Think{ MonAct::a1 } : Think{ MonAct::idle, aip[6] };
        if (*input.state < aip[0] && roll() >= aip[4]) return roll() < aip[5] ? circle(4, 0, set_off_unused) : Think{ MonAct::idle, 15 };
        return walk;
    }
    // VileDog (69, FUN_005fa280): its first think stands 5; in melee aip1 %
    // A1, else stands aip2; out of it aip3 % walks in, else stands 10.
    if (ai_name == "VileDog") {
        if (*input.state == 0) { *input.state = 1; return { MonAct::idle, 5 }; }
        if (input.in_melee) return roll() < aip[0] ? Think{ MonAct::a1 } : idle2;
        return roll() < aip[2] ? walk : Think{ MonAct::idle, 10 };
    }
    // DoomKnight (72, FUN_005faa90): in melee aip1 % A1, else stands aip2;
    // out of it aip3 % runs in, else stands aip4.
    if (ai_name == "DoomKnight") {
        if (input.in_melee) return roll() < aip[0] ? Think{ MonAct::a1 } : idle2;
        return roll() < aip[2] ? Think{ MonAct::run, 7 } : Think{ MonAct::idle, aip[3] };
    }
    // AbyssKnight (73, FUN_005fab80): Bone Armor (Skill2, at no unit) when its
    // state's off and life under aip1 %, aip2 %. In melee aip3 % A1, else
    // stands aip4. Within aip5 its fire timer (state) starts at aip6; at 0
    // Skill1 (the missile), the timer aip6; else it counts down. aip7 % runs
    // in (steps 6 or 7 on the seed's low bit); within aip8 circles 3; else
    // stands 15.
    if (ai_name == "AbyssKnight") {
        if (input.skill[1] && !input.skill_state && input.life_pct < aip[0] && roll() < aip[1]) return cast(1, ThinkUnit::none);
        if (input.in_melee) return roll() < aip[2] ? Think{ MonAct::a1 } : Think{ MonAct::idle, aip[3] };
        if (input.dist < aip[4] && *input.state < 1) *input.state = aip[5];
        if (input.skill[0] && *input.state == 0) { *input.state = aip[5]; return cast(0); }
        if (*input.state > 0) --*input.state;
        if (roll() < aip[6]) { set_pace(0, int(rng.next() & 1) + 6); return walk; }
        if (input.dist < aip[7]) return circle(3, 0, set_off_unused);
        return { MonAct::idle, 15 };
    }
    // OblivionKnight (74, FUN_005faf00): within aip1 its curse (Skill4) at a
    // target not under it, its cooldown (state) aip3 frames; else to a knight
    // (class 0x136, within 50, FUN_005fae40) farther from the target than it is, else
    // backs off 10 (pace 50), else Bone Spirit (Skill3). The second target
    // within aip2: Skill6 past the cooldown (aip4 %), else aip5 %: Bone
    // Spirit (aip6 %) or the missile (Skill1) at it. Past aip8, aip7 % about
    // the target 6; else 70 % circles 3, 30 % stands 10.
    if (ai_name == "OblivionKnight") {
        auto cursed = [&](int slot) { auto out = cast(slot); *input.state = aip[2] + input.frame; return out; };
        if (input.dist < aip[0]) {
            if (input.skill45[0] && !input.skill_state) return cursed(3);
            if (input.kin && input.kin_dist <= 2500 && input.dist * input.dist < squared(input.kin_x, input.kin_y, input.target_x, input.target_y)) return { MonAct::walk, 4, 0, 0, ThinkUnit::kin };
            set_pace(50, 0);
            if (away(10, true)) return { MonAct::none };
            if (input.skill[2]) return cast(2);
        }
        if (input.second && input.second_dist < aip[1]) {
            if (input.skill678[0] && *input.state < input.frame && roll() < aip[3]) return cursed(5);
            if (roll() < aip[4]) {
                if (input.skill[2] && roll() < aip[5]) return cast(2, ThinkUnit::second);
                if (input.skill[0]) return cast(0, ThinkUnit::second);
            }
        }
        if (aip[7] < input.dist && roll() < aip[6]) { auto out = think_wander(rng, 6); out.act = MonAct::around; return out; }
        if (roll() > 69) return { MonAct::idle, 10 };
        return circle(3, 0, set_off_unused);
    }
    // Megademon (89, FUN_005e0c80; Venom Lords, Infector of Souls): Inferno
    // (Skill1) out of melee within its level (at least 1) while state 0xc is
    // off: aip1 % past its cooldown (state, a frame), the cooldown aip6
    // frames; else state 0xc off. In melee: Inferno aip2 % past the cooldown,
    // else aip3 % A1, else aip5 % circles 3, else stands 5. Out of it aip4 %
    // walks in, else stands 10.
    if (ai_name == "Megademon") {
        const int reach = input.skill[0] ? std::max(input.skill_level, 1) : 1;
        auto inferno = [&] { *input.state = aip[5] + input.frame; return cast(0); };
        bool bottom = false;
        if (!input.skill[0] || input.in_melee || reach <= input.dist) { if (input.state_c) effect(ThinkEffect::state, 0xc, 0); }
        else if (!input.state_c) {
            if (*input.state < input.frame && roll() < aip[0]) return inferno();
            bottom = true;
        } else effect(ThinkEffect::state, 0xc, 0);
        if (!bottom && input.in_melee) {
            if (input.skill[0] && *input.state < input.frame && roll() < aip[1]) return inferno();
            if (roll() >= aip[2]) return roll() >= aip[4] ? Think{ MonAct::idle, 5 } : circle(3, 0, set_off_unused);
            return { MonAct::a1 };
        }
        return roll() >= aip[3] ? Think{ MonAct::idle, 10 } : walk;
    }
    // FingerMage (70, FUN_005fa380; Storm Casters): pace (Run·100 / Velocity
    // − 100, 0..120). Hit: in melee A1, or Skill1 within aip5. Fleeing (state
    // 1, state2 its thinks): past aip3 % life, 25 %, or past aip6 thinks it
    // stops (stands 15); within 14 it backs off 14, else stands 15. Alone
    // under aip4 % life it flees (back off 9). Out of melee past aip5: within
    // aip7 walks in, else stands 15; else aip2 % Skill1, else the target under
    // state 0x54 walks to it, else stands aip8. In melee aip1 % A1, else aip2 %
    // Skill1, else circles 5.
    if (ai_name == "FingerMage") {
        int pct = input.velocity > 0 ? input.run * 100 / input.velocity - 100 : 0;
        pct = pct < 1 ? 0 : std::min(pct, 120);
        if (input.got_hit) {
            *input.state = 0;
            if (input.in_melee) return { MonAct::a1 };
            if (input.skill[0] && input.dist < aip[4]) return cast(0);
        }
        if (*input.state == 1) {
            ++*input.state2;
            if (aip[2] < input.life_pct || roll() < 25 || aip[5] < *input.state2) *input.state = 0;
            else if (input.dist < 14) { set_pace(pct, 0); away(14, false); return { MonAct::none }; }
            return { MonAct::idle, 15 };
        }
        if (!input.grouped && input.life_pct < aip[3]) {
            *input.state = 1; *input.state2 = 0;
            away(9, false);
            return { MonAct::none };
        }
        if (!input.in_melee) {
            if (aip[4] <= input.dist) return input.dist < aip[6] ? walk : Think{ MonAct::idle, 15 };
            if (!input.skill[0] || roll() >= aip[1]) return input.skill_state ? walk0 : Think{ MonAct::idle, aip[7] };
            return cast(0);
        }
        if (roll() < aip[0]) return { MonAct::a1 };
        if (!input.skill[0] || roll() >= aip[1]) return circle(5, 0, set_off_unused);
        return cast(0);
    }
    // Regurgitator (71, FUN_005fa710; Corpse Spitters): a corpse (state 2
    // walking to it, 3 at it, 4 eaten, 5 off; state2 its id, state3 tries).
    // 5: backs off 16, done. 2: the corpse still there (DD, mode 12): past 2
    // (squared 4) approaches it again (over 5 tries: wanders 8, done), then
    // 3 and stands 8; gone: wanders 8, done. 3: eats it (Skill1), 4. 4: out
    // of melee S2 at the target, 5; in melee backs off 8. Else the nearest
    // corpse within aip6: state 1 (searching) goes to it (approach, past
    // squared 2) or stands 8 there (3); none: 20 % done, else wanders 8.
    // Else a corpse within 3 (squared 9) aip2 %, or aip5 %: to it. In melee
    // aip1 % A1, else stands 15; out of it aip3 % walks to the target,
    // else a corpse aip4 %: to it; else stands 8.
    if (ai_name == "Regurgitator") {
        const bool eatable = input.command_unit && input.leader_mode == 12;   // FUN_00552f60(state2): a corpse (DD)
        auto done = [&] { *input.state = *input.state2 = *input.state3 = 0; };
        auto go_eat = [&] { *input.state = 2; *input.state2 = input.kin_id; *input.state3 = 0; return Think{ MonAct::approach, 1, 0, 0, ThinkUnit::kin }; };
        const int kin_squared = squared(input.self_x, input.self_y, input.kin_x, input.kin_y);
        switch (*input.state) {
        case 5: away(16, true); *input.state = 0; return { MonAct::none };
        case 2:
            if (!eatable) { done(); return think_wander(rng, 8); }
            if (kin_squared > 4) {
                if (*input.state3 > 5) { done(); return think_wander(rng, 8); }
                ++*input.state3;
            }
            *input.state = 3;
            return { MonAct::idle, 8 };
        case 3:
            if (input.skill[0] && eatable) { *input.state = 4; return cast(0, ThinkUnit::kin); }
            done();
            return think_wander(rng, 8);
        case 4:
            if (!input.in_melee) { *input.state = 5; return { MonAct::a2 }; }
            away(8, true);
            return { MonAct::none };
        default: break;
        }
        const bool found = input.corpse && input.kin_dist <= aip[5] * aip[5];   // FUN_005fa690 within aip6 (squared)
        if (*input.state == 1) {
            if (found) {
                *input.state2 = input.kin_id;
                if (kin_squared > 2) { *input.state = 2; *input.state3 = 0; return { MonAct::approach, 1, 0, 0, ThinkUnit::kin }; }
                *input.state = 3; *input.state3 = 0;
                return { MonAct::idle, 8 };
            }
            if (roll() < 20) { done(); return { MonAct::none }; }
            return think_wander(rng, 8);
        }
        if (found) {
            if (kin_squared < 9 && roll() < aip[1]) return go_eat();
            if (roll() < aip[4]) return go_eat();
        }
        const int rolled = roll();
        if (input.in_melee) return rolled < aip[0] ? Think{ MonAct::a1 } : Think{ MonAct::idle, 15 };
        if (rolled < aip[2]) return walk0;
        if (found && roll() < aip[3]) return go_eat();
        return { MonAct::idle, 8 };
    }
    // Diablo (51, FUN_005e9170; monster-ai.md "Act 4's MonAIs"): his kept
    // point (the leader word, kind 10) is his own spot the first time; his
    // action from the chooser (FUN_005e8810), a pending one (state) first.
    if (ai_name == "Diablo") {
        int word_x = input.word_x, word_y = input.word_y;
        if (!input.has_word) { word_x = input.self_x; word_y = input.self_y; effect(ThinkEffect::mark, word_x, word_y); }
        const auto apart = point_apart;
        int act = *input.state;
        if (act == 0 && !input.target) act = input.stuck ? 16 : int(rng.next() % 1000) > 0 ? 11 : 4;
        else if (act == 0) {
            const bool leader = input.target_player && input.command_unit;            // FUN_005353f0 -> FUN_00552f60
            const bool leader_near = leader && input.in_chaos && apart(input.kin_x - word_x, input.kin_y - word_y, 0) < 0x55;
            bool casting = false;
            for (const auto& [id, level] : { std::pair{ input.left_skill, input.left_level }, std::pair{ input.right_skill, input.right_level } })
                casting = casting || id == 0x3b || id == 0x38 || (id == 0x33 && level > 3) || (id == 0x1b && level > 7);
            const int off = apart(input.target_x - word_x, input.target_y - word_y, (input.target_size >> 1) + 1);
            const bool point_far = off > 0x55, point_very_far = off > 0x69;
            std::array<int, 17> weight{};
            if (input.reach) {
                weight = { 0, 0, 40, 70, 0, 40, 24, 40, 15, 0, 0, 0, 0, 0, 0, 0, 0 };
                if (input.target_life_pct < 20) weight[2] = 50;
                if (input.target_state_b) weight[7] = weight[8] = 0;
                if (input.target_light_res < input.target_fire_res) weight[6] -= 10;
                if (input.target_fire_res < input.target_light_res) weight[6] += 10;
                if (!input.line_clear) weight[5] = weight[6] = 0;
                if (leader_near) weight[15] = 10;
            } else if (input.line_clear) {
                weight = { 0, 0, 0, 0, 0, 25, 25, 0, 15, 20, 10, 0, 20, 0, 0, 0, 0 };
                if (apart(input.self_x - input.target_x, input.self_y - input.target_y, input.self_size) > 25) { weight[8] -= 5; weight[10] = 20; weight[5] = 0; }
                if (input.target_light_res < input.target_fire_res) { weight[6] -= 10; weight[8] -= 10; }
                if (input.target_fire_res < input.target_light_res) { weight[6] += 10; weight[8] += 10; }
                if (input.players_looked < 2) weight[6] -= 10;
                if (input.players_looked > 3) weight[6] += 5;
                if (input.score > 60) weight[9] += 10;
                if (input.players_looked < 2 && input.skill_bonus < 2) weight[9] = 0;
                if (casting && !point_far) { weight[6] += 10; weight[10] = 30; weight[13] = 15; }
                else if (point_far) { weight[1] = 0; weight[12] = 10; weight[10] = 0; weight[13] = 15; if (weight[9] == 0) weight[9] = 10; }
                if (point_very_far) { weight[9] = 20; weight[14] = 60; }
                if (leader_near) weight[15] = 15;
            } else {
                weight = { 0, 0, 0, 0, 5, 0, 25, 0, 25, 40, 0, 0, 25, 0, 0, 0, 0 };
                if (input.players_looked < 2) { weight[8] -= 5; weight[6] = 0; weight[1] = 25; weight[9] = 0; }
                if (casting && !point_far) weight[13] = 15;
                else if (point_far) {
                    weight[1] = 0; weight[12] = 15; weight[13] = 25;
                    if (weight[9] == 0) weight[9] = 20;
                    if (input.players_looked < 2) weight[9] -= 5;
                }
                if (point_very_far) weight[14] = 60;
                if (leader_near) weight[15] = 20;
            }
            if (input.usable) weight[9] = 0;                                            // FUN_005fd470(199): the target already in a prison
            if (weight[15] != 0 && leader && !input.usable) weight[15] = 0;
            int total = 0;
            for (const int value : weight) total += value;
            const int rolled = total > 0 ? rng(total) : 0;
            act = 11;
            for (int index = 0, sum = 0; index < 17; ++index)
                if ((sum += weight[std::size_t(index)]) > rolled) { act = index; break; }
        }
        *input.state = 0;
        auto skill = [&](int slot) { return slot < 5 ? skill_set(slot) : input.skill678[std::size_t(slot - 5)]; };
        if (act != 5 && skill(7) && !input.skill_state) return cast(7, input.target ? ThinkUnit::target : ThinkUnit::none);   // Skill8 first, at the target (none: no unit)
        switch (act) {
        case 1: set_pace(20, 0); return { MonAct::move_to, 0, input.target_x, input.target_y, ThinkUnit::none };
        case 2: return { MonAct::a1 };
        case 3: return { MonAct::a2 };
        case 4: return { MonAct::s4 };
        case 5:
            if (skill(0)) {
                if (!input.state_c) { *input.state = 5; return cast(0); }
                effect(ThinkEffect::state, 0xc, 0);
            }
            break;
        case 6: if (skill(2)) return cast(2); return { MonAct::idle, 2 };
        case 7: if (skill(1)) return cast(1); break;
        case 8: if (skill(3)) return cast(3); break;
        case 9: if (skill(6)) return cast(6); break;
        case 10: if (skill(4)) return cast(4); break;
        case 12: { bool set_off = false; return circle(4, 0, set_off); }
        case 13: if (skill(5)) return cast(5); break;
        case 14: {                                                                // to his point, else halfway back toward himself
            set_pace(50, 100);
            if (away(-1, false)) return { MonAct::move_to, 0, word_x, word_y, ThinkUnit::none };
            const int half = apart(input.self_x - word_x, input.self_y - word_y, 0) >> 1;
            const int to_x = word_x + half * ((input.self_x > word_x) - (input.self_x < word_x)), to_y = word_y + half * ((input.self_y > word_y) - (input.self_y < word_y));
            set_pace(50, 100);
            if (away(-1, false)) return { MonAct::move_to, 0, to_x, to_y, ThinkUnit::none };
            break;
        }
        case 15:
            if (!input.target || !input.target_player) return { MonAct::idle, 3 };
            if (!input.command_unit) break;
            if (skill(6)) return cast(6, ThinkUnit::none);
            break;
        case 16: { set_pace(20, 0); return think_wander(rng, 5); }
        default: return { MonAct::idle, input.difficulty == 1 ? 8 : input.difficulty == 2 ? 4 : 12 };
        }
        return { MonAct::idle, 2 };
    }
    // Baal (135, FUN_005fcfe0) and his Vile Effigy (140, FUN_005fd210;
    // monster-ai.md "Baal"): Diablo's kept point and pick (FUN_005fbdd0); the
    // chooser (FUN_005fc630): a pending act (AI control +0x14) first, no
    // target: stuck 4, else 8 % Tentacles (9), else stands (1); in reach,
    // a clear line or neither, weights by FUN_005fc1c0 / FUN_005fc450 /
    // FUN_005fc300 rolled on the unit seed. The acts (FUN_005fcb60), then a
    // wait of 25. The clone dies with no master (FUN_00552fd0) or a busy one;
    // its acts 7, 9, 14, 15 swing (2) and with no target it stands 15.
    if (ai_name == "BaalCrab" || ai_name == "BaalCrabClone") {
        const bool clone = ai_name == "BaalCrabClone";
        if (clone && ((input.grouped && input.command_busy) || !input.rider || input.command_busy)) return { MonAct::die };
        int word_x = input.word_x, word_y = input.word_y;
        if (!input.has_word) { word_x = input.self_x; word_y = input.self_y; effect(ThinkEffect::mark, word_x, word_y); }
        const auto apart = point_apart;
        const bool casting = [&] {                                                  // FUN_005fc0c0: the target's right hand, then its left
            for (const auto& [id, level] : { std::pair{ input.right_skill, input.right_level }, std::pair{ input.left_skill, input.left_level } })
                if (id == 0x3b || id == 0x38 || (id == 0x33 && level > 3) || (id == 0x1b && level > 7)) return true;
            return false;
        }();
        int act = *input.state;
        if (act == 0 && !input.target) act = input.stuck ? 4 : rng.next() % 100 < 8 ? 9 : 1;
        else if (act == 0) {
            const int off = apart(input.target_x - word_x, input.target_y - word_y, (input.target_size >> 1) + 1);
            const bool point_far = off > 0x4b, point_very_far = off > 100;
            const bool normal = input.difficulty == 0;
            std::array<int, 16> weight{};
            if (input.reach) {                                                      // FUN_005fc1c0
                weight = { 0, (normal ? 75 : 50) + 100 - input.life_pct, 0, 0, 0, 0, 0, 0, 20, 30, 150, 10, 10, normal ? 45 : 70, 40, 0 };
                if (input.target_life_pct < 33) weight[10] = 200;
                if (input.target_state_b) weight[8] = weight[13] = weight[11] = 0;
                if (!input.line_clear) weight[11] = weight[13] = weight[12] = 0;
            } else if (input.line_clear) {                                          // FUN_005fc450
                weight = { 0, (normal ? 125 : 100) + 100 - input.life_pct, 5, 5, 5, 0, 5, 0, 40, 40, 0, 70, 80, normal ? 40 : 60, 20, std::max((2 - *input.state2) * 10, 0) };
                const int apart_target = apart(input.self_x - input.target_x, input.self_y - input.target_y, input.self_size);   // FUN_005dc380
                if (apart_target > 0x23) { weight[6] = 15; weight[13] = 0; }
                if (apart_target > 0x19) { weight[14] = 30; weight[11] = 0; }
                if (input.players_looked < 2) weight[11] -= 10;
                if (input.players_looked > 3) weight[13] += 25;
                if (input.score > 0x3c) weight[8] = 70;
                if (input.players_looked < 2 && input.difficulty < 2) weight[8] = 0;   // FUN_00573930 +0xc
                if (casting && !point_far) { weight[6] += 30; weight[11] += 10; weight[14] += 15; }
                else if (point_far) { weight[6] += 25; weight[14] += 35; weight[2] = 0; weight[3] = 25; }
                if (point_very_far) weight[5] = 60;
            } else {                                                                // FUN_005fc300
                weight = { 0, (normal ? 125 : 100) + 100 - input.life_pct, 20, 20, 20, 0, 20, 0, 80, 70, 0, 0, 0, 0, 0, 0 };
                if (input.players_looked < 2) { weight[2] = 45; weight[10] = 25; }
                if (casting && !point_far) { weight[13] = 50; weight[14] = 50; }
                else if (point_far) { weight[13] = 25; weight[2] = 0; weight[6] = 0; weight[3] = 35; weight[11] = 25; }
                if (point_very_far) weight[5] = 60;
            }
            int total = 0;
            for (const int value : weight) total += value;
            const int rolled = total > 0 ? rng(total) : 0;
            act = 1;
            for (int index = 0, sum = 0; index < 16; ++index)
                if ((sum += weight[std::size_t(index)]) > rolled) { act = index; break; }
        }
        if (clone && (act == 7 || act == 9 || act == 14 || act == 15)) act = 2;
        if (clone && !input.target) return { MonAct::idle, 15 };
        auto skill = [&](int slot) { return slot < 5 ? skill_set(slot) : input.skill678[std::size_t(slot - 5)]; };
        auto waited = [](Think out) { out.wait = 25; return out; };
        const ThinkUnit baal_aim = input.target ? ThinkUnit::target : ThinkUnit::none;
        switch (act) {
        case 1: return waited({ MonAct::idle, input.difficulty == 1 ? 15 : input.difficulty == 2 ? 5 : 35 });
        case 2: case 6: { Think out{ MonAct::keep, 12, 0, 2 }; out.unit = baal_aim; return waited(out); }   // FUN_005de6d0 -> FUN_005de4e0(.., 2, ..)
        case 3: { bool set_off = false; auto out = circle(6, 1, set_off); out.unit = baal_aim; return waited(out); }
        case 4: return waited(think_wander(rng, 16));
        case 5: return waited({ MonAct::move_to, 0, word_x, word_y, ThinkUnit::none });
        case 8:                                                                     // Defense Curse, else Blood Mana baal_aim a player
            if (!input.target) break;
            if (skill(5) && (input.target_max_mana < input.target_max_life || !input.target_player)) return waited(cast(5));
            if (skill(6) && input.target_player) return waited(cast(6));
            return waited({ MonAct::none });
        case 9: return waited(cast(2, baal_aim));                                         // Tentacles (0x13b, S2)
        case 10: { Think out{ MonAct::a2 }; out.unit = baal_aim; return waited(out); }
        case 11: return waited(cast(0, baal_aim));                                        // Nova (0x13c, S3)
        case 12: return waited(cast(1, baal_aim));                                        // Inferno (0x13d, S1)
        case 13: return waited(cast(3, baal_aim));                                        // Cold Missiles (0x13e, A1)
        case 14: {                                                                  // Teleport 25 off, away from the target
            if (!input.target) break;
            const int span = std::max(input.dist, 1);
            auto out = cast(4, ThinkUnit::none);
            out.x = (input.self_x - input.target_x) * 25 / span + input.self_x;
            out.y = (input.self_y - input.target_y) * 25 / span + input.self_y;
            return waited(out);
        }
        case 15: effect(ThinkEffect::clone); break;                                 // FUN_005fc860: a Vile Effigy
        default: break;
        }
        return waited({ MonAct::idle, 5 });
    }
    // TrappedSoul (99, FUN_005e9f10; the souls InitFn 46 makes): flag
    // 0x20000; no target or past 4: burning (S1) once risen, else stands 15.
    // The first think rises (S2), its timer (state2) the frame. In melee past
    // the timer: the target left of it (or level) and below (or level), A1;
    // not left of it and above (or level), A2 (timer 35 frames on); else
    // burns (timer 5 on).
    if (ai_name == "TrappedSoul") {
        if (!input.target || input.dist > 4) return *input.state != 0 ? Think{ MonAct::s1 } : Think{ MonAct::idle, 15 };
        if (*input.state == 0) { *input.state = 1; *input.state2 = input.frame; return { MonAct::s2 }; }
        if (!input.in_melee || input.frame <= *input.state2) return { MonAct::s1 };
        const unsigned self_x = unsigned(input.self_x), self_y = unsigned(input.self_y), target_x = unsigned(input.target_x), target_y = unsigned(input.target_y);
        if (target_x <= self_x && self_y <= target_y) { *input.state2 = input.frame + 35; return { MonAct::a1 }; }
        if (!(target_x < self_x) && target_y <= self_y) { *input.state2 = input.frame + 35; return { MonAct::a2 }; }
        *input.state2 = input.frame + 5;
        return { MonAct::s1 };
    }
    // Izual (55, FUN_005f89b0): its first think greets (a4q1, FUN_005b4390);
    // a stand due (state2) first. In melee: swings left (state3) or aip1 %
    // A1; else Frost Nova (Skill1, at the target's spot) aip4 %, then a stand
    // of aip5 and aip6 swings; else stands aidel. Out of it within 10 the nova
    // aip3 % (none while swings are left); aip2 % (or swings left) walks in;
    // else within 10 stands aidel, past it keeps 6..9 off.
    if (ai_name == "Izual") {
        if (*input.state == 0) { *input.state = 1; effect(ThinkEffect::greet); }
        if (*input.state2 != 0) { const Think out{ MonAct::idle, *input.state2 }; *input.state2 = 0; return out; }
        auto nova = [&] { auto out = cast(0); out.spot = true; *input.state2 = aip[4]; *input.state3 = aip[5]; return out; };
        if (input.in_melee) {
            if (*input.state3 > 0 || roll() < aip[0]) { if (*input.state3 > 0) --*input.state3; return { MonAct::a1 }; }
            *input.state3 = 0;
            if (!input.skill[0] || roll() >= aip[3]) return { MonAct::idle, input.aidel };
            return nova();
        }
        if (input.skill[0] && input.dist < 10) {
            if (*input.state3 > 0) return walk;
            if (roll() < aip[2]) return nova();
        }
        if (*input.state3 < 1 && roll() >= aip[1]) return input.dist < 11 ? Think{ MonAct::idle, input.aidel } : Think{ MonAct::keep, 6, 9, 2 };
        return walk;
    }
    // UberIzual (144, FUN_005f8c80): Izual's without the greeting, after two
    // of its own: Skill2 (Chilling Armor) on itself while its state (skill
    // +0x80) is off; Skill3 (Teleport) to the target's spot when the line to
    // it is blocked (FUN_00622aa0 mask 6, `line_clear`).
    if (ai_name == "UberIzual") {
        if (input.skill[1] && !input.skill_state) return cast(1, ThinkUnit::none);
        if (!input.line_clear) { auto out = cast(2, ThinkUnit::none); out.spot = true; return out; }
        if (*input.state2 != 0) { const Think out{ MonAct::idle, *input.state2 }; *input.state2 = 0; return out; }
        auto nova = [&] { auto out = cast(0); out.spot = true; *input.state2 = aip[4]; *input.state3 = aip[5]; return out; };
        if (input.in_melee) {
            if (*input.state3 > 0 || roll() < aip[0]) { if (*input.state3 > 0) --*input.state3; return { MonAct::a1 }; }
            *input.state3 = 0;
            if (!input.skill[0] || roll() >= aip[3]) return { MonAct::idle, input.aidel };
            return nova();
        }
        if (input.skill[0] && input.dist < 10) {
            if (*input.state3 > 0) return walk;
            if (roll() < aip[2]) return nova();
        }
        if (*input.state3 < 1 && roll() >= aip[1]) return input.dist < 11 ? Think{ MonAct::idle, input.aidel } : Think{ MonAct::keep, 6, 9, 2 };
        return walk;
    }
    // UberMephisto / UberDiablo / UberBaal (146 / 147 / 145): a bare `ret 4`
    // in 1.14d (bugs.md #26), so they don't act after the search.
    if (ai_name == "UberMephisto" || ai_name == "UberDiablo" || ai_name == "UberBaal") return { MonAct::none };
    // ---- Act 5's (monster-ai.md "Act 5's MonAIs"; tools/emu/mon_ais.py).
    auto keep_off = [&](int most, int want) { return Think{ MonAct::keep, most, want, 2 }; };   // FUN_005de6d0 -> FUN_005de4e0(.., 2, ..)
    const Think walk_in{ MonAct::walk, 0 };                                                       // FUN_005dec80(target, 0)
    // ReanimatedHorde (114, FUN_005e1540): in melee aip1 % A1, else stands
    // aip2. Skill2 (its revive, Sk2mode) with three clear lines to a target
    // between 5 and aip3 off, aip4 %; aip5 % walks in; aip6 % keeps 4 off;
    // else stands aip7.
    if (ai_name == "ReanimatedHorde") {
        if (input.in_melee) return roll() < aip[0] ? Think{ MonAct::a1 } : idle2;
        if (input.skill[1] && input.lines_clear && input.dist < aip[2] && input.dist > 5 && roll() < aip[3]) return cast(1);
        if (roll() < aip[4]) return walk_in;
        if (roll() < aip[5]) return keep_off(4, 0);
        return { MonAct::idle, aip[6] };
    }
    // SiegeBeast (115, FUN_005e1900): nothing once dead (mode 0 / 12).
    // Riderless and not under state 0x69's test, an imp within aip1 takes it
    // (FUN_005e17e0). In melee aip3 % Skill1 (at no unit), aip2 % A1, else
    // stands aip4; out of it Skill1 within its reach aip5 %; with clear
    // lines aip6 % keeps 12 off at aip7 % pace; else keeps 12 off.
    if (ai_name == "SiegeBeast") {
        if (input.self_dead) return { MonAct::none };
        if (!input.rider && !input.state69 && input.imp_near) effect(ThinkEffect::mount);
        if (input.in_melee) {
            if (input.skill[0] && roll() < aip[2]) return cast(0, ThinkUnit::none);
            return roll() < aip[1] ? Think{ MonAct::a1 } : Think{ MonAct::idle, aip[3] };
        }
        if (input.skill[0] && input.dist < input.skill1_range && roll() < aip[4]) return cast(0, ThinkUnit::none);
        if (input.lines_clear && roll() < aip[5]) set_pace(std::clamp(aip[6], 0, 0x7f), 0);
        return keep_off(12, 0);
    }
    // Minion (116, FUN_005e1b60): a live command of kind 1 (its frame not
    // past) whose unit's found and free makes that its target (and in melee
    // by reach); else the command's done. Out of melee, uncommanded, aip3 %
    // walks in, else stands aip4; commanded it walks in. In melee,
    // uncommanded, stands aip2 unless aip1 %; then aip2 % A2, else A1.
    if (ai_name == "Minion") {
        bool commanded = false, melee = input.in_melee;
        ThinkUnit aim = ThinkUnit::target;
        if (*input.command != 0) {
            if (*input.command == 1 && input.frame < input.command_until && input.command_unit && !input.command_busy) {
                commanded = true;
                aim = ThinkUnit::kin;
                melee = input.reach;
            } else effect(ThinkEffect::command_done);
        }
        if (!melee) {
            if (!commanded && roll() >= aip[2]) return { MonAct::idle, aip[3] };
            return aimed(walk_in, aim);
        }
        if (!commanded && roll() >= aip[0]) return { MonAct::idle, aip[1] };
        return aimed(roll() < aip[1] ? Think{ MonAct::a2 } : Think{ MonAct::a1 }, aim);
    }
    // SuicideMinion (117, FUN_005e1d30): its fuse (state) lit in melee at
    // aip5 frames on; past it, it blows (mode 0 at the target). Out of melee
    // unlit aip3 % walks in; else stands aip2.
    if (ai_name == "SuicideMinion") {
        if (*input.state != 0) { if (*input.state < input.frame) return { MonAct::die }; }
        else if (!input.in_melee) { if (roll() < aip[2]) return walk_in; }
        else *input.state = aip[4] + input.frame;
        return idle2;
    }
    // Succubus (118, FUN_005e1e00): the target not in state 0x20, within
    // aip4, aip3 %: Skill1 at a target with at least aip7 % life; Skill2 with
    // its own at most aip8 %; Skill3 unless the target is a player with
    // FUN_00625d60 at least FUN_00625d10; Skill4 at a player. In melee aip1 %
    // A1, else stands aip5. Skill5 aip8 % (on the unit seed) at the second
    // target; aip2 % walks in; else stands aip6.
    if (ai_name == "Succubus") {
        if (!input.target_state20 && input.dist < aip[3] && roll() < aip[2]) {
            if (input.skill[0] && input.target_life_pct >= aip[6]) return cast(0);
            if (input.skill[1] && input.self_life_pct <= aip[7]) return cast(1);
            if (input.skill[2] && (input.target_max_mana < input.target_max_life || !input.target || !input.target_player)) return cast(2);
            if (input.skill45[0] && input.target && input.target_player) return cast(3);
        }
        if (input.in_melee) return roll() >= aip[0] ? Think{ MonAct::idle, aip[4] } : Think{ MonAct::a1 };
        if (input.skill45[1] && aip[7] > 0 && input.second && rng(100) < aip[7]) return cast(4, ThinkUnit::second);
        return roll() >= aip[1] ? Think{ MonAct::idle, aip[5] } : walk_in;
    }
    // SuccubusWitch (119, FUN_005e2120): the Succubus's casts (a target
    // needed). In melee aip3 % backs off aip4 (running), else aip1 % A1, else
    // stands aip6. Out of it: Skill5 (aip8 %, aip5 % first) at the second
    // target; within aip4 aip3 % backs off aip4; with no Skill5 the second
    // target aip5 % gets S2; aip2 % walks in; half the time circles 6, else
    // (or when it can't) stands aip6.
    if (ai_name == "SuccubusWitch") {
        if (input.target && !input.target_state20 && input.dist < aip[3] && roll() < aip[2]) {
            if (input.skill[0] && input.target_life_pct >= aip[6]) return cast(0);
            if (input.skill[1] && input.self_life_pct <= aip[7]) return cast(1);
            if (input.skill[2] && (input.target_max_mana < input.target_max_life || !input.target_player)) return cast(2);
            if (input.skill45[0] && input.target_player) return cast(3);
        }
        if (input.in_melee) {
            if (roll() < aip[2] && away(aip[3] & 0xff, true)) return { MonAct::none };
            return roll() < aip[0] ? Think{ MonAct::a1 } : Think{ MonAct::idle, aip[5] };
        }
        int dist = input.dist;                                     // FUN_005ddc30 writes the second target's distance over it
        if (input.skill45[1] && aip[7] > 0 && roll() < aip[4]) {
            dist = input.second ? input.second_dist : 0x7fffffff;
            if (input.second && rng(100) < aip[7]) return cast(4, ThinkUnit::second);
        }
        if (dist < aip[3] && roll() < aip[2] && away(aip[3] & 0xff, true)) return { MonAct::none };
        if (!input.skill45[1] && input.second && roll() < aip[4]) { Think out{ MonAct::s2 }; out.unit = ThinkUnit::second; return out; }
        if (roll() < aip[1]) return walk_in;
        if (roll() > 49) return { MonAct::idle, aip[5] };
        bool set_off = false;
        auto out = circle(6, 1, set_off);
        return set_off ? out : Think{ MonAct::idle, aip[5] };
    }
    // Overseer (120, FUN_005e27a0; Shenk's quest step, FUN_00587900, isn't
    // here): hit, past its timer (state), Skill1 (the warcry) at the one that
    // hit it, the timer aip1 frames on. In melee aip6 %: aip7 % A2, else A1.
    // Skill2 aip2 % at a minion under half life within 24, Skill3 aip3 % at
    // one within 20 (FUN_005e26d0). No minions: 40 % walks in, else stands
    // 10. Closer than aip4 - aip5 it backs off to aip4; within aip4 + aip5
    // half the time circles 5, else stands 10; past it walks in.
    if (ai_name == "Overseer") {
        if (input.skill[0] && input.got_hit && *input.state < input.frame && input.attacker) {
            *input.state = aip[0] + input.frame;
            return cast(0, ThinkUnit::second);
        }
        if (input.in_melee && roll() < aip[5]) return roll() >= aip[6] ? Think{ MonAct::a1 } : Think{ MonAct::a2 };
        if (input.skill[1] && input.hurt_minion && roll() < aip[1]) return cast(1, ThinkUnit::kin);
        if (input.free_minion && roll() < aip[2]) return cast(2, ThinkUnit::aid);   // Skill3 "!= 0": an unset one (-1) passes too
        if (input.young == 0) return roll() > 59 ? Think{ MonAct::idle, 10 } : walk_in;
        if (input.dist < aip[3] - aip[4]) { (void)away((aip[3] - input.dist) & 0xff, false); return { MonAct::none }; }
        if (input.dist <= aip[4] + aip[3]) {
            if (rng(100) >= 50) return { MonAct::idle, 10 };
            bool set_off = false;
            return circle(5, 0, set_off);
        }
        return walk_in;
    }
    // MinionSpawner (121, FUN_005e2bd0; the evil huts): under aip1 made
    // (state2) and the target within aip4, past its timer (state): Skill1 (a
    // minion) at the target while fewer than aip2 minions live about
    // (FUN_005e2b70), the timer aip3 frames on; else stands aip2.
    if (ai_name == "MinionSpawner") {
        if (*input.state2 >= aip[0]) return { MonAct::none };
        if (aip[3] < input.dist || input.frame < *input.state) return idle2;
        if (input.skill[0] && input.young < aip[1]) { ++*input.state2; *input.state = aip[2] + input.frame; return cast(0); }
        return idle2;
    }
    // Imp (122, FUN_005e2ff0; imp1..imp4's aips as a, b, c, d): riding (state:
    // its beast's id, -1 none) and not under 0x69's test: a beast gone, busy,
    // ridden or under it drops the ride; past b1 it walks to it, else Skill1
    // (its teleport) onto it. In melee under a1 % life it teleports (within
    // a2 of itself), else c2 % backs off 5 (running). Then a3 % teleports;
    // within c1 c2 % backs off 5; Skill4 at the second target within c3 (c4
    // %) or d3 (d4 %); 33 % approaches 4; else 80 % stands 10, 20 % wanders 8.
    auto imp_teleport = [&] {                                       // FUN_005df850: Skill1 at a point within a2 of itself
        const int radius = input.imp_aip[0][1];
        Think out = cast(0, ThinkUnit::none);
        out.x = rng(2 * radius) - radius + input.self_x;
        out.y = rng(2 * radius) - radius + input.self_y;
        return out;
    };
    // A riding imp's think (special 16, FUN_005e2d80; its init imp_ride_pick):
    // off its mount or out of state 0x8f, back to its own think, stands 1.
    // Past 24, its mount busy or under 0x69's test, or no pick (+0x18 0): it
    // teleports off (as below), its ride word -1. Within b2, b3 %: its pick
    // (Skill3 Fireball / Skill5 Inferno) at the target, a stand 20 when unset;
    // else 50 % S1 at it, else stands 20.
    // ponytail: its +0x1c (two more draws) is never set: the pick table's 0s.
    if (ai_name == "ImpRide") {
        if (!input.riding || !input.rider) { effect(ThinkEffect::toggle); return { MonAct::idle, 1 }; }
        if (input.dist > 24 || input.command_busy || input.state69 || *input.state2 == 0) { *input.state = -1; return imp_teleport(); }
        if (input.dist < input.imp_aip[1][1] && roll() < input.imp_aip[1][2]) {
            const int slot = *input.state2;
            return slot >= 0 && slot < 5 && (slot < 3 ? input.skill[std::size_t(slot)] : input.skill45[std::size_t(slot - 3)]) ? cast(slot) : Think{ MonAct::idle, 20 };
        }
        if (roll() < 50) return { MonAct::s1 };
        return { MonAct::idle, 20 };
    }
    // SiegeTower (113, FUN_005e1860): unridden (no mount, or it's busy) with
    // a target, an imp within 20 takes it (FUN_005e17e0); stands aip1.
    if (ai_name == "SiegeTower") {
        if ((!input.rider || input.command_busy) && input.target && input.imp_near) effect(ThinkEffect::mount);
        return { MonAct::idle, aip[0] };
    }
    if (ai_name == "Imp") {
        const auto& first = input.imp_aip[0];
        const auto& third = input.imp_aip[2];
        const auto& fourth = input.imp_aip[3];
        auto teleport = imp_teleport;
        if (*input.state != -1 && !input.state69) {
            if (!input.command_unit || input.command_busy || input.rider) *input.state = -1;
            else {
                if (input.imp_aip[1][0] * input.imp_aip[1][0] < squared(input.self_x, input.self_y, input.kin_x, input.kin_y)) { Think out = walk_in; out.unit = ThinkUnit::kin; return out; }
                if (input.skill[0]) return cast(0, ThinkUnit::kin);
            }
        }
        if (input.in_melee) {
            if (input.skill[0] && input.life_pct < first[0]) return teleport();
            if (roll() < third[1] && away(5, true)) return { MonAct::none };
        }
        if (input.skill[0] && roll() < first[2]) return teleport();
        if (input.dist < third[0] && roll() < third[1]) { (void)away(5, false); return { MonAct::none }; }
        if (input.skill45[0] && input.second) {
            if (input.second_dist < third[2]) { if (rng(100) < third[3]) return cast(3, ThinkUnit::second); }
            else if (input.second_dist < fourth[2] && rng(100) < fourth[3]) return cast(3, ThinkUnit::second);
        }
        if (roll() < 0x21) return { MonAct::approach, 4 };
        if (roll() > 0x13) return { MonAct::idle, 10 };
        return think_wander(rng, 8);
    }
    // Catapult (123, FUN_005e34c0): aip1 % A1 (at no unit), else stands 15.
    if (ai_name == "Catapult") {
        if (roll() < aip[0]) { Think out{ MonAct::a1 }; out.unit = ThinkUnit::none; return out; }
        return { MonAct::idle, 15 };
    }
    // FrozenHorror (124, FUN_005e3530): Skill1 (Arctic Blast) within its level
    // aip3 % while state 0xc is off; state 0xc then off. In melee aip1 % A1;
    // out of it aip2 % walks straight at it; else stands aip4.
    if (ai_name == "FrozenHorror") {
        const int reach = input.skill[0] ? std::max(input.skill_level, 0) : 0;
        if (input.skill[0] && input.dist < reach && roll() < aip[2] && !input.state_c) return cast(0);
        if (input.state_c) effect(ThinkEffect::state, 0xc, 0);
        if (!input.in_melee) { if (roll() < aip[1]) return { MonAct::direct }; }
        else if (roll() < aip[0]) return { MonAct::a1 };
        return { MonAct::idle, aip[3] };
    }
    // BloodLord (125, FUN_005e36f0): out of melee aip2 % walks straight at
    // it, else stands aip4; in it aip1 %: aip3 % Skill1 in A2, else A1; else
    // stands aip4.
    if (ai_name == "BloodLord") {
        if (!input.in_melee) return roll() < aip[1] ? Think{ MonAct::direct } : Think{ MonAct::idle, aip[3] };
        if (roll() < aip[0]) return roll() < aip[2] ? cast(0) : Think{ MonAct::a1 };
        return { MonAct::idle, aip[3] };
    }
    // CatapultSpotter (126, FUN_005ee040): a check for its catapult (FUN_005edf70,
    // 3 % of thinks once it has called one, state3): none, it dies (mode 0).
    // Every aip2 frames since its last call (state3) with a target, aip1 %:
    // a strike (0x6e3514[rand(5)], kept for aip5 calls: state its pick, state2
    // the calls left) at a point within aip4 of the target, where a spot's
    // free (else stands 15); else stands aip2.
    if (ai_name == "CatapultSpotter") {
        if (*input.state3 == 0 || roll() <= 2) { if (input.catapult_gone) return { MonAct::die }; }
        if (*input.state3 == 0) *input.state3 = 1;
        if (aip[1] - input.frame + *input.state3 < 1 && input.target && roll() < aip[0]) {
            static constexpr std::array<int, 5> kStrikes{ 287, 288, 303, 304, 305 };
            int pick = 0;
            if (*input.state2 < 1) { pick = rng(5); *input.state = pick; *input.state2 = aip[4]; }
            else { pick = *input.state; --*input.state2; }
            Think out{ MonAct::skill, pick >= 0 && pick < 5 ? kStrikes[std::size_t(pick)] : 0 };   // only rand(5) is ever kept
            out.unit = ThinkUnit::none;
            out.x = rng(2 * aip[3]) - aip[3] + input.target_x;
            out.y = rng(2 * aip[3]) - aip[3] + input.target_y;
            if (!input.strike_spot) return { MonAct::idle, 15 };
            *input.state3 = input.frame;
            return out;
        }
        return idle2;
    }
    // NpcBarb (127, FUN_005edc50; Harrogath's defenders): in melee A1, then a
    // wait of aip1; within aip3 aip2 % runs at it at 100 % pace. Else a walk
    // to a point about its spot (up to three tries, FUN_005dee50: 20 square,
    // 40 left of it then 30 above or below it); none sets off, stands 15.
    if (ai_name == "NpcBarb") {
        if (input.target) {
            if (input.in_melee) { Think out{ MonAct::a1 }; out.wait = aip[0]; return out; }
            if (input.dist < aip[2] && roll() < aip[1]) { set_pace(100, 0); return { MonAct::run }; }
        }
        auto head_to = [&](int to_x, int to_y) { return away(-1, false) ? std::optional<Think>{ Think{ MonAct::move_to, 0, to_x, to_y, ThinkUnit::none } } : std::nullopt; };
        const std::uint32_t first = rng.next(), first_y = rng.next();
        if (auto out = head_to(int(first % 20) - 40 + input.self_x, int(first_y % 20) - 10 + input.self_y)) return *out;
        const std::uint32_t side = rng.next(), second_x = rng.next(), second_y = rng.next();
        if (auto out = head_to(int(second_x % 20) - 10 + input.self_x, ((side & 1) ? -30 : 30) + int(second_y % 20) - 10 + input.self_y)) return *out;
        const std::uint32_t third_x = rng.next(), third_y = rng.next();
        if (auto out = head_to(int(third_x % 20) - 10 + input.self_x, ((side & 1) ? 30 : -30) + int(third_y % 20) - 10 + input.self_y)) return *out;
        return { MonAct::idle, 15 };
    }
    // GenericSpawner (129, FUN_005e61b0; the evil huts' doors): with a target,
    // no spawn type found leaves the game (flag 0x20000); within 21: aip3
    // made (state2) it leaves; every aip1 frames (state, abs) with room for
    // one, a young out (sequence 0xa7). Else stands 20.
    if (ai_name == "GenericSpawner") {
        if (input.target) {
            if (input.spawn_unset && !input.spawn_found) return { MonAct::gone };
            if (input.dist < 0x15) {
                if (aip[2] <= *input.state2) return { MonAct::gone };
                if (aip[0] <= std::abs(input.frame - *input.state)) {
                    *input.state = input.frame;
                    if (input.spawn_room) { ++*input.state2; return { MonAct::seq, 0xa7 }; }
                }
            }
        }
        return { MonAct::idle, 20 };
    }
    // DeathMauler (130, FUN_005ee260): out of melee Skill1 (its burrow
    // charge) within aip3 aip4 %, aip2 % walks in, else stands 15; in it
    // aip1 % A1, else stands 15.
    if (ai_name == "DeathMauler") {
        if (!input.in_melee) {
            if (input.skill[0] && input.dist < aip[2] && roll() < aip[3]) return cast(0);
            if (roll() < aip[1]) return walk_in;
        } else if (roll() < aip[0]) return { MonAct::a1 };
        return { MonAct::idle, 15 };
    }
    // PutridDefiler (137, FUN_005efa90): in melee A1. Its find within 25
    // (FUN_005efa50): in reach skill 300 (mode 8) at it, else straight at it;
    // none: within aip1 it backs off aip2, else stands 25.
    if (ai_name == "PutridDefiler") {
        if (input.target && input.in_melee) return { MonAct::a1 };
        if (!input.defile) {
            if (input.dist < aip[0]) { (void)away(aip[1] & 0xff, false); return { MonAct::none }; }
            return { MonAct::idle, 25 };
        }
        if (input.reach) { Think out{ MonAct::skill, 300 }; out.unit = ThinkUnit::kin; return out; }
        Think out{ MonAct::direct }; out.unit = ThinkUnit::kin; return out;
    }
    // BaalTentacle (139, FUN_005ef820; Festering Appendages): its master gone
    // or busy, it goes (FUN_0057ccb0). Its life (state3) aip3 + rand(aip3)
    // x 25 frames from its first think; past it, it goes; till then in melee
    // aip1 % A1, else stands aip2.
    if (ai_name == "BaalTentacle") {
        if (!input.rider || input.command_busy) return { MonAct::die };
        if (*input.state3 == 0) { const int extra = rng(aip[2]); *input.state3 = input.frame + (extra + aip[2]) * 25; }
        if (*input.state3 < input.frame) return { MonAct::die };
        if (input.in_melee && rng(100) < aip[0]) return { MonAct::a1 };
        return idle2;
    }
    // BaalTaunt (136, FUN_005ef710): a target standing still (mode 1) counts
    // up the scratch word; past aip2 it's 0 again and Skill1 (Baal Taunt, A1)
    // goes at it. Otherwise (not still: 0 again) past aip3 it warps to the
    // target (FUN_00554ea0), past aip1 walks straight at it; else stands 25.
    if (ai_name == "BaalTaunt") {
        if (!input.target) return { MonAct::idle, 25 };
        if (input.target_still && ++*input.state > aip[1]) { *input.state = 0; return cast(0); }
        if (!input.target_still) *input.state = 0;
        if (input.dist > aip[2]) return { MonAct::warp, 0, input.target_x, input.target_y, ThinkUnit::none };
        if (input.dist > aip[0]) return { MonAct::direct };
        return { MonAct::idle, 25 };
    }
    // BaalMinion (141, FUN_005ef910; the waves' minions): out of melee (or no
    // target) aip2 % straight at it; in it aip1 %: Skill1 aip3 % (rand), else
    // A1; else stands aip3. A wait of aip4 after.
    if (ai_name == "BaalMinion") {
        Think out{ MonAct::none };
        if (!input.target || !input.in_melee) { if (roll() < aip[1]) out = { MonAct::direct }; }
        else if (roll() < aip[0]) out = input.skill[0] && rng(100) < aip[2] ? cast(0) : Think{ MonAct::a1 };
        else out = { MonAct::idle, aip[2] };
        out.wait = aip[3];
        return out;
    }
    // Ancient (133, FUN_005ef1a0): asleep, they stand 25. Their foe: Talic's
    // target or, as the others always, a pick (FUN_005e8f20); none, stands 25.
    // `apart`: FUN_006416d0, the units' distance less half each one's size.
    // Talic (FUN_005eeb70; not under state 0x36): Skill1 (Whirlwind) within aip1
    // aip2 % to aip4 past the foe along their line (mode set directly,
    // FUN_005a7c20); out of reach straight at it, a wait of 10; aip3 %: A1 or
    // A2 (rand(2)); else stands 25.
    // Madawc (FUN_005eeda0): in reach aip4 % backs off aip5 running, a wait of
    // 25; Skill1 (at no unit) aip3 % unless its states are on; within aip1
    // aip2 % A1, else stands 10; past it approaches to aip1.
    // Korlic (FUN_005ef010): Skill1's sequence within aip1 aip2 %; out of
    // reach straight at it, a wait of 10; aip3 %: A1 or A2; else stands 25.
    if (ai_name == "Ancient") {
        if (input.asleep || (input.ancient == 0 && input.state36)) return { MonAct::idle, 25 };
        if ((input.ancient != 0 || !input.target) && !input.picked) return { MonAct::idle, 25 };
        const int apart = point_apart(input.target_x - input.self_x, input.target_y - input.self_y, input.self_size / 2 + input.target_size / 2);
        auto swing = [&] { return rng(2) != 0 ? Think{ MonAct::a1 } : Think{ MonAct::a2 }; };
        auto straight = [&] { Think out{ MonAct::direct }; out.wait = 10; return out; };
        if (input.ancient == 0) {
            if (input.skill[0] && apart < aip[0] && rng(100) < aip[1]) {
                const int off_x = input.target_x - input.self_x, off_y = input.target_y - input.self_y;
                const int span = std::max(point_apart(off_x, off_y, 0), 1);
                Think out = cast(0, ThinkUnit::none);
                out.x = input.target_x + aip[3] * off_x / span;
                out.y = input.target_y + aip[3] * off_y / span;
                return out;
            }
            if (!input.reach) return straight();
            if (rng(100) < aip[2]) return swing();
            return { MonAct::idle, 25 };
        }
        if (input.ancient == 1) {
            if (input.reach && roll() < aip[3] && away(aip[4] & 0xff, true)) { Think out{ MonAct::none }; out.wait = 25; return out; }
            if (input.skill[0] && !input.skill_state && roll() < aip[2]) return cast(0, ThinkUnit::none);
            if (apart < aip[0] && roll() < aip[1]) return { MonAct::a1 };
            if (apart < aip[0]) return { MonAct::idle, 10 };
            return { MonAct::approach, int(std::int8_t(apart - aip[0])) };
        }
        if (input.skill[0] && apart < aip[0] && rng(100) < aip[1]) return { MonAct::seq, 0 };
        if (!input.reach) return straight();
        if (roll() < aip[2]) return swing();
        return { MonAct::idle, 25 };
    }
    // Nihlathak (128, FUN_005ee5d0; his quest's step FUN_0058bc40 isn't here):
    // state 0xc off. Skill1 (his teleport) is FUN_005df850 within aip2 of
    // himself. No target: hit, he teleports; else stands 25. In melee aip1 %
    // teleports; within aip5 40 % backs off 5 running. Skill3 (Corpse
    // Explosion) aip3 % at a corpse (FUN_0056e390). Skill4 within 14 60 %.
    // Skill2 aip4 %: at a minion within 25 (FUN_005e26d0), else Skill5 (a
    // minion) where there's room, else wanders 6. Past 13 (or no Skill4) 60 %
    // straight at it, a stand of 5 after; else Skill4.
    if (ai_name == "Nihlathak") {
        if (input.state_c) effect(ThinkEffect::state, 0xc, 0);
        auto teleport = [&] {
            const int radius = aip[1];
            Think out = cast(0, ThinkUnit::none);
            out.x = rng(2 * radius) - radius + input.self_x;
            out.y = rng(2 * radius) - radius + input.self_y;
            return out;
        };
        if (!input.target) return input.got_hit ? teleport() : Think{ MonAct::idle, 25 };
        if (input.in_melee && roll() < aip[0]) return teleport();
        if (input.dist < aip[4] && roll() < 40 && away(5, true)) return { MonAct::none };
        if (input.skill[2] && roll() < aip[2] && input.corpse) return cast(2, ThinkUnit::kin);
        if (input.skill45[0] && roll() < 60 && input.dist < 14) return cast(3);
        if (input.skill[1] && roll() < aip[3]) {
            if (input.free_minion) return cast(1, ThinkUnit::aid);   // FUN_005e26d0's second find (within 25, +0x10)
            if (input.skill45[1] && input.spawn_room) return cast(4);
            return think_wander(rng, 6);
        }
        if (!input.skill45[0] || input.dist > 13) {
            Think out{ MonAct::idle, 5 };
            if (roll() < 60) { out = { MonAct::direct }; out.wait = 5; }
            return out;
        }
        return cast(3);
    }
    // ClawViperEx (142, FUN_005f1de0; Act 5's claw vipers): its charge's
    // state (aip6: 2 state 0x5b, else 0x5a) off after (state). In melee aip3 %
    // A2, else stands aip5. Within aip7 aip4 %: A1 past its timer (state2,
    // then aip8 frames on), else stands aip5. Skill1 within aip2 aip1 % when
    // usable (FUN_005fd470), its state on, state 1; 50 % walks in; else
    // stands aip5.
    if (ai_name == "ClawViperEx") {
        const int charge_state = aip[5] == 2 ? 0x5b : 0x5a;
        if (*input.state != 0 && aip[5] != 0) effect(ThinkEffect::state, charge_state, 0);
        if (input.in_melee) return roll() < aip[2] ? Think{ MonAct::a2 } : Think{ MonAct::idle, aip[4] };
        if (input.dist < aip[6] && roll() < aip[3]) {
            if (*input.state2 < input.frame) { *input.state2 = aip[7] + input.frame; return { MonAct::a1 }; }
            return { MonAct::idle, aip[4] };
        }
        if (input.skill[0] && input.dist < aip[1] && roll() < aip[0] && input.usable) {
            if (aip[5] != 0) effect(ThinkEffect::state, charge_state, 1);
            *input.state = 1;
            return cast(0);
        }
        return roll() < 0x32 ? walk : Think{ MonAct::idle, aip[4] };
    }
    // HellMeteor (33, FUN_005f56d0; the River of Flame's): with Skill1, aip1 %
    // casts it at a point aip3 about itself (rand(2 aip3) - aip3 off on x,
    // then y), at no unit; else it stands aip2.
    if (ai_name == "HellMeteor") {
        if (input.skill[0] && roll() < aip[0]) {
            const int x = rng(2 * aip[2]) + input.self_x - aip[2];
            const int y = rng(2 * aip[2]) + input.self_y - aip[2];
            return { MonAct::skill, 0, x, y, ThinkUnit::none };
        }
        return { MonAct::idle, aip[1] };
    }
    // Sarcophagus (45, FUN_005f6a10; init FUN_005f6630, the nest's): past 25
    // stands 25; once it's laid more than aip3 it's gone (mode 0); every
    // aip1 frames (stamped only when it lays) it lays (Skill1, Nest) where
    // there's room; else stands 20 + a seed step % 10.
    if (ai_name == "Sarcophagus") {
        if (input.dist > 25) return { MonAct::idle, 25 };
        if (*input.state2 > aip[2]) return { MonAct::gone };
        if (input.skill[0] && std::abs(input.frame - *input.state) >= aip[0] && input.spot_free) {
            ++*input.state2;
            *input.state = input.frame;
            return { MonAct::skill, 0 };
        }
        return { MonAct::idle, int(rng.next() % 10) + 20 };
    }
    // Spirit (97, FUN_005e3840; Mephisto's in the Durance): once it's struck
    // (its scratch word) it stands 50; in melee it strikes (A1); else stands 10.
    if (ai_name == "Spirit") {
        if (*input.state != 0) return { MonAct::idle, 50 };
        if (input.in_melee) { *input.state = 1; return { MonAct::a1 }; }
        return { MonAct::idle, 10 };
    }
    // ArcaneTower (93, FUN_005e0f60; the lightning spire): its count (state2)
    // starts at aip1; before its frame (state3) it stands 10. Casting (state
    // 0, with Skill1) it casts Skill1 at the target, aip2 frames on, and after
    // the count's done strikes: aip4 of them (A1), aip6 frames apart; the
    // count done, it casts again aip5 frames on (aip3 after the casts).
    if (ai_name == "ArcaneTower") {
        if (*input.state2 == 0) *input.state2 = aip[0];
        if (input.frame < *input.state3) return { MonAct::idle, 10 };
        if (input.skill[0] && *input.state == 0) {
            if (--*input.state2 < 1) { *input.state = 1; *input.state2 = aip[3]; *input.state3 = aip[2] + input.frame; }
            else *input.state3 = aip[1] + input.frame;
            return cast(0);
        }
        if (--*input.state2 < 1) { *input.state = 0; *input.state2 = aip[0]; *input.state3 = aip[4] + input.frame; }
        else *input.state3 = aip[5] + input.frame;
        return { MonAct::a1 };
    }
    // Trap-RightArrow / Trap-LeftArrow (78 / 79, FUN_005fb6c0 / FUN_005fb7e0):
    // a target aip1..aip2 off and within 2 of its column (Right) or row
    // (Left), past its frame (state): aip3 frames on, the level's poison kind
    // casts Skill1 (state3 aip4 frames on), another the spike (A1); else it
    // stands 30. Out of that range it stands 40.
    if (ai_name == "Trap-RightArrow" || ai_name == "Trap-LeftArrow") {
        if (input.dist < aip[0] || aip[1] < input.dist) return { MonAct::idle, 40 };
        const int off = ai_name == "Trap-RightArrow" ? input.target_x - input.self_x : input.target_y - input.self_y;
        if (std::abs(off) < 3 && *input.state < input.frame) {
            *input.state = aip[2] + input.frame;
            if (input.trap_kind != 1) return { MonAct::a1 };
            if (input.skill[0]) { *input.state3 = aip[3] + input.frame; return cast(0); }
        }
        return { MonAct::idle, 30 };
    }
    // DesertTurret (94, FUN_005e0980; the fire towers): with Skill1 and no
    // frame yet it casts it where it stands (state3 0, state the frame);
    // before its frame (state) it stands 10. Within aip4 of the target it
    // turns its step (state3) toward the target's eighth (DAT_00745600 of
    // the 64 direction, 0x6e33e0) and, the lines there and to the target
    // clear, casts Skill1 aip5 steps (0x6e33a0) off itself, at no unit: aip1
    // frames on, aip3 once it's cast more than aip2 times running (the run
    // then 0). Else the run counts down one and it stands 10 (15 out of reach).
    if (ai_name == "DesertTurret") {
        if (input.skill[0] && *input.state == 0) { *input.state3 = 0; *input.state = input.frame; return { MonAct::skill, 0, 0, 0, ThinkUnit::none }; }
        if (input.frame < *input.state) return { MonAct::idle, 10 };
        const bool in_reach = input.dist <= aip[3];
        if (in_reach) {
            const int eighth = ((direction64(input.self_x, input.self_y, input.target_x, input.target_y) + 4) >> 3) & 7;
            const int step = kTurretTurn[std::size_t(eighth)][std::size_t(*input.state3 & 7)];
            *input.state3 = step;
            if (input.skill[0] && input.ray_clear[std::size_t(step)] && input.usable) {
                if (++*input.state2 > aip[1]) { *input.state = aip[2] + input.frame; *input.state2 = 0; }
                else *input.state = aip[0] + input.frame;
                return { MonAct::skill, 0, input.self_x + kTurretStep[std::size_t(step)][0] * aip[4], input.self_y + kTurretStep[std::size_t(step)][1] * aip[4], ThinkUnit::none };
            }
        }
        if (*input.state2 > 0) --*input.state2;
        return { MonAct::idle, in_reach ? 10 : 15 };
    }
    return { MonAct::untraced };
}

// The Countess (special AI 0xd, FUN_005e5c50; FUN_005b15d0 runs it in
// place of her CorruptRogue think). Home is where she first thought
// (command 10). Out of home's room she walks back; with the target out of
// it she walks back, or at home exactly a firewall within 25, else stands
// 10; over 40 from home she walks back. Else a firewall; else rand(100):
// in melee under aip3 + 10 A1, out of it under aip1 a run at the target;
// else stands aip2. A firewall (FUN_005e5b70): at her map AI's next point
// (state: its index; state2: the frame of the last), until they run out;
// 700 frames on from the last they start over.
// ponytail: a walk home always sets off, so FUN_00540e60(2, 0)'s fallback
// (on down the list) doesn't come up.
struct CountessIn { bool away = false, target_away = false, at_home = false; };
inline Think countess_think(const ThinkIn& input, const CountessIn& where, std::span<const std::pair<int, int>> path, Rng& rng) {
    auto firewall = [&](Think& out) {
        if (*input.state < int(path.size())) {
            const auto [point_x, point_y] = path[std::size_t(*input.state)];
            ++*input.state; *input.state2 = input.frame;
            out = { MonAct::point, 0, point_x, point_y };
            return true;
        }
        if (std::abs(input.frame - *input.state2) > 700) *input.state = 0;
        return false;
    };
    Think out;
    if (where.away) return { MonAct::home };
    if (where.target_away) {
        if (!where.at_home) return { MonAct::home };
        if (input.dist < 25 && firewall(out)) return out;
        return { MonAct::idle, 10 };
    }
    if (input.home_dist > 40) return { MonAct::home };
    if (firewall(out)) return out;
    const int roll = rng(100);
    if (input.in_melee) return roll < input.aip[2] + 10 ? Think{ MonAct::a1 } : Think{ MonAct::idle, input.aip[1] };
    return roll < input.aip[0] ? Think{ MonAct::run } : Think{ MonAct::idle, input.aip[1] };
}

// The AI toggle (FUN_005b0e00(ctrl, special)): the think special names
// (FUN_005b15d0: the special table 0x73d358's row, 0 the MonStats AI's
// MonAI row). On that row's think already with an other think (the row's
// fourth word) it swaps to that, ctrl[0] = special; else the scratch words
// (+0x14..+0x1c) are cleared and the row's think set (its init run). Only
// a MonAI row has an other think, so it's reached by toggling 0 on a
// monster on its own think: an AI-swapping state (Dim Vision, Terror,
// Cloak of Shadows) whose end (FUN_005c3370, on expiry or when another
// curse takes its place, FUN_006269f0) comes while it's back on its own.
// `special`: the monster's special AI (0 its own think), `other`: on its
// row's other think.
// ponytail: the init (row +4) and FUN_0058ede0 aren't run; d2d's thinks
// take their first values as they go.
inline void ai_toggle(int& special, bool& other, std::array<int*, 3> scratch, int next, bool has_other) {
    if (next == 0 && special == 0 && !other && has_other) { other = true; return; }
    special = next; other = false;
    for (int* word : scratch) *word = 0;
}

// The riding imp's init (special 16's, FUN_005e2cd0): one draw on its seed;
// slot 4 (Skill5, Inferno) on an even draw, else slot 2 (Skill3, Fireball)
// (0x6e34e0 {2, 4}), the other when it lacks that one, 0 when it has neither.
inline int imp_ride_pick(Rng& rng, bool has_fireball, bool has_inferno) {
    const bool inferno_first = (rng.next() & 1) == 0;
    if (inferno_first ? has_inferno : has_fireball) return inferno_first ? 4 : 2;
    if (inferno_first ? has_fireball : has_inferno) return inferno_first ? 2 : 4;
    return 0;
}

// MonAI rows with an other think (the fourth word) d2d has: 15 SandMaggot,
// 29 BatDemon. ponytail: rows 51, 52, 61, 65, 128, 135, 140, 145, 147, 152
// (Acts 3..5) have one too; not ported, so they stay on their own think.
inline bool has_other_think(std::string_view ai_name) { return ai_name == "SandMaggot" || ai_name == "BatDemon" || ai_name == "FetishShaman" || ai_name == "FrogDemon" || ai_name == "Diablo"; }

// Dim Vision's and Cloak of Shadows' think (special AI 10, FUN_005e8020;
// search type 1): in melee with a swing it swings (A1, mode 4); else 20 %
// it wanders 3, else stands 10.
inline Think dim_vision_think(const ThinkIn& input, Rng& rng) {
    if (input.in_melee && input.swings) return { MonAct::a1 };
    if (rng(100) < 20) return think_wander(rng, 3);
    return { MonAct::idle, 10 };
}

// Terror's think (special AI 11, FUN_005e8140; Howl's too, FUN_005ddd00).
// With state 0x38 gone it toggles back (FUN_005b0e00(ctrl, 0)) and thinks
// next frame (FUN_005417d0(2, frame + 1)). Past its reach (state, 30 when
// 0) it stands 10. Its pace is Run / Velocity (0..120 % on). The first time
// (state3 0; state3 1) its skill-set target is cleared and it backs off 30
// (running with an RN mode, FUN_005df140; else walking, FUN_005defe0);
// after, in melee with a swing it swings (A1), else backs off 30 again
// (flags 4), wandering 6 if it can't.
// ponytail: its init (FUN_005e80e0) sets a skill-set target to the unit it
// was at; d2d's search finds the same one.
template <class Away>
Think terror_think(const ThinkIn& input, Rng& rng, Away&& away) {
    if (!input.afraid) {
        input.effects->push_back({ ThinkEffect::toggle, 0, 0 });
        Think out{ MonAct::none };
        out.event = 1;
        return out;
    }
    if (input.dist > (*input.state != 0 ? *input.state : 30)) return { MonAct::idle, 10 };
    const int pace = input.velocity > 0 ? std::clamp(input.run * 100 / input.velocity - 100, 0, 120) : 0;
    if (*input.state3 == 0) {
        *input.state3 = 1;
        input.effects->push_back({ ThinkEffect::unset, 0, 0 });
        *input.pace = pace;
        return away(30, input.runs) ? Think{ MonAct::none } : Think{ MonAct::idle, input.aidel };   // flags 0: aidel on when it can't (FUN_005a73e0)
    }
    if (input.in_melee && input.swings) return { MonAct::a1 };
    *input.pace = pace;
    if (away(30, input.runs)) return { MonAct::none };
    return think_wander(rng, 6);
}

// The other thinks (the MonAI row's fourth word; ai_toggle). BatDemon's
// (FUN_005f4fd0): in S4 it waits 1 (state 1); at state 4 it rises (S3,
// FUN_005ddf90(10, none); state 1); else it toggles back (FUN_005b0e00(ctrl,
// ctrl[0])) and stands 1. SandMaggot's (FUN_005f1750): with its AI command
// 14's +0x18 at 1 and a Skill1 it casts Skill1 at the target (event 30; the
// word 0); else it toggles back keeping the third scratch word, event 1.
// ponytail: nothing found sets command 14's +0x18, so d2d's word stays 0.
inline Think other_think(std::string_view ai_name, const ThinkIn& input) {
    // Diablo's (FUN_005e8480): state 0xc (his lightning's) off, back to his
    // own think (FUN_005b0e00), stands 1; his kept point stays.
    if (ai_name == "Diablo") {
        if (input.state_c) input.effects->push_back({ ThinkEffect::state, 0xc, 0 });
        input.effects->push_back({ ThinkEffect::toggle, 0, 0 });
        return { MonAct::idle, 1 };
    }
    if (ai_name == "BatDemon") {
        if (input.in_s4) { *input.state = 1; Think out{ MonAct::none }; out.wait = 1; return out; }
        if (*input.state == 4) { *input.state = 1; Think out{ MonAct::s3 }; out.unit = ThinkUnit::none; return out; }
        input.effects->push_back({ ThinkEffect::toggle, 0, 0 });
        return { MonAct::idle, 1 };
    }
    if (ai_name == "SandMaggot") {
        if (*input.cast_word == 1 && input.skill[0]) { *input.cast_word = 0; Think out{ MonAct::skill, 0 }; out.event = 30; return out; }
        input.effects->push_back({ ThinkEffect::toggle, 0, 1 });
        Think out{ MonAct::none };
        out.event = 1;
        return out;
    }
    // FrogDemon's (FUN_005f81d0): up (state3 past 1) with a Skill1, not on
    // a curse's think (special 10 / 11), it dives (Skill1, under, take-off);
    // else toggled back, waits 1.
    // ponytail: the special taken as 0, as an other think runs.
    if (ai_name == "FrogDemon") {
        if (input.skill[0] && *input.state3 > 1) {
            *input.state3 = 1;
            input.effects->push_back({ ThinkEffect::take_off, 0, 0 });
            Think out{ MonAct::skill, 0 }; out.wait = 12;
            return out;
        }
        input.effects->push_back({ ThinkEffect::toggle, 0, 0 });
        Think out{ MonAct::none }; out.wait = 1;
        return out;
    }
    // FetishShaman's (FUN_005f9950): its Inferno state off, toggled back,
    // stands 1.
    if (ai_name == "FetishShaman") {
        if (input.state_c) input.effects->push_back({ ThinkEffect::state, 0xc, 0 });
        input.effects->push_back({ ThinkEffect::toggle, 0, 0 });   // to AI control +0, 0 on an other think
        return { MonAct::idle, 1 };
    }
    return { MonAct::untraced };
}

}  // namespace d2d::rules
