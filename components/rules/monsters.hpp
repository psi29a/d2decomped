// Monsters: MonStats / MonStats2 / MonLvl rows, which monsters a level
// spawns and where (game.exe's monster region and room population), and a
// spawned monster's stats. docs/research/re/monsters.md. Fighting is in
// combat.hpp, what they drop in drops.hpp.
#pragma once

#include "montypes.hpp"
#include "rules.hpp"
#include "uniques.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
};

// A level's population so far (monster region +4 rooms done, +0xc rooms
// in the level, +0x2c8 uniques made) and what its uniques roll with.
struct Population {
    int rooms_done = 0, rooms_total = 0, uniques = 0;
    int umin = 0, umax = 0;                                   // Levels MonUMin / MonUMax for the difficulty
    int difficulty = 0;
    const UMods* umods = nullptr;
};

// A room to populate: its rect in subtiles and its seed.
struct SpawnRoom { int x = 0, y = 0, width = 0, height = 0; Rng seed; };

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
                                                        int rooms_total = 0) {
    std::vector<ObjectGroupPick> picks;
    const bool throttle = rooms_total > 0 && rooms_populated_before * 128 / rooms_total > 96;
    for (int i = 0; i < 8; ++i) {
        int roll = int(room_seed.next() % 100);
        if (throttle) roll = 100;
        const std::uint8_t group_id = level_mon.obj_group[std::size_t(i)];
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
    const int room_x = room.x + 1, room_y = room.y + 1, room_width = room.x + room.width - room_x, room_height = room.y + room.height - room_y;
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
// (FUN_005bde80), rolls unique-or-group (FUN_005be020; no uniques while
// MonUMin/Max are 0, as in normal act 1), then places a group
// (FUN_0054df80): a free spot at a random point of the room (20 tries,
// not within WarpDist of an entrance, FUN_0054dc40), the leader there,
// PartyMin..Max minions round it (FUN_005b2830, radius 4), then
// MinGrp..MaxGrp - 1 more of its type (radius 3). Fallen and dung
// soldiers come as one leader plus their party.
// `fits(x, y)`: can a monster stand at subtile (x, y); `near_entrance(x, y)`:
// too close to where players come in.
// With `pop`, champions and uniques as FUN_005be020 / FUN_005a43e0 roll them.
// ponytail: the seed at +0x20 the counts use isn't identified — the
// room's seed stands in; MonStats `spawn` replacements aren't applied (no
// act 1 wilderness monster has one).
template <class Fits, class Near>
void populate_room(const Monsters& monsters, const Region& reg, int density, SpawnRoom room, Rng& game,
                   Fits&& fits, Near&& near_entrance, std::vector<Spawn>& out, Population* pop = nullptr) {
    using monster_detail::place;
    if (pop) ++pop->rooms_done;                                 // FUN_0054ebc0
    if (reg.types.empty() || density <= 0) return;
    density = std::min(density, 10000);
    auto spot = [&](int& spot_x, int& spot_y) { return room_spot(room, fits, near_entrance, spot_x, spot_y); };
    for (int tries = (room.height / 3) * (room.width / 3); tries > 0; --tries) {
        if (int(game.next() % 100000) > density) continue;
        const int type = pick_type(reg, room.seed);
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
            // ponytail: normal's pick from Levels.txt umon1.. isn't there (act 1
            // normal has MonUMin / MonUMax 0); a monster's own seed (unit
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
        if (type_info.minion[0] >= 0) {                         // FUN_005b2830
            const int count = room.seed.range(type_info.party_min, type_info.party_max);
            const int kinds = type_info.minion[1] >= 0 ? 2 : 1;
            for (int i = 0; i < count; ++i) nearby(type_info.minion[std::size_t(i % kinds)], 4);
        }
        for (int extra = room.seed(high - low + 1) + low - 1; extra > 0; --extra) nearby(type, 3);
    }
}

// A monster's stats at `level`: MonStats' percentages of the MonLvl row
// (1.10+ tables; normal uses the monster's own Level). HP is rolled.
// ponytail: the stat init (MONSTER_InitStats in 1.10) isn't traced in
// game.exe — this is the documented txt contract.
// Elemental attacks come as MonLvl damage percentages too (El1..3 MinD/MaxD).
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
    auto pct = [](int base, int percent) { return base * percent / 100; };
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

// Distance between two units of sizes `size_a` / `size_b` (MonStats2
// SizeX; a player's 2), subtiles apart (FUN_00641530): close up, the
// 8x8 table at 0x6eb180 (one less when either is size 3), else the deltas
// less their half sizes, the smaller plus twice the larger.
inline int unit_distance(int dx, int dy, int size_a, int size_b) {
    static constexpr std::array<std::array<int, 8>, 8> kNear{ {
        { -1, -1, -1, 0, 2, 4, 6, 8 }, { -1, -1, 0, 1, 2, 4, 6, 8 }, { -1, 0, 0, 2, 3, 5, 7, 8 }, { 0, 1, 2, 2, 4, 5, 7, 8 },
        { 2, 2, 3, 4, 5, 6, 7, 9 }, { 4, 4, 5, 5, 6, 7, 8, 9 }, { 6, 6, 7, 7, 7, 8, 10, 10 }, { 8, 8, 8, 8, 9, 9, 10, 11 } } };
    dx = std::abs(dx); dy = std::abs(dy);
    if (dx < 8 && dy < 8 && size_a < 4 && size_b < 4) {
        int near = kNear[std::size_t(dy)][std::size_t(dx)];
        if (near < 0) return 0;
        if (size_a == 3 || size_b == 3) near = std::max(near - 1, 0);
        return size_a < 2 || size_b < 2 ? near + 1 : near;
    }
    const int half = size_a / 2 + size_b / 2, across = std::max(dx - half, 0), down = std::max(dy - half, 0);
    return down < across ? down + across * 2 : across + down * 2;
}

// A unit's direction 0..63 from (x, y) to (tx, ty), subtiles (FUN_0064fdc0
// -> FUN_0064fc60): the smaller delta over the larger in 128ths picks an
// eighth of a quadrant (the table at 0x6eb7e0), folded into its octant.
// 0 faces +x+y (screen south), counting clockwise on screen.
inline int direction64(int x, int y, int tx, int ty) {
    static constexpr std::array<int, 7> kStep{ 13, 26, 39, 53, 68, 85, 105 };
    const int across = std::abs(tx - x), down = std::abs(ty - y);
    const bool steep = across <= down;
    const int major = steep ? down : across, minor = steep ? across : down;
    // 16.16 deltas, 127 x the smaller in 32 bits (it wraps past 258 subtiles).
    const int eighth = major == 0 ? 0 : std::clamp(int(std::uint32_t(minor) * 0x10000u * 0x7fu) / int(std::uint32_t(major) * 0x10000u), 0, 127);
    int dir = int(std::ranges::count_if(kStep, [&](int step) { return step <= eighth; }));
    if (!steep) dir = (-dir - 1) & 0xf;
    if (ty < y) dir = (-dir - 1) & 0x1f;
    return tx < x ? (dir + 8) & 0x3f : (((-dir - 1) & 0x3f) + 8) & 0x3f;
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

// Where Andariel's spray missile on SC frame `frame` is aimed, subtiles
// from her, facing `dir64` (FUN_005cb580): the radius-3 ring point that
// way (0x6e3188 into the DIR32 ring table FUN_0063e7e0), then frames 4..12
// sweep across it (0x6e3140, 99 = no offset; frame 8 aims at the ring point).
inline std::pair<int, int> andariel_spray_aim(int dir64, int frame) {
    static constexpr std::array<int, 32> kDx{ 0, -1, -1, -1, 0, 1, 1, 1, 0, -1, -2, -2, -2, -2, -2, -1, 0, 1, 2, 2, 2, 2, 2, 1, 0, -3, -3, -3, 0, 3, 3, 3 };
    static constexpr std::array<int, 32> kDy{ -1, -1, 0, 1, 1, 1, 0, -1, -2, -2, -2, -1, 0, 1, 2, 2, 2, 2, 2, 1, 0, -1, -2, -2, -3, -3, 0, 3, 3, 3, 0, -3 };
    static constexpr std::array<int, 8> kRing{ 29, 28, 27, 26, 25, 24, 31, 30 };
    static constexpr std::array<std::array<int, 9>, 8> kSweep{ {
        { 27, 14, 15, 3, 99, 7, 21, 22, 31 }, { 26, 12, 13, 2, 99, 6, 19, 20, 30 }, { 25, 10, 11, 1, 99, 5, 17, 18, 29 }, { 24, 8, 9, 0, 99, 4, 15, 16, 28 },
        { 31, 22, 23, 7, 99, 3, 13, 14, 27 }, { 30, 20, 7, 6, 99, 2, 1, 12, 26 }, { 29, 18, 19, 5, 99, 1, 9, 10, 25 }, { 28, 16, 17, 4, 99, 0, 23, 8, 24 } } };
    const auto octant = std::size_t(((dir64 + 4) >> 3) & 7);
    int x = kDx[std::size_t(kRing[octant])], y = kDy[std::size_t(kRing[octant])];
    if (const int sweep = kSweep[octant][std::size_t(std::clamp(frame - 4, 0, 8))]; sweep != 99) { x += kDx[std::size_t(sweep)]; y += kDy[std::size_t(sweep)]; }
    return { x, y };
}

// The per-type MonAI thinks (the MonAI table 0x73ca18 + AI * 16: {target
// search, init, think, ...}; FUN_005b15d0), each ending in one AI helper:
//   idle n       FUN_005de080: stand n frames
//   a1 / a2      FUN_005ddf90(4 / 5): attack the target
//   skill n      FUN_005dead0: Skill(n+1) in Sk(n+1)mode at the target
//   walk n       FUN_005dec80 / FUN_005ded40, flags n: walk at the target
//                (flags 2: can't set off, rand(100) < 70 wander 4, else stand 10)
//   run          FUN_005ded20 / FUN_005defb0: run at it
//   approach     FUN_005def80: walk at it
//   keep n x     FUN_005de6d0: walk to x subtiles off it, n at most
//   circle n x   FUN_005df7d0: one seed step (x: its low byte >= 0x80), walk round it
//   wander x y   FUN_005de200: walk to (x, y) subtiles off, four seed steps
//   none         already on its way (a back-off, FUN_005defe0 / FUN_005df140)
// A think's rand(100)s are one step each of the monster's seed (+0x20),
// drawn here in game.exe's order; a skill test with no skill (-1) draws
// nothing.
enum class MonAct : std::uint8_t { idle, a1, a2, skill, walk, run, approach, keep, circle, wander, none, untraced };
struct Think { MonAct act = MonAct::idle; int n = 0, x = 0, y = 0; };
struct ThinkIn {
    std::array<int, 8> aip{};                   // aip1..8 for its difficulty
    bool in_melee = false, got_hit = false;     // got hit: the last mode it left was GH (FUN_005dd2b0)
    int dist = 0;                               // AI distance to the target, subtiles
    int difficulty = 0, level = 0, life_pct = 100;
    std::array<bool, 3> skill{};                // Skill1..3 set
    int* state = nullptr;                       // the AI's scratch word (AI control +0x14)
};

inline bool traced_ai(std::string_view ai) {
    static constexpr std::array<std::string_view, 12> kTraced{ "Skeleton", "Zombie", "Bighead", "BloodHawk", "Brute", "Wraith", "Goatman",
                                                               "CorruptRogue", "QuillRat", "CorruptArcher", "CorruptLancer", "SkeletonBow" };
    return std::ranges::contains(kTraced, ai);
}

// FUN_005de200(r): one side r off, the other rand(r), on the seed's low
// bit; the next two low bits negate x, then y.
inline Think think_wander(Rng& rng, int r) {
    Think out{ MonAct::wander, r, r, r };
    if ((rng.next() & 1) == 0) out.x = rng(r); else out.y = rng(r);
    if (rng.next() & 1) out.x = -out.x;
    if (rng.next() & 1) out.y = -out.y;
    return out;
}
inline Think think_circle(Rng& rng, int n) { return { MonAct::circle, n, (rng.next() & 0xff) >= 0x80 ? 1 : 0 }; }
// FUN_005dec80 flags 2 when the walk can't set off.
inline Think walk_failed(Rng& rng) { return rng(100) < 70 ? think_wander(rng, 4) : Think{ MonAct::idle, 10 }; }

// `away(n, run)` backs off n subtiles from the target (FUN_005defe0 walking,
// FUN_005df140 running) and says whether it set off.
template <class Away>
Think mon_think(std::string_view ai, const ThinkIn& in, Rng& rng, Away&& away) {
    const auto& aip = in.aip;
    auto r = [&] { return rng(100); };
    auto a1_or_a2 = [&](int chance) { return Think{ r() < chance ? MonAct::a1 : MonAct::a2 }; };
    const Think idle2{ MonAct::idle, aip[1] }, walk{ MonAct::walk, 7 };
    // Skeleton (2, FUN_005efcf0; hellbovine too).
    if (ai == "Skeleton") {
        if (!in.in_melee) { if (r() < aip[0]) return walk; }
        else if (r() < aip[2]) return a1_or_a2(aip[3]);
        return idle2;
    }
    // Zombie (3, FUN_005efe20): runs at a foe it's hit by, or aip1 % one
    // within aip2; else wanders 3, bar in the Burial Grounds (level 17).
    if (ai == "Zombie") {
        if (in.in_melee) return a1_or_a2(aip[3]);
        if (!in.got_hit && !(in.dist < aip[1] && r() < aip[0]) && in.level != 17) return think_wander(rng, 3);
        return { MonAct::run };
    }
    // Bighead (4, FUN_005eff50): above aip1 % life it closes in (aip3 % a
    // shot within 15); below, it backs off under 3, walks in past 15, else
    // aip4 % shoots, aip2 % circles, else stands 10.
    // ponytail: the second target search (FUN_005ddc30) taken as its target.
    if (ai == "Bighead") {
        if (!in.in_melee && in.got_hit) return { MonAct::a2 };
        if (in.life_pct >= aip[0]) {
            if (in.in_melee) return { MonAct::a1 };
            if (in.dist < 15 && r() < aip[2]) return { MonAct::a2 };
            return walk;
        }
        if (in.dist < 3) return away(5, false) ? Think{ MonAct::none } : Think{ MonAct::a2 };
        if (in.dist > 15) return { MonAct::approach, 6 };
        if (r() < aip[3]) return { MonAct::a2 };
        if (r() >= aip[1]) return { MonAct::idle, 10 };
        return think_circle(rng, 3);
    }
    // BloodHawk (5, FUN_005f00e0): aip1 % a charge (state 1: a swing when
    // it lands in melee), else wanders (aip2 %: 4, else 3) past 3 subtiles;
    // in melee aip3 % a swing; else backs off 4, or swings.
    if (ai == "BloodHawk") {
        const bool charged = *in.state == 1;
        *in.state = 0;
        if (charged && in.in_melee) return { MonAct::a1 };
        if (!in.in_melee) {
            if (r() < aip[0]) { *in.state = 1; return { MonAct::walk, 0 }; }
            if (in.dist > 3) return think_wander(rng, r() < aip[1] ? 4 : 3);
        } else if (r() < aip[2]) return { MonAct::a1 };
        return away(4, false) ? Think{ MonAct::none } : Think{ MonAct::a1 };
    }
    // Brute (7, FUN_005efb80): walks in without a draw; in melee aip3 % a
    // swing (aip4 % A1), else aip3 % circles 4, else stands 15.
    if (ai == "Brute") {
        if (!in.in_melee) return walk;
        if (r() < aip[2]) return a1_or_a2(aip[3]);
        if (r() < aip[2]) return think_circle(rng, 4);
        return { MonAct::idle, 15 };
    }
    // Wraith (9, FUN_005f0a20) and Goatman (12, FUN_005f12a0): aip1 % close
    // in (the wraith drifts up to 12 subtiles), in melee aip3 % A1, else
    // stand aip2.
    if (ai == "Wraith" || ai == "Goatman") {
        if (!in.in_melee) { if (r() < aip[0]) return ai == "Wraith" ? Think{ MonAct::keep, 12, 0 } : walk; }
        else if (r() < aip[2]) return { MonAct::a1 };
        return idle2;
    }
    // CorruptRogue (10, FUN_005f0b00): runs at a target past 20 - 3 x
    // difficulty (FUN_00573930's difficulty); in melee aip3 % A1, else
    // aip1 % close in (aip5 % running).
    if (ai == "CorruptRogue") {
        if (in.dist > 20 - 3 * in.difficulty) return { MonAct::run };
        if (in.in_melee) return r() < aip[2] ? Think{ MonAct::a1 } : idle2;
        if (r() >= aip[0]) return idle2;
        return r() < aip[4] ? Think{ MonAct::run } : walk;
    }
    // QuillRat (14, FUN_005f1140): A1 in melee, spikes (A2) when hit;
    // wanders (aip4, at least 3) at aip1 or more; else aip2 % a back-off
    // of aip4 first, then the spikes.
    // ponytail: the leader's command (FUN_0058ee80) to shoot isn't sent.
    if (ai == "QuillRat") {
        if (in.in_melee) return { MonAct::a1 };
        if (in.got_hit) return { MonAct::a2 };
        const int roam = std::max(aip[3], 3);
        if (in.dist >= aip[0]) return think_wander(rng, roam);
        if (r() >= aip[1]) {
            if (away(aip[3] & 0xff, false)) return { MonAct::none };
            if (in.dist > 3) return think_wander(rng, roam);
        }
        return { MonAct::a2 };
    }
    // CorruptArcher (35, FUN_005f5a20): shoots a foe it's hit by; under 6,
    // aip4 % runs 12 off; past aip8, aip1 % walks in; past aip5 runs in;
    // else aip2 % a shot (Skill2 aip6 %, Skill3 aip7 %, else Skill1 or A1).
    // ponytail: its target search (FUN_005ddc30) is the think's target; with
    // none the driver doesn't call it, so the 50 % circle is left out.
    if (ai == "CorruptArcher") {
        if (!in.in_melee && in.got_hit) return { MonAct::a1 };
        if (in.dist < 6 && r() < aip[3] && away(12, true)) return { MonAct::none };
        if (aip[7] > 0 && aip[7] < in.dist && r() < aip[0]) return { MonAct::approach, aip[7] };
        if (aip[4] < in.dist) return { MonAct::run };
        if (r() >= aip[1]) return { MonAct::idle, aip[2] };
        if (in.skill[1] && r() < aip[5]) return { MonAct::skill, 1 };
        if (in.skill[2] && r() < aip[6]) return { MonAct::skill, 2 };
        return in.skill[0] ? Think{ MonAct::skill, 0 } : Think{ MonAct::a1 };
    }
    // CorruptLancer (36, FUN_005f5d50): runs in past aip5 (state 1: it
    // strikes on arrival); at range aip1 % closes in (aip4 % running);
    // in melee aip2 % (or on arrival) Skill1..3 at aip6..8 %, else A1.
    if (ai == "CorruptLancer") {
        if (aip[4] < in.dist) { *in.state = 1; return { MonAct::run }; }
        if (!in.in_melee) {
            if (r() >= aip[0]) return { MonAct::idle, aip[2] };
            return r() >= aip[3] ? Think{ MonAct::approach, 3 } : Think{ MonAct::run };
        }
        if (*in.state == 0 && r() >= aip[1]) return { MonAct::idle, aip[2] };
        *in.state = 0;
        for (int skill = 0; skill < 3; ++skill)
            if (in.skill[std::size_t(skill)] && r() < aip[std::size_t(5 + skill)]) return { MonAct::skill, skill };
        return { MonAct::a1 };
    }
    // SkeletonBow (37, FUN_005f6070): shoots a foe it's hit by; past 19,
    // aip3 % keeps aip5 off (aip4 at most a go), else stands 20; else aip1
    // % shoots, 20 % circles 3, else stands aip2.
    if (ai == "SkeletonBow") {
        if (in.got_hit) return { MonAct::a1 };
        if (in.dist > 19) return r() < aip[2] ? Think{ MonAct::keep, aip[3], aip[4] } : Think{ MonAct::idle, 20 };
        if (r() < aip[0]) return { MonAct::a1 };
        if (r() < 20) return think_circle(rng, 3);
        return idle2;
    }
    return { MonAct::untraced };
}

}  // namespace d2d::rules
