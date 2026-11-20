// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of ambient_dip.hpp's functions; their notes are in the header.
#include "ambient_dip.hpp"

#include "skills.hpp"

#include <optional>

namespace d2d::rules {

std::optional<DipFrames> state_cloak_dip(const SkillTables& skill_tables, int skill_id, int level) {
    const Skill* skill = skill_tables.get(skill_id);
    if (!skill) return std::nullopt;
    return cloak_dip(eval_calc(skill_tables, skill->auralen, CalcEnv{}, skill_id, level));
}

}
