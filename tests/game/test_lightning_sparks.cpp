// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Client do 8 (FUN_004d38d0): a lightning missile lays CltParam1 client
// missiles of row CltSubMissile1 each client frame from age InitSteps (+0x135)
// while its last step entered a new subtile (path flag 8: FUN_00650150), each
// at motion z rand(CltParam2) - CltParam2 / 2 and frame rand(AnimLen)
// (FUN_004cc870), living its Range (d2d::game::SparkTrail). Then, with the
// game data, the rows that carry it (lightningbolt, chainlightning ->
// lightninghit, loaded though no skill names it).

#include <client_sparks.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

void synthetic() {
    GameData::MissileInfo hit;                  // lightninghit: CltParam2 16, AnimLen 8, AnimSpeed 16, Range 12
    hit.clt_params[1] = 16; hit.anim_len = 8; hit.anim_speed = 16; hit.range = 12;
    GameData::MissileInfo bolt;                 // lightningbolt: client do 8, CltParam1 3
    bolt.clt_do = 8; bolt.clt_params[0] = 3; bolt.clt_sub_info = &hit;
    GameData::MissileInfo late = bolt;          // InitSteps 2 (mephlight's)
    late.init_steps = 2;
    GameData::MissileInfo plain = bolt;         // another client do: nothing
    plain.clt_do = 1;
    GameData::MissileInfo single = bolt;        // CltParam1 0: still one (FUN_004d38d0: at least 1)
    single.clt_params[0] = 0;

    SparkTrail trail;
    trail.rng = d2d::rules::Rng(5);
    const std::uint32_t born = 4000;
    // Each a frame on, x in cells: subtile floor(x * 5). The bolt enters a new
    // subtile at frames 1 and 3 and stays in its own at frame 2.
    const auto frame_of = [&](float bolt_x, float other_x) {
        return std::vector<View::Shot>{ { &bolt, bolt_x, 20.f, 17, born }, { &late, other_x, 4.f, 0, born },
                                        { &plain, other_x, 1.f, 0, born }, { &single, other_x, 7.f, 0, born } };
    };
    trail.tick(frame_of(10.f, 3.f), born);              // age 0, before any step: nothing (path flag 8 down)
    assert(trail.sparks.empty());
    trail.tick(frame_of(10.27f, 3.27f), born + 40);     // stepped: bolt 3, single 1; late waits for InitSteps
    assert(trail.sparks.size() == 4);
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& spark = trail.sparks[i].shot;
        assert(spark.info == &hit && spark.x == 10.27f && spark.y == 20.f && spark.dir == 17);
        assert(spark.motion_z >= -8 && spark.motion_z <= 7);
        assert((born + 40 - spark.born) % 40 == 0 && born + 40 - spark.born <= 7 * 40);
        assert(trail.sparks[i].dies == born + 40 + 12 * 40);
    }
    assert(trail.sparks[3].shot.y == 7.f);

    // The same rolls by hand: z then frame per spark, one rng.
    d2d::rules::Rng again(5);
    for (std::size_t i = 0; i < 3; ++i) {
        const int z_offset = again(16) - 8, frame = again(8);
        assert(trail.sparks[i].shot.motion_z == z_offset && trail.sparks[i].shot.born == born + 40 - std::uint32_t(frame) * 40);
    }

    trail.tick(frame_of(10.3f, 3.5f), born + 80);       // the bolt in the same subtile: none; late (age 2) 3, single 1
    assert(trail.sparks.size() == 4 + 3 + 1);
    assert(std::ranges::count_if(trail.sparks, [](const ClientSpark& spark) { return spark.shot.x == 10.3f; }) == 0);
    assert(std::ranges::count_if(trail.sparks, [](const ClientSpark& spark) { return spark.shot.y == 4.f; }) == 3);
    trail.tick(frame_of(10.5f, 3.5f), born + 120);      // the bolt on again: 3; the others stood still
    assert(trail.sparks.size() == 4 + 3 + 1 + 3);

    trail.tick({}, born + 40 + 12 * 40 - 1);            // not yet spent
    assert(trail.sparks.size() == 11);
    trail.tick({}, born + 40 + 12 * 40);                // frame 1's go
    assert(trail.sparks.size() == 7);
    trail.tick({}, born + 120 + 12 * 40);
    assert(trail.sparks.empty() && trail.seen.empty());

    // A bolt a subtile a frame: every z in -8..7 and every frame 0..7 comes up.
    std::array<int, 16> z_seen{}; std::array<int, 8> frames_seen{};
    for (std::uint32_t tick = 0; tick < 400; ++tick) {
        trail.sparks.clear();
        trail.tick(std::vector<View::Shot>{ { &bolt, float(tick) / 5 + 0.1f, 0.1f, 0, 0 } }, tick * 40);
        assert(trail.sparks.size() == (tick == 0 ? 0u : 3u));
        for (const auto& spark : trail.sparks) { ++z_seen[std::size_t(spark.shot.motion_z + 8)]; ++frames_seen[(tick * 40 - spark.shot.born) / 40]; }
    }
    assert(std::ranges::all_of(z_seen, [](int seen) { return seen > 0; }) && std::ranges::all_of(frames_seen, [](int seen) { return seen > 0; }));
}

}  // namespace

int main() try {
    synthetic();

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: these rows are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    // The rows: Missiles.txt's CltSubMissile1, CltParam1/2 and InitSteps.
    const auto& hit = data->missiles.at("lightninghit");
    assert(hit.clt_params[1] == 16 && hit.anim_len == 8 && hit.range == 12 && hit.cel_file == "LightningStrike");
    for (const char* name : { "lightningbolt", "chainlightning", "sentrylightningbolt" }) {
        const auto& row = data->missiles.at(name);
        assert(row.clt_do == 8 && row.clt_sub_info == &hit && row.clt_params[0] == 3 && row.init_steps == 0);
    }

    std::printf("ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
}
