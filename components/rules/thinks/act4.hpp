// SPDX-License-Identifier: GPL-3.0-or-later
// Act 4's MonAI thinks (monster-ai.md "Act 4's MonAIs"; tools/emu/mon_ais.py),
// Diablo's and Baal's, the Ubers'.
#pragma once

#include "thinker.hpp"

#include <array>

namespace d2d::rules {

struct Act4Thinks : Thinker {
    // VileMother (68, FUN_005fa010): under aip1 laid (state), aip3 %, with
    // under aip2 young within 25, it lays one toward the target (FUN_005f9e30:
    // Skill1 at the first of eight spots that fits). In melee aip4 % A1, else
    // stands aip7; out of it, still laying, aip5 % on: aip6 % circles 4, else
    // stands 15; else walks in.
    Think vile_mother() {
        if (*input.state < aip[0] && roll() < aip[2] && input.young < aip[1] && input.lay_ok) { ++*input.state; return { MonAct::spawn, 2 }; }
        if (input.in_melee) return roll() < aip[3] ? Think{ MonAct::a1 } : Think{ MonAct::idle, aip[6] };
        if (*input.state < aip[0] && roll() >= aip[4]) return roll() < aip[5] ? circle(4, 0, set_off_unused) : Think{ MonAct::idle, 15 };
        return walk;
    }
    // VileDog (69, FUN_005fa280): its first think stands 5; in melee aip1 %
    // A1, else stands aip2; out of it aip3 % walks in, else stands 10.
    Think vile_dog() {
        if (*input.state == 0) { *input.state = 1; return { MonAct::idle, 5 }; }
        if (input.in_melee) return roll() < aip[0] ? Think{ MonAct::a1 } : idle2;
        return roll() < aip[2] ? walk : Think{ MonAct::idle, 10 };
    }
    // DoomKnight (72, FUN_005faa90): in melee aip1 % A1, else stands aip2;
    // out of it aip3 % runs in, else stands aip4.
    Think doom_knight() {
        if (input.in_melee) return roll() < aip[0] ? Think{ MonAct::a1 } : idle2;
        return roll() < aip[2] ? Think{ MonAct::run, 7 } : Think{ MonAct::idle, aip[3] };
    }
    // AbyssKnight (73, FUN_005fab80): Bone Armor (Skill2, at no unit) when its
    // state's off and life under aip1 %, aip2 %. In melee aip3 % A1, else
    // stands aip4. Within aip5 its fire timer (state) starts at aip6; at 0
    // Skill1 (the missile), the timer aip6; else it counts down. aip7 % runs
    // in (steps 6 or 7 on the seed's low bit); within aip8 circles 3; else
    // stands 15.
    Think abyss_knight() {
        if (input.skill[1] && !input.skill_state && input.life_pct < aip[0] && roll() < aip[1]) return cast(1, ThinkUnit::none);
        if (input.in_melee) return roll() < aip[2] ? Think{ MonAct::a1 } : Think{ MonAct::idle, aip[3] };
        if (input.dist < aip[4] && *input.state < 1) *input.state = aip[5];
        if (input.skill[0] && *input.state == 0) { *input.state = aip[5]; return cast(0); }
        if (*input.state > 0) --*input.state;
        if (roll() < aip[6]) { set_pace(0, int(rng.next() & 1) + 6); return walk; }
        if (input.dist < aip[7]) return circle(3, 0, set_off_unused);
        return { MonAct::idle, 15 };
    }
    // OblivionKnight (74, FUN_005faf00): within aip1 its curse (Skill4) at a
    // target not under it, its cooldown (state) aip3 frames; else to a knight
    // (class 0x136, within 50, FUN_005fae40) farther from the target than it is, else
    // backs off 10 (pace 50), else Bone Spirit (Skill3). The second target
    // within aip2: Skill6 past the cooldown (aip4 %), else aip5 %: Bone
    // Spirit (aip6 %) or the missile (Skill1) at it. Past aip8, aip7 % about
    // the target 6; else 70 % circles 3, 30 % stands 10.
    Think oblivion_knight() {
        auto cursed = [&](int slot) { auto out = cast(slot); *input.state = aip[2] + input.frame; return out; };
        if (input.dist < aip[0]) {
            if (input.skill45[0] && !input.skill_state) return cursed(3);
            if (input.kin && input.kin_dist <= 2500 && input.dist * input.dist < squared(input.kin_x, input.kin_y, input.target_x, input.target_y)) return { MonAct::walk, 4, 0, 0, ThinkUnit::kin };
            set_pace(50, 0);
            if (away(10, true)) return { MonAct::none };
            if (input.skill[2]) return cast(2);
        }
        if (input.second && input.second_dist < aip[1]) {
            if (input.skill678[0] && *input.state < input.frame && roll() < aip[3]) return cursed(5);
            if (roll() < aip[4]) {
                if (input.skill[2] && roll() < aip[5]) return cast(2, ThinkUnit::second);
                if (input.skill[0]) return cast(0, ThinkUnit::second);
            }
        }
        if (aip[7] < input.dist && roll() < aip[6]) { auto out = think_wander(rng, 6); out.act = MonAct::around; return out; }
        if (roll() > 69) return { MonAct::idle, 10 };
        return circle(3, 0, set_off_unused);
    }
    // Megademon (89, FUN_005e0c80; Venom Lords, Infector of Souls): Inferno
    // (Skill1) out of melee within its level (at least 1) while state 0xc is
    // off: aip1 % past its cooldown (state, a frame), the cooldown aip6
    // frames; else state 0xc off. In melee: Inferno aip2 % past the cooldown,
    // else aip3 % A1, else aip5 % circles 3, else stands 5. Out of it aip4 %
    // walks in, else stands 10.
    Think megademon() {
        const int reach = input.skill[0] ? std::max(input.skill_level, 1) : 1;
        auto inferno = [&] { *input.state = aip[5] + input.frame; return cast(0); };
        bool bottom = false;
        if (!input.skill[0] || input.in_melee || reach <= input.dist) { if (input.state_c) effect(ThinkEffect::state, 0xc, 0); }
        else if (!input.state_c) {
            if (*input.state < input.frame && roll() < aip[0]) return inferno();
            bottom = true;
        } else effect(ThinkEffect::state, 0xc, 0);
        if (!bottom && input.in_melee) {
            if (input.skill[0] && *input.state < input.frame && roll() < aip[1]) return inferno();
            if (roll() >= aip[2]) return roll() >= aip[4] ? Think{ MonAct::idle, 5 } : circle(3, 0, set_off_unused);
            return { MonAct::a1 };
        }
        return roll() >= aip[3] ? Think{ MonAct::idle, 10 } : walk;
    }
    // FingerMage (70, FUN_005fa380; Storm Casters): pace (Run·100 / Velocity
    // − 100, 0..120). Hit: in melee A1, or Skill1 within aip5. Fleeing (state
    // 1, state2 its thinks): past aip3 % life, 25 %, or past aip6 thinks it
    // stops (stands 15); within 14 it backs off 14, else stands 15. Alone
    // under aip4 % life it flees (back off 9). Out of melee past aip5: within
    // aip7 walks in, else stands 15; else aip2 % Skill1, else the target under
    // state 0x54 walks to it, else stands aip8. In melee aip1 % A1, else aip2 %
    // Skill1, else circles 5.
    Think finger_mage() {
        int pct = input.velocity > 0 ? input.run * 100 / input.velocity - 100 : 0;
        pct = pct < 1 ? 0 : std::min(pct, 120);
        if (input.got_hit) {
            *input.state = 0;
            if (input.in_melee) return { MonAct::a1 };
            if (input.skill[0] && input.dist < aip[4]) return cast(0);
        }
        if (*input.state == 1) {
            ++*input.state2;
            if (aip[2] < input.life_pct || roll() < 25 || aip[5] < *input.state2) *input.state = 0;
            else if (input.dist < 14) { set_pace(pct, 0); away(14, false); return { MonAct::none }; }
            return { MonAct::idle, 15 };
        }
        if (!input.grouped && input.life_pct < aip[3]) {
            *input.state = 1; *input.state2 = 0;
            away(9, false);
            return { MonAct::none };
        }
        if (!input.in_melee) {
            if (aip[4] <= input.dist) return input.dist < aip[6] ? walk : Think{ MonAct::idle, 15 };
            if (!input.skill[0] || roll() >= aip[1]) return input.skill_state ? walk0 : Think{ MonAct::idle, aip[7] };
            return cast(0);
        }
        if (roll() < aip[0]) return { MonAct::a1 };
        if (!input.skill[0] || roll() >= aip[1]) return circle(5, 0, set_off_unused);
        return cast(0);
    }
    // Regurgitator (71, FUN_005fa710; Corpse Spitters): a corpse (state 2
    // walking to it, 3 at it, 4 eaten, 5 off; state2 its id, state3 tries).
    // 5: backs off 16, done. 2: the corpse still there (DD, mode 12): past 2
    // (squared 4) approaches it again (over 5 tries: wanders 8, done), then
    // 3 and stands 8; gone: wanders 8, done. 3: eats it (Skill1), 4. 4: out
    // of melee S2 at the target, 5; in melee backs off 8. Else the nearest
    // corpse within aip6: state 1 (searching) goes to it (approach, past
    // squared 2) or stands 8 there (3); none: 20 % done, else wanders 8.
    // Else a corpse within 3 (squared 9) aip2 %, or aip5 %: to it. In melee
    // aip1 % A1, else stands 15; out of it aip3 % walks to the target,
    // else a corpse aip4 %: to it; else stands 8.
    Think regurgitator() {
        const bool eatable = input.command_unit && input.leader_mode == 12;   // FUN_00552f60(state2): a corpse (DD)
        auto done = [&] { *input.state = *input.state2 = *input.state3 = 0; };
        auto go_eat = [&] { *input.state = 2; *input.state2 = input.kin_id; *input.state3 = 0; return Think{ MonAct::approach, 1, 0, 0, ThinkUnit::kin }; };
        const int kin_squared = squared(input.self_x, input.self_y, input.kin_x, input.kin_y);
        switch (*input.state) {
        case 5: away(16, true); *input.state = 0; return { MonAct::none };
        case 2:
            if (!eatable) { done(); return think_wander(rng, 8); }
            if (kin_squared > 4) {
                if (*input.state3 > 5) { done(); return think_wander(rng, 8); }
                ++*input.state3;
            }
            *input.state = 3;
            return { MonAct::idle, 8 };
        case 3:
            if (input.skill[0] && eatable) { *input.state = 4; return cast(0, ThinkUnit::kin); }
            done();
            return think_wander(rng, 8);
        case 4:
            if (!input.in_melee) { *input.state = 5; return { MonAct::a2 }; }
            away(8, true);
            return { MonAct::none };
        default: break;
        }
        const bool found = input.corpse && input.kin_dist <= aip[5] * aip[5];   // FUN_005fa690 within aip6 (squared)
        if (*input.state == 1) {
            if (found) {
                *input.state2 = input.kin_id;
                if (kin_squared > 2) { *input.state = 2; *input.state3 = 0; return { MonAct::approach, 1, 0, 0, ThinkUnit::kin }; }
                *input.state = 3; *input.state3 = 0;
                return { MonAct::idle, 8 };
            }
            if (roll() < 20) { done(); return { MonAct::none }; }
            return think_wander(rng, 8);
        }
        if (found) {
            if (kin_squared < 9 && roll() < aip[1]) return go_eat();
            if (roll() < aip[4]) return go_eat();
        }
        const int rolled = roll();
        if (input.in_melee) return rolled < aip[0] ? Think{ MonAct::a1 } : Think{ MonAct::idle, 15 };
        if (rolled < aip[2]) return walk0;
        if (found && roll() < aip[3]) return go_eat();
        return { MonAct::idle, 8 };
    }
    // Diablo (51, FUN_005e9170; monster-ai.md "Act 4's MonAIs"): his kept
    // point (the leader word, kind 10) is his own spot the first time; his
    // action from the chooser (FUN_005e8810), a pending one (state) first.
    Think diablo() {
        int word_x = input.word_x, word_y = input.word_y;
        if (!input.has_word) { word_x = input.self_x; word_y = input.self_y; effect(ThinkEffect::mark, word_x, word_y); }
        const auto apart = point_apart;
        int act = *input.state;
        if (act == 0 && !input.target) act = input.stuck ? 16 : int(rng.next() % 1000) > 0 ? 11 : 4;
        else if (act == 0) {
            const bool leader = input.target_player && input.command_unit;            // FUN_005353f0 -> FUN_00552f60
            const bool leader_near = leader && input.in_chaos && apart(input.kin_x - word_x, input.kin_y - word_y, 0) < 0x55;
            bool casting = false;
            for (const auto& [id, level] : { std::pair{ input.left_skill, input.left_level }, std::pair{ input.right_skill, input.right_level } })
                casting = casting || id == 0x3b || id == 0x38 || (id == 0x33 && level > 3) || (id == 0x1b && level > 7);
            const int off = apart(input.target_x - word_x, input.target_y - word_y, (input.target_size >> 1) + 1);
            const bool point_far = off > 0x55, point_very_far = off > 0x69;
            std::array<int, 17> weight{};
            if (input.reach) {
                weight = { 0, 0, 40, 70, 0, 40, 24, 40, 15, 0, 0, 0, 0, 0, 0, 0, 0 };
                if (input.target_life_pct < 20) weight[2] = 50;
                if (input.target_state_b) weight[7] = weight[8] = 0;
                if (input.target_light_res < input.target_fire_res) weight[6] -= 10;
                if (input.target_fire_res < input.target_light_res) weight[6] += 10;
                if (!input.line_clear) weight[5] = weight[6] = 0;
                if (leader_near) weight[15] = 10;
            } else if (input.line_clear) {
                weight = { 0, 0, 0, 0, 0, 25, 25, 0, 15, 20, 10, 0, 20, 0, 0, 0, 0 };
                if (apart(input.self_x - input.target_x, input.self_y - input.target_y, input.self_size) > 25) { weight[8] -= 5; weight[10] = 20; weight[5] = 0; }
                if (input.target_light_res < input.target_fire_res) { weight[6] -= 10; weight[8] -= 10; }
                if (input.target_fire_res < input.target_light_res) { weight[6] += 10; weight[8] += 10; }
                if (input.players_looked < 2) weight[6] -= 10;
                if (input.players_looked > 3) weight[6] += 5;
                if (input.score > 60) weight[9] += 10;
                if (input.players_looked < 2 && input.skill_bonus < 2) weight[9] = 0;
                if (casting && !point_far) { weight[6] += 10; weight[10] = 30; weight[13] = 15; }
                else if (point_far) { weight[1] = 0; weight[12] = 10; weight[10] = 0; weight[13] = 15; if (weight[9] == 0) weight[9] = 10; }
                if (point_very_far) { weight[9] = 20; weight[14] = 60; }
                if (leader_near) weight[15] = 15;
            } else {
                weight = { 0, 0, 0, 0, 5, 0, 25, 0, 25, 40, 0, 0, 25, 0, 0, 0, 0 };
                if (input.players_looked < 2) { weight[8] -= 5; weight[6] = 0; weight[1] = 25; weight[9] = 0; }
                if (casting && !point_far) weight[13] = 15;
                else if (point_far) {
                    weight[1] = 0; weight[12] = 15; weight[13] = 25;
                    if (weight[9] == 0) weight[9] = 20;
                    if (input.players_looked < 2) weight[9] -= 5;
                }
                if (point_very_far) weight[14] = 60;
                if (leader_near) weight[15] = 20;
            }
            if (input.usable) weight[9] = 0;                                            // FUN_005fd470(199): the target already in a prison
            if (weight[15] != 0 && leader && !input.usable) weight[15] = 0;
            int total = 0;
            for (const int value : weight) total += value;
            const int rolled = total > 0 ? rng(total) : 0;
            act = 11;
            for (int index = 0, sum = 0; index < 17; ++index)
                if ((sum += weight[std::size_t(index)]) > rolled) { act = index; break; }
        }
        *input.state = 0;
        auto skill = [&](int slot) { return slot < 5 ? skill_set(slot) : input.skill678[std::size_t(slot - 5)]; };
        if (act != 5 && skill(7) && !input.skill_state) return cast(7, input.target ? ThinkUnit::target : ThinkUnit::none);   // Skill8 first, at the target (none: no unit)
        switch (act) {
        case 1: set_pace(20, 0); return { MonAct::move_to, 0, input.target_x, input.target_y, ThinkUnit::none };
        case 2: return { MonAct::a1 };
        case 3: return { MonAct::a2 };
        case 4: return { MonAct::s4 };
        case 5:
            if (skill(0)) {
                if (!input.state_c) { *input.state = 5; return cast(0); }
                effect(ThinkEffect::state, 0xc, 0);
            }
            break;
        case 6: if (skill(2)) return cast(2); return { MonAct::idle, 2 };
        case 7: if (skill(1)) return cast(1); break;
        case 8: if (skill(3)) return cast(3); break;
        case 9: if (skill(6)) return cast(6); break;
        case 10: if (skill(4)) return cast(4); break;
        case 12: { bool set_off = false; return circle(4, 0, set_off); }
        case 13: if (skill(5)) return cast(5); break;
        case 14: {                                                                // to his point, else halfway back toward himself
            set_pace(50, 100);
            if (away(-1, false)) return { MonAct::move_to, 0, word_x, word_y, ThinkUnit::none };
            const int half = apart(input.self_x - word_x, input.self_y - word_y, 0) >> 1;
            const int to_x = word_x + half * ((input.self_x > word_x) - (input.self_x < word_x)), to_y = word_y + half * ((input.self_y > word_y) - (input.self_y < word_y));
            set_pace(50, 100);
            if (away(-1, false)) return { MonAct::move_to, 0, to_x, to_y, ThinkUnit::none };
            break;
        }
        case 15:
            if (!input.target || !input.target_player) return { MonAct::idle, 3 };
            if (!input.command_unit) break;
            if (skill(6)) return cast(6, ThinkUnit::none);
            break;
        case 16: { set_pace(20, 0); return think_wander(rng, 5); }
        default: return { MonAct::idle, input.difficulty == 1 ? 8 : input.difficulty == 2 ? 4 : 12 };
        }
        return { MonAct::idle, 2 };
    }
    // Baal (135, FUN_005fcfe0) and his Vile Effigy (140, FUN_005fd210;
    // monster-ai.md "Baal"): Diablo's kept point and pick (FUN_005fbdd0); the
    // chooser (FUN_005fc630): a pending act (AI control +0x14) first, no
    // target: stuck 4, else 8 % Tentacles (9), else stands (1); in reach,
    // a clear line or neither, weights by FUN_005fc1c0 / FUN_005fc450 /
    // FUN_005fc300 rolled on the unit seed. The acts (FUN_005fcb60), then a
    // wait of 25. The clone dies with no master (FUN_00552fd0) or a busy one;
    // its acts 7, 9, 14, 15 swing (2) and with no target it stands 15.
    Think baal_crab() {
        const bool clone = ai_name == "BaalCrabClone";
        if (clone && ((input.grouped && input.command_busy) || !input.rider || input.command_busy)) return { MonAct::die };
        int word_x = input.word_x, word_y = input.word_y;
        if (!input.has_word) { word_x = input.self_x; word_y = input.self_y; effect(ThinkEffect::mark, word_x, word_y); }
        const auto apart = point_apart;
        const bool casting = [&] {                                                  // FUN_005fc0c0: the target's right hand, then its left (FUN_005fc070)
            for (const auto& [id, level] : { std::pair{ input.right_skill, input.right_level }, std::pair{ input.left_skill, input.left_level } })
                if (id == 0x3b || id == 0x38 || (id == 0x33 && level > 3) || (id == 0x1b && level > 7)) return true;
            return false;
        }();
        int act = *input.state;
        if (act == 0 && !input.target) act = input.stuck ? 4 : rng.next() % 100 < 8 ? 9 : 1;
        else if (act == 0) {
            const int off = apart(input.target_x - word_x, input.target_y - word_y, (input.target_size >> 1) + 1);
            const bool point_far = off > 0x4b, point_very_far = off > 100;
            const bool normal = input.difficulty == 0;
            std::array<int, 16> weight{};
            if (input.reach) {                                                      // FUN_005fc1c0
                weight = { 0, (normal ? 75 : 50) + 100 - input.life_pct, 0, 0, 0, 0, 0, 0, 20, 30, 150, 10, 10, normal ? 45 : 70, 40, 0 };
                if (input.target_life_pct < 33) weight[10] = 200;
                if (input.target_state_b) weight[8] = weight[13] = weight[11] = 0;
                if (!input.line_clear) weight[11] = weight[13] = weight[12] = 0;
            } else if (input.line_clear) {                                          // FUN_005fc450
                weight = { 0, (normal ? 125 : 100) + 100 - input.life_pct, 5, 5, 5, 0, 5, 0, 40, 40, 0, 70, 80, normal ? 40 : 60, 20, std::max((2 - *input.state2) * 10, 0) };
                const int apart_target = apart(input.self_x - input.target_x, input.self_y - input.target_y, input.self_size);   // FUN_005dc380
                if (apart_target > 0x23) { weight[6] = 15; weight[13] = 0; }
                if (apart_target > 0x19) { weight[14] = 30; weight[11] = 0; }
                if (input.players_looked < 2) weight[11] -= 10;
                if (input.players_looked > 3) weight[13] += 25;
                if (input.score > 0x3c) weight[8] = 70;
                if (input.players_looked < 2 && input.difficulty < 2) weight[8] = 0;   // FUN_00573930 +0xc
                if (casting && !point_far) { weight[6] += 30; weight[11] += 10; weight[14] += 15; }
                else if (point_far) { weight[6] += 25; weight[14] += 35; weight[2] = 0; weight[3] = 25; }
                if (point_very_far) weight[5] = 60;
            } else {                                                                // FUN_005fc300
                weight = { 0, (normal ? 125 : 100) + 100 - input.life_pct, 20, 20, 20, 0, 20, 0, 80, 70, 0, 0, 0, 0, 0, 0 };
                if (input.players_looked < 2) { weight[2] = 45; weight[10] = 25; }
                if (casting && !point_far) { weight[13] = 50; weight[14] = 50; }
                else if (point_far) { weight[13] = 25; weight[2] = 0; weight[6] = 0; weight[3] = 35; weight[11] = 25; }
                if (point_very_far) weight[5] = 60;
            }
            int total = 0;
            for (const int value : weight) total += value;
            const int rolled = total > 0 ? rng(total) : 0;
            act = 1;
            for (int index = 0, sum = 0; index < 16; ++index)
                if ((sum += weight[std::size_t(index)]) > rolled) { act = index; break; }
        }
        if (clone && (act == 7 || act == 9 || act == 14 || act == 15)) act = 2;
        if (clone && !input.target) return { MonAct::idle, 15 };
        auto skill = [&](int slot) { return slot < 5 ? skill_set(slot) : input.skill678[std::size_t(slot - 5)]; };
        auto waited = [](Think out) { out.wait = 25; return out; };
        const ThinkUnit baal_aim = input.target ? ThinkUnit::target : ThinkUnit::none;
        switch (act) {
        case 1: return waited({ MonAct::idle, input.difficulty == 1 ? 15 : input.difficulty == 2 ? 5 : 35 });
        case 2: case 6: { Think out{ MonAct::keep, 12, 0, 2 }; out.unit = baal_aim; return waited(out); }   // FUN_005de6d0 -> FUN_005de4e0(.., 2, ..)
        case 3: { bool set_off = false; auto out = circle(6, 1, set_off); out.unit = baal_aim; return waited(out); }
        case 4: return waited(think_wander(rng, 16));
        case 5: return waited({ MonAct::move_to, 0, word_x, word_y, ThinkUnit::none });
        case 8:                                                                     // Defense Curse, else Blood Mana baal_aim a player
            if (!input.target) break;
            if (skill(5) && (input.target_max_mana < input.target_max_life || !input.target_player)) return waited(cast(5));
            if (skill(6) && input.target_player) return waited(cast(6));
            return waited({ MonAct::none });
        case 9: return waited(cast(2, baal_aim));                                         // Tentacles (0x13b, S2)
        case 10: { Think out{ MonAct::a2 }; out.unit = baal_aim; return waited(out); }
        case 11: return waited(cast(0, baal_aim));                                        // Nova (0x13c, S3)
        case 12: return waited(cast(1, baal_aim));                                        // Inferno (0x13d, S1)
        case 13: return waited(cast(3, baal_aim));                                        // Cold Missiles (0x13e, A1)
        case 14: {                                                                  // Teleport 25 off, away from the target
            if (!input.target) break;
            const int span = std::max(input.dist, 1);
            auto out = cast(4, ThinkUnit::none);
            out.x = (input.self_x - input.target_x) * 25 / span + input.self_x;
            out.y = (input.self_y - input.target_y) * 25 / span + input.self_y;
            return waited(out);
        }
        case 15: effect(ThinkEffect::clone); break;                                 // FUN_005fc860: a Vile Effigy
        default: break;
        }
        return waited({ MonAct::idle, 5 });
    }
    // TrappedSoul (99, FUN_005e9f10; the souls InitFn 46 makes): flag
    // 0x20000; no target or past 4: burning (S1) once risen, else stands 15.
    // The first think rises (S2), its timer (state2) the frame. In melee past
    // the timer: the target left of it (or level) and below (or level), A1;
    // not left of it and above (or level), A2 (timer 35 frames on); else
    // burns (timer 5 on).
    Think trapped_soul() {
        if (!input.target || input.dist > 4) return *input.state != 0 ? Think{ MonAct::s1 } : Think{ MonAct::idle, 15 };
        if (*input.state == 0) { *input.state = 1; *input.state2 = input.frame; return { MonAct::s2 }; }
        if (!input.in_melee || input.frame <= *input.state2) return { MonAct::s1 };
        const unsigned self_x = unsigned(input.self_x), self_y = unsigned(input.self_y), target_x = unsigned(input.target_x), target_y = unsigned(input.target_y);
        if (target_x <= self_x && self_y <= target_y) { *input.state2 = input.frame + 35; return { MonAct::a1 }; }
        if (!(target_x < self_x) && target_y <= self_y) { *input.state2 = input.frame + 35; return { MonAct::a2 }; }
        *input.state2 = input.frame + 5;
        return { MonAct::s1 };
    }
    // Izual (55, FUN_005f89b0): its first think greets (a4q1, FUN_005b4390);
    // a stand due (state2) first. In melee: swings left (state3) or aip1 %
    // A1; else Frost Nova (Skill1, at the target's spot) aip4 %, then a stand
    // of aip5 and aip6 swings; else stands aidel. Out of it within 10 the nova
    // aip3 % (none while swings are left); aip2 % (or swings left) walks in;
    // else within 10 stands aidel, past it keeps 6..9 off.
    Think izual() {
        if (*input.state == 0) { *input.state = 1; effect(ThinkEffect::greet); }
        if (*input.state2 != 0) { const Think out{ MonAct::idle, *input.state2 }; *input.state2 = 0; return out; }
        auto nova = [&] { auto out = cast(0); out.spot = true; *input.state2 = aip[4]; *input.state3 = aip[5]; return out; };
        if (input.in_melee) {
            if (*input.state3 > 0 || roll() < aip[0]) { if (*input.state3 > 0) --*input.state3; return { MonAct::a1 }; }
            *input.state3 = 0;
            if (!input.skill[0] || roll() >= aip[3]) return { MonAct::idle, input.aidel };
            return nova();
        }
        if (input.skill[0] && input.dist < 10) {
            if (*input.state3 > 0) return walk;
            if (roll() < aip[2]) return nova();
        }
        if (*input.state3 < 1 && roll() >= aip[1]) return input.dist < 11 ? Think{ MonAct::idle, input.aidel } : Think{ MonAct::keep, 6, 9, 2 };
        return walk;
    }
    // UberIzual (144, FUN_005f8c80): Izual's without the greeting, after two
    // of its own: Skill2 (Chilling Armor) on itself while its state (skill
    // +0x80) is off; Skill3 (Teleport) to the target's spot when the line to
    // it is blocked (FUN_00622aa0 mask 6, `line_clear`).
    Think uber_izual() {
        if (input.skill[1] && !input.skill_state) return cast(1, ThinkUnit::none);
        if (!input.line_clear) { auto out = cast(2, ThinkUnit::none); out.spot = true; return out; }
        if (*input.state2 != 0) { const Think out{ MonAct::idle, *input.state2 }; *input.state2 = 0; return out; }
        auto nova = [&] { auto out = cast(0); out.spot = true; *input.state2 = aip[4]; *input.state3 = aip[5]; return out; };
        if (input.in_melee) {
            if (*input.state3 > 0 || roll() < aip[0]) { if (*input.state3 > 0) --*input.state3; return { MonAct::a1 }; }
            *input.state3 = 0;
            if (!input.skill[0] || roll() >= aip[3]) return { MonAct::idle, input.aidel };
            return nova();
        }
        if (input.skill[0] && input.dist < 10) {
            if (*input.state3 > 0) return walk;
            if (roll() < aip[2]) return nova();
        }
        if (*input.state3 < 1 && roll() >= aip[1]) return input.dist < 11 ? Think{ MonAct::idle, input.aidel } : Think{ MonAct::keep, 6, 9, 2 };
        return walk;
    }
    // UberMephisto / UberDiablo / UberBaal (146 / 147 / 145): a bare `ret 4`
    // in 1.14d (bugs.md #26), so they don't act after the search.
    Think ubers() {
        return { MonAct::none };
    }
};

inline constexpr std::array<ThinkRow, 17> kAct4Thinks{ {
    ThinkRow{ "VileMother", &think_as<Act4Thinks, &Act4Thinks::vile_mother> },
    ThinkRow{ "VileDog", &think_as<Act4Thinks, &Act4Thinks::vile_dog> },
    ThinkRow{ "DoomKnight", &think_as<Act4Thinks, &Act4Thinks::doom_knight> },
    ThinkRow{ "AbyssKnight", &think_as<Act4Thinks, &Act4Thinks::abyss_knight> },
    ThinkRow{ "OblivionKnight", &think_as<Act4Thinks, &Act4Thinks::oblivion_knight> },
    ThinkRow{ "Megademon", &think_as<Act4Thinks, &Act4Thinks::megademon> },
    ThinkRow{ "FingerMage", &think_as<Act4Thinks, &Act4Thinks::finger_mage> },
    ThinkRow{ "Regurgitator", &think_as<Act4Thinks, &Act4Thinks::regurgitator> },
    ThinkRow{ "Diablo", &think_as<Act4Thinks, &Act4Thinks::diablo> },
    ThinkRow{ "BaalCrab", &think_as<Act4Thinks, &Act4Thinks::baal_crab> },
    ThinkRow{ "BaalCrabClone", &think_as<Act4Thinks, &Act4Thinks::baal_crab> },
    ThinkRow{ "TrappedSoul", &think_as<Act4Thinks, &Act4Thinks::trapped_soul> },
    ThinkRow{ "Izual", &think_as<Act4Thinks, &Act4Thinks::izual> },
    ThinkRow{ "UberIzual", &think_as<Act4Thinks, &Act4Thinks::uber_izual> },
    ThinkRow{ "UberMephisto", &think_as<Act4Thinks, &Act4Thinks::ubers> },
    ThinkRow{ "UberDiablo", &think_as<Act4Thinks, &Act4Thinks::ubers> },
    ThinkRow{ "UberBaal", &think_as<Act4Thinks, &Act4Thinks::ubers> },
} };

}  // namespace d2d::rules
