// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Friendly auras (do 65, FUN_005cf010) between the player and the merc, with
// the game's MPQs (D2_MPQ_DIR; the 1.14d ones with D2_PATCH_INSTALLER): an
// Act 2 merc's Defiance on itself and on the player within its aurarange,
// the player's on the merc, Prayer's pulse healing both.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
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
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    constexpr int kPaladin = 3;
    auto made = new_character(*data, kPaladin, "Headless", false, true, rng);
    Character character;
    character.character_class = kPaladin;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto& fight = world.fight;
    auto& actor = *world.actor;

    const int defiance = data->skills.by_name.at("Defiance"), prayer = data->skills.by_name.at("Prayer");
    Monster merc;
    merc.hit_points = 10;
    merc.unit.x = actor.player.x + 1; merc.unit.y = actor.player.y;
    actor.merc = merc;
    actor.merc_st.level = 20;
    actor.merc_ids = { defiance, prayer, 0, 0, 0, 0 }; actor.merc_levels = { 4, 4, 0, 0, 0, 0 };
    actor.aura = -1;
    const int bare_merc = fight.merc_fighter().defense;
    fight.update_fighters(1000);
    const int bare_player = actor.player_combat.defense;

    // Its Defiance on itself and the player beside it; past its aurarange, the player has none.
    actor.merc_aura = defiance;
    const auto covering = fight.friendly_auras(actor.player.x, actor.player.y);
    assert(covering.size() == 1 && covering[0].skill->id == defiance && covering[0].level == 4);
    assert(fight.merc_fighter().defense > bare_merc);
    fight.update_fighters(1000);
    assert(actor.player_combat.defense > bare_player);
    actor.merc->unit.x = actor.player.x + 40;
    assert(fight.friendly_auras(actor.player.x, actor.player.y).empty() && fight.merc_fighter().defense > bare_merc);
    fight.update_fighters(1000);
    assert(actor.player_combat.defense == bare_player);
    std::printf("OK: the merc's Defiance on itself and the player within its range\n");

    // The player's Defiance on the merc beside it.
    actor.merc_aura = -1;
    actor.merc->unit.x = actor.player.x + 1;
    actor.aura = defiance;
    fight.skill_level = [&](int id) { return id == defiance ? 1 : 0; };
    assert(fight.merc_fighter().defense > bare_merc);
    std::printf("OK: the player's Defiance on the merc\n");

    // The merc's Prayer heals both on its pulse.
    actor.aura = -1;
    actor.merc_aura = prayer;
    actor.merc_aura_next = 0;
    actor.merc->hit_points = 1;
    auto& life = actor.character.stats.values[d2d::d2s::kLife];
    life = 256;
    fight.aura_pulse(2000);
    assert(actor.merc->hit_points > 1 && life > 256);
    const auto healed = life;
    fight.aura_pulse(2040);                               // before perdelay: no second pulse
    assert(life == healed);
    std::printf("OK: the merc's Prayer heals the player and itself on its pulse\n");

    // Its Holy Freeze (do 81): -dm34 velocity and attack rate on a monster near it, its cold pulse.
    const int holy_freeze = data->skills.by_name.at("Holy Freeze");
    actor.merc_ids[0] = holy_freeze; actor.merc_levels[0] = 6;
    actor.merc_aura = holy_freeze;
    actor.merc_aura_next = 0;
    const auto zombie = data->monsters.by_id.at("zombie1");
    d2d::rules::Rng make_rng{ 3 };
    fight.monsters.push_back(make_monster(*data, int(zombie), actor.merc->unit.x + 1, actor.merc->unit.y, make_rng, 0));
    auto& chilled = fight.monsters.back();
    chilled.hit_points = chilled.stats.hit_points = 100000;
    fight.monster_states(3000);
    assert(chilled.speed_pct < 0 && chilled.anim_pct < 0);
    fight.aura_pulse(3000);
    assert(chilled.hit_points < 100000);                    // its cold (no ELen: the slow is the aurastats')
    chilled.unit.x = actor.merc->unit.x + 40;
    fight.monster_states(3000);
    assert(chilled.speed_pct == 0 && chilled.anim_pct == 0);
    std::printf("OK: the merc's Holy Freeze slows the monsters near it and its pulse hurts them\n");

    // An antidote fed to the merc (FUN_005bef90 / FUN_005beda0): its poison gone, +50 poison resist on it.
    actor.merc_poison_until = 999999;
    const int poison_res = fight.merc_fighter().res[3];
    fight.merc_drinks("yps", 3000);
    assert(actor.merc_poison_until == 0 && fight.merc_fighter().res[3] > poison_res);
    std::printf("OK: the merc's antidote ends its poison and raises its resist\n");

    // A boss's (Aura Enchanted): Holy Freeze slows the player near it, Fanaticism speeds the monsters' attacks.
    actor.merc.reset();
    fight.monsters.clear();
    fight.update_fighters(4000);
    const int pace = actor.player_combat.velocity_pct, rate = actor.player_combat.sias;
    fight.monsters.push_back(make_monster(*data, int(zombie), actor.player.x + 1, actor.player.y, make_rng, 0));
    fight.monsters.back().aura = holy_freeze; fight.monsters.back().aura_lvl = 6;
    fight.update_fighters(4000);
    assert(actor.player_combat.velocity_pct < pace && actor.player_combat.sias < rate);
    fight.monsters.back().aura = data->skills.by_name.at("Fanaticism");
    std::vector<Foe> foes(1);
    fight.monster_auras(foes, 4000);
    assert(fight.monsters.back().aura_rate > 0 && anim_rate_pct(fight.monsters.back(), "A1") > anim_rate_pct(fight.monsters.back(), "WL"));
    std::printf("OK: a boss's Holy Freeze slows the player, its Fanaticism speeds the attacks\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
