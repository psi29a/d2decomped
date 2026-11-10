// SPDX-License-Identifier: GPL-3.0-or-later
// Act 3's MonAI thinks (docs/research/re/monster-ai.md "Act 3's thinks").
#pragma once

#include "thinker.hpp"

#include <array>

namespace d2d::rules {

struct Act3Thinks : Thinker {
    // Mosquito (24, FUN_005f3730): in melee, aip3 % (or before its first
    // swing, state2 0) it swings: aip4 % Skill1, else A1; else 80 % stands
    // 15, 20 % flies off (state 2). Off: walks in (state 0); state 1 drifts
    // (wander 4) for aip5 thinks; state 2 backs off 10, then drifts.
    Think mosquito() {
        if (input.in_melee) {
            if (roll() < aip[2] || *input.state2 == 0) {
                ++*input.state2;
                if (input.skill[0] && roll() < aip[3]) return cast(0);
                return { MonAct::a1 };
            }
            if (roll() > 20) return { MonAct::idle, 15 };
            *input.state = 2; *input.state2 = 0;
        }
        switch (*input.state) {
        case 0: set_pace(100, 0); return walk0;
        case 1: {
            set_pace(50, 0);
            const auto out = think_wander(rng, 4);
            if (++*input.state2 > aip[4]) *input.state = *input.state2 = 0;
            return out;
        }
        case 2:
            set_pace(100, 0);
            away(10, false);
            *input.state = 1; *input.state2 = 0;
            return { MonAct::none };
        default:
            *input.state = *input.state2 = 0;
            return { MonAct::idle, 10 };
        }
    }
    // ThornHulk (27, FUN_005f4850): out of melee it runs in. In melee, its
    // frenzy (Skill1) on (state: thinks left) it keeps casting; else aip1
    // % it fights: with no cooldown (state2) aip4 % frenzies (state aip6),
    // else aip2 % A1, else A2; else aip3 % circles 4, else stands 15. A
    // cast waits aip5 past the cast's end (its mode-end event, if one's due).
    Think thorn_hulk() {
        if (!input.in_melee) { *input.state = 0; return { MonAct::run, 7 }; }
        auto frenzy = [&] {
            auto out = cast(0);
            if (input.frame < input.next_event) out.wait = aip[4] - input.frame + input.next_event;
            return out;
        };
        if (*input.state < 1) {
            if (roll() >= aip[0]) {
                if (roll() >= aip[2]) return { MonAct::idle, 15 };
                bool set_off = false;
                return circle(4, 0, set_off);
            }
            if (*input.state2 < 1 && roll() < aip[3]) { *input.state = aip[5]; return frenzy(); }
            --*input.state2;
            return { roll() >= aip[1] ? MonAct::a1 : MonAct::a2 };
        }
        const auto out = frenzy();
        if (--*input.state < 1) *input.state2 = 3;
        return out;
    }
    // Tentacle (56, FUN_005f8f80) and TentacleHead (57, FUN_005f9270): the
    // head's tentacles die with it (no group leader), or 40 % once it's
    // dying (DD). Skill1 (rise) first, then on timers (state2: frames,
    // state3: 1 up, 2 under): up, Skill2 (submerge, at itself) after aip4 ×
    // 25 frames when the target's in melee or within aip6 (a tentacle 5 %
    // anyway, its head not in S4); under, Skill1 again after aip3 × 25
    // past aip6 off, or out of melee aip2 % (a tentacle: 50 % with its head
    // in S4; the head waits 20, not 8). Up, not due: wait aip5. Else in
    // melee aip1 % A1 (the head: at its second target), else stands aip5.
    Think tentacle() {
        const bool head = ai_name == "TentacleHead";
        const auto target = input.target ? ThinkUnit::target : ThinkUnit::none;
        if (!head && !input.grouped) return { MonAct::die };
        if (!head && input.leader_mode == 12 && roll() < 40) return { MonAct::die };
        auto rise = [&](int wait) {
            auto out = cast(0, target); out.wait = wait;
            *input.state2 = aip[2] * 25 + input.frame; *input.state3 = 1;
            return out;
        };
        if (input.skill[0]) {
            if (*input.state3 == 0) return rise(8);
            if (*input.state3 == 2 && *input.state2 < input.frame
                && (aip[5] < input.dist || (!input.in_melee && roll() < aip[1]) || (!head && input.leader_mode == 14 && roll() < 50)))
                return rise(head ? 20 : 8);
        }
        if (input.skill[1] && *input.state3 == 1) {
            if (*input.state2 < input.frame && (input.in_melee || input.dist < aip[5] || (!head && input.leader_mode != 14 && roll() < 5))) {
                *input.state2 = aip[3] * 25 + input.frame; *input.state3 = 2;
                return cast(1, ThinkUnit::self);
            }
        }
        if (*input.state3 == 1) return { MonAct::none, 0, 0, 0, ThinkUnit::target, aip[4] };
        if (head) {
            const auto second = input.second ? ThinkUnit::second : ThinkUnit::none;
            return roll() >= aip[0] ? Think{ MonAct::idle, aip[4] } : aimed_at({ MonAct::a1 }, second);
        }
        if (input.in_melee && roll() < aip[0]) return aimed_at({ MonAct::a1 }, target);
        return { MonAct::idle, aip[4] };
    }
    // FetishBlowgun (96, FUN_005e1250): its group's command first (1: A1 at
    // the unit, 14: a walk to it, at 50 %), done either way. Past aip1 out
    // of melee it closes about the target (6, at 50 %); in melee or under 6
    // off, aip2 % it breaks off (state 2). State 0: shoots its second
    // target (none: circles 4) for rand(3) + 3 thinks, then state 1: circles
    // 6 (none: stands 10). State 2: under 13 off it backs off 14 (can't: A1
    // at the second target); else 20 % circles 4, or stands 10.
    Think fetish_blowgun() {
        if (*input.command) {
            if ((*input.command == 1 || *input.command == 14) && input.command_unit) {
                *input.state = *input.state2 = 0;
                effect(ThinkEffect::command_done);
                if (*input.command == 1) return aimed_at({ MonAct::a1 }, ThinkUnit::kin);
                set_pace(50, 0);
                return aimed_at(walk0, ThinkUnit::kin);
            }
            effect(ThinkEffect::command_done);
        }
        if (!input.in_melee && aip[0] < input.dist) {
            set_pace(50, 0);
            auto out = think_wander(rng, 6); out.act = MonAct::around;
            return out;
        }
        if ((input.in_melee || input.dist < 6) && roll() < aip[1]) *input.state = 2;
        const auto second = input.second ? ThinkUnit::second : ThinkUnit::none;
        bool set_off = false;
        switch (*input.state) {
        case 0: {
            ++*input.state2;
            if (rng(3) + 3 < *input.state2) { *input.state = 1; *input.state2 = 0; }
            if (input.second) return aimed_at({ MonAct::a1 }, ThinkUnit::second);
            return circle(4, 0, set_off);
        }
        case 1: {
            *input.state = *input.state2 = 0;
            set_pace(50, 0);
            const auto out = circle(6, 1, set_off);
            return set_off ? out : Think{ MonAct::idle, 10 };
        }
        case 2:
            if (input.dist < 13) {
                set_pace(50, 0);
                if (away(14, false)) return { MonAct::none };
                *input.state = *input.state2 = 0;
                return aimed_at({ MonAct::a1 }, second);
            }
            if (roll() <= 19) {
                const auto out = circle(4, 1, set_off);
                if (set_off) return out;
            }
            *input.state = *input.state2 = 0;
            return { MonAct::idle, 10 };
        default: return { MonAct::idle, 10 };
        }
    }
    // FetishShaman (65, FUN_005f9a80): Inferno (Skill1) when the target's
    // AI distance is under Skill1's level (FUN_006442a0, at least 1) and
    // its state (0xc) is off, its group told to attack (command 1); else
    // that state off. A dead fetish within aip5 (FUN_005f99a0, squared;
    // the nearest): aip1 % with Skill3 usable, the group sent there
    // (command 14) and Skill3's sequence on it when its squared distance is
    // at most aip3 (game.exe compares it unsquared), else about it 10.
    // Else aip4 % circles 4, else stands 10.
    Think fetish_shaman() {
        const int reach = input.skill[0] ? std::max(input.skill_level, 1) : 1;
        if (input.skill[0] && input.dist < reach && !input.state_c) { effect(ThinkEffect::rally, 1); return cast(0); }
        if (input.state_c) effect(ThinkEffect::state, 0xc, 0);
        if (input.corpse && input.kin_dist <= aip[4] * aip[4] && roll() < aip[0] && input.usable) {
            effect(ThinkEffect::rally, 14);
            if (input.kin_dist <= aip[2]) return aimed_at({ MonAct::seq, 2 }, ThinkUnit::kin);
            auto out = think_wander(rng, 10); out.act = MonAct::around; out.unit = ThinkUnit::kin;
            return out;
        }
        if (roll() >= aip[3]) return { MonAct::idle, 10 };
        bool set_off = false;
        return circle(4, 0, set_off);
    }
    // FrogDemon (52, FUN_005f8260; search type 5): state3 0 at first, 1
    // under, 2 up. First: past 12 off Skill1 (dive, wait 8) and under;
    // else up by Skill2 if it can land (FUN_005f2fc0), else stands 12 (up
    // if its spot is clear of 0xc01). Under: Skill2 (surface, at itself)
    // when it can land within aip8, or within 19 after 65 dives (state2);
    // else stays under (take-off, wait 24, state2 + 1). Up: no target waits
    // 32; out of melee, past aip6 aip4 % circles 3, else approaches 4;
    // within it aip5 % A2, else aip4 % circles 4, else waits aip7. In melee
    // aip2 % A2, else aip1 % A1, aip3 % circles 3, else waits aip7. Taking
    // off or landing waits 12 (the Vulture's).
    Think frog_demon() {
        int& phase = *input.state3;
        auto surface = [&](Think out) { out.act = MonAct::skill; out.n = 1; out.unit = ThinkUnit::self; phase = 2; return out; };
        auto waits = [](int frames) { Think out{ MonAct::none }; out.wait = frames; return out; };
        bool set_off = false;
        if (phase == 0) {
            if (input.skill[0] && input.dist > 12) {
                effect(ThinkEffect::take_off);
                phase = 1;
                auto out = cast(0, input.target ? ThinkUnit::target : ThinkUnit::none); out.wait = 12;
                return out;
            }
            if (input.skill[1] && input.can_land) { effect(ThinkEffect::land); return surface(waits(12)); }
            if (input.spot_clear) phase = 2;
            return { MonAct::idle, 12 };
        }
        if (phase == 1) {
            if (input.skill[1] && input.can_land && (input.dist < aip[7] || (input.dist <= 19 && *input.state2 >= 65))) {
                effect(ThinkEffect::land);
                return surface(waits(12));
            }
            effect(ThinkEffect::take_off);
            ++*input.state2; phase = 1;
            return waits(24);
        }
        if (!input.target) return waits(32);
        if (!input.in_melee) {
            if (aip[5] <= input.dist) return roll() >= aip[3] ? Think{ MonAct::approach, 4 } : circle(3, 0, set_off);
            if (roll() < aip[4]) return { MonAct::a2 };
            if (roll() < aip[3]) return circle(4, 0, set_off);
            return waits(aip[6]);
        }
        if (roll() < aip[1]) return { MonAct::a2 };
        if (roll() < aip[0]) return { MonAct::a1 };
        if (roll() < aip[2]) return circle(3, 0, set_off);
        return waits(aip[6]);
    }
    // ZakarumZealot (48, FUN_005f6e60): its pace Run / Velocity (0..120 %
    // on). One it spares runs off 8 (can't: wanders 6). Hit, the first time
    // (state2 0) under aip3 % life, it runs off 8 (can't: state2 5); then
    // on its spot's 0x40 (FUN_0064cb30) it circles 4 in melee, else walks
    // in. State2 counts down. Out of melee aip4 % runs in (mode RN), else
    // walks in. In melee, after a swing (state 1) aip1 % keeps at it; else
    // 80 % stands 10, 20 % circles 4. A swing: aip2 % A2, else A1.
    Think zakarum_zealot() {
        int pct = 0;
        if (input.velocity > 0) pct = std::min(std::max(input.run * 100 / input.velocity - 100, 0), 120);
        bool set_off = false;
        if (input.spared) {
            set_pace(pct, 0);
            if (away(8, true)) return { MonAct::none };
            return think_wander(rng, 6);
        }
        if (input.got_hit) {
            if (*input.state2 == 0 && input.life_pct < aip[2]) {
                *input.state = 0;
                set_pace(pct, 0);
                if (away(8, true)) return { MonAct::none };
                *input.state2 = 5;
            }
            if (!input.spot_clear) return input.in_melee ? circle(4, 0, set_off) : walk;
        }
        if (*input.state2 != 0) --*input.state2;
        if (!input.in_melee) {
            *input.state = 0;
            if (roll() >= aip[3]) return walk;
            set_pace(pct, 0);
            return { MonAct::run, 0 };
        }
        if (*input.state != 0 && roll() >= aip[0]) {
            *input.state = 0;
            if (roll() < 80) return { MonAct::idle, 10 };
            return circle(4, 0, set_off);
        }
        *input.state = 1;
        return { roll() < aip[1] ? MonAct::a2 : MonAct::a1 };
    }
    // ZakarumPriest (49, FUN_005f72d0): in melee, or hit, under 33 % life
    // with Skill3 (its teleport) off cooldown (state: frame, aip5 × 4 on)
    // to a point past the target (twice as far, four times in melee) if
    // usable; in melee then aip1 % A1. Skill1 (its heal) 25 % at the
    // weakest zakarum (MonStats 0xeb / 0xee) under 61 % within aip6
    // (FUN_005f7240, squared). With no line to the target, aip1 % its
    // Skill4 (off cooldown, state2: aip5 on) aip2 %; with one, aip4 % the
    // same, then Skill2 (state3: 20 on) aip3 %. Else 30 % circles 4, else
    // stands 20.
    Think zakarum_priest() {
        if (input.in_melee || input.got_hit) {
            if (skill_set(2) && input.life_pct < 33 && *input.state < input.frame) {
                *input.state = input.frame + aip[4] * 4;
                const int shift = input.in_melee ? 2 : 0;
                const int point_x = input.target_x + ((input.target_x - input.self_x) << shift);
                const int point_y = input.target_y + ((input.target_y - input.self_y) << shift);
                if (input.usable) return { MonAct::skill, 2, point_x, point_y, ThinkUnit::none };
            }
            if (input.in_melee && roll() < aip[0]) return { MonAct::a1 };
        }
        if (input.skill[0] && input.aid && input.kin_dist <= aip[5] * aip[5] && roll() < 25) return cast(0, ThinkUnit::aid);
        auto skill4 = [&] { *input.state2 = aip[4] + input.frame; return cast(3); };
        const bool ready4 = skill_set(3) && *input.state2 < input.frame;
        if (!input.line_clear) {
            if (roll() < aip[0] && ready4 && roll() < aip[1]) return skill4();
        } else if (roll() < aip[3]) {
            if (ready4 && roll() < aip[1]) return skill4();
            if (input.skill[1] && *input.state3 < input.frame && roll() < aip[2]) { *input.state3 = input.frame + 20; return cast(1); }
        }
        bool set_off = false;
        if (roll() < 30) return circle(4, 0, set_off);
        return { MonAct::idle, 20 };
    }
    // HighPriest (85, FUN_005e0490; the Council): state 0 picks. In melee,
    // aip1 % A1 (state 1), else backs off 6. Off cooldown (state2), aip2 %
    // Skill2 at the weakest ally (itself too) under 75 % life within 50 (FUN_005e0430;
    // aip3 on); within aip8 aip4 % Skill1 at the target and one of the four
    // corners 5 off it (FUN_00472210, 0x6e337c; 100 on). With an S1
    // missile (MissS1) aip5 % within its Range - 2: S1. Else 80 % circles 3
    // within aip8, approaches 6 past it. Else (state 1): out of melee, under
    // 6 aip7 % S1; aip6 % stands 10 (state 0); else 30 % wanders 12, 70 %
    // walks in. In melee aip7 % S1, else aip6 % backs off 6 (state 0), else
    // 90 % A1, 10 % stands 10.
    Think high_priest() {
        const Think shoot{ MonAct::s1 };
        bool set_off = false;
        if (*input.state == 0) {
            if (input.in_melee) {
                if (roll() >= aip[0]) { away(6, false); return { MonAct::none }; }
                *input.state = 1;
                return { MonAct::a1 };
            }
            if (input.skill[1] && *input.state2 < input.frame && roll() < aip[1] && input.aid && input.kin_dist <= 2500) {
                *input.state2 = aip[2] + input.frame;
                return cast(1, ThinkUnit::aid);
            }
            if (input.skill[0] && *input.state2 < input.frame && input.dist < aip[7] && roll() < aip[3]) {
                static constexpr std::array<std::pair<int, int>, 4> kCorners{ { { -5, -5 }, { 5, -5 }, { 5, 5 }, { -5, 5 } } };
                const auto [corner_x, corner_y] = kCorners[rng.next() & 3];
                *input.state2 = input.frame + 100;
                return { MonAct::skill, 0, input.target_x + corner_x, input.target_y + corner_y };
            }
            if (aip[4] > 0 && input.s1_range >= 0 && input.dist < input.s1_range - 2 && roll() < aip[4]) return shoot;
            if (roll() < 80) return input.dist <= aip[7] ? circle(3, 0, set_off) : Think{ MonAct::approach, 6 };
        }
        *input.state = 1;
        if (!input.in_melee) {
            if (input.dist < 6 && roll() < aip[6]) return shoot;
            if (roll() < aip[5]) { *input.state = 0; return { MonAct::idle, 10 }; }
            if (roll() > 69) return think_wander(rng, 12);
            return walk;
        }
        if (roll() < aip[6]) return shoot;
        if (roll() < aip[5]) { away(6, false); *input.state = 0; return { MonAct::none }; }
        if (roll() > 89) return { MonAct::idle, 10 };
        return { MonAct::a1 };
    }
    // Mephisto (50, FUN_005f78b0): hurt = (100 - life %) / 5. Picks a case
    // (state3 keeps it): past 20 off 4; one under way goes on (past 4: the
    // fallback); under 21 % life within 5, 40 %: 1; under hurt + 50 %: 2
    // (state rand(3) + 3 casts); else the fallback. In melee, past aip1 %:
    // 3, else 2. Fallback: 35 % stands 10; else within 6, 35 %, walks in (4
    // steps); else circles 4. 0: stands 5. 1: runs off 8 (can't: in melee
    // Skill3, else 2). 2: state counts its casts down; every other one
    // circles 3 (can't: wanders 12). Else a cast at his chooser's unit
    // (FUN_005f77c0: aip2 % the weakest within 32, else with a player
    // nearest aip3 % the weakest again (game.exe returns the wrong find,
    // bugs.md #21), else the target), by the skills in a row from Skill1 (n;
    // none: wanders 6): past Normal with no line to it or past 30 off
    // Skill6; under 15 off, past Normal, rand < 100 / n Skill5; under 2 ×
    // 100 / n Skill4, 3 × Skill2, else Skill1. Then under 50 - hurt % the
    // count restarts (state2 0). 3: under hurt + 80 %, under 80 - hurt % A1,
    // else Skill3; else circles 3 (can't: wanders 12). 4: about the target 6.
    // ponytail: about the target always sets off, so its fallback
    // (FUN_00540e60, keep 12 / 6) isn't taken.
    Think mephisto() {
        const int hurt = std::max((100 - input.life_pct) / 5, 0);
        int& phase = *input.state3;
        bool set_off = false;
        auto fallback = [&]() -> Think {
            if (roll() > 64) { phase = 0; return { MonAct::idle, 10 }; }
            const bool walks = roll() > 64;
            phase = 0;
            if (walks && input.dist < 6) { set_pace(0, 4); return walk; }
            return circle(4, 0, set_off);
        };
        int pick = 0;
        if (!input.in_melee) {
            if (input.dist > 20) pick = 4;
            else if (phase != 0) { if (unsigned(phase) > 4) return fallback(); pick = phase; }
            else if (input.life_pct < 21 && input.dist < 5 && roll() < 40) pick = 1;
            else if (roll() >= hurt + 50) return fallback();
            else { pick = 2; *input.state = int(rng.next() % 3) + 3; }
        } else if (roll() > aip[0]) pick = 3;
        else { pick = 2; *input.state = int(rng.next() % 3) + 3; }
        phase = pick;
        auto circle_or_wander = [&] { auto out = circle(3, 1, set_off); return set_off ? out : think_wander(rng, 12); };
        switch (pick) {
        case 0: phase = 4; return { MonAct::idle, 5 };
        case 1:
            phase = 0;
            set_pace(50, 0);
            if (away(8, false)) return { MonAct::none };
            if (input.in_melee) return cast(2);
            [[fallthrough]];
        case 2: {
            if (--*input.state == 0) phase = 0;
            if (*input.state2 == 0) { *input.state2 = 2; return circle_or_wander(); }
            ++*input.state2;
            auto chosen = ThinkUnit::target;
            if (input.weak && roll() < aip[1]) chosen = ThinkUnit::second;
            else if (input.near_player && roll() < aip[2]) chosen = input.weak ? ThinkUnit::second : ThinkUnit::none;
            int count = 0;
            for (int slot = 0; slot < 8 && (slot < 5 ? skill_set(slot) : input.skill678[std::size_t(slot - 5)]); ++slot) ++count;
            Think out;
            if (count <= 0) out = think_wander(rng, 6);
            else {
                const int share = 100 / count, rolled = roll();
                const bool blocked = input.difficulty > 0 && chosen != ThinkUnit::none && !input.line_clear;
                if (input.difficulty > 0 && (blocked || input.dist > 30)) out = cast(5, chosen);
                else if (input.dist < 15 && rolled < share && input.difficulty > 0) out = cast(4, chosen);
                else if (rolled < share * 2) out = cast(3, chosen);
                else if (rolled < share * 3) out = cast(1, chosen);
                else out = cast(0, chosen);
            }
            if (roll() < 50 - hurt) *input.state2 = 0;
            return out;
        }
        case 3:
            phase = 0;
            if (roll() < hurt + 80) return roll() < 80 - hurt ? Think{ MonAct::a1 } : cast(2);
            set_pace(50, 0);
            return circle_or_wander();
        default: {
            phase = 0;
            set_pace(50, 0);
            auto out = think_wander(rng, 6); out.act = MonAct::around;
            return out;
        }
        }
    }
    // WillOWisp (25, FUN_005f39b0). With a target past the cooldown (state3,
    // a frame) within 32 (squared 0x400) and state under 4, rand(1000) <=
    // difficulty + 2: state 4. 4: the wisps within 32 (itself too); four
    // dance (word 1 6) and five (5), each word 2 0 and word 3 its place + 1,
    // all standing to the next 337th frame; else state 2, stands 8. Dancing
    // (6..12 a step, 5 the fives' one): to the target + its step's point
    // (0x6e.. tables on the stack, by word 3 + state x 4) a walk (mode 2,
    // FUN_005de490), 34 % a wander 4 instead, while past 3 off; there, on
    // each 67th frame, up to 3 casts (SC, FUN_005ddfc0 mode 7) at the target
    // + the step's other point, then the next step (past 12, or the fives':
    // state 2, cooldown 1800 frames), standing to the next 337th frame; a
    // player it's at gets stat 0x50 (magic find) (difficulty + 1) x 50 for
    // 0x1a5e00 frames each step. 1: S1 (in melee state 3; else aip1 %,
    // state 2), else walks in (aip3 %) or wanders 6. Else: out of melee SC
    // (state 2 or aip1 %), in melee A1 (state 3 or aip2 %), state 0; else
    // aip3 % walks in, wanders 4 (state 1, state2 3).
    Think will_owisp() {
        static constexpr std::array<std::pair<int, int>, 53> kSteps{ { {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, { 0, 0 },
            { -10, -3 }, { 1, -5 }, { 6, 0 }, { 4, 10 }, { -9, -2 }, { -6, -11 }, { -2, -5 }, { 6, 1 }, { 0, 13 }, { -12, 0 }, { 7, 5 }, { -3, -11 }, { -15, -3 }, { -6, -3 },
            { -4, -5 }, { -6, -13 }, { -12, -2 }, { -6, 2 }, { -5, 5 }, { 1, 11 }, { -13, -6 }, { -8, -11 }, { -7, 0 }, { 1, 8 }, { -5, -8 }, { -8, 1 }, { 1, -6 }, { 6, 9 } } };
        static constexpr std::array<std::pair<int, int>, 53> kCasts{ { {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, { 6, 9 },
            { 1, -5 }, { 6, 0 }, { 4, 10 }, { -10, -3 }, { 1, 8 }, { -9, -2 }, { -5, 4 }, { 1, 8 }, { -12, 0 }, { 7, 5 }, { -3, -11 }, { 11, -2 }, { 7, 9 }, { -15, -3 },
            { -6, -13 }, { 9, 7 }, { -5, -7 }, { -5, -7 }, { 9, 0 }, { -12, -2 }, { -8, -11 }, { -2, -6 }, { -2, -6 }, { -13, -6 }, { -8, 1 }, { 1, -6 }, { 6, 9 }, { 0, 0 } } };
        static constexpr std::array<std::pair<int, int>, 6> kFiveSteps{ { { -5, 0 }, { -5, -5 }, { 3, -5 }, { 6, 3 }, { 3, 6 }, { -5, 3 } } };
        static constexpr std::array<std::pair<int, int>, 6> kFiveCasts{ { { 0, 0 }, { 6, 3 }, { 3, 6 }, { -5, 3 }, { -5, -5 }, { 3, -5 } } };
        int& state = *input.state;
        int& count = *input.state2;
        int& word3 = *input.state3;
        const int frame = input.frame;
        auto stand_to = [&](int period) { return Think{ MonAct::idle, period - frame % period }; };
        const int off_x = input.target_x - input.self_x, off_y = input.target_y - input.self_y;
        if (input.target && frame > word3 && off_x * off_x + off_y * off_y < 0x400 && state < 4 && rng(1000) <= input.difficulty + 2) state = 4;
        if (state == 4) {
            if (input.wisps == 4 || input.wisps == 5) {
                for (int found = 0; found < input.wisps; ++found) {
                    effect(ThinkEffect::set_word, found * 4 + 1, input.wisps == 4 ? 6 : 5);
                    effect(ThinkEffect::set_word, found * 4 + 2, 0);
                    effect(ThinkEffect::set_word, found * 4 + 3, found + 1);
                }
                return stand_to(337);
            }
            state = 2;
            return { MonAct::idle, 8 };
        }
        if (state >= 5) {
            const bool five = state == 5;
            if (word3 > (five ? 5 : 4)) { state = 2; return { MonAct::idle, 8 }; }
            const std::size_t step = five ? std::size_t(word3) : std::size_t(word3 + state * 4);
            if (step >= kSteps.size()) { state = 2; return { MonAct::idle, 8 }; }
            const auto [to_x, to_y] = five ? kFiveSteps[step] : kSteps[step];
            const int point_x = input.target_x + to_x, point_y = input.target_y + to_y;
            const int across = std::abs(input.self_x - point_x), down = std::abs(input.self_y - point_y);
            if ((down < across ? down + across * 2 : across + down * 2) / 2 > 3) {   // FUN_005dc5c0
                if (rng(100) < 34) return think_wander(rng, 4);
                return { MonAct::move_to, 2, point_x, point_y, ThinkUnit::none };
            }
            if (frame % 67 != 0) return stand_to(67);
            if (count < 3) {
                const auto [cast_x, cast_y] = five ? kFiveCasts[step] : kCasts[step];
                ++count;
                if (cast_x == 0 && cast_y == 0) return stand_to(five ? 337 : 67);
                return { MonAct::mode_at, 7, input.target_x + cast_x, input.target_y + cast_y, ThinkUnit::none };
            }
            if (five) { state = 2; word3 = frame + 1800; return stand_to(337); }
            ++state; count = 0;
            if (state - 6 > 6) { state = 2; word3 = frame + 1800; }
            if (input.bless_ok) effect(ThinkEffect::bless, 0x50, (input.difficulty + 1) * 50);
            return stand_to(337);
        }
        if (state == 1) {
            if (count <= 0) {
                if (input.in_melee) { state = 3; return { MonAct::s1, 0, 0, 0, ThinkUnit::none }; }
                if (roll() < aip[0]) { state = 2; return { MonAct::s1, 0, 0, 0, ThinkUnit::none }; }
            }
            --count;
            state = 1;
            return roll() < aip[2] ? walk0 : think_wander(rng, 6);
        }
        if (!input.in_melee) {
            if (state == 2 || roll() < aip[0]) { state = 0; return { MonAct::sc }; }
        } else if (state == 3 || roll() < aip[1]) { state = 0; return { MonAct::a1 }; }
        state = 1; count = 3;
        return roll() < aip[2] ? walk0 : think_wander(rng, 4);
    }
    // Hydra (86, FUN_005e9e60; Mephisto's): past its end frame (state) it's
    // gone (FUN_005ddfc0); a target within 25, 60 % Skill1; else stands 10.
    Think hydra() {
        if (*input.state < input.frame) return { MonAct::gone };
        if (input.target && input.dist < 25 && roll() < 60) return cast(0);
        return { MonAct::idle, 10 };
    }
};

inline constexpr auto kAct3Thinks = std::to_array<ThinkRow>({
    { "Mosquito", &think_as<Act3Thinks, &Act3Thinks::mosquito> },
    { "ThornHulk", &think_as<Act3Thinks, &Act3Thinks::thorn_hulk> },
    { "Tentacle", &think_as<Act3Thinks, &Act3Thinks::tentacle> },
    { "TentacleHead", &think_as<Act3Thinks, &Act3Thinks::tentacle> },
    { "FetishBlowgun", &think_as<Act3Thinks, &Act3Thinks::fetish_blowgun> },
    { "FetishShaman", &think_as<Act3Thinks, &Act3Thinks::fetish_shaman> },
    { "FrogDemon", &think_as<Act3Thinks, &Act3Thinks::frog_demon> },
    { "ZakarumZealot", &think_as<Act3Thinks, &Act3Thinks::zakarum_zealot> },
    { "ZakarumPriest", &think_as<Act3Thinks, &Act3Thinks::zakarum_priest> },
    { "HighPriest", &think_as<Act3Thinks, &Act3Thinks::high_priest> },
    { "Mephisto", &think_as<Act3Thinks, &Act3Thinks::mephisto> },
    { "WillOWisp", &think_as<Act3Thinks, &Act3Thinks::will_owisp> },
    { "Hydra", &think_as<Act3Thinks, &Act3Thinks::hydra> },
});

}  // namespace d2d::rules
