// SPDX-License-Identifier: GPL-3.0-or-later
// Act 5's MonAI thinks (monster-ai.md "Act 5's MonAIs"; tools/emu/mon_ais.py),
// the traps' and the rest the levels place.
#pragma once

#include "monsters.hpp"
#include "thinker.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace d2d::rules {

struct Act5Thinks : Thinker {
    // ReanimatedHorde (114, FUN_005e1540): in melee aip1 % A1, else stands
    // aip2. Skill2 (its revive, Sk2mode) with three clear lines to a target
    // between 5 and aip3 off, aip4 %; aip5 % walks in; aip6 % keeps 4 off;
    // else stands aip7.
    Think reanimated_horde() {
        if (input.in_melee) return roll() < aip[0] ? Think{ MonAct::a1 } : idle2;
        if (input.skill[1] && input.lines_clear && input.dist < aip[2] && input.dist > 5 && roll() < aip[3]) return cast(1);
        if (roll() < aip[4]) return walk_in;
        if (roll() < aip[5]) return keep_off(4, 0);
        return { MonAct::idle, aip[6] };
    }
    // SiegeBeast (115, FUN_005e1900): nothing once dead (mode 0 / 12).
    // Riderless and not under its alignment test, an imp within aip1 takes it
    // (FUN_005e17e0). In melee aip3 % Skill1 (at no unit), aip2 % A1, else
    // stands aip4; out of it Skill1 within its reach aip5 %; with clear
    // lines aip6 % keeps 12 off at aip7 % pace; else keeps 12 off.
    Think siege_beast() {
        if (input.self_dead) return { MonAct::none };
        if (!input.rider && !input.state69 && input.imp_near) effect(ThinkEffect::mount);
        if (input.in_melee) {
            if (input.skill[0] && roll() < aip[2]) return cast(0, ThinkUnit::none);
            return roll() < aip[1] ? Think{ MonAct::a1 } : Think{ MonAct::idle, aip[3] };
        }
        if (input.skill[0] && input.dist < input.skill1_range && roll() < aip[4]) return cast(0, ThinkUnit::none);
        if (input.lines_clear && roll() < aip[5]) set_pace(std::clamp(aip[6], 0, 0x7f), 0);
        return keep_off(12, 0);
    }
    // Minion (116, FUN_005e1b60): a live command of kind 1 (its frame not
    // past) whose unit's found and free makes that its target (and in melee
    // by reach); else the command's done. Out of melee, uncommanded, aip3 %
    // walks in, else stands aip4; commanded it walks in. In melee,
    // uncommanded, stands aip2 unless aip1 %; then aip2 % A2, else A1.
    Think minion() {
        bool commanded = false, melee = input.in_melee;
        ThinkUnit aim = ThinkUnit::target;
        if (*input.command != 0) {
            if (*input.command == 1 && input.frame < input.command_until && input.command_unit && !input.command_busy) {
                commanded = true;
                aim = ThinkUnit::kin;
                melee = input.reach;
            } else effect(ThinkEffect::command_done);
        }
        if (!melee) {
            if (!commanded && roll() >= aip[2]) return { MonAct::idle, aip[3] };
            return aimed(walk_in, aim);
        }
        if (!commanded && roll() >= aip[0]) return { MonAct::idle, aip[1] };
        return aimed(roll() < aip[1] ? Think{ MonAct::a2 } : Think{ MonAct::a1 }, aim);
    }
    // SuicideMinion (117, FUN_005e1d30): its fuse (state) lit in melee at
    // aip5 frames on; past it, it blows (mode 0 at the target). Out of melee
    // unlit aip3 % walks in; else stands aip2.
    Think suicide_minion() {
        if (*input.state != 0) { if (*input.state < input.frame) return { MonAct::die }; }
        else if (!input.in_melee) { if (roll() < aip[2]) return walk_in; }
        else *input.state = aip[4] + input.frame;
        return idle2;
    }
    // Succubus (118, FUN_005e1e00): the target not cursed, within
    // aip4, aip3 %: Skill1 at a target with at least aip7 % life; Skill2 with
    // its own at most aip8 %; Skill3 unless the target is a player with
    // FUN_00625d60 at least FUN_00625d10; Skill4 at a player. In melee aip1 %
    // A1, else stands aip5. Skill5 aip8 % (on the unit seed) at the second
    // target; aip2 % walks in; else stands aip6.
    Think succubus() {
        if (!input.target_cursed && input.dist < aip[3] && roll() < aip[2]) {
            if (input.skill[0] && input.target_life_pct >= aip[6]) return cast(0);
            if (input.skill[1] && input.self_life_pct <= aip[7]) return cast(1);
            if (input.skill[2] && (input.target_max_mana < input.target_max_life || !input.target || !input.target_player)) return cast(2);
            if (input.skill45[0] && input.target && input.target_player) return cast(3);
        }
        if (input.in_melee) return roll() >= aip[0] ? Think{ MonAct::idle, aip[4] } : Think{ MonAct::a1 };
        if (input.skill45[1] && aip[7] > 0 && input.second && rng(100) < aip[7]) return cast(4, ThinkUnit::second);
        return roll() >= aip[1] ? Think{ MonAct::idle, aip[5] } : walk_in;
    }
    // SuccubusWitch (119, FUN_005e2120): the Succubus's casts (a target
    // needed). In melee aip3 % backs off aip4 (running), else aip1 % A1, else
    // stands aip6. Out of it: Skill5 (aip8 %, aip5 % first) at the second
    // target; within aip4 aip3 % backs off aip4; with no Skill5 the second
    // target aip5 % gets S2; aip2 % walks in; half the time circles 6, else
    // (or when it can't) stands aip6.
    Think succubus_witch() {
        if (input.target && !input.target_cursed && input.dist < aip[3] && roll() < aip[2]) {
            if (input.skill[0] && input.target_life_pct >= aip[6]) return cast(0);
            if (input.skill[1] && input.self_life_pct <= aip[7]) return cast(1);
            if (input.skill[2] && (input.target_max_mana < input.target_max_life || !input.target_player)) return cast(2);
            if (input.skill45[0] && input.target_player) return cast(3);
        }
        if (input.in_melee) {
            if (roll() < aip[2] && away(aip[3] & 0xff, true)) return { MonAct::none };
            return roll() < aip[0] ? Think{ MonAct::a1 } : Think{ MonAct::idle, aip[5] };
        }
        int dist = input.dist;                                     // FUN_005ddc30 writes the second target's distance over it
        if (input.skill45[1] && aip[7] > 0 && roll() < aip[4]) {
            dist = input.second ? input.second_dist : 0x7fffffff;
            if (input.second && rng(100) < aip[7]) return cast(4, ThinkUnit::second);
        }
        if (dist < aip[3] && roll() < aip[2] && away(aip[3] & 0xff, true)) return { MonAct::none };
        if (!input.skill45[1] && input.second && roll() < aip[4]) { Think out{ MonAct::s2 }; out.unit = ThinkUnit::second; return out; }
        if (roll() < aip[1]) return walk_in;
        if (roll() > 49) return { MonAct::idle, aip[5] };
        bool set_off = false;
        auto out = circle(6, 1, set_off);
        return set_off ? out : Think{ MonAct::idle, aip[5] };
    }
    // Overseer (120, FUN_005e27a0; Shenk's quest step, FUN_00587900, isn't
    // here): hit, past its timer (state), Skill1 (the warcry) at the one that
    // hit it, the timer aip1 frames on. In melee aip6 %: aip7 % A2, else A1.
    // Skill2 aip2 % at a minion under half life within 24, Skill3 aip3 % at
    // one within 20 (FUN_005e26d0). No minions: 40 % walks in, else stands
    // 10. Closer than aip4 - aip5 it backs off to aip4; within aip4 + aip5
    // half the time circles 5, else stands 10; past it walks in.
    Think overseer() {
        if (input.skill[0] && input.got_hit && *input.state < input.frame && input.attacker) {
            *input.state = aip[0] + input.frame;
            return cast(0, ThinkUnit::second);
        }
        if (input.in_melee && roll() < aip[5]) return roll() >= aip[6] ? Think{ MonAct::a1 } : Think{ MonAct::a2 };
        if (input.skill[1] && input.hurt_minion && roll() < aip[1]) return cast(1, ThinkUnit::kin);
        if (input.free_minion && roll() < aip[2]) return cast(2, ThinkUnit::aid);   // Skill3 "!= 0": an unset one (-1) passes too
        if (input.young == 0) return roll() > 59 ? Think{ MonAct::idle, 10 } : walk_in;
        if (input.dist < aip[3] - aip[4]) { (void)away((aip[3] - input.dist) & 0xff, false); return { MonAct::none }; }
        if (input.dist <= aip[4] + aip[3]) {
            if (rng(100) >= 50) return { MonAct::idle, 10 };
            bool set_off = false;
            return circle(5, 0, set_off);
        }
        return walk_in;
    }
    // MinionSpawner (121, FUN_005e2bd0; the evil huts): under aip1 made
    // (state2) and the target within aip4, past its timer (state): Skill1 (a
    // minion) at the target while fewer than aip2 minions live about
    // (FUN_005e2b70), the timer aip3 frames on; else stands aip2.
    Think minion_spawner() {
        if (*input.state2 >= aip[0]) return { MonAct::none };
        if (aip[3] < input.dist || input.frame < *input.state) return idle2;
        if (input.skill[0] && input.young < aip[1]) { ++*input.state2; *input.state = aip[2] + input.frame; return cast(0); }
        return idle2;
    }
    // A riding imp's think (special 16, FUN_005e2d80; its init imp_ride_pick):
    // off its mount or out of state 0x8f, back to its own think, stands 1.
    // Past 24, its mount dead or under its alignment test, or no pick (+0x18 0): it
    // teleports off (as below), its ride word -1. Within b2, b3 %: its pick
    // (Skill3 Fireball / Skill5 Inferno) at the target, a stand 20 when unset;
    // else 50 % S1 at it, else stands 20.
    // Its +0x1c (two more draws when set) is the pick table's second word,
    // 0 in both rows (0x6e34e0: {2, 0}, {4, 0}): never drawn.
    Think imp_ride() {
        if (!input.riding || !input.rider) { effect(ThinkEffect::toggle); return { MonAct::idle, 1 }; }
        if (input.dist > 24 || input.command_busy || input.state69 || *input.state2 == 0) { *input.state = -1; return imp_teleport(); }
        if (input.dist < input.imp_aip[1][1] && roll() < input.imp_aip[1][2]) {
            const int slot = *input.state2;
            return slot >= 0 && slot < 5 && (slot < 3 ? input.skill[std::size_t(slot)] : input.skill45[std::size_t(slot - 3)]) ? cast(slot) : Think{ MonAct::idle, 20 };
        }
        if (roll() < 50) return { MonAct::s1 };
        return { MonAct::idle, 20 };
    }
    // SiegeTower (113, FUN_005e1860): unridden (no mount, or it's busy) with
    // a target, an imp within 20 takes it (FUN_005e17e0); stands aip1.
    Think siege_tower() {
        if ((!input.rider || input.command_busy) && input.target && input.imp_near) effect(ThinkEffect::mount);
        return { MonAct::idle, aip[0] };
    }
    // Imp (122, FUN_005e2ff0; imp1..imp4's aips as a, b, c, d): riding (state:
    // its beast's id, -1 none) and not under its alignment test: a beast gone,
    // dead, ridden or under it drops the ride; past b1 it walks to it, else Skill1
    // (its teleport) onto it. In melee under a1 % life it teleports (within
    // a2 of itself), else c2 % backs off 5 (running). Then a3 % teleports;
    // within c1 c2 % backs off 5; Skill4 at the second target within c3 (c4
    // %) or d3 (d4 %); 33 % approaches 4; else 80 % stands 10, 20 % wanders 8.
    Think imp() {
        const auto& first = input.imp_aip[0];
        const auto& third = input.imp_aip[2];
        const auto& fourth = input.imp_aip[3];
        auto teleport = [this] { return imp_teleport(); };
        if (*input.state != -1 && !input.state69) {
            if (!input.command_unit || input.command_busy || input.rider) *input.state = -1;
            else {
                if (input.imp_aip[1][0] * input.imp_aip[1][0] < squared(input.self_x, input.self_y, input.kin_x, input.kin_y)) { Think out = walk_in; out.unit = ThinkUnit::kin; return out; }
                if (input.skill[0]) return cast(0, ThinkUnit::kin);
            }
        }
        if (input.in_melee) {
            if (input.skill[0] && input.life_pct < first[0]) return teleport();
            if (roll() < third[1] && away(5, true)) return { MonAct::none };
        }
        if (input.skill[0] && roll() < first[2]) return teleport();
        if (input.dist < third[0] && roll() < third[1]) { (void)away(5, false); return { MonAct::none }; }
        if (input.skill45[0] && input.second) {
            if (input.second_dist < third[2]) { if (rng(100) < third[3]) return cast(3, ThinkUnit::second); }
            else if (input.second_dist < fourth[2] && rng(100) < fourth[3]) return cast(3, ThinkUnit::second);
        }
        if (roll() < 0x21) return { MonAct::approach, 4 };
        if (roll() > 0x13) return { MonAct::idle, 10 };
        return think_wander(rng, 8);
    }
    // Catapult (123, FUN_005e34c0): aip1 % A1 (at no unit), else stands 15.
    Think catapult() {
        if (roll() < aip[0]) { Think out{ MonAct::a1 }; out.unit = ThinkUnit::none; return out; }
        return { MonAct::idle, 15 };
    }
    // FrozenHorror (124, FUN_005e3530): Skill1 (Arctic Blast) within its level
    // aip3 % while state 0xc is off; state 0xc then off. In melee aip1 % A1;
    // out of it aip2 % walks straight at it; else stands aip4.
    Think frozen_horror() {
        const int reach = input.skill[0] ? std::max(input.skill_level, 0) : 0;
        if (input.skill[0] && input.dist < reach && roll() < aip[2] && !input.state_c) return cast(0);
        if (input.state_c) effect(ThinkEffect::state, 0xc, 0);
        if (!input.in_melee) { if (roll() < aip[1]) return { MonAct::direct }; }
        else if (roll() < aip[0]) return { MonAct::a1 };
        return { MonAct::idle, aip[3] };
    }
    // BloodLord (125, FUN_005e36f0): out of melee aip2 % walks straight at
    // it, else stands aip4; in it aip1 %: aip3 % Skill1 in A2, else A1; else
    // stands aip4.
    Think blood_lord() {
        if (!input.in_melee) return roll() < aip[1] ? Think{ MonAct::direct } : Think{ MonAct::idle, aip[3] };
        if (roll() < aip[0]) return roll() < aip[2] ? cast(0) : Think{ MonAct::a1 };
        return { MonAct::idle, aip[3] };
    }
    // CatapultSpotter (126, FUN_005ee040): a check for its catapult (FUN_005edf70,
    // 3 % of thinks once it has called one, state3): none, it dies (mode 0).
    // Every aip2 frames since its last call (state3) with a target, aip1 %:
    // a strike (0x6e3514[rand(5)], kept for aip5 calls: state its pick, state2
    // the calls left) at a point within aip4 of the target, where a spot's
    // free (else stands 15); else stands aip2.
    Think catapult_spotter() {
        if (*input.state3 == 0 || roll() <= 2) { if (input.catapult_gone) return { MonAct::die }; }
        if (*input.state3 == 0) *input.state3 = 1;
        if (aip[1] - input.frame + *input.state3 < 1 && input.target && roll() < aip[0]) {
            static constexpr std::array<int, 5> kStrikes{ 287, 288, 303, 304, 305 };
            int pick = 0;
            if (*input.state2 < 1) { pick = rng(5); *input.state = pick; *input.state2 = aip[4]; }
            else { pick = *input.state; --*input.state2; }
            Think out{ MonAct::skill, pick >= 0 && pick < 5 ? kStrikes[std::size_t(pick)] : 0 };   // only rand(5) is ever kept
            out.unit = ThinkUnit::none;
            out.x = rng(2 * aip[3]) - aip[3] + input.target_x;
            out.y = rng(2 * aip[3]) - aip[3] + input.target_y;
            if (!input.strike_spot) return { MonAct::idle, 15 };
            *input.state3 = input.frame;
            return out;
        }
        return idle2;
    }
    // NpcBarb (127, FUN_005edc50; Harrogath's defenders): in melee A1, then a
    // wait of aip1; within aip3 aip2 % runs at it at 100 % pace. Else a walk
    // to a point about its spot (up to three tries, FUN_005dee50: 20 square,
    // 40 left of it then 30 above or below it); none sets off, stands 15.
    Think npc_barb() {
        if (input.target) {
            if (input.in_melee) { Think out{ MonAct::a1 }; out.wait = aip[0]; return out; }
            if (input.dist < aip[2] && roll() < aip[1]) { set_pace(100, 0); return { MonAct::run }; }
        }
        auto head_to = [&](int to_x, int to_y) { return away(-1, false) ? std::optional<Think>{ Think{ MonAct::move_to, 0, to_x, to_y, ThinkUnit::none } } : std::nullopt; };
        const std::uint32_t first = rng.next(), first_y = rng.next();
        if (auto out = head_to(int(first % 20) - 40 + input.self_x, int(first_y % 20) - 10 + input.self_y)) return *out;
        const std::uint32_t side = rng.next(), second_x = rng.next(), second_y = rng.next();
        if (auto out = head_to(int(second_x % 20) - 10 + input.self_x, ((side & 1) ? -30 : 30) + int(second_y % 20) - 10 + input.self_y)) return *out;
        const std::uint32_t third_x = rng.next(), third_y = rng.next();
        if (auto out = head_to(int(third_x % 20) - 10 + input.self_x, ((side & 1) ? 30 : -30) + int(third_y % 20) - 10 + input.self_y)) return *out;
        return { MonAct::idle, 15 };
    }
    // GenericSpawner (129, FUN_005e61b0; the evil huts' doors): with a target,
    // no spawn type found leaves the game (flag 0x20000); within 21: aip3
    // made (state2) it leaves; every aip1 frames (state, abs) with room for
    // one, a young out (sequence 0xa7). Else stands 20.
    Think generic_spawner() {
        if (input.target) {
            if (input.spawn_unset && !input.spawn_found) return { MonAct::gone };
            if (input.dist < 0x15) {
                if (aip[2] <= *input.state2) return { MonAct::gone };
                if (aip[0] <= std::abs(input.frame - *input.state)) {
                    *input.state = input.frame;
                    if (input.spawn_room) { ++*input.state2; return { MonAct::seq, 0xa7 }; }
                }
            }
        }
        return { MonAct::idle, 20 };
    }
    // DeathMauler (130, FUN_005ee260): out of melee Skill1 (its burrow
    // charge) within aip3 aip4 %, aip2 % walks in, else stands 15; in it
    // aip1 % A1, else stands 15.
    Think death_mauler() {
        if (!input.in_melee) {
            if (input.skill[0] && input.dist < aip[2] && roll() < aip[3]) return cast(0);
            if (roll() < aip[1]) return walk_in;
        } else if (roll() < aip[0]) return { MonAct::a1 };
        return { MonAct::idle, 15 };
    }
    // PutridDefiler (137, FUN_005efa90): in melee A1. Its find within 25
    // (FUN_005efa50): in reach skill 300 (mode 8) at it, else straight at it;
    // none: within aip1 it backs off aip2, else stands 25.
    Think putrid_defiler() {
        if (input.target && input.in_melee) return { MonAct::a1 };
        if (!input.defile) {
            if (input.dist < aip[0]) { (void)away(aip[1] & 0xff, false); return { MonAct::none }; }
            return { MonAct::idle, 25 };
        }
        if (input.reach) { Think out{ MonAct::skill, 300 }; out.unit = ThinkUnit::kin; return out; }
        Think out{ MonAct::direct }; out.unit = ThinkUnit::kin; return out;
    }
    // BaalTentacle (139, FUN_005ef820; Festering Appendages): its master gone
    // or busy, it goes (FUN_0057ccb0). Its life (state3) aip3 + rand(aip3)
    // x 25 frames from its first think; past it, it goes; till then in melee
    // aip1 % A1, else stands aip2.
    Think baal_tentacle() {
        if (!input.rider || input.command_busy) return { MonAct::die };
        if (*input.state3 == 0) { const int extra = rng(aip[2]); *input.state3 = input.frame + (extra + aip[2]) * 25; }
        if (*input.state3 < input.frame) return { MonAct::die };
        if (input.in_melee && rng(100) < aip[0]) return { MonAct::a1 };
        return idle2;
    }
    // BaalTaunt (136, FUN_005ef710): a target standing still (mode 1) counts
    // up the scratch word; past aip2 it's 0 again and Skill1 (Baal Taunt, A1)
    // goes at it. Otherwise (not still: 0 again) past aip3 it warps to the
    // target (FUN_00554ea0), past aip1 walks straight at it; else stands 25.
    Think baal_taunt() {
        if (!input.target) return { MonAct::idle, 25 };
        if (input.target_still && ++*input.state > aip[1]) { *input.state = 0; return cast(0); }
        if (!input.target_still) *input.state = 0;
        if (input.dist > aip[2]) return { MonAct::warp, 0, input.target_x, input.target_y, ThinkUnit::none };
        if (input.dist > aip[0]) return { MonAct::direct };
        return { MonAct::idle, 25 };
    }
    // BaalMinion (141, FUN_005ef910; the waves' minions): out of melee (or no
    // target) aip2 % straight at it; in it aip1 %: Skill1 aip3 % (rand), else
    // A1; else stands aip3. A wait of aip4 after.
    Think baal_minion() {
        Think out{ MonAct::none };
        if (!input.target || !input.in_melee) { if (roll() < aip[1]) out = { MonAct::direct }; }
        else if (roll() < aip[0]) out = input.skill[0] && rng(100) < aip[2] ? cast(0) : Think{ MonAct::a1 };
        else out = { MonAct::idle, aip[2] };
        out.wait = aip[3];
        return out;
    }
    // Ancient (133, FUN_005ef1a0): asleep, they stand 25. Their foe: Talic's
    // target or, as the others always, a pick (FUN_005e8f20); none, stands 25.
    // `apart`: FUN_006416d0, the units' distance less half each one's size.
    // Talic (FUN_005eeb70; not under state 0x36): Skill1 (Whirlwind) within aip1
    // aip2 % to aip4 past the foe along their line (mode set directly,
    // FUN_005a7c20); out of reach straight at it, a wait of 10; aip3 %: A1 or
    // A2 (rand(2)); else stands 25.
    // Madawc (FUN_005eeda0): in reach aip4 % backs off aip5 running, a wait of
    // 25; Skill1 (at no unit) aip3 % unless its states are on; within aip1
    // aip2 % A1, else stands 10; past it approaches to aip1.
    // Korlic (FUN_005ef010): Skill1's sequence within aip1 aip2 %; out of
    // reach straight at it, a wait of 10; aip3 %: A1 or A2; else stands 25.
    Think ancient() {
        if (input.asleep || (input.ancient == 0 && input.state36)) return { MonAct::idle, 25 };
        if ((input.ancient != 0 || !input.target) && !input.picked) return { MonAct::idle, 25 };
        const int apart = point_apart(input.target_x - input.self_x, input.target_y - input.self_y, input.self_size / 2 + input.target_size / 2);
        auto swing = [&] { return rng(2) != 0 ? Think{ MonAct::a1 } : Think{ MonAct::a2 }; };
        auto straight = [&] { Think out{ MonAct::direct }; out.wait = 10; return out; };
        if (input.ancient == 0) {
            if (input.skill[0] && apart < aip[0] && rng(100) < aip[1]) {
                const int off_x = input.target_x - input.self_x, off_y = input.target_y - input.self_y;
                const int span = std::max(point_apart(off_x, off_y, 0), 1);
                Think out = cast(0, ThinkUnit::none);
                out.x = input.target_x + aip[3] * off_x / span;
                out.y = input.target_y + aip[3] * off_y / span;
                return out;
            }
            if (!input.reach) return straight();
            if (rng(100) < aip[2]) return swing();
            return { MonAct::idle, 25 };
        }
        if (input.ancient == 1) {
            if (input.reach && roll() < aip[3] && away(aip[4] & 0xff, true)) { Think out{ MonAct::none }; out.wait = 25; return out; }
            if (input.skill[0] && !input.skill_state && roll() < aip[2]) return cast(0, ThinkUnit::none);
            if (apart < aip[0] && roll() < aip[1]) return { MonAct::a1 };
            if (apart < aip[0]) return { MonAct::idle, 10 };
            return { MonAct::approach, int(std::int8_t(apart - aip[0])) };
        }
        if (input.skill[0] && apart < aip[0] && rng(100) < aip[1]) return { MonAct::seq, 0 };
        if (!input.reach) return straight();
        if (roll() < aip[2]) return swing();
        return { MonAct::idle, 25 };
    }
    // Nihlathak (128, FUN_005ee5d0; his quest's step FUN_0058bc40 isn't here):
    // state 0xc off. Skill1 (his teleport) is FUN_005df850 within aip2 of
    // himself. No target: hit, he teleports; else stands 25. In melee aip1 %
    // teleports; within aip5 40 % backs off 5 running. Skill3 (Corpse
    // Explosion) aip3 % at a corpse (FUN_0056e390). Skill4 within 14 60 %.
    // Skill2 aip4 %: at a minion within 25 (FUN_005e26d0), else Skill5 (a
    // minion) where there's room, else wanders 6. Past 13 (or no Skill4) 60 %
    // straight at it, a stand of 5 after; else Skill4.
    Think nihlathak() {
        if (input.state_c) effect(ThinkEffect::state, 0xc, 0);
        auto teleport = [&] {
            const int radius = aip[1];
            Think out = cast(0, ThinkUnit::none);
            out.x = rng(2 * radius) - radius + input.self_x;
            out.y = rng(2 * radius) - radius + input.self_y;
            return out;
        };
        if (!input.target) return input.got_hit ? teleport() : Think{ MonAct::idle, 25 };
        if (input.in_melee && roll() < aip[0]) return teleport();
        if (input.dist < aip[4] && roll() < 40 && away(5, true)) return { MonAct::none };
        if (input.skill[2] && roll() < aip[2] && input.corpse) return cast(2, ThinkUnit::kin);
        if (input.skill45[0] && roll() < 60 && input.dist < 14) return cast(3);
        if (input.skill[1] && roll() < aip[3]) {
            if (input.free_minion) return cast(1, ThinkUnit::aid);   // FUN_005e26d0's second find (within 25, +0x10)
            if (input.skill45[1] && input.spawn_room) return cast(4);
            return think_wander(rng, 6);
        }
        if (!input.skill45[0] || input.dist > 13) {
            Think out{ MonAct::idle, 5 };
            if (roll() < 60) { out = { MonAct::direct }; out.wait = 5; }
            return out;
        }
        return cast(3);
    }
    // ClawViperEx (142, FUN_005f1de0; Act 5's claw vipers): its charge's
    // state (aip6: 2 state 0x5b, else 0x5a) off after (state). In melee aip3 %
    // A2, else stands aip5. Within aip7 aip4 %: A1 past its timer (state2,
    // then aip8 frames on), else stands aip5. Skill1 within aip2 aip1 % when
    // usable (FUN_005fd470), its state on, state 1; 50 % walks in; else
    // stands aip5.
    Think claw_viper_ex() {
        const int charge_state = aip[5] == 2 ? 0x5b : 0x5a;
        if (*input.state != 0 && aip[5] != 0) effect(ThinkEffect::state, charge_state, 0);
        if (input.in_melee) return roll() < aip[2] ? Think{ MonAct::a2 } : Think{ MonAct::idle, aip[4] };
        if (input.dist < aip[6] && roll() < aip[3]) {
            if (*input.state2 < input.frame) { *input.state2 = aip[7] + input.frame; return { MonAct::a1 }; }
            return { MonAct::idle, aip[4] };
        }
        if (input.skill[0] && input.dist < aip[1] && roll() < aip[0] && input.usable) {
            if (aip[5] != 0) effect(ThinkEffect::state, charge_state, 1);
            *input.state = 1;
            return cast(0);
        }
        return roll() < 0x32 ? walk : Think{ MonAct::idle, aip[4] };
    }
    // HellMeteor (33, FUN_005f56d0; the River of Flame's): with Skill1, aip1 %
    // casts it at a point aip3 about itself (rand(2 aip3) - aip3 off on x,
    // then y), at no unit; else it stands aip2.
    Think hell_meteor() {
        if (input.skill[0] && roll() < aip[0]) {
            const int x = rng(2 * aip[2]) + input.self_x - aip[2];
            const int y = rng(2 * aip[2]) + input.self_y - aip[2];
            return { MonAct::skill, 0, x, y, ThinkUnit::none };
        }
        return { MonAct::idle, aip[1] };
    }
    // Sarcophagus (45, FUN_005f6a10; init FUN_005f6630, the nest's): past 25
    // stands 25; once it's laid more than aip3 it's gone (mode 0); every
    // aip1 frames (stamped only when it lays) it lays (Skill1, Nest) where
    // there's room; else stands 20 + a seed step % 10.
    Think sarcophagus() {
        if (input.dist > 25) return { MonAct::idle, 25 };
        if (*input.state2 > aip[2]) return { MonAct::gone };
        if (input.skill[0] && std::abs(input.frame - *input.state) >= aip[0] && input.spot_free) {
            ++*input.state2;
            *input.state = input.frame;
            return { MonAct::skill, 0 };
        }
        return { MonAct::idle, int(rng.next() % 10) + 20 };
    }
    // Spirit (97, FUN_005e3840; Mephisto's in the Durance): once it's struck
    // (its scratch word) it stands 50; in melee it strikes (A1); else stands 10.
    Think spirit() {
        if (*input.state != 0) return { MonAct::idle, 50 };
        if (input.in_melee) { *input.state = 1; return { MonAct::a1 }; }
        return { MonAct::idle, 10 };
    }
    // ArcaneTower (93, FUN_005e0f60; the lightning spire): its count (state2)
    // starts at aip1; before its frame (state3) it stands 10. Casting (state
    // 0, with Skill1) it casts Skill1 at the target, aip2 frames on, and after
    // the count's done strikes: aip4 of them (A1), aip6 frames apart; the
    // count done, it casts again aip5 frames on (aip3 after the casts).
    Think arcane_tower() {
        if (*input.state2 == 0) *input.state2 = aip[0];
        if (input.frame < *input.state3) return { MonAct::idle, 10 };
        if (input.skill[0] && *input.state == 0) {
            if (--*input.state2 < 1) { *input.state = 1; *input.state2 = aip[3]; *input.state3 = aip[2] + input.frame; }
            else *input.state3 = aip[1] + input.frame;
            return cast(0);
        }
        if (--*input.state2 < 1) { *input.state = 0; *input.state2 = aip[0]; *input.state3 = aip[4] + input.frame; }
        else *input.state3 = aip[5] + input.frame;
        return { MonAct::a1 };
    }
    // Trap-RightArrow / Trap-LeftArrow (78 / 79, FUN_005fb6c0 / FUN_005fb7e0):
    // a target aip1..aip2 off and within 2 of its column (Right) or row
    // (Left), past its frame (state): aip3 frames on, the level's poison kind
    // casts Skill1 (state3 aip4 frames on), another the spike (A1); else it
    // stands 30. Out of that range it stands 40.
    Think arrow_trap() {
        if (input.dist < aip[0] || aip[1] < input.dist) return { MonAct::idle, 40 };
        const int off = ai_name == "Trap-RightArrow" ? input.target_x - input.self_x : input.target_y - input.self_y;
        if (std::abs(off) < 3 && *input.state < input.frame) {
            *input.state = aip[2] + input.frame;
            if (input.trap_kind != 1) return { MonAct::a1 };
            if (input.skill[0]) { *input.state3 = aip[3] + input.frame; return cast(0); }
        }
        return { MonAct::idle, 30 };
    }
    // Trap-Melee (87, FUN_005fba60; trap-melee, an Act 3 MonPreset): out of
    // melee it stands 40; in melee, a step of its seed % 100 under aip1 an A1
    // at the target, else it stands aip2.
    Think trap_melee() {
        if (!input.in_melee) return { MonAct::idle, 40 };
        if (roll() < aip[0]) return { MonAct::a1 };
        return { MonAct::idle, aip[1] };
    }
    // DesertTurret (94, FUN_005e0980; the fire towers): with Skill1 and no
    // frame yet it casts it where it stands (state3 0, state the frame);
    // before its frame (state) it stands 10. Within aip4 of the target it
    // turns its step (state3) toward the target's eighth (DAT_00745600 of
    // the 64 direction, 0x6e33e0) and, the lines there and to the target
    // clear, casts Skill1 aip5 steps (0x6e33a0) off itself, at no unit: aip1
    // frames on, aip3 once it's cast more than aip2 times running (the run
    // then 0). Else the run counts down one and it stands 10 (15 out of reach).
    Think desert_turret() {
        if (input.skill[0] && *input.state == 0) { *input.state3 = 0; *input.state = input.frame; return { MonAct::skill, 0, 0, 0, ThinkUnit::none }; }
        if (input.frame < *input.state) return { MonAct::idle, 10 };
        const bool in_reach = input.dist <= aip[3];
        if (in_reach) {
            const int eighth = ((direction64(input.self_x, input.self_y, input.target_x, input.target_y) + 4) >> 3) & 7;
            const int step = kTurretTurn[std::size_t(eighth)][std::size_t(*input.state3 & 7)];
            *input.state3 = step;
            if (input.skill[0] && input.ray_clear[std::size_t(step)] && input.usable) {
                if (++*input.state2 > aip[1]) { *input.state = aip[2] + input.frame; *input.state2 = 0; }
                else *input.state = aip[0] + input.frame;
                return { MonAct::skill, 0, input.self_x + kTurretStep[std::size_t(step)][0] * aip[4], input.self_y + kTurretStep[std::size_t(step)][1] * aip[4], ThinkUnit::none };
            }
        }
        if (*input.state2 > 0) --*input.state2;
        return { MonAct::idle, in_reach ? 10 : 15 };
    }
};

