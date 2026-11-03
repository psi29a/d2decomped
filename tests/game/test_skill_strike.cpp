// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's skill strike adds the skill's to-hit to its rating as points
// (FUN_0057d9b0's monster branch, 0x57db93: stat 19 + FUN_006449f0's bonus
// + dex x 5), not as a percent of it. No game data: the skill is typed in.

#include <ai.hpp>
#include <gamedata.hpp>
#include <skills.hpp>

#include <cassert>

using namespace d2d::game;

int main() {
    const GameData game_data;
    Monster monster;
    monster.stats.to_hit = 200;
    d2d::rules::Skill skill;
    skill.tohit = 30; skill.levtohit = 10;
    assert(skill_strike(game_data, monster, skill, 3).to_hit == 200 + 30 + 2 * 10);   // not 200 x 1.5
    return 0;
}
