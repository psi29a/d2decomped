// SPDX-License-Identifier: GPL-3.0-or-later
// Wearing and carrying items: requirements, body slots, the grid, the hands, the belt, picking up, identifying.
// The tables these read are rules.hpp's.
#pragma once

#include "item_price.hpp"
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

std::pair<int, int> item_size(const Tables& tables, const std::string& code);

// Where an inventory grid takes an item it's given: a w x h spot in the cols x rows grid of placed items, wholly
// free.
std::pair<int, int> free_spot(const Tables& tables, const std::vector<const d2d::d2s::Item*>& placed,
                              int cols, int rows, int width, int height, bool players);

void wield_enters(const Tables& tables, std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item);

// Whether a death makes a corpse (FUN_0057f700): the live corpse count
// (FUN_0063d5f0) against 0xf, unsigned (jbe at 0x57f730). Over 15,
// FUN_0057f540 drops the gear instead and there is no corpse.
// tools/emu/corpse_cap.py.
constexpr bool death_makes_corpse(int corpses) { return unsigned(corpses) <= 15; }

// Which corpse a save keeps.
int corpse_to_save(const Tables& tables, const std::vector<const std::vector<d2d::d2s::Item>*>& corpses, const std::string& npc_id,
                   const d2d::d2s::Header& header, int reduced);

// A scroll's tome (Books.txt pairs tsc / tbk, isc / ibk) for `scroll`: true when `book` is one with room.
bool tome_takes(const Tables& tables, const d2d::d2s::Item& scroll, const d2d::d2s::Item& book);

int belt_box(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, int boxes);

bool stacks_with(const d2d::d2s::Item& one, const d2d::d2s::Item& other);

// Buys stock item i of the open tab (C->S 0x32, FUN_00577830): gold down
// by the price (more than carried + stash gold: no_gold), carried gold
// first, then the stash (FUN_00576d90); the item leaves the stock unless
// it's a perm one (FUN_00576f50 asks FUN_00576ed0: store_perm). A scroll goes into a carried tome with room
// (FUN_0055f640). Anything else is a copy (FUN_0055a2a0), stack and all,
// offered to the belt first (FUN_0055e9b0 -> FUN_0063c790: belt_box,
// `boxes` the worn belt's, 4 with none), else the inventory's first free
// spot (FUN_00560200; nowhere for it: no_room, nothing paid).
// `fill` (the packet's 0x80000000: shift + right click on a multibuy item,
// FUN_004b2650) counts for a perm item only (FUN_00576ed0):
// - a scroll fills the tome: as many as the gold buys (local_14 / local_8),
//   at most its room;
// - an AutoStack type (keys; FUN_0062e790, its own ItemTypes row +0x13)
//   priced one at a time (stat 0x46 set to 1) tops up the first carried
//   stack of it with room, worn then the backpack (FUN_00577700:
//   FUN_0063c2f0, FUN_0063c200), with as many as the gold buys (none:
//   no_gold, 0x89a); with none carried, it buys one stack of that many,
//   at most its most;
// - anything else is bought again while it goes to the belt (every row,
//   FUN_0055e9b0): the first copy that can't is dropped unpaid and ends it
//   (0x577d9c -> LAB_00577e68: the gold put back, nothing said) unless a
//   body slot takes it; only the first copy goes to the inventory. Gold
//   running out after one is bought says no_gold (0x8d4).
// Given a wearer, an item bound for the inventory is first offered to a body
// slot (0x577d10 .. FUN_00562e00 -> FUN_0055d710 mode 0: auto_equip_slot,
// `required_level` the item's), unless a weapon is in hand (FUN_0063bef0,
// either arm, ItemTypes 0x2d) and the item is a weapon too. Bolts (cqv,
// 0x883e9c) go on only by a crossbow in hand (its wclass xbw, FUN_00623c60
// 7), arrows (aqv, 0x883ea0) only by a bow (bow, 1), neither with nothing in
// hand: in vanilla data every bow's wclass is bow and every crossbow's xbw,
// so auto_equip_slot's own test (the hand's weapon shoots the quiver's type)
// is the same one.
struct Wearer;

int auto_equip_slot(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, const Wearer& wearer,
                           int required_level);

// BodyLocs.txt codes -> body slot (d2s slot numbering: 1 head .. 10 gloves).
int body_slot(std::string_view code);

// The class code (ama, sor, nec, pal, bar, dru, ass) an item type is
// restricted to, "" for anyone: ItemTypes Class along the Equiv chain.
std::string type_class(const Tables& tables, const std::string& type, int depth = 0);

inline constexpr std::string_view kClassCode[7] = { "ama", "sor", "nec", "pal", "bar", "dru", "ass" };   // by d2s class

