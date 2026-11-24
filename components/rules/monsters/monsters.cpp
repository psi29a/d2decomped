// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of monsters.hpp's functions; their notes are in the header.
#include "monsters.hpp"

#include "combat.hpp"
#include "item_gen.hpp"
#include "montypes.hpp"
#include "points.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::rules {

std::vector<Components> roll_components(const Components& choices, Rng& rng) {
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
Components monster_look(const std::vector<Components>* sets, const Components& choices, Rng& unit_seed) {
    if (sets && !sets->empty()) return (*sets)[std::size_t(unit_seed(int(sets->size())))];
    Components look{};
    for (std::size_t layer = 0; layer < 16; ++layer) look[layer] = std::uint8_t(choices[layer] < 1 ? 0 : unit_seed(choices[layer]));
    return look;
}

Region monster_region(const Monsters& monsters, const LevelMon& level_mon, int difficulty, Rng& seed) {
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

std::vector<ObjectGroupPick> place_object_groups(const LevelMon& level_mon,
                                                 const std::vector<ObjGroup>& obj_groups,
                                                 Rng& room_seed,
                                                 int rooms_populated_before,
                                                 int rooms_total,
                                                 const std::vector<std::uint8_t>* subclass) {   // objects.txt SubClass by Id: only those throttle
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

// A type by rarity from the region (FUN_005bde80) on `seed`.
int pick_type(const Region& reg, Rng& seed) {
    int pick = seed(reg.total) + 1;
    std::size_t index = 0;
    for (; index < reg.types.size(); ++index) { pick -= reg.types[index].second; if (pick < 1) break; }
    return reg.types[std::min(index, reg.types.size() - 1)].first;
}

int swing_max_damage(const MonStats& stats, std::string_view swing_mode) {
    if (swing_mode.empty()) return 0;
    int total = swing_mode == "A2" ? stats.a2_max : swing_mode == "SC" || swing_mode == "S1" ? stats.s1_max : stats.a1_max;
    for (const auto& element : stats.elements)
        if (element.type >= 0 && element.mode == swing_mode) total += element.type == 3 ? element.max * 10 >> 8 : element.max;
    if (swing_mode == "A1" || swing_mode == "A2")
        for (const auto& enchant : stats.enchants)
            if (enchant) total += enchant->second;
    return total;
}

// A classic game's swing (FUN_005a4f50 0x5a5112, game +0x70 0): past Normal
// a monster whose MonStats Align (+0x4c) isn't 1 swings for x 10 / 12 of its
// damage (min and max) and x 10 / 15 of its to-hit, truncated; its elements
// as they are, and no player-count growth (that's the expansion branch).
void classic_swing(MonStats& stats, int difficulty, int row_align) {
    if (difficulty == 0 || row_align == 1) return;
    for (int* damage : { &stats.a1_min, &stats.a1_max, &stats.a2_min, &stats.a2_max, &stats.s1_min, &stats.s1_max }) *damage = *damage * 10 / 12;
    for (int* to_hit : { &stats.to_hit, &stats.to_hit2, &stats.to_hit_s1 }) *to_hit = *to_hit * 10 / 15;
}

// A hostile monster's swing growth past Normal, in 128ths (FUN_005a4f20 on
// its stat 0x64, table 0x6e21f4): 8 a player past the first to 8, then n x 8 - 16.
int player_swing_128ths(int players) {
    static constexpr std::array<int, 9> kMore{ 0, 0, 8, 16, 24, 32, 40, 48, 56 };
    return players < 2 ? 0 : players < 9 ? kMore[std::size_t(players)] : players * 8 - 16;
}

// The player-count bonus in % (FUN_005738f0 life, table 0x6e1590;
// FUN_00573910 experience, 0x6e15b4): 50 a player past the first up to 8,
// then (n - 2) x 50 and (5n + 130) x 2.
int player_life_pct(int players) {
    static constexpr std::array<int, 9> kPct{ 0, 0, 50, 100, 150, 200, 250, 300, 350 };
    return players < 9 ? kPct[std::size_t(std::max(players, 0))] : (players - 2) * 50;
}

int player_exp_pct(int players) {
    static constexpr std::array<int, 9> kPct{ 0, 0, 50, 100, 150, 200, 250, 300, 350 };
    return players < 9 ? kPct[std::size_t(std::max(players, 0))] : (players * 5 + 130) * 2;
}

MonProps monster_props(const Tables& tables, const MonType& type, int difficulty, Rng& unit_seed) {
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

MonRanges monster_ranges(const Monsters& monsters, int type, int difficulty, int level) {
    if (type < 0 || std::size_t(type) >= monsters.types.size() || monsters.lvl.empty()) return {};
    const auto& type_info = monsters.types[std::size_t(type)];
    const auto index = std::size_t(std::clamp(difficulty, 0, 2));
    const auto& level_row = monsters.lvl[std::min<std::size_t>(std::size_t(std::max(level < 0 ? type_info.level[index] : level, 0)), monsters.lvl.size() - 1)];
    const auto& per_difficulty = type_info.diff[index];
    auto pct = [&](int base, int percent) { return type_info.no_ratio ? percent : base * percent / 100; };
    return { pct(level_row.hit_points[index], per_difficulty.min_hp), pct(level_row.hit_points[index], per_difficulty.max_hp),
             pct(level_row.damage[index], per_difficulty.a1_min), pct(level_row.damage[index], per_difficulty.a1_max) };
}

RevivedLife revive_life(const Monsters& monsters, int type, int difficulty, int level, int clvl, Rng& rng) {
    const auto range = monster_ranges(monsters, type, difficulty, level);
    RevivedLife revived{ int(std::uint32_t(range.min_hp + rng(range.max_hp - range.min_hp + 1)) << 8), level };
    if (level != 0 && clvl < level) revived = { std::max(mul_div(revived.life_fine, clvl, level), 1), clvl };
    return revived;
}

// The level a spawned monster starts at (FUN_00573cb0 0x573e15..0x573e70):
// its MonStats Level / Level(N) / Level(H); in an expansion game on
// Nightmare or Hell the area's Levels.txt MonLvl2Ex / MonLvl3Ex instead
// (FUN_0061a1b0 -> FUN_0061dca0), unless the row is noRatio (MonStats flag
// +0xc & 4) or boss (+0xc & 0x40: masks 0x6ce270 / 0x6ce280), which keep
// their own. `area_level` is that Levels.txt column of the area the unit is
// made in, -1 when the area isn't known (the MonStats level, as before).
int start_level(const MonType& type_info, int difficulty_index, int area_level) {
    if (area_level < 0 || difficulty_index == 0 || type_info.no_ratio || type_info.boss_column) return type_info.level[std::size_t(difficulty_index)];
    return area_level;
}

// FUN_00573cb0 (0x57401f..0x574052): stat 0x4a = the life in 256ths x DamageRegen >> 12, the life shifted first
// when it's over 0x7fffffff / DamageRegen; a DamageRegen with bits over 0xfff fails its assert (0x57401f).
// tools/emu/pet_regen.py runs it.
int monster_regen(int life, int damage_regen) {
    if (damage_regen <= 0 || damage_regen >= 0x1000) return 0;
    const int fine = life << 8;
    return 0x7fffffff / damage_regen < fine ? (fine >> 12) * damage_regen : damage_regen * fine >> 12;
}

// FUN_005a6920, a monster's unit event 3, each frame it's filed: life (stat 6) += stat 0x4a; over the max life
// (FUN_00625d10) the max, and the event removed. tools/emu/pet_regen.py runs it.
RegenFrame regen_frame(int life, int max_life, int regen) {
    const std::int64_t after = std::int64_t(life) + regen;
    return after > max_life ? RegenFrame{ max_life, false } : RegenFrame{ int(after), true };
}

MonStats monster_stats(const Monsters& monsters, int type, int difficulty, Rng& rng, int level_add, int players, int area_level) {
    MonStats stats;
    if (type < 0 || std::size_t(type) >= monsters.types.size()) return stats;
    const auto& type_info = monsters.types[std::size_t(type)];
    stats.prime_evil = type_info.prime_evil;
    if (type_info.align != 0) players = 1;
    const int difficulty_index = std::clamp(difficulty, 0, 2);
    // A MonStats Level of 0 is kept (stat 0xc as read, FUN_00573cb0 0x573e18..0x573e70)
    // and its life, defence and experience rolled at MonLvl row 0 (all 1s).
    const int base_level = start_level(type_info, difficulty_index, area_level);
    const int spawn_level = std::max(base_level, 0);
    stats.level = std::max(base_level + level_add, 0);
    if (monsters.lvl.empty()) return stats;
    // Life, defence and experience are read once, at the MonStats level
    // (FUN_00573cb0); leveladd (FUN_005a0e40) raises only the level stat
    // after it. To-hit and damage are read at the raised level, at the hit
    // (FUN_005a4f50, stat 12).
    const auto& spawn_row = monsters.lvl[std::min<std::size_t>(std::size_t(spawn_level), monsters.lvl.size() - 1)];
    const auto& level_row = monsters.lvl[std::min<std::size_t>(std::size_t(stats.level), monsters.lvl.size() - 1)];
    const auto& per_difficulty = type_info.diff[std::size_t(difficulty_index)];
    auto pct = [&](int base, int percent) { return type_info.no_ratio ? percent : base * percent / 100; };
    const int hit_points = spawn_row.hit_points[std::size_t(difficulty_index)];
    stats.hit_points  = rng.range(pct(hit_points, per_difficulty.min_hp), pct(hit_points, per_difficulty.max_hp));
    stats.hit_points = int(std::min<std::int64_t>(stats.hit_points + std::int64_t(stats.hit_points) * player_life_pct(players) / 100, 0x7fffff));
    stats.regen = monster_regen(stats.hit_points, type_info.damage_regen);   // a boss's mods come after
    stats.armor_class  = pct(spawn_row.armor_class[std::size_t(difficulty_index)], per_difficulty.armor_class);
    stats.to_hit  = pct(level_row.to_hit[std::size_t(difficulty_index)], per_difficulty.a1_th);
    stats.to_hit2 = pct(level_row.to_hit[std::size_t(difficulty_index)], per_difficulty.a2_th);
    stats.to_hit_s1 = pct(level_row.to_hit[std::size_t(difficulty_index)], per_difficulty.s1_th);
    stats.a1_min = pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.a1_min);
    stats.a1_max = std::max(pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.a1_max), stats.a1_min);
    stats.a2_min = pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.a2_min);
    stats.a2_max = std::max(pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.a2_max), stats.a2_min);
    stats.s1_min = pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.s1_min);
    stats.s1_max = std::max(pct(level_row.damage[std::size_t(difficulty_index)], per_difficulty.s1_max), stats.s1_min);
    stats.exp = pct(spawn_row.experience[std::size_t(difficulty_index)], per_difficulty.exp);
    stats.exp += int(std::int64_t(stats.exp) * player_exp_pct(players) / 100);
    for (std::size_t element = 0; element < 3; ++element) {
        const auto& element_info = per_difficulty.elements[element];
        // Every typed one keeps its mode and chance, which the swing rolls on
        // (roll_elements); one that does no damage here is type -2 (rand -3).
        if (type_info.el_type[element] == -1) continue;
        const int kind = type_info.el_type[element] >= 0 && element_info.max <= 0 ? -2 : type_info.el_type[element];
        stats.elements[element] = { kind, element_info.pct ? element_info.pct : 100, pct(level_row.damage[std::size_t(difficulty_index)], element_info.min),
                    std::max(pct(level_row.damage[std::size_t(difficulty_index)], element_info.max), pct(level_row.damage[std::size_t(difficulty_index)], element_info.min)), element_info.dur, type_info.el_mode[element] };
    }
    return stats;
}

