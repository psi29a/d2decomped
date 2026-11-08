// SPDX-License-Identifier: GPL-3.0-or-later
// Fight (fight.hpp): states, buffs and charge-up skills. The rest is in this directory.
#include "fight.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"
#include "internal.hpp"
#include "log.hpp"

#include <combat.hpp>
#include <d2s_items.hpp>
#include <missiles.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

// Cloak of Shadows on a monster (FUN_005d6520): its state, with the AI
// toggle at its end whatever the monster, then special AI 10 on one
// swaps_ai lets.
void cloak(const GameData& game_data, Monster& monster, int skill, int lvl, std::uint32_t until, std::uint32_t now_ms) {
    if (put_state(game_data, monster, monster.cry, { skill, lvl, until, true }, now_ms) && swaps_ai(game_data, monster)) ai_toggle(game_data, monster, 10);
}

// A state's end on a monster (its stat list off, FUN_006269f0: the
// callback; FUN_005c3370 toggles an AI-swapping one's AI back).
void end_state(const GameData& game_data, Monster& monster, Monster::SkillState& slot) {
    if (slot.swaps) ai_toggle(game_data, monster, 0);
    if (monster.own_level > 0 && slot.skill >= 0) {             // Conversion's end (FUN_005d01a0): its level and life back, life in proportion
        if (const auto* skill = game_data.skills.get(slot.skill); skill && skill->auratarget == "conversion") {
            monster.hit_points = std::max(int(std::int64_t(monster.hit_points) * monster.own_max_life / std::max(monster.stats.hit_points, 1)), 1);
            monster.stats.level = monster.own_level; monster.stats.hit_points = monster.own_max_life;
            monster.max_life_frac = monster.own_max_life_frac;
            if (monster.hit_points == monster.stats.hit_points) monster.life_frac = std::max(monster.life_frac, monster.max_life_frac);
            monster.own_level = monster.own_max_life = monster.own_max_life_frac = 0;
        }
    }
    slot = {};
}

// A timed state put on a monster in its slot (FUN_0056e970): the same
// skill at the same level only runs longer, one at a higher level stays
// (the put fails); else the one there comes off (end_state).
// ponytail: d2d keeps one curse and one other state; game.exe's other
// states don't take each other's place.
bool put_state(const GameData& game_data, Monster& monster, Monster::SkillState& slot, Monster::SkillState next, std::uint32_t now_ms) {
    if (slot.skill >= 0 && now_ms < slot.until && slot.skill == next.skill) {
        if (slot.level == next.level) { slot.until = next.until; return true; }
        if (slot.level > next.level) return false;
    }
    end_state(game_data, monster, slot);
    slot = next;
    return true;
}

auto Fight::skill_element(d2d::rules::Fighter& fighter, const d2d::rules::Skill& skill) -> void {
        if (skill.etype < 0 || skill.etype >= 5) return;
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const auto env = calc_env();
        auto& [low, high] = fighter.elem[std::size_t(skill.etype)];
        const int len = d2d::rules::elem_length(game_data->skills, skill, env, lvl);
        const std::int64_t elo = d2d::rules::elem_damage(game_data->skills, skill, env, lvl, false, 0, true),
                           ehi = d2d::rules::elem_damage(game_data->skills, skill, env, lvl, true, 0, true);
        if (skill.etype == 3) {                                  // poison: 256ths a tick over its length
            low += int(elo * std::max(len, 1) >> 8); high += int(ehi * std::max(len, 1) >> 8);
            fighter.poison_len = std::max(fighter.poison_len, len);
            return;
        }
        low += int(elo >> 8); high += int(ehi >> 8);
        if (skill.etype == 2) fighter.cold_len = std::max(fighter.cold_len, len);
    }

// A state of the same States.txt group goes off as this one comes on (FUN_0056c740).
auto Fight::add_state(const d2d::rules::Skill& skill, int lvl, std::uint32_t until) -> void {
        const auto group_of = [&](const d2d::rules::Skill& row) { const auto info = game_data->states.find(row.aurastate); return info != game_data->states.end() ? info->second.group : 0; };
        const int group = group_of(skill);
        std::erase_if(actor->self_states, [&](const SelfState& state) {
            const auto* other = game_data->skills.get(state.skill);
            return state.skill == skill.id || (group != 0 && other && other->aurastate != skill.aurastate && group_of(*other) == group);
        });
        actor->self_states.push_back({ skill.id, lvl, until });
    }

