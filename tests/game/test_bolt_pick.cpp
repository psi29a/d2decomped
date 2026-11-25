// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Which missile a missile fan shoots by the player's weapon, as
// tools/emu/bolt_pick.py runs game.exe's FUN_005db410: `--bolt-pick` reads its
// cases (`skill wclass`: a do-8 Skills.txt row and the weapon's class number,
// FUN_00623c60) and prints each with the Missiles row d2d makes
// (Fight::skill_missile), with the game's MPQs (D2_MPQ_DIR). Without
// arguments: the rule on made-up rows (test_data.hpp, no game data, so CI runs
// it), then Multiple Shot, Strafe and Guided Arrow by a bow or a crossbow.

#include <character.hpp>
#include <character_store.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <loot.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int pick_cases(const GameData& data, World& world) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int skill_id = 0, wclass = 0;
        if (!(fields >> skill_id >> wclass)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        const auto* skill = data.skills.get(skill_id);
        world.actor->player_combat.bow = wclass == 1;           // bow's class (FUN_00629fe0's map at 0x7446a0)
        const auto* missile = skill ? world.fight.skill_missile(*skill) : nullptr;
        if (missile) std::printf("%s | %d\n", line.c_str(), missile->id);
        else std::printf("%s | -\n", line.c_str());
    }
    return 0;
}

// The rule on made-up rows: do 8 / 10 / 12 take srvmissileb without a bow, srvmissilea with one or when
// srvmissileb is empty; another do (a nova) keeps srvmissilea.
void rule_checks() {
    d2d::test::TestData fixture;
    auto& data = fixture.data;
    for (const auto& [name, id] : { std::pair{ "arrowrow", 1 }, std::pair{ "boltrow", 2 } }) {
        auto& missile = data.missiles[name];
        missile.name = name;
        missile.id = id;
    }
    using Do = d2d::rules::ServerDoFunction;
    const auto add = [&](int id, const char* name, Do kind, const char* alternate) {
        auto& skill = fixture.skill(id, name);
        skill.name = name;
        skill.srvdofunc = kind;
        skill.srvmissilea = "arrowrow";
        skill.srvmissileb = alternate;
    };
    add(1, "fan", Do::kMissileFan, "boltrow");
    add(2, "guided", Do::kGuidedMissile, "boltrow");
    add(3, "strafe", Do::kStrafe, "boltrow");
    add(4, "fan without", Do::kMissileFan, "");
    add(5, "nova", Do::kNova, "boltrow");
    Level arena;
    arena.id = 2;
    arena.ds1 = d2d::ds1::Map(30, 30, 0, 1);
    arena.walk.resize(150 * 150);
    const Level* level = &arena;
    d2d::rules::Rng random{ 7 };
    Cues cues;
    cues.game_data = &data;
    Loot loot{ &data, level, random, cues };
    Fight fight{ &data, level, random, loot, cues };
    Fight::Hero hero;
    fight.act_as(hero);
    const auto row = [&](int skill_id, bool bow) {
        hero.player_combat.bow = bow;
        const auto* missile = fight.skill_missile(*data.skills.get(skill_id));
        return missile ? missile->id : -1;
    };
    for (const int skill_id : { 1, 2, 3 }) assert(row(skill_id, true) == 1 && row(skill_id, false) == 2);
    assert(row(4, true) == 1 && row(4, false) == 1);
    assert(row(5, true) == 1 && row(5, false) == 1);
    std::puts("OK: do 8 / 10 / 12 take srvmissileb without a bow, srvmissilea with one or with none set (made-up rows)");
}

}  // namespace

int main(int argc, char** argv) try {
    const bool oracle = argc > 1 && std::string_view(argv[1]) == "--bolt-pick";
    if (!oracle) rule_checks();
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
    if (oracle) return pick_cases(*data, world);
    // A crossbow's Multiple Shot and Strafe shoot bolts, a bow's arrows; Guided Arrow is guidedarrow either way.
    const auto row = [&](const char* name, bool bow) {
        world.actor->player_combat.bow = bow;
        const auto* missile = world.fight.skill_missile(*data->skills.get(data->skills.by_name.at(name)));
        return missile ? missile->name : std::string("-");
    };
    assert(row("Multiple Shot", true) == "multipleshotarrow" && row("Multiple Shot", false) == "multipleshotbolt");
    assert(row("Strafe", true) == "strafearrow" && row("Strafe", false) == "strafebolt");
    assert(row("Guided Arrow", false) == "guidedarrow" && row("Teeth", false) == "teeth");
    std::puts("OK: srvmissileb for a weapon that isn't a bow (FUN_00623c60), srvmissilea for a bow");

    // Fighter::bow from the weapon in hand: a short bow's wclass is bow, a light crossbow's xbw.
    auto& items = world.actor->character.items;
    std::erase_if(items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped; });
    world.actor->character.stats.values[d2d::d2s::kStr] = 100;
    world.actor->character.stats.values[d2d::d2s::kDex] = 100;
    const auto holding = [&](const char* code) {
        std::erase_if(items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped; });
        d2d::d2s::Item weapon;
        weapon.code = code;
        weapon.location = d2d::d2s::item_location::kEquipped;
        weapon.slot = d2d::d2s::body_location::kRightArm;
        items.push_back(weapon);
        return world.fight.player_fighter().bow;
    };
    assert(holding("sbw") && !holding("lxb") && !holding("hax"));
    std::puts("OK: Fighter::bow is set by a bow's wclass, not a crossbow's or an axe's");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_bolt_pick: %s\n", error.what());
    return 1;
}
