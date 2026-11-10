// SPDX-License-Identifier: GPL-3.0-or-later
// Act 1's MonAI thinks (docs/research/re/monster-ai.md "Thinks: Fetish,
// Arach, Vampire, Griswold, the Smith"; tools/emu/mon_ais_act1.py).
#pragma once

#include "level_ids.hpp"
#include "monsters.hpp"
#include "thinker.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>

namespace d2d::rules {

struct Act1Thinks : Thinker {
    // Skeleton (2, FUN_005efcf0; hellbovine too).
    Think skeleton() {
        if (!input.in_melee) { if (roll() < aip[0]) return walk; }
        else if (roll() < aip[2]) return a1_or_a2(aip[3]);
        return idle2;
    }
    // Zombie (3, FUN_005efe20): runs at a foe it's hit by, or aip1 % one
    // within aip2, at pace 100 (FUN_005de190, 0x5eff19); else wanders 3,
    // bar in the Burial Grounds.
    Think zombie() {
        if (input.in_melee) return a1_or_a2(aip[3]);
        if (!input.got_hit && !(input.dist < aip[1] && roll() < aip[0]) && input.level != level_ids::kBurialGrounds) return think_wander(rng, 3);
        *input.pace = 100;
        return { MonAct::run };
    }
    // Bighead (4, FUN_005eff50): above aip1 % life it closes in (aip3 % a
    // shot within 15); below, it backs off under 3 (pace 50), walks in past
    // 15, else aip4 % shoots, aip2 % circles, else stands 10. A shot (at
    // the target) needs a second target (FUN_005ddc30), its distance unread.
    Think bighead() {
        if (!input.in_melee && input.got_hit) return { MonAct::a2 };
        if (input.life_pct >= aip[0]) {
            if (input.in_melee) return { MonAct::a1 };
            if (input.dist < 15 && input.second && roll() < aip[2]) return { MonAct::a2 };
            return walk;
        }
        if (input.dist < 3) { *input.pace = 50; return away(5, false) ? Think{ MonAct::none } : Think{ MonAct::a2 }; }
        if (input.dist > 15) return { MonAct::approach, 6 };
        if (input.second && roll() < aip[3]) return { MonAct::a2 };
        if (roll() >= aip[1]) return { MonAct::idle, 10 };
        return think_circle(rng, 3);
    }
    // BloodHawk (5, FUN_005f00e0): aip1 % a charge (state 1: a swing when
    // it lands in melee), else wanders (aip2 %: 4, else 3) past 3 subtiles;
    // in melee aip3 % a swing; else backs off 4, or swings.
    Think blood_hawk() {
        const bool charged = *input.state == 1;
        *input.state = 0;
        if (charged && input.in_melee) return { MonAct::a1 };
        if (!input.in_melee) {
            if (roll() < aip[0]) { *input.state = 1; *input.pace = aip[4]; if (input.steps) *input.steps = input.dist; return { MonAct::walk, 0 }; }
            if (input.dist > 3) {
                const bool wide = roll() < aip[1];
                if (wide) *input.pace = -50;
                return think_wander(rng, wide ? 4 : 3);
            }
        } else if (roll() < aip[2]) return { MonAct::a1 };
        *input.pace = aip[3];
        return away(4, false) ? Think{ MonAct::none } : Think{ MonAct::a1 };
    }
    // Brute (7, FUN_005efb80): walks in without a draw, at pace 100 less
    // its life % (40 to 100 of it: 0 to 60, 0x5efcbf); in melee aip3 % a
    // swing (aip4 % A1), else aip3 % circles 4, else stands 15.
    Think brute() {
        if (!input.in_melee) { *input.pace = 100 - std::clamp(input.life_pct, 40, 100); return walk; }
        if (roll() < aip[2]) return a1_or_a2(aip[3]);
        if (roll() < aip[2]) return think_circle(rng, 4);
        return { MonAct::idle, 15 };
    }
    // Wraith (9, FUN_005f0a20) and Goatman (12, FUN_005f12a0): aip1 % close
    // in (the wraith drifts up to 12 subtiles), in melee aip3 % A1, else
    // stand aip2.
    Think wraith() {
        if (!input.in_melee) { if (roll() < aip[0]) return ai_name == "Wraith" ? Think{ MonAct::keep, 12, 0 } : walk; }
        else if (roll() < aip[2]) return { MonAct::a1 };
        return idle2;
    }
    // CorruptRogue (10, FUN_005f0b00): runs at a target past 20 - 3 x
    // difficulty (FUN_00573930's difficulty); in melee aip3 % A1, else
    // aip1 % close in (aip5 % running).
    Think corrupt_rogue() {
        const auto run = [&] { *input.pace = aip[3]; return Think{ MonAct::run }; };     // FUN_005de190(aip4, 0) before each run
        if (input.dist > 20 - 3 * input.difficulty) return run();
        if (input.in_melee) return roll() < aip[2] ? Think{ MonAct::a1 } : idle2;
        if (roll() >= aip[0]) return idle2;
        return roll() < aip[4] ? run() : walk;
    }
    // QuillRat (14, FUN_005f1140): A1 in melee, spikes (A2) when hit;
    // wanders (aip4, at least 3) at aip1 or more; else aip2 % a back-off
    // of aip4 first, then the spikes.
    // A pending command with a live unit would make it shoot first
    // (FUN_0058ee80), but nothing sends one to a group holding quillrats
    // (stubs.md; monster-ai.md "AI commands").
    Think quill_rat() {
        if (input.in_melee) return { MonAct::a1 };
        if (input.got_hit) return { MonAct::a2 };
        const int roam = std::max(aip[3], 3);
        if (input.dist >= aip[0]) return think_wander(rng, roam);
        if (roll() >= aip[1]) {
            if (away(aip[3] & 0xff, false)) return { MonAct::none };
            if (input.dist > 3) return think_wander(rng, roam);
        }
        return { MonAct::a2 };
    }
    // CorruptArcher (35, FUN_005f5a20): at its second target (FUN_005ddc30,
    // its distance); none, it circles 3 or (50 %) stands aip3. It shoots a
    // foe it's hit by; under 6, aip4 % runs 12 off (pace 100); past aip8,
    // aip1 % walks in to aip8 (pace 10); past aip5 runs in aip5 (pace 100);
    // else aip2 % a shot (Skill2 aip6 %, Skill3 aip7 %, else Skill1 or A1).
    Think corrupt_archer() {
        if (!input.second) return roll() > 49 ? Think{ MonAct::idle, aip[2] } : think_circle(rng, 3);
        const int dist = input.second_dist;
        if (!input.in_melee && input.got_hit) return at_second({ MonAct::a1 });
        if (dist < 6 && roll() < aip[3]) { *input.pace = 100; if (away_second(12, true)) return { MonAct::none }; }
        if (aip[7] > 0 && aip[7] < dist && roll() < aip[0]) { *input.pace = 10; return at_second({ MonAct::approach, aip[7] }); }
        if (aip[4] < dist) { *input.pace = 100; return at_second({ MonAct::run, aip[4] }); }
        if (roll() >= aip[1]) return { MonAct::idle, aip[2] };
        if (input.skill[1] && roll() < aip[5]) return at_second({ MonAct::skill, 1 });
        if (input.skill[2] && roll() < aip[6]) return at_second({ MonAct::skill, 2 });
        return at_second(input.skill[0] ? Think{ MonAct::skill, 0 } : Think{ MonAct::a1 });
    }
    // CorruptLancer (36, FUN_005f5d50): runs in past aip5 (state 1: it
    // strikes on arrival); at range aip1 % closes in (aip4 % running);
    // in melee aip2 % (or on arrival) Skill1..3 at aip6..8 %, else A1.
    Think corrupt_lancer() {
        if (aip[4] < input.dist) { *input.state = 1; *input.pace = 100; return { MonAct::run }; }
        if (!input.in_melee) {
            if (roll() >= aip[0]) return { MonAct::idle, aip[2] };
            if (roll() >= aip[3]) return { MonAct::approach, 3 };
            *input.pace = 100;
            return { MonAct::run };
        }
        if (*input.state == 0 && roll() >= aip[1]) return { MonAct::idle, aip[2] };
        *input.state = 0;
        for (int skill = 0; skill < 3; ++skill)
            if (input.skill[std::size_t(skill)] && roll() < aip[std::size_t(5 + skill)]) return { MonAct::skill, skill };
        return { MonAct::a1 };
    }
    // SkeletonBow (37, FUN_005f6070): shoots a foe it's hit by; past 19,
    // aip3 % keeps aip5 off (aip4 at most a go), else stands 20; else aip1
    // % shoots, 20 % circles 3, else stands aip2.
    Think skeleton_bow() {
        if (input.got_hit) return { MonAct::a1 };
        if (input.dist > 19) return roll() < aip[2] ? Think{ MonAct::keep, aip[3], aip[4] } : Think{ MonAct::idle, 20 };
        if (roll() < aip[0]) return { MonAct::a1 };
        if (roll() < 20) return think_circle(rng, 3);
        return idle2;
    }
    // Fallen (6, FUN_005f02c0): with a unit dying near, it backs off 12 from
    // its target, dropping its command (a seed step: 1 in 20 a scream).
    // Charging (command 1) it walks in, then aip3 % swings (aip4 % A1),
    // else stands 5. Otherwise: hit, it walks in; a leader within 15 aip1 %
    // taunts (S2) and sets its group charging; aip2 or nearer it walks in,
    // else 30 % wanders 3; in melee aip3 % (or on a scare, state 1) swings,
    // else 30 % taunts, else stands 10.
    // Its group's senders (Fallen, FallenShaman) only send 1 (monster-ai.md
    // "AI commands"); any other would be dropped.
    // Out of NU (FUN_005f02c0's mode test after the dying scan) it stands
    // 10. Every mode's end leaves NU before a think (FUN_005a8030: WL / RN
    // FUN_00624690(unit, 1), the rest FUN_005a7c20 mode 1), so only a think
    // event due mid-mode meets it, and d2d thinks at a mode's end (ai.cpp's
    // think scheduling note): never here.
    Think fallen() {
        if (input.dying) {
            *input.state = 1; *input.command = 0; *input.pace = 50;
            if (away(12, false)) { (void)rng.next(); return { MonAct::none }; }
        }
        if (*input.command == 1) {
            if (!input.in_melee) return { MonAct::walk, 0 };
            if (roll() >= aip[2]) return { MonAct::idle, 5 };
            return a1_or_a2(aip[3]);
        }
        if (!input.in_melee && input.got_hit) return { MonAct::walk, 0 };
        if (input.dist < 15 && input.leader && roll() < aip[0]) { *input.rally = true; return { MonAct::s2 }; }
        if (!input.in_melee) {
            if (input.dist <= aip[1]) return walk;
            return roll() < 30 ? think_wander(rng, 3) : Think{ MonAct::idle, 10 };
        }
        if (*input.state != 0 || roll() < aip[2]) { *input.state = 0; return a1_or_a2(aip[3]); }
        return roll() < 30 ? Think{ MonAct::s2 } : Think{ MonAct::idle, 10 };
    }
    // FallenShaman (13, FUN_005f1440): in melee aip3 % A1; aip1 % sets its
    // group charging; with a corpse (FUN_005dd0b0) aip1 % raises it
    // (Skill1); within aip5, aip2 % a fire bolt (Skill2) at the target,
    // then the same at its second target (FUN_005ddc30) within aip5 of it
    // (0x5f167d..0x5f16d9); else aip3 % circles 3, else stands 10.
    Think fallen_shaman() {
        if (input.in_melee && roll() < aip[2]) return { MonAct::a1 };
        if (roll() < aip[0]) *input.rally = true;
        if (input.corpse && roll() < aip[0] && input.skill[0] && input.usable) return { MonAct::skill, 0 };   // FUN_005fd470 after the roll
        if (input.dist < aip[4] && roll() < aip[1]) return { MonAct::skill, 1 };
        if (input.second && input.second_dist < aip[4] && roll() < aip[1]) return at_second({ MonAct::skill, 1 });
        if (roll() >= aip[2]) return { MonAct::idle, 10 };
        return think_circle(rng, 3);
    }
    // FoulCrowNest (43, FUN_005f6650; init FUN_005f6630 keeps the frame in
    // the state): past 20 stands 25; once it's laid aip3 it collapses;
    // every aip1 frames it lays (Skill1, Nest) where there's room; else
    // stands 20 + a seed step % 10.
    Think foul_crow_nest() {
        if (input.dist > 20) return { MonAct::idle, 25 };
        if (*input.state2 >= aip[2]) return { MonAct::die };
        if (input.skill[0] && std::abs(input.frame - *input.state) >= aip[0]) {
            *input.state = input.frame;
            if (input.spot_free) { ++*input.state2; return { MonAct::skill, 0 }; }
        }
        return { MonAct::idle, int(rng.next() % 10) + 20 };
    }
    // BloodRaven (59, FUN_005e6320; init FUN_005e6300 clears the flag):
    // past 45 stands 5; 50 or more from home (her command 10, set at her
    // first think) she heads back until within 5 (the flag: *command);
    // past 20 she closes to a spot half as far off (12 at least); every
    // think adds 3 to the state, and out of melee, with fewer than 8 + 2 *
    // difficulty raised (state2), rand(100) < state raises a zombie (Nest)
    // 5..19 off the target; past 5, 5 % moves 12 about the target, else
    // not hit 80 % strikes ((difficulty + 4) * 10 % Quick Strike, else A1),
    // else circles 4; within 5, 30 % backs off to 12 running, else A1.
    // ponytail: the walks home / about the target and the circle always
    // set off; the second target search (FUN_005ddc30) is the target.
    Think blood_raven() {
        if (input.dist > 45) return { MonAct::idle, 5 };
        if (input.target_home_dist >= 50 || input.home_dist > 50) { *input.command = 1; *input.pace = 100; return { MonAct::home }; }
        if (*input.command && input.home_dist > 5) { *input.pace = 100; return { MonAct::home }; }
        *input.command = 0;
        auto around = [&](int radius) { auto out = think_wander(rng, radius); out.act = MonAct::around; return out; };
        if (input.dist > 20) { *input.pace = 100; return around(std::max(input.dist >> 1, 12)); }
        *input.state += 3;
        if (input.skill[0] && !input.in_melee && *input.state2 < input.difficulty * 2 + 8 && roll() < *input.state) {
            const int reach = rng(15) + 5;
            int x = reach, y = reach;
            if (rng.next() & 1) y = rng(reach); else x = rng(reach);
            if (rng.next() & 1) x = -x;
            if (rng.next() & 1) y = -y;
            ++*input.state2; *input.state = 0;
            return { MonAct::skill, 0, x, y };
        }
        if (input.dist > 5) {
            if (roll() < 5) { *input.pace = 100; return around(12); }
            if (!input.got_hit && roll() < 80) return input.skill[1] && roll() < (input.difficulty + 4) * 10 ? Think{ MonAct::skill, 1 } : Think{ MonAct::a1 };
            *input.pace = 50;
            return think_circle(rng, 4);             // FUN_005df7d0(4, 1): its walk flagged 4
        }
        if (roll() < 30) { *input.pace = 100; if (away(12 - input.dist, true)) return { MonAct::none }; }
        return { MonAct::a1 };
    }
    // SkeletonMage (FUN_005f96c0): with a second target (FUN_005ddc30) by
    // its distance: past aip2, aip3 % closes to aip2 (pace 10); within
    // aip4, aip5 % backs off 5 from it (pace 25; blocked: A1 at the
    // target); within aip6, aip1 % shoots it (A1, MissA1). Then, the
    // distance 0x7fffffff with none, past aip2 aip3 % closes in on the
    // target; else aip7 % circles 4, else stands aip8.
    Think skeleton_mage() {
        const int dist = input.second ? input.second_dist : 0x7fffffff;
        if (input.second) {
            if (aip[1] < dist && roll() < aip[2]) { *input.pace = 10; return at_second({ MonAct::approach, aip[1] }); }
            if (dist <= aip[3] && roll() < aip[4]) { *input.pace = 25; return away_second(5, false) ? Think{ MonAct::none } : Think{ MonAct::a1 }; }
            if (dist < aip[5] && roll() < aip[0]) return at_second({ MonAct::a1 });
        }
        if (aip[1] < dist && roll() < aip[2]) { *input.pace = 10; return { MonAct::approach, aip[1] }; }
        if (roll() >= aip[6]) return { MonAct::idle, aip[7] };
        return think_circle(rng, 4);
    }
    // GargoyleTrap (FUN_005f9490): after a shot it stands its scratch word
    // (aip3) frames; else, facing the target square on, within 5 subtiles
    // of its row or column and nearer than aip1, aip2 % shoots Skill1;
    // else it stands aip4.
    Think gargoyle_trap() {
        if (*input.state > 0) { const int frames = *input.state; *input.state = 0; return { MonAct::idle, frames }; }
        if ((std::abs(input.off_x) <= 5 || std::abs(input.off_y) <= 5) && input.skill[0] && input.dist < aip[0] && roll() < aip[1]) {
            *input.state = aip[2];
            return { MonAct::skill, 0 };
        }
        return { MonAct::idle, aip[3] };
    }
    // Arach (26, FUN_005f4510): hurt (state 1), over 75 % life it's done:
    // aip3 % charges (state 2, walk 0), else circles 6; else in melee aip1
    // - 25 % A1; aip4 or more off, not hit, circles 12; else backs off 4.
    // Out of melee: hit or charging (state2) it charges; every 21st think
    // (state3) aip3 % a charge; else 20 % wanders 6, else stands 15. In
    // melee aip1 % A1; at aip5 % life or more aip2 % circles 4, else
    // stands 15; under, hurt: it lays (Skill1, Sk1mode, at no unit) out of
    // Spider Lay's state, else backs off 8. A back-off (FUN_005defe0 flags
    // 0) that can't set off thinks aidel on.
    Think arach() {
        const Think charge{ MonAct::walk, 0 }, stand{ MonAct::idle, 15 }, stuck{ MonAct::idle, input.aidel };
        if (*input.state == 1) {
            *input.state2 = 0;
            if (input.life_pct > 75) {
                *input.state = 0;
                if (roll() < aip[2]) { *input.state = 2; return charge; }
                return think_circle(rng, 6);
            }
            if (input.in_melee && aip[0] > 25 && roll() < aip[0] - 25) return { MonAct::a1 };
            if (aip[3] <= input.dist && !input.got_hit) { *input.state = 0; return think_circle(rng, 12); }
            return away(4, false) ? Think{ MonAct::none } : stuck;
        }
        if (!input.in_melee) {
            if (input.got_hit || *input.state2 == 1) { *input.state2 = 1; return charge; }
            if (++*input.state3 > 20) *input.state3 = 0;
            *input.state2 = 0;
            if (*input.state3 == 1 && roll() < aip[2]) { *input.state2 = 1; return charge; }
            return roll() < 20 ? think_wander(rng, 6) : stand;
        }
        *input.state = 2;
        if (roll() < aip[0]) return { MonAct::a1 };
        if (aip[4] <= input.life_pct) return roll() < aip[1] ? think_circle(rng, 4) : stand;
        *input.state = 1;
        if (input.skill[0] && !input.laying) return { MonAct::skill, 0 };
        return away(8, false) ? Think{ MonAct::none } : stuck;
    }
    // Vampire (28, FUN_005f4a70): aip5 bit 0 lets it shoot (Skill1 or
    // Skill4, even odds, at its second target within 20), bit 1 cast a
    // firewall (Skill2), bit 2 a meteor (Skill3), aip4 % each, 11 thinks
    // apart (state3). Hit, it's on (state 1), keeping the hitter's distance
    // under 30 (state2); in melee 30 % with bit 0 a shot, else A1. Fleeing
    // (state 2): over 74 % life it walks in; under 14 or within state2 it
    // backs off 8, the pace Run / Velocity (0..120 % on); past aip3, or
    // past aip2 %, it stands 15; else a cast, a shot, or circles 4. Else
    // under 33 % life it flees, backing off 8; in melee aip1 % A1 (bit 0:
    // 70 %, else a shot), else 33 % circles 4, else stands 10; aip3 or more
    // off it walks in on, else stands 15; then aip2 % a cast or a shot (25
    // % circles 4 first) or walks in; else past 20 walks in, under 9 50 %
    // backs off 8, else 50 % circles 4, else stands 10. A shot but the
    // hit one in melee (at the target) needs a second target (FUN_005ddc30)
    // and goes at it, by its distance; a back-off's pace stays when blocked.
    Think vampire() {
        const bool shoots = aip[4] & 1, at_hand = shoots && input.second;
        const int second_dist = input.second ? input.second_dist : 0x7fffffff;
        auto shot = [&](ThinkUnit unit) { Think out{ MonAct::skill, roll() < 50 ? 0 : 3 }; out.unit = unit; return out; };
        auto cast = [&] {                                   // FUN_005dead0(Sk2mode / Sk3mode, at the target)
            for (const int skill : { 1, 2 })
                if ((aip[4] & (skill << 1)) && *input.state3 < 1 && roll() < aip[3]) { *input.state3 = 11; return skill; }
            return 0;
        };
        if (*input.state3 > 0) --*input.state3;
        if (input.got_hit) {
            if (*input.state == 0) *input.state = 1;
            if (input.dist < 30 && *input.state2 < input.dist) *input.state2 = input.dist;
            if (input.in_melee) return roll() > 30 || !shoots ? Think{ MonAct::a1 } : shot(ThinkUnit::target);
        }
        if (*input.state == 2) {
            if (input.life_pct > 74) { *input.state = 1; return walk; }
            if (input.dist < 14 || input.dist <= *input.state2) {
                *input.pace = input.velocity > 0 ? std::clamp(input.run * 100 / input.velocity - 100, 0, 120) : 0;
                if (away(8, false)) return { MonAct::none };       // flags 4: no aidel think when it can't
            }
            if (aip[2] <= input.dist || roll() >= aip[1]) return { MonAct::idle, 15 };
            if (const int skill = cast()) return { MonAct::skill, skill };
            return !at_hand || second_dist > 20 ? think_circle(rng, 4) : shot(ThinkUnit::second);
        }
        if (input.life_pct < 33) {
            *input.state = 2;
            if (away(8, false)) return { MonAct::none };
        }
        if (input.in_melee) {
            *input.state = 1;
            if (roll() < aip[0]) {
                if (!at_hand || roll() > 30) return { MonAct::a1 };
                if (second_dist < 21) return shot(ThinkUnit::second);
            }
            return roll() < 33 ? think_circle(rng, 4) : Think{ MonAct::idle, 10 };
        }
        if (aip[2] <= input.dist) return *input.state != 1 ? Think{ MonAct::idle, 15 } : walk;
        *input.state = 1;
        if (roll() >= aip[1]) {
            if (input.dist > 20) return walk;
            if (input.dist < 9 && roll() < 50) return away(8, false) ? Think{ MonAct::none } : Think{ MonAct::idle, input.aidel };
            return roll() < 50 ? think_circle(rng, 4) : Think{ MonAct::idle, 10 };
        }
        if (const int skill = cast()) return { MonAct::skill, skill };
        if (!at_hand || second_dist > 20) return walk;
        return roll() > 74 ? think_circle(rng, 4) : shot(ThinkUnit::second);
    }
    // Fetish (30, FUN_005f53e0): at rest (state 0) in melee it squares up
    // (state 1); squared up, after aip3 thinks (state2) with its target
    // over aip4 % life it backs off 14 (state 2); in melee aip1 % A1, else
    // stands aip2. Backing off: past 12, after two thinks it rests, 20 %
    // circling 4, else standing 10; within, it backs off again, or rests
    // and stands 10. Else it walks in. Its walks and back-offs go at pace
    // 50; a back-off (flags 4) that can't set off thinks again at once.
    // First its leader's command (FUN_0058ee80; the fetish shaman's 1 at its
    // target, 0xe to a corpse it raises): with that unit found by id
    // (FUN_00552f60, dead or alive), back to rest,
    // a walk at it (FUN_005dec80(unit, 0)) at pace 50; taken either way. The
    // shaman's target is taken as the fetish's own (one player, or its
    // nearest: the same foe).
    // ponytail: the pathers (2, 0xd) as path_to's.
    Think fetish() {
        if (input.command && *input.command) {
            const bool sent = (*input.command == 1 || *input.command == 14) && input.command_unit;
            if (input.effects) input.effects->push_back({ ThinkEffect::command_done, 0, 0 });   // FUN_0058ed10
            if (sent) {
                *input.state = *input.state2 = 0;
                *input.pace = 50;
                Think out{ MonAct::walk, 0 };
                if (*input.command == 14) out.unit = ThinkUnit::kin;   // the raised corpse (game/ai/act1.cpp's kin)
                return out;
            }
        }
        auto swing = [&] { return roll() >= aip[0] ? Think{ MonAct::idle, aip[1] } : Think{ MonAct::a1 }; };
        if (*input.state == 0) {
            if (input.in_melee) { *input.state2 = 0; *input.state = 1; return swing(); }
        } else if (*input.state == 1) {
            if (aip[2] < ++*input.state2 && aip[3] < input.target_life_pct) {
                *input.state = 2; *input.state2 = 0;
                *input.pace = 50;                                 // kept when it can't set off (FUN_005de190 stands)
                (void)away(14, false);
                return { MonAct::none };
            }
            if (input.in_melee) return swing();
        } else {
            if (*input.state == 2) {
                if (input.dist > 12) {
                    if (++*input.state2 > 1) *input.state = *input.state2 = 0;
                    return roll() < 20 ? think_circle(rng, 4) : Think{ MonAct::idle, 10 };
                }
                *input.pace = 50;
                if (away(14, false)) return { MonAct::none };
                *input.state = *input.state2 = 0;
            }
            return { MonAct::idle, 10 };
        }
        *input.pace = 50;
        return walk;
    }
    // Griswold (90, FUN_005e5ac0): in melee 80 % A1, else 50 % walks in;
    // else stands 10.
    Think griswold() {
        if (input.in_melee) { if (roll() < 80) return { MonAct::a1 }; }
        else if (roll() < 50) return walk;
        return { MonAct::idle, 10 };
    }
    // Smith (98, FUN_005e3890): A1 in melee, else walks in, the faster the
    // more he's hurt (pace: half the life % he's lost).
    Think smith() {
        if (input.in_melee) return { MonAct::a1 };
        *input.pace = (100 - std::clamp(input.life_pct, 0, 100)) >> 1;
        return walk;
    }
};

inline constexpr auto kAct1Thinks = std::to_array<ThinkRow>({
    { "Skeleton", &think_as<Act1Thinks, &Act1Thinks::skeleton> },
    { "Zombie", &think_as<Act1Thinks, &Act1Thinks::zombie> },
    { "Bighead", &think_as<Act1Thinks, &Act1Thinks::bighead> },
    { "BloodHawk", &think_as<Act1Thinks, &Act1Thinks::blood_hawk> },
    { "Brute", &think_as<Act1Thinks, &Act1Thinks::brute> },
    { "Wraith", &think_as<Act1Thinks, &Act1Thinks::wraith> },
    { "Goatman", &think_as<Act1Thinks, &Act1Thinks::wraith> },
    { "CorruptRogue", &think_as<Act1Thinks, &Act1Thinks::corrupt_rogue> },
    { "QuillRat", &think_as<Act1Thinks, &Act1Thinks::quill_rat> },
    { "CorruptArcher", &think_as<Act1Thinks, &Act1Thinks::corrupt_archer> },
    { "CorruptLancer", &think_as<Act1Thinks, &Act1Thinks::corrupt_lancer> },
    { "SkeletonBow", &think_as<Act1Thinks, &Act1Thinks::skeleton_bow> },
    { "Fallen", &think_as<Act1Thinks, &Act1Thinks::fallen> },
    { "FallenShaman", &think_as<Act1Thinks, &Act1Thinks::fallen_shaman> },
    { "FoulCrowNest", &think_as<Act1Thinks, &Act1Thinks::foul_crow_nest> },
    { "BloodRaven", &think_as<Act1Thinks, &Act1Thinks::blood_raven> },
    { "SkeletonMage", &think_as<Act1Thinks, &Act1Thinks::skeleton_mage> },
    { "GargoyleTrap", &think_as<Act1Thinks, &Act1Thinks::gargoyle_trap> },
    { "Arach", &think_as<Act1Thinks, &Act1Thinks::arach> },
    { "Vampire", &think_as<Act1Thinks, &Act1Thinks::vampire> },
    { "Fetish", &think_as<Act1Thinks, &Act1Thinks::fetish> },
    { "Griswold", &think_as<Act1Thinks, &Act1Thinks::griswold> },
    { "Smith", &think_as<Act1Thinks, &Act1Thinks::smith> },
});

}  // namespace d2d::rules
