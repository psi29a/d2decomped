// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Act 1 merc's shot, as tools/emu/merc_shot.py runs game.exe's
// FUN_005ce1b0 (RogueMissile): `--merc-shot` reads its cases (`magic exploding
// level`: the merc's stats 157 / 158 and the skill's level) and prints each
// with the Missiles row d2d shoots (Fight::merc_shot), its level and the
// row's own physical min and max at it (256ths, missile_phys), with the
// game's MPQs (D2_MPQ_DIR). Without arguments: the pick, a roguehire's shot at
// a zombie in a frame of its turn (merc_turn), and rogue1's point in the blow
// (that one without game data, so CI runs it).

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
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
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int shot_cases(const GameData& data, const Fight& fight) {
    const auto& rogue_missile = *data.skills.get(data.skills.by_name.at("RogueMissile"));
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int magic = 0, exploding = 0, level = 0;
        if (!(fields >> magic >> exploding >> level)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        const auto shot = fight.merc_shot(rogue_missile, magic, exploding, level);
        if (!shot.info) {
            std::printf("%s | -\n", line.c_str());
            continue;
        }
        const d2d::rules::CalcEnv env;
        std::printf("%s | %d %d %d %d\n", line.c_str(), shot.info->id, shot.level, missile_phys(data, *shot.info, env, -1, shot.level, false),
                    missile_phys(data, *shot.info, env, -1, shot.level, true));
    }
    return 0;
}

// rogue1's MinDamage / MaxDamage 1 (HitShift 8): a point more in the roll, before the crit. No game data.
void row_point_checks() {
    auto bow = d2d::rules::simple_fighter(10, 10, 1000000);
    const d2d::rules::Target zombie{ .hit_points = 400, .max_hp = 400, .armor_class = 1, .level = 1 };
    d2d::rules::Swing plain;
    plain.missile = true;
    auto with_row = plain;
    with_row.row_lo = with_row.row_hi = 256;
    const auto blow = [&](const d2d::rules::Swing& swing) {   // the first that lands, on the same draws each time
        d2d::rules::Rng roll{ 3 };
        auto landed = d2d::rules::player_blow(bow, zombie, 2, roll, swing);
        for (int tries = 0; tries < 20 && !landed.hit; ++tries) landed = d2d::rules::player_blow(bow, zombie, 2, roll, swing);
        return landed;
    };
    assert(blow(plain).hit && blow(plain).phys == 10 && blow(with_row).phys == 11);
    bow.phys_pct = 50;                                           // the record's +0x78 scales the row's point too
    assert(blow(plain).phys == 15 && blow(with_row).phys == 16);
    bow.phys_pct = 0;
    bow.deadly = 100;
    assert(blow(with_row).phys == 22);
    bow.deadly = 0;
    // SrcDamage scales the weapon's part only (0x64bc43..0x64bc67), the row's point goes in whole (0x64bdf7).
    plain.srcdam = with_row.srcdam = 64;
    assert(blow(plain).phys == 5 && blow(with_row).phys == 6);
    plain.srcdam = 128;
    // The record's % takes the larger of the owner's stats 18 / 17 (0x64bdc2..0x64bddc).
    bow.unit_min_pct = 20; bow.unit_max_pct = 40;
    assert(blow(plain).phys == 14);
    bow.unit_min_pct = bow.unit_max_pct = 0;
    // The hit adds the demons' % on a demon, the undead's (+50 Blunt) on an undead (FUN_005a89a0, 0x5a8b09..0x5a8bff).
    bow.demon_pct = 30; bow.undead_pct = 10; bow.blunt = true;
    auto demon = zombie;
    demon.demon = true;
    assert(blow(plain).phys == 10);
    const auto struck = [&](const d2d::rules::Target& target) {
        d2d::rules::Rng roll{ 3 };
        auto landed = d2d::rules::player_blow(bow, target, 2, roll, plain);
        for (int tries = 0; tries < 20 && !landed.hit; ++tries) landed = d2d::rules::player_blow(bow, target, 2, roll, plain);
        return landed.phys;
    };
    auto undead = zombie;
    undead.undead = true;
    assert(struck(demon) == 13 && struck(undead) == 16);
    bow.phys_pct = -200;                                         // at least -90, twice
    assert(struck(zombie) == 1);
    std::puts("OK: rogue1's point joins the roll whole, the weapon's part x SrcDamage, the % with stats 17 / 18 and the demons' (FUN_0064b860, FUN_005a89a0)");
}

}  // namespace

int main(int argc, char** argv) try {
    const bool oracle = argc > 1 && std::string_view(argv[1]) == "--merc-shot";
    if (!oracle) row_point_checks();
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
    World world(&*data, -1, -1);
    world.enter(character);
    auto& fight = world.fight;
    if (oracle) return shot_cases(*data, fight);

    // The pick: rogue1 at the skill's level; magicarrow at stat 157's, before explodingarrow at 158's; none at 0 or under.
    const auto& rogue_missile = *data->skills.get(data->skills.by_name.at("RogueMissile"));
    assert(rogue_missile.srvdofunc == d2d::rules::ServerDoFunction::kRogueMissile);
    const auto rogue = fight.merc_shot(rogue_missile, 0, -4, 3);
    assert(rogue.info && rogue.info->name == "rogue1" && rogue.level == 3);
    const auto magic = fight.merc_shot(rogue_missile, 5, 9, 1);
    assert(magic.info && magic.info->name == "magicarrow" && magic.level == 5);
    const auto exploding = fight.merc_shot(rogue_missile, -1, 9, 1);
    assert(exploding.info && exploding.info->name == "explodingarrow" && exploding.level == 9);
    std::puts("OK: the merc's shot is rogue1, or its gear's magic / exploding arrow at that stat's level (FUN_005ce1b0)");

    // A roguehire with a zombie 5 cells off: its turns shoot rogue1 at it.
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world.level = &open;
    int hid = -1;
    for (const auto& [id, merc] : data->mercs)
        if (merc.npc.id == "roguehire" && hid < 0) hid = id;
    auto& actor = *world.actor;
    actor.character.header.merc_type = std::uint16_t(hid);
    actor.character.header.merc_seed = 7;
    actor.character.header.merc_dead = false;
    world.spawn_merc();
    assert(actor.merc);
    actor.player.x = actor.merc->unit.x = 10.5f;
    actor.player.y = 9.5f;
    actor.merc->unit.y = 10.5f;
    d2d::rules::Rng make_rng{ 3 };
    fight.monsters.push_back(make_monster(*data, int(data->monsters.by_id.at("zombie1")), 15.5f, 10.5f, make_rng, 0));
    fight.monsters.back().stats.hit_points = fight.monsters.back().hit_points = 1000000;
    const Crowd crowd;
    int shots = 0;
    for (int frame = 0; frame < 200 && shots == 0; ++frame) {
        fight.merc_turn(40000 + std::uint32_t(frame) * 40, 0.04f, crowd, false);
        for (const auto& missile : fight.missiles)
            if (missile.by_merc) {
                assert(missile.info && missile.info->name == "rogue1");
                ++shots;
            }
    }
    assert(shots > 0);
    std::puts("OK: a roguehire shoots rogue1 at a zombie (RogueMissile, do 110)");

    return 0;
} catch (const std::exception& error) {
    std::printf("test_merc_shot: %s\n", error.what());
    return 1;
}
