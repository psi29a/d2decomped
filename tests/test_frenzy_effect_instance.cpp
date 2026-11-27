// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
#include <fight.hpp>
#include <gamedata.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

int main() try {
    d2d::game::GameData data;
    data.skills.rows.resize(1);
    auto& skill = data.skills.rows[0];
    skill.id = 0;
    skill.name = "Frenzy";
    skill.auralen = d2d::rules::compile_calc("10", data.skills.names);
    d2d::game::World world(&data, -1, -1);
    world.fight.skill_level = [](int) { return 3; };
    world.fight.frenzy(skill, 100);
    const auto instance = world.actor->self_states.front().instance;
    assert(instance != 0 && world.actor->self_states.front().level == 1);
    for (const std::uint32_t now : { 200u, 300u, 400u }) {
        world.fight.frenzy(skill, now);
        assert(world.actor->self_states.size() == 1);
        const auto& state = world.actor->self_states.front();
        assert(state.instance == instance && state.until == now + 400);
        assert(state.level == (now == 200 ? 2 : 3));
        assert(world.actor->effect_serial == instance);
    }
    world.actor->self_states.clear();
    world.fight.frenzy(skill, 900);
    assert(world.actor->self_states.front().instance != instance);
    assert(world.actor->self_states.front().level == 1);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
