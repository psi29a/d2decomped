// SPDX-License-Identifier: GPL-3.0-or-later
// What the fight/ files share: no part of the game's API.
#pragma once

#include "ai.hpp"
#include "gamedata.hpp"

#include <merc.hpp>
#include <monsters.hpp>
#include <montypes.hpp>
#include <skills.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::game {

// Cloak of Shadows on a monster (FUN_005d6520): its state, with the AI
// toggle at its end whatever the monster, then special AI 10 on one
// swaps_ai lets.
void cloak(const GameData& game_data, Monster& monster, int skill, int lvl, std::uint32_t until, std::uint32_t now_ms);

// Diablo's score of a pet or monster (FUN_005e8530 reads any unit's stats
// so): its resists as stats 0x24 / 0x25 / 0x27 / 0x29 / 0x2b, raw, and its
// aura as its right skill (FUN_005701b0 sets it: a merc's FUN_005e4d30, a
// summon's FUN_005c4470, Aura Enchanted's FUN_005a1650) at its level.
// A monster's or summon's max damage is its last swing's (rules::swing_max_damage),
// the merc's its max damage stats (merc_foe).
// None has a left skill: every FUN_005701b0 call with EDX 1 is a player's
// (FUN_0055c5c0, FUN_0056af80 test unit type 0; FUN_0055da70 / FUN_00580280
// put a player's saved hands back).
d2d::rules::DiabloPick unit_pick(const GameData& game_data, std::array<int, 5> res, int aura, int aura_level, int max_damage);

// A think's move as a path by its type (FUN_00649970) from subtile
// (from_x, from_y): the cells to walk, empty when none.
auto move_route(const Level& level, const UnitState& unit, int from_x, int from_y, const d2d::rules::MercMove& move, const Crowd& crowd)
    -> std::vector<std::pair<float, float>>;

// A unit put at the free spot nearest (x, y) cells (FUN_00554ea0 ->
// FUN_0064e7b0, its size, mask 0x1c09); none: it stays.
void put_near(const Level& level, const Crowd& crowd, UnitState& unit, float x, float y);

// A pet's or the merc's think mode 3 (FUN_005e3930): a spot in the owner's
// room (FUN_0054dc40: up to 20 tries inside its rect less a subtile each
// side, on the room's seed, each where MonStats class 0x16b fits:
// FUN_005b2f20 radius -1, test only, spawn_spot), then put_near it.
// False when no try fits.
bool room_teleport(const GameData& game_data, const Level& level, const Crowd& crowd, Spawning& spawning, UnitState& unit);

// A monster mode's number (MonMode: DT 0 .. RN 15); a MonSeq sequence
// (seq_swtigerfist) or Skills.txt monanim xx plays SQ (14).
int monster_mode(std::string_view token);

// FUN_005eab20 by name: may a Shadow (MonStats row `type_info`, raised by
// `summoning`) have the skill? One that summons nothing, or neither this
// monster nor the Shadow's pettype.
bool shadow_may_have(const d2d::rules::Skill* skill, const d2d::rules::MonType& type_info, const d2d::rules::Skill& summoning);

}  // namespace d2d::game