// The AI's distance to a unit, subtiles (FUN_005dc530): half of the
// smaller delta plus twice the larger.
int ai_distance(int dx, int dy) {
    dx = std::abs(dx); dy = std::abs(dy);
    return dy < dx ? (dy + dx * 2) / 2 : (dx + dy * 2) / 2;
}

// A unique Fallen Shaman's corpse (FUN_005f1380, the callback of its room search FUN_005dd0b0): a dead monster of class 19 or 58, not
// flagged champion (bit 4) or unique (bit 8: FUN_005a0180 mask 0xc), within its aip4 by the squared distance (the limit is aip4 squared,
// 0x5f14c8..0x5f14d2). The corpse's own state (DD, the corpse bit, not raised) is the caller's.
bool shaman_corpse_reach(int base, bool champion, bool unique, int off_x, int off_y, int aip4) {
    return (base == 19 || base == 58) && !champion && !unique && off_x * off_x + off_y * off_y <= aip4 * aip4;
}

// A unit's distance to a point less `less` on each axis
// (FUN_005dc480 / FUN_005dc380 / FUN_006417f0): (max x 2 + min) / 2.
int point_apart(int dx, int dy, int less) {
    dx = std::max(std::abs(dx) - less, 0); dy = std::max(std::abs(dy) - less, 0);
    return (dx <= dy ? dx + dy * 2 : dy + dx * 2) / 2;
}

