// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_005fd470, a skill usable, where a think asks it: a Claw Viper's
// Charge (do 67) needs a clear line to its target (FUN_00645910, 0xc01),
// so a wall between them stops the charge; a Fallen Shaman's Resurrect
// (TgtPlaceCheck) is asked after its roll. Needs the data; SKIPs without it.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <montypes.hpp>
#include <rules.hpp>

#include <algorithm>
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
    // The rules: the Shaman's Resurrect only when usable.
    d2d::rules::ThinkIn shaman;
    std::array<int, 8> aip{ 100, 0, 0, 0, 0, 0, 0, 0 };
    shaman.aip = aip;
    shaman.corpse = true;
    shaman.skill = { true, true, false };
    bool rally = false;
    shaman.rally = &rally;
    d2d::rules::Rng roll(7);
    auto away = [](int, bool) { return false; };
    assert(d2d::rules::mon_think("FallenShaman", shaman, roll, away).act == d2d::rules::MonAct::skill);
    shaman.usable = false;
    assert(d2d::rules::mon_think("FallenShaman", shaman, roll, away).act != d2d::rules::MonAct::skill);

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's MonStats: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const auto viper = data->monsters.by_id.find("clawviper1");
    assert(viper != data->monsters.by_id.end());
    auto& body = const_cast<d2d::rules::MonType&>(data->monsters.types[viper->second]);
    body.diff[0].aip[0] = 100;                                    // it always charges when it can
    body.diff[0].aip[1] = 60;
    Level here;
    here.id = d2d::rules::level_ids::kBloodMoor;
    here.ds1 = d2d::ds1::Map(16, 16, 1, 1);                       // 80 x 80 subtiles
    here.walk.assign(80 * 80, 0);
    const Crowd crowd;
    std::vector<Missile> shots;
    std::array<Foe, 1> player{};
    player[0].x = 6.1f; player[0].y = 8.1f;                       // 15 subtiles east
    d2d::rules::Rng rng(7);
    const auto charges = [&] {
        d2d::rules::Rng spawn{ 0x5eed };
        std::vector<Monster> pack{ make_monster(*data, int(viper->second), 3.1f, 8.1f, spawn, 0) };
        pack[0].aware = true;
        monster_update(*data, here, pack[0], player, rng, 40000, 0.04f, crowd, shots, pack);
        return pack[0].charging >= 0;
    };
    assert(charges());
    for (int at_y = 0; at_y < 80; ++at_y) here.walk[std::size_t(at_y) * 80 + 22] = 0x0400;   // a wall Charge's line meets (0xc01)
    assert(!charges());

    // A Fetish Shaman finds a fetish corpse (FUN_005f99a0) and raises it; none, it stands.
    const auto shaman_row = data->monsters.by_id.find("fetishshaman1");
    const auto fetish_row = data->monsters.by_id.find("fetish1");
    assert(shaman_row != data->monsters.by_id.end() && fetish_row != data->monsters.by_id.end());
    auto& shaman_body = const_cast<d2d::rules::MonType&>(data->monsters.types[shaman_row->second]);
    shaman_body.diff[0].aip = { 100, 1, 100, 0, 20, 0, 0, 0 };    // always, any group, raise within 100, reach 20
    Level moor = *data->level(d2d::rules::level_ids::kBloodMoor);   // its rooms: the search walks the near ones
    std::ranges::fill(moor.walk, std::uint16_t(0));
    const float room_x = float(moor.rooms[0].x) + 2.1f, room_y = float(moor.rooms[0].y) + 2.1f;
    std::array<Foe, 1> far_player{};
    far_player[0].x = room_x + 6.f; far_player[0].y = room_y;     // 30 subtiles east: no Inferno
    const auto raises = [&](bool with_corpse) {
        d2d::rules::Rng spawn{ 0x5eed };
        std::vector<Monster> pack{ make_monster(*data, int(shaman_row->second), room_x, room_y, spawn, 0) };
        if (with_corpse) {
            pack.push_back(make_monster(*data, int(fetish_row->second), room_x, room_y + 1.f, spawn, 0));   // 5 subtiles south
            pack.back().hit_points = 0;
            pack.back().mode = "DD";
        }
        pack[0].aware = true;
        monster_update(*data, moor, pack[0], far_player, rng, 40000, 0.04f, crowd, shots, pack);
        return pack[0].mode != "NU";
    };
    assert(!raises(false) && raises(true));
    std::puts("OK: Charge needs a clear line (FUN_005fd470); Resurrect is asked after the roll; a Fetish Shaman raises its kin");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_skill_usable: %s\n", error.what());
    return 1;
}