auto Fight::self_state(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        std::uint32_t until = actor->pmode_until;
        if (skill.srvstfunc == ServerStartFunction::kBerserk) {
            const auto env = calc_env();
            const int ticks = d2d::rules::eval_calc(game_data->skills, skill.calc[1], env, skill.id, lvl);
            until = now_ms + std::uint32_t(ticks > 0 ? ticks : 10) * 40;
        }
        add_state(skill, lvl, until);
    }

auto Fight::start_state(const d2d::rules::Skill& skill, int lvl, std::uint32_t now_ms) -> void {
        const int len = calc(skill, skill.auralen, lvl);
        const std::uint32_t until = len > 0 ? now_ms + std::uint32_t(len) * 40 : ~0u;
        add_state(skill, lvl, until);
        // Armageddon / Hurricane's first strike: Param4 frames on (FUN_005c8190 -> FUN_004efc80).
        if (skill.srvdofunc == ServerDoFunction::kStormAroundCaster)
            (skill.aurastate == "armageddon" ? actor->armageddon_next : actor->hurricane_next) = now_ms + std::uint32_t(skill.par[3]) * 40;
        for (std::size_t k = 0; k < skill.aurastat.size(); ++k)
            if (skill.aurastat[k] == d2d::d2s::kBoneArmor) actor->absorb_pool = calc(skill, skill.aura_calc[k], lvl) >> 8, actor->absorb_frac = std::max(calc(skill, skill.aura_calc[k], lvl), 0) & 255, actor->absorb_skill = skill.id;
        if (skill.srvdofunc == ServerDoFunction::kCloakOfShadows)
            for (auto& monster : monsters)
                if (monster.targetable() && std::hypot(monster.unit.x - actor->player.x, monster.unit.y - actor->player.y) * 5 <= float(calc(skill, skill.aurarange, lvl)))
                    cloak(*game_data, monster, skill.id, lvl, until, now_ms);
    }

auto Fight::state_of(std::string_view name) const -> const SelfState* {
        for (const auto& state : actor->self_states)
            if (const auto* skill = game_data->skills.get(state.skill); skill && skill->aurastate == name) return &state;
        return nullptr;
    }

