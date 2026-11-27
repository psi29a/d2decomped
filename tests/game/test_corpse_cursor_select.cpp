// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// test-sources: apps/d2d/session/corpse_select.cpp
// test-includes: apps/d2d
// FUN_00464310 runs the active skill's SelectProc: the three player corpse
// proc kinds differ on corpseSel, Velocity, SwitchAI, walking and boss flags.
#include "session/corpse_select.hpp"

#include <ai/units.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <skills.hpp>
#include <uniques.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;

// The synthetic proc gates, then the stock rows' SelectProc from the 1.14d data.
int main() try {
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

    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));  // SelectProc 0: no pass
    assert(!d2d::client::corpse_cursor_takes(nullptr, nullptr, corpse, type, false));
    skill.select_proc = 2;   // Raise Skeleton's
    assert(d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    type.velocity = 0;
    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));

    d2d::rules::Skill right = skill;
    right.select_proc = 1;   // Corpse Explosion's
    assert(d2d::client::corpse_cursor_takes(&skill, &right, corpse, type, false));

    skill.select_proc = 1;
    assert(d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    type.corpse_sel = false;
    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));

    type.corpse_sel = true;
    type.velocity = 8;
    skill.select_proc = 3;   // Revive's
    assert(d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    type.switch_ai = false;
    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    type.switch_ai = true;
    corpse.boss = d2d::rules::Boss::champion;
    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    corpse.boss = d2d::rules::Boss::none;
    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, true));

    skill.select_proc = 5;   // Find Item's
    assert(d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));

    corpse.corpse_used = true;
    assert(!d2d::client::corpse_cursor_takes(&skill, nullptr, corpse, type, false));
    std::puts("OK: corpse cursor selection matches FUN_00464310's player SelectProcs");

    // The stock rows (collision.md "The skill select-proc runner"): loaded, not by id.
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no D2_MPQ_DIR for the stock rows"); return D2D_TEST_SKIPPED; }
    const auto data = d2d::game::load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    const auto proc = [&](int id) { const auto* row = data->skills.get(id); assert(row); return row->select_proc; };
    assert(proc(70) == 2 && proc(80) == 2);    // Raise Skeleton, Skeletal Mage
    assert(proc(74) == 1 && proc(83) == 1);    // Corpse, Poison Explosion
    assert(proc(95) == 3);                     // Revive
    assert(proc(131) == 4 && proc(142) == 5 && proc(150) == 6);   // Find Potion, Find Item, Grim Ward
    assert(proc(0) == 0);                      // Attack
    std::puts("OK: the 1.14d Skills.txt rows carry those SelectProcs");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_corpse_cursor_select: %s\n", error.what());
    return 1;
}
