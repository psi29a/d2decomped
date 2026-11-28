// SPDX-License-Identifier: GPL-3.0-or-later
// Fight (fight.hpp): casting, targets and the fight's part of the tick. The rest is in this
// directory.
#include "fight.hpp"

#include "ai.hpp"
#include "ai/units.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"
#include "internal.hpp"
#include "level.hpp"
#include "log.hpp"
#include "missile_flight.hpp"
#include "spawning.hpp"

#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <item_price.hpp>
#include <level_ids.hpp>
#include <melee_reach.hpp>
#include <merc.hpp>
#include <monsters.hpp>
#include <overlays.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <unit_size.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

// A monster's or summon's max damage is its last swing's
// (rules::swing_max_damage), the merc's its max damage stats (merc_foe).
// None has a left skill: every FUN_005701b0 call with EDX 1 is a player's
// (FUN_0055c5c0, FUN_0056af80 test unit type 0; FUN_0055da70 /
// FUN_00580280 put a player's saved hands back).
// Diablo's score of a pet or monster (FUN_005e8530 reads any unit's stats
// so): its resists as stats 0x24 / 0x25 / 0x27 / 0x29 / 0x2b, raw, and its
// aura as its right skill (FUN_005701b0 sets it: a merc's FUN_005e4d30, a
// summon's FUN_005c4470, Aura Enchanted's FUN_005a1650) at its level.
// A monster's or summon's max damage is its last swing's (rules::swing_max_damage),
// the merc's its max damage stats (merc_foe).
// None has a left skill: every FUN_005701b0 call with EDX 1 is a player's
// (FUN_0055c5c0, FUN_0056af80 test unit type 0; FUN_0055da70 / FUN_00580280
// put a player's saved hands back).
d2d::rules::DiabloPick unit_pick(const GameData& game_data, std::array<int, 5> res, int aura, int aura_level, int max_damage) {
    d2d::rules::DiabloPick pick;
    pick.max_damage = max_damage;
    std::tie(pick.damage_resist, pick.magic_resist, pick.fire_resist, pick.light_resist, pick.cold_resist) = std::tuple{ res[0], res[1], res[2], res[3], res[4] };
    if (const auto* skill = aura > 0 ? game_data.skills.get(aura) : nullptr) { pick.right_skill = skill->id; pick.right_level = aura_level; pick.right_rank = skill->attack_rank; }
    return pick;
}

// A cast's cost (FUN_0056bfe0): a charged entry's (Hero::charge, for
// that skill) is a charge and no mana, else the skill's mana.

// A cast's cost (FUN_0056bfe0, after the skill's start or do function):
// from a charged entry for that skill, one charge (FUN_0056bec0) and no
// mana; else its mana.
auto Fight::from_charges(int skill) const -> bool { return actor->item_charge.item >= 0 && actor->item_charge.skill == skill; }

// The cast's entry still there at cast time (FUN_00647840): its item
// active, the same level, charges left.
auto Fight::charge_live() const -> bool {
        const auto& cast = actor->item_charge;
        return std::ranges::any_of(charged_skills(actor->character.items),
                                   [&](const ChargedSkill& had) { return had.item == cast.item && had.skill == cast.skill && had.level == cast.level; });
    }

auto Fight::cast_level(int skill) const -> int {
        if (from_charges(skill) && !charge_live()) return 0;
        return skill_level ? skill_level(skill) : 0;
    }

auto Fight::cost_of(const d2d::rules::Skill& skill, int lvl) const -> int { return from_charges(skill.id) ? 0 : d2d::rules::mana_formula(skill, lvl); }

// Whether the player's skill can go now (FUN_00647960), game.exe's status: 0, or the first
// of 3 no row, 7 level 0, 6 aura, 5 passive, 2 the weapon (skill_refused), 1 mana (cost_of),
// 4 restrict (Skill::restrict_form), 1 startmana x 256 out of state inferno, 8 skilldelay on
// and the skill's delay above 0.

// In state 0x72 (Blood Mana's curse, curses[0]) the cost is asked of life (FUN_00647540: stat 6,
// not 8). FUN_00647840's status 2 (an entry's item at 0 charges) never comes: the stat hook takes
// the entry away at 0 (SKILLS_AddOrRemoveItemSkill, skills.md "Item charges"), as charged_skills
// does.
auto Fight::skill_status(int skill, std::uint32_t now_ms) -> int {
        const auto* row = game_data->skills.get(skill);
        if (!row) return 3;
        const int lvl = cast_level(skill);
        if (lvl <= 0) return 7;
        if (row->aura) return 6;
        if (row->passive) return 5;
        if (skill_refused(*game_data, actor->character, skill)) return 2;
        const auto mana = actor->character.stats.values[d2d::d2s::kMana];
        if ((blood_mana() ? actor->character.stats.values[d2d::d2s::kLife] : mana) < cost_of(*row, lvl)) return 1;
        // FUN_00644060: restrict 0 refuses a player in a restrict state (FUN_0063a510: wolf, bear), 2 asks for
        // one and for one of State1..3 (the list ends at the first empty), any other value passes.
        if (row->restrict_form == 0 || row->restrict_form == 2) {
            const bool shifted = std::ranges::any_of(actor->self_states, [&](const auto& held) {
                const auto* held_row = game_data->skills.get(held.skill);
                const auto found = held_row ? game_data->states.find(held_row->aurastate) : game_data->states.end();
                return found != game_data->states.end() && found->second.restrict_form;
            });
            bool listed = false;
            for (const auto& name : row->restrict_states) {
                if (name.empty()) break;
                if (state_of(name)) { listed = true; break; }
            }
            if (row->restrict_form == 0 ? shifted : !(shifted && listed)) return 4;
        }
        if (row->startmana > 0 && mana < std::int64_t(row->startmana) * 256 && now_ms >= actor->burning) return 1;   // FUN_006440f0
        if (now_ms < actor->skill_delay && d2d::rules::eval_calc(game_data->skills, row->delay, calc_env(), skill, lvl) > 0) return 8;   // FUN_006478f0
        return 0;
    }

// The client's ask before a skill goes (FUN_004610c0): status 1, 2 or 4 on a skill with
// AttackNoMana asks again for Attack (0), which goes in its place, silently. 0 or 5 goes:
// the skill that goes.

// Else -1 and the status's line (0x711ddc): 1 needmana, 2..4 impossible, 6..8 none.
auto Fight::ask(int skill, std::uint32_t now_ms) -> int {
        int status = skill_status(skill, now_ms);
        if (const auto* row = game_data->skills.get(skill); row && row->attack_no_mana && (status == 1 || status == 2 || status == 4)) {
            skill = 0;
            status = skill_status(skill, now_ms);
        }
        if (status == 0 || status == 5) return skill;
        if (status == 1) need_mana(now_ms);
        else if (status >= 2 && status <= 4) impossible(now_ms);
        return -1;
    }

// The end of the swing skill's do (FUN_0056f7f0 at LAB_0056fa93): its delay (Skills.txt
// `delay`, frames) above 0 puts the player in skilldelay till now plus it (FUN_0056ef90).
// An SQ skill (anim 0x12) only while its sequence position (unit +0x38, units.md) is under
// a row: only an event on row 0, Charge's, whose delay is 0. Dragon Flight's events (rows 9,
// 14) never start it, so the sequence path doesn't call this. A shift back (shift_back)
// starts it too (FUN_0056f020).
auto Fight::start_delay(std::uint32_t now_ms) -> void {
        const auto* row = game_data->skills.get(actor->swing_skill);
        if (!row) return;
        const int lvl = skill_level ? skill_level(row->id) : 1;
        if (const int delay = d2d::rules::eval_calc(game_data->skills, row->delay, calc_env(), row->id, lvl); delay > 0) actor->skill_delay = now_ms + std::uint32_t(delay) * 40;
    }

auto Fight::pay(const d2d::rules::Skill& skill, int lvl) -> void {
        auto& mana = actor->character.stats.values[d2d::d2s::kMana];
        actor->last_paid = 0;
        if (from_charges(skill.id)) (void)use_charge(actor->character.items, actor->item_charge);
        else if (blood_mana()) { if (skill.mana != 0 || skill.lvlmana != 0) (void)blood_pay(d2d::rules::mana_cost(skill, lvl)); }   // FUN_0056bfe0 -> FUN_005d2b60
        else { actor->last_paid = d2d::rules::mana_paid(skill, lvl, mana); mana -= actor->last_paid; }
    }

auto Fight::blood_mana() const -> bool {
        const auto* curse = game_data->skills.get(actor->curses[0].skill);
        return curse && curse->auratarget == "blood_mana";
    }

// Short of it the state goes and life is set to 1 (0x100), false; paid, the state goes once life is
// under the curse's Param5 (+0x158) points.
auto Fight::blood_pay(std::int64_t cost) -> bool {
        auto& life = actor->character.stats.values[d2d::d2s::kLife];
        const auto* curse = game_data->skills.get(actor->curses[0].skill);
        if (life < cost) { actor->curses[0] = {}; life = 0x100; return false; }
        life -= cost;
        if (curse && life < std::int64_t(curse->par[4]) * 256) actor->curses[0] = {};
        return true;
    }

// A skill with no start function pays in its do, on the cast mode's action frame (FUN_0056f7f0 ->
// FUN_0056bfe0): a cast cut off before it costs nothing, and short of the mana by then it pays nothing
// (mana_paid) and stands. A start function pays at the command unless the row has UseManaOnDo
// (FUN_0056f640, 0x56f78c; V read): Plague Javelin and Blade Fury have both and pay in the do, not
// modelled here. cast_summon and start_swing still pay at the command through pay(); in game.exe
// their srvstfunc-none rows (summons, Smite, Frenzy, Double Swing, Hunger) also pay in the do
// (0x56f773, 0x56fa57; V read): not moved, not claimed.
// A get-hit start clears the queued event (FUN_0057fe30 -> FUN_00553990 -> FUN_00540e60 at
// 0x57fe78; V read), so a cut cast pays nothing; the oracle models that, it does not run it.
auto Fight::owe(const d2d::rules::Skill& skill, int lvl) -> void {
        if (skill.srvstfunc == ServerStartFunction::kNone) actor->owed = { skill.id, lvl };
        else pay(skill, lvl);
    }

auto Fight::settle_cast() -> void {
        const auto owed = std::exchange(actor->owed, {});
        actor->last_paid = 0;                                   // nothing owed: nothing to give back
        const auto* skill = owed.skill >= 0 ? game_data->skills.get(owed.skill) : nullptr;
        if (skill) pay(*skill, owed.lvl);
    }

// Holy Shield (FUN_005c9480): the holyshield state for auralencalc
// ticks, its aurastats (toblock dm56) on the player. Without a shield
// it is refused before this (weapon_gate: itypea1 shld).

