// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of ambient_dip.hpp's functions; their notes are in the header.
#include "ambient_dip.hpp"

#include "skills.hpp"

#include <optional>

namespace d2d::rules {

//   runs its setfunc, FUN_004d87e0): the state's stat list's skill (stat
//   0x15e) and level (0x15f); a skill outside Skills.txt starts none, else
//   cloak_dip of its auralencalc at that level (FUN_00646ca0), in the level
//   of the unit's room (FUN_0061a1b0).
std::optional<DipFrames> state_cloak_dip(const SkillTables& skill_tables, int skill_id, int level) {
    const Skill* skill = skill_tables.get(skill_id);
    if (!skill) return std::nullopt;
    return cloak_dip(eval_calc(skill_tables, skill->auralen, CalcEnv{}, skill_id, level));
}

}
