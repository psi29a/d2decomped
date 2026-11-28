// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// curse_resistance (stat 0x6d, Fade's aurastat): a curse state put on the player (FUN_0056e970, a curse
// by FUN_0063a420) is none at 100 or more, else its length less that % of it (d2d_rules_mul_div). Fade's
// state gives the stat. Needs the 1.14d game data.

#include <ai/units.hpp>
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
#include <vector>

using namespace d2d::game;

namespace fs = std::filesystem;

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
    auto& fight = rig.world->fight;
    const int amplify = data->skills.by_name.at("Amplify Damage");
    const Foe::Cast cast{ .skill = amplify, .level = 3 };
    const auto plain = fight.monster_curse(cast, rig.now);
    const auto cut = fight.monster_curse(cast, rig.now, 40);
    assert(plain.skill == amplify && cut.skill == amplify);
    const std::uint32_t frames = (plain.until - rig.now) / 40;
    assert((cut.until - rig.now) / 40 == frames - frames * 40 / 100);
    assert(fight.monster_curse(cast, rig.now, 100).skill < 0);
    std::printf("Amplify Damage at 3: %u frames, %u at 40%% curse resistance, none at 100%%\n", frames, (cut.until - rig.now) / 40);
    // Fade's state gives it.
    const auto& fade = *data->skills.get(data->skills.by_name.at("Fade"));
    fight.start_state(fade, 5, rig.now);
    fight.update_fighters(rig.now);
    assert(rig.world->actor->psum[d2d::d2s::kCurseResistance] > 0);
    std::printf("test_curse_resistance: ok (Fade 5: %d%%)\n", int(rig.world->actor->psum[d2d::d2s::kCurseResistance]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_curse_resistance: %s\n", error.what());
    return 1;
}