// Its row has no auraevent and no passivestat, so the aura events
// (+0x84) and the passive part (FUN_005c6dc0) add nothing (skills.md).
auto Fight::cast(int skill, std::uint32_t now_ms) -> bool {
        const auto* skill_row = game_data->skills.get(skill);
        if (!skill_row || dead() || actor->pmode >= 0 || !self_cast(*skill_row)) return false;
        if (ask(skill, now_ms) != skill) return false;
        const int lvl = cast_level(skill);
        owe(*skill_row, lvl);
        actor->swing_skill = skill;
        actor->attack_mon = -1;
        set_pmode(kModeSC, now_ms);
        actor->pstruck = false;
        return true;
    }

// Frenzy's state (FUN_005d8c70): each hit that lands raises it a level,
// up to the skill's, for auralencalc ticks; its aurastats (velocitypercent
// dm34, attackrate dm56) are taken at that level.
auto Fight::frenzy(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const auto env = calc_env();
        const auto found = std::ranges::find(actor->self_states, skill.id, &SelfState::skill);
        const int frenzy_charges = std::min(found == actor->self_states.end() ? 1 : found->level + 1, lvl);
        const auto until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data->skills, skill.auralen, env, skill.id, lvl), 1)) * 40;
        if (found == actor->self_states.end()) actor->self_states.push_back({ skill.id, frenzy_charges, until, actor->next_effect_instance() });
        else *found = { skill.id, frenzy_charges, until, found->instance };
    }

auto Fight::next_target() -> bool {
        const auto* skill = game_data->skills.get(actor->swing_skill);
        if (skill && skill->srvdofunc == ServerDoFunction::kRepeatedHit) { other_target(); return actor->attack_mon >= 0; }
        return actor->attack_mon >= 0 && monsters[std::size_t(actor->attack_mon)].alive();
    }

// The range of the weapon in hand (FUN_00622870: FUN_0063c9b0's, the
// right hand's else the left's; its rangeadder), 0 with none.
auto Fight::weapon_range() const -> int {
        for (const int slot : { d2d::d2s::body_location::kRightArm, d2d::d2s::body_location::kLeftArm }) {
            const auto held = std::ranges::find_if(actor->character.items, [&](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == slot; });
            if (held == actor->character.items.end()) continue;
            const auto info = game_data->rules.item_info.find(held->code);
            if (info != game_data->rules.item_info.end() && d2d::rules::type_is(game_data->rules, info->second.type, "weap") && (held->flags & 0x4100u) == 0) return info->second.range_adder;
        }
        return 0;
    }

auto Fight::in_reach() const -> std::vector<int> {
        std::vector<int> out;
        if (d2d::rules::level_ids::is_town(level->id)) return out;
        auto sub = [](float cells) { return int(std::floor(cells * 5)); };
        const auto wall = [&](std::uint16_t mask) { return [this, mask](int at_x, int at_y) { return level->blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, mask); }; };
        const int range = weapon_range(), radius = range + 4, player_x = sub(actor->player.x), player_y = sub(actor->player.y);
        const auto rooms = near_rooms(*game_data, *level, actor->player.x, actor->player.y);
        for (std::size_t i = 0; i < monsters.size(); ++i) {
            const auto& monster = monsters[i];
            if (!monster.targetable() || monster.align == 2) continue;   // a converted one is no foe (FUN_00554200)
            if (!std::ranges::contains(rooms, std::pair{ level, room_of(*level, monster.unit.x, monster.unit.y) })) continue;
            const int dx = sub(monster.unit.x) - player_x, dy = sub(monster.unit.y) - player_y;
            if (dx * dx + dy * dy > radius * radius) continue;
            const int size = game_data->monsters.types[std::size_t(monster.type)].size;
            const int apart = d2d::rules::unit_distance(dx, dy, 2, size);
            const bool tentacle = monster.type == int(game_data->mon_bin[0x102]) || monster.type == int(game_data->mon_bin[0x105]);
            const bool melee = (tentacle && apart < range + 8) || apart < 1
                            || (apart <= range + 1 && !d2d::rules::sight_blocked(player_x, player_y, 2, player_x + dx, player_y + dy, size, wall(0x804)));
            if (melee && !d2d::rules::sight_blocked(player_x, player_y, 0, player_x + dx, player_y + dy, 0, wall(0x04))) out.push_back(int(i));
        }
        return out;
    }

auto Fight::engage(std::uint32_t now_ms) -> std::optional<std::pair<float, float>> {
        if (actor->attack_mon < 0 || actor->pmode >= 0) return std::nullopt;
        const auto& target = monsters[std::size_t(actor->attack_mon)];
        // Dead, or turned to the player's side while the button was held (Conversion): dropped, as
        // World::apply turns a fresh command on it away.
        if (!target.alive() || target.ally_not_interact(*game_data, mon_level ? mon_level->id : 0)) { actor->attack_mon = -1; return std::nullopt; }
        const auto* skill = game_data->skills.get(actor->attack_skill);
        if (skill && (missile_skill(*skill) || spot_skill(*skill))) {   // from here, at it
            cast_missile(actor->attack_skill, target.unit.x, target.unit.y, now_ms);
            return std::nullopt;
        }
        if (skill && moving_skill(*skill) && (skill->srvdofunc != ServerDoFunction::kCharge || std::hypot(target.unit.x - actor->player.x, target.unit.y - actor->player.y) > kMeleeReach)) {
            actor->player.dir = direction16(target.unit.x - actor->player.x, target.unit.y - actor->player.y);
            actor->move_x = target.unit.x; actor->move_y = target.unit.y;                  // Whirlwind to it, Charge and Leap Attack at it
            start_swing(now_ms);
            return std::nullopt;
        }
        if (melee_reach_to(target, false)) {
            actor->player.dir = direction16(target.unit.x - actor->player.x, target.unit.y - actor->player.y);
            start_swing(now_ms);
            return std::nullopt;
        }
        return std::pair{ target.unit.x, target.unit.y };
    }

// The monster is in the player's melee reach (FUN_00622c40: the unit distance and the line of mask
// 0x804), as the walk's end has it (`blow` false) or as the blow's to-hit test does (true).
auto Fight::melee_reach_to(const Monster& target, bool blow) const -> bool {
        // FUN_00622c40(player, monster, extra): within the weapon's range + extra + 1 by FUN_00641530
        // (always at 0 or less; a tentacle or its head within the range + 8) and no wall of mask 0x804
        // on the line (FUN_00622aa0). The client ends its walk, and the server starts the swing, with
        // `extra` 1 while the monster moves (FUN_00622dc0: FUN_00480c80 at 0x480d0f, FUN_00549ba0 at
        // 0x549cc8); the blow asks again with `extra` 1 for a player (FUN_0057ec10 at 0x57eca6).
        const auto sub = [](float cells) { return int(std::floor(cells * 5)); };
        const int from_x = sub(actor->player.x), from_y = sub(actor->player.y), to_x = sub(target.unit.x), to_y = sub(target.unit.y);
        const int size = game_data->monsters.types[std::size_t(target.type)].size;
        const bool tentacle = target.type == int(game_data->mon_bin[0x102]) || target.type == int(game_data->mon_bin[0x105]);
        const bool vulture = target.type == int(game_data->mon_bin[0x6e]);
        const int mode = target.unit.walking ? 2 : vulture && target.mode == "S1" ? 8 : vulture && target.mode == "S2" ? 9 : 1;
        const int extra = blow || d2d::rules::unit_moving(d2d::rules::UnitType::kMonster, mode, vulture ? 0x6e : -1) ? 1 : 0;
        const auto wall = [this](int at_x, int at_y) { return level->blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x804); };
        return d2d::rules::melee_reach(d2d::rules::unit_distance(from_x - to_x, from_y - to_y, 2, size), weapon_range(), tentacle, extra,
                                       [&] { return d2d::rules::sight_blocked(from_x, from_y, 2, to_x, to_y, size, wall); });
    }

auto Fight::crowd(Crowd& crowd_out) -> void {
        for (auto& monster : monsters)
            if (monster.targetable() && std::abs(monster.unit.x - actor->player.x) < 12 && std::abs(monster.unit.y - actor->player.y) < 12) {
                crowd_out.units.push_back(&monster.unit);
                crowd_out.held.emplace_back(&monster.landing_mark, &monster.leaping);   // its landing while it leaps (FUN_005d9d50), one taken this frame too
            }
        for (auto& pet : actor->pets)                                 // pets stand in the way too (Bone Wall)
            if (pet.monster.alive() && pet.where == level) crowd_out.units.push_back(&pet.monster.unit);
    }

// The same about (x, y): the monsters within 12 of it and every hero's pets on the level (FUN_0064d870's
// grid holds every unit's stamp, wherever the acting player stands).
auto Fight::crowd_around(Crowd& crowd_out, float x, float y) -> void {
        for (auto& monster : monsters)
            if (monster.targetable() && std::abs(monster.unit.x - x) < 12 && std::abs(monster.unit.y - y) < 12) {
                crowd_out.units.push_back(&monster.unit);
                crowd_out.held.emplace_back(&monster.landing_mark, &monster.leaping);   // its landing while it leaps (FUN_005d9d50), one taken this frame too
            }
        for (auto* hero : heroes)
            for (auto& pet : hero->pets)
                if (pet.monster.alive() && pet.where == level) crowd_out.units.push_back(&pet.monster.unit);
    }

auto Fight::player_foe(std::uint32_t now_ms) -> Foe {
        Foe foe{ actor->player.x, actor->player.y, int(actor->character.stats.get(d2d::d2s::kLevel)), !dead(), actor->player.walking, actor->player_combat };
        foe.running = actor->owner_mode == 3;                   // RN (World: walking with run on and stamina)
        foe.neutral = actor->pmode < 0 && actor->owner_mode == 1;   // mode 1: no swing, cast or get-hit, not moving
        foe.id = player_foe_id();
        foe.room_stamp = actor->player.room_stamp;
        foe.seed = &actor->seed;
        foe.life_pct = int(std::int64_t(actor->character.stats.get(d2d::d2s::kLife)) * 100 / std::max<std::int64_t>(actor->character.stats.get(d2d::d2s::kMaxLife), 1));
        foe.max_life = int(actor->character.stats.values[d2d::d2s::kMaxLife]); foe.max_mana = int(actor->character.stats.values[d2d::d2s::kMaxMana]);
        // Diablo's score of the player (FUN_005e8530).
        const auto stat = [&](int id) { return int(actor->psum[std::size_t(id)]); };
        auto& pick = foe.pick;
        pick.damage_resist = stat(0x24); pick.magic_resist = stat(0x25); pick.fire_resist = stat(0x27); pick.light_resist = stat(0x29); pick.cold_resist = stat(0x2b);
        pick.max_damage = stat(0x16) + stat(0x31) + stat(0x33) + stat(0x35) + stat(0x37) + (stat(0x3a) >> 8);
        pick.state_b = now_ms < actor->chilled;
        const auto hand = [&](std::uint32_t id, int& out_id, int& out_level, int& out_rank) {
            const auto* skill = game_data->skills.get(int(id));
            out_id = skill ? skill->id : -1; out_level = skill && skill_level ? skill_level(skill->id) : skill ? 1 : 0; out_rank = skill ? skill->attack_rank : 0;
        };
        hand(actor->character.header.left_skill, pick.left_skill, pick.left_level, pick.left_rank);
        hand(actor->character.header.right_skill, pick.right_skill, pick.right_level, pick.right_rank);
        on_foe_states(foe, 0, now_ms);
        return foe;
    }

