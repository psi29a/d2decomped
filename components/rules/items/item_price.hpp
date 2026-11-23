// SPDX-License-Identifier: GPL-3.0-or-later
// What an item is worth to a vendor and what mending it costs: prices, durability, charges, stacks.
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

// FUN_0062efb0's last step for a buy, a gamble or a repair: the player's
// reduced prices (stat 87, at most 99) off, at least 1.
long long price_reduced(long long price, int reduced);

bool type_is(const Tables& tables, const std::string& type, std::string_view want, int depth = 0);

int max_durability(const d2d::d2s::Item& item);

bool indestructible(const Tables& tables, const d2d::d2s::Item& item);

// The price base of an armor: cost x its own defence (stat 31, not the stat list's) / maxac, for buy, sell and
// repair alike; any other item keeps its cost.
int armor_scaled(const Tables& tables, const d2d::d2s::Item& item, int cost);

// game.exe's `mul * price * value / 1024 + add` in 32 bits: above 0xffff (and a
// mul) the price * value is divided first. `limit` is the buy side's price *
// value, which picks the branch for the sale and the repair too; `shift` is 10,
// or 12 for a sale's skill terms.
int price_term(int mul, int add, int price, int value, int limit, int shift);

// What an item's magic stats add to a price (FUN_00628e70): each stat of the
// item's full list less its base one, summed by stat and param, costs the
// ItemStatCost Multiply / Add on `price` x value (a sale's too). A skill stat
// takes the skill's Skills.txt cost mult / add instead: Encode 1 (the param is
// the skill) by the value, Encode 2 / 3 (skill << 6 | level: on-hit, charges) by
// the level, a sale by / 4096. Encode 4 (by time) is mult 0 and no add in 1.14d.
// `buy` is the buy side's price, which picks the branch. The save's lists (the
// main one, a runeword's after it) stand for the magic lists. The list is the
// full one (FUN_00625c30: the stat list's +0x48; FUN_00625560: FUN_00625420
// full less FUN_00625350 base), what the sockets hang on it included
// (socket_props), as stat 91 is read (FUN_00625500 -> FUN_00625420).
std::vector<d2d::d2s::ItemProp> socket_props(const Tables& tables, const d2d::d2s::Item& parent, const d2d::d2s::Item& filled);

int stat_costs(const Tables& tables, const d2d::d2s::Item& item, int buy, int price, bool sell);

// What an item type's staff mods add.
// `buy` picks the branch.
int staff_mod_costs(const Tables& tables, const d2d::d2s::Item& item, int buy, int price, bool sell);

// A stack's most (FUN_006295b0): Items.txt maxstack plus item_extra_stack
// (stat 0xfe), at most 0x1ff.
inline constexpr int kExtraStack = 0xfe, kReplenishQuantity = 0xfd, kReplenishDurability = 0xfc;

int stack_most(const Tables& tables, const d2d::d2s::Item& item);

// A tome (ItemTypes book) or a quiver (bowq / xboq): FUN_0062efb0 prices
// them by their quantity up front, not as a stack.
bool priced_by_contents(const Tables& tables, const d2d::d2s::Item& item);

// A stack's share of its extras.
bool priced_as_stack(const Tables& tables, const d2d::d2s::Item& item);

int stack_divisor(const Tables& tables, const d2d::d2s::Item& item);

int unsigned_share(int total, int divisor);

// What a stack lacks of its most (0062fc97): none when it's full or it replenishes itself.
int stack_missing(const Tables& tables, const d2d::d2s::Item& item);

// The repair channel (FUN_0062efb0's local_c) after the NPC's rep mults,
// before a stack's missing count and the charges: `repair_cost`. A sale
// of a restocking stack takes it off (0062fcb6) without the worn share.
long long repair_each(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, const d2d::d2s::Header& header, bool worn_share);

bool repairable(const Tables& tables, const d2d::d2s::Item& item);

// Buy (sell = false) or sell price of an item at the NPC with MonStats Id
// npc_id, for the player's header (difficulty, quest flags): FUN_0062efb0
// (D2Common's transaction cost). A buy, gamble or repair then takes the
// player's reduced prices (stat 87) off (price_reduced).
int item_price(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, bool sell,
               const d2d::d2s::Header& header, int reduced = 0);

// An item's max durability with its modifiers: the save's base, plus
// flat maxdurability (stat 73), times item_maxdurability_percent (75).
int max_durability(const d2d::d2s::Item& item);

