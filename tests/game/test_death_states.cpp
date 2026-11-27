// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A player's death (FUN_0057f330 -> FUN_00639fb0) takes modeled player states off when States.txt does
// not mark them plrstaydeath: skill/self, aura and charge-up states; curse and mark; scalar combat states;
// and potion and shrine states. The independently living merc keeps its states. Needs 1.14d data (else SKIP).

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

using namespace d2d::game;

namespace {

struct Rig {
    const GameData& data;
    d2d::rules::Rng rng{ 7 };
    Character character;
    std::unique_ptr<World> world;
    Level open;
    std::uint32_t now = 1000 * kTickMs;

    explicit Rig(const GameData& game_data) : data(game_data) {
        auto made = new_character(data, 1, "Headless", false, true, rng);
        character.character_class = 1;
        character.name = "Headless";
        character.header = made.header;
        character.stats = made.stats;
        character.items = made.items;
        character.panel = panel_stats(data, character.header, character.items, character.stats);
        world = std::make_unique<World>(&data, -1, -1);
        world->enter(character);
        open = *data.level(d2d::rules::level_ids::kBloodMoor);
        std::ranges::fill(open.walk, std::uint16_t(0));
        world->level = &open;
        world->fight.enter(&open);
        world->fight.skill_level = [](int) { return 1; };
        world->fight.skill_base = world->fight.skill_level;
    }

    // Start the skill's state, die; whether the state is still on.
    bool kept(int skill) {
        auto& fight = world->fight;
        world->actor->self_states.clear();
        fight.start_state(*data.skills.get(skill), 1, now);
        const bool started = !world->actor->self_states.empty();
        fight.die(now);
        return started && std::ranges::contains(world->actor->self_states, skill, &Fight::Hero::SelfState::skill);
    }
};

}  // namespace

// Replay the skill-state rows and the separately modeled player-state containers
// through Fight::die (FUN_0057f330 -> FUN_00639fb0), with the merc as a control.
int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    Rig rig(*data);
    int dropped = 0, stayed = 0;
    for (int skill = 0; skill < 400; ++skill) {
        const auto* row = data->skills.get(skill);
        if (!row || row->aurastate.empty()) continue;
        const auto info = data->states.find(row->aurastate);
        if (info == data->states.end()) continue;
        rig.world->actor->self_states.clear();
        rig.world->fight.start_state(*row, 1, rig.now);
        if (rig.world->actor->self_states.empty()) continue;
        const bool still_on = rig.kept(skill);
        if (still_on != info->second.plr_stay_death) {
            std::fprintf(stderr, "skill %d state %s: kept %d, plrstaydeath %d\n", skill, row->aurastate.c_str(), still_on, info->second.plr_stay_death);
            return 1;
        }
        (still_on ? stayed : dropped)++;
    }
    const auto state_drops = [&](std::string_view name) {
        const auto found = data->states.find(std::string(name));
        assert(found != data->states.end() && found->second.row >= 0);
        return !found->second.plr_stay_death;
    };
    for (const std::string_view name : { "healthpot", "manapot", "shrine_stamina", "antidote", "thawing", "staminapot",
                                         "cold", "inferno", "stunned", "nomanaregen", "skilldelay" })
        assert(state_drops(name));

    const auto curse_skill = std::ranges::find_if(data->skills.rows, [&](const d2d::rules::Skill& skill) {
        const auto state = data->states.find(skill.auratarget);
        return state != data->states.end() && state->second.curse && !state->second.plr_stay_death;
    });
    const auto charge_skill = std::ranges::find_if(data->skills.rows, [&](const d2d::rules::Skill& skill) {
        const auto state = data->states.find(skill.aurastate);
        return skill.progressive && state != data->states.end() && !state->second.plr_stay_death;
    });
    const auto aura_skill = std::ranges::find_if(data->skills.rows, [&](const d2d::rules::Skill& skill) {
        const auto state = data->states.find(skill.aurastate);
        return skill.aura && state != data->states.end() && !state->second.plr_stay_death;
    });
    assert(curse_skill != data->skills.rows.end() && charge_skill != data->skills.rows.end() && aura_skill != data->skills.rows.end());
    auto& hero = *rig.world->actor;
    hero.aura = aura_skill->id;
    hero.curses[0] = { curse_skill->id, 1, rig.now + 10000 };
    hero.marks[0] = { curse_skill->id, 1, rig.now + 10000 };
    hero.charges.push_back({ charge_skill->id, 1, 3, rig.now + 10000 });
    hero.regen = { { 1, 0, rig.now + 10000, false }, { 0, 1, rig.now + 10000, false } };
    hero.potion_states[0] = { { { d2d::d2s::kPoisonResist, 50 } }, rig.now + 10000 };
    hero.potion_states[1] = { { { d2d::d2s::kColdResist, 50 } }, rig.now + 10000 };
    hero.stamina_pot = { 100, rig.now + 10000 };
    hero.chilled = hero.burning = hero.stunned = hero.skill_delay = rig.now + 10000;
    hero.no_mana_regen = true;
    const auto stamina_shrine = std::ranges::find(data->shrines, 14, &d2d::rules::ShrineRow::code);
    assert(stamina_shrine != data->shrines.end());
    hero.boost = Fight::Hero::Boost{ int(stamina_shrine - data->shrines.begin()), {}, rig.now + 10000, 1 };
    hero.merc.emplace();
    hero.merc->hit_points = 100;
    hero.curses[1] = { curse_skill->id, 1, rig.now + 10000 };
    hero.merc_potion_states[0] = { { { d2d::d2s::kPoisonResist, 50 } }, rig.now + 10000 };
    hero.merc_buff = { charge_skill->id, 1, rig.now + 10000 };
    hero.merc_cry = { charge_skill->id, 1, rig.now + 10000 };

    rig.world->fight.die(rig.now);
    assert(hero.curses[0].skill < 0 && hero.marks[0].skill < 0 && hero.charges.empty() && hero.regen.empty());
    assert(hero.potion_states[0].until == 0 && hero.potion_states[1].until == 0 && hero.stamina_pot.until == 0 && hero.boost.until == 0);
    assert(hero.aura == 0 && hero.chilled == 0 && hero.burning == 0 && hero.stunned == 0 && hero.skill_delay == 0 && !hero.no_mana_regen);
    assert(hero.merc && hero.merc->hit_points == 100 && hero.curses[1].skill == curse_skill->id && hero.merc_potion_states[0].until > rig.now);
    assert(hero.merc_buff.skill == charge_skill->id && hero.merc_cry.skill == charge_skill->id);
    std::printf("test_death_states: ok (%d states ended with the death, %d stayed)\n", dropped, stayed);
    assert(dropped > 0);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_death_states: %s\n", error.what());
    return 1;
}
