// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// d2d's effect dismissal is opt-in and instance-specific.
#include "skills.hpp"
#include <d2s_items.hpp>
#include <effect_policy.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <protocol.hpp>
#include <replication.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>

using namespace d2d::game;

int main() try {
    const cmd::DismissEffect request{ 1, UINT64_C(0x12345678abcdef01) };
    auto bytes = encode(request);
    const auto decoded = decode(bytes);
    assert(decoded && std::get<cmd::DismissEffect>(*decoded).instance == request.instance);
    auto invalid = bytes;
    invalid[1] = 2;
    assert(!decode(invalid));
    bytes.pop_back();
    assert(!decode(bytes));
    assert(dismissible_self_effect("Frozen Armor"));
    assert(!dismissible_self_effect("Berserk"));
    assert(!dismissible_self_effect("Werewolf"));

    Fight::Hero hero;
    const auto first = hero.next_effect_instance();
    const auto second = hero.next_effect_instance();
    assert(first != second);
    hero.effect_serial = std::numeric_limits<std::uint64_t>::max();
    assert(hero.next_effect_instance() == 0);

    World world(nullptr, -1, -1);
    world.actor->boost = { 1, {}, 200, 42 };
    world.apply(cmd::DismissEffect{ 1, 42 }, 100); // disabled by default
    assert(world.actor->boost.instance == 42);
    world.dismiss_effects = true;
    world.apply(cmd::DismissEffect{ 1, 41 }, 100); // replaced instance
    assert(world.actor->boost.instance == 42);
    world.apply(cmd::DismissEffect{ 1, 42 }, 200); // expired instance
    assert(world.actor->boost.instance == 42);

    GameData data;
    View sent;
    sent.level = &data.town;
    sent.effects.push_back({ 1, 8, UINT64_C(0x12345678abcdef01), 900, true });
    View received;
    ViewEncoder encoder;
    assert(apply_view(data, encode_view(data, sent, encoder), received));
    assert(received.effects.size() == 1 && received.effects[0].instance == sent.effects[0].instance
           && received.effects[0].remaining_ms == 900 && received.effects[0].dismissible);
    data.skills.rows.resize(5);
    data.skills.rows[0].id = 0; data.skills.rows[0].name = "Frozen Armor";
    data.skills.rows[1].id = 1; data.skills.rows[1].name = "Berserk";
    const auto set_cry = [&](int id, const char* name, int stat) {
        auto& skill = data.skills.rows[std::size_t(id)];
        skill.id = id; skill.name = name; skill.aurastat[0] = stat;
        skill.aura_calc[0] = d2d::rules::compile_calc("50", data.skills.names);
    };
    set_cry(2, "Shout", d2d::d2s::kSkillArmorPercent);
    set_cry(3, "Battle Orders", d2d::d2s::kMaxLifePercent);
    set_cry(4, "Battle Command", d2d::d2s::kAllSkills);
    data.shrines.resize(2);
    data.shrines[1].code = 8;
    World live(&data, -1, -1);
    live.actor->boost = { 1, { { 39, 20 } }, 200, 42 };
    live.apply(cmd::DismissEffect{ 1, 42 }, 100);
    assert(live.actor->boost.instance == 42); // host opt-in is required with real rules too
    live.dismiss_effects = true;
    live.apply(cmd::DismissEffect{ 1, 41 }, 100);
    live.apply(cmd::DismissEffect{ 1, 42 }, 200);
    assert(live.actor->boost.instance == 42); // stale and expired requests do not mutate
    live.apply(cmd::DismissEffect{ 1, 42 }, 100);
    assert(live.actor->boost.instance == 0);
    live.fight.add_state(data.skills.rows[0], 1, 200);
    const auto replaced = live.actor->self_states[0].instance;
    live.fight.add_state(data.skills.rows[0], 1, 200);
    assert(live.actor->self_states.size() == 1 && live.actor->self_states[0].instance != replaced);
    live.apply(cmd::DismissEffect{ 0, replaced }, 100);
    assert(live.actor->self_states.size() == 1);
    live.actor->self_states = { { 0, 1, 200, 43 }, { 1, 1, 200, 44 } };
    live.apply(cmd::DismissEffect{ 0, 44 }, 100);
    assert(live.actor->self_states.size() == 2);
    live.apply(cmd::DismissEffect{ 0, 43 }, 100);
    assert(live.actor->self_states.size() == 1 && live.actor->self_states[0].instance == 44);

    live.actor->self_states.clear();
    live.actor->character.stats.values[d2d::d2s::kMaxLife] = 100 * 256;
    live.actor->character.stats.values[d2d::d2s::kLife] = 100 * 256;
    live.actor->self_states = { { 2, 1, 200, 45 }, { 3, 1, 200, 46 }, { 4, 1, 200, 47 } };
    live.fight.update_fighters(100);
    assert(live.actor->bo_life == 50 * 256);
    assert(live.actor->psum[d2d::d2s::kSkillArmorPercent] == 50);
    assert(live.actor->psum[d2d::d2s::kAllSkills] == 50);
    live.apply(cmd::DismissEffect{ 0, 46 }, 100);
    assert(live.actor->bo_life == 0);
    assert(live.actor->character.stats.values[d2d::d2s::kMaxLife] == 100 * 256);
    assert(live.actor->character.stats.values[d2d::d2s::kLife] <= 100 * 256);
    live.apply(cmd::DismissEffect{ 0, 45 }, 100);
    assert(live.actor->psum[d2d::d2s::kSkillArmorPercent] == 0);
    live.apply(cmd::DismissEffect{ 0, 47 }, 100);
    assert(live.actor->psum[d2d::d2s::kAllSkills] == 0);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
