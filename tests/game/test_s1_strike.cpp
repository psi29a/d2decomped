// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Fire Hit's and Jab's hit (FUN_005cae40, FUN_005db2d0): the swing's S1
// damage, to-hit and elements, plus the skill's to-hit (FUN_006449f0) as
// points. Fire Hit's row: ToHit 20, LevToHit 10.

#include <ai.hpp>
#include <skills.hpp>

#include <cassert>

using namespace d2d::game;

int main() {
    d2d::rules::MonStats stats;
    stats.a1_min = 1; stats.a1_max = 2; stats.s1_min = 8; stats.s1_max = 12; stats.to_hit = 50; stats.to_hit_s1 = 90;
    stats.elements[0] = { 0, 100, 3, 4, 0, "S1" };
    stats.elements[1] = { 1, 100, 3, 4, 0, "A1" };
    const d2d::rules::SkillTables tables;
    d2d::rules::Skill fire_hit;
    fire_hit.tohit = 20; fire_hit.levtohit = 10;
    s1_strike(stats, tables, fire_hit, 4);
    assert(stats.a1_min == 8 && stats.a1_max == 12 && stats.to_hit == 90 + 20 + 3 * 10);
    assert(stats.elements[0].mode == "A1" && stats.elements[1].mode.empty());
    return 0;
}
