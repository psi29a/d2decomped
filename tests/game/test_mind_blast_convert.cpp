// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Mind Blast's conversion (FUN_005d76e0 -> FUN_005d7680 -> FUN_005d7430):
// each unit of the walk turns when rand(100) is at most dm(Param5, Param6)
// (FUN_005d72b0's SETLE), for Param3 + rand(Param4) frames, and takes no
// blow then; one above the caster's level comes down to it. The rolls are
// replayed on a copy of the fight's rng, cast by cast.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::printf("SKIP: Mind Blast's Params are 1.14d's: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }

    const auto* blast = data->skills.get(data->skills.by_name.at("Mind Blast"));
    assert(blast && blast->srvdofunc == d2d::rules::ServerDoFunction::kMindBlast);
    assert(blast->par[2] == 150 && blast->par[3] == 100 && blast->par[4] == 15 && blast->par[5] == 40);
    const int conversion = data->skills.by_name.at("Conversion");

    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, 6, "Headless", false, true, made_rng);
    Character character;
    character.character_class = 6;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.level = data->level(d2d::rules::level_ids::kBloodMoor);
    assert(world.level);
    const int caster_level = int(world.actor->character.stats.get(d2d::d2s::kLevel));

    d2d::rules::Rng rng{ 3 };
    const auto fallen = data->monsters.by_id.find("fallen1");
    assert(fallen != data->monsters.by_id.end());
    Monster made = make_monster(*data, fallen->second, 10, 10, rng, 0);
    made.stats.level = caster_level + 20;                     // comes down to the caster's when turned
    const int own_life = made.stats.hit_points = made.hit_points = 400;
    made.life_frac = made.max_life_frac = 0;
    assert(swaps_ai(*data, made));

    constexpr std::uint32_t kNow = 1000 * 40;
    int turned = 0, struck = 0, at_edge = 0;
    for (int cast = 0; cast < 3000; ++cast) {
        const int lvl = 1 + cast % 20;
        const int chance = d2d::rules::calc_dm(15, 40, lvl);
        world.fight.monsters.clear();
        world.fight.add_monster(Monster(made));
        auto replay = world.fight.rng;
        const int roll = int(replay(100));
        const Fight::Caster caster{ .x = made.unit.x, .y = made.unit.y, .level = lvl, .target = 0, .to_x = made.unit.x, .to_y = made.unit.y };
        world.fight.spot(*blast, kNow, caster);
        const auto& after = world.fight.monsters[0];
        if (roll <= chance) {
            const auto until = kNow + std::uint32_t(150 + int(replay(100))) * 40;
            assert(after.align == 2 && after.chase == -1);
            assert(after.states.size() == 1 && after.states[0].skill == conversion && after.states[0].level == lvl && after.states[0].until == until);
            assert(after.hit_points <= own_life && after.stats.level == caster_level && after.own_level == caster_level + 20);
            assert(after.struck_by != world.fight.player_foe_id());
            ++turned;
            at_edge += roll == chance;
        } else {
            assert(after.align == 0 && after.states.empty() && after.own_level == 0 && after.stats.level == caster_level + 20);
            ++struck;
        }
    }
    assert(turned > 0 && struck > 0 && at_edge > 0);

    // One already turned keeps its node and takes only the new end.
    world.fight.monsters.clear();
    Monster held = made;
    held.align = 0;
    held.states = { { conversion, 9, kNow + 5 * 40 } };
    world.fight.add_monster(std::move(held));
    for (int tries = 0; tries < 200 && world.fight.monsters[0].states[0].until == kNow + 5 * 40; ++tries) {
        world.fight.monsters[0].hit_points = own_life;
        world.fight.spot(*blast, kNow, { .x = made.unit.x, .y = made.unit.y, .level = 20, .target = 0, .to_x = made.unit.x, .to_y = made.unit.y });
    }
    const auto& kept = world.fight.monsters[0];
    assert(kept.states.size() == 1 && kept.states[0].level == 9 && kept.states[0].until >= kNow + 150 * 40);
    std::printf("mind blast conversion: %d turned (%d at the edge), %d struck\n", turned, at_edge, struck);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_mind_blast_convert: %s\n", error.what());
    return 1;
}
