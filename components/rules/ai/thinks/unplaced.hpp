// SPDX-License-Identifier: GPL-3.0-or-later
// The MonAI thinks whose monsters no 1.14d table or preset places (the
// spawners and helpers: monster-ai.md "Unplaced spawners and special AIs"),
// and the special AIs nothing in game.exe is seen to set but the merc's
// lost owner (special 6). tools/emu/ai_group2.py runs each in game.exe;
// tests/monsters/test_ai_group2.cpp replays its runs.
#pragma once

#include "monsters.hpp"
#include "thinker.hpp"

#include <array>

namespace d2d::rules {

struct UnplacedThinks : Thinker {
    // EvilHole (76, FUN_005fb410; evilhole1..5, demonhole): its timer
    // (state, frame + aip2 when not above 0, with the count aip1 in state2).
    // In NU it stands 5 past 5, else goes to S3 (mode 10); S3 goes to S4
    // (11), each waiting 20. In S4 with young left, past its timer it
    // starts it again and lays one by its spawn column (FUN_0063efa0,
    // FUN_005b2f20: flags 0x4020000; a demonhole's gets state 0xb8 and
    // overlay 0xca), waiting aip2 either way. Else it's gone (mode 0).
    Think evil_hole() {
        if (*input.state < 1) {
            *input.state = aip[1] + input.frame;
            *input.state2 = aip[0];
        }
        if (input.mode == 1) {
            if (input.dist > 5) return { MonAct::idle, 5 };
            return mode_then_wait(10);
        }
        if (input.mode == 10) return mode_then_wait(11);
        if (input.mode != 11 || *input.state2 < 1) return { MonAct::gone };
        if (*input.state < input.frame) {
            *input.state = aip[1] + input.frame;
            if (input.spawned) {
                --*input.state2;
                effect(ThinkEffect::spawn, -1);
                if (input.demon_hole) {
                    effect(ThinkEffect::state, 0xb8, 1);
                    effect(ThinkEffect::overlay, 0xca);
                }
            }
        }
        Think out{ MonAct::none };
        out.wait = aip[1];
        return out;
    }
    // InvisoSpawner (82, FUN_005e0160): its count (state2) is aip1 while
    // its timer (state) is 0. Within aip2: none left, it's gone; past its
    // timer it makes a mummy1 (row 0x60, FUN_0063ec70) at a spot in its room
    // (FUN_0054dc40, FUN_005b2f20 flags 0x42), one fewer left, aip3 frames
    // to the next. It stands 15.
    Think inviso_spawner() {
        if (*input.state == 0) *input.state2 = aip[0];
        if (input.dist <= aip[1]) {
            if (*input.state2 < 1) return { MonAct::gone };
            if (*input.state <= input.frame && input.spawned) {
                --*input.state2;
                *input.state = aip[2] + input.frame;
                effect(ThinkEffect::spawn, 0x60);
            }
        }
        return { MonAct::idle, 15 };
    }
    // MosquitoNest (83, FUN_005e0260; suckernest1..4, not enabled): within
    // aip2, once it's laid more than aip1 it dies (flag 0x20000, mode 0);
    // with Skill1, past its timer and room for its young (FUN_005fd350), it
    // lays (Skill1, one more laid, aip3 frames to the next). It stands 25.
    Think mosquito_nest() {
        if (input.dist <= aip[1]) {
            if (*input.state2 > aip[0]) return { MonAct::die };
            if (input.skill[0] && *input.state < input.frame && input.spot_free) {
                ++*input.state2;
                *input.state = aip[2] + input.frame;
                return cast(0);
            }
        }
        return { MonAct::idle, 25 };
    }
    // 7TIllusion (88, FUN_005ea080; seventombs): first mode 4 at 10
    // subtiles left of itself (state2 1 after), then it dies (flag 0x20000,
    // mode 0).
    Think seven_tombs() {
        if (*input.state2 == 1) return { MonAct::die };
        *input.state2 = 1;
        return { MonAct::mode_at, 4, input.self_x - 10, input.self_y, ThinkUnit::none };
    }
    // InvisoPet (103, FUN_005ea7a0; invisopet): about its owner
    // (FUN_0058f0d0) within aip1, half that while the owner runs (mode 3,
    // the halving truncates). Before its timer, the owner not running and not
    // hit, it stands aip2; else skill 0x124 in mode 9 at a point rand(2r) - r
    // off the owner on each axis, the timer aip2 frames on.
    Think inviso_pet() {
        int radius = aip[0];
        const int owner_mode = input.owner ? input.owner_mode : 0;
        if (owner_mode == 3) radius /= 2;
        if (input.frame <= *input.state && owner_mode != 3 && !input.got_hit) return { MonAct::idle, aip[1] };
        const int point_x = rng(2 * radius) - radius + input.owner_x;
        const int point_y = rng(2 * radius) - radius + input.owner_y;
        *input.state = aip[1] + input.frame;
        return { MonAct::skill, kInvisoPetSkill, point_x, point_y, ThinkUnit::none };
    }
    static constexpr int kInvisoPetSkill = 0x124;