auto Fight::merc_foe(std::uint32_t now_ms) -> Foe {
        if (now_ms >= actor->curses[1].until) actor->curses[1] = {};   // merc_fighter reads one still on
        for (auto& state : actor->merc_potion_states) if (now_ms >= state.until) state = {};
        Foe foe{ actor->merc ? float(actor->merc->unit.x) : 0.f, actor->merc ? float(actor->merc->unit.y) : 0.f, actor->merc_st.level, actor->merc && actor->merc->mode != "DT",
                 actor->merc && actor->merc->unit.walking, merc_fighter() };
        foe.fighter.prime_evil_pct = 200;                // a hireling under a prime evil's blow (FUN_0057c060)
        foe.pet = true;                                  // the merc: its MonStats class's size
        if (actor->merc) { foe.room_stamp = actor->merc->unit.room_stamp; foe.seed = &actor->merc->seed; }
        foe.id = merc_foe_id();
        foe.listed = actor->merc ? actor->merc_listed : 0;
        foe.neutral = actor->merc && actor->merc->mode == "NU";
        foe.life_pct = actor->merc ? int(std::int64_t(actor->merc->hit_points) * 100 / std::max(merc_max_life(), 1)) : 0;
        if (const int row = actor->merc_npc ? game_data->monsters.row(actor->merc_npc->id) : -1; row >= 0) {
            const auto& type_info = game_data->monsters.types[std::size_t(row)];
            foe.size = type_info.size; foe.threat = type_info.threat;
            foe.knock_under = type_info.large ? 0x20 : type_info.small ? 0x80 : 0x40;   // as a monster's (monster_foe_props); crush a hireling's 10
        }
        // The merc's curse's damageresist (Amplify Damage's -100 %) for
        // Diablo's score; merc_fighter has all its aurastats.
        int curse_dr = 0;
        for (const auto& [id, value] : curse_stats(actor->curses[1], calc_env()))
            if (id == d2d::d2s::kDamageResist) curse_dr += value;
        if (actor->merc) {                                          // its stats for Diablo's score: base + gear + curse, raw (no penalty or cap)
            const auto sum = merc_gear().sum;
            const auto stat = [&](int id) { return int(sum[std::size_t(id)]); };
            int aura_level = 0;
            for (std::size_t k = 0; k < actor->merc_ids.size(); ++k) if (actor->merc_ids[k] == actor->merc_aura) aura_level = actor->merc_levels[k];
            foe.pick = unit_pick(*game_data, { stat(0x24) + curse_dr, stat(0x25), actor->merc_st.resist + stat(0x27), actor->merc_st.resist + stat(0x29), actor->merc_st.resist + stat(0x2b) },
                                 actor->merc_aura, aura_level,
                                 stat(0x16) + stat(0x31) + stat(0x33) + stat(0x35) + stat(0x37) + (stat(0x3a) >> 8));   // 0x16 holds no base (merc_fighter)
        }
        on_foe_states(foe, 1, now_ms);
        return foe;
    }

// The states on the player (which 0) or the merc (1) the AIs read (their skills'
// +0x82; the player's shrine state, a curse to FUN_00625760), and the zealots'
// spare (FUN_0065c310(pcdata +0x10 + diff * 4, 0x15, 0): a pet's owner's, FUN_0058f090).
auto Fight::on_foe_states(Foe& foe, std::size_t which, std::uint32_t now_ms) const -> void {
        for (const auto* held : { &actor->curses[which], &actor->marks[which] })
            if (const auto* skill = now_ms < held->until ? game_data->skills.get(held->skill) : nullptr) foe.states.push_back(skill->auratarget);
        if (const auto& boost = actor->boost; which == 0 && now_ms < boost.until && boost.shrine > 0 && std::size_t(boost.shrine) < game_data->shrines.size())
            if (const auto state = d2d::rules::shrine_state(game_data->shrines[std::size_t(boost.shrine)].code); !state.empty()) foe.states.push_back(state);
        foe.a3q5_done = zealots_spare();
    }

auto Fight::zealots_spare() const -> bool {
        return d2d::rules::quest::bit(actor->character.header.quests[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))], 0x15, 0);
    }

// A LastCollide row passes the unit it struck last (FUN_005a8730); a
// Pierce row with a charge left flies on, as skill_missile_hits'.
auto Fight::plain_missile_hits(Missile& missile, std::size_t monster_index, std::uint32_t now_ms) -> bool {
        // The merc's plain shot (a missile row with no Skill, so the owner's weapon damage x SrcDamage / 128, FUN_0064b860)
        // as its swing's blow (merc_strike): its gear's elements, leech, Crushing Blow (halved), Open Wounds, item_slow,
        // which a record with SrcDamage carries into the owner's event 6 (record flag 0x20, 0x5ad893).
        // Its row's own physical (rogue1's MinDamage / MaxDamage 1, Fight::merc_shot) goes in the roll with the weapon's.
        // FUN_005adf10 runs hit function 4 (FUN_005b07a0) before the arrow's record: the merc's explodingarrow
        // makes explodingarrowexp2 with RogueMissile as its skill. That skill has no damage, but the sub-missile's
        // other hit effects still run when it lands.
        if (missile.by_merc && missile.info && missile.info->hit_func == 4) {
            const auto rogue_skill = game_data->skills.by_name.find("RogueMissile");
            if (rogue_skill != game_data->skills.by_name.end()) {
                Missile hit = missile;
                hit.skill = rogue_skill->second;
                burst(hit, now_ms, int(monster_index));
            }
        }
        d2d::rules::Swing flight{ .srcdam = missile.info ? missile.info->src_damage : 128 };
        flight.missile = true;
        if (missile.by_merc && missile.info) {
            flight.row_lo = missile_phys(*game_data, *missile.info, merc_calc_env(), -1, missile.level, false);
            flight.row_hi = missile_phys(*game_data, *missile.info, merc_calc_env(), -1, missile.level, true);
        }
        auto struck = missile.by_merc ? merc_target(monster_index) : target_of(monster_index);
        struck.life_steal_divisor = struck.mana_steal_divisor = 1;   // a hireling's leech: no divisor (merc_target; the plain path's other owners too)
        // The to-hit and the crits on its owner's seed, the physical on its own, the block on the struck unit's.
        flight.crit = missile.crit;
        auto& own_seed = missile.seed ? *missile.seed : rng;
        auto& struck_seed = monsters[monster_index].seed;
        auto blow = missile.by_merc && actor->merc ? d2d::rules::player_blow(merc_fighter(), struck, actor->merc_st.level, owner_seed(missile), flight, &own_seed, &struck_seed)
                                                   : d2d::rules::player_blow(d2d::rules::simple_fighter(missile.min, missile.max, missile.attack_rating), struck, missile.by_merc ? actor->merc_st.level : missile.level,
                                                                             owner_seed(missile), {}, &own_seed, &struck_seed);
        if (missile.info) blow.hit_class = missile.info->hit_class;   // an arrow's, not hand to hand's (as a skill's missile, Missiles.txt HitClass)
        land(monster_index, blow, false, now_ms, merc_foe_id());
        monster_return_fire(monster_index, missile, true, now_ms);
        missile.last_hit = int(monster_index);
        missile.struck->push_back(int(monster_index));
        // FUN_005adf10 -> FUN_005ada80: a charge left flies on, one fewer, while the merc has skill_pierce or item_pierce.
        if (missile.info && missile.info->pierce && missile.pierce > 0 && pierce_armed(true)) {
            --missile.pierce;
            return false;
        }
        return true;
    }

auto Fight::near_players(float x, float y) const -> bool {
        return std::ranges::any_of(heroes, [&](const Hero* hero) { return std::abs(x - hero->player.x) < 30 && std::abs(y - hero->player.y) < 30; });
    }

auto Fight::restamp() -> void {
        if (!level) return;
        const auto seen = [&](UnitState& unit) {
            const int room = room_of(*level, unit.x, unit.y);
            if (room == unit.room_seen && unit.room_level == level) return;   // the same room of the same level
            unit.room_seen = room;
            unit.room_level = level;
            unit.room_stamp = ++room_stamps;
            if (unit.made_stamp == 0) unit.made_stamp = unit.room_stamp;
        };
        for (Hero* hero : heroes) {
            seen(hero->player);
            if (hero->merc) seen(hero->merc->unit);
            for (auto& pet : hero->pets) if (pet.where == level) seen(pet.monster.unit);
        }
        for (auto& monster : monsters) seen(monster.unit);
    }

