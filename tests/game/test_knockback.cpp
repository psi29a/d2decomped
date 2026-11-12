// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's knockback: LAB_005a77d0 takes one with MonStats2 mGH and mKB
// (FUN_0046c140 bits 3 / 13) into KB, drawn and timed by its GH COF
// (MonMode.txt's token: no monster has a KB one). Through the REAL Fight in
// the Blood Moor; needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    constexpr int kBarbarian = 4;
    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, kBarbarian, "Headless", false, true, made_rng);
    Character character;
    character.character_class = kBarbarian;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.level = data->level(2);                                         // the Blood Moor
    assert(world.level);
    auto& fight = world.fight;
    auto& hero = *world.actor;
    // A way one cell clear of the hero: the knockback's direction.
    std::pair<float, float> away{};
    for (const auto& [step_x, step_y] : { std::pair{ 1.f, 0.f }, { -1.f, 0.f }, { 0.f, 1.f }, { 0.f, -1.f } })
        if (!world.level->unit_blocked(hero.player.x + step_x, hero.player.y + step_y, hero.player.shape)
            && !world.level->unit_blocked(hero.player.x + 2 * step_x, hero.player.y + 2 * step_y, hero.player.shape)) { away = { step_x, step_y }; break; }
    assert(away.first != 0 || away.second != 0);

    d2d::rules::Rng rng{ 3 };
    const auto knock = [&](std::string_view type_id) -> Monster& {
        const auto type = data->monsters.by_id.find(std::string(type_id));
        assert(type != data->monsters.by_id.end());
        Monster made = make_monster(*data, type->second, 10, 10, rng, 0);
        made.unit.x = hero.player.x + away.first * 0.6f; made.unit.y = hero.player.y + away.second * 0.6f;
        fight.monsters.clear();
        fight.add_monster(Monster(made));
        fight.knock_back(fight.monsters.back(), hero.player.x, hero.player.y, 1000);
        return fight.monsters.back();
    };
    for (const std::string_view type_id : { "zombie1", "fallen1", "skeleton1" }) {
        auto& monster = knock(type_id);
        assert(data->monsters.types[std::size_t(monster.type)].can_knock_back && monster.mode == "KB");
        std::fprintf(stderr, "%s KB: %u ms\n", std::string(type_id).c_str(), monster.mode_until - 1000);
        assert(monster.mode_until > 1000);                                // the GH COF's length
    }
    const auto unknocked = std::ranges::find_if(data->monsters.types, [](const auto& type) { return type.can_get_hit && !type.can_knock_back; });
    assert(unknocked != data->monsters.types.end());
    assert(knock(unknocked->id).mode != "KB");
    std::puts("OK: a monster with mGH and mKB is knocked back, drawn by its GH COF");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_knockback: %s\n", error.what());
    return 1;
}
