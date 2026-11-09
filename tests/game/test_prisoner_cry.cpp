// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A caged Act 5 prisoner (Wussie, FUN_005ee3c0) with a player by and its
// door whole cries: rand(1000) < 100 on its seed sends S->C 0x8a (0x5ee579),
// whose client plays wussie_help_me on it (FUN_004b3380, 0x4b33c0: 0x11ff).
// Each think draws once; the cry is that draw under 100. Needs the data;
// SKIPs without it.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <vector>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's MonStats: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    assert(std::size_t(d2d::rules::RescueQuest::kPrisoner) < data->monsters.types.size());
    assert(data->monsters.types[std::size_t(d2d::rules::RescueQuest::kPrisoner)].ai_name == "Wussie");
    assert(data->sound_index.at("wussie_help_me") == d2d::rules::RescueQuest::kHelpMe);
    Level here;
    here.id = d2d::rules::level_ids::kArreatPlateau;
    here.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    here.walk.assign(80 * 80, 0);
    const Crowd crowd;
    std::vector<Missile> shots;
    std::array<Foe, 1> player{};
    player[0].x = 6.1f; player[0].y = 8.1f;
    d2d::rules::Rng rng(7);
    d2d::rules::Rng spawn{ 0x5eed };
    std::vector<Monster> pack{ make_monster(*data, d2d::rules::RescueQuest::kPrisoner, 8.1f, 8.1f, spawn, 0) };
    auto& prisoner = pack[0];
    prisoner.aware = true;
    prisoner.wussie.player = true;                                // a player within 16, its door whole
    int cries = 0;
    for (int think = 0; think < 200; ++think) {
        auto draw = prisoner.seed;
        const bool cry = draw(1000) < 100;
        prisoner.next_act = 0;
        monster_update(*data, here, prisoner, player, rng, 40000 + std::uint32_t(think) * 40 * 30, 0.04f, crowd, shots, pack);
        assert(prisoner.wussie.cried && prisoner.help_sound == cry);
        cries += cry;
        prisoner.help_sound = false; prisoner.wussie.cried = false;
    }
    assert(cries > 5 && cries < 40);

    std::printf("OK: a caged prisoner cries on 1 draw in 10 (%d of 200)\n", cries);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_prisoner_cry: %s\n", error.what());
    return 1;
}
