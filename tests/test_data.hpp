// SPDX-License-Identifier: GPL-3.0-or-later
// Made-up game data for a test that runs without the MPQs (CI has none): a
// GameData holding only the rows the test puts in. The values are invented,
// so such a test checks d2d's rule, not Blizzard's numbers; the oracles
// (tools/emu) and a local run with the game data check those. Nothing here
// comes from the game's files.
#pragma once

#include <gamedata.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace d2d::test {

struct TestData {
    d2d::game::GameData data;

    // MonStats row `row` named `id`, its BaseId `base` (-1: itself), its
    // Level / Level(N) / Level(H).
    d2d::rules::MonType& monster(int row, std::string id, int base = -1, std::array<int, 3> level = { 1, 1, 1 }) {
        auto& types = data.monsters.types;
        if (std::size_t(row) >= types.size()) types.resize(std::size_t(row) + 1);
        if (std::size_t(row) >= data.mon_npc.size()) data.mon_npc.resize(std::size_t(row) + 1);
        auto& type = types[std::size_t(row)];
        type.id = id;
        type.base = base < 0 ? row : base;
        type.level = level;
        data.monsters.by_id[std::move(id)] = row;
        return type;
    }
    // MonLvl row `level`, the same on every difficulty.
    void mon_level(int level, int life, int defence, int to_hit, int damage, int experience = 1) {
        auto& rows = data.monsters.lvl;
        if (std::size_t(level) >= rows.size()) rows.resize(std::size_t(level) + 1);
        auto& row = rows[std::size_t(level)];
        row.hit_points.fill(life); row.armor_class.fill(defence); row.to_hit.fill(to_hit); row.damage.fill(damage); row.experience.fill(experience);
    }
    // Levels.txt MonLvl1Ex..3Ex of level `level_id`.
    void area_level(int level_id, std::array<int, 3> per_difficulty) {
        auto& rows = data.area_level;
        if (std::size_t(level_id) >= rows.size()) rows.resize(std::size_t(level_id) + 1);
        rows[std::size_t(level_id)] = { per_difficulty[0], per_difficulty[1], per_difficulty[2], per_difficulty[0] };
    }
    // Skills.txt row `id` named `name`.
    d2d::rules::Skill& skill(int id, const std::string& name) {
        auto& rows = data.skills.rows;
        if (std::size_t(id) >= rows.size()) rows.resize(std::size_t(id) + 1);
        auto& row = rows[std::size_t(id)];
        row.id = id;
        data.skills.by_name[name] = id;
        return row;
    }
    // States.txt row `name`.
    d2d::game::GameData::StateInfo& state(const std::string& name, bool curse = false) {
        auto& info = data.states[name];
        info.curse = curse;
        return info;
    }
};

}  // namespace d2d::test