// Diablo's score of a player (FUN_005e8530, C division throughout): the
// resists ((dr + mr x 2) x 4 + fire + lightning + cold in reach) / 15; the
// reach (FUN_00622c40) 100 else a clear line (FUN_005dd290: FUN_00622aa0
// mask 4) 75, plus 100 when Diablo is past 0x55 from his point and the
// player inside it (FUN_005dc480), both x 5; (the hands' level x attackrank
// / 4, state 0xb 200, the max damages / 2) x 2; 300 under 20 % life; all
// / 22, at least 1.

// `cold_in_reach`: false for Baal's own copy (FUN_005fbb00, chooser_pick's
// point_far 0x4b), which counts cold whether or not the player is in reach
// (a mismatch traced against stock 1.14d).
int diablo_score(const DiabloPick& pick, int life_pct, bool reach, bool line_clear, bool point_bonus, bool cold_in_reach) {
    const int resists = ((pick.damage_resist + pick.magic_resist * 2) * 4 + (reach || !cold_in_reach ? pick.cold_resist : 0) + pick.light_resist + pick.fire_resist) / 15;
    const int closeness = (reach ? 100 : line_clear ? 75 : 0) + (point_bonus ? 100 : 0);
    const int ranked = (pick.left_level * pick.left_rank + pick.right_level * pick.right_rank) / 4;
    const int score = ((life_pct < 20 ? 100 : 0) * 3 + resists + closeness * 5 + (ranked + (pick.state_b ? 200 : 0) + pick.max_damage / 2) * 2) / 22;
    return score == 0 ? 1 : score;
}

