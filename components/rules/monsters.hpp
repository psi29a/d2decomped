// SPDX-License-Identifier: GPL-3.0-or-later
// Monsters: MonStats / MonStats2 / MonLvl rows, which monsters a level
// spawns and where (game.exe's monster region and room population), and a
// spawned monster's stats. docs/research/re/monsters.md. Fighting is in
// combat.hpp, what they drop in drops.hpp.
#pragma once

#include "level_ids.hpp"
#include "montypes.hpp"
#include "rules.hpp"
#include "uniques.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
// objgroup entry. The picked object's placement (FUN_00731d00's
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

// FUN_005a43e0 at (lx, ly): champion or unique (FUN_005a0760), counted,
// then a champion's pack (FUN_0054e1e0: 1..3 more champions, radius 4) or
// a unique's minions (FUN_005a0c00: 3..6 of minion1 or its own type,
// radius 3).
// ponytail: a monster's own seed (unit +0x20) is the room's here.
template <class Fits>
void boss_pack(const Monsters& monsters, int utype, int leader_x, int leader_y, SpawnRoom& room, Fits&& fits, std::vector<Spawn>& out, Population& pop,
               Rng& game) {
    using monster_detail::place;
    const auto& unique_type = monsters.types[std::size_t(utype)];
    auto boss = roll_boss(*pop.umods, unique_type, pop.difficulty, true, room.seed);
    const int leader = int(out.size());
    out.push_back({ utype, leader_x, leader_y, leader, -1, boss.kind, boss.mods, boss.name_seed, game.next() });
    ++pop.uniques;
    int spot_x, spot_y;
    if (boss.kind == Boss::champion) {
        for (int remaining = room.seed(3) + 1; remaining > 0; --remaining)
            if (place(room, leader_x, leader_y, 4, fits, spot_x, spot_y)) out.push_back({ utype, spot_x, spot_y, leader, -1, Boss::champion, { umod::champion }, 0, game.next() });
    } else {
        const int minion_type = unique_type.minion[0] >= 0 ? unique_type.minion[0] : utype;
        for (int remaining = room.seed(4) + 3; remaining > 0; --remaining)
            if (place(room, leader_x, leader_y, 3, fits, spot_x, spot_y)) out.push_back({ minion_type, spot_x, spot_y, leader, -1, Boss::minion, {}, 0, game.next() });
    }
}