auto Fight::absorb(Foe& foe, std::uint32_t now_ms) -> int {
        using namespace d2d::d2s;
        // The hit in 256ths, as game.exe's damage records hold it: the absorbs below take from it and what they
        // leave keeps its fraction (foe.frac is set to it).
        std::int64_t fine = (std::int64_t(foe.damage) << 8) + foe.frac;
        // A hit the flat and percent reduction took to 0 still goes through the absorbs: game.exe takes the pool from the
        // unreduced physical (0x57c2fc) before it reduces, so only a frame with no blow at all has nothing to absorb.
        if (fine <= 0 && foe.blows.empty()) return 0;
        const auto blows_fine = [&] {
            std::int64_t sum = 0;
            for (const auto& blow : foe.blows) sum += (std::int64_t(blow.damage) << 8) + blow.frac;
            return sum;
        };
        // FUN_0057c1e0 raises the absorb event (0xb) on each record BEFORE it reduces it (the loop at 0x57c378): the
        // states take from the unreduced physical (StruckBlow::raw) and the flat and percent reduction are put back on
        // what is left (settle_blow).
        // Bone Armor (event 22, FUN_005c6370) takes from the physical part
        // (the hit's +8), Cyclone Armor (25, FUN_005c87a0) from fire,
        // lightning and cold (+0x10, +0x1c, +0x24), each blow's own, in
        // landing order (rules::absorb_blows). The pool is in 256ths.
        const auto armor = [&] {
            if ((actor->absorb_pool <= 0 && actor->absorb_frac <= 0) || !std::ranges::contains(actor->self_states, actor->absorb_skill, &SelfState::skill)) return;
            const std::int64_t before = blows_fine();
            const auto* pool_skill = game_data->skills.get(actor->absorb_skill);
            int pool = actor->absorb_pool * 256 + actor->absorb_frac;
            (void)d2d::rules::absorb_blows(foe.blows, pool, pool_skill && pool_skill->aurastate == "cyclonearmor");
            actor->absorb_pool = pool >> 8; actor->absorb_frac = pool & 255;
            if (pool <= 0) std::erase_if(actor->self_states, [&](const SelfState& state) { return state.skill == actor->absorb_skill; });
            fine += blows_fine() - before;
        };
        // Energy Shield (FUN_005ca6c0, an absorb event like the armors') takes calc1 % of each record field while the mana lasts,
        // BEFORE the resistance pass: rules::energy_shield_blows works on the unresisted fields and puts what is left back through
        // the resistances. The mana is in 256ths. Damage no blow carries (loose) is taken from as it stands.
        // calc1 goes in as is (FUN_005ca6c0 has no cap): Skills.txt's own calc1 is min(edmn,95), and one <= 0 takes nothing.
        const auto shield_pass = [&] {
            const auto* energy_shield = state_of("energyshield");   // after the armor's pass, which may drop its state
            if (!energy_shield) return;
            const std::int64_t before_shield = blows_fine();
            const auto* skill = game_data->skills.get(energy_shield->skill);
            d2d::rules::EnergyShield shield{ calc(*skill, skill->calc[0], energy_shield->level), std::max(calc(*skill, skill->calc[1], energy_shield->level), 1),
                                             actor->character.stats.values[kMana] };
            const std::int64_t loose = fine - blows_fine();
            d2d::rules::energy_shield_blows(foe.blows, shield);
            fine += blows_fine() - before_shield;
            if (loose > 0) fine -= shield.take(int(std::min<std::int64_t>(loose, 0x7fffffff)));
            actor->character.stats.values[kMana] = shield.mana;
            if (shield.absorbed > 0 && actor->character.character_class == 1) {
                static constexpr std::array<std::string_view, 5> kEnergyShieldHits = { "sorceress_energyshield_hit_1", "sorceress_energyshield_hit_2",
                    "sorceress_energyshield_hit_3", "sorceress_energyshield_hit_4", "sorceress_energyshield_hit_5" };
                cues.cue_on(unit_key(kSoundOnPlayer, actor->id), kEnergyShieldHits[std::size_t(sound_rng(int(kEnergyShieldHits.size())))], now_ms, actor->player.x, actor->player.y);
            }
        };
        // The unit's event list runs newest first (FUN_005c0ad0, 0x5c0ad0, puts a node at its head) and a re-cast moves its
        // state's nodes to the front (do 18 FUN_005c9480 / do 23 FUN_005c9c10: FUN_005c0b50 then re-register): self_states is
        // in cast order (add_state), so the later of the two takes from each record first. Each takes from its own pool, so a
        // whole pass over the blows each is the same as both on each record.
        const auto* energy_shield = state_of("energyshield");
        const auto shield_at = energy_shield ? energy_shield - actor->self_states.data() : -1;
        const auto armor_at = std::ranges::find(actor->self_states, actor->absorb_skill, &SelfState::skill) - actor->self_states.begin();
        if (shield_at > armor_at) { shield_pass(); armor(); }
        else { armor(); shield_pass(); }
        foe.frac = int(fine & 255);
        return int(fine >> 8);
    }

auto Fight::buff_events(Foe& foe, std::uint32_t now_ms) -> void {
        // Frozen Armor (event 2, FUN_005cac50, damagedinmelee): a striker whose
        // blow did damage gets a hit carrying only calc1 as its freeze length
        // (record +0x34) through FUN_0057c6c0. The length is cut by the cold
        // resist less the wearer's cold pierce (none through immunity; row 6
        // of the table at 0x732980). FUN_0057b230: a special (MonStats boss,
        // monster data +0x16 & 8: champions, uniques, superuniques) is chilled
        // for it (FUN_0057af80: not at coldeffect 0, / MonsterColdDivisor when
        // negative, at least a tick); any other is frozen only at a negative
        // coldeffect, for length / MonsterFreezeDivisor, and not chilled.
        // Shiver Armor (3, FUN_005cad40, attackedinmelee): its element
        // (FUN_0056e0c0) on any.
        // Not on state 0x36 (uninterruptable). The chill rolls shatter (state
        // 0x6b) as a blow's does; a length / divisor of 0 is state 1 for no frame.
        // The freeze's think at its end + 1 gives way to the thaw's (mon_think).
        for (const int striker : foe.melee_by) {
            const int found = this->monster_index(striker);
            if (found < 0 || !monsters[std::size_t(found)].alive()) continue;
            const auto monster_index = std::size_t(found);
            if (const auto* frozen_armor = state_of("frozenarmor"); frozen_armor && std::ranges::contains(foe.damaged_by, striker) && monsters[monster_index].type >= 0 && !monsters[monster_index].uninterruptable()) {
                const auto* skill = game_data->skills.get(frozen_armor->skill);
                auto& struck = monsters[monster_index];
                const auto& struck_type = game_data->monsters.types[std::size_t(struck.type)];
                const auto difficulty = std::size_t(struck.difficulty);
                const int cold_effect = struck_type.diff[difficulty].cold_effect;
                int res = target_of(monster_index).res[4];
                if (res < 100) res = std::max(res - pierce()[2], -100);
                const int length = d2d::rules::resisted(calc(*skill, skill->calc[0], frozen_armor->level), res);
                const bool special = struck_type.boss_column || (struck.boss != d2d::rules::Boss::none && struck.boss != d2d::rules::Boss::minion);
                if (length > 0 && special && cold_effect != 0) {
                    const int divisor = game_data->cold_divisor[difficulty];
                    const int ticks = std::max(cold_effect < 0 && divisor != 0 ? length / divisor : length, 1);
                    struck.chill_until = std::max(struck.chill_until, now_ms + std::uint32_t(ticks) * 40);
                    struck.shatter = struck.seed.next() % 100 < 20 && !struck.npc.dead_col;
                } else if (const int divisor = game_data->freeze_divisor[difficulty]; length > 0 && !special && cold_effect < 0 && divisor != 0 && length / divisor > 0) {
                    freeze(struck, now_ms + std::uint32_t(length / divisor) * 40);
                }
            }
            if (const auto* shiver_armor = state_of("shiverarmor")) {
                const auto* skill = game_data->skills.get(shiver_armor->skill);
                land(monster_index, d2d::rules::missile_blow(d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), shiver_armor->level), target_of(monster_index), pierce(), rng),
                     true, now_ms, player_foe_id());
            }
        }
    }

