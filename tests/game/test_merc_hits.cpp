// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The merc's hits as game.exe makes them, with the game's MPQs (D2_MPQ_DIR;
// the 1.14d ones with D2_PATCH_INSTALLER): its life steal heals it with no
// LifeStealDivisor (FUN_0057c420: a hireling's leech skips the divisors,
// 0x57c58a..0x57c597).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::puts("FAIL: load_game_data failed"); return 1; }
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    auto made = new_character(*data, 0, "Archer", false, true, rng);
    Character character;
    character.character_class = 0;
    character.name = "Archer";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    // A ring on the merc: item_slow 40 and 50 % life steal (the stats alone matter).
    d2d::d2s::Item ring;
    ring.code = "rin";
    ring.location = d2d::d2s::item_location::kEquipped;
    ring.slot = 6;
    ring.props = { { d2d::d2s::kItemSlow, 0, 40 }, { d2d::d2s::kLifeDrainMinDamage, 0, 50 } };
    character.merc_items.push_back(ring);
    World world(&*data, -1, -1);
    world.enter(character);
    auto& fight = world.fight;
    auto& actor = *world.actor;
    Monster merc;
    merc.stats.hit_points = merc.hit_points = 200;
    merc.set_life_fine(50 * 256);
    merc.unit.x = actor.player.x + 1; merc.unit.y = actor.player.y;
    actor.merc = merc;
    actor.merc_st.level = 20;
    actor.merc_st.life = 200;                              // its max life: merc_max_life
    assert(fight.merc_fighter().item_slow == 40 && fight.merc_fighter().life_steal == 50);

    const auto zombie = int(data->monsters.by_id.at("zombie1"));
    d2d::rules::Rng make_rng{ 3 };
    fight.monsters.push_back(make_monster(*data, zombie, actor.player.x + 4, actor.player.y, make_rng, 0));
    auto& struck = fight.monsters.back();
    struck.stats.hit_points = struck.hit_points = 100000;
    struck.stats.armor_class = 0;
    // Its swing (merc_strike): its life steal heals it, with no LifeStealDivisor.
    for (int swing = 0; swing < 50 && actor.merc->life_fine() <= 50 * 256; ++swing) fight.merc_strike(fight.monsters.size() - 1, {}, 1000);
    assert(actor.merc->life_fine() > 50 * 256);
    std::printf("OK: the merc's swing heals it by its life steal (%d/256 -> %d/256)\n", 50 * 256, actor.merc->life_fine());
    return 0;
} catch (const std::exception& error) {
    std::printf("test_merc_hits: %s\n", error.what());
    return 1;
}