// Populate one room (FUN_0054ec90): one roll of the game seed per 3x3
// subtiles against the density; a hit picks a type by rarity
// (FUN_005bde80), rolls unique-or-group (FUN_005be020; MonUMin/Max from
// FUN_005479c0, e.g. 1 / 1 in normal Cold Plains), then places a group
// (FUN_0054df80): a free spot at a random point of the room (20 tries,
// not within WarpDist of an entrance, FUN_0054dc40), the leader there,
// PartyMin..Max minions round it (FUN_005b2830, radius 4), then
// MinGrp..MaxGrp - 1 more of its type (radius 3). Fallen and dung
// soldiers come as one leader plus their party.
// `fits(x, y)`: can a monster stand at subtile (x, y); `near_entrance(x, y)`:
// too close to where players come in.
// With `pop`, champions and uniques as FUN_005be020 / FUN_005a43e0 roll them.
// ponytail: the seed at +0x20 the counts use isn't identified — the
// room's seed stands in.
template <class Fits, class Near>
void populate_room(const Monsters& monsters, const Region& reg, int density, SpawnRoom room, Rng& game,
                   Fits&& fits, Near&& near_entrance, std::vector<Spawn>& out, Population* pop = nullptr) {
    using monster_detail::place;
    if (pop) ++pop->rooms_done;                                 // FUN_0054ebc0
    if (reg.types.empty() || density <= 0) return;
    density = std::min(density, 10000);
    auto spot = [&](int& spot_x, int& spot_y) { return room_spot(room, fits, near_entrance, spot_x, spot_y); };
    if (room.areas.empty()) room.areas.push_back({ room.x, room.y, room.width, room.height });
    for (room.area = 0; room.area < int(room.areas.size()); ++room.area)   // FUN_0054ec90: per area
    for (int tries = (room.areas[std::size_t(room.area)][3] / 3) * (room.areas[std::size_t(room.area)][2] / 3); tries > 0; --tries) {
        if (int(game.next() % 100000) > density) continue;
        int type = pick_type(reg, room.seed);
        if (const int spawn = monsters.types[std::size_t(type)].place_spawn; spawn >= 0 && room.seed(100) > 20) type = spawn;   // FUN_005bde80: a crow nest's crows
        const auto& type_info = monsters.types[std::size_t(type)];
        // FUN_005be020 (room seed): a unique while under MonUMin (chance
        // rooms done / rooms in the level) or under MonUMax (6 %); else a
        // group (its champion answer, 1, becomes a group too).
        bool boss = false;
        if (pop && pop->uniques < pop->umin && pop->rooms_total > 0)
            boss = room.seed(100) < pop->rooms_done * 100 / pop->rooms_total;
        if (pop && !boss && pop->uniques < pop->umax) boss = room.seed(100) < 6;
        if (!boss) (void)room.seed(100);
        if (boss && pop->umods) {
            // FUN_005a43e0: one monster of a type picked again (FUN_005bde80
            // unique pick: NM / hell use the region's list), at a spot
            // (FUN_005a09e0), champion or unique (FUN_005a0760), then a
            // champion's pack (FUN_0054e1e0) or a unique's minions (FUN_005a0c00).
            // ponytail: normal's pick from Levels.txt umon1.. isn't there (the
            // region's list stands in); a monster's own seed (unit
            // +0x20) is the room's here; MonStats `spawn` replacement skipped.
            const int utype = pick_type(reg, room.seed);
            int spot_x, spot_y, leader_x, leader_y;
            if (!spot(spot_x, spot_y) || !place(room, spot_x, spot_y, -1, fits, leader_x, leader_y)) continue;
            boss_pack(monsters, utype, leader_x, leader_y, room, fits, out, *pop, game);
            continue;
        }
        int low = type_info.min_grp, high = type_info.max_grp;
        if (type_info.base == 19 || type_info.base == 91) low = high = 1;  // FUN_0054ec40
        if (type_info.sparse && type_info.sparse < int(game.next() % 100)) continue;
        if (!low || !high || low > high) continue;
        int spot_x = 0, spot_y = 0;
        if (!spot(spot_x, spot_y)) continue;
        int leader_x, leader_y;
        if (!place(room, spot_x, spot_y, -1, fits, leader_x, leader_y)) continue;
        const int leader = int(out.size());
        out.push_back({ type, leader_x, leader_y, leader, -1, Boss::none, {}, 0, game.next() });
        auto nearby = [&](int who, int radius) {
            int found_x, found_y;
            if (who >= 0 && std::size_t(who) < monsters.types.size() && place(room, leader_x, leader_y, radius, fits, found_x, found_y))
                out.push_back({ who, found_x, found_y, leader, -1, Boss::none, {}, 0, game.next() });
        };
        // The counts roll the leader's own seed (unit +0x20): its look and
        // one stat roll (FUN_00573xxx) as made, the party (FUN_005b2830),
        // then MinGrp - 1 + rand(MaxGrp - MinGrp + 1) more (FUN_0054df80).
        Rng unit_seed{ out[std::size_t(leader)].seed };
        const std::vector<Components>* sets = nullptr;
        for (std::size_t i = 0; i < reg.types.size() && i < reg.components.size(); ++i)
            if (reg.types[i].first == type) sets = &reg.components[i];
        (void)monster_look(sets, type_info.choices, unit_seed);
        unit_seed.next();
        if (type_info.minion[0] >= 0) {                         // FUN_005b2830
            const int count = unit_seed.range(type_info.party_min, type_info.party_max);
            const int kinds = type_info.minion[1] >= 0 ? 2 : 1;
            for (int i = 0; i < count; ++i) nearby(type_info.minion[std::size_t(i % kinds)], 4);
        }
        for (int extra = unit_seed(high - low + 1) + low - 1; extra > 0; --extra) nearby(type, 3);
    }
}

