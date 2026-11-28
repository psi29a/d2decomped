// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Concentration aura holds a swing a hit would cut (FUN_0057eec0, 0x57ef51): an interruptible skill's
// swing under its state 0x2a takes no get-hit when a rand(100) on the player's seed (+0x20) is under the
// state's stat 164 (skill_concentration, Param5: 20); without the aura every hit cuts it. Needs the 1.14d game data.

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
        auto made = new_character(data, 3, "Headless", false, true, rng);
        character.character_class = 3;
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
    auto& actor = *rig.world->actor;
    const int concentration = data->skills.by_name.at("Concentration");
    actor.swing_skill = data->skills.by_name.at("Sacrifice");
    assert(data->skills.get(actor.swing_skill)->interrupt);
    actor.pmode = kModeA1;
    fight.update_fighters(rig.now);
    assert(actor.psum[d2d::d2s::kSkillConcentration] == 0);
    for (int k = 0; k < 50; ++k) assert(fight.hit_interrupts());
    fight.skill_level = [&](int id) { return id == concentration ? 1 : 0; };
    actor.aura = concentration;
    fight.update_fighters(rig.now);
    const auto hold = actor.psum[d2d::d2s::kSkillConcentration];
    assert(hold == 20);
    int held = 0;
    for (int k = 0; k < 400; ++k) {
        auto expected = actor.seed;
        const bool cut = fight.hit_interrupts();
        assert(cut == !(std::int64_t(expected(100)) < hold));
        assert(actor.seed.low == expected.low);
        held += !cut;
    }
    std::printf("test_concentration_hold: ok (held %d of 400 at %d%%)\n", held, int(hold));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_concentration_hold: %s\n", error.what());
    return 1;
}