// What the player brings to an equip check: d2s class and the current
// strength, dexterity and level. `holy_shield` is the effective Holy
// Shield level while state 0x65 is on (FUN_00485be0 / FUN_00485d40), else 0.
struct Wearer { int cls = 0, str = 0, dex = 0, lvl = 1, holy_shield = 0; };

const ItemInfo* info_of(const Tables& tables, const d2d::d2s::Item& item);

// game.exe's value x times / over, truncated.
int scale(int value, int times, int over);

// FUN_0062eaf0's strength, dexterity and level: the base's ReqStr / ReqDex
// each + itself x the item's stat 91 ("requirements -x%") / 100
// (FUN_00483360), 10 less on an ethereal item; a wearer with no strength
// or no dexterity meets neither.
bool meets_stats(const ItemInfo& info, const d2d::d2s::Item& item, int str, int dex, int lvl, int required_level = -1,
                 const Tables* tables = nullptr);

// The class, stat and level requirements alone.
bool meets_requirements(const Tables& tables, const d2d::d2s::Item& item, const Wearer& wearer, int required_level = -1);

// Can `it` be worn in body slot `slot` by `w` at all, ignoring what's already equipped?
bool can_wear(const Tables& tables, const d2d::d2s::Item& item, int slot, const Wearer& wearer, int required_level = -1);

// A two-handed weapon fills both hands; only a bow's own quiver joins it (hands_ok).
bool blocks_other_hand(const Tables& tables, const d2d::d2s::Item& item, const Wearer& wearer);

// Can a and b be held in the two hands together?
bool hands_ok(const Tables& tables, const d2d::d2s::Item& first, const d2d::d2s::Item& second, const Wearer& wearer);

// A body slot's other side: the arms 4 <-> 5, the rings 6 <-> 7, the switch arms 11 <-> 12; 0 for the rest.
int other_side(int slot);

// Where a corpse's item from `slot` goes: `taken` the player's slot, `other_taken` its other side's.
int corpse_slot(int slot, bool taken, bool other_taken);

// The item cursor.

// Put `held` into a stored grid (location 0; panel 1 inventory, 4 cube,
// 5 stash) with its top-left cell at (col, row).
bool put_in_grid(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
                 int panel, int cols, int rows, int col, int row);

// A hand's stack `held` joins: its index in `items`, -1 none.
int hand_stack(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& held, int slot);

// The cursor's stack onto `target`.
void stack_onto(const Tables& tables, d2d::d2s::Item& target, std::optional<d2d::d2s::Item>& held);

// The wielded weapon (weapon-switch.md "The wielded weapon"): the
// inventory keeps one item's id at +0x1c, here that item's `wielded`.
// Usable (FUN_0062a4e0): not broken (flag 0x100) nor flagged 0x4000.
inline bool item_usable(const d2d::d2s::Item& item) { return (item.flags & 0x4100u) == 0; }

bool in_a_hand(const d2d::d2s::Item& item);

bool weapon_item(const Tables& tables, const d2d::d2s::Item& item);

// The item in hand `slot`, if any.
int hand_index(const std::vector<d2d::d2s::Item>& items, int slot);

// FUN_0063bef0: the left hand's item (cell 5), then the right's (cell 4),
// if it's a weapon (ItemTypes weap) and the wielded one; usable or not.
const d2d::d2s::Item* wielded_weapon(const Tables& tables, const std::vector<d2d::d2s::Item>& items);

void wield(std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item);

// FUN_0063d1d0: weapon `item` is now in a hand (one of `items`). Usable, it
// becomes the wielded one when no weapon is (FUN_0063bef0 says none) or
// the wielded one is a throwing potion (ItemTypes tpot); unusable, it isn't.
// Every hand exit clears the leaving weapon's mark (wield_leaves), so the
// wielded one found here is never `item` itself.
void wield_enters(const Tables& tables, std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item);

// FUN_0063d2b0: weapon `leaving`, still in its hand, comes off. It stops
// being the wielded one, and the other hand's item, if usable (any item,
// not only a weapon), becomes it.
void wield_leaves(const Tables& tables, std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& leaving);

// A loaded character's hands.
int wield_settle(const Tables& tables, std::vector<d2d::d2s::Item>& items);

// FUN_0055db00: worn `item` (one of `items`) meets its requirements:
// FUN_0062eaf0's (identified, class, strength, dexterity, level), and a
// quiver (its ItemTypes Quiver, +0xe: FUN_0062e740) only with an item of
// its bow's type (FUN_00629bb0) in the right hand, or the left when the
// quiver is the right hand's.
bool requirements_hold(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, const Wearer& wearer,
                       int required_level = -1);

