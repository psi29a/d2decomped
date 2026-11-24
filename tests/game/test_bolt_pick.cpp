// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Which missile a missile fan shoots by the player's weapon, as
// tools/emu/bolt_pick.py runs game.exe's FUN_005db410: `--bolt-pick` reads its
// cases (`skill wclass`: a do-8 Skills.txt row and the weapon's class number,
// FUN_00623c60) and prints each with the Missiles row d2d makes
// (Fight::skill_missile), with the game's MPQs (D2_MPQ_DIR).

#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

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

int pick_cases(const GameData& data, const Fight& fight) {
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
        const auto* missile = skill ? fight.skill_missile(*skill) : nullptr;
        if (missile) std::printf("%s | %d\n", line.c_str(), missile->id);
        else std::printf("%s | -\n", line.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
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
    if (argc > 1 && std::string_view(argv[1]) == "--bolt-pick") return pick_cases(*data, world.fight);
    std::puts("OK: --bolt-pick prints d2d's missile fan rows");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_bolt_pick: %s\n", error.what());
    return 1;
}
