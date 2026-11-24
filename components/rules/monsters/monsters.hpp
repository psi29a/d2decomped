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
#include <functional>
#include <optional>
#include <span>
#include <string>
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
std::vector<Components> roll_components(const Components& choices, Rng& rng);

// A monster's look as it's made, the first roll of its unit seed.
Components monster_look(const std::vector<Components>* sets, const Components& choices, Rng& unit_seed);

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
Region monster_region(const Monsters& monsters, const LevelMon& level_mon, int difficulty, Rng& seed);

// An item a monster wears from the start (FUN_005d6b60 as it's made): its
// code, body slot, quality and item level, and the two game seed steps
// FUN_00573b20 -> FUN_00559ce0 took for it (its unit and item seeds).
struct SpawnGear {
    std::string code;
    int loc = 0, quality = 0, ilvl = 1;
    std::uint32_t unit_seed = 0, item_seed = 0;
};

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
    std::vector<SpawnGear> gear;                              // its MonEquip items
    bool preset = false;                                      // made by its room's preset units (FUN_005559a0), not its populate
};

// A level's population so far (monster region +4 rooms done, +0xc rooms
// in the level, +0x2c8 uniques made) and what its uniques roll with.
struct Population {
    int rooms_done = 0, rooms_total = 0, uniques = 0;
    int umin = 0, umax = 0;                                   // Levels MonUMin / MonUMax for the difficulty
    int difficulty = 0;
    const UMods* umods = nullptr;
    int wanderers = 0;                                        // +0x2c4: FUN_0054ef50's made
};

// FUN_0054f060's wanderer: a room seed step with its low 15 bits clear, on a
// level with MonWndr (Levels +0x2e), draws rand(100) (FUN_0054eff0) and is
// due under 3 with fewer than 3 made in the level's monster region (+0x2c4).
inline bool wanderer_due(Rng& seed, bool wander_level, int made) {
    if ((seed.next() & 0x7fff) != 0 || !wander_level) return false;
    return seed(100) < 3 && made < 3;
}

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
std::vector<ObjectGroupPick> place_object_groups(const LevelMon& level_mon,
                                                 const std::vector<ObjGroup>& obj_groups,
                                                 Rng& room_seed,
                                                 int rooms_populated_before = 0,
                                                 int rooms_total = 0,
                                                 const std::vector<std::uint8_t>* subclass = nullptr);

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

// A type by rarity from the region on `seed`.
int pick_type(const Region& reg, Rng& seed);

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
// (FUN_00483360: truncated), life capped at 0x7fffff (FUN_00573cb0); not
// for a MonStats row with an Align (+0x4c: FUN_00573930 skips the bonus).
struct MonStats {
    int level = 1, hit_points = 1, armor_class = 0, to_hit = 0, a1_min = 0, a1_max = 0, a2_min = 0, a2_max = 0, exp = 0;
    int s1_min = 0, s1_max = 0;
    int to_hit2 = 0, to_hit_s1 = 0;            // A2TH's, S1TH's: the hit's mode's (FUN_005a4f50 flags 0x10 / 0x20)
    // A missile whose row has ToHit 0 (Missiles.txt +0x18b): FUN_005adf10
    // skips FUN_0057d9b0's roll; block and avoid still come (FUN_005ad730).
    bool no_hit_roll = false;
    // A shot whose physical record is 0 (FUN_005a89a0: min and max both over 0, else 0): FUN_005ad730 passes can-block 0 to FUN_0057dfb0,
    // which then skips the block roll (the defender's other rolls still come).
    bool unblockable = false;
    int regen = 0;                            // stat 0x4a: life a frame, 256ths (DamageRegen, FUN_00573cb0)
    struct El { int type = -1, pct = 0, min = 0, max = 0, dur = 0; std::string_view mode; };
    std::array<El, 3> elements{};
    // Its own poison stats 57 / 58 / 59 (MonUMod poison hit, FUN_005a1e00),
    // on every hit it lands (FUN_0057b7d0): per tick in 256ths, ticks.
    int poison_min = 0, poison_max = 0, poison_len = 0;
    // A boss's enchants (MonUMod fire / lightning / cold, ghostly's cold): fire, lightning and cold
    // min / max on its own stats, apart from a swing's (FUN_005a4f50 sets those on the hit's list),
    // so each comes on every A1 / A2 blow; the cold's length in frames.
    std::array<std::optional<std::pair<int, int>>, 5> enchants{};   // by element type: fire, lightning, cold, (poison: its own), magic
    int enchant_len = 0;
    bool prime_evil = false;                  // MonStats primeevil (+0xc & 0x80, FUN_0063edc0): FUN_0057c060's pair percent
};
// The max damage a monster's last swing left in its stats (FUN_005a4f50,
// at each attack mode: A2's for A2, S1's for SC / S1, A1's otherwise; each
// element of that mode as its own stat, poison x 10), as Diablo's score
// sums it (FUN_005e8530: maxdamage, fire / lightning / magic / cold max,
// poison max >> 8). 0 before any swing.
// Only the elements the swing rolled in count (Monster::swing_stats).
// The player-count growth past Normal is in Monster::swing_stats.
int swing_max_damage(const MonStats& stats, std::string_view swing_mode);

