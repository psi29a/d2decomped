// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A player's death (FUN_0057f330 -> FUN_00639fb0) takes off the skill states States.txt does not mark
// plrstaydeath, through the REAL Fight::die: every skill with an aurastate is started, the player dies,
// and its state is still on exactly when plrstaydeath is 1. Needs the 1.14d data (else SKIP).

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
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
    std::printf("test_death_states: ok (%d states ended with the death, %d stayed)\n", dropped, stayed);
    assert(dropped > 0);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_death_states: %s\n", error.what());
    return 1;
}
