// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Succubi (FUN_005e1e00 / FUN_005e2120) cast only at a target not cursed
// (FUN_00625760(target, 0x20): a stat list flagged 0x20, a States.txt curse
// state's, FUN_0056e970): a monster's curse, or a shrine's state (128..137,
// curse 1), on the player's foe. Battle Orders isn't one. Needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <think_scratch.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const auto cursed = [&](const Foe& foe) {
        return std::ranges::any_of(foe.states, [&](std::string_view state) { return data->states.at(std::string(state)).curse; });
    };
    assert(data->states.at("decrepify").curse && data->states.at("shrine_armor").curse && !data->states.at("battleorders").curse);

    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, 0, "Headless", false, true, made_rng);
    Character character;
    character.character_class = 0;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    constexpr std::uint32_t kNow = 1000;
    assert(!cursed(world.fight.player_foe(kNow)));
    const auto armor = std::ranges::find(data->shrines, 6, &d2d::rules::ShrineRow::code);   // the armor shrine
    assert(armor != data->shrines.end());
    world.actor->boost = { int(armor - data->shrines.begin()), {}, kNow + 40 };
    assert(cursed(world.fight.player_foe(kNow)));
    assert(!cursed(world.fight.player_foe(kNow + 40)));                     // run out
    world.actor->curses[0] = { data->skills.by_name.at("Decrepify"), 5, kNow + 40 };
    assert(cursed(world.fight.player_foe(kNow)));

    // The Succubus within aip4 on its roll: Skill1 at an uncursed target,
    // none at a cursed one.
    const auto succubus = [&](bool target_cursed) {
        int state = 0;
        d2d::rules::ThinkIn input{ .aip = { 0, 0, 100, 10, 0, 0, 0, 100 }, .dist = 5, .state = &state };
        input.skill[0] = true;
        ThinkScratch scratch;
        scratch.point(input);
        input.target_player = true; input.target_life_pct = 100; input.self_life_pct = 100;
        input.target_cursed = target_cursed;
        d2d::rules::Rng roll{ 5 };
        return d2d::rules::mon_think("Succubus", input, roll, [](int, bool) { return false; });
    };
    assert(succubus(false).act == d2d::rules::MonAct::skill && succubus(false).n == 0);
    assert(succubus(true).act != d2d::rules::MonAct::skill);
    std::printf("target cursed: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_target_cursed: %s\n", error.what());
    return 1;
}
