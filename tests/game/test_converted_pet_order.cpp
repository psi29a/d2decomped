// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster Conversion turned (FUN_005d0350 at 0x5d0501: FUN_005b1900(game,
// monster, 0, the caster's list)) enters the caster's list as its newest, as a
// summon does, so a hostile monster's search (FUN_005dd7f0, a tie to the
// first tried) takes it before a pet that entered earlier and after one that
// entered later. The REAL Fight::summon_one, Fight::turn_good and
// Fight::world (foes: player, merc, summons, then the monsters). Needs the
// game data; SKIPs without it.
#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    auto loaded = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!loaded || !loaded->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    const auto& data = *loaded;
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, 2, "Gravedigger", false, true, rng);
    character.name = "Gravedigger";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(data, character.header, character.items, character.stats);
    character.stats.values[d2d::d2s::kLevel] = 40;
    auto world = std::make_unique<World>(&data, -1, -1);
    world->enter(character);
    Level open = *data.level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    auto* actor = world->actor;
    const int zombie = data.monsters.row("fallen1"), skeleton = data.monsters.row("skeleton1");
    const auto& raise = *data.skills.get(data.skills.by_name.at("Raise Skeleton"));
    const int raise_id = data.skills.by_name.at("Raise Skeleton");
    world->fight.skill_level = [&](int skill) { return skill == raise_id ? 30 : 0; };
    world->fight.skill_base = world->fight.skill_level;
    const std::uint32_t start = 1000 * kTickMs;
    const Crowd crowd;
    // Events in order: 's' a summon, 'c' a monster turned by Conversion (Fight::turn_good), all 2 cells off the hostile
    // monster. Foes: 0 the player, 1 the merc, then the summons (foe 2 on), the monsters after them (the hostile one
    // first, then each turned one). The result is the foe the hostile monster takes on its first think.
    const auto chosen = [&](const std::string& events) {
        actor->pets.clear();
        actor->merc.reset();
        world->fight.monsters.clear();
        actor->list_serial = 0;
        actor->character.header.merc_type = 0;
        const float x = actor->player.x + 8, y = actor->player.y;
        Monster hostile = make_monster(data, zombie, x, y, rng, 0);
        hostile.aware = false;
        world->fight.add_monster(std::move(hostile));
        for (const char event : events) {
            if (event == 's') {
                world->fight.summon_one(raise, skeleton, 30, world->fight.calc_env(), x - 2, y, start, -1);
                auto& unit = actor->pets.back().monster.unit;
                unit.x = x - 2; unit.y = y;
                actor->pets.back().monster.next_act = ~0u;
            } else {
                Monster turned = make_monster(data, skeleton, x - 2, y, rng, 0);
                turned.aware = false; turned.next_act = ~0u;
                world->fight.add_monster(std::move(turned));
                world->fight.turn_good(world->fight.monsters.back(), 40);
            }
        }
        for (std::uint32_t now = start + kTickMs; now <= start + 60 * kTickMs; now += kTickMs) {
            world->fight.world(true, now, 0.04f, crowd);
            if (const int chase = world->fight.monsters[0].chase; chase >= 0) return chase;
        }
        return -1;
    };
    int failed = 0;
    const auto expect = [&](int got, int want, const char* what) {
        if (got != want) { std::printf("FAIL: %s: the monster took foe %d, game.exe's order takes %d\n", what, got, want); ++failed; }
    };
    // One summon, then a conversion: the converted monster (foe 4: the hostile monster is foe 3) is the newest, so it wins the tie.
    expect(chosen("sc"), 4, "a summon, then a conversion");
    // A conversion, then a summon (foe 2): the summon is newer.
    expect(chosen("cs"), 2, "a conversion, then a summon");
    if (failed) return 1;
    std::puts("OK: a converted monster is its caster's newest pet in the monster's search order");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_converted_pet_order: %s\n", error.what());
    return 1;
}