auto Fight::return_fire(const Foe& foe, std::uint32_t now_ms) -> void {
        // Chilling Armor (event 1, FUN_005cab40): a ReturnFire missile's
        // owner, alive and hostile, gets the skill's srvmissilea (+0x48)
        // back from the wearer at where it stands.
        if (const auto* chilling_armor = state_of("chillingarmor"))
            for (const int owner : foe.fired_by) {
                const auto* skill = game_data->skills.get(chilling_armor->skill);
                const auto found = game_data->missiles.find(skill->srvmissilea);
                const int shooter = monster_index(owner);
                if (shooter < 0 || !monsters[std::size_t(shooter)].alive() || found == game_data->missiles.end()) continue;
                const auto& shooter_unit = monsters[std::size_t(shooter)].unit;
                launch(found->second, *skill, chilling_armor->level, actor->player.x, actor->player.y, shooter_unit.x - actor->player.x, shooter_unit.y - actor->player.y, found->second.range, now_ms);
            }
    }

auto Fight::monster_return_fire(std::size_t monster_index, const Missile& missile, bool by_merc, std::uint32_t now_ms) -> void {
        // The same event 1 (FUN_005cab40) on a monster in Chilling Armor
        // (UberIzual's): the srvmissilea from it at the shooter, carrying
        // the skill's element at its level.
        const auto& monster = monsters[monster_index];
        const auto* skill = monster.self_state.skill >= 0 ? game_data->skills.get(monster.self_state.skill) : nullptr;
        if (!missile.info || !missile.info->return_fire || !monster.alive() || !skill || skill->aurastate != "chillingarmor") return;
        const auto& shooter = by_merc && actor->merc ? actor->merc->unit : actor->player;
        andariel_missile(*game_data, monster, skill->srvmissilea, shooter.x - monster.unit.x, shooter.y - monster.unit.y, now_ms, pending, monster.self_state.level, skill);
    }

auto Fight::merc_buff_events(Foe& foe, std::uint32_t now_ms) -> void {
        const auto* skill = actor->merc_buff.skill >= 0 && now_ms < actor->merc_buff.until ? game_data->skills.get(actor->merc_buff.skill) : nullptr;
        if (!skill || skill->aurastate != "frozenarmor") return;
        const int frames = std::max(d2d::rules::eval_calc(game_data->skills, skill->calc[0], merc_calc_env(), skill->id, actor->merc_buff.level), 1);
        for (const int striker : foe.damaged_by) {                    // as the player's (buff_events): its strikers freeze
            const int found = this->monster_index(striker);
            if (found < 0 || !monsters[std::size_t(found)].alive() || monsters[std::size_t(found)].uninterruptable()) continue;   // state 0x36: FUN_0057b230's first test
            const auto monster_index = std::size_t(found);
            freeze(monsters[monster_index], now_ms + std::uint32_t(frames) * 40);
            monsters[monster_index].chill_until = std::max(monsters[monster_index].chill_until, now_ms + std::uint32_t(frames) * 40);
        }
    }