// A classic game's swing.
void classic_swing(MonStats& stats, int difficulty, int row_align);

// A hostile monster's swing growth past Normal, in 128ths: 8 a player past the first to 8, then n x 8 - 16.
int player_swing_128ths(int players);
// The player-count bonus in %: 50 a player past the first up to 8, then (n - 2) x 50 and (5n + 130) x 2.
int player_life_pct(int players);
int player_exp_pct(int players);

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
MonProps monster_props(const Tables& tables, const MonType& type, int difficulty, Rng& unit_seed);

// FUN_006538a0's values before monster_stats' roll, at the type's own
// Level (or `level`): life min / max, A1 min / max (the skill tooltips'
// summon lines, game::skill_text; Revive's life, revive_life).
struct MonRanges { int min_hp = 0, max_hp = 0, a1_min = 0, a1_max = 0; };
MonRanges monster_ranges(const Monsters& monsters, int type, int difficulty, int level = -1);

// Revive (do 58, FUN_005c56c0 0x5c5776..0x5c5830) raises the corpse with a
// life rolled at its own level: FUN_006538a0's range there, min + rand(max -
// min + 1) on its unit seed (FUN_0045c3e0), in 256ths. When that level is
// above the owner's (and not 0), the life becomes MulDiv(life, clvl, level)
// (FUN_00483360), 1 at least, and the level the owner's. FUN_005c4470's
// calc1 % comes after.
struct RevivedLife { int life_fine = 0, level = 0; };
RevivedLife revive_life(const Monsters& monsters, int type, int difficulty, int level, int clvl, Rng& rng);

// The level a spawned monster starts at.
int start_level(const MonType& type_info, int difficulty_index, int area_level);

MonStats monster_stats(const Monsters& monsters, int type, int difficulty, Rng& rng, int level_add = 0, int players = 1, int area_level = -1);

// The AI's distance to a unit, subtiles: half of the smaller delta plus twice the larger.
int ai_distance(int dx, int dy);

// A unique Fallen Shaman's corpse.
bool shaman_corpse_reach(int base, bool champion, bool unique, int off_x, int off_y, int aip4);

// A unit's distance to a point less `less` on each axis: (max x 2 + min) / 2.
int point_apart(int dx, int dy, int less);

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

// Diablo's score of a player.
int diablo_score(const DiabloPick& pick, int life_pct, bool reach, bool line_clear, bool point_bonus, bool cold_in_reach = true);

inline constexpr std::array<std::array<int, 8>, 8> kNear{ {
    { -1, -1, -1, 0, 2, 4, 6, 8 }, { -1, -1, 0, 1, 2, 4, 6, 8 }, { -1, 0, 0, 2, 3, 5, 7, 8 }, { 0, 1, 2, 2, 4, 5, 7, 8 },
    { 2, 2, 3, 4, 5, 6, 7, 9 }, { 4, 4, 5, 5, 6, 7, 8, 9 }, { 6, 6, 7, 7, 7, 8, 10, 10 }, { 8, 8, 8, 8, 9, 9, 10, 11 } } };
