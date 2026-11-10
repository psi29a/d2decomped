// SPDX-License-Identifier: GPL-3.0-or-later
// Act 2's MonAI thinks (docs/research/re/monster-ai.md "Act 2's thinks";
// tools/emu/mon_ais.py).
#pragma once

#include "monsters.hpp"
#include "thinker.hpp"

#include <algorithm>
#include <array>
#include <tuple>

namespace d2d::rules {

struct Act2Thinks : Thinker {
    // Duriel (44, FUN_005f67b0): his aura (Skill4) on if it's off; out of
    // melee aip5 % Skill1 (his charge), else walks in; in melee aip2 %
    // Skill3, aip3 % Skill2, else aip4 % A2, else A1.
    Think duriel() {
        if (skill_set(3) && !input.aura_on) effect(ThinkEffect::aura, 3);
        if (!input.in_melee) {
            if (input.skill[0] && roll() < aip[4]) return cast(0);
            return walk;
        }
        if (input.skill[2] && roll() < aip[1]) return cast(2);
        if (input.skill[1] && roll() < aip[2]) return cast(1);
        return roll() < aip[3] ? Think{ MonAct::a2 } : Think{ MonAct::a1 };
    }
    // Swarm (19, FUN_005f2460): out of melee aip1 % walks in; in melee aip3
    // % A1; else stands aip2.
    Think swarm() {
        if (!input.in_melee) { if (roll() < aip[0]) return walk; }
        else if (roll() < aip[2]) return { MonAct::a1 };
        return idle2;
    }
    // PinHead (39, FUN_005f6340): out of melee (state 0) aip3 % walks in,
    // else stands aip4; in melee, after its first swing (state 1) aip1 %
    // goes on, else stands aip2; then aip5 % Skill1, aip6 % Skill2, else A1.
    Think pin_head() {
        if (!input.in_melee) {
            *input.state = 0;
            return roll() >= aip[2] ? Think{ MonAct::idle, aip[3] } : walk;
        }
        if (*input.state != 0 && roll() >= aip[0]) return idle2;
        *input.state = 1;
        if (input.skill[0] && roll() < aip[4]) return cast(0);
        if (input.skill[1] && roll() < aip[5]) return cast(1);
        return { MonAct::a1 };
    }
    // Mummy (21, FUN_005f2850): hit out of melee it walks in; within aip1,
    // in melee aip3 % swings (aip4 % A1), else walks in (the stand aip5 it
    // asks first is overtaken); past aip1 aip2 % wanders 3, else stands aip5.
    Think mummy() {
        if (input.got_hit && !input.in_melee) return walk;
        if (input.dist <= aip[0]) {
            if (input.in_melee && roll() < aip[2]) return a1_or_a2(aip[3]);
            return walk;
        }
        if (roll() < aip[1]) return think_wander(rng, 3);
        return { MonAct::idle, aip[4] };
    }
    // SandLeaper (17, FUN_005f20a0): within 5, aip1 % its leap (Skill1) if
    // it can; out of melee past 10 it bounds about the target 5 (pace 75),
    // else aip3 % walks in, aip4 % circles 4; in melee aip2 % A2; else
    // stands 10.
    Think sand_leaper() {
        if (input.skill[0] && input.dist < 5 && roll() < aip[0] && input.usable) return cast(0);
        if (!input.in_melee) {
            if (input.dist > 10) { set_pace(75, 0); auto out = think_wander(rng, 5); out.act = MonAct::around; return out; }
            if (roll() < aip[2]) return walk;
            if (roll() < aip[3]) return think_circle(rng, 4);
        } else if (roll() < aip[1]) return { MonAct::a2 };
        return { MonAct::idle, 10 };
    }
    // ClawViper (16, FUN_005f1b60): after its charge (state) its aip6 state
    // goes off (2: 0x5b, else 0x5a); out of melee within aip2, aip1 % the
    // charge (Skill1) if it can, its state on; else 50 % walks in, else
    // stands aip5; in melee aip3 % swings (aip4 % A1), else stands aip5.
    Think claw_viper() {
        const int state_id = aip[5] == 2 ? 0x5b : 0x5a;
        if (*input.state != 0 && aip[5] != 0) effect(ThinkEffect::state, state_id, 0);
        const Think stand{ MonAct::idle, aip[4] };
        if (!input.in_melee) {
            if (input.skill[0] && input.dist < aip[1] && roll() < aip[0] && input.usable) {
                if (aip[5] != 0) effect(ThinkEffect::state, state_id, 1);
                *input.state = 1;
                return cast(0);
            }
            return roll() < 50 ? walk : stand;
        }
        if (roll() >= aip[2]) return stand;
        return a1_or_a2(aip[3]);
    }
    // PantherWoman (18, FUN_005f22b0): in melee aip2 % A1, else stands
    // aip4; aip1 % runs in (pace 75); its nearest kin past aip3 it walks to
    // (pace 75); 25 % circles 3; else stands aip4.
    Think panther_woman() {
        const Think stand{ MonAct::idle, aip[3] };
        if (input.in_melee) return roll() < aip[1] ? Think{ MonAct::a1 } : stand;
        if (roll() < aip[0]) { set_pace(75, 0); return { MonAct::run, 7 }; }
        if (input.kin && aip[2] * aip[2] < input.kin_dist) { set_pace(75, 0); return aimed(walk, ThinkUnit::kin); }
        if (roll() < 25) return think_circle(rng, 3);
        return stand;
    }
    // PantherJavelin (95, FUN_005e1080): at its second target's distance
    // (none: the most there is): under 8 aip4 % backs off 16; past aip6 - 6
    // aip1 % moves 4 about the target; with none or at aip6 or more it walks
    // to its kin past aip3, else stands aip5; else aip2 % throws (A1) at it.
    Think panther_javelin() {
        const int reach = input.second ? input.second_dist : 0x7fffffff;
        if (reach < 8 && roll() < aip[3]) { (void)away(16, false); return { MonAct::none }; }
        if (aip[5] - 6 < reach && roll() < aip[0]) { auto out = think_wander(rng, 4); out.act = MonAct::around; return out; }
        if (!input.second || aip[5] <= reach) {
            if (input.kin && aip[2] < input.kin_dist) return aimed(walk, ThinkUnit::kin);
        } else if (roll() < aip[1]) return aimed({ MonAct::a1 }, ThinkUnit::second);
        return { MonAct::idle, aip[4] };
    }
    // Scarab (20, FUN_005f2540): its leader within 19, itself leading, aip5
    // % sends the group charging (and charges). Charging (command 1): in
    // melee Skill1 (the command done), else walks in (pace 100). Else out of
    // melee once it's circled (state) it walks in (4 steps), 90 % done
    // circling; else circles 0. In melee aip1 % on: aip4 % Skill1, aip2 %
    // A1, else A2; else stands aip3.
    Think scarab() {
        int command = *input.command;
        if (command == 0 && input.dist <= 19 && input.leader && roll() < aip[4]) { *input.rally = true; effect(ThinkEffect::rally); command = 1; }
        if (command == 1) {
            if (input.in_melee && input.skill[0]) { effect(ThinkEffect::command_done); return cast(0); }
            set_pace(100, 0);
            return walk_in;
        }
        if (!input.in_melee) {
            if (*input.state != 0) {
                set_pace(0, 4);
                if (roll() > 10) *input.state = 0;
                return walk;
            }
            *input.state = 1;
            return think_circle(rng, 0);
        }
        if (roll() >= aip[0]) return { MonAct::idle, aip[2] };
        if (input.skill[0] && roll() < aip[3]) return cast(0);
        return roll() >= aip[1] ? Think{ MonAct::a2 } : Think{ MonAct::a1 };
    }
    // BatDemon (29, FUN_005f5040): state 0 / 4: it rises (S3, wait 8),
    // state 1, its regen up aip5 / 8 (state3); state 1: after two drops
    // (state2) it lands (S2, wait 6; state 3, the regen back) unless out of
    // melee past 6, not hit, past 13 or under 51 % life, when it drops
    // again (S4, wait 10 then 15); state 2 (fleeing): under aip1 % life it
    // backs off 15 (state 4), 33 % walks in (state 3), 15 % wanders 6, else
    // stands 10; state 3: the same back-off; out of melee walks in; dropped,
    // A2; hit aip2 % backs off 12 (state 2); aip3 %: aip4 % A2, else A1;
    // else stands 10; other states within 15 walk in (state 3), else state 4
    // and stand 15.
    // Its other think: other_think.
    Think bat_demon() {
        const int life = input.life_pct;
        auto mode = [&](MonAct act, int wait) { Think out{ act }; out.unit = ThinkUnit::none; out.wait = wait; return out; };
        if (*input.state == 0 || *input.state == 4) {
            *input.state = 1; *input.state2 = 0;
            if (input.regen != 0) {
                *input.state3 = aip[4] * input.regen / 8;
                effect(ThinkEffect::stat, 0x4a, *input.state3 + input.regen);
            } else *input.state3 = 0;
            return mode(MonAct::s3, 8);
        }
        if (*input.state == 1) {
            if (*input.state2 > 1 && !(!input.in_melee && input.dist > 6 && !input.got_hit && (input.dist > 13 || life < 51))) {
                if (*input.state3 != 0) effect(ThinkEffect::stat, 0x4a, input.regen - *input.state3);
                *input.state = 3;
                return mode(MonAct::s2, 6);
            }
            const int wait = *input.state2 == 0 ? 10 : 15;
            ++*input.state2;
            return mode(MonAct::s4, wait);
        }
        if (*input.state == 2) {
            if (life < aip[0] && away(15, false)) { *input.state = 4; return { MonAct::none }; }
            if (roll() < 33) { *input.state = 3; return walk_in; }
            if (roll() < 15) return think_wander(rng, 6);
            return { MonAct::idle, 10 };
        }
        if (*input.state == 3) {
            if (life < aip[0] && away(15, false)) { *input.state = 4; return { MonAct::none }; }
            if (!input.in_melee) return walk_in;
            if (*input.state2 > 0) { *input.state2 = 0; return { MonAct::a2 }; }
            if (input.got_hit && roll() < aip[1] && away(12, false)) { *input.state = 2; return { MonAct::none }; }
            if (roll() < aip[2]) return roll() < aip[3] ? Think{ MonAct::a2 } : Think{ MonAct::a1 };
            return { MonAct::idle, 10 };
        }
        if (input.dist < 15) { *input.state = 3; return walk_in; }
        *input.state = 4;
        return { MonAct::idle, 15 };
    }
    // Baboon (11, FUN_005f0cd0): its pace Run / Velocity (0..120 % on). At
    // rest (state 0): out of melee it runs in; hit under aip1 % life, 50 %
    // it flees rand(5) + 2 thinks (state), its regen up aip5 / 8 (state3),
    // backing off 15; hit after a swing (state2) 20 % circles 3; after a
    // swing aip3 % on, else aip2 % circles 3 and stands 15; else aip4 % A1,
    // else A2 (state2 1). Fleeing: a think off (state2 0); done or over 75 %
    // life its regen back; out of melee over 75 % it's done and runs in; in
    // melee 33 % swings (aip4 % A1); past 23 not hit 33 % circles 4 and it
    // stands 20; else backs off 15, or out of melee wanders 5, in it swings.
    Think baboon() {
        const int life = input.life_pct;
        int pct = input.velocity < 1 ? 0 : input.run * 100 / input.velocity;
        pct = pct < 100 ? 0 : std::min(pct - 100, 120);
        auto swing = [&] { return roll() < aip[3] ? Think{ MonAct::a1 } : Think{ MonAct::a2 }; };
        if (*input.state == 0) {
            if (!input.in_melee) return { MonAct::run, 7 };
            if (input.got_hit) {
                if (life < aip[0] && roll() < 50) {
                    *input.state = rng(5) + 2;
                    if (input.regen != 0) {
                        *input.state3 = aip[4] * input.regen / 8;
                        effect(ThinkEffect::stat, 0x4a, *input.state3 + input.regen);
                    } else *input.state3 = 0;
                    set_pace(pct, 0);
                    (void)away(15, false);
                    return { MonAct::none };
                }
                if (*input.state2 != 0 && roll() < 20) { *input.state2 = 0; return think_circle(rng, 3); }
            }
            if (*input.state2 != 0 && roll() >= aip[2]) {
                if (roll() < aip[1]) { (void)think_circle(rng, 3); *input.state2 = 0; }
                return { MonAct::idle, 15 };
            }
            *input.state2 = 1;
            return swing();
        }
        --*input.state;
        *input.state2 = 0;
        if (*input.state == 0 || life > 75) effect(ThinkEffect::stat, 0x4a, input.regen - *input.state3);
        if (!input.in_melee) {
            if (life > 75) { *input.state = 0; set_pace(pct, 0); return { MonAct::run, 7 }; }
        } else if (roll() < 33) return swing();
        if (input.dist > 23 && !input.got_hit) {
            if (roll() < 33) (void)think_circle(rng, 4);
            return { MonAct::idle, 20 };
        }
        set_pace(pct, 0);
        if (away(15, false)) return { MonAct::none };
        if (!input.in_melee) return think_wander(rng, 5);
        return swing();
    }
    // SandRaider (8, FUN_005f0700): its thinks count up (state; at 0 its
    // states 0x5a / 0x5b off, state2 0); at aip5 it shows its overlay
    // (aip6 1: 0x96, else 0x2e) and stands aidel + 1; past it, its state on
    // (aip6 1: 0x5a, else 0x5b; state2 1). Under aip1 % life, seven times
    // (state3), it walks to its nearest other monster. Out of melee past 4,
    // not yet on, aip2 % circles 0; out of melee it walks in (not on: aip4
    // %); in melee, on, Skill1 (the count starts over); aip3 % swings (aip7
    // or more A1, else A2); else stands 15, the count over past aip5 +
    // max(24 - aip5, 6).
    Think sand_raider() {
        if (*input.state == 0) { effect(ThinkEffect::state, 0x5a, 0); effect(ThinkEffect::state, 0x5b, 0); *input.state2 = 0; }
        ++*input.state;
        if (*input.state == aip[4]) {
            effect(ThinkEffect::overlay, aip[5] == 1 ? 0x96 : 0x2e);
            return { MonAct::idle, input.aidel + 1 };
        }
        if (aip[4] < *input.state) { effect(ThinkEffect::state, aip[5] == 1 ? 0x5a : 0x5b, 1); *input.state2 = 1; }
        if (*input.state3 < 7 && input.life_pct < aip[0]) {
            if (input.kin) return aimed(walk_in, ThinkUnit::kin);
            ++*input.state3;
        }
        auto rest = [&] {
            const int count = std::max(24 - aip[4], 6);
            if (aip[4] + count < *input.state) *input.state = *input.state2 = 0;
            return Think{ MonAct::idle, 15 };
        };
        if (input.dist > 4 && *input.state2 == 0 && roll() < aip[1]) return think_circle(rng, 0);
        if (!input.in_melee) {
            if (*input.state2 == 0 && roll() >= aip[3]) return rest();
            return walk_in;
        }
        if (*input.state2 == 1 && input.skill[0]) { *input.state = *input.state2 = 0; return cast(0); }
        if (roll() < aip[2]) return rng(100) >= aip[6] ? Think{ MonAct::a1 } : Think{ MonAct::a2 };
        return rest();
    }
    // GreaterMummy (22, FUN_005f2b10): in melee aip1 % A1; within 5 aip1 %
    // A2; aip3 % heals a hurt one in reach (Skill2); aip2 % raises a corpse
    // (Skill1's sequence) if it can; aip4 % Skill3 at its second target;
    // with none in reach it closes in (pace 50, 3); else 50 % circles 3,
    // else stands 6.
    // Radament's wider reach (aip5 + 10) and the corpse / hurt pick are
    // the search's (game/ai/act2.cpp).
    Think greater_mummy() {
        if (input.in_melee && roll() < aip[0]) return { MonAct::a1 };
        if (input.dist < 5 && roll() < aip[0]) return { MonAct::a2 };
        if (input.skill[1] && input.aid && roll() < aip[2]) return cast(1, ThinkUnit::aid);
        if (input.corpse && roll() < aip[1] && input.usable) return aimed({ MonAct::seq, 0 }, ThinkUnit::kin);
        if (input.skill[2] && roll() < aip[3] && input.second) return cast(2, ThinkUnit::second);
        if (input.in_reach < 1) { set_pace(50, 0); return { MonAct::approach, 3 }; }
        if (roll() > 49) return { MonAct::idle, 6 };
        return think_circle(rng, 3);
    }
    // SandMaggot (15, search type 4; FUN_005f1800): state 3 (buried, its
    // skill ready: state2 the frame) it lays (Skill1, event 25; ready aip5
    // on; state 1), else waits 20. Under 25 % life with its second target
    // within 7, ready, 20 % it burrows (Skill2, event 30; state 3). In
    // melee aip4 % A1; its second target within 15 aip2 % A2 at it; 20 %
    // circles 6; under aip3 eggs laid (state3) aip1 %: not circled (state
    // 2) it circles 6 (state 2), else Skill3 (an egg, event 20; state 1);
    // else waits 12.
    // Its other think: other_think.
    Think sand_maggot() {
        const bool ready = *input.state2 < input.frame;
        const int second_dist = input.second ? input.second_dist : 0x7fffffff;
        auto with_event = [](Think out, int frames) { out.event = frames; return out; };
        if (*input.state == 3) {
            if (ready && input.skill[0]) { *input.state2 = aip[4] + input.frame; *input.state = 1; return with_event(cast(0), 25); }
            return with_event({ MonAct::none }, 20);
        }
        if (input.life_pct < 25 && input.skill[1] && second_dist < 7 && ready && rng(100) < 20) {
            *input.state2 = aip[4] + input.frame; *input.state = 3;
            return with_event(cast(1), 30);
        }
        if (input.in_melee && roll() < aip[3]) return { MonAct::a1 };
        if (input.second && second_dist < 15 && rng(100) < aip[1]) return aimed({ MonAct::a2 }, ThinkUnit::second);
        if (roll() < 20) return think_circle(rng, 6);
        if (*input.state3 < aip[2] && rng(100) < aip[0]) {
            if (*input.state != 2 || !input.skill[2]) { *input.state = 2; return think_circle(rng, 6); }
            ++*input.state3; *input.state = 1;
            return with_event(cast(2), 20);
        }
        return with_event({ MonAct::none }, 12);
    }
    // Summoner (53, FUN_005f85c0): its first think opens his quest (state
    // 1). Within 5 aip6 % he backs off 6. aip1 % he casts, else wanders 4:
    // aip2 % Skill5; the element the target resists less (cold when its
    // cold resist is no more than its fire), turned about past aip3 %:
    // Skill2 (aip7 reach, ready at state2: aip4 frames on) or Skill1 at his
    // second target (aip8 reach); Skill4 every aip5 frames (state3); Skill3
    // at the second (aip8); the other element's Skill2; else Skill5. Each
    // at the target's spot.
    Think summoner() {
        if (*input.state == 0) { effect(ThinkEffect::summoner_open); *input.state = 1; }
        if (input.dist < 5 && roll() < aip[5]) (void)away(6, false);
        bool cold = input.target_cold_res <= input.target_fire_res;
        auto spot = [&](int slot, ThinkUnit unit = ThinkUnit::target) { auto out = cast(slot, unit); out.spot = true; return out; };
        if (roll() < aip[0]) {
            if (skill_set(4) && roll() < aip[1]) return spot(4);
            if (aip[2] < roll()) cold = !cold;
            auto bolt = [&] { *input.state2 = aip[3] + input.frame; return spot(1); };
            const bool bolt_ready = input.skill[1] && *input.state2 < input.frame && input.dist < aip[6];
            if (cold) {
                if (bolt_ready) return bolt();
                if (input.skill[0] && input.second && input.dist < aip[7]) return spot(0, ThinkUnit::second);
            }
            if (skill_set(3) && *input.state3 < input.frame) { *input.state3 = aip[4] + input.frame; return spot(3); }
            if (input.skill[2] && input.second && input.dist < aip[7]) return spot(2, ThinkUnit::second);
            if (!cold && bolt_ready) return bolt();
            if (skill_set(4)) return spot(4);
        }
        return think_wander(rng, 4);
    }
    // Vulture (23, FUN_005f3170; state: its thinks aloft, -1 landed). Far
    // (over 12) in another room: down, it keeps 9 off (2 a go); aloft it
    // lands (S2, 2 a go to 3 off; -1). Down, not in a group, far, 60 % it
    // takes off for rand(8) + 24 thinks (S1 8 to 8 off). Aloft (2 or more)
    // with no weak one about, past 5 or 85 %: it flies (S1) to a point
    // rand(2r) about the target (r = thinks + 8) pushed off itself until
    // it's clamp(2r, 12, 36) away, kept in state2 / 3 (then re-placed on a
    // free spot), a think less; else (or 1 left) it lands to 3 off (-1),
    // or can't (8 more) and fights on. In melee aip1 % A1, else waits
    // aip2; landed or aip5 %: aip4 % circles 6, else keeps 9 off; down.
    // Each waits 12.
    Think vulture() {
        auto wait = [](Think out, int frames) { out.wait = frames; return out; };
        auto keep = [&](ThinkUnit unit, int mode, int most, int want) { Think out{ MonAct::keep, most, want, mode }; out.unit = unit; return out; };
        auto land = [&] { if (!input.can_land) return false; effect(ThinkEffect::land); return true; };
        const int off_x = input.self_x - input.target_x, off_y = input.self_y - input.target_y;
        const bool distant = off_x * off_x + off_y * off_y > 0x90;
        int& aloft = *input.state;
        if (!input.same_room && distant) {
            if (aloft < 1) return keep(ThinkUnit::target, 2, 9, 0);
            if (land()) { aloft = -1; return wait(keep(ThinkUnit::self, 9, 2, 3), 12); }
        }
        if (aloft == 0 && !input.grouped && distant && rng(100) < 60) {
            effect(ThinkEffect::take_off);
            aloft = int(rng.next() & 7) + 24;
            return wait(keep(ThinkUnit::target, 8, 8, 8), 12);
        }
        Think fallback{ MonAct::none };
        bool landing = aloft == 1;
        if (aloft >= 2) {
            if (!input.victim && (input.dist > 5 || rng(100) > 14)) {
                effect(ThinkEffect::take_off);
                auto gap = [&](int x, int y) { return npc_distance(input.self_x, input.self_y, x, y); };
                int& spot_x = *input.state2;
                int& spot_y = *input.state3;
                if (spot_x == 0 || spot_y == 0 || gap(spot_x, spot_y) < 2) {
                    const int reach = aloft + 8;
                    int x = rng(reach * 2) + input.target_x - reach;
                    int y = rng(reach * 2) + input.target_y - reach;
                    const int need = std::clamp(reach * 2, 12, 36);
                    while (gap(x, y) < need) {
                        x += (x > input.self_x) - (x < input.self_x);
                        y += (y > input.self_y) - (y < input.self_y);
                        if (x == input.self_x && y == input.self_y) { ++x; ++y; }
                    }
                    spot_x = x; spot_y = y;
                } else if (input.free_spot[0]) std::tie(spot_x, spot_y) = *input.free_spot[0];
                else if (input.free_spot[1]) std::tie(spot_x, spot_y) = *input.free_spot[1];
                --aloft;
                Think fly{ MonAct::fly, 8, spot_x, spot_y };
                fly.unit = ThinkUnit::none;
                return wait(fly, 12);
            }
            aloft = 1;
            landing = true;
        }
        if (landing) {
            if (land()) { aloft = -1; return wait(keep(ThinkUnit::target, 9, 2, 3), 12); }
            aloft = 8;
            fallback.wait = 12;
        }
        if (input.in_melee) return roll() >= aip[0] ? wait(fallback, aip[1]) : wait({ MonAct::a1 }, fallback.wait);
        if (aloft != -1 && rng(100) >= aip[4]) return wait(fallback, aip[1]);
        const Think out = rng(100) < aip[3] ? think_circle(rng, 6) : keep(ThinkUnit::target, 2, 9, 0);
        aloft = 0;
        return wait(out, 12);
    }
    // SandMaggotQueen (66, FUN_005f9cf0; Coldworm): rested (state3) she
    // stands aip2 x 25; under aip1 young made (state) she plays S1 where she
    // stands (FUN_005de440, mode 8 at her own spot; state2 1), then her young
    // comes up (spawn 1: ai.cpp, counted there), each a wait of aip2 and a
    // rest after. Done, she does nothing.
    Think sand_maggot_queen() {
        if (*input.state3) { *input.state3 = 0; return { MonAct::idle, aip[1] * 25 }; }
        if (*input.state2 == 0 && *input.state >= aip[0]) return { MonAct::none };
        const int phase = *input.state2 != 0;
        Think out{ MonAct::spawn, phase };
        out.wait = aip[1];
        *input.state3 = phase;
        *input.state2 = 1 - phase;
        return out;
    }
    // MaggotEgg (40, FUN_005f6530): unhatched, aip2 % it hatches (Skill1,
    // wait aip1; state 1); hatched it dies; else stands aip1.
    Think maggot_egg() {
        if (*input.state == 1) { Think out{ MonAct::die }; return out; }
        if (input.skill[0] && roll() < aip[1]) { *input.state = 1; Think out = cast(0); out.wait = aip[0]; return out; }
        return { MonAct::idle, aip[0] };
    }
    // MaggotLarva (38, FUN_005f6220): out of melee aip3 % walks in (flags
    // 1), else stands aip4; in melee, rested (state 0), aip1 % A1 (state
    // 1); else stands aip2.
    Think maggot_larva() {
        if (!input.in_melee) {
            *input.state = 0;
            return roll() < aip[2] ? Think{ MonAct::walk, 1 } : Think{ MonAct::idle, aip[3] };
        }
        if (*input.state == 0 && roll() < aip[0]) { *input.state = 1; return { MonAct::a1 }; }
        *input.state = 0;
        return { MonAct::idle, aip[1] };
    }
};

inline constexpr auto kAct2Thinks = std::to_array<ThinkRow>({
    { "Duriel", &think_as<Act2Thinks, &Act2Thinks::duriel> },
    { "Swarm", &think_as<Act2Thinks, &Act2Thinks::swarm> },
    { "PinHead", &think_as<Act2Thinks, &Act2Thinks::pin_head> },
    { "Mummy", &think_as<Act2Thinks, &Act2Thinks::mummy> },
    { "SandLeaper", &think_as<Act2Thinks, &Act2Thinks::sand_leaper> },
    { "ClawViper", &think_as<Act2Thinks, &Act2Thinks::claw_viper> },
    { "PantherWoman", &think_as<Act2Thinks, &Act2Thinks::panther_woman> },
    { "PantherJavelin", &think_as<Act2Thinks, &Act2Thinks::panther_javelin> },
    { "Scarab", &think_as<Act2Thinks, &Act2Thinks::scarab> },
    { "BatDemon", &think_as<Act2Thinks, &Act2Thinks::bat_demon> },
    { "Baboon", &think_as<Act2Thinks, &Act2Thinks::baboon> },
    { "SandRaider", &think_as<Act2Thinks, &Act2Thinks::sand_raider> },
    { "GreaterMummy", &think_as<Act2Thinks, &Act2Thinks::greater_mummy> },
    { "SandMaggot", &think_as<Act2Thinks, &Act2Thinks::sand_maggot> },
    { "Summoner", &think_as<Act2Thinks, &Act2Thinks::summoner> },
    { "Vulture", &think_as<Act2Thinks, &Act2Thinks::vulture> },
    { "SandMaggotQueen", &think_as<Act2Thinks, &Act2Thinks::sand_maggot_queen> },
    { "MaggotEgg", &think_as<Act2Thinks, &Act2Thinks::maggot_egg> },
    { "MaggotLarva", &think_as<Act2Thinks, &Act2Thinks::maggot_larva> },
});

}  // namespace d2d::rules