// Distance between two units of sizes `size_a` / `size_b` (MonStats2
// SizeX; a player's 2), subtiles apart (FUN_00641530): close up, the
// 8x8 table at 0x6eb180 (one less when either is size 3), else the deltas
// less their half sizes, the smaller plus twice the larger.
int unit_distance(int dx, int dy, int size_a, int size_b) {
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
int unit_range(int dx, int dy, int size_a, int size_b) {
    const int half = size_a / 2 + size_b / 2, across = std::max(std::abs(dx) - half, 0), down = std::max(std::abs(dy) - half, 0);
    return (across <= down ? across + down * 2 : down + across * 2) / 2;
}

// A monster's door (FUN_005b0f50 → FUN_005dd0b0 mode 8, FUN_005dcd50):
// of the closed IsDoor objects about, `spots` their offsets from it in
// subtiles, the nearest by squared distance (FUN_005b0bd0) under 9, the
// first on a tie. Its index, or -1.
int door_pick(std::span<const std::pair<int, int>> spots) {
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
bool object_reach(int dx, int dy, int size, int size_x, int size_y) {
    if (unit_distance(dx, dy, size, size_x) == 0) return true;
    const int left = -(size_x / 2), top = -(size_y / 2);
    if (size_x < 1 || size_y < 1) return std::abs(dx - left) <= 1 && std::abs(dy - top) <= 1;
    if (dx < left - 2 || dx > left + size_x + 2 || dy < top - 2 || dy > top + size_y + 2) return false;
    if (size > 2 || (dy >= top - 1 && dy <= top + size_y + 1)) return true;
    return dx >= left - 1 && dx <= left + size_x + 1;
}

// A chase's check each frame of its move (FUN_00650840 -> FUN_006503f0, a
// path of type 2 / 0xd / 0xf): 0 stop when its target is within `stop`
// (+0x93: FUN_00649070, 0 for a walk or run) by unit_distance; 2 re-path
// when the target, a player or monster (`mover`), is over 5 subtiles on
// either axis from where it stood when pathed (`moved`: SP2 +0x14 less its
// spot now), or when the path has run out (`idx` +0x24 at `count` +0x28)
// short of its end (SP3 +0x18); else 1. With no target only the last test
// counts.

// A monster's re-path (FUN_00650350) is refused (0) when the budget
// (+0x94, 0x14 at each mode start: FUN_005a7c20 -> FUN_006490e0) is 0;
// else the points reached come off it (FUN_00649140, floor 0).
// tools/emu/moves.py checks it against game.exe.
int chase_check(bool target, int distance, int stop, bool mover, int moved_x, int moved_y, int idx, int count, bool at_end, int& budget) {
    const bool ran_out = idx >= count && !at_end;
    if (target && distance <= stop) return 0;
    if (target ? !((mover && (std::abs(moved_x) > 5 || std::abs(moved_y) > 5)) || ran_out) : !ran_out) return 1;
    if (budget == 0) return 0;
    budget = std::max(budget - idx, 0);
    return 2;
}

SearchPick search_pick(std::span<const SearchFoe> foes, int best, bool need_sight) {
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
    pick.nine = nine; pick.nine_best = nine_best;
    return pick;
}

// FUN_005b1900 inserts each pet right after its player's list head (0x5b1966..0x5b1979) and FUN_005dd7f0 takes
// only a strictly nearer candidate (0x5dd960), so on a tie the pet that entered last wins. A summon enters where
// it is made; the merc when it is hired or joins and again when it is revived (FUN_00579aa0 at 0x579b43).
// `serial` is the order they entered (greater is newer). With one player the pets after it are sorted newest first
// (equal serials keep d2d's order) and the rest (monsters) follow; with several players d2d's order stays.
// ponytail: a converted monster (FUN_005d0350 at 0x5d0501) is the caster's newest pet in game.exe; here it stays after the pets.
std::vector<std::size_t> search_order(std::span<const ListEntry> entries) {
    std::vector<std::size_t> order(entries.size());
    std::iota(order.begin(), order.end(), std::size_t(0));
    if (std::ranges::count(entries, ListKind::player, &ListEntry::kind) != 1) return order;
    const auto head = std::ranges::find(entries, ListKind::player, &ListEntry::kind) - entries.begin();
    const auto from = order.begin() + head + 1;
    const auto pets = std::stable_partition(from, order.end(), [&](std::size_t slot) { return entries[slot].kind == ListKind::pet; });
    std::stable_sort(from, pets, [&](std::size_t one, std::size_t other) { return entries[one].serial > entries[other].serial; });
    return order;
}

// A chest trap's Trap-Missile / Trap-Poison / Trap-Nova think
// (FUN_005fb5b0 / FUN_005fb900 / FUN_005fb9b0): FUN_005dd7f0 first picks
// the nearest player-list unit under the AI's default distance 35, then the
// think acts only when that picked unit is within aip1.

// A player entry starts each list and its pets follow it; search_pick
// carries the dead-player and list-skip rules.
int trap_target(std::span<const SearchFoe> foes, int reach) {
    const auto picked = search_pick(foes, 35, false);
    return picked.target >= 0 && picked.best <= reach ? picked.target : -1;
}

int near_distance(int dx, int dy, int size) {
    return ai_distance(std::max(std::abs(dx) - size, 0), std::max(std::abs(dy) - size, 0));
}

std::vector<int> room_order(std::span<const RoomUnit> units, std::span<const int> rooms) {
    std::vector<int> order;
    for (const int room : rooms) {
        const auto first = order.size();
        for (std::size_t i = units.size(); i-- > 0;)
            if (units[i].room == room && units[i].eligible) order.push_back(int(i));
        std::stable_sort(order.begin() + std::ptrdiff_t(first), order.end(), [&](int left, int right) { return units[std::size_t(left)].stamp > units[std::size_t(right)].stamp; });
    }
    return order;
}

NearPick search_near(std::span<const NearFoe> foes, int align, bool need_sight) {
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

SightPick search_sight(std::span<const SightFoe> foes) {
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

int search_threat(std::span<const SightFoe> foes, int skip, int& distance) {
    int best = -1;
    distance = 0x7fffffff;
    for (std::size_t i = 0; i < foes.size(); ++i) {
        const auto& foe = foes[i];
        if (int(i) == skip || !foe.enemy || (foe.invisible && foe.in_melee) || foe.distance >= 0x31 || foe.threat < 2 || foe.distance >= distance || foe.blocked) continue;
        best = int(i); distance = foe.distance;
    }
    return best;
}

// A unit's direction 0..63 from (x, y) to (tx, ty), subtiles (FUN_0064fdc0
// -> FUN_0064fc60): the smaller delta over the larger in 128ths picks an
// eighth of a quadrant (the table at 0x6eb7e0), folded into its octant.
// 0 faces +x+y (screen south), counting clockwise on screen.
int direction64(int x, int y, int target_x, int target_y) {
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

AndarielAct andariel_think(bool in_melee, const std::array<int, 8>& aip, Rng& rng) {
    if (in_melee) return rng(100) < aip[0] ? AndarielAct::spray : AndarielAct::melee;
    if (rng(100) < aip[1]) return AndarielAct::idle;
    if (rng(100) < aip[2]) return rng(100) < aip[3] ? AndarielAct::spray : AndarielAct::bolt;
    return AndarielAct::walk;
}

// Where Andariel's spray missile on SC frame `frame` is aimed, subtiles
// from her, facing `dir64` (FUN_005cb580): the radius-3 ring point that
// way (0x6e3188 into the DIR32 ring table FUN_0063e7e0), then frames 4..12
// sweep across it (0x6e3140, 99 = no offset; frame 8 aims at the ring point).
std::pair<int, int> andariel_spray_aim(int dir64, int frame) {
    static constexpr std::array<int, 32> kDx{ 0, -1, -1, -1, 0, 1, 1, 1, 0, -1, -2, -2, -2, -2, -2, -1, 0, 1, 2, 2, 2, 2, 2, 1, 0, -3, -3, -3, 0, 3, 3, 3 };
    static constexpr std::array<int, 32> kDy{ -1, -1, 0, 1, 1, 1, 0, -1, -2, -2, -2, -1, 0, 1, 2, 2, 2, 2, 2, 1, 0, -1, -2, -2, -3, -3, 0, 3, 3, 3, 0, -3 };
    const auto octant = std::size_t(((dir64 + 4) >> 3) & 7);
    int x = kDx[std::size_t(kRing[octant])], y = kDy[std::size_t(kRing[octant])];
    if (const int sweep = kSweep[octant][std::size_t(std::clamp(frame - 4, 0, 8))]; sweep != 99) { x += kDx[std::size_t(sweep)]; y += kDy[std::size_t(sweep)]; }
    return { x, y };
}

// A monster's skill level (FUN_00573cb0): Sk*lvl + DifficultyLevels
// MonsterSkillBonus (+0x10 via FUN_00573930).
int monster_skill_level(int sk_lvl, int difficulty) {
    static constexpr std::array<int, 3> kMonsterSkillBonus{ 0, 3, 7 };
    return sk_lvl + kMonsterSkillBonus[std::size_t(std::clamp(difficulty, 0, 2))];
}

Think think_wander(Rng& rng, int radius) {
    Think out{ MonAct::wander, radius, radius, radius };
    if ((rng.next() & 1) == 0) out.x = rng(radius); else out.y = rng(radius);
    if (rng.next() & 1) out.x = -out.x;
    if (rng.next() & 1) out.y = -out.y;
    return out;
}

// The Gargoyle Trap's shot (srvdofunc 93, FUN_005cc050), in subtiles from
// the trap at (x, y): the point square on at its target (on the axis it's
// nearer the target, its own spot moved up to 4 toward the target's; on
// the other, the target's), then the missile (FUN_0059fa30 flags 3) from
// a sixth of the way there less a subtile on both axes, flying along that
// same offset. {from_x, from_y, offset_x, offset_y}.
std::array<int, 4> gargoyle_shot(int x, int y, int target_x, int target_y) {
    auto toward = [](int from, int target) {
        for (int step = 0; step < 4; ++step) from += (from < target) - (from > target);
        return from;
    };
    const bool down = std::abs(target_x - x) < std::abs(target_y - y);
    const int off_x = (down ? toward(x, target_x) : target_x) - x, off_y = (down ? target_y : toward(y, target_y)) - y;
    return { x + off_x / 6 - 1, y + off_y / 6 - 1, off_x, off_y };
}

// The desert turret's fan (srvdofunc 105, FUN_005cd6a0): `count`
// missiles from (x, y) at the point (to_x, to_y), side by side 2
// subtiles apart across the way (by the offset's signs), centred on it.
// None when the point is its spot. {from x, from y, offset x, offset y}
// each.

// Each starts the offset / 6 out on both axes from the x offset
// (bugs.md), and flies the offset on (FUN_0059fa30 flags 3).
std::vector<std::array<int, 4>> turret_fan(int x, int y, int to_x, int to_y, int count) {
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

SearchMiss search_missed(std::string_view ai_name, bool has_walk, bool got_hit, bool on_0x40, int nearest) {
    const bool type_4_5 = ai_name == "SandMaggot" || ai_name == "FrogDemon";
    if (has_walk && ((!type_4_5 && got_hit) || on_0x40)) return { .wander = true };
    if (type_4_5) return { .stand = 20, .mode_kept = true };
    return { .stand = nearest < 25 ? 10 : nearest < 35 ? nearest - 10 : 25 };
}

Think countess_think(const ThinkIn& input, const CountessIn& where, std::span<const std::pair<int, int>> path, Rng& rng) {
    auto home = [&] {
        if (input.otherwise)
            input.otherwise->emplace_back([&input, where, path, &rng] { auto again = where; again.stuck = true; return countess_think(input, again, path, rng); });
        return Think{ MonAct::home };
    };
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
    if (where.away && !where.stuck) return home();
    if (where.target_away && where.at_home) {
        if (input.dist < 25 && firewall(out)) return out;
        return { MonAct::idle, 10 };
    }
    if ((where.target_away || input.home_dist > 40) && !where.stuck) return home();
    if (firewall(out)) return out;
    const int roll = rng(100);
    if (input.in_melee) return roll < input.aip[2] + 10 ? Think{ MonAct::a1 } : Think{ MonAct::idle, input.aip[1] };
    return roll < input.aip[0] ? Think{ MonAct::run } : Think{ MonAct::idle, input.aip[1] };
}

// On that row's think already with an other think (the row's fourth word)
// it swaps to that, ctrl[0] = special; else the scratch words
// (+0x14..+0x1c) are cleared and the row's think set (its init run). Only
// a MonAI row has an other think, so it's reached by toggling 0 on a
// monster on its own think: an AI-swapping state (Dim Vision, Terror,
// Cloak of Shadows) whose end (FUN_005c3370, on expiry or when another
// curse takes its place, FUN_006269f0) comes while it's back on its own.
// True on a full toggle, where FUN_0058ede0 also frees the AI command list
// (AI control +0x20: Monster::ai_command, the caller's). The inits (row
// +4) a toggle can reach: a monster swaps only as game::swaps_ai allows
// (it walks, SwitchAI, not unique), and of the MonAI rows with an init
// (0x73ca18) only Imp's walks with SwitchAI in 1.14d MonStats (the rest
// are summons, the act 5 barbarians, or don't walk). Its FUN_005e2fd0 sets
// +0x14 to -1, which the Imp's inputs (game/ai/act5.cpp) take for 0;
// special 16's (FUN_005e2cd0) is imp_ride_pick, run by its caller; special
// 11's is terror_think's note; 10 and 12 have none.
bool ai_toggle(int& special, bool& other, std::array<int*, 3> scratch, int next, bool has_other) {
    if (next == 0 && special == 0 && !other && has_other) { other = true; return false; }
    special = next; other = false;
    for (int* word : scratch) *word = 0;
    return true;
}

// The riding imp's init (special 16's, FUN_005e2cd0): one draw on its seed;
// slot 4 (Skill5, Inferno) on an even draw, else slot 2 (Skill3, Fireball)
// (0x6e34e0 {2, 4}), the other when it lacks that one, 0 when it has neither.
int imp_ride_pick(Rng& rng, bool has_fireball, bool has_inferno) {
    const bool inferno_first = (rng.next() & 1) == 0;
    if (inferno_first ? has_inferno : has_fireball) return inferno_first ? 4 : 2;
    if (inferno_first ? has_fireball : has_inferno) return inferno_first ? 2 : 4;
    return 0;
}

// Leader death and minion pack promotion (FUN_0058f530 / FUN_0058f6c0):
// When a pack's leader dies, the first living minion in the group is elected
// as the new leader, updating siblings. Returns the elected leader ID or -1 if empty.
int ai_minion_elect_leader(std::span<const int> live_minions) {
    return live_minions.empty() ? -1 : live_minions.front();
}

void ancient_flag_apply(std::uint32_t& flags, std::uint32_t mask, bool turn_on) {
    if (turn_on) flags |= mask;
    else flags &= ~mask;
}

// Generic spawner initialization (FUN_005e6190): nests/sarcophagus init;
// sets spawn_class to -1 (none until assigned).
int generic_spawner_init() {
    return -1;
}

// HighPriest other think (FUN_005e0400): before cooldown frame (+0x14 < frame)
// yields/waits (event 0); once reached, stands 15 frames.
Think high_priest_other_think(int cooldown_frame, int current_frame) {
    if (cooldown_frame < current_frame) return { MonAct::none };
    return { MonAct::idle, 15 };
}

// Act 5 minion melee think (FUN_005e2610): out of melee, walk at target (89%);
// in melee, A2 attack at target (95%); else stand 10 frames.
Think minion_melee_think(bool in_melee, Rng& rng) {
    if (!in_melee) {
        if (rng(100) < 89) return { MonAct::walk };
    } else {
        if (rng(100) < 95) return { MonAct::a2 };
    }
    return { MonAct::idle, 10 };
}

// MonAI rows with an other think (the fourth word, table 0x73ca18 + ai x
// 16): 15 SandMaggot, 29 BatDemon, 51 Diablo, 52 FrogDemon, 65 FetishShaman;
// Diablo's FUN_005e8480 also 147 UberDiablo's, the same code FUN_005fcf30
// 135 BaalCrab's, 140 BaalCrabClone's and 145 UberBaal's, and FUN_005e5280
// (that less the kept point's restore, which no toggle of d2d's drops) 61
// Hireable's, 128 Nihlathak's and 152's. A boss reaches one when Cloak of
// Shadows' callback toggles it (monster-ai.md "Curse thinks").
bool is_diablo_other(std::string_view ai_name) {
    return ai_name == "Diablo" || ai_name == "UberDiablo" || ai_name == "BaalCrab" || ai_name == "BaalCrabClone" || ai_name == "UberBaal"
        || ai_name == "Hireable" || ai_name == "Nihlathak";
}

// Dim Vision's and Cloak of Shadows' think (special AI 10, FUN_005e8020;
// search type 1): in melee with a swing it swings (A1, mode 4); else 20 %
// it wanders 3, else stands 10.
Think dim_vision_think(const ThinkIn& input, Rng& rng) {
    if (input.in_melee && input.swings) return { MonAct::a1 };
    if (rng(100) < 20) return think_wander(rng, 3);
    return { MonAct::idle, 10 };
}

// BatDemon's (FUN_005f4fd0): in S4 it waits 1 (state 1); at state 4 it rises
// (S3, FUN_005ddf90(10, none); state 1); else it toggles back
// (FUN_005b0e00(ctrl, ctrl[0])) and stands 1. SandMaggot's (FUN_005f1750):
// with its AI command 14's +0x18 at 1 and a Skill1 it casts Skill1 at the
// target (event 30; the word 0); else it toggles back keeping the third
// scratch word, event 1. Nothing sets that word (stubs.md: FUN_005f1750's
// cast branch), so it stays 0.
Think other_think(std::string_view ai_name, const ThinkIn& input) {
    // Diablo's (FUN_005e8480): state 0xc (his lightning's) off, back to his
    // own think (FUN_005b0e00), stands 1; his kept point stays.
    if (is_diablo_other(ai_name)) {
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
    // else toggled back, waits 1. The special test never fails: an other
    // think is swapped to only on toggle 0 (the special rows, FUN_005b15d0's
    // 0x73d358, have no fourth word), so ctrl[0] is 0 while it runs.
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

// The AI driver's first step (FUN_005b10e0 -> FUN_005b0ff0), on the unit
// its AI control names at +0xc / +0x10. FUN_005dd330 sets them: a unique's
// minions their leader (FUN_005a0c00, 0x5a0cb9), a revived monster its owner
// (FUN_005c55c0, 0x5c561b), item_reanimate's (FUN_005c07a0, 0x5c08ca) and
// Taunt's (FUN_005d8570) their player. None (+0xc -1):
// nothing. The monster without a WL mode (FUN_0046c140(row, 2)), the leader
// not found (FUN_00552f60) or dead or dying (FUN_005541b0): +0xc cleared, so
// never again. Within 1 (FUN_005dc380) of a player (+0x10 0): a back-off of
// 0x13 from it (FUN_005defe0); that failing, the walk below. Over 0x14: the
// path set (FUN_005de190(7, 0, 0x28)) and a walk to a spot 0x13 about it
// (FUN_005df530). Either way the step acted and the search and think wait.
LeaderStep leader_follow(int leader_id, int leader_type, bool can_walk, bool found, bool dying, int gap, bool backed) {
    LeaderStep step;
    if (leader_id == -1) return step;
    if (!can_walk || !found || dying) { step.cleared = true; return step; }
    if (gap <= 1 && leader_type == 0) {
        step.back = true;
        step.acted = true;
        step.walk = !backed;
        return step;
    }
    if (gap > 0x14) step.acted = step.path = step.walk = true;
    return step;
}

// FUN_005df530 (0x5df5b9..0x5df634): a draw's low bit picks the axis that
// gets the whole reach, the other rand(reach) (FUN_0045c3e0); a draw each
// for the x and y signs.
std::pair<int, int> spot_about(Rng& seed, int x, int y, int reach) {
    int off_x = 0, off_y = 0;
    if ((seed.next() & 1) == 0) { off_x = seed(reach); off_y = reach; }
    else { off_x = reach; off_y = seed(reach); }
    if (seed.next() & 1) off_x = -off_x;
    if (seed.next() & 1) off_y = -off_y;
    return { x + off_x, y + off_y };
}

// A MonStats row's walk and run animation base (+0x36 / +0x38), as the loader
// derives them (FUN_00650f00, row by row in table order; a BaseId out of the
// table is the row itself). Walk: its BaseId row's AnimData WL speed
// (FUN_0066a9b0, default weapon class) x its Velocity / the base row's. Run:
// from the base row's walk base as it stands then / 2 (0 for a base row not yet
// reached; under row 0x19a) or the base row's AnimData RN speed (0x19a on),
// x Run / the base row's Run. Neither is scaled when the base row is the row
// itself or the base row's value isn't above 0. Unsigned 32-bit divides; each
// held to 0x7fff (0x650f23..0x651029).
int monster_walk_base(bool own_base, int base_walk_speed, int velocity, int base_velocity) {
    std::uint32_t walk = std::uint32_t(base_walk_speed);
    if (!own_base && std::int16_t(base_velocity) > 0) walk = std::uint32_t(std::int16_t(velocity) * base_walk_speed) / std::uint32_t(std::int16_t(base_velocity));
    return int(std::min<std::uint32_t>(walk, 0x7fff));
}

int monster_run_base(bool own_base, int start, int run, int base_run) {
    std::uint32_t running = std::uint32_t(start);
    if (!own_base && std::int16_t(base_run) > 0) running = std::uint32_t(std::int16_t(run) * start) / std::uint32_t(std::int16_t(base_run));
    return int(std::min<std::uint32_t>(running, 0x7fff));
}

// A monster's animation rate in a mode, the speed word FUN_00623f50 stores at
// unit +0x4c (type 1). In order: a cast (SC) at min(100 + effective FCR, 175)
// %; a block (BL) at 50 (100 in state 0x65) + effective FBR %, at least 1; a
// get-hit (GH) at 50 + effective FHR %; a knockback (KB) at the walk base; a
// moving mode (FUN_006214a0: WL, RN; S1..S4 too for a class under 0x19a that
// isn't a MonStats npc) at the walk base (the run base in RN) x max(stat 0x43 +
// effective FRW, 25) %, truncated toward 0; an attack (FUN_00621580: A1, A2;
// S1..S4 and SQ with UseAttackRate) at effective IAS + stat 68, 15..175 %; DT
// and DD (FUN_00621630) at the speed; any other at stat 69, 15..175 %. The
// effective stats are FUN_00621740's 120 v / (120 + v) (FRW 150). The rest
// multiply as unsigned 32-bit and divide by 100; a rate of 0 or below is 0,
// over 0x7fff is 0x7fff.
int monster_mode_rate(const ModeRateIn& rate_in) {
    const auto stored = [](std::int64_t rate) { return int(std::clamp<std::int64_t>(rate, 0, 0x7fff)); };
    const auto times = [&](int pct) { return std::int64_t(std::uint32_t(rate_in.speed) * std::uint32_t(pct) / 100u); };   // mul / shr: unsigned
    constexpr int kSc = 7, kGh = 3, kBl = 6, kKb = 13, kRn = 15;
    const bool skill_mode = rate_in.mode >= 8 && rate_in.mode <= 11, sequence = rate_in.mode == 14;
    if (rate_in.mode == kSc) return stored(times(std::min(100 + effective_speed(rate_in.fcr), 175)));
    if (rate_in.mode == kBl) return stored(std::max<std::int64_t>(times((rate_in.blocking ? 100 : 50) + effective_speed(rate_in.fbr)), 1));
    if (rate_in.mode == kGh) return stored(times(50 + effective_speed(rate_in.fhr)));
    if (rate_in.mode == kKb) return stored(rate_in.base.walk);
    const bool moving = rate_in.mode == 2 || rate_in.mode == kRn || (!rate_in.npc && rate_in.low_class && skill_mode);
    if (moving) {
        const int pct = std::max(rate_in.velocity_pct + effective_speed(rate_in.frw, 150), 25);
        return stored(std::int64_t(rate_in.mode == kRn ? rate_in.base.run : rate_in.base.walk) * pct / 100);   // imul, sar: toward 0
    }
    if (rate_in.mode == 4 || rate_in.mode == 5 || ((skill_mode || sequence) && rate_in.use_attack_rate))
        return stored(times(std::clamp(effective_speed(rate_in.ias) + rate_in.attack_rate, 15, 175)));
    if (rate_in.mode == 0 || rate_in.mode == 12) return stored(rate_in.speed);
    return stored(times(std::clamp(rate_in.other_rate, 15, 175)));
}


}