auto Fight::world(bool in_moor, std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> void {
        Hero& acting = *actor;
        if (now_ms >= acting.curses[0].until) acting.curses[0] = {};   // its state's end, in town too (blood_mana asks for it gone)
        // The mercs' curses and potion states end in town too, each hero's (merc_turn runs for all of them
        // below): merc_fighter (a sheet, a walk) reads one still held, and merc_foe, which drops them out of
        // town, doesn't run there.
        const auto merc_states_end = [now_ms](Hero& hero) {
            if (now_ms >= hero.curses[1].until) hero.curses[1] = {};
            for (auto& state : hero.merc_potion_states) if (now_ms >= state.until) state = {};
        };
        merc_states_end(acting);
        for (Hero* hero : heroes) merc_states_end(*hero);
        // Monsters think while a player is near (D2 runs the rooms around
        // each player); what they hit comes off that player's life, and a hit
        // of a twelfth of max life or more makes them flinch (GH).
        // Armageddon / Hurricane's next run in town takes its state off
        // (FUN_005c8520 / FUN_005c8380 on FUN_0061ab00); buff_tick runs only out of town.
        if (in_town(level))
            std::erase_if(actor->self_states, [&](const SelfState& state) {
                const auto* skill = game_data->skills.get(state.skill);
                return skill && skill->srvdofunc == ServerDoFunction::kStormAroundCaster
                    && now_ms >= (skill->aurastate == "armageddon" ? actor->armageddon_next : actor->hurricane_next);
            });
        if (in_moor) {
            // Each player's side: the player and the merc, then everyone's
            // pets; the monsters (Confuse, Attract) after them.
            restamp();
            struct Side { Hero* hero; std::size_t at = 0, pets = 0; };
            std::vector<Side> sides;
            std::vector<Foe> foes;
            for (Hero* hero : heroes) {
                act_as(*hero);
                sides.push_back({ hero, foes.size() });
                foes.push_back(player_foe(now_ms));
                foes.push_back(merc_foe(now_ms));
            }
            monster_auras(foes, now_ms);
            for (auto& side : sides) {
                act_as(*side.hero);
                side.pets = foes.size();
                for (auto& pet : actor->pets) foes.push_back(pet_foe(pet));
                for (std::size_t k = side.pets; k < foes.size(); ++k) foes[k].a3q5_done = zealots_spare();
            }
            // With Confuse or Attract about, every monster is a foe too, after
            // the players' lists (lists 8 / 9, FUN_005dd0b0 mode 5's units),
            // hit as a player is (combat.md). Its death drops; the player
            // gets the experience when the killer or the dead one carries
            // its curse (FUN_0057e7b0: the stat list's owner, flag 0x800).
            // The monsters' missiles fly with these foes too (missiles_update).
            // A monster's poison reaches a monster foe through take_blows, and
            // monster_blow uses that foe's block chance as it does a player's.
            // A monster has no mana (FUN_00573cb0 sets no stat 8), so a Mana Burn
            // on one takes nothing, as here.
            // A poison or bleeding kill is its state's owner's (Monster::poison_owner, monster_dots).
            const std::size_t allies = foes.size();
            // `for_missiles`: a monster's CollideFriend missile in flight (FUN_005a8730) strikes its owner's
            // friends, so the monsters are foes then too, aligned or not.
            const auto monster_foes = [&](bool for_missiles) {
                const bool collide_friend = for_missiles && std::ranges::any_of(missiles, [](const Missile& missile) {
                    return missile.owner >= 0 && !missile.friendly && missile.info && missile.info->collide_friend;
                });
                if (collide_friend || std::ranges::any_of(monsters, [](const Monster& monster) { return monster.align || monster.set_kind; }))
                    for (auto& monster : monsters) {
                        const auto target = monster.target(*game_data);
                        auto fighter = d2d::rules::simple_fighter(monster.stats.a1_min, monster.stats.a1_max, monster.stats.to_hit, target.armor_class);
                        fighter.block = target.block;
                        fighter.dr_pct = target.res[0];       // its physical resistance (FUN_0057bf80's percent, as player_blow's res[0])
                        for (std::size_t k = 0; k < 4; ++k) fighter.res[k] = std::min(target.res[2 + k], 95);
                        const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
                        Foe& foe = foes.emplace_back(Foe{ monster.unit.x, monster.unit.y, monster.stats.level, monster.targetable() && monster.mode != "DT", monster.unit.walking, fighter });
                        foe.size = type_info.size; foe.threat = type_info.threat; foe.monster = &monster; foe.id = monster.id; foe.room_stamp = monster.unit.room_stamp;
                        foe.seed = &monster.seed;
                        monster_foe_props(*game_data, monster, foe);
                        foe.pick = unit_pick(*game_data, { target.res[0], target.res[1], target.res[2], target.res[3], target.res[4] }, monster.aura, monster.aura_lvl,
                                             d2d::rules::swing_max_damage(monster.swing_stats(), monster.swing_mode));
                        foe.pick.state_b = now_ms < monster.chill_until;   // state 0xb, cold
                    }
            };
            monster_foes(false);
            std::vector<Monster> born;
            auto* rooms_in_play = &spawning.levels[level];
            act_as(*heroes.front());
            for (std::size_t i = 0; i < monsters.size(); ++i) {
                auto& monster = monsters[i];
                if (!near_players(monster.unit.x, monster.unit.y)) continue;
                const std::size_t made_from = missiles.size();
                const bool thorns_kill = monster_update(*game_data, *level, monster, foes, rng, now_ms, elapsed, crowd, missiles, monsters, &born, &area_seen, &open_door,
                                                        rooms_in_play, game_players(), &spawning);
                // What its think made takes its unit seed off the game seed now, in the order made, as game.exe
                // seeds a missile as it's made (a visual-only one none).
                seed_missiles(made_from);
                if (thorns_kill) killed(i, now_ms);              // on a player's thorns
            }
            const auto cursed = [&](const Monster& monster) { return monster.curse.skill >= 0 && now_ms < monster.curse.until; };
            const auto monster_blows = [&] {
                for (std::size_t i = allies; i < foes.size(); ++i) {
                    const auto monster_index = i - allies;
                    auto& struck = monsters[monster_index];
                    const bool was_alive = struck.alive();
                    // One damage record a landed blow (FUN_0057c6c0 then FUN_0057cee0's monster branch, which a
                    // pet or a hireling takes too): its own armor pool, then its get-hit on its own class and mark.
                    bool dead = was_alive && take_blows(struck, foes[i], now_ms);
                    // Its Crushing Blows after the hits, less its own damageresist
                    // (FUN_005bffc0), and a knockback as the player's (knock_back).
                    for (const int divisor : foes[i].crushed)
                        if (!dead && struck.alive()) dead = hurt(*game_data, struck, int(crushing_blow(struck.hit_points, divisor, struck.target(*game_data).res[0])), now_ms);
                    if (!dead && was_alive && struck.alive() && foes[i].knocked_from) knock_back(struck, foes[i].knocked_from->first, foes[i].knocked_from->second, now_ms);
                    if (dead)
                        killed(monster_index, now_ms, cursed(struck) || std::ranges::any_of(foes[i].melee_by, [&](int striker) { const int at_index = this->monster_index(striker); return at_index >= 0 && cursed(monsters[std::size_t(at_index)]); }));
                }
            };
            monster_blows();
            for (const auto& side : sides)
                if (foes[side.at].blessed) side.hero->wisp_blessing = { foes[side.at].blessed, now_ms + 0x1a5e00u * 40 };   // FUN_006251f0: 0x1a5e00 frames
            // Impregnate's state off at a death (FUN_005d21b0): the skill's summon
            // (painworm1, else 0x227) in its summode at the dead one
            // (FUN_005b2490: the level's own of its family, level_monster, by
            // FUN_005b23c0 -> FUN_005b2a00 radius 1 in the dead one's room,
            // spawn_spot; none: no worm).
            for (auto& monster : monsters)
                if (!monster.alive() && std::exchange(monster.impregnated, false))
                    if (const auto* impregnate = game_data->skills.get(game_data->skills.by_name.contains("Impregnate") ? game_data->skills.by_name.at("Impregnate") : -1)) {
                        const auto worm = game_data->monsters.by_id.find(impregnate->summon);
                        const int worm_type = level_monster(*game_data, *level, worm != game_data->monsters.by_id.end() ? int(worm->second) : int(game_data->mon_bin[0x227]));
                        int at_x = 0, at_y = 0;
                        if (!spawn_spot(*game_data, *level, crowd, &spawning.levels[level], worm_type, int(std::floor(monster.unit.x * 5)), int(std::floor(monster.unit.y * 5)), 1,
                                        room_of(*level, monster.unit.x, monster.unit.y), at_x, at_y)) continue;
                        auto young = make_monster(*game_data, worm_type, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, game_players(), level->id,
                                                  d2d::rules::Rng{ spawning.game.next() }, spawning.region(level->id));
                        young.aware = true;
                        set_mode(*game_data, young, impregnate->summode.empty() ? "NU" : impregnate->summode, now_ms);
                        d2d::log::info("a painworm comes out of {}", monster.npc.name);
                        born.push_back(std::move(young));
                    }
            foes.resize(allies);
            std::vector<int> born_ids;
            for (auto& young : born) {
                if (young.owner_born >= 0 && std::size_t(young.owner_born) < born_ids.size()) young.party_owner = born_ids[std::size_t(young.owner_born)];
                add_monster(std::move(young));
                born_ids.push_back(next_id - 1);
            }
            boss_events(foes, now_ms);
            monster_dots(now_ms, elapsed);
            for (auto& monster : monsters) { monster.in_aura = false; monster.res_cut = {}; }
            // Each pet foe's blows and Crushing Blows the pet loop applied (a monster missile's come after: missiles_update).
            std::vector<std::size_t> pet_blows_taken(allies, 0), pet_crushes_taken(allies, 0);
            for (const auto& side : sides) {
                act_as(*side.hero);
                if (now_ms >= actor->curses[0].until) actor->curses[0] = {};   // its state's end (blood_mana asks for it gone)
                monster_states(now_ms);
                buff_events(foes[side.at], now_ms);
                if (actor->merc) merc_buff_events(foes[side.at + 1], now_ms);
                buff_tick(now_ms);
                aura_pulse(now_ms);
                for (auto& monster : monsters) monster.in_aura = monster.in_aura || (monster.alive() && in_aura(monster));   // its auratargetstate shows (states.md)
                // Its resists as the client reads them (FUN_004526c0: GetUnitStat, the states' stats in): the curse
                // and this side's auras; two sides' Convictions are one state, so the deepest cut stands.
                for (std::size_t i = 0; i < monsters.size(); ++i)
                    if (auto& monster = monsters[i]; monster.alive() && (monster.in_aura || monster.curse.skill >= 0 || !monster.states.empty() || monster.self_state.skill >= 0)) {
                        const auto base = monster.target(*game_data).res, now = target_of(i).res;
                        for (std::size_t k = 0; k < 6; ++k) monster.res_cut[k] = std::min(monster.res_cut[k], now[k] - base[k]);
                    }
                for (std::size_t k = 0; k < actor->pets.size(); ++k) {
                    auto& pet = actor->pets[k];
                    const auto& foe = foes[side.pets + k];
                    for (const auto& blow : foe.blows) pet_hurt(pet, blow.damage, now_ms, blow.hit_class, blow.marked, blow.frac);   // a record a blow, as the monsters'
                    for (const int divisor : foe.crushed) pet_crushed(pet, divisor, foe.pick.damage_resist, now_ms);   // after the hits (FUN_005bffc0)
                    pet_poisoned(pet, foe, now_ms);
                    if (foe.knocked_from && pet.monster.alive()) knock_back(pet.monster, foe.knocked_from->first, foe.knocked_from->second, now_ms);
                    clay_slows(pet, foe, now_ms);
                    if (const auto curse = monster_curse(foe.cursed, now_ms); curse.skill >= 0 && pet.monster.alive()) { pet.curse = curse; pet_buffs(pet); }
                    if (foe.flashed >= 0) d2d::rules::add_one_shot(pet.monster.one_shots, foe.flashed, now_ms);
                    pet_blows_taken[side.pets + k] = foe.blows.size();
                    pet_crushes_taken[side.pets + k] = foe.crushed.size();
                    foes[side.pets + k].poison = foes[side.pets + k].poison_ticks = 0;   // taken: what's left after the missiles is theirs
                    foes[side.pets + k].knocked_from.reset();
                }
                pets_turn(now_ms, elapsed, crowd);
                // pets_turn may drop pets (and shift the rest): each pet foe's seed back on its own pet, found by
                // id; one gone is no longer there to strike (CodeRabbit on #815).
                rebind_pet_foes(std::span(foes).subspan(side.pets), actor->pets);
                player_shots(now_ms);
            }
            missile_tick(now_ms);
            // The merc's arrows strike the first live monster they reach.
            const auto reach = [&](float x, float y) { return missile_rooms(*game_data, *level, x, y, &spawning); };
            // A missile's sounds (FUN_004cd540, FUN_004d2d70): its TravelSound from its first frame,
            // on it while it flies; its HitSound where it struck (gone before its end).
            struct Flying { int sound; std::uint32_t dies; float x, y; int hit_sound; };
            std::vector<Flying> flying;
            for (auto& missile : missiles) {
                if (!missile.info) continue;
                const int phase = int((std::uint64_t(now_ms - missile.born) * std::uint32_t(missile.info->anim_speed)) / (40u * 16u));
                const int length = std::max(missile.info->anim_len, 1);
                if (missile.info->prog_sound > 0 && phase % length == length - 2 && phase != missile.prog_sound_phase) {
                    cues.cue_on(missile.sound ? unit_key(kSoundOnMissile, missile.sound) : 0, missile.info->prog_sound, now_ms, missile.x, missile.y);
                    missile.prog_sound_phase = phase;
                }
                if (missile.sound == 0) {
                    missile.sound = ++missile_sounds;
                    cues.cue_on(unit_key(kSoundOnMissile, missile.sound), missile.info->travel_sound, now_ms, missile.x, missile.y, missile.sound);
                }
                flying.push_back({ missile.sound, missile.dies, missile.x, missile.y, missile.info->hit_sound });
            }
            monster_foes(true);                                  // a monster's missile strikes the monsters hostile to its owner (FUN_005a8730)
            missiles_update(*level, missiles, foes, rng, now_ms, reach, [&](Missile& missile) {
                if (Hero* caster = hero_of(missile.caster)) act_as(*caster);
                merc_blow = casting_merc = missile.by_merc;
                const bool moving = missile.velocity_x != 0 || missile.velocity_y != 0;
                if (missile.skill >= 0 && bolt_heals_ally(missile, now_ms)) return true;   // Holy Bolt on a party member: spent
                for (std::size_t i = 0; i < monsters.size(); ++i) {
                    auto& monster = monsters[i];
                    if (!monster.targetable()) continue;
                    // FUN_005a8730: the player's side's missile passes a monster on its side (rogue2, a converted
                    // one) by, unless CollideFriend (FUN_00554200: the owner, a player, against its alignment).
                    if (d2d::rules::friends(2, monster.align) && !(missile.info && missile.info->collide_friend)) continue;
                    if (const int size = game_data->monsters.types[std::size_t(monster.type)].size;
                        moving ? !missile_struck(*level, missile, monster.unit.x, monster.unit.y, size) : !standing_struck(missile, monster.unit.x, monster.unit.y, size)) continue;
                    if (missile.row) {                             // a shrine's potion: everyone in its burst
                        for (std::size_t j = 0; j < monsters.size(); ++j)
                            if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - missile.x, monsters[j].unit.y - missile.y) * 5 <= float(std::max(missile.burst, 1)))
                                land(j, d2d::rules::missile_blow(*missile.row, target_of(j), {}, rng), true, now_ms, -1);   // a shrine's: no unit of the foes
                        return true;
                    }
                    if (missile.skill >= 0) {
                        if (std::ranges::contains(*missile.struck, int(i))) continue;
                        if (skill_missile_hits(missile, i, now_ms)) return true;
                        // FUN_005ae1f0: one unit a frame, its hit ends the missile's frame.
                        if (missile.info && (missile.info->next_hit || missile.info->last_unit_rule()) && missile.last_hit_ms == now_ms) return false;
                        continue;
                    }
                    // A Pierce arrow keeps its struck list, as a skill's Pierce row does (missiles.md); LastCollide passes the last one by.
                    if (std::ranges::contains(*missile.struck, int(i)) || (missile.info && missile.info->last_collide && missile.last_hit == int(i))) continue;
                    return plain_missile_hits(missile, i, now_ms);
                }
                return false;
            }, [&](int owner) { const int index = monster_index(owner); return index >= 0 && monsters[std::size_t(index)].alive(); },
            [&](Missile& missile) {                       // its flight over: the hit with no unit, as where its Range runs out
                if (missile.skill < 0 || !missile.info || !std::ranges::contains(kNoUnitHit, missile.info->hit_func)) return;
                if (Hero* caster = hero_of(missile.caster)) act_as(*caster);
                merc_blow = casting_merc = missile.by_merc;
                burst(missile, now_ms);
            }, [&](int owner) -> d2d::rules::Rng* {                     // a monster shooter not among the foes: its own seed
                const int index = monster_index(owner);
                return index >= 0 ? &monsters[std::size_t(index)].seed : nullptr;
            });
            war_cry_reach(*game_data, *level, missiles, monsters, now_ms);   // a monster's nova on its allies (hit 18): the subtiles it just crossed
            merc_blow = casting_merc = false;
            for (auto& foe : foes)                               // the shooters' hit hooks: MonProp's events (knock, crush), Cursed
                for (const int owner : foe.shot_by)
                    if (const int shooter = monster_index(owner); shooter >= 0) {
                        monprop_hit(monsters[std::size_t(shooter)], foe, true);
                        cursed_hit(*game_data, monsters[std::size_t(shooter)], foes, foe);
                    }
            monster_blows();                                     // the monsters' missiles' blows on monsters
            foes.resize(allies);
            for (const auto& side : sides) {
                act_as(*side.hero);
                return_fire(foes[side.at], now_ms);              // the missiles that reached the player just now
            }
            for (const auto& flown : flying)
                if (now_ms < flown.dies && !std::ranges::contains(missiles, flown.sound, &Missile::sound))
                    cues.cue_on(unit_key(kSoundOnMissile, flown.sound), flown.hit_sound, now_ms, flown.x, flown.y);
            std::ranges::move(pending, std::back_inserter(missiles));
            pending.clear();
            // Each missile its sound id (its identity on the client: View::Shot::sound) before any view sees
            // it, the frame it's made: its TravelSound starts then too.
            for (auto& missile : missiles)
                if (missile.info && missile.sound == 0) {
                    missile.sound = ++missile_sounds;
                    cues.cue_on(unit_key(kSoundOnMissile, missile.sound), missile.info->travel_sound, now_ms, missile.x, missile.y, missile.sound);
                }
            for (auto& monster : monsters) {
                if (near_players(monster.unit.x, monster.unit.y)) monster_sounds(monster, now_ms);
                else { monster.seq_sounds = 0; monster.skill_sound = -1; }   // a far Jab's event-3 rows and skill start go unheard, not saved for later
            }
            for (const auto& side : sides) {
                act_as(*side.hero);
                hurt_side(std::span(foes).subspan(side.at, 2), now_ms);
                // A monster's missile that struck a pet (missiles_update: Foe::take, monprop_hit) after the pet loop read
                // its foe: those blows, Crushing Blows, poison and knockback now, in the pet loop's order, the pet found
                // by its id (pets_turn may have dropped or added some).
                for (std::size_t slot = side.pets; slot < allies && foes[slot].pet; ++slot) {
                    const auto& foe = foes[slot];
                    const auto pet = std::ranges::find_if(actor->pets, [&](const Pet& owned) { return owned.monster.id == foe.id; });
                    if (pet == actor->pets.end()) continue;
                    for (std::size_t index = pet_blows_taken[slot]; index < foe.blows.size(); ++index)
                        pet_hurt(*pet, foe.blows[index].damage, now_ms, foe.blows[index].hit_class, foe.blows[index].marked, foe.blows[index].frac);
                    for (std::size_t index = pet_crushes_taken[slot]; index < foe.crushed.size(); ++index)
                        pet_crushed(*pet, foe.crushed[index], foe.pick.damage_resist, now_ms);
                    pet_poisoned(*pet, foe, now_ms);
                    if (foe.knocked_from && pet->monster.alive()) knock_back(pet->monster, foe.knocked_from->first, foe.knocked_from->second, now_ms);
                }
            }
        }
        for (Hero* hero : heroes) {
            act_as(*hero);
            // In town each pet runs its own think as out of it: the AI driver
            // (FUN_005b1740) and its steps before the think (FUN_005b10e0,
            // FUN_005b1650, FUN_005b13e0) test no town; the thinks test the
            // pet's room themselves (pets.hpp). The last level's monsters stay
            // out of their sight: Fight::enter keeps them through a town.
            if (!in_moor) {
                auto elsewhere = std::exchange(monsters, {});
                try { pets_turn(now_ms, elapsed, crowd); } catch (...) { monsters = std::move(elsewhere); throw; }
                monsters = std::move(elsewhere);
            }
            if (actor->merc && actor->merc_npc) merc_turn(now_ms, elapsed, crowd, !in_moor);
        }
        act_as(acting);
    }

