// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Every character has its class's CharStats Skill 1..10 (record +0xae) at level 1:
// FUN_00647ee0 adds Attack, then FUN_00647110 for each of those ten, item or not.
// Left Hand Throw is the Barbarian's only, Left Hand Swing the Barbarian's and the Assassin's.

#include <character.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::printf("FAIL: load_game_data failed\n"); return 1; }
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }

    const auto id_of = [&](const std::string& name) {
        const auto found = data->skills.by_name.find(name);
        assert(found != data->skills.by_name.end());
        return found->second;
    };
    constexpr int kSorceress = 1, kBarbarian = 4, kAssassin = 6;
    for (int cls = 0; cls < 7; ++cls) {
        Character character;
        character.character_class = cls;
        const auto level = [&](const char* name) { return skill_base_level(*data, character, id_of(name)); };
        assert(skill_base_level(*data, character, 0) == 1);   // Attack
        for (const char* name : { "Throw", "Kick", "Unsummon", "Scroll of Identify", "Book of Identify", "Scroll of Townportal", "Book of Townportal" })
            assert(level(name) == 1);
        assert(level("Left Hand Throw") == (cls == kBarbarian ? 1 : 0));
        assert(level("Left Hand Swing") == (cls == kBarbarian || cls == kAssassin ? 1 : 0));
        assert(level(cls == kSorceress ? "Magic Arrow" : "Fire Bolt") == 0);   // another class's skill: no points, none
    }
    std::puts("OK: CharStats' general skills at level 1 (FUN_00647ee0)");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_general_skills: %s\n", error.what());
    return 1;
}
