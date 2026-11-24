// SPDX-License-Identifier: GPL-3.0-or-later
// The party screen's hostile button on the host: game.exe 1.14d FUN_005a5e50
// (PartyScreen.cpp, entry 4 of FUN_005a6000's table, C->S 0x5d action 4).
#pragma once

#include <cstdint>

#include "level_ids.hpp"

namespace d2d::rules {

// What FUN_005a5e50 does with one C->S 0x5d action 4, as read from game.exe.
enum class HostileToggle {
    kViolation,   // *out = 1: FUN_005a6000 returns 3 (a bad packet); nothing else happens
    kResend,      // the relation already is the asked state: FUN_0055b4c0 resends both flags (S->C 0x8c), no change
    kIgnored,     // not hostile yet and the other unit's level (FUN_00625480) is under 9: nothing
    kCleared,     // `on` is 0: the declarer's flag 8 toward the other goes (FUN_005a6080), S->C 0x5a type 4 to the other
    kCoolingDown, // the pair's 60 s stamp is still ahead: the declarer says voice event 0x13, S->C 0x5a (0x10), flags resent
    kDeclared,    // both flags 8 set (outside a party), the pair stamp = now + 60000, the player's +0x160 = now
};

// The pair stamp's length (FUN_005a5e50 adds 0xea60 to GetTickCount for FUN_0055b720).
inline constexpr std::uint32_t kHostilePairCooldownMs = 60000;
// The level every declaring player needs (stat 0xc, FUN_006253b0 `< 9` refuses).
inline constexpr int kHostileMinLevel = 9;

struct HostileToggleInput {
    bool in_room = true;               // FUN_00620bb0 (the declarer's room) is not null
    int room_level_id = 1;             // that room's level (FUN_0061ab00: +0x10 -> +0x58 -> +0x1d0)
    int self_level = 0;                // FUN_006253b0(declarer, 0xc)
    int other_level = 0;               // FUN_006253b0(other, 0xc)
    int other_level_again = 0;         // FUN_00625480(other, 0xc), read again once the relation exists
    bool hostile = false;              // FUN_0055b300(declarer, other, 8) != 0
    std::uint32_t pair_stamp = 0;      // FUN_0055b6c0(declarer, other): the node's +8
    std::uint32_t now = 0;             // GetTickCount
    std::uint32_t requested = 0;              // the packet's byte [2]
};

// FUN_005a5e50's branch, with the relation nodes made (FUN_0055b140 makes none for
// a unit in state 7 or while FUN_0055b110's toggle is set: not modelled). The
// refusal needs the declarer's room to be in a town (FUN_006426a0: levels 1,
// 0x28, 0x4b, 0x67, 0x6d), a level of 9 or more for both, as game.exe tests.
// `requested` (the packet byte) is compared whole with the flag's masked 8 (`FUN_0055b300(..., 8) != on`),
// so the state "equal" is 0 with 0, or a `requested` of 8.
constexpr HostileToggle hostile_toggle(const HostileToggleInput& input) {
    if (!input.in_room || !level_ids::is_town(input.room_level_id) || input.self_level < kHostileMinLevel || input.other_level < kHostileMinLevel)
        return HostileToggle::kViolation;
    if (std::uint32_t(input.hostile ? 8 : 0) == input.requested) return HostileToggle::kResend;
    if (!input.hostile && input.other_level_again < kHostileMinLevel) return HostileToggle::kIgnored;
    if (input.requested == 0) return HostileToggle::kCleared;
    if (input.pair_stamp > input.now) return HostileToggle::kCoolingDown;
    return HostileToggle::kDeclared;
}

}  // namespace d2d::rules
