// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Baal on his throne (BaalThrone, FUN_005ef320) with no monster about casts
// Baal Corpse Explode (skill 285) in S3; its do-func (srvdofunc 141,
// FUN_005d2e80) spends the corpses on S3's action frame, as every plain
// cast's (FUN_0056f7f0), not as the cast starts, and he taunts (unit event
// 0x10). With a monster about he casts Skill1 (Decrepify) in S3 at the
// target on seed(100) < aip1, else stands 10; not at all, and with no roll,
// at a target already in its auratargetstate. Needs the data; SKIPs without
// it.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
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
#include <iterator>
#include <string_view>
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
    const auto& types = data->monsters.types;
    const auto throne = std::ranges::find(types, std::string_view("BaalThrone"), &d2d::rules::MonType::ai_name);
    assert(throne != types.end());
    const auto zombie = data->monsters.by_id.find("zombie1");
    assert(zombie != data->monsters.by_id.end());
    Level here;
    here.id = d2d::rules::level_ids::kThroneOfDestruction;
    here.ds1 = d2d::ds1::Map(16, 16, 1, 1);                       // 80 x 80 subtiles
    here.walk.assign(80 * 80, 0);
    const Crowd crowd;
    std::vector<Missile> shots;
    std::array<Foe, 1> player{};
    player[0].x = 4.1f; player[0].y = 8.1f;
    d2d::rules::Rng rng(7);
    d2d::rules::Rng spawn{ 0x5eed };
    std::vector<Monster> pack{ make_monster(*data, int(std::distance(types.begin(), throne)), 8.1f, 8.1f, spawn, 0),
                               make_monster(*data, int(zombie->second), 10.1f, 8.1f, spawn, 0) };
    auto& baal = pack[0];
    auto& corpse = pack[1];
    corpse.hit_points = 0;
    corpse.mode = "DD";                                           // 10 subtiles east: in the 40
    baal.aware = true;
    constexpr std::uint32_t kStart = 40000;
    monster_update(*data, here, baal, player, rng, kStart, 0.04f, crowd, shots, pack);
    assert(baal.mode == "S3" && baal.skill == data->skills.by_name.at("Baal Corpse Explode"));
    assert(baal.taunt_sound);
    assert(!corpse.corpse_used);                                  // not as the cast starts
    for (std::uint32_t now = kStart + 40; now < baal.mode_until && !corpse.corpse_used; now += 40)
        monster_update(*data, here, baal, player, rng, now, 0.04f, crowd, shots, pack);
    assert(corpse.corpse_used);                                   // on its action frame

    // A wave about: Decrepify at the player, or stand 10.
    const int decrepify = data->skills.by_name.at("Decrepify");
    const int aip1 = throne->diff[0].aip[0];
    corpse.hit_points = corpse.stats.hit_points;
    corpse.mode = "NU";
    corpse.corpse_used = false;
    int casts = 0, stands = 0;
    for (std::uint32_t now = kStart + 400 * 40, thinks = 0; thinks < 200; now += 40) {
        baal.mode = "NU"; baal.mode_until = baal.next_act = now; baal.ai_state3 = 0;
        d2d::rules::Rng roll = baal.seed;
        monster_update(*data, here, baal, player, rng, now, 0.04f, crowd, shots, pack);
        ++thinks;
        const bool cast = roll(100) < aip1;
        assert(cast == (baal.mode == "S3"));
        if (cast) { assert(baal.skill == decrepify && baal.chase == 0); ++casts; }
        else { assert(baal.next_act == now + 10 * 40); ++stands; }
    }
    assert(casts > 0 && stands > 0);
    player[0].states = { "decrepify" };
    for (std::uint32_t now = kStart + 800 * 40, thinks = 0; thinks < 50; now += 40, ++thinks) {
        baal.mode = "NU"; baal.mode_until = baal.next_act = now;
        const auto before = baal.seed.low;
        monster_update(*data, here, baal, player, rng, now, 0.04f, crowd, shots, pack);
        assert(baal.mode == "NU" && baal.seed.low == before);
    }
    std::puts("OK: Baal's corpses are spent on S3's action frame; he decrepifies the target while a wave fights");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_baal_corpse_cast: %s\n", error.what());
    return 1;
}
