// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The merc's Frozen Armor (event 2, FUN_005cac50) as the player's: with the
// game's MPQs (D2_MPQ_DIR; the 1.14d ones with D2_PATCH_INSTALLER), a monster
// whose blow did damage to the merc gets FUN_0057b230's rule
// (frozen_armor_freeze): a plain zombie frozen for its length over
// MonsterFreezeDivisor, a champion chilled instead, never frozen.

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
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
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Chiller", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Chiller";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto& fight = world.fight;
    auto& actor = *world.actor;
    Monster merc;
    merc.hit_points = 100;
    merc.unit.x = actor.player.x + 1; merc.unit.y = actor.player.y;
    actor.merc = merc;
    actor.merc_st.level = 20;
    const int frozen_armor = data->skills.by_name.at("Frozen Armor");
    actor.merc_buff = { frozen_armor, 5, 1000000 };

    const auto zombie = int(data->monsters.by_id.at("zombie1"));
    d2d::rules::Rng make_rng{ 3 };
    fight.monsters.push_back(make_monster(*data, zombie, actor.player.x + 2, actor.player.y, make_rng, 0));
    fight.monsters.back().id = 501;
    fight.monsters.push_back(make_monster(*data, zombie, actor.player.x + 2, actor.player.y + 1, make_rng, 0));
    fight.monsters.back().id = 502;
    fight.monsters.back().boss = d2d::rules::Boss::champion;
    auto& plain = fight.monsters[0];
    auto& champion = fight.monsters[1];
    const auto& zombie_row = data->monsters.types[std::size_t(zombie)];
    assert(zombie_row.diff[0].cold_effect < 0 && data->freeze_divisor[0] > 0);

    Foe foe;
    foe.damaged_by = { 501, 502 };
    fight.merc_buff_events(foe, 1000);
    const auto* skill = data->skills.get(frozen_armor);
    const int length = d2d::rules::eval_calc(data->skills, skill->calc[0], fight.merc_calc_env(), frozen_armor, 5);
    const auto plain_rule = d2d::rules::monster_freeze(length, fight.target_of(0), 0, false, zombie_row.diff[0].cold_effect, data->freeze_divisor[0]);
    assert(plain_rule.freeze_ticks > 0 && plain.frozen_until == 1000 + std::uint32_t(plain_rule.freeze_ticks) * 40 && plain.chill_until == 0);
    assert(champion.frozen_until == 0 && champion.chill_until > 1000);
    std::printf("OK: the merc's Frozen Armor freezes a zombie %d ticks and chills a champion, as the player's does\n", plain_rule.freeze_ticks);
    return 0;
} catch (const std::exception& error) {
    std::printf("test_merc_frozen_armor: %s\n", error.what());
    return 1;
}