// What a filled socket adds to `parent`: a jewel's own properties, or the
// gem/rune's gems.txt bonus for the parent's kind (weapon, shield, else
// helm/armour). game.exe hangs the socketed item's stat list on the
// parent's (FUN_00626e10), so these count as the parent's own.
std::vector<d2d::d2s::ItemProp> socket_props(const Tables& tables, const d2d::d2s::Item& parent, const d2d::d2s::Item& filled);

// item_indesctructible (stat 152, Zod's too): never wears, never needs repair.
bool indestructible(const Tables& tables, const d2d::d2s::Item& item);

// A broken item (item flag 0x100, set by FUN_0055f850): its stats and
// defense are off until a repair (FUN_0055f900) clears it.
// Item flag 0x200, "repaired": FUN_0055f900 sets it as it clears 0x100. Nothing
// in game.exe's rules reads it back (the one reader, FUN_0053d440's caller in
// InvMode.cpp, tells a client); the item's save writer keeps it (FUN_006312b0).
inline constexpr std::uint32_t kItemRepaired = 0x200;

inline bool broken(const d2d::d2s::Item& item) { return (item.flags & 0x100) != 0; }

// A stack a repair refills: stackable and throwable — every stacking weapon.
bool restocks(const Tables& tables, const d2d::d2s::Item& item);

// What an item's missing charges add to its repair.
int charges_repair_cost(const Tables& tables, const d2d::d2s::Item& item);

// A repair's recharge: each stat 204 below its max back to max.
bool recharge(d2d::d2s::Item& item);

// A charged skill (stat 204) below its max: FUN_0062a840's second answer 0.
bool charge_missing(const d2d::d2s::Item& item);

// FUN_00629930: an item with durability (nodurability 0, a durability
// column, max != 0, not indestructible).
bool has_durability(const Tables& tables, const d2d::d2s::Item& item);

// The item's ItemTypes row has Repair.
bool type_mends(const Tables& tables, const d2d::d2s::Item& item);

// What a repair may price (FUN_0062e660): identified, not ethereal, and a
// charge missing, or a type with Repair that restocks or has durability.
// A whole item passes: worn or not isn't asked here.
bool repairable(const Tables& tables, const d2d::d2s::Item& item);

// What Repair All counts.
bool needs_repair(const Tables& tables, const d2d::d2s::Item& item);

// Repair cost at the NPC (FUN_0062efb0 mode 3): the buy base with its
// quality extras, times missing / max durability, times npc.txt rep mult
// and the quest rep mults / 1024, then the missing charges'
// (charges_repair_cost, after the mults). An item `repairable` refuses
// (FUN_0062e660 at 0062efee) costs 0. A stack (stackable, not a tome or
// quiver) pays a unit's repair, after the mults, times what it lacks of its
// most (`stack_missing`; its durability free as a throwable's). In
// vanilla data every base of a type without Repair also has nodurability
// 1, so the Repair test is redundant there and kept for fidelity (mods).
// With charges missing the gate passes and the base is scaled by the
// durability as FUN_0062efb0 does (0062f9e7): an item without durability
// pays its whole base, a durable one its missing share. A whole item that
// passes the gate costs 0 before the minimum, so 1 (0062fd5c): Repair All
// skips it by `needs_repair`, not by its price. What its sockets hold adds
// half of each one's cost (FUN_006292f0 at 0062f86c, the repair total too),
// after the staff mods and before the missing share and the mults.
long long repair_each(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, const d2d::d2s::Header& header, bool worn_share);

int repair_cost(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, const d2d::d2s::Header& header, int reduced = 0);

// Paying (FUN_00576d90): the purse first, then the stash; nothing taken
// and false when both together fall short. A payment only lowers a
// purse within its cap, so only a growing one can pass it.
bool pay(d2d::d2s::Stats& stats, std::int64_t cost);

// Mends an item: only one its gate passes.
bool mend(const Tables& tables, d2d::d2s::Item& item);

// An item Repair All takes: a worn one, the 13 body slots, never the inventory grid, that `needs_repair`.
bool repair_all_takes(const Tables& tables, const d2d::d2s::Item& item);

// Is type t (or one of its Equiv ancestors) the type want?
bool type_is(const Tables& tables, const std::string& type, std::string_view want, int depth);

}  // namespace d2d::rules
