// SPDX-License-Identifier: GPL-3.0-or-later
// Ammunition: whether a stack has any left (FUN_0062a310), what a
// shot spends (FUN_0056c310), what a hand's item does once it has none
// (FUN_00580030, .\PLAYER\PlrModes.cpp) and the weapon put back after
// (FUN_0057ff70).
// docs/research/re/skills.md "Shots and ammo". Checked against
// game.exe's own code by tools/emu/items_traces2.py (FUN_0062a310),
// tools/emu/ammo_out.py (FUN_00580030; tests/test_ammo_out.cpp),
// tools/emu/shot_spent.py (FUN_0056c310; tests/test_shot_spent.cpp) and
// tools/emu/reequip.py (FUN_0057ff70; tests/test_reequip.cpp).
#pragma once

#include "replenish.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace d2d::rules {

inline constexpr int kItemThrowable = 0x7d;   // item_throwable: no 1.14d item rolls it (stubs.md)

// FUN_0062a310: the ammunition has some left. Its quantity (stat 0x46)
// above 0; else an item with stat 0x7d always has (game.exe writes its
// quantity 0 there); else only one without a max stack (FUN_006295b0 at 0
// or less).
[[nodiscard]] inline bool quantity_left(int quantity, bool throw_stat, int most) { return quantity > 0 || throw_stat || most <= 0; }
[[nodiscard]] bool quantity_left(const Tables& tables, const d2d::d2s::Item& item);

// FUN_0056c310: a shot spends one of the hand's ammunition (FUN_0056c3f0
// picks it). The quantity (stat 0x46) less 1 in 32 bits, 0 when that is
// below 0 (INT_MIN wraps to INT_MAX and stays); the replenish timer queued
// (FUN_00558580); then durability (stat 0x48) put back at its max
// (FUN_00625e00) on every shot. `answer`, FUN_0056c310's: 0 when the
// quantity went below 0, else the new quantity + 1 in 32 bits.
struct ShotSpent {
    int quantity = 0, durability = 0, answer = 0;
    friend bool operator==(const ShotSpent&, const ShotSpent&) = default;
};
[[nodiscard]] ShotSpent shot_spent(int quantity, int durability_most);

// What FUN_00580030 reads of a hand's item.
struct SpentItem {
    bool stackable = false;      // FUN_006289f0: Items stackable
    bool throwable = false;      // FUN_0062ba80: its ItemTypes row's Throwable
    bool throw_stat = false;     // stat 0x7d set
    int quantity = 0;            // stat 0x46
    bool weapon = false;         // ItemTypes weap (0x2d)
    bool magic = false;          // FUN_0062a0f0: quality 4..9
    bool broken = false;         // item flag 0x100
    bool reload = false;         // its ItemTypes row's Reload (+0x11, FUN_0062e7e0)
    bool reequip = false;        // its ItemTypes row's ReEquip (+0x12, FUN_0062e830)
};
enum class SpentFate {
    stays,      // not ammunition, some left, or a magic weapon broken already
    emptied,    // stat 0x7d: its quantity is written 0 and it stays
    breaks,     // a magic or better weapon: FUN_0055f850
    leaves,     // off its slot (FUN_00560cd0)
};
// `reload`: the pack's first of its code, not broken, is put in its slot
// (reload_pick). `reequip`: when none took it, ReEquip runs (reequip_slot).
struct Spent {
    SpentFate fate = SpentFate::stays;
    bool reload = false, reequip = false;
    friend bool operator==(const Spent&, const Spent&) = default;
};

// FUN_00580030: a stackable or throwable item, or one with stat 0x7d, at
// quantity 0 or less.
[[nodiscard]] Spent spent_fate(const SpentItem& item);

// The reload's pick among the pack's items of the spent one's type, in
// the pack's order (FUN_0063cd80): the first of its class (its code) that
// isn't broken, and no other; -1 none.
struct PackItem { bool same_class = false, broken = false; };
[[nodiscard]] int reload_pick(std::span<const PackItem> pack);

// FUN_0057ff70, ReEquip (the player in EDI; the game and the spent slot on
// the stack, `ret 8`). The player keeps one item's unit id (FUN_00623410,
// player data +0x90): the weapon wielded when an item last went on a body
// slot (equip's `kept_weapon`, wield_settle). With one kept (not 0) it goes
// back on (FUN_00562a30) when it is of the game (FUN_00552f60, unit type
// 4), on page 0, the inventory (FUN_00628250), the player's (FUN_0063ad50),
// and FUN_0055d710 (mode 0: auto_equip_slot) finds it a slot: that slot,
// else -1. The spent slot only seeds the slot found. A found slot that isn't
// empty is fatal (line 0x4ea); auto_equip_slot finds only empty ones. The
// kept id is cleared (FUN_006233a0 at 0x58001d) whenever one was kept, put
// on or not.
struct KeptWeapon {
    int id = 0;               // FUN_00623410's answer, 0 none
    bool of_game = false;     // FUN_00552f60 finds it
    int page = 0xff;          // FUN_00628250: 0 the inventory
    bool ours = false;        // FUN_0063ad50: in the player's inventory
    int found_slot = -1;      // FUN_0055d710 mode 0's slot, -1 none
};
[[nodiscard]] int reequip_slot(const KeptWeapon& kept);

}  // namespace d2d::rules
