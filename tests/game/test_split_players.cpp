// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Fight::game_players during split-level turns (World::tick): each level's
// turn holds only its own players in `heroes`, but a monster's life and
// experience and NoDrop count every player in the game (FUN_00535790 reads
// the game's count, FUN_005538d0), or the /players count when it's larger.

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <tuple>
#include <utility>
#include <vector>

namespace filesystem = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const filesystem::path data_dir = mpq_dir ? filesystem::path(mpq_dir) : filesystem::path(home ? home : "") / "Workspace/private/diablo2";
    if (!filesystem::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? filesystem::path(patch) : filesystem::path{}, 0x1234);
    assert(data && data->town.id == 1);

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Split", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Split";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    World world(&*data, -1, -1);
    world.enter(character);
    std::uint32_t now_ms = 60 * kTickMs;
    const Level* town = world.level;
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    assert(moor);
    auto wanderer = std::move(character);
    wanderer.name = "Wanderer";
    auto& second = world.join(wanderer);
    second.on_level = moor;
    std::tie(second.player.x, second.player.y) = moor->nearest_free(float(moor->ds1.width()) / 2, float(moor->ds1.height()) / 2);
    world.lead().on_level = town;
    world.split_levels = true;

    // One player on each level: each turn's heroes are one, the game's two.
    std::vector<int> heroes, counted;
    world.turn_done = [&] { heroes.push_back(int(world.fight.heroes.size())); counted.push_back(world.fight.game_players()); };
    world.tick({}, now_ms + kTickMs, now_ms);
    now_ms += kTickMs;
    std::printf("two levels: %zu turns, heroes %d %d, counted %d %d\n", counted.size(), heroes[0], heroes[1], counted[0], counted[1]);
    std::fflush(stdout);   // seen before an assert fails
    assert(counted.size() == 2 && heroes[0] == 1 && heroes[1] == 1 && counted[0] == 2 && counted[1] == 2);
    assert(world.fight.players_elsewhere == 0 && world.fight.game_players() == 2);   // after the turns: all the heroes back

    // /players 5 is larger than the two: every turn counts 5.
    world.fight.players_setting = 5;
    heroes.clear();
    counted.clear();
    world.tick({}, now_ms + kTickMs, now_ms);
    assert(counted.size() == 2 && counted[0] == 5 && counted[1] == 5);
    world.turn_done = nullptr;

    std::printf("OK: a split-level turn counts the whole game's players\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_split_players: %s\n", error.what());
    return 1;
}