// Distance between two units of sizes `size_a` / `size_b` (MonStats2 SizeX.
int unit_distance(int dx, int dy, int size_a, int size_b);

// The plain distance.
int unit_range(int dx, int dy, int size_a, int size_b);

// A monster's door.
int door_pick(std::span<const std::pair<int, int>> spots);

// Can a unit of `size` (MonStats2 SizeX) operate an object of SizeX / SizeY `size_x` / `size_y`, `dx`, `dy`
// subtiles from it?
bool object_reach(int dx, int dy, int size, int size_x, int size_y);

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
// DAT_006f1648: the three ways back toward the start a blocked end tries
// (FUN_00678df0, by FUN_00678c10's 5x5 index; 0xff: no third). Straight
// ways only, unlike kTry.
inline constexpr std::array<std::array<int, 3>, 25> kBackTry{ { { 4, 6, 0xff }, { 4, 6, 0xff }, { 4, 2, 6 }, { 4, 2, 0xff }, { 4, 2, 0xff },
    { 6, 4, 0xff }, { 4, 6, 0xff }, { 4, 2, 6 }, { 4, 2, 0xff }, { 2, 4, 0xff }, { 6, 0, 4 }, { 6, 0, 4 }, { 6, 0, 4 }, { 2, 0, 4 }, { 2, 0, 4 },
    { 6, 0, 0xff }, { 0, 6, 0xff }, { 0, 2, 6 }, { 2, 0, 0xff }, { 2, 0, 0xff }, { 0, 6, 0xff }, { 0, 6, 0xff }, { 0, 2, 6 }, { 0, 2, 0xff },
    { 0, 2, 0xff } } };

// A path's blocked end walked back toward its start (FUN_00648120, which
// FUN_00649970 runs for a path type with flag 0x1000, PATHTYPEFLAGS
// 0x6eb690: types 0, 1 and 7, when FUN_0064d910 finds the end blocked).
// Three spots leave the end: one steps the first way back each round (the
// way read anew from where it stands), the other two keep stepping the
// second and third ways of the first read. The first free one is the new
// end. False (no path at all) when the first spot reaches the start with
// none free, or the free one is the start. A player's end is then pushed
// on (FUN_00648050): while it's under 5 off the start on both axes, a step
// along the longer axis from the start (both on a diagonal: the table
// 0x6eb490) up to 4 off, stopping at the first blocked.
// tools/emu/walk_back.py; tests/test_walk_back.cpp.
template <class Blocked>
bool end_walk_back(int x, int y, int& to_x, int& to_y, bool player, Blocked&& blocked) {
    auto tries = [&](int from_x, int from_y) -> const std::array<int, 3>& {   // FUN_00678c10
        const int dx = x - from_x, dy = y - from_y, adx = std::abs(dx), ady = std::abs(dy);
        int aim_x = dx, aim_y = dy;
        if (adx >= ady * 2) aim_y = dy < 0 ? -1 : dy & 1;
        else if (adx * 2 <= ady) aim_x = dx < 0 ? -1 : dx & 1;
        return kBackTry[std::size_t(std::clamp(aim_x, -2, 2) * 5 + 12 + std::clamp(aim_y, -2, 2))];
    };
    const auto first = tries(to_x, to_y);
    int way = first[0];
    std::pair<int, int> lead{ to_x, to_y }, second = lead, third = lead, found = lead;
    auto step = [](std::pair<int, int>& spot, int dir) { spot.first += kDir[std::size_t(dir)][0]; spot.second += kDir[std::size_t(dir)][1]; };
    for (;;) {
        if (lead == std::pair{ x, y }) return false;
        step(lead, way);
        if (!blocked(lead.first, lead.second)) { found = lead; break; }
        step(second, first[1]);
        if (!blocked(second.first, second.second)) { found = second; break; }
        if (first[2] != 0xff) {
            step(third, first[2]);
            if (!blocked(third.first, third.second)) { found = third; break; }
        }
        way = tries(lead.first, lead.second)[0];
    }
    std::tie(to_x, to_y) = found;
    if (found == std::pair{ x, y }) return false;
    if (!player) return true;
    const int dx = to_x - x, dy = to_y - y, adx = std::abs(dx), ady = std::abs(dy);
    if (adx >= 5 || ady >= 5) return true;
    const int push_x = adx >= ady ? (dx > 0) - (dx < 0) : 0, push_y = ady >= adx ? (dy > 0) - (dy < 0) : 0;
    int at_x = to_x, at_y = to_y;
    for (int off = std::max(adx, ady); off < 5; ++off) {
        at_x += push_x; at_y += push_y;
        if (blocked(at_x, at_y)) break;
        to_x = at_x; to_y = at_y;
    }
    return true;
}

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

