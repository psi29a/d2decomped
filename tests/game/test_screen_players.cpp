// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// World::screen_players: a LAN joiner (own_screen, session/lan.cpp) plays on its own
// machine, so the host's screen isn't local co-op's. ClientSession::coop_partners reads
// it: two or more hide the HUD and fill the Party screen with the screen's players.

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>

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
    auto made = new_character(*data, kSorceress, "Host", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Host";
    character.header = std::move(made.header);
    character.stats = std::move(made.stats);
    character.items = std::move(made.items);
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    World world(&*data, -1, -1);
    world.enter(character);
    assert(world.screen_players() == 1);

    // A LAN joiner: two players in the game, one on the host's screen.
    auto joiner = character;
    joiner.name = "Joiner";
    world.join(joiner).own_screen = true;
    std::printf("host + LAN joiner: %zu players, %d on the screen\n", world.players.size(), world.screen_players());
    assert(world.players.size() == 2 && world.screen_players() == 1);

    // A pad partner (local co-op) shares the screen.
    auto partner = std::move(character);
    partner.name = "Partner";
    world.join(partner);
    assert(world.players.size() == 3 && world.screen_players() == 2);

    std::printf("OK: a LAN joiner isn't on the host's screen\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_screen_players: %s\n", error.what());
    return 1;
}