auto Fight::buff_tick(std::uint32_t now_ms) -> void {
        if (const auto* blaze = state_of("blaze"); blaze && actor->player.walking && now_ms / 40 != actor->blaze_frame) {
            actor->blaze_frame = now_ms / 40;
            const auto* skill = game_data->skills.get(blaze->skill);
            if (const auto found = game_data->missiles.find(skill->srvmissilea); found != game_data->missiles.end())
                launch(found->second, *skill, blaze->level, actor->player.x, actor->player.y, 0, 0, found->second.range + found->second.lev_range * blaze->level, now_ms);
        }
        // Armageddon / Hurricane (do 124, FUN_005c8190: the state and its
        // timer event, FUN_005417d0(5, ...) at Param4 frames, FUN_004efc80
        // reading Skills +0x154). The event runs the state's srvactivefunc
        // (FUN_0056d790): each run files the next Param4 frames on, so
        // Armageddon (146, FUN_005c8520) drops its control row (hit 56) on a
        // spot within aurarange every 8 frames, Hurricane (145, FUN_005c8380)
        // strikes what's within aurarange with its cold every 20. A run in
        // town takes the state off instead (FUN_0061ab00): Fight::world.
        // Blade Shield: blade_shield, every perdelay frames.
        if (const auto* armageddon = state_of("armageddon"); armageddon && now_ms >= actor->armageddon_next) {
            const auto* skill = game_data->skills.get(armageddon->skill);
            actor->armageddon_next = now_ms + std::uint32_t(std::max(skill->par[3], 1)) * 40;
            if (const auto found = game_data->missiles.find(skill->srvmissilea); found != game_data->missiles.end()) {
                const int radius = std::max(calc(*skill, skill->aurarange, armageddon->level), 1);
                launch(found->second, *skill, armageddon->level, actor->player.x + float(int(rng(2 * radius + 1)) - radius) / 5, actor->player.y + float(int(rng(2 * radius + 1)) - radius) / 5,
                       0, 0, found->second.range, now_ms);
            }
        }
        if (const auto* state = state_of("hurricane"); state && now_ms >= actor->hurricane_next) {
            const auto* skill = game_data->skills.get(state->skill);
            actor->hurricane_next = now_ms + std::uint32_t(std::max(skill->par[3], 1)) * 40;
            const auto damage = d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), state->level);
            const int radius = std::max(calc(*skill, skill->aurarange, state->level), 1);
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - actor->player.x, monsters[j].unit.y - actor->player.y) * 5 <= float(radius))
                    land(j, d2d::rules::missile_blow(damage, target_of(j), pierce(), rng), true, now_ms, player_foe_id());
        }
        if (const auto* state = state_of("bladeshield"); state && now_ms >= actor->blade_next) {
            const auto* skill = game_data->skills.get(state->skill);
            actor->blade_next = now_ms + std::uint32_t(std::max(calc(*skill, skill->perdelay_calc, state->level), 1)) * 40;
            blade_shield(*skill, state->level, actor->player.x, actor->player.y, actor->player_combat, true, player_foe_id(), now_ms);
        }
        // Thunder Storm (do 29, FUN_005ca4d0): a periodic skill (stats 0x15e /
        // 0x15f), every perdelay frames while its state lasts. The bolt
        // (srvmissilea, hit at once: FUN_005adf10) goes at the monster within
        // Param7 subtiles (FUN_005c8bc0, record +0x160) whose unit id comes
        // next after the last one struck, round to the lowest (FUN_0056bd10's
        // callback 0x56bcd0): it takes them in turn.
        if (const auto* thunderstorm = state_of("thunderstorm"); thunderstorm && now_ms >= actor->storm_next) {
            const auto* skill = game_data->skills.get(thunderstorm->skill);
            actor->storm_next = now_ms + std::uint32_t(std::max(calc(*skill, skill->perdelay_calc, thunderstorm->level), 1)) * 40;
            int best = -1, lowest = -1;
            for (std::size_t j = 0; j < monsters.size(); ++j) {
                const auto& monster = monsters[j];
                if (!monster.targetable() || monster.align == 2 || std::hypot(monster.unit.x - actor->player.x, monster.unit.y - actor->player.y) * 5 > float(skill->par[6])) continue;
                if (lowest < 0 || monster.id < monsters[std::size_t(lowest)].id) lowest = int(j);
                if (monster.id > actor->storm_last && (best < 0 || monster.id < monsters[std::size_t(best)].id)) best = int(j);
            }
            if (best < 0) best = lowest;
            if (best >= 0) { actor->storm_last = monsters[std::size_t(best)].id; actor->storm_struck = true; }
            if (best >= 0)
                land(std::size_t(best), d2d::rules::missile_blow(d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), thunderstorm->level),
                                                                 target_of(std::size_t(best)), pierce(), rng), true, now_ms, player_foe_id());
        }
    }

