// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// UX regression: controller release is a stop, not a new walk to a subtile.
// This is not an original-game parity claim.
#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <protocol.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <variant>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
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

    const int width = town.ds1.width() * 5, height = town.ds1.height() * 5;
    town.walk.assign(std::size_t(width) * std::size_t(height), std::uint16_t(0));
    std::uint32_t now = 40000;
    world.apply(cmd::Move{ actor->player.x + 5, actor->player.y, true }, now);
    for (int frame = 0; frame < 8; ++frame) {
        world.tick({}, now + kTickMs, now);
        now += kTickMs;
    }
    const float stop_x = actor->player.x, stop_y = actor->player.y;
    const int facing = actor->player.dir;
    world.apply(cmd::Move{ stop_x, stop_y, true }, now);
    for (int frame = 0; frame < 10; ++frame) {
        world.tick({}, now + kTickMs, now);
        now += kTickMs;
        assert(!actor->player.walking && actor->player.path.empty());
        assert(actor->player.x == stop_x && actor->player.y == stop_y);
        assert(actor->player.dir == facing);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_pad_stop: %s\n", error.what());
    return 1;
}