inline constexpr auto kAct5Thinks = std::to_array<ThinkRow>({
    { "ReanimatedHorde", &think_as<Act5Thinks, &Act5Thinks::reanimated_horde> },
    { "SiegeBeast", &think_as<Act5Thinks, &Act5Thinks::siege_beast> },
    { "Minion", &think_as<Act5Thinks, &Act5Thinks::minion> },
    { "SuicideMinion", &think_as<Act5Thinks, &Act5Thinks::suicide_minion> },
    { "Succubus", &think_as<Act5Thinks, &Act5Thinks::succubus> },
    { "SuccubusWitch", &think_as<Act5Thinks, &Act5Thinks::succubus_witch> },
    { "Overseer", &think_as<Act5Thinks, &Act5Thinks::overseer> },
    { "MinionSpawner", &think_as<Act5Thinks, &Act5Thinks::minion_spawner> },
    { "ImpRide", &think_as<Act5Thinks, &Act5Thinks::imp_ride> },
    { "SiegeTower", &think_as<Act5Thinks, &Act5Thinks::siege_tower> },
    { "Imp", &think_as<Act5Thinks, &Act5Thinks::imp> },
    { "Catapult", &think_as<Act5Thinks, &Act5Thinks::catapult> },
    { "FrozenHorror", &think_as<Act5Thinks, &Act5Thinks::frozen_horror> },
    { "BloodLord", &think_as<Act5Thinks, &Act5Thinks::blood_lord> },
    { "CatapultSpotter", &think_as<Act5Thinks, &Act5Thinks::catapult_spotter> },
    { "NpcBarb", &think_as<Act5Thinks, &Act5Thinks::npc_barb> },
    { "GenericSpawner", &think_as<Act5Thinks, &Act5Thinks::generic_spawner> },
    { "DeathMauler", &think_as<Act5Thinks, &Act5Thinks::death_mauler> },
    { "PutridDefiler", &think_as<Act5Thinks, &Act5Thinks::putrid_defiler> },
    { "BaalTentacle", &think_as<Act5Thinks, &Act5Thinks::baal_tentacle> },
    { "BaalTaunt", &think_as<Act5Thinks, &Act5Thinks::baal_taunt> },
    { "BaalMinion", &think_as<Act5Thinks, &Act5Thinks::baal_minion> },
    { "Ancient", &think_as<Act5Thinks, &Act5Thinks::ancient> },
    { "Nihlathak", &think_as<Act5Thinks, &Act5Thinks::nihlathak> },
    { "ClawViperEx", &think_as<Act5Thinks, &Act5Thinks::claw_viper_ex> },
    { "HellMeteor", &think_as<Act5Thinks, &Act5Thinks::hell_meteor> },
    { "Sarcophagus", &think_as<Act5Thinks, &Act5Thinks::sarcophagus> },
    { "Spirit", &think_as<Act5Thinks, &Act5Thinks::spirit> },
    { "ArcaneTower", &think_as<Act5Thinks, &Act5Thinks::arcane_tower> },
    { "Trap-RightArrow", &think_as<Act5Thinks, &Act5Thinks::arrow_trap> },
    { "Trap-LeftArrow", &think_as<Act5Thinks, &Act5Thinks::arrow_trap> },
    { "Trap-Melee", &think_as<Act5Thinks, &Act5Thinks::trap_melee> },
    { "DesertTurret", &think_as<Act5Thinks, &Act5Thinks::desert_turret> },
});

}  // namespace d2d::rules
