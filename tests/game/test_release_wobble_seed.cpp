// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A charge release's second-scan ring (do 143, FUN_005d4f40 -> FUN_005d4870): each
// missile's path callback (FUN_005d4680) lays its wobbling route (rules::wobble_route) on that missile's own seed
// (unit +0x20), which launch takes off the game seed as the missile is made. Royal Strike's second charge
// (royalstrikechainlightning). The REAL Fight::prg; the expected routes
// replay the game seed. Needs the game data; SKIPs without it.
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <missiles.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kAssassin, "Knuckles", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kAssassin;
    character.name = "Knuckles";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    auto world = std::make_unique<World>(&*data, -1, -1);
    world->enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    auto& fight = world->fight;
    const auto& royal = *data->skills.get(data->skills.by_name.at("Royal Strike"));
    const float target_x = world->actor->player.x + 3.f, target_y = world->actor->player.y;
    auto replay = fight.spawning.game;                                 // the seeds launch will take, in order
    fight.pending.clear();
    fight.prg(royal, d2d::rules::ServerDoFunction::kReleaseMissileAtUnitsSecondScan, 2, 5, target_x, target_y, 1000 * kTickMs);
    assert(!fight.pending.empty());
    const int from_x = open.world_x * 5 + int(std::floor(target_x * 5)), from_y = open.world_y * 5 + int(std::floor(target_y * 5));
    for (const auto& missile : fight.pending) {
        assert(missile.aim_at && !missile.route.empty());
        const int offset_x = int(std::lround((missile.aim_at->first - target_x) * 5)), offset_y = int(std::lround((missile.aim_at->second - target_y) * 5));
        d2d::rules::Rng own{ replay.next() };
        const int range = std::min(missile.info->range + missile.info->lev_range * 5, 77);
        assert(missile.route == d2d::rules::wobble_route(from_x, from_y, from_x + offset_x, from_y + offset_y, range, own));
        assert(missile.seed && missile.seed->low == own.low && missile.seed->high == own.high);   // the route's draws stepped its seed
    }
    std::printf("OK: %zu routes laid on their missiles' own seeds\n", fight.pending.size());
    return 0;
} catch (const std::exception& error) {
    std::printf("test_release_wobble_seed: %s\n", error.what());
    return 1;
}
