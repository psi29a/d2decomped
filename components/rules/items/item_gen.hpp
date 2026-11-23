// SPDX-License-Identifier: GPL-3.0-or-later
// Making items: bases, affixes, uniques and sets, imbuing, sockets and runewords.
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

// A mod's stats, per Properties.txt funcs (FUN_0065fd70 over the func
// table at 0x7462f8), in the save's (stat, param, value) form. A func
// that takes a value draws min..max (FUN_0065e9e0: none when min == max)
// when it needs one: 1 / 2 / 13 / 21 / 22 always, 12 / 36 as the param;
// 3 / 4 / 5 / 6 / 7 / 8 / 9 / 10 / 14 / 24 reuse what the first func
// gave (0: a draw), 17 the param (0: a draw); 15 / 16 are min / max and
// 11 / 18 / 20 / 23 don't draw. Nothing is stored for no stat or a 0
// (FUN_0065ea50, which counts a poison with poison max damage).
// 1 / 2 / 3 / 4 / 8 value, 5 / 6 / 7 min / max / % damage, 9 / 22 / 24
// param, 10 skill tab (class*3+tab -> class<<3|tab), 11 chance to cast
// (param skill<<6 | level, value chance, 5 if none), 12 random skill
// (param min..max, value param), 18 by time (FUN_0065f870: the period,
// param 0..3, + ((max + 256) << 10 | (min + 256)) << 2, each 0..1023;
// returns the max part), 19 charges (FUN_0065f6a0: of c = min
// rand(c - c/8) + c/8 + 1 now, value now | c<<8), 20 indestructible
// (stat 152), 21 class skills, 36 the param drawn, value the func's val.
// With `on` (the item the mods land on, FUN_0065fec0's kind 1 superior)
// as game.exe: ac% (2 / 4), and on a superior ac / dur% (1 / 3 / 13),
// first raise an armor's defence to maxac + 1 (FUN_0065ccc0); min / max
// damage (5 / 6) go to the one-hand, two-hand and throw stats the base has
// (FUN_0065ecf0 / FUN_0065ee80; anything but a weapon all three); dmg%
// (7) too small to move the base's max is +1 max damage instead
// (FUN_0065f010); dur% (13) fills the durability to the new most
// (FUN_0065fc90); ethereal (23) makes it so. 5 / 6 pick the hands by
// the base's min / max bytes, and an amount that takes a nonzero base below
// 1 is held: min at 1 (1 - base), max at 0 (-base). Sockets (14,
// FUN_0065f590): the grid (at most 6) and the type's MaxSock cap the value.
// 11 / 19's level is max, or with none off ilvl and the skill's reqlevel
// ((ilvl - req) / 4 + 1 up to its maxlvl; negative: one per (99 - req) /
// -max levels past req). Codes match whatever their case.
// Without `on` (no item: set bonuses, a monster's gear) damage is one-hand,
// charges full and 11 / 19 off ilvl 1.
struct ModItem { d2d::d2s::Item* item; const ItemBase* base; int kind; bool throwable, raise; };   // raise: ac / dur% (1 / 3 / 13) raise the defence too (superior: FUN_0065fec0 kind 1)

std::string lower_case(std::string_view text);

void apply_mod(const Tables& tables, const Mod& mod, std::vector<d2d::d2s::ItemProp>& out, Rng& rng, const ModItem* target = nullptr);

// Affix level for an item of level ilvl with base qlvl.
int affix_level(int ilvl, int qlvl, int magic_lvl);

// A random affix row for an item of `type` at affix level alvl, weighted by frequency; skips groups already
// taken.
int pick_affix(const Tables& tables, const std::vector<Affix>& list, const std::string& type, int alvl, bool rare,
               const std::vector<int>& groups, Rng& rng, bool by_level = false);

// A scroll's or tome's Books.txt row.
int books_row(const Tables& tables, const std::string& code, bool scroll);

// A new item of `code` at level ilvl and quality (4 magic, 5 set, 6 rare, 7 unique; a set / unique none fits is
// remade, below), identified, with its defence and durability rolled.
d2d::d2s::Item roll_item(const Tables& tables, const std::string& code, int ilvl, int quality, Rng& rng,
                         Rng* unit_seed = nullptr, std::vector<bool>* found_uniques = nullptr, bool bovine = false,
                         int difficulty = 0, int special_wanted = 0);

// The item as roll_item makes it, with its unit's seed.
d2d::d2s::Item generate_item(const Tables& tables, const std::string& code, int ilvl, int quality, Rng& rng,
                             Rng* unit_seed = nullptr, std::vector<bool>* found_uniques = nullptr, bool bovine = false,
                             int difficulty = 0, int special_wanted = 0);

// The item's unit seed: where the making left it while it's in play, else fresh from its fingerprint, as a
// loaded item's.
[[nodiscard]] Rng item_seed(const d2d::d2s::Item& item);

// Item flag 0x1000 (data +0x18, the save's flags too): no NPC service
// takes the item (FUN_0062c590 / FUN_0062c770 / FUN_0062c6a0).
inline constexpr std::uint32_t kItemNoService = 0x1000;

// Whether a vendor buys `item` (FUN_00579510 -> FUN_0062a130, else result 9):
// no flag 0x1000, not an Items.txt quest item (+0x12a) nor of type 0x27
// (ques, the type itself: FUN_00629d40 reads +0x11e).
bool sellable(const Tables& tables, const d2d::d2s::Item& item);

// Whether Charsi's imbue takes `item`.
bool imbuable(const Tables& tables, const d2d::d2s::Item& item, bool expansion = true);

// A player's item level (FUN_00558200): the base level, at least 1.
// No +4 here. Charsi's imbue (FUN_00579d60) adds 4 when this is past 5.
inline int player_ilvl(int clvl) { return clvl < 2 ? 1 : clvl; }

// The imbued item.
d2d::d2s::Item imbue_item(const Tables& tables, const d2d::d2s::Item& item, int clvl, Rng& rng);

// The most sockets `item` may have: its gemsockets, capped by its type's MaxSock1 / 25 / 40 for item level <=
// 25, <= 40, above.
int most_sockets(const Tables& tables, const d2d::d2s::Item& item);

// Whether Larzuk's socketing takes `item`.
bool socketable(const Tables& tables, const d2d::d2s::Item& item);

// Larzuk's sockets: magic 1 + rand(the most, at most 2) on the item's seed; set, rare, unique, crafted (5..9) 1;
// the rest the most.
d2d::d2s::Item socket_item(const Tables& tables, const d2d::d2s::Item& item);

// Whether Anya's personalizing takes `item`.
bool nameable(const Tables& tables, const d2d::d2s::Item& item);

// The runeword `item`'s sockets spell, or null: not magic ..
const Tables::Runeword* runeword_row(const Tables& tables, const d2d::d2s::Item& item);

// The last filler in.
bool make_runeword(const Tables& tables, d2d::d2s::Item& item);

// FUN_00562660's checks: `filler` is a socket filler (item type "sock":
// gems, runes, jewels) and `target` identified, socketed, with a socket free.
bool takes_socket(const Tables& tables, const d2d::d2s::Item& filler, const d2d::d2s::Item& target);

}  // namespace d2d::rules
