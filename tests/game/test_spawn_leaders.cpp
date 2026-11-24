// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A spawn's leader is its placement's first in the level's spawns (objgroups.cpp); in the level's
// monster list it is where that spawn landed (Spawning::LevelState::spawn_slots), not the same
// index once another unit came first. The Den of Evil with a stray monster added after its first
// rooms: every minion (a unique's or superunique's, Corpsefire's) still leads to its boss, and a
// group's first to itself.

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    const auto* den = data->level(d2d::rules::level_ids::kDenOfEvil);
    assert(den);
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kSorceress, "Leader", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kSorceress;
    character.name = "Leader";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.arrive(den, float(den->ds1.width()) / 2, float(den->ds1.height()) / 2, "a test");
    std::uint32_t now_ms = 60 * kTickMs;
    world.tick({}, now_ms + kTickMs, now_ms);
    assert(world.fight.mon_level == den);
    // A stray unit before the rest of the rooms come up (as a quest's or a birth would be).
    auto stray = make_monster(*data, 0, 1.f, 1.f, rng, 0, true, 1, den->id);
    world.fight.add_monster(std::move(stray), den);
    for (float y = 0.5f; y < float(den->ds1.height()); y += 4.f)
        for (float x = 0.5f; x < float(den->ds1.width()); x += 4.f) world.fight.rooms_up(*den, x, y, false);
    const auto& pack = world.fight.monsters;
    int minions = 0, heads = 0;
    for (std::size_t index = 0; index < pack.size(); ++index) {
        const auto& monster = pack[index];
        if (monster.leader < 0) continue;
        assert(std::size_t(monster.leader) < pack.size());
        const auto& head = pack[std::size_t(monster.leader)];
        assert(head.leader == monster.leader);                       // its group's first leads itself
        heads += monster.leader == int(index);
        if (monster.boss == d2d::rules::Boss::minion) {
            ++minions;
            assert(head.boss == d2d::rules::Boss::unique || head.boss == d2d::rules::Boss::superunique);
        }
    }
    std::printf("%zu monsters, %d group heads, %d minions, each to its boss\n", pack.size(), heads, minions);
    assert(minions > 0);
    std::printf("test_spawn_leaders: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_spawn_leaders: %s\n", error.what());
    return 1;
}
