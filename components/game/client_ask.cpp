// SPDX-License-Identifier: GPL-3.0-or-later
#include "client_ask.hpp"

#include "fight.hpp"
#include "gamedata.hpp"

#include <character.hpp>

#include <algorithm>
#include <cstdint>

namespace d2d::game {

// FUN_004610c0 (skills.md "The client asks before it sends a skill"): FUN_004d9fc0's status, then on 1, 2 or 4 a skill
// with AttackNoMana has Attack's entry put in the slot and is asked again (LAB_00461101). FUN_004d9fc0 is FUN_00647960
// (Fight::skill_status) on the client's unit, and a 0 turned 8 while the client's frame clock (0x7a0498) is under 0x7a04fc
// (FUN_0044ce50). The client's FUN_006478f0 reads state skilldelay (0x79) on its own unit, which the host sends (States.txt
// nosend empty), so the World's Hero::skill_delay stands aside while asking; the charged entry's level and its no-mana
// cost come from Hero::item_charge, set for the ask.
ClientAsk client_ask(Fight& fight, int skill, const ChargedSkill& entry, bool host_delay, std::uint32_t guard, std::uint32_t now_ms) {
    Fight::Hero& hero = *fight.actor;
    const auto kept_charge = hero.item_charge;
    const auto kept_delay = hero.skill_delay;
    hero.item_charge = entry;
    hero.skill_delay = host_delay ? 0xffffffffU : 0;
    const auto status_of = [&](int asked) {
        const int status = fight.skill_status(asked, now_ms);
        return status == 0 && now_ms < guard ? 8 : status;
    };
    ClientAsk ask{ .skill = skill, .status = status_of(skill) };
    if (const auto* row = fight.game_data->skills.get(skill); row && row->attack_no_mana && (ask.status == 1 || ask.status == 2 || ask.status == 4)) {
        ask.skill = 0;
        hero.item_charge = {};
        ask.status = status_of(0);
    }
    hero.item_charge = kept_charge;
    hero.skill_delay = kept_delay;
    return ask;
}

// FUN_004c6680 at 0x4c68a4..0x4c68da: the skill's delay calc (+0x190) above 0, for the client's own player, capped at 12
// (0x4c68b8), goes on the frame clock into 0x7a04fc (FUN_0044ce40); an SQ skill (anim 0x12) only on its sequence's row 0
// (+0x38 under 0x100). d2d's World sets Hero::skill_delay at the same do (Fight::start_delay), so the delay is what it set.
std::uint32_t client_skill_guard(std::uint32_t delay_until, std::uint32_t now_ms) {
    return delay_until > now_ms ? now_ms + std::min(delay_until - now_ms, 12U * 40U) : 0;
}

}  // namespace d2d::game
