// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's curse slot, as tools/emu/curse_put.py runs game.exe's
// timed-state put (FUN_0056e970): `--curse-put` reads its puts (`seq skill
// level length frame`, one monster a seq, frames in order) and prints `seq
// skill level length frame taken skill level left curses`. Each goes through
// put_state (fight/states.cpp), which puts a curse state in the curse slot
// whoever casts it (Taunt, Battle Cry, Cloak, the Overseer's whip too) and
// any other in its own node; `length` frames from `frame`. Then whether it was
// taken, the curse the monster holds: its skill, level and frames left (-1 -1
// -1: none), and how many curse states (States.txt curse) it holds.
// Needs the game data. Without it, the rule on two typed-in skills: a
// monster holding Attract refuses every curse state (States.txt curse), a
// re-cast of Attract included and Taunt, until it ends; a state that isn't a
// curse still goes on. Two other states keep their own nodes side by side.

#include <ai.hpp>
#include <ai/units.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <skills.hpp>
#include <test_data.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int curse_put_cases(const GameData& game_data) {
    Monster monster;
    int last = -1;
    std::string line;
    while (std::getline(std::cin, line)) {
        int seq = 0, skill = 0, level = 0, length = 0, frame = 0;
        if (line.empty()) continue;
        if (std::sscanf(line.c_str(), "%d %d %d %d %d", &seq, &skill, &level, &length, &frame) != 5) {
            std::fprintf(stderr, "malformed put: %s\n", line.c_str());
            return 1;
        }
        if (seq != last) { monster = Monster{}; last = seq; }
        const auto now = std::uint32_t(frame) * 40;
        const auto* row = game_data.skills.get(skill);
        (void)row;
        const bool taken = put_state(game_data, monster, { skill, level, now + std::uint32_t(length) * 40 }, now);
        const auto curse_state = [&](const Monster::SkillState& slot) {
            const auto* held = slot.skill >= 0 && now < slot.until ? game_data.skills.get(slot.skill) : nullptr;
            const auto state = held ? game_data.states.find(held->auratarget) : game_data.states.end();
            return state != game_data.states.end() && state->second.curse;
        };
        int curses = int(curse_state(monster.curse));
        for (const auto& other : monster.states) curses += int(curse_state(other));
        const auto& held = monster.curse;
        if (held.skill >= 0 && now < held.until)
            std::printf("%d %d %d %d %d %d %d %d %d %d\n", seq, skill, level, length, frame, taken ? 1 : 0, held.skill, held.level, int((held.until - now) / 40), curses);
        else
            std::printf("%d %d %d %d %d %d -1 -1 -1 %d\n", seq, skill, level, length, frame, taken ? 1 : 0, curses);
    }
    return 0;
}

// Attract (86), Amplify Damage (66), Taunt (30) and Inner Sight (8) typed in; 25 frames a put.
int attract_checks() {
    d2d::test::TestData test;
    test.skill(86, "Attract").auratarget = "attract";
    test.skill(66, "Amplify Damage").auratarget = "amplifydamage";
    test.skill(30, "Taunt").auratarget = "taunt";
    test.skill(8, "Inner Sight").auratarget = "innersight";
    for (const char* curse : { "attract", "amplifydamage", "taunt" }) test.state(curse, true);
    test.state("innersight");
    const GameData& game_data = test.data;
    Monster monster;
    const auto put = [&](int skill, int level, std::uint32_t now) { return put_state(game_data, monster, { skill, level, now + 1000 }, now); };
    assert(put(86, 1, 0) && monster.curse.skill == 86);
    assert(!put(66, 5, 40) && monster.curse.skill == 86);              // a curse on an attracted monster: refused
    assert(!put(86, 2, 80) && monster.curse.level == 1 && monster.curse.until == 1000);   // Attract again too
    assert(!put_state(game_data, monster, { 30, 1, 1040 }, 40));  // Taunt, a curse state: refused
    assert(put_state(game_data, monster, { 8, 1, 1040 }, 40));    // Inner Sight, not a curse: it goes on
    assert(put(66, 5, 1000) && monster.curse.skill == 66);             // Attract over: the curse goes on
    assert(!put(66, 4, 1040) && put(86, 1, 1080) && monster.curse.skill == 86);   // no Attract: d2d's rule as before
    std::puts("OK: an attracted monster refuses every curse, Attract's re-cast too");
    return 0;
}

// The same typed-in skills; 25 frames a put.
int shared_slot_checks() {
    d2d::test::TestData test;
    test.skill(66, "Amplify Damage").auratarget = "amplifydamage";
    test.skill(30, "Taunt").auratarget = "taunt";
    test.skill(8, "Inner Sight").auratarget = "innersight";
    for (const char* curse : { "amplifydamage", "taunt" }) test.state(curse, true);
    test.skill(17, "Slow Missiles").auratarget = "slowmissiles";
    test.state("innersight");
    test.state("slowmissiles");
    const GameData& game_data = test.data;
    Monster monster;
    const auto put = [&](int skill, int level, std::uint32_t now) { return put_state(game_data, monster, { skill, level, now + 1000 }, now); };
    const auto other_skills = [&] { std::vector<int> skills; for (const auto& held : monster.states) skills.push_back(held.skill); return skills; };
    assert(put(66, 3, 0) && put(8, 1, 0));                              // a curse and Inner Sight side by side
    assert(put(30, 1, 40) && monster.curse.skill == 30 && other_skills() == std::vector<int>{ 8 });   // Taunt takes the curse's place
    assert(!put(30, 0, 80) && put(66, 1, 120) && monster.curse.skill == 66);   // one curse slot: the same rule
    assert(put(30, 2, 160) && !put(30, 1, 200));                        // a lower Taunt meets the higher one
    assert(monster.curse.skill == 30 && monster.curse.level == 2);
    assert(put(17, 2, 240) && (other_skills() == std::vector<int>{ 8, 17 }));   // Slow Missiles beside Inner Sight: their own nodes
    assert(!put(17, 1, 280) && put(8, 2, 320) && (other_skills() == std::vector<int>{ 8, 17 }) && monster.states[0].level == 2);   // each its own rule
    std::puts("OK: Taunt shares the curse slot, other states keep their own nodes");
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--curse-put") {
        const char* mpq_dir = std::getenv("D2_MPQ_DIR");
        if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::fprintf(stderr, "--curse-put needs D2_MPQ_DIR\n"); return 1; }
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
        if (!data || !data->patched) { std::fprintf(stderr, "--curse-put needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return 1; }
        return curse_put_cases(*data);
    }
    return attract_checks() | shared_slot_checks();
} catch (const std::exception& error) {
    std::printf("test_curse_put: %s\n", error.what());
    return 1;
}
