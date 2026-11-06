// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Through the real World tick: a ground click whose path runs into a wall ends
// on the tick it's blocked, on a subtile's centre (FUN_00650150), with no new
// path from there (FUN_00650840 stops the same frame, LAB_006508ff). Needs the data; SKIPs
// without it.

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <protocol.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace d2d::game;
namespace fs = std::filesystem;

namespace {

bool on_centre(float value) {
    const float centre = (std::floor(value * 5) + 0.5f) / 5;
    return std::abs(value - centre) < 0.001f;
}

}  // namespace

int main() {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto* actor = world.actor;
    auto& town = data->town;
    assert(world.level == &town);

    // Open ground, the player off-centre: a walk east plans its path.
    const int width = town.ds1.width() * 5, height = town.ds1.height() * 5;
    const int at_x = int(std::floor(actor->player.x * 5)), at_y = int(std::floor(actor->player.y * 5));
    assert(at_x > 10 && at_y > 10 && at_x + 40 < width && at_y + 10 < height);
    town.walk.assign(std::size_t(width) * std::size_t(height), std::uint16_t(0));
    actor->player.x = (float(at_x) + 0.2f) / 5;
    actor->player.y = (float(at_y) + 0.3f) / 5;
    std::uint32_t now = 1000 * kTickMs;
    world.apply(cmd::Move{ float(at_x + 35) / 5, float(at_y) / 5, true }, now);
    world.tick({}, now + kTickMs, now);
    now += kTickMs;
    assert(actor->player.walking && !actor->player.path.empty());

    // Then walls all round a patch 7 subtiles wide: the path runs into them, unknown to it.
    town.walk.assign(std::size_t(width) * std::size_t(height), std::uint16_t(1));
    for (int row = -3; row <= 3; ++row)
        for (int column = -3; column <= 3; ++column)
            town.walk[std::size_t(at_y + row) * std::size_t(width) + std::size_t(at_x + column)] = 0;
    bool ended = false;
    for (int tick = 0; tick < 200 && !ended; ++tick) {
        world.tick({}, now + kTickMs, now);
        now += kTickMs;
        ended = !actor->player.walking;
        assert(ended || !actor->player.path.empty());   // no path left and still walking: it would plan again
    }
    assert(ended);
    assert(actor->player.path.empty());
    assert(on_centre(actor->player.x) && on_centre(actor->player.y));
    const float stop_x = actor->player.x, stop_y = actor->player.y;
    for (int tick = 0; tick < 10; ++tick) {
        world.tick({}, now + kTickMs, now);
        now += kTickMs;
    }
    assert(!actor->player.walking && actor->player.x == stop_x && actor->player.y == stop_y);
    std::printf("ok: the walk ended on a centre\n");
    return 0;
}
