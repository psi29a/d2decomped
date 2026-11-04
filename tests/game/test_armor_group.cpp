// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Which self states a new one switches off, through the REAL Fight::start_state.
// `--armor-group`: one line `X Y kept` for every ordered pair of skills that give a state
// (Skills.txt aurastate), kept 1 when X's state is still on after Y's began. game.exe's answers:
// `python3 armor_group.py game`. With no argument it checks that Shiver Armor switches Frozen Armor off.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
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

    // Start X, then Y; whether X's state is still on.
    bool kept(int first, int second) {
        auto& fight = world->fight;
        world->actor->self_states.clear();
        fight.start_state(*data.skills.get(first), 1, now);
        fight.start_state(*data.skills.get(second), 1, now);
        return std::ranges::contains(world->actor->self_states, first, &Fight::Hero::SelfState::skill);
    }
};

}  // namespace

int main(int argc, char** argv) try {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (!mode.empty() && mode != "--armor-group") {
        std::printf("SKIP: modes are none and `--armor-group` (see tools/emu/armor_group.py)\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    Rig rig(*data);
    if (mode.empty()) {
        assert(rig.kept(40, 40) && !rig.kept(40, 50) && !rig.kept(50, 60));   // Frozen, Shiver, Chilling Armor: one group
        std::printf("test_armor_group: ok\n");
        return 0;
    }
    std::vector<int> giving;
    for (int id = 0; const auto* skill = data->skills.get(id); ++id)
        if (!skill->aurastate.empty() && data->states.contains(skill->aurastate)) giving.push_back(id);
    for (const int first : giving)
        for (const int second : giving) std::printf("%d %d %d\n", first, second, rig.kept(first, second) ? 1 : 0);
    return 0;
} catch (const std::exception& error) {
    std::printf("test_armor_group: %s\n", error.what());
    return 1;
}