auto Fight::monster_states(std::uint32_t now_ms) -> void {
        for (auto& monster : monsters) {
            monster.dmg_pct = monster.speed_pct = monster.reflect_pct = monster.anim_pct = 0;
            monster.align = 0;                                // Confuse's / Attract's end (FUN_005c3db0 / 0x5c3b00), or another curse in its place
            monster.afraid = false;
            for (auto* effect : { &monster.curse, &monster.cry, &monster.frenzy, &monster.self_state }) {
                if (effect->skill < 0) continue;
                if (now_ms >= effect->until) { end_state(*game_data, monster, *effect); continue; }
                const auto* skill = game_data->skills.get(effect->skill);
                if (!skill) continue;
                for (std::size_t aura_stat = 0; aura_stat < skill->aurastat.size(); ++aura_stat) {
                    if (skill->aurastat[aura_stat] == 25) monster.dmg_pct += calc(*skill, skill->aura_calc[aura_stat], effect->level);
                    if (skill->aurastat[aura_stat] == 67) monster.speed_pct += calc(*skill, skill->aura_calc[aura_stat], effect->level);
                    if (skill->aurastat[aura_stat] == 68 || skill->aurastat[aura_stat] == 69)   // attackrate, other_animrate: the same rate (MonFrenzy)
                        monster.anim_pct = std::max(monster.anim_pct, calc(*skill, skill->aura_calc[aura_stat], effect->level));
                }
                const auto& targets = skill->auratarget;
                if (targets == "ironmaiden") monster.reflect_pct += calc(*skill, skill->calc[0], effect->level);
                if (targets == "terror") monster.afraid = true;
                if (targets == "conversion") monster.align = 2;
                if (targets == "confuse" || targets == "attract") monster.align = 1;
            }
            // The enemy auras over it (the player's, the merc's): Holy Freeze's
            // velocitypercent and attackrate / other_animrate (one rate), -dm34.
            if (monster.alive())
                for (const auto& aura : enemy_auras(monster.unit.x, monster.unit.y)) {
                    int rate = 0;
                    for (std::size_t aura_stat = 0; aura_stat < aura.skill->aurastat.size(); ++aura_stat) {
                        const int value = d2d::rules::eval_calc(game_data->skills, aura.skill->aura_calc[aura_stat], aura.env, aura.skill->id, aura.level);
                        if (aura.skill->aurastat[aura_stat] == 67) monster.speed_pct += value;
                        if (aura.skill->aurastat[aura_stat] == 68 || aura.skill->aurastat[aura_stat] == 69) rate = value;
                    }
                    monster.anim_pct += rate;
                }
            // The chill's end (LAB_0057ad80) takes shatter (107) off only while
            // it lives (FUN_005541b0, 0x57ada5): one that died chilled keeps it.
            if (monster.alive() && now_ms >= monster.chill_until) monster.shatter = false;
            if (monster.set_kind && now_ms >= monster.set_until) monster.set_kind = 0;   // event 10 (0x5a7f70 -> FUN_00573120)
        }
    }

auto Fight::charge(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void { charge(actor->charges, skill, skill_level ? skill_level(skill.id) : 1, now_ms); }

// A charge on `held` (the player's, or a Shadow's), at most 3, auralen on.
auto Fight::charge(std::vector<Charge>& held, const d2d::rules::Skill& skill, int lvl, std::uint32_t now_ms) -> void {
        auto found = std::ranges::find(held, skill.id, &Charge::skill);
        if (found == held.end()) found = held.insert(held.end(), Charge{ skill.id });
        found->level = std::max(found->level, lvl);
        found->count = std::min(found->count + 1, 3);
        const auto env = calc_env();
        found->until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data->skills, skill.auralen, env, skill.id, lvl), 1)) * 40;
    }

