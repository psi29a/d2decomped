// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// test-sources: apps/d2d/session/corpse_select.cpp
// test-includes: apps/d2d
// FUN_00464310 runs the active skill's SelectProc: the three player corpse
// proc kinds differ on corpseSel, Velocity, SwitchAI, walking and boss flags.
#include "session/corpse_select.hpp"

#include <ai/units.hpp>
#include <skills.hpp>
#include <uniques.hpp>

#include <cassert>
#include <cstdio>

int main() {
    d2d::game::Monster corpse;
    corpse.hit_points = 0;
    corpse.mode = "DD";
    d2d::rules::MonType type;
    type.corpse_sel = true;
    type.velocity = 8;
    type.can_walk = true;
    type.switch_ai = true;
    d2d::rules::Skill skill;
    skill.target_corpse = true;

    assert(d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    skill.id = 70;
    assert(d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    type.velocity = 0;
    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));

    d2d::rules::Skill right = skill;
    right.id = 74;
    assert(d2d::client::corpse_cursor_takes(&skill, &right, corpse, type, false));

    skill.id = 74;
    assert(d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    type.corpse_sel = false;
    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));

    type.corpse_sel = true;
    type.velocity = 8;
    skill.id = 95;
    assert(d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    type.switch_ai = false;
    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    type.switch_ai = true;
    corpse.boss = d2d::rules::Boss::champion;
    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    corpse.boss = d2d::rules::Boss::none;
    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, true));

    skill.id = 142;
    assert(d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));

    corpse.corpse_used = true;
    skill.id = 0;
    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    std::puts("OK: corpse cursor selection matches FUN_00464310's player SelectProcs");
}
