// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A caged Act 5 prisoner (Wussie, FUN_005ee3c0) with a player by and its
// door whole cries: rand(1000) < 100 on its seed sends S->C 0x8a (0x5ee579),
// whose client plays wussie_help_me on it (FUN_004b3380, 0x4b33c0: 0x11ff).
// Each think draws once; the cry is that draw under 100. Caged, it's in no
// target list, so an evil monster beside it doesn't take it; with its door
// broken its think puts it in list 8 (FUN_005b1990, 0x5ee549) and one does.
// Its portal open and 4 or more off, 70 % it walks at it (FUN_005dec80 flags
// 7); walled in, it can't set off: sight forced, then 70 % wanders 4, else
// stands 10 (walk_failed), each on its seed. Needs the data; SKIPs without it.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
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

    // An evil zombie in melee of it, the player far off: lists 0..7 have no one.
    const auto zombie = data->monsters.by_id.find("zombie1");
    assert(zombie != data->monsters.by_id.end());
    std::vector<Monster> fight{ make_monster(*data, int(zombie->second), 8.3f, 8.1f, spawn, 0), prisoner };
    fight[1].listed = false;
    std::array<Foe, 2> foes{};
    foes[0].x = 1.1f; foes[0].y = 1.1f;                           // the player, 50 subtiles off
    foes[1].x = fight[1].unit.x; foes[1].y = fight[1].unit.y; foes[1].of = &fight[1]; foes[1].id = fight[1].id; foes[1].size = 1;
    fight[0].aware = true;
    monster_update(*data, here, fight[0], foes, rng, 200000, 0.04f, crowd, shots, fight);
    assert(fight[0].chase != 1);                                  // caged: no target
    fight[1].wussie.door_broken = true;
    fight[1].next_act = 0;
    monster_update(*data, here, fight[1], foes, rng, 200040, 0.04f, crowd, shots, fight);
    assert(fight[1].listed && fight[1].wussie.open_portal);
    fight[0].next_act = 0;
    for (std::uint32_t now = 200080; now < 200080 + 40 * 40 && fight[0].chase != 1; now += 40)
        monster_update(*data, here, fight[0], foes, rng, now, 0.04f, crowd, shots, fight);
    assert(fight[0].chase == 1);                                  // in list 8: taken

    // Walled in on its subtile (40, 40), its portal 10 east.
    for (int at_y = 39; at_y <= 41; ++at_y)
        for (int at_x = 39; at_x <= 41; ++at_x)
            if (at_x != 40 || at_y != 40) here.walk[std::size_t(at_y) * 80 + std::size_t(at_x)] = 0x0001;
    std::vector<Monster> caged{ make_monster(*data, d2d::rules::RescueQuest::kPrisoner, 8.1f, 8.1f, spawn, 0) };
    auto& walled = caged[0];
    walled.aware = true;
    walled.wussie.portal = true; walled.wussie.portal_x = 50; walled.wussie.portal_y = 40;
    int stuck = 0;
    for (int think = 0; think < 100; ++think) {
        auto draw = walled.seed;
        const bool walks = draw(100) < 70;
        const auto failed = d2d::rules::walk_failed(draw);
        walled.mode = "NU"; walled.next_act = 0; walled.force_sight = false;
        walled.unit.x = 8.1f; walled.unit.y = 8.1f;
        const std::uint32_t now = 400000 + std::uint32_t(think) * 40 * 30;
        monster_update(*data, here, walled, player, rng, now, 0.04f, crowd, shots, caged);
        if (!walks) continue;
        ++stuck;
        assert(walled.force_sight);
        if (failed.act == d2d::rules::MonAct::idle) assert(walled.mode == "NU" && walled.next_act == now + 10 * 40);
    }
    assert(stuck > 50);
    std::printf("OK: a caged prisoner cries on 1 draw in 10 (%d of 200); listed once its door's broken; walled in it can't walk at its portal\n", cries);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_prisoner_cry: %s\n", error.what());
    return 1;
}