auto Fight::add_charges(const std::vector<Charge>& held, d2d::rules::Fighter& fighter, d2d::rules::Swing& swing, bool player_levels) -> void {
        const auto env = calc_env();
        for (const auto& charge : held) {
            const auto* skill = game_data->skills.get(charge.skill);
            if (!skill) continue;
            const int lvl = std::max(charge.level, player_levels && skill_level ? skill_level(charge.skill) : 0);
            const auto bonus = d2d::rules::charge_bonus(game_data->skills, *skill, env, lvl, charge.count);
            swing.ed_pct += bonus.ed_pct;
            swing.ar_pct += bonus.ar_pct;
            fighter.life_steal += bonus.life_steal;
            fighter.mana_steal += bonus.mana_steal;
            if (bonus.etype >= 0 && bonus.etype < 5) {
                auto& [low, high] = fighter.elem[std::size_t(bonus.etype)];
                low += bonus.elem_lo; high += bonus.elem_hi;
                if (bonus.etype == 2) fighter.cold_len = bonus.elem_len;   // FUN_0056c8e0 case 4 sets +0x30
                swing.freeze_len += bonus.freeze_len;
                // A finisher has no calc4 conversion of its own: the charge's is the record's.
                if (bonus.conv_pct > 0 && swing.conv_type < 0) { swing.conv_type = bonus.etype; swing.conv_pct = bonus.conv_pct; }
            }
        }
    }

auto Fight::progressive_tohit() -> int {
        const auto env = calc_env();
        int total = 0;
        for (const auto& charge : actor->charges)
            if (const auto* skill = game_data->skills.get(charge.skill))
                total += d2d::rules::charge_bonus(game_data->skills, *skill, env, std::max(charge.level, skill_level ? skill_level(charge.skill) : 0), charge.count).ar_pct;
        return total;
    }

auto Fight::row_damage(const GameData::MissileInfo& missile_info, int lvl, const d2d::rules::CalcEnv& env, int skill_id) const -> d2d::rules::MissileDamage {
        auto damage = d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev, missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
        if (damage.etype >= 0 && lvl >= 1 && !missile_info.edmg_sym.empty()) {   // FUN_0064b100 / FUN_0064b1d0
            damage.elo = missile_elem(*game_data, missile_info, env, skill_id, lvl, false);
            damage.ehi = std::max(missile_elem(*game_data, missile_info, env, skill_id, lvl, true), damage.elo);
        }
        damage.srcdam = missile_info.src_damage;
        return damage;
    }

auto Fight::release(std::vector<Charge>& held, std::size_t monster_index, std::uint32_t now_ms, bool player_levels) -> void {
        const float target_x = monsters[monster_index].unit.x, target_y = monsters[monster_index].unit.y;
        const auto released = held;
        held.clear();
        for (const auto& charge : released) {
            const auto* skill = game_data->skills.get(charge.skill);
            if (!skill) continue;
            const int lvl = std::max(charge.level, player_levels && skill_level ? skill_level(skill->id) : 0);
            const int count = std::clamp(charge.count, 1, 3);
            for (int k = skill->prgstack ? 1 : count; k <= count; ++k) prg(*skill, skill->prgfunc[std::size_t(k - 1)], k, lvl, target_x, target_y, now_ms);
        }
    }

