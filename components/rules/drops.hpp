// SPDX-License-Identifier: GPL-3.0-or-later
// Drops: treasure classes (TreasureClassEx and the auto weapN / armoN
// classes) and item quality rolls (ItemRatio), as game.exe rolls them
// (docs/research/re/drops.md).
#pragma once

#include "replenish.hpp"
#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace d2d::rules {

// What a kill drops: an item (code, quality 1 low .. 7 unique) or gold
// ("gld": `gold` coins, or while 0 its amount is rolled as it's made:
// gold_amount, times `mul` / 256 when the entry was "gld,mul=N").
struct Drop { std::string code; int quality = 2; int gold = 0; int mul = 0; int special = 0; };   // special: a unique / set row + 1 to make (struct +0x40)

// Does the floor take a drop? FUN_0055a550 asks FUN_00555da0 for a spot
// before it makes the item (after the quality roll, so after its draws) and
// makes none when that finds none. (drop, items made so far) -> placed. A
// drop placed here is the caller's to put on the floor now: a later one's
// spot takes it into account. An empty one places every drop.
using Placement = std::function<bool(const Drop&, int made)>;

// The item's quality at item level ilvl (FUN_00558640, all draws off the
// dropper's seed): a type flagged Normal stays normal; a base that only
// drops unique (Items +0x129), or a quest item of a Magic type, is unique.
// A Magic type is magic past those; else magic (mf undiminished),
// superior, normal, low. mod: Unique, Set, Rare, Magic.
int roll_quality(const Tables& tables, const std::string& code, int ilvl, const std::array<int, 4>& mod, Rng& rng, int magic_find = 0);

// The quality an item asked for at `quality` is made at (FUN_00557450):
// an always-magic type's quest base unique, else magic at least; no rare
// where the type can't be; an only-unique base unique; an always-normal
// type (or none) normal.
int made_quality(const Tables& tables, const std::string& code, int quality);

// A stand's item's quality (FUN_00556f60, nothing asked for), the first
// draw on its own seed: unique, rare, set, magic, superior, normal in
// turn, each won at rand(base - past / divisor) == 0 (odds under 1 win),
// past being ilvl - qlvl, at least 1. A quest base is normal.
int stand_quality(const Tables& tables, const std::string& code, int ilvl, Rng& item_seed);

// One TreasureClassEx item cell (FUN_00654440): the code, then comma
// parameters. Gold keeps "gld,mul=N" as its code (the roll reads the
// multiplier). Quotes around the cell are dropped. A unique or set by name
// (flag 1 / 2, entry +0xa its row) is the loader's (gamedata_load.cpp).
struct TreasureEntry { std::string code; int flags = 0; int index = 0; };
TreasureEntry treasure_entry(std::string cell);

// A gold pile's coins (FUN_00557ab0, the first draw on the new item's
// unit seed): ilvl (at least 1, FUN_00558d90) + rand(5 ilvl), at least 1;
// a ",mul=N" entry scales it by N / 256 (FUN_0055a6d0).
int gold_amount(int ilvl, int mul, Rng& item_seed);

// A monster's class moved on by level (FUN_00654e00): while the next row
// of its group has a level <= lvl.
std::string tc_upgrade(const Tables& tables, std::string name, int lvl);

namespace detail {
// One class's picks (a frame of FUN_0055a6d0's stack), NoDrop for `shares`
// players; false once `max` items dropped (the whole roll stops).
// A drop `place` refuses is not made: not in `out`, not counted, and the
// pick is spent (FUN_0055a6d0 0x55a550 returning 0: no count, no
// FUN_005589a0, the next pick).
bool roll_class(const Tables& tables, const TreasureClass& treasure, std::array<int, 4> mod, int ilvl, Rng& rng, std::vector<Drop>& out,
                int& count, int max, int shares, int magic_find, int depth, int forced = 0, const Placement& place = {});
}  // namespace detail

// Resolve treasure class `tc` (FUN_0055a6d0) off the dropper's seed `rng`:
// each pick rolls rand(NoDrop + total) (NoDrop scaled by `players`, the
// /players count); a class entry picks in turn, depth first, the highest
// quality modifiers on the way winning; an item rolls its quality at
// `ilvl`. Negative picks: no NoDrop roll, the n-th of |picks| is the entry
// whose running weight passes n (the Countess: her item TC, then her rune
// TC), stopping at the weights' total. At most `max` items (6) in all.
// A nonzero `forced` quality (a chest round's) replaces the quality roll, gold's too.
// A unique / set entry (flags 1 / 2) is quality 7 / 5, forced or not, and skips the
// quality roll; its row goes with it (Drop::special, generate_item's `special`).
// Flag modifiers (bin +0x30 / +0x32) are 0, so those draws do not run.
// `place` (FUN_00555da0 through FUN_0055a550): a drop it refuses is not made
// and not counted (Loot's monster drops pass one).
// With `players` in the game and `nearby` of the killer's party on its
// level (FUN_005408e0; a lone killer 1), NoDrop rolls for n = nearby +
// (players - nearby) / 2 (drops.md "Rolling"); a monster dropper (unit type
// 1, FUN_0044be50) caps n at its monster_playercount (stat 100, at least 1):
// `dropper_players`, 0 for none.
void roll_drops(const Tables& tables, const std::string& treasure_class, int ilvl, Rng& rng, std::vector<Drop>& out,
                int players = 1, int magic_find = 0, int max = 6, int forced = 0, int nearby = 1, int dropper_players = 0, const Placement& place = {});

// One chest round (FUN_00585b90): its class `tc` (chest_tc) off the
// object's unit seed, qualities at its tier (A..C = 0..2), onto `out`;
// each placed as it's rolled (`place`, FUN_0055a550 as for a monster).
// Returns the first item's quality as made (made_quality), 0 for none.
int chest_round(const Tables& tables, const std::string& treasure_class, Rng& unit_seed, std::vector<Drop>& out, int forced, const Placement& place = {});

// The auto classes (FUN_006541c0): for each ItemTypes row with
// TreasureClass (bow, weap, mele, armo, abow), in its order, <code>N for N
// = 3, 6 .. 96 holds the spawnable non-quest bases (items-table order:
// weapons, armor, misc) of the type (type or type2, by Equiv) with qlvl in
// N-2..N, weighted by their type's Rarity; tpot bases only in a tpot class.
struct AutoBase { std::string code, type, type2; int level = 0; };
void add_auto_treasure(Tables& tables, const std::vector<std::string>& type_order, const std::vector<AutoBase>& bases);

// A vendor's item (FUN_00576330) for a fill's ask: in Nightmare above clvl
// 25 rand(100000) on the proxy seed under ilvl * 64 + 4000 takes the
// ubercode, else the NightmareUpgrade; in Hell an expansion game's roll under
// ilvl * 16 + 1000 the ultracode, else under ilvl * 128 + 5000 the ubercode,
// then the HellUpgrade wins.
std::optional<d2d::d2s::Item> make_store_item(const Tables& tables, const StoreAsk& ask, int difficulty, int clvl, bool expansion,
                                              Rng& proxy, Rng& game, std::vector<bool>* found_uniques);

}  // namespace d2d::rules