// An inventory charm, as FUN_0062ff70 has it before its requirements: an
// item of ItemType 0xd (char) on page 0 (item data +0x45), not broken
// (0x100) nor flagged 0x4000.
bool inventory_charm(const Tables& tables, const d2d::d2s::Item& item);

// FUN_0055dbc0 (ItemMode.cpp). `wearer()` is the strength, dexterity and
// level with the gear's stats on, which a flagged item's no longer are;
// `level(item)` its required level.
// - Pass 1, the inventory's charms (0x55dc07..0x55dc7b): one that passes
//   FUN_0062ff70 (an inventory charm meeting FUN_0062eaf0's requirements)
//   has its stats put on (FUN_0055d970 -> FUN_0063d1d0). One short of them
//   gets nothing; game.exe flags no charm, so `charm_short` (not saved)
//   says it. No caller of FUN_0055dbc0 is a level up, so a charm reached
//   by levelling counts from the next item move.
// - The worn passes (body slots 1..10; the weapon switch's 11 / 12 aren't
//   walked). First, in slot order, an unbroken unflagged item that fails
//   gets flag 0x4000 (its stats off, FUN_0055c730) and FUN_0063d2b0.
//   Then, repeated while one changed (one's stats can let another pass), an
//   unbroken flagged item that meets them loses 0x4000 and gets
//   FUN_0063d1d0 (its stats on, FUN_0055c2c0). A flagged item's own stats
//   don't count toward its own requirements; an unflagged one's do.
template <class WearerOf, class LevelOf>
inline void settle_requirements(const Tables& tables, std::vector<d2d::d2s::Item>& items, WearerOf&& wearer, LevelOf&& level) {
    const auto worn = [](const d2d::d2s::Item& item) {
        return item.location == d2d::d2s::item_location::kEquipped && item.slot >= d2d::d2s::body_location::kFirst
            && item.slot <= d2d::d2s::body_location::kLast;
    };
    for (auto& item : items) item.charm_short = inventory_charm(tables, item) && !requirements_hold(tables, items, item, wearer(), level(item));
    for (int slot = d2d::d2s::body_location::kFirst; slot <= d2d::d2s::body_location::kLast; ++slot)
        for (auto& item : items)
            if (worn(item) && item.slot == slot && item_usable(item) && !requirements_hold(tables, items, item, wearer(), level(item))) {
                item.flags |= 0x4000u;
                wield_leaves(tables, items, item);
            }
    for (bool changed = true; changed;) {
        changed = false;
        for (int slot = d2d::d2s::body_location::kFirst; slot <= d2d::d2s::body_location::kLast; ++slot)
            for (auto& item : items)
                if (worn(item) && item.slot == slot && !broken(item) && (item.flags & 0x4000u) && requirements_hold(tables, items, item, wearer(), level(item))) {
                    item.flags &= ~0x4000u;
                    wield_enters(tables, items, item);
                    changed = true;
                }
    }
}

// The item Throw takes (FUN_0056f460; the gate FUN_00647640 for skill 2):
// the first usable weapon in hand (FUN_0063c9b0: the right hand's, then
// the left's); when that isn't the wielded one (FUN_0063bef0), the other
// hand's item (FUN_00643d60), whatever it is. -1 none. `wielded` says
// whether the first was the wielded one.
int throw_hand(const Tables& tables, const std::vector<d2d::d2s::Item>& items, bool* wielded = nullptr);

// Equip `held` in body slot `slot` (1..10), swapping out what's there; a
// stack onto its kind in a hand joins it (hand_stack, stack_onto).
bool equip(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
           int slot, const Wearer& wearer, int required_level = -1, int cols = 0, int rows = 0, int* kept_weapon = nullptr);

// A bag item worn in one step. The hand must be empty. Any refusal leaves
// `items` and `held` as they were.
int bag_equip_slot(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const std::optional<d2d::d2s::Item>& held,
                   int id, const Wearer& wearer, int required_level = -1);

// `kept_weapon` as equip's: the ToBody it stands for (deviations.md 18) is code 1 or 5.
bool equip_from_bag(const Tables& tables, std::vector<d2d::d2s::Item>& items, const std::optional<d2d::d2s::Item>& held,
                    int id, const Wearer& wearer, int required_level, int cols, int rows, int* kept_weapon = nullptr);

// An empty hand clicked with nothing held, beside a two-hander.
int two_hander_beside(const Tables& tables, const std::vector<d2d::d2s::Item>& items, int slot);

// Put `held` in belt box `box` (0..boxes-1): potions, scrolls — ItemTypes
// Beltable along the Equiv chain. A smaller belt's lost rows: spill_belt.
bool put_in_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
                 int box, int boxes);

