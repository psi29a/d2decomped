// SPDX-License-Identifier: GPL-3.0-or-later
// A joined game's ask before a skill goes to the host (FUN_004610c0 -> FUN_004d9fc0).
#pragma once

#include <character.hpp>

#include <cstdint>

namespace d2d::game {

struct Fight;

// What the client asked: the skill that goes (Attack in an AttackNoMana skill's place) and its status (0 or 5 go).
struct ClientAsk {
    int skill = 0;
    int status = 0;
};

// The client's status for the acting player's `skill` from `entry` (item -1: the character's own): Fight::skill_status
// with skilldelay the host's state 121 (`host_delay`) and any status 0 an 8 before `guard` (World ms).
ClientAsk client_ask(Fight& fight, int skill, const ChargedSkill& entry, bool host_delay, std::uint32_t guard, std::uint32_t now_ms);

// The client's own guard from a do that put the player in skilldelay till `delay_until`: min(delay, 12) frames.
std::uint32_t client_skill_guard(std::uint32_t delay_until, std::uint32_t now_ms);

}  // namespace d2d::game