// A critter's walk on the client (FUN_004aff60 command 1 -> FUN_004804a0 ->
// FUN_00649970): its path type 2 from FUN_00649d00, so the toward pather
// alone (FUN_00679c80, near 0 at a spot), with no wall pather after it (that's
// the server's FUN_005a6290). None at its own subtile or over 100 off on an
// axis; leading points on its own subtile dropped (FUN_0064fe40). A clear line
// is one point however far; `steps` (path +0x91: 0xe from FUN_00649d00, a
// bird's 0x4d from FUN_00648e70(path, 0x5a)) counts only past a wall.
template <class Blocked>
std::vector<std::pair<int, int>> critter_path(int x, int y, int to_x, int to_y, int steps, Blocked&& blocked) {
    if ((to_x == x && to_y == y) || std::abs(to_x - x) > 100 || std::abs(to_y - y) > 100) return {};
    auto points = toward_path(x, y, to_x, to_y, steps, 0, blocked);
    points.erase(points.begin(), std::ranges::find_if(points, [&](const std::pair<int, int>& point) { return point != std::pair(x, y); }));
    return points;
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

// A chase's check each frame of its move.
int chase_check(bool target, int distance, int stop, bool mover, int moved_x, int moved_y, int idx, int count, bool at_end, int& budget);

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
// with a best, FUN_005dd510 keeps it unless list 9's is under 6 away and
// there's no path to the best: the caller's, with `nine` / `nine_best`).
// tools/emu/search.py checks this against game.exe.
struct SearchFoe { int distance = 0; bool pet = false, away = false, dead = false, blocked = false; int list = 0; };
struct SearchPick { int target = -1, best = 0, nearest = 0x7fffffff, nine = -1, nine_best = 0x7fffffff; };
SearchPick search_pick(std::span<const SearchFoe> foes, int best, bool need_sight);

// A chest trap's Trap-Missile / Trap-Poison / Trap-Nova think.
int trap_target(std::span<const SearchFoe> foes, int reach);

// FUN_00650d70: friends by alignment (stat 0xac: 0 evil, 1 neutral, 2 good).
// Evil befriends evil, good good, neutral nobody. FUN_00554200's enemies
// are the rest, owners first (a pet is its player's: good).
inline bool friends(int mine, int theirs) { return mine == 0 ? theirs == 0 : mine == 2 && theirs == 2; }
// A monster's own unit alignment from its MonStats Align: 1 (good) is 2, 2 (neutral) is 1, else 0 (evil).
constexpr int unit_alignment(int row_align) { return row_align == 1 ? 2 : row_align == 2 ? 1 : 0; }

// FUN_005dc380: ai_distance less the candidate's size on each axis (FUN_00620510).
int near_distance(int dx, int dy, int size);
// The AI driver's first step FUN_005b0ff0: what a monster whose AI control names a leader does (rules::leader_follow).
struct LeaderStep { bool acted = false, cleared = false, back = false, path = false, walk = false; };
[[nodiscard]] LeaderStep leader_follow(int leader_id, int leader_type, bool can_walk, bool found, bool dying, int gap, bool backed);
// FUN_005df530: a spot `reach` about (x, y) on the unit's seed (one axis reach, the other rand(reach), random signs).
[[nodiscard]] std::pair<int, int> spot_about(Rng& seed, int x, int y, int reach);

// FUN_005dd0b0 mode 1 (FUN_005dce60): the eligible units in the near
// rooms' order, each room's units newest first; `units` is oldest first.
// `stamp`: when the unit came into its room (UnitState::room_stamp), its
// list's head the latest; units of one stamp (0: none kept) newest index first.
struct RoomUnit { int room = -1; bool eligible = false; std::uint32_t stamp = 0; };
std::vector<int> room_order(std::span<const RoomUnit> units, std::span<const int> rooms);

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
NearPick search_near(std::span<const NearFoe> foes, int align, bool need_sight);

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
SightPick search_sight(std::span<const SightFoe> foes);

// FUN_005dd510's mode 7 (FUN_005dcc60): with no path to the primary, the
// nearest other enemy of threat 2 or more under 0x31 off in sight; -1 none.
int search_threat(std::span<const SightFoe> foes, int skip, int& distance);

// FUN_005dd510's path test: a type 2 path (the toward pather, near 1 at a
// unit; path type flags 0, 0x6eb690: no stamp lifted) at the path's `steps`
// (+0x91, kept) from (x, y) to the foe at (to_x, to_y) has a point once its
// leading points on its own subtile are dropped (FUN_00648780: the count,
// +0x28). FUN_00649970 makes none at its own subtile or over 100 off.
template <class Blocked>
bool path_found(int x, int y, int to_x, int to_y, int steps, Blocked&& blocked) {
    if ((to_x == x && to_y == y) || std::abs(to_x - x) > 100 || std::abs(to_y - y) > 100) return false;
    const auto points = toward_path(x, y, to_x, to_y, steps, 1, blocked);
    return std::ranges::any_of(points, [&](const std::pair<int, int>& point) { return point != std::pair(x, y); });
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

// A unit's direction 0..63 from (x, y) to (tx, ty), subtiles.
int direction64(int x, int y, int target_x, int target_y);

// Andariel's think (MonAI 34 "Andariel", FUN_005f5830; the driver
// FUN_005b1740 calls it once she has a target). aip1..4 by difficulty:
// in melee, aip1 % AndrialSpray (skill 1) else a swing (A1); at range,
// aip2 % stand 5 frames, else aip3 % a skill — aip4 % (a second draw)
// the spray, else AndyPoisonBolt (skill 2) — else walk at the target.
// One rand(100) on her seed each, as drawn here; her MonStats row has
// both skills, so their >= 0 tests always pass.
enum class AndarielAct : std::uint8_t { spray, melee, idle, bolt, walk };
// Her two skills' Skills.txt ids: AndrialSpray (Skill1), AndyPoisonBolt (Skill2).
inline constexpr int kAndrialSpray = 164, kAndyPoisonBolt = 201;
AndarielAct andariel_think(bool in_melee, const std::array<int, 8>& aip, Rng& rng);

inline constexpr std::array<int, 8> kRing{ 29, 28, 27, 26, 25, 24, 31, 30 };
inline constexpr std::array<std::array<int, 9>, 8> kSweep{ {
    { 27, 14, 15, 3, 99, 7, 21, 22, 31 }, { 26, 12, 13, 2, 99, 6, 19, 20, 30 }, { 25, 10, 11, 1, 99, 5, 17, 18, 29 }, { 24, 8, 9, 0, 99, 4, 15, 16, 28 },
    { 31, 22, 23, 7, 99, 3, 13, 14, 27 }, { 30, 20, 7, 6, 99, 2, 1, 12, 26 }, { 29, 18, 19, 5, 99, 1, 9, 10, 25 }, { 28, 16, 17, 4, 99, 0, 23, 8, 24 } } };
// Where Andariel's spray missile on SC frame `frame` is aimed, subtiles from her, facing `dir64`.
std::pair<int, int> andariel_spray_aim(int dir64, int frame);

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
    // The ElementalBeast's: its mode's action event filed id frames on (strike, FUN_005417d0 type 0).
    // The spawners': a monster made (spawn, FUN_005b2f20: id its class row, -1 the spawner's own
    // spawn column); a minion out of its leader's list (leave_group, FUN_0058f160).
    enum Kind : std::uint8_t { state, overlay, stat, rally, command_done, summoner_open, aura, land, take_off, toggle, unset, set_word, bless, greet, mark, mount, retarget, clone, strike, spawn, leave_group } kind;
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
    int target_home_dist = 0;                   // the same from the target to it (her think's d_t)
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
    // What it does when the walk it returned can't set off: game.exe's think
    // drops the pending think event (FUN_00540e60(2, 0)) and goes on down
    // its list. Tried in order, each only when the one before failed too.
    std::vector<std::function<Think()>>* otherwise = nullptr;
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
    // Baal's teleport spot (FUN_0054dc40 with no unit: 20 tries at random in
    // his room on its seed, each FUN_005b2f20 at radius -1), asked only when
    // act 14 is chosen; none (or no callback): no spot.
    std::function<std::optional<std::pair<int, int>>()> room_spot;
    // The curse thinks' (special AI 10 / 11): it can swing (an A1 mode,
    // FUN_0046c140 bit 4, and not interact, MonStats flag 9), it has an RN
    // mode (bit 15), Terror's state (0x38) is on it. The other thinks': the
    // BatDemon in S4 (unit +0x10 mode 11); the SandMaggot's AI command 14
    // +0x18 (FUN_0058efa0).
    bool swings = true, runs = false, afraid = true, in_s4 = false;
    // GoodNpcRanged's: in NU (unit +0x10 mode 1), in town (FUN_0061ab00 on its room).
    bool neutral = true, in_town = false;
    int* cast_word = nullptr;
    // Act 3's: its soonest mode-end event's frame (FUN_005415a0 type 1, 0 none);
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
    // Act 5's: a line to the target clear of 0x805 (FUN_005dc640, any of three);
    // Skill1's Skills.txt +0x158 (FUN_004e6c70, its reach); the siege
    // beast's rider (FUN_00552fd0), it lies dead (FUN_0063ea40: mode 0 or
    // 12), its alignment test (FUN_006259b0: state 0x69's stat 0xac, set
    // under Confuse / Attract / Conversion), an imp within aip1 to take on
    // (FUN_005dd0b0, FUN_005e1720); the target cursed (FUN_00625760: a stat
    // list flagged 0x20, a States.txt curse state's, FUN_0056e970),
    // its FUN_00625d10 / FUN_00625d60 values, its own life % (FUN_00621f20
    // on itself); its command's last frame (+0x14) and its unit busy
    // (FUN_005541b0).
    bool lines_clear = true, rider = false, self_dead = false, state69 = false, imp_near = false, target_cursed = false, command_busy = false;
    int skill1_range = 0, target_max_life = 0, target_max_mana = 0, self_life_pct = 100, command_until = 0;
    // The Overseer's: the one that last struck at it (FUN_005de830: not
    // dying or dead, its enemy); its search (FUN_005e26d0): a minion within 24
    // under 50 % life, one within 20 not special (FUN_005a0180 8), how many
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
    // The spawners' and the special AIs' (thinks.hpp): its mode (unit +0x10);
    // the unit it made came out (FUN_005b2f20); its class 0x2c7 (demonhole);
    // a leader or owner found (FUN_0058f0d0, FUN_00553540) and that's itself,
    // its mode and spot; a group member just hit (FUN_0058f380 with
    // LAB_005e5710); its command's point (+0xc / +0x10, with `command`); a
    // unit found by its search (FUN_005dd7f0 / FUN_005de890) and that one in
    // melee (the search's out flag).
    int mode = 1, owner_mode = 0, owner_x = 0, owner_y = 0, command_x = 0, command_y = 0;
    bool spawned = true, demon_hole = false, owner = true, self_owner = false, group_hit = false, found = false, found_melee = false;
};

// A monster's skill level: Sk*lvl + DifficultyLevels MonsterSkillBonus.
int monster_skill_level(int sk_lvl, int difficulty);

// FUN_005de200(radius): one side radius off, the other rand(radius), on the seed's low
// bit; the next two low bits negate x, then y.
Think think_wander(Rng& rng, int radius);
inline Think think_circle(Rng& rng, int count) { return { MonAct::circle, count, (rng.next() & 0xff) >= 0x80 ? 1 : 0 }; }
// FUN_005dec80 flags 2 when the walk can't set off.
inline Think walk_failed(Rng& rng) { return rng(100) < 70 ? think_wander(rng, 4) : Think{ MonAct::idle, 10 }; }

// The Gargoyle Trap's shot, in subtiles from the trap at (x, y).
std::array<int, 4> gargoyle_shot(int x, int y, int target_x, int target_y);

// The desert turret's casting steps (0x6e33a0: subtiles, x aip5) and its
// step's turn toward the target's eighth (0x6e33e0: [eighth][step]).
inline constexpr std::array<std::array<int, 2>, 8> kTurretStep{ { { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { 1, 0 } } };
inline constexpr std::array<std::array<int, 8>, 8> kTurretTurn{ { { 0, 0, 1, 2, 5, 6, 7, 0 }, { 1, 1, 1, 2, 3, 6, 7, 0 }, { 1, 2, 2, 2, 3, 4, 7, 0 }, { 1, 2, 3, 3, 3, 4, 5, 6 },
                                                                  { 1, 2, 3, 4, 4, 4, 5, 6 }, { 7, 2, 3, 4, 5, 5, 5, 6 }, { 7, 0, 3, 4, 5, 6, 6, 6 }, { 7, 0, 1, 4, 5, 6, 7, 7 } } };

// The desert turret's fan: `count` missiles from (x, y) at the point (to_x, to_y), side by side 2 subtiles apart
// across the way (by the offset's signs), centred on it.
std::vector<std::array<int, 4>> turret_fan(int x, int y, int to_x, int to_y, int count);

// A search that found no one. FUN_005de890 (search type 1): with a WL
// mode (MonStats2 mode bits, FUN_0046c140(class, 2)), one just hit (its
// last mode GH, FUN_005dd2b0) or standing on collision bit 0x40
// (FUN_0064d910) wanders 5 (FUN_005de200); else it stands 10, nearest - 10
// from 25 off, 25 from 35. Types 4 / 5 (SandMaggot, FrogDemon:
// FUN_005de9d0) take no hit into account and think again 20 frames on
// (FUN_00540e60 drops its think events, FUN_005417d0 files event 2), its
// mode going on.
struct SearchMiss { bool wander = false; int stand = 0; bool mode_kept = false; };
SearchMiss search_missed(std::string_view ai_name, bool has_walk, bool got_hit, bool on_0x40, int nearest);

// The Countess (special AI 0xd, FUN_005e5c50; FUN_005b15d0 runs it in
// place of her CorruptRogue think). Home is where she first thought
// (command 10). Out of home's room she walks back; with the target out of
// it she walks back, or at home exactly a firewall within 25, else stands
// 10; over 40 from home she walks back. Else a firewall; else rand(100):
// in melee under aip3 + 10 A1, out of it under aip1 a run at the target;
// else stands aip2. A firewall (FUN_005e5b70): at her map AI's next point
// (state: its index; state2: the frame of the last), until they run out;
// 700 frames on from the last they start over. A walk home that can't set
// off (FUN_005dede0 0: 0x5e5ded, 0x5e5f51 and the one between) drops the
// think event (FUN_00540e60(2, 0)) and she goes on down the list: `stuck`,
// the think again by ThinkIn::otherwise. tools/emu/ai_countess_home.py.
struct CountessIn { bool away = false, target_away = false, at_home = false, stuck = false; };
Think countess_think(const ThinkIn& input, const CountessIn& where, std::span<const std::pair<int, int>> path, Rng& rng);

// The AI toggle (FUN_005b0e00(ctrl, special)): the think special names
// (FUN_005b15d0: the special table 0x73d358's row, 0 the MonStats AI's
// MonAI row). `special`: the monster's special AI (0 its own think),
// `other`: on its row's other think.
bool ai_toggle(int& special, bool& other, std::array<int*, 3> scratch, int next, bool has_other);

// The riding imp's init.
int imp_ride_pick(Rng& rng, bool has_fireball, bool has_inferno);

// Leader death and minion pack promotion: When a pack's leader dies, the first living minion in the group is
// elected as the new leader, updating siblings.
int ai_minion_elect_leader(std::span<const int> live_minions);

// Ancient AI scratch flag update (FUN_005ef1f0): sets or clears bitmask in +0x18.
void ancient_flag_apply(std::uint32_t& flags, std::uint32_t mask, bool turn_on);

// Generic spawner initialization: nests/sarcophagus init; sets spawn_class to -1 (none until assigned).
int generic_spawner_init();

// HighPriest other think: before cooldown frame yields/waits (event 0); once reached, stands 15 frames.
Think high_priest_other_think(int cooldown_frame, int current_frame);

// Act 5 minion melee think: out of melee, walk at target (89%); in melee, A2 attack at target (95%); else stand
// 10 frames.
Think minion_melee_think(bool in_melee, Rng& rng);

// MonAI rows with an other think.
bool is_diablo_other(std::string_view ai_name);
inline bool has_other_think(std::string_view ai_name) { return ai_name == "SandMaggot" || ai_name == "BatDemon" || ai_name == "FetishShaman" || ai_name == "FrogDemon" || is_diablo_other(ai_name); }

// Dim Vision's and Cloak of Shadows' think: in melee with a swing it swings (A1, mode 4); else 20 % it wanders
// 3, else stands 10.
Think dim_vision_think(const ThinkIn& input, Rng& rng);

// Terror's think (special AI 11, FUN_005e8140; Howl's too, FUN_005ddd00).
// With state 0x38 gone it toggles back (FUN_005b0e00(ctrl, 0)) and thinks
// next frame (FUN_005417d0(2, frame + 1)). Past its reach (state, 30 when
// 0) it stands 10. Its pace is Run / Velocity (0..120 % on). The first time
// (state3 0; state3 1) its skill-set target is cleared and it backs off 30
// (running with an RN mode, FUN_005df140; else walking, FUN_005defe0);
// after, in melee with a swing it swings (A1), else backs off 30 again
// (flags 4), wandering 6 if it can't.
// Its init (0x5e80e0, an AI table entry Ghidra misses) sets a skill-set target to the unit it was at,
// which the first think's search takes (game/ai.cpp search_target).
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

// The other thinks (the MonAI row's fourth word; ai_toggle).
Think other_think(std::string_view ai_name, const ThinkIn& input);

// A MonStats row's walk / run animation base (+0x36 / +0x38) as the loader
// derives them (FUN_00650f00); `own_base`: its BaseId row is itself; `start`:
// the run base before Run scales it.
struct MoveBase { int walk = 0, run = 0; };
int monster_walk_base(bool own_base, int base_walk_speed, int velocity, int base_velocity);
int monster_run_base(bool own_base, int start, int run, int base_run);

// A monster's animation rate in a mode, the speed word FUN_00623f50 stores at
// unit +0x4c (256 a frame a tick).
struct ModeRateIn {
    int mode = 1;                    // monster mode, 0 DT .. 15 RN
    bool low_class = true;           // MonStats row under 0x19a (the mode table at 0x6e8b90, else 0x6e8cd0)
    bool npc = false;                // MonStats npc (+0xd & 1): only WL and RN move
    bool use_attack_rate = false;    // its current skill's UseAttackRate
    int speed = 256;                 // the mode's AnimData speed
    MoveBase base;                   // its MonStats row's walk / run base
    int velocity_pct = 75;           // stat 0x43
    int frw = 0, fhr = 0, fbr = 0, fcr = 0, ias = 0;   // raw stats 96, 99, 102, 105, 93
    bool blocking = false;           // state 0x65
    int attack_rate = 100, other_rate = 100;           // stats 68 / 69
};
int monster_mode_rate(const ModeRateIn& rate_in);

}  // namespace d2d::rules