// What the monsters did to the acting player (`side` [0]) and their merc ([1])
// this frame: the merc's life and flinch, then the player's poison, chill,
// stun, knockback, curses, Mana Burn, block, armour wear and life.
// A monster's curse as it takes hold: its auralen, dimvision's and terror's
// over AiCurseDivisor, 25 frames at least; skill -1 for none. Then the
// target's curse_resistance (stat 0x6d) as FUN_0056e970 puts a curse state
// (FUN_0063a420): 100 or more none, else the length less that % of it
// (d2d_rules_mul_div).
auto Fight::rebind_pet_foes(std::span<Foe> pet_foes, std::vector<Pet>& pets) -> void {
        for (auto& foe : pet_foes) {
            if (!foe.pet) break;
            const auto owned = std::ranges::find(pets, foe.id, [](const Pet& pet) { return pet.monster.id; });
            foe.seed = owned != pets.end() ? &owned->monster.seed : nullptr;
            if (owned == pets.end()) foe.alive = false;
        }
    }

auto Fight::monster_curse(const Foe::Cast& cursed, std::uint32_t now_ms, int curse_resistance) -> Hero::Curse {
        const auto* skill = game_data->skills.get(cursed.skill);
        if (!skill) return {};
        int length = calc(*skill, skill->auralen, cursed.level);
        if (const int divisor = game_data->curse_divisor[std::size_t(std::clamp(game_difficulty, 0, 2))]; divisor != 0 && (skill->auratarget == "dimvision" || skill->auratarget == "terror"))
            length /= divisor;
        length = std::max(length, 25);
        if (curse_resistance > 99) return {};
        if (curse_resistance != 0) length -= int(std::int64_t(length) * curse_resistance / 100);
        return { skill->id, cursed.level, now_ms + std::uint32_t(std::max(length, 0)) * 40, cursed.pick };
    }

