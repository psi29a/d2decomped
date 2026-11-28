// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A weapon missile's bypass (FUN_0064b860 0x64bcbb..: stats 103 / 104 / 106 onto its record's flags on the
// SrcDamage path) takes every field of the record in FUN_0057c1e0, the skill's elements as the weapon's: a Fire
// Arrow with skill_bypass_undead into an undead that resists fire wholly still does its fire. Needs the 1.14d
// game data.

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    const int skeleton = int(data->monsters.by_id.at("skeleton1"));
    for (auto& per_difficulty : data->monsters.types[std::size_t(skeleton)].diff) per_difficulty.res = { 0, 0, 100, 0, 0, 0 };   // fire immune
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kAmazon, "Arrows", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kAmazon;
    character.name = "Arrows";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world.level = &open;
    world.fight.enter(&open);
    const auto& fire_arrow = *data->skills.get(data->skills.by_name.at("Fire Arrow"));
    world.fight.skill_level = [&](int id) { return id == fire_arrow.id ? 5 : 0; };
    world.fight.skill_base = world.fight.skill_level;
    std::uint32_t now = 40000;
    world.tick({}, now, now - 40);
    world.fight.monsters.clear();
    auto& actor = *world.actor;
    d2d::rules::Rng make_rng{ 3 };
    world.fight.monsters.push_back(make_monster(*data, skeleton, actor.player.x + 2, actor.player.y, make_rng, 0));
    auto& target = world.fight.monsters.front();
    target.hit_points = target.stats.hit_points = 100000;
    const auto* row = &data->missiles.at(fire_arrow.srvmissile);
    assert(row && fire_arrow.srcdam > 0);           // its record takes the weapon (SrcDam 128): the bypass path
    const auto took = [&](int bypass, std::uint32_t seed) {
        actor.player_combat.bypass = bypass;
        actor.seed = d2d::rules::Rng{ seed };
        target.seed = d2d::rules::Rng{ seed + 1 };
        target.hit_points = 100000; target.life_frac = 0;
        Missile arrow{ row, target.unit.x, target.unit.y, 0, 0, 0, now, now + 400, {} };
        arrow.friendly = true; arrow.caster = actor.id; arrow.skill = fire_arrow.id; arrow.level = 5;
        arrow.seed = d2d::rules::Rng{ seed + 2 };
        world.fight.strike(arrow, 0, now);
        return 100000 - target.hit_points;
    };
    long plain = 0, bypassed = 0;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) { plain += took(0, seed); bypassed += took(0x100, seed); }
    std::printf("Fire Arrow 5 into a fire-immune skeleton, 200 shots: %ld, %ld with bypass undead\n", plain, bypassed);
    assert(bypassed > plain);                           // the fire counted past the immunity
    std::puts("test_missile_bypass: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_missile_bypass: %s\n", error.what());
    return 1;
}