auto Fight::prg(const d2d::rules::Skill& skill, ServerDoFunction func, int count, int lvl, float target_x, float target_y, std::uint32_t now_ms) -> void {
        if (int(func) <= 0) return;
        const auto env = calc_env();
        const auto& mname = count <= 1 ? skill.srvmissilea : count == 2 ? skill.srvmissileb : skill.srvmissilec;
        const auto mit = game_data->missiles.find(mname);
        const GameData::MissileInfo* missile_info = mit == game_data->missiles.end() ? nullptr : &mit->second;
        int radius = d2d::rules::eval_calc(game_data->skills, skill.prgcalc[std::size_t(count - 1)], env, skill.id, lvl);
        if (radius == 0) radius = d2d::rules::eval_calc(game_data->skills, skill.prgcalc[0], env, skill.id, lvl);
        if (func == ServerDoFunction::kReleaseAreaHit) {
            d2d::rules::MissileDamage damage = d2d::rules::missile_damage(game_data->skills, skill, env, lvl);
            damage.srcdam = 0;
            const std::array<int, 4> pierce{ int(actor->psum[d2d::d2s::kPassiveFirePierce]), int(actor->psum[d2d::d2s::kPassiveLightningPierce]), int(actor->psum[d2d::d2s::kPassiveColdPierce]), int(actor->psum[d2d::d2s::kPassivePoisonPierce]) };
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (monsters[i].targetable() && std::hypot(monsters[i].unit.x - target_x, monsters[i].unit.y - target_y) * 5 <= float(std::max(radius, 1)))
                    land(i, d2d::rules::missile_blow(damage, target_of(i), pierce, rng), true, now_ms, player_foe_id());
            return;
        }
        if ((func == ServerDoFunction::kReleaseNova || func == ServerDoFunction::kReleaseScatter) && missile_info) {
            auto put = [&](float x, float y, float velocity_x, float velocity_y, int range) {
                Missile missile{ missile_info, x, y, velocity_x, velocity_y, missile_heading(velocity_x, velocity_y), now_ms, now_ms + std::uint32_t(std::max(range, 1)) * 40, {} };
                missile.friendly = true; missile.caster = actor->id; missile.skill = skill.id; missile.level = lvl;
                missiles.push_back(std::move(missile));
                return missiles.size() - 1;
            };
            if (func == ServerDoFunction::kReleaseNova) {
                const auto shared = std::make_shared<std::vector<int>>();
                const float speed = missile_speed(*missile_info, lvl);
                const int range = missile_info->range + missile_info->lev_range * lvl + d2d::rules::eval_calc(game_data->skills, skill.calc[0], env, skill.id, lvl);
                for (int k = 0; k < 64; ++k) {
                    const auto [aim_x, aim_y] = d2d::rules::kNovaRing[std::size_t(k)];
                    const float length = std::hypot(float(aim_x), float(aim_y));
                    missiles[put(target_x, target_y, float(aim_x) / length * speed, float(aim_y) / length * speed, range)].struck = shared;
                }
            } else {
                for (int k = 0; k < radius * radius; ++k) {
                    const int offset_x = radius - int(rng(2 * radius + 1)), offset_y = radius - int(rng(2 * radius + 1));
                    if (offset_x * offset_x + offset_y * offset_y > radius * radius) continue;
                    const float x = target_x + float(offset_x) / 5, y = target_y + float(offset_y) / 5;
                    if (!level->blocked(x, y, 0x04)) put(x, y, 0, 0, missile_info->range + missile_info->lev_range * lvl);
                }
            }
            return;
        }
        // 40 (FUN_005d5010, Royal Strike's first): the count's missile standing
        // at the target (FUN_0056ede0: its meteor). 41 (FUN_005d5080, its
        // third): prgcalc of them from the target toward random points
        // round it. 37 / 143 (FUN_005d4e70 -> FUN_005d4150, FUN_005d4f40 ->
        // FUN_005d4870: Claws of Thunder's third, Royal Strike's second):
        // a scan of 64 directions (0x100 / 4) for the units within prgcalc
        // (else aurarange) subtiles, a missile at each carrying
        // FUN_004efc20 + 1 hops.
        // ponytail: the scan's pick (FUN_005d40f0 / FUN_005d4680) and the
        // hop count read as: every monster in reach, the level's hops; 41's
        // spread as ±20 subtiles.
        if (!missile_info) return;
        if (func == ServerDoFunction::kReleaseAtTarget) { launch(*missile_info, skill, lvl, target_x, target_y, 0, 0, missile_info->range, now_ms); return; }
        if (func == ServerDoFunction::kReleaseSpread) {
            for (int k = 0; k < std::max(radius, 1); ++k) {
                const float dx = float(int(rng(41)) - 20) / 5, dy = float(int(rng(41)) - 20) / 5;
                launch(*missile_info, skill, lvl, target_x, target_y, dx, dy, missile_info->range + missile_info->lev_range * lvl, now_ms);
            }
            return;
        }
        if (func == ServerDoFunction::kReleaseMissileAtUnits || func == ServerDoFunction::kReleaseMissileAtUnitsSecondScan) {
            const int reach = radius > 0 ? radius : d2d::rules::eval_calc(game_data->skills, skill.aurarange, env, skill.id, lvl);
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (monsters[i].targetable() && std::hypot(monsters[i].unit.x - target_x, monsters[i].unit.y - target_y) * 5 <= float(std::max(reach, 1)))
                    launch(*missile_info, skill, lvl, target_x, target_y, monsters[i].unit.x - target_x + 0.01f, monsters[i].unit.y - target_y, missile_info->range + missile_info->lev_range * lvl, now_ms).hops = lvl + 1;
            return;
        }
        if (std::ranges::find(told, -1000 - int(func)) != told.end()) return;
        told.push_back(-1000 - int(func));
        d2d::log::info("not implemented: {}'s release srvprgfunc {} ({} charges)", skill.name, int(func), count);
    }

}  // namespace d2d::game