// A belt put on or taken off (FUN_005608c0, from the swap FUN_00560f00 and
// body -> cursor FUN_00560cd0; none worn: Belts.txt row 2, one row): each
// item in a box the belt now worn lacks, `boxes` up to 15 in order, goes to
// the inventory (FUN_0063b950: free_spot, a player's best fit), else it's
// returned, for the caller to drop at the player's feet (FUN_00555da0,
// FUN_00558aa0).
std::vector<d2d::d2s::Item> spill_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, int boxes, int cols, int rows);

// Pick up item i into the (empty) cursor.
bool pick_up(std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held, std::size_t index);

// Picking an item up (FUN_00563560, the server's auto-place; gold apart):
// - stacking first (FUN_00560020): a scroll into an inventory tome of its
//   kind with room, one scroll (FUN_0055ffa0 / FUN_0055ef20); a tome's
//   scrolls into such a tome, past its maxstack the rest staying on the
//   ground in the picked one (FUN_0055d370); an AutoStack type (keys) into
//   the inventory's stacks of it with room, the rest placed on (FUN_0055d0d0);
// - the belt (FUN_0063c790): a 1x1 item of a Beltable type (its own row,
//   FUN_0062bad0) to the first column whose bottom item matches (the same
//   code, or both of hp1-5 / mp1-5 / rvs, rvl: FUN_00628a40), its lowest
//   free box below `boxes`; else, autobelt, the first free bottom box
//   (FUN_0063c600). FUN_00628ba0 lets every item through: its bottom check
//   FUN_0063c560 (no isc / tsc) returns 0 or 1 and the caller tests -1,
//   so a scroll goes into a column whose bottom is the same scroll;
// - else the inventory's first free spot (FUN_005600a0); no room leaves it
//   on the ground (FUN_0055c9a0 message 0x17: the class's "can't carry").
//   Before the belt, given a wearer, the auto-equip (auto_equip_slot).
// Tomes by the Books.txt pairs tsc / tbk, isc / ibk.
enum class Pickup { kGone, kStays, kNoRoom };

// The body slot a pickup goes to (FUN_0055d710 mode 0), -1 none: an
// identified item the wearer can use (FUN_0062eaf0), of a type with a body
// slot (FUN_0062fdf0), not broken (0x100) nor flagged 0x4000 (FUN_0062a4e0),
// not a throwing potion (ItemTypes row 0x26, tpot); ammunition (its type's
// Quiver, FUN_00628480) only with a weapon in the hands that shoots it
// (FUN_0062e6f0). One slot (BodyLoc1 == BodyLoc2) when it's empty; two
// (rings, the hands) BodyLoc1 when both are empty, else the empty one when
// what fills the other goes with it (FUN_0055d670: two rings, a bow and its
// ammunition, a one-hander and a shield, two weapons a Barbarian or two
// claws an Assassin holds: rules::hands_ok).
int auto_equip_slot(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, const Wearer& wearer,
                    int required_level = -1);

// The belt groups of FUN_00628a40, 3-char codes in game.exe order (0x744684 hp, 0x74466c mp, 0x744660 rv).
inline constexpr std::array<std::string_view, 3> kGroups{ "hp1hp2hp3hp4hp5", "mp1mp2mp3mp4mp5", "rvlrvs" };

// The item's own ItemTypes row is Beltable.
bool beltable(const Tables& tables, const d2d::d2s::Item& item);

// The belt box `item` goes to (FUN_0063c600), -1 none: a 1x1 Beltable item
// to the first column whose bottom item matches (the same code, or both
// of hp1-5 / mp1-5 / rvs, rvl: FUN_00628a40), its lowest free box below
// `boxes`; else, autobelt, the first free bottom box.
int belt_box(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, int boxes);

// Shift + left click on an inventory item, the hand empty: an item stored in the inventory moves to belt_box's
// box.
bool auto_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, int id, int boxes);

// Two items stack (FUN_0062c850): the same class, quality and file index
// (data +0x28: a low / superior row, qsub), both of a Stackable row
// (+0x132), both ethereal or neither (0x400000), of no magic or better
// quality (FUN_0062a2f0: 1..3 or past 9), with damage stats 21-24 and
// 159 / 160 equal (the class's, for such items) and no sockets (stat 0xc2).
bool stacks_with(const d2d::d2s::Item& one, const d2d::d2s::Item& other);

Pickup pick_up(const Tables& tables, std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item, int cols, int rows, int boxes,
               const Wearer* wearer = nullptr, int required_level = -1);

// Cain's "Identify Items".
bool cain_identifies(const d2d::d2s::Item& item);

inline int unidentified(const std::vector<d2d::d2s::Item>& items) { return int(std::ranges::count_if(items, cain_identifies)); }

int identify_all(std::vector<d2d::d2s::Item>& items);

}  // namespace d2d::rules