auto Fight::curse_stats(const Hero::Curse& curse, const d2d::rules::CalcEnv& env) const -> std::vector<std::pair<int, int>> {
        const auto* skill = curse.skill >= 0 ? game_data->skills.get(curse.skill) : nullptr;
        if (!skill) return {};
        // Picks 2 and 3 carry only aura events, and those do nothing on a player in 1.14d:
        // pick 2's Iron Maiden func (FUN_005c5f50) returns the struck's calc3 % (calc2 on a
        // player) of the picked row, Life Tap's, both empty: a 0 hit with no result flags;
        // pick 3's Life Tap func (FUN_005c61e0) heals the striker Decrepify's calc1 %,
        // empty: 0, and its prgoverlay (+0x10c) is none. bugs.md #43.
        if (curse.pick >= 0) return d2d::rules::monster_curse_stats(curse.pick, skill->par[6]);
        std::vector<std::pair<int, int>> stats;
        for (std::size_t k = 0; k < skill->aurastat.size(); ++k)
            if (skill->aurastat[k] >= 0) stats.emplace_back(skill->aurastat[k], d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], env, skill->id, curse.level));
        return stats;
    }

// FUN_005809d0 starts a get-hit only if FUN_0057edd0 (the modes) and FUN_0057eec0 (the used skill) both agree. The second refuses a unit
// whose used skill's Skills.txt `interrupt` is clear (Zeal, Smite, Charge, Jab, Strafe, Fend, Whirlwind, Frenzy, Concentrate, Kick, the
// scrolls and tomes); the used-skill pointer lives from the skill's start to the end of its swing or cast (idle, walk and run clear it),
// so only a swing or a cast is held by it. FUN_0057edd0 allows a get-hit from A1, A2, SC, TH, and from KK while its end event is queued.
auto Fight::skill_in_use() const -> bool {
        constexpr int kModeTH = 11;
        return attack_mode(actor->pmode) || actor->pmode == kModeA2 || actor->pmode == kModeSC || actor->pmode == kModeTH;
    }

auto Fight::hit_interrupts() -> bool {
        if (uninterruptable()) return false;   // state 0x36 is FUN_0057eec0's first test (0x57eec7): a Leap, Leap Attack or Whirlwind under way refuses every new mode
        if (!skill_in_use()) return true;
        const auto* used = game_data->skills.get(actor->swing_skill);
        // The barrel kick (World::operate) swings KK with no skill id; game.exe starts it with the unit's skill 1, Kick (FUN_005868a0 -> FUN_00580a70), whose `interrupt` is blank.
        if (actor->pmode == kModeKK && actor->swing_skill == 0)
            if (const auto kick = game_data->skills.by_name.find("Kick"); kick != game_data->skills.by_name.end()) used = game_data->skills.get(kick->second);
        if (!used) return true;
        if (!used->interrupt) return false;
        // Under the Concentration aura's state (0x2a, 0x57ef51) a rand(100) on the unit's seed (+0x20, 0x57ef72)
        // under that state's stat 164 (skill_concentration: its Param5) holds the swing; else Concentrate's state
        // (0xf, its swing's aurastate: self_state) does (0x57ef85). A monster is cut all the same (0x57ef94).
        if (const auto hold = actor->psum[d2d::d2s::kSkillConcentration]; hold > 0 && std::int64_t(actor->seed(100)) < hold) return false;
        return std::ranges::none_of(actor->self_states, [&](const SelfState& state) {
            const auto* skill = game_data->skills.get(state.skill);
            return skill && skill->aurastate == "concentrate";
        });
    }

// A swing or cast that a get-hit cut off: nothing of it goes on (its repeats, sequence and movement), as when it ends.
auto Fight::cancel_swing() -> void {
        actor->kicks_left = 0;
        actor->seq = {};
        actor->seq_loop = false;
        actor->smove = {};
        actor->item_charge = {};
    }

