// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's target search tries a player's pets as game.exe's list holds
// them (FUN_005dd7f0 over FUN_005b1900's order): the pet that entered the
// list last first, a summon where it was made, the merc when it was hired,
// joined or revived. A tie goes to whoever is tried first. The REAL
// Fight::world, Fight::summon_one and World::spawn_merc (foes: player, merc,
// summons oldest first). Needs the game data; SKIPs without it.
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
#include <cassert>
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
    world->fight.skill_level = [&](int skill) { return skill == raise_id ? 30 : 0; };   // level 30: room for 3 skeletons
    world->fight.skill_base = world->fight.skill_level;
    const int merc_type = data.mercs.begin()->first;
    const std::uint32_t start = 1000 * kTickMs;
    const Crowd crowd;
    // The pets come through the real call sites: a summon through Fight::summon_one, the merc through World::spawn_merc
    // (hire, join and revive all call it). Events in order: 's' a summon 2 cells off the monster (+ dx cells nearer),
    // 'm' the merc hired or joined, 'r' the merc dies and is revived. The merc stands `merc_off` cells off.
    // The result is the foe the monster takes on its first think: 0 the player, 1 the merc, 2.. the summons oldest first.
    const auto chosen = [&](const std::string& events, float merc_off, float summon_near) {
        actor->pets.clear();
        actor->merc.reset();
        world->fight.monsters.clear();
        actor->character.header.merc_type = std::uint16_t(merc_type);
        actor->character.header.merc_seed = 7;
        actor->character.header.merc_dead = false;
        const float x = actor->player.x + 8, y = actor->player.y;
        Monster hostile = make_monster(data, zombie, x, y, rng, 0);
        hostile.aware = false;
        world->fight.add_monster(std::move(hostile));
        int summons = 0;
        for (const char event : events) {
            if (event == 's') {
                world->fight.summon_one(raise, skeleton, 30, world->fight.calc_env(), x - 2 + (summons == 0 ? summon_near : 0.f), y, start, -1);
                auto& unit = actor->pets.back().monster.unit;         // summon_one places it on a free spot: put it on the exact one, so the tie holds
                unit.x = x - 2 + (summons == 0 ? summon_near : 0.f); unit.y = y;
                actor->pets.back().monster.next_act = ~0u;           // and it stands
                ++summons;
            } else {
                if (event == 'r') actor->merc.reset();                 // it died; the revive's call is spawn_merc again
                world->spawn_merc();
            }
        }
        if (actor->merc) {
            actor->merc->unit.x = x - merc_off; actor->merc->unit.y = y;
            actor->merc->next_act = ~0u;
        }
        for (std::uint32_t now = start + kTickMs; now <= start + 60 * kTickMs; now += kTickMs) {
            world->fight.world(true, now, 0.04f, crowd);
            if (const int chase = world->fight.monsters[0].chase; chase >= 0) return chase;
        }
        return -1;
    };
    int failed = 0;
    const auto expect = [&](int got, int want) { if (got != want) { std::printf("FAIL: the monster took foe %d, game.exe's order takes %d\n", got, want); ++failed; } };
    // Two summons the same distance off: the newer, foe 3. Three alike: the newest, foe 4.
    expect(chosen("ss", 0, 0), 3);
    expect(chosen("sss", 0, 0), 4);
    // The merc, hired first, and a summon the same distance off: the summon, foe 2. A strictly nearer merc wins, foe 1.
    expect(chosen("ms", 2, 0), 2);
    expect(chosen("ms", 1, 0), 1);
    // A nearer older summon beats a newer one, foe 2.
    expect(chosen("ss", 0, 1), 2);
    // A merc revived after the summons, or hired after them, is the newest: it wins the tie, foe 1; a nearer summon still wins.
    expect(chosen("msr", 2, 0), 1);
    expect(chosen("sm", 2, 0), 1);
    expect(chosen("mssr", 2, 0), 1);
    expect(chosen("msr", 2, 1), 2);
    if (failed) return 1;
    std::puts("OK: a monster tries a player's pet that entered the list last first");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_pet_search_order: %s\n", error.what());
    return 1;
}