// A monster's stats at `level` (FUN_00573cb0 with FUN_006538a0): MonLvl's
// row for the level (at most its last; the L- columns in an expansion
// game) x MonStats' percentage / 100, or the MonStats value itself for a
// noRatio row; life min + rand(max - min + 1) on the unit's seed.
// Elemental attacks: MonLvl damage x El1..3 MinD / MaxD % the same way
// (FUN_005a4f50), the length as written. tools/emu/monstats.py checks
// every row and difficulty against game.exe.
// ponytail: single player; the /players life and experience bonus
// (FUN_005738f0 / FUN_00573910: 50 % a player past 2) waits for players;
// Nightmare / Hell take the area's level (FUN_0061dca0) for a ratio row,
// not Level(N) / Level(H), which waits for those difficulties.
struct MonStats {
    int level = 1, hit_points = 1, armor_class = 0, to_hit = 0, a1_min = 0, a1_max = 0, a2_min = 0, a2_max = 0, exp = 0;
    struct El { int type = -1, pct = 0, min = 0, max = 0, dur = 0; std::string_view mode; };
    std::array<El, 3> elements{};
};
inline MonStats monster_stats(const Monsters& monsters, int type, int difficulty, Rng& rng, int level_add = 0) {
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
    stats.armor_class  = pct(level_row.armor_class[std::size_t(difficulty_index)], per_difficulty.armor_class);
    stats.to_hit  = pct(level_row.to_hit[std::size_t(difficulty_index)], per_difficulty.a1_th);
    stats.a1_min = pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.a1_min);
    stats.a1_max = std::max(pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.a1_max), stats.a1_min);
    stats.a2_min = pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.a2_min);
    stats.a2_max = std::max(pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.a2_max), stats.a2_min);
    stats.exp = pct(level_row.experience[std::size_t(difficulty_index)], per_difficulty.exp);
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
enum class MonAct : std::uint8_t { idle, a1, a2, s2, skill, walk, run, approach, keep, circle, wander, none, die, around, home, point, untraced };
struct Think { MonAct act = MonAct::idle; int n = 0, x = 0, y = 0; };
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
};

// A monster's skill level (FUN_00573cb0): Sk*lvl + DifficultyLevels
// MonsterSkillBonus (+0x10 via FUN_00573930).
inline int monster_skill_level(int sk_lvl, int difficulty) {
    static constexpr std::array<int, 3> kMonsterSkillBonus{ 0, 3, 7 };
    return sk_lvl + kMonsterSkillBonus[std::size_t(std::clamp(difficulty, 0, 2))];
}

