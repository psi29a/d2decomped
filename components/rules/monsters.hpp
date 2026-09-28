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
void boss_pack(const Monsters& monsters, int utype, int leader_x, int leader_y, SpawnRoom& room, Fits&& fits, std::vector<Spawn>& out, Population& pop) {
    using monster_detail::place;
    const auto& unique_type = monsters.types[std::size_t(utype)];
    auto boss = roll_boss(*pop.umods, unique_type, pop.difficulty, true, room.seed);
    const int leader = int(out.size());
    out.push_back({ utype, leader_x, leader_y, leader, -1, boss.kind, boss.mods, boss.name_seed });
    ++pop.uniques;
    int spot_x, spot_y;
    if (boss.kind == Boss::champion) {
        for (int remaining = room.seed(3) + 1; remaining > 0; --remaining)
            if (place(room, leader_x, leader_y, 4, fits, spot_x, spot_y)) out.push_back({ utype, spot_x, spot_y, leader, -1, Boss::champion, { umod::champion } });
    } else {
        const int minion_type = unique_type.minion[0] >= 0 ? unique_type.minion[0] : utype;
        for (int remaining = room.seed(4) + 3; remaining > 0; --remaining)
            if (place(room, leader_x, leader_y, 3, fits, spot_x, spot_y)) out.push_back({ minion_type, spot_x, spot_y, leader, -1, Boss::minion, {} });
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
            boss_pack(monsters, utype, leader_x, leader_y, room, fits, out, *pop);
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
        out.push_back({ type, leader_x, leader_y, leader });
        auto nearby = [&](int who, int radius) {
            int found_x, found_y;
            if (who >= 0 && std::size_t(who) < monsters.types.size() && place(room, leader_x, leader_y, radius, fits, found_x, found_y))
                out.push_back({ who, found_x, found_y, leader });
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

}  // namespace d2d::rules