    // Special AI 3's think (FUN_005e5870, init FUN_005e5730): a pack keeping
    // its formation. Its command 8, or a hit on it or its group (FUN_0058f380
    // with LAB_005e5710; then it sends command 8 first, FUN_0058f730): out of
    // the group (FUN_0058f160), back to its own think, stands 10. A follower
    // walks to its command's point plus its offset (state, state2), the
    // command done, or stands 15 without one. The leader: 30 % stands 10;
    // else its phase (state3) steps on (wrapping at 100) and picks a side by
    // quarter: rand(10) one way (rand(2) its sign), 5 the other; a walk
    // there that sets off sends command 2 to that point, one that can't
    // keeps the phase 25 on.
    Think formation_walk() {
        const bool told_back = input.command && *input.command == 8;
        if (!told_back) {
            if (!input.group_hit && !input.got_hit) return formation_step();
            effect(ThinkEffect::rally, 8);
        }
        effect(ThinkEffect::leave_group);
        effect(ThinkEffect::toggle);
        return { MonAct::idle, 10 };
    }
    Think formation_step() {
        if (!input.self_owner) {
            if (!input.command || *input.command == 0) return { MonAct::idle, 15 };
            effect(ThinkEffect::command_done);
            return { MonAct::move_to, 0, input.command_x + *input.state, input.command_y + *input.state2, ThinkUnit::none };
        }
        if (rng.next() % 100 < 30) return { MonAct::idle, 10 };
        int phase = *input.state3 + 1;
        if (phase > 99) phase = 0;
        int point_x = input.self_x, point_y = input.self_y;
        int side = rng(10);
        if (rng(2) != 0) side = -side;
        if (phase < 25) { point_x += side; point_y -= 5; }
        else if (phase < 50) { point_x += 5; point_y += side; }
        else if (phase < 75) { point_x += side; point_y += 5; }
        else { point_x -= 5; point_y += side; }
        if (!away(-1, false)) {                             // FUN_005dee50 couldn't set off
            *input.state3 = phase + 25;
            return { MonAct::none };
        }
        effect(ThinkEffect::rally, 2);
        *input.state3 = phase;
        return { MonAct::move_to, 0, point_x, point_y, ThinkUnit::none };
    }
    // Special AI 6's think (FUN_005e7c10; the merc's when it has no owner,
    // FUN_005e52d0): not in NU it stands 5. Out of town, with a unit its
    // search finds (FUN_005dd7f0, the second target here): rand(100) under
    // 80 strikes it (A1) when it's in melee, under 30 walks at it (flags 7)
    // when not, else stands 10. Else a seed step: % 100 under 20 wanders 5,
    // else stands 10.
    Think npc_melee() {
        if (!input.neutral) return { MonAct::idle, 5 };
        if (!input.in_town && input.found) {
            const int draw = roll();
            if (input.found_melee) return draw < 80 ? at_second({ MonAct::a1 }) : Think{ MonAct::idle, 10 };
            return draw < 30 ? at_second({ MonAct::walk, 7 }) : Think{ MonAct::idle, 10 };
        }
        if (rng.next() % 100 < 20) return think_wander(rng, 5);
        return { MonAct::idle, 10 };
    }
    // Special AI 9's think (FUN_005e7f80): its command 8, or hit (it or its
    // group, FUN_0058f380) or under 20 from the target (then command 8 sent
    // first): out of the group, back to its own think. It stands 10.
    Think minion_retreat() {
        const bool told_back = input.command && *input.command == 8;
        if (!told_back) {
            if (!input.group_hit && !input.got_hit && input.dist >= 20) return { MonAct::idle, 10 };
            effect(ThinkEffect::rally, 8);
        }
        effect(ThinkEffect::leave_group);
        effect(ThinkEffect::toggle);
        return { MonAct::idle, 10 };
    }
    // Special AI 12's think (FUN_005e8340): its owner (FUN_00553540, the
    // target here) gone or in town: back to its own think, its next think
    // next frame (FUN_00540e60, FUN_005417d0). Else the first time (state
    // 0) it runs at the owner (flags 7, state 1); after, the owner in reach
    // (FUN_00622c40) it strikes the owner (A1); a unit in melee its search
    // finds (FUN_005de890) it strikes (state 0); else runs at the owner
    // (state 1).
    Think stationary_npc() {
        if (!input.owner || input.in_town) {
            effect(ThinkEffect::toggle);
            Think out{ MonAct::none };
            out.event = 1;
            return out;
        }
        if (*input.state == 0) {
            *input.state = 1;
            return { MonAct::run, 7 };
        }
        if (input.reach) return { MonAct::a1 };
        if (input.found && input.found_melee) {
            *input.state = 0;
            return at_second({ MonAct::a1 });
        }
        *input.state = 1;
        return { MonAct::run, 7 };
    }

private:
    static Think mode_then_wait(int mode) {                 // FUN_005ddfc0(mode, 0, 0), FUN_005de0f0(20)
        Think out{ MonAct::mode_at, mode, 0, 0, ThinkUnit::none };
        out.wait = 20;
        return out;
    }
};

inline constexpr auto kUnplacedThinks = std::to_array<ThinkRow>({
    { "EvilHole", &think_as<UnplacedThinks, &UnplacedThinks::evil_hole> },
    { "InvisoSpawner", &think_as<UnplacedThinks, &UnplacedThinks::inviso_spawner> },
    { "MosquitoNest", &think_as<UnplacedThinks, &UnplacedThinks::mosquito_nest> },
    { "7TIllusion", &think_as<UnplacedThinks, &UnplacedThinks::seven_tombs> },
    { "InvisoPet", &think_as<UnplacedThinks, &UnplacedThinks::inviso_pet> },
});

}  // namespace d2d::rules