inline bool traced_ai(std::string_view ai_name) {
    static constexpr std::array<std::string_view, 23> kTraced{ "Skeleton", "Zombie", "Bighead", "BloodHawk", "Brute", "Wraith", "Goatman",
                                                               "CorruptRogue", "QuillRat", "CorruptArcher", "CorruptLancer", "SkeletonBow", "Fallen", "FallenShaman", "FoulCrowNest",
                                                               "BloodRaven", "SkeletonMage", "GargoyleTrap", "Arach", "Vampire", "Fetish", "Griswold", "Smith" };
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

// `away(n, run)` backs off n subtiles from the target (FUN_005defe0 walking,
// FUN_005df140 running) and says whether it set off.
template <class Away>
Think mon_think(std::string_view ai_name, const ThinkIn& input, Rng& rng, Away&& away) {
    const auto& aip = input.aip;
    auto roll = [&] { return rng(100); };
    auto a1_or_a2 = [&](int chance) { return Think{ roll() < chance ? MonAct::a1 : MonAct::a2 }; };
    const Think idle2{ MonAct::idle, aip[1] }, walk{ MonAct::walk, 7 };
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
    // shot within 15); below, it backs off under 3, walks in past 15, else
    // aip4 % shoots, aip2 % circles, else stands 10.
    // ponytail: the second target search (FUN_005ddc30) taken as its target.
    if (ai_name == "Bighead") {
        if (!input.in_melee && input.got_hit) return { MonAct::a2 };
        if (input.life_pct >= aip[0]) {
            if (input.in_melee) return { MonAct::a1 };
            if (input.dist < 15 && roll() < aip[2]) return { MonAct::a2 };
            return walk;
        }
        if (input.dist < 3) return away(5, false) ? Think{ MonAct::none } : Think{ MonAct::a2 };
        if (input.dist > 15) return { MonAct::approach, 6 };
        if (roll() < aip[3]) return { MonAct::a2 };
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
    // CorruptArcher (35, FUN_005f5a20): shoots a foe it's hit by; under 6,
    // aip4 % runs 12 off; past aip8, aip1 % walks in; past aip5 runs in;
    // else aip2 % a shot (Skill2 aip6 %, Skill3 aip7 %, else Skill1 or A1).
    // ponytail: its target search (FUN_005ddc30) is the think's target; with
    // none the driver doesn't call it, so the 50 % circle is left out.
    if (ai_name == "CorruptArcher") {
        if (!input.in_melee && input.got_hit) return { MonAct::a1 };
        if (input.dist < 6 && roll() < aip[3] && away(12, true)) return { MonAct::none };
        if (aip[7] > 0 && aip[7] < input.dist && roll() < aip[0]) return { MonAct::approach, aip[7] };
        if (aip[4] < input.dist) return { MonAct::run };
        if (roll() >= aip[1]) return { MonAct::idle, aip[2] };
        if (input.skill[1] && roll() < aip[5]) return { MonAct::skill, 1 };
        if (input.skill[2] && roll() < aip[6]) return { MonAct::skill, 2 };
        return input.skill[0] ? Think{ MonAct::skill, 0 } : Think{ MonAct::a1 };
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
    // SkeletonMage (FUN_005f96c0): past aip2, aip3 % closes to aip2; within
    // aip4, aip5 % backs off 5 (blocked: A1); within aip6, aip1 % shoots
    // (A1, MissA1); past aip2 aip3 % again closes in; else aip7 % circles 4,
    // else stands aip8.
    // ponytail: its target search (FUN_005ddc30) is the think's target, so
    // the first test comes round twice.
    if (ai_name == "SkeletonMage") {
        if (aip[1] < input.dist && roll() < aip[2]) return { MonAct::approach, aip[1] };
        if (input.dist <= aip[3] && roll() < aip[4]) return away(5, false) ? Think{ MonAct::none } : Think{ MonAct::a1 };
        if (input.dist < aip[5] && roll() < aip[0]) return { MonAct::a1 };
        if (aip[1] < input.dist && roll() < aip[2]) return { MonAct::approach, aip[1] };
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
    // backs off 8, else 50 % circles 4, else stands 10.
    // ponytail: the second target search (FUN_005ddc30) taken as its target.
    if (ai_name == "Vampire") {
        const bool shoots = aip[4] & 1;
        auto shot = [&] { return Think{ MonAct::skill, roll() < 50 ? 0 : 3 }; };
        auto cast = [&] {                                   // FUN_005dead0(Sk2mode / Sk3mode, at the target)
            for (const int skill : { 1, 2 })
                if ((aip[4] & (skill << 1)) && *input.state3 < 1 && roll() < aip[3]) { *input.state3 = 11; return skill; }
            return 0;
        };
        if (*input.state3 > 0) --*input.state3;
        if (input.got_hit) {
            if (*input.state == 0) *input.state = 1;
            if (input.dist < 30 && *input.state2 < input.dist) *input.state2 = input.dist;
            if (input.in_melee) return roll() > 30 || !shoots ? Think{ MonAct::a1 } : shot();
        }
        if (*input.state == 2) {
            if (input.life_pct > 74) { *input.state = 1; return walk; }
            if (input.dist < 14 || input.dist <= *input.state2) {
                *input.pace = input.velocity > 0 ? std::clamp(input.run * 100 / input.velocity - 100, 0, 120) : 0;
                if (away(8, false)) return { MonAct::none };       // flags 4: no aidel think when it can't
                *input.pace = 0;
            }
            if (aip[2] <= input.dist || roll() >= aip[1]) return { MonAct::idle, 15 };
            if (const int skill = cast()) return { MonAct::skill, skill };
            return !shoots || input.dist > 20 ? think_circle(rng, 4) : shot();
        }
        if (input.life_pct < 33) {
            *input.state = 2;
            if (away(8, false)) return { MonAct::none };
        }
        if (input.in_melee) {
            *input.state = 1;
            if (roll() < aip[0]) {
                if (!shoots || roll() > 30) return { MonAct::a1 };
                if (input.dist < 21) return shot();
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
        if (!shoots || input.dist > 20) return walk;
        return roll() > 74 ? think_circle(rng, 4) : shot();
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

}  // namespace d2d::rules