// Resolves one side's queued blows on the player and its allies, with the hits sent back (Iron Maiden's
// record, FUN_005c5f50) and their get-hit gate (FUN_0057cee0).
auto Fight::hurt_side(std::span<Foe> side, std::uint32_t now_ms) -> void {
        side[1].damage += std::exchange(actor->merc_hurt, 0);      // its swings' thorns (merc_strike), its poison (apply_regen)
        if (side[1].poison > 0 && side[1].poison_ticks > 0) {       // one at a time: a new one at least as strong replaces it (FUN_0057ac50)
            const double rate = side[1].poison / 256.0 / 40.0;      // its life is whole points, the poison 256ths a frame
            if (now_ms >= actor->merc_poison_until || rate >= actor->merc_poison_rate) {
                actor->merc_poison_rate = rate;
                actor->merc_poison_until = now_ms + std::uint32_t(side[1].poison_ticks) * 40;
            }
            // FUN_0057ac50 files a monster's event 3 (its regeneration) for the next frame, the merc's too.
            if (actor->merc && actor->merc->hit_points > 0) actor->merc->stats.regen = d2d::rules::merc_life_regen(actor->merc_st.life);
        }
        // What the merc's gear absorb took off the blows is given back as life first, a blow at a time, capped at its max life
        // (FUN_0057c6c0 -> FUN_0057a980, 0x57c844, as for the player below); the blows' totals come off after.
        if (actor->merc && actor->merc->hit_points > 0 && actor->merc->mode != "DT" && !side[1].blows.empty()) {
            const std::int64_t life = actor->merc->life_fine();
            actor->merc->set_life_fine(int(life + d2d::rules::absorb_heal(side[1].blows, life, std::int64_t(merc_max_life()) << 8)));
        }
        if (actor->merc && actor->merc->mode != "DT")           // Crushing Blows, after the hits (monprop_hit)
            for (const int divisor : side[1].crushed) {
                side[1].damage += int(crushing_blow(std::max(actor->merc->hit_points - side[1].damage, 0), divisor, side[1].pick.damage_resist));
            }
        if (actor->merc && side[1].knocked_from && actor->merc->hit_points > side[1].damage && actor->merc->mode != "DT")
            knock_back(*actor->merc, side[1].knocked_from->first, side[1].knocked_from->second, now_ms);   // as a monster's, before its get-hit
        // FUN_0057cb00's size test (rules::get_hit) once per landed blow, as game.exe tests one damage
        // record at a time (FUN_0057c6c0 then FUN_0057cee0 per hit): the divisor by that blow's hit class
        // (16 for every Act 1 melee class and any blow with an element), 37.5 % from that share of max
        // life, 75 % from twice it, sure from four times; every blow rolls; a hit inside a get-hit starts no new one (FUN_0057edd0).
        // What the unit's states absorbed (Bone Armor, Cyclone Armor, Energy Shield: absorb) is off the
        // blows already: an armor's from each blow's own part (rules::absorb_blows).
        // The merc's life carries the 256ths as a monster's (hurt).
        if (actor->merc && side[1].frac > 0 && actor->merc->mode != "DT") {
            actor->merc->life_frac += side[1].frac;
            side[1].damage += actor->merc->life_frac >> 8;
            actor->merc->life_frac &= 255;
        }
        if (actor->merc && (side[1].damage > 0 || side[1].frac > 0) && actor->merc->mode != "DT") {
            actor->merc->hit_points -= side[1].damage;
            if (actor->merc->hit_points == 1 && actor->merc->life_frac > 0) actor->merc->hit_points = 0;   // under one whole point is dead (hurt, 0057c86c)
            if (actor->merc->hit_points <= 0) actor->merc->life_frac = 0;
            // Still alive, a hurt unit with a regeneration (total stat 0x4a) files its event 3 for the
            // next frame (FUN_0057c6c0, 0x57c8fd..0x57c92f): the merc's runs from its first hurt (apply_regen).
            else if (const int regen = d2d::rules::merc_life_regen(actor->merc_st.life); regen + merc_gear().sum[d2d::d2s::kHitPointRegeneration] != 0)
                actor->merc->stats.regen = regen;
            auto& unit = actor->merc->unit;
            const auto mode = actor->merc->hit_points <= 0                                     ? std::string_view("DT")
                            : actor->merc->mode != "KB" && d2d::rules::struck_big(side[1].blows, std::int64_t(merc_max_life()) << 8, actor->merc->seed) ? std::string_view("GH")
                                                                                                   : actor->merc->mode;
            if (mode != actor->merc->mode) {
                actor->merc->mode = mode; unit.mode_ms = now_ms; unit.walking = false; unit.path.clear();
                actor->merc->mode_until = now_ms + (mode == "GH" ? d2d::game::get_hit_ms(game_data->npc_timing(actor->merc->npc, mode), merc_fighter().fhr) : game_data->npc_timing(actor->merc->npc, mode).length_ms());
            }
        }
        auto& foe = side[0];
        // The last landed blow's hit class (unit +0xb0), before a knockback's GH plays its impact.
        // The client gets it on every landed hit, a get-hit or not: FUN_0057cee0 copies it with
        // result flag 1, and a hit too small for a GH sets unit flag 0x8000, so FUN_00597cf0 sends
        // it in S->C 0x0c. A later block plays its class.
        // State 0x36 (uninterruptable) would drop result flag 4 (no 0x0c), but it's never on a
        // struck player: only the death sets it, and clears it at once (FUN_0054c0e0).
        if (foe.hits > 0 || foe.soft_hits > 0) actor->struck_class = actor->shown_class = foe.hit_class;
        // Poison works on the player over its ticks (a negative potion).
        // One poison at a time: a new one at least as strong replaces it
        // (and its length), a weaker one is ignored (FUN_0057ac50).
        if (foe.poison > 0 && foe.poison_ticks > 0) {
            const Regen poison{ -foe.poison / 40.0, 0, now_ms + std::uint32_t(foe.poison_ticks) * 40, true };
            const auto old = std::ranges::find_if(actor->regen, [](const Regen& regen_entry) { return regen_entry.poison; });
            if (old == actor->regen.end()) actor->regen.push_back(poison);
            else if (poison.life <= old->life) *old = poison;
        }
        if (foe.chill_ticks > 0) actor->chilled = std::max(actor->chilled, now_ms + std::uint32_t(foe.chill_ticks) * 40);   // only ever lengthened (FUN_0057af80)
        if (foe.stun_ticks > 0) actor->stunned = now_ms + std::uint32_t(std::min(foe.stun_ticks, 250)) * 40;          // state 21, a new one's length (FUN_0057aae0)
        // Knockback (result flag 8: FUN_0057cee0's player branch, mode 0x13,
        // FUN_0057f090): path type 8, 5 subtiles on from the attacker;
        // with no room it stays (FUN_0057f020, NU).
        // The KB mode is drawn as GH (PlrMode.txt "Knock back": GH), the
        // player sliding there a subtile a frame (knock_moved: FUN_00623f50's
        // KB velocity 0x1000). KB's frame is the walk's (0x732c10[0x13] =
        // FUN_00580c20): the path's end is the mode's (FUN_0057f020, NU), so
        // it lasts the slide, not FHR's get-hit.
        if (actor->leap) {                                          // Leap's flight, then its landing's knockback (FUN_005da120, FUN_0056bad0)
            const float done = std::clamp(float(now_ms - actor->leap->start) / float(std::max<std::uint32_t>(actor->leap->lands - actor->leap->start, 1)), 0.f, 1.f);
            actor->player.x = actor->leap->from_x + (actor->leap->to_x - actor->leap->from_x) * done; actor->player.y = actor->leap->from_y + (actor->leap->to_y - actor->leap->from_y) * done;
            actor->player.goal_x = actor->player.x; actor->player.goal_y = actor->player.y;
            if (now_ms >= actor->leap->lands) {
                if (const auto* skill = game_data->skills.get(actor->leap->skill)) {
                    const float radius = float(calc(*skill, skill->calc[0], actor->leap->level));
                    for (std::size_t j = 0; j < monsters.size(); ++j)
                        if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - actor->player.x, monsters[j].unit.y - actor->player.y) * 5 <= radius) {
                            d2d::rules::Blow blow{ .hit = true };
                            blow.knockback = true;
                            land(j, blow, true, now_ms, player_foe_id());
                        }
                }
                if (actor->character.character_class == 4) cues.cue_on(unit_key(kSoundOnPlayer, actor->id), "barbarian_leap_land", now_ms, actor->player.x, actor->player.y);
                actor->leap.reset();
            }
        }
        if (actor->knock) {
            if (actor->pmode == kModeGH) {
                const float dx = (*actor->knock)[2] - (*actor->knock)[0], dy = (*actor->knock)[3] - (*actor->knock)[1], length = std::hypot(dx, dy);
                const float done = length > 0 ? d2d::game::knock_moved(now_ms - actor->player.mode_ms, length) / length : 1.f;
                actor->player.x = (*actor->knock)[0] + dx * done; actor->player.y = (*actor->knock)[1] + dy * done;
            }
            if (actor->pmode != kModeGH || now_ms >= actor->pmode_until) actor->knock.reset();   // done, or cut short
        }
        bool knocked_now = false;   // this call started a knock: game.exe's flag chain is knockback OR get-hit per hit (LAB_0057d364)
        // FUN_00580a70 asks FUN_0057edd0 for new mode 0x13 first (0x580adc): it refuses a knockback in a get-hit (a slide, drawn as GH
        // with `knock` set, is allowed: 0x13 from 0x13) and in a block, like a big hit's mode 4 (tools/emu/mode_switch.py compare-knock).
        if (foe.knocked_from && !attack_mode(actor->pmode) && actor->pmode != kModeBL && (actor->pmode != kModeGH || actor->knock)) {
            const auto [from_x, from_y] = *foe.knocked_from;
            const float dx = actor->player.x - from_x, dy = actor->player.y - from_y, distance = std::max(std::hypot(dx, dy), 0.01f);
            if (const float next_x = actor->player.x + dx / distance, next_y = actor->player.y + dy / distance; !level->unit_blocked(next_x, next_y, actor->player.shape)) {
                actor->knock = std::array{ float(actor->player.x), float(actor->player.y), next_x, next_y };
                set_pmode(kModeGH, now_ms);
                actor->pmode_until = now_ms + d2d::game::knock_ms(std::hypot(next_x - (*actor->knock)[0], next_y - (*actor->knock)[1]));
                knocked_now = true;
            }
        }
        for (std::size_t k = 0; k < 2; ++k)
            if (const auto curse = monster_curse(side[k].cursed, now_ms, k == 0 ? int(actor->psum[d2d::d2s::kCurseResistance]) : 0); curse.skill >= 0) {
                actor->curses[k] = curse;
                d2d::log::info("{} cursed: skill {} level {}", k ? "the merc" : "the player", curse.skill, curse.level);
            }
        if (side[0].flashed >= 0) d2d::rules::add_one_shot(actor->one_shots, side[0].flashed, now_ms);
        if (side[1].flashed >= 0 && actor->merc) d2d::rules::add_one_shot(actor->merc->one_shots, side[1].flashed, now_ms);
        for (std::size_t k = 0; k < 2; ++k)
            if (const auto* skill = game_data->skills.get(side[k].marked.skill)) {
                if (now_ms >= actor->marks[k].until) d2d::log::info("{} marked: {}", k ? "the merc" : "the player", skill->auratarget);
                actor->marks[k] = { skill->id, side[k].marked.level, now_ms + std::uint32_t(std::max(calc(*skill, skill->auralen, side[k].marked.level), 5)) * 40 };
            }
        if (foe.mana_burn > 0) {                             // Mana Burn
            actor->character.stats.values[d2d::d2s::kMana] = std::max<std::int64_t>(actor->character.stats.values[d2d::d2s::kMana] - std::int64_t(foe.mana_burn), 0);
            d2d::log::info("mana burn: -{} mana", foe.mana_burn / 256);
        }
        if (foe.blocked && actor->pmode < 0) set_pmode(kModeBL, now_ms);   // a block plays out (FBR)
        for (int landed = 0; landed < foe.hits; ++landed) wear_armor(now_ms);
        foe.damage = absorb(foe, now_ms);                   // leaves foe.frac what the absorbs left of the 256ths
        frozen_armor_events(foe, now_ms);                    // after the absorbs, as FUN_0057c6c0 raises 0xb first
        // Iron Maiden's reflect on what the absorbs and the reduction left of each blow's physical (ai.cpp's
        // note: the striker's domeleedamage comes after the struck unit's events in FUN_0057c6c0).
        for (const auto& blow : foe.blows) {
            if (!blow.reflect || blow.striker < 0) continue;
            const int index = monster_index(blow.striker);
            if (index < 0) continue;
            auto& striker = monsters[std::size_t(index)];
            const auto* maiden = striker.curse.skill >= 0 ? game_data->skills.get(striker.curse.skill) : nullptr;
            if (!maiden || maiden->auratarget != "ironmaiden" || !striker.alive()) continue;
            const int physical = blow.raw >= 0 ? int(std::max<std::int64_t>(d2d::rules::reduce_fine_signed(blow.raw, blow.pct, blow.flat), 0)) : blow.physical << 8;
            if (const auto back = d2d::rules::iron_maiden_fine(true, false, false, physical, striker.reflect_pct, 0, 0)) {
                const int fine = d2d::rules::resisted_fine(*back, std::max(striker.target(*game_data).res[0], -100));
                const bool out_of_gh = striker.mode != "GH";
                if (hurt(*game_data, striker, fine >> 8, now_ms, fine & 255, maiden->hit_class, false, 0, record_gets_hit(maiden->result_flags | 0x20))) { killed(std::size_t(index), now_ms); continue; }   // FUN_005c5f50's record: ResultFlags | 0x20, a GH only with flag 4 (FUN_0057cee0)
                if (out_of_gh) striker.struck_event = true;   // 0x4021: FUN_0057cee0's 0x4000 branch raises event 4
            }
        }
        const int fine = foe.frac;
        foe.damage += int(actor->self_hurt >> 8);
        actor->self_hurt &= 255;
        using namespace d2d::d2s;
        bool died = false;
        // A blast (a trap object, an exploding barrel: take_blast) took the life itself: that death
        // first, before this frame's blows name another striker.
        if (!dead() && actor->struck_by.kind == 2 && actor->character.stats.values[kLife] <= 0) { die(now_ms); died = true; }
        if (foe.striker_id >= 0) actor->struck_by = { 1, foe.striker_id };
        // What the gear's absorb took off the blows is given back as life first, a blow at a time (FUN_0057c6c0 -> FUN_0057a980, 0x57c844),
        // capped at the max life; the blows' totals then come off below.
        if (!foe.blows.empty() && !dead()) {
            auto& life = actor->character.stats.values[kLife];
            life += d2d::rules::absorb_heal(foe.blows, life, actor->character.stats.values[kMaxLife]);
        }
        if ((foe.damage > 0 || fine > 0) && !dead()) {      // a dead player takes no more (no second death)
            auto& life = actor->character.stats.values[kLife];
            // FUN_0057c6c0 takes the records off one at a time, so the killer (FUN_0054d8a0's) is the striker of the
            // record that took the life under one point (0x57c865), not the frame's last; the blows are in landing order.
            if (life - ((std::int64_t(foe.damage) << 8) + fine) < 0x100) {
                std::int64_t left = life;
                for (const auto& blow : foe.blows) {
                    left -= (std::int64_t(blow.damage) << 8) + blow.frac;
                    if (left < 0x100) { if (blow.striker >= 0) actor->struck_by = { 1, blow.striker }; break; }
                }
            }
            life -= (std::int64_t(foe.damage) << 8) + fine;
            if (life < 0x100) life = 0;                     // under one whole point is dead (FUN_0057c6c0, 0057c865)
            if (life <= 0) { die(now_ms); died = true; }
        }
        for (const int divisor : foe.crushed) {             // Crushing Blows, after the hits (crushing_blow, FUN_005bffc0)
            if (died || dead()) break;
            auto& life = actor->character.stats.values[kLife];
            const std::int64_t crush = crushing_blow(life, divisor, foe.pick.damage_resist);
            life = std::max<std::int64_t>(life - crush, 0);
            d2d::log::info("crushing blow: -{} life", crush >> 8);
            if (life <= 0) { die(now_ms); died = true; }
        }
        // Stunned, every landed hit is a get-hit, however small, even one
        // absorbed to nothing (FUN_0057cee0: state 21 skips FUN_0057cb00's
        // too-small test; result flag 4 is the hit landing, FUN_0057ec10,
        // before any damage). That's all a stun does to a player: no input
        // is refused. A hit inside a get-hit or block starts no new one,
        // stunned or not (FUN_0057edd0 refuses mode 4 from modes 4 and 9).
        // Else the size test per landed blow (rules::struck_big).
        // The rolls are on the player's own seed (FUN_0057cb00: the struck unit's +0x20, 0x57cbb8).
        const bool big = d2d::rules::struck_big(foe.blows, actor->character.stats.get(kMaxLife), actor->seed);
        // A real get-hit refuses another (FUN_0057edd0: GH from GH); a knockback slide is drawn as GH but is game.exe's
        // mode 0x13, which allows it (0x57ee0b), and the get-hit then ends the slide. A swing or cast is cut only if its skill allows it (FUN_0057eec0).
        const bool get_hit_running = actor->pmode == kModeGH && !actor->knock;
        if (!died && (big || (foe.hits > 0 && now_ms < actor->stunned)) && actor->pmode != kModeBL && !get_hit_running && !knocked_now && hit_interrupts()) {
            if (skill_in_use()) cancel_swing();   // the swing's queued strike is gone with its mode
            actor->knock.reset();
            set_pmode(kModeGH, now_ms);
        }
        // Too small for mode 4 (FUN_0057cb00 returns 1): the server only sets
        // flag 0x8000. The client still plays the struck sounds, command 0x13
        // calling FUN_004cc5b0 with mode 0x13 (LAB_00461439). A miss or a block
        // never gets here (hits stays 0). An attack in progress is not cancelled.
        // A burner's soft hit (result flag 0x4000, no 4) sets the same 0x8000, even stunned.
        else if (!died && (foe.hits > 0 || foe.soft_hits > 0) && actor->pmode != kModeGH && actor->pmode != kModeDT && actor->pmode != kModeDD) play_player_hit(now_ms);
    }

// Bosses' auras (Aura Enchanted), each at its level within its
// aurarange: Might, Blessed Aim, Fanaticism add their damage / to-hit %
// to the monsters there; Conviction cuts the foes' resistances and
// defense; Holy Fire, Shock and Freeze strike the foes every perdelay,
// less magic damage reduction as a monster's element (monster_blow).

// Fanaticism's attackrate speeds their attacks (Monster::aura_rate);
// Holy Freeze's slow is the player's stats (update_fighters).
auto Fight::monster_auras(std::vector<Foe>& foes, std::uint32_t now_ms) -> void {
        for (auto& monster : monsters) monster.aura_dmg = monster.aura_th = monster.aura_rate = 0;
        for (auto& aura_monster : monsters) {
            if (!aura_monster.alive() || !aura_monster.aura || !near_players(aura_monster.unit.x, aura_monster.unit.y)) continue;
            const auto* skill = game_data->skills.get(aura_monster.aura);
            if (!skill) continue;
            const d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, aura_monster.stats.level, &rng };
            auto val = [&](const d2d::rules::Calc& calc_row) { return d2d::rules::eval_calc(game_data->skills, calc_row, env, skill->id, aura_monster.aura_lvl); };
            const float radius = float(val(skill->aurarange)) / 5;                     // subtiles -> cells
            auto in_range = [&](float x, float y) { return std::hypot(x - aura_monster.unit.x, y - aura_monster.unit.y) <= radius; };
            if (skill->srvdofunc == ServerDoFunction::kFriendlyAura) {
                for (auto& monster : monsters)
                    if (monster.alive() && in_range(monster.unit.x, monster.unit.y))
                        for (std::size_t i = 0; i < skill->aurastat.size(); ++i) {
                            if (skill->aurastat[i] == 25) monster.aura_dmg += val(skill->aura_calc[i]);     // damagepercent
                            if (skill->aurastat[i] == 119) monster.aura_th += val(skill->aura_calc[i]);     // item_tohit_percent
                            if (skill->aurastat[i] == 68) monster.aura_rate += val(skill->aura_calc[i]);    // attackrate (Fanaticism)
                        }
                continue;
            }
            for (auto& foe : foes) {
                if (!foe.alive || !in_range(foe.x, foe.y)) continue;
                for (std::size_t i = 0; i < skill->aurastat.size(); ++i) {
                    const int value = val(skill->aura_calc[i]);
                    switch (skill->aurastat[i]) {
                        case 39: foe.fighter.res[0] += value; break;                         // fireresist
                        case 41: foe.fighter.res[1] += value; break;                         // lightresist
                        case 43: foe.fighter.res[2] += value; break;                         // coldresist
                        case 171: foe.fighter.defense += foe.fighter.defense * value / 100; break;   // skill_armor_percent
                        default: break;
                    }
                }
            }
            if (skill->etype < 0 || skill->etype > 2 || now_ms < aura_monster.aura_next) continue;
            aura_monster.aura_next = aura_due(*skill, aura_monster.aura_lvl, now_ms);
            const int low = d2d::rules::elem_damage(game_data->skills, *skill, env, aura_monster.aura_lvl, false) >> 8;
            const int high = std::max(d2d::rules::elem_damage(game_data->skills, *skill, env, aura_monster.aura_lvl, true) >> 8, low);
            for (auto& foe : foes)
                if (foe.alive && in_range(foe.x, foe.y)) {
                    // FUN_0056e0c0: low + rand(high - low) on the aura's monster's seed.
                    const int dealt = std::max(d2d::rules::resisted(low + aura_monster.seed(high - low), foe.fighter.res[std::size_t(skill->etype)]) - foe.fighter.mdr, 0);
                    foe.damage += dealt; foe.elemental += dealt;
                    foe.blows.push_back({ dealt, 0, true, 0, dealt });
                    foe.blows.back().striker = aura_monster.id;
                    foe.striker_id = aura_monster.id;
                }
        }
    }

// A magic shrine's missiles from (x, y), at level clvl / 5 (1..8): the
// Storm Shrine's 16 fireballs (FUN_00582da0, missile 62) toward x = +-5k
// subtiles (k 1..4, + for odd k), y 5, -10, 15, -20; the Exploding and
// Poison Shrines' 6 potions (FUN_005830e0 / FUN_00583410, missiles 45 /
// 48) toward (-6, 6), (-6, -6), (0, 6), (0, -6), (6, 6), (6, -6).

// ponytail: they hit monsters only (game.exe's are the shrine's own and
// may hit players too: other players wait for networking); the poison
// potion's cloud is a burst of its row's poison.
auto Fight::shrine_missiles(int code, float x, float y, int clvl, std::uint32_t now_ms) -> void {
        const int lvl = std::clamp(clvl / 5, 1, 8);
        if (code == 19) {
            const auto found = game_data->missiles.find("fireball");
            const auto found_skill = game_data->skills.by_name.find("Fire Ball");
            if (found == game_data->missiles.end() || found_skill == game_data->skills.by_name.end()) return;
            for (int ring = 1; ring <= 4; ++ring)
                for (const int offset_y : { 5, -10, 15, -20 }) {
                    const float offset_x = float(ring % 2 ? 5 * ring : -5 * ring);
                    launch(found->second, *game_data->skills.get(found_skill->second), lvl, x, y, offset_x / 5, float(offset_y) / 5, found->second.range, now_ms);
                }
            return;
        }
        const auto found = game_data->missiles.find(code == 21 ? "explosivepotion" : "chokinggaspoition");
        if (found == game_data->missiles.end()) return;
        const auto& missile_info = found->second;
        const auto damage = d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev, missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
        static constexpr std::pair<int, int> kBurst[] = { { -6, 6 }, { -6, -6 }, { 0, 6 }, { 0, -6 }, { 6, 6 }, { 6, -6 } };
        for (const auto& [dx, dy] : kBurst) {
            const float dir_x = float(dx) / 5, dir_y = float(dy) / 5, speed = missile_speed(missile_info, lvl), distance = std::hypot(dir_x, dir_y);
            Missile missile{ &missile_info, x, y, dir_x / distance * speed, dir_y / distance * speed, missile_heading(dir_x, dir_y), now_ms, now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, {} };
            missile.friendly = true; missile.caster = actor->id; missile.row = damage; missile.burst = missile_info.hit_par1;
            seed_made(missile);
            pending.push_back(std::move(missile));
        }
    }

// The player went to `to`: an outdoor level's monsters come back, the
// last one's are kept as they were; nothing in flight follows.
// Its monsters come as its rooms come into play (rooms_up).
auto Fight::enter(const Level* destination) -> void {
        if (in_town(destination) || destination == mon_level) return;
        if (mon_level) kept[mon_level] = std::move(monsters);
        monsters.clear();
        if (const auto found = kept.find(destination); found != kept.end()) {
            monsters = std::move(found->second);
            kept.erase(found);
        }
        mon_level = destination;
        missiles.clear();
        kept_missiles.erase(destination);   // what a turn of its own left in flight (take_turn): nothing follows here either
        actor->attack_mon = actor->merc_target = -1;
        for (auto& pet : actor->pets) pet.target = -1;
    }

auto Fight::take_turn(const Level* destination) -> void {
        if (in_town(destination) || destination == mon_level) return;
        if (mon_level) { kept[mon_level] = std::move(monsters); kept_missiles[mon_level] = std::move(missiles); }
        monsters.clear(); missiles.clear();
        if (const auto found = kept.find(destination); found != kept.end()) { monsters = std::move(found->second); kept.erase(found); }
        if (const auto found = kept_missiles.find(destination); found != kept_missiles.end()) { missiles = std::move(found->second); kept_missiles.erase(found); }
        mon_level = destination;
    }

// The player at (x, y) in `here`: the rooms coming into play round
// the player populate (player_moved), their monsters joining `monsters`
// or the level's `kept`. `arrived`: just came in through a warp.
// (Named `here` because `level` would shadow the class member.)
auto Fight::rooms_up(const Level& here, float x, float y, bool arrived) -> void {
        for (const auto& [grown, from] : player_moved(*game_data, spawning, here, x, y, arrived)) {
            if (remote_monsters) continue;          // the rooms still come up (their objects); the host has the monsters
            const auto& spawns = spawning.levels[grown].spawns;
            const auto& region = std::size_t(grown->id) < spawning.regions.size() ? spawning.regions[std::size_t(grown->id)] : d2d::rules::Region{};
            auto made = spawn_monsters(*game_data, std::span(spawns).subspan(from), region, rng, game_difficulty, game_players(), grown->id);
            // A spawn's leader is a `spawns` index (its placement's first); in the level's monster list it's
            // where that spawn landed, which other units made before (a quest's, a birth) and the rows
            // spawn_monsters skips move.
            auto& slots = spawning.levels[grown].spawn_slots;
            slots.resize(spawns.size(), -1);
            std::size_t next = 0;
            for (std::size_t index = from; index < spawns.size() && next < made.size(); ++index) {
                if (spawns[index].type < 0 || std::size_t(spawns[index].type) >= game_data->mon_npc.size()) continue;   // spawn_monsters' skip
                auto& monster = made[next++];
                const auto& list = grown == mon_level ? monsters : kept[grown];       // add_monster's
                if (monster.leader == int(index)) monster.leader = int(list.size());   // its group's first: itself
                else if (monster.leader >= 0) monster.leader = std::size_t(monster.leader) < slots.size() ? slots[std::size_t(monster.leader)] : -1;
                slots[index] = add_monster(std::move(monster), grown);
            }
        }
    }

}  // namespace d2d::game
