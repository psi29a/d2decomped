// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Taunt's do (71, FUN_005d8570) clears the taunted monster's leader
// (FUN_005dd330(monster, null) at 0x5d8718: AI control +0xc -1, +0x10 1),
// its put (FUN_005c35c0) taken or refused, as tools/emu/taunt_leader.py runs
// it: `--taunt-leader` reads its cases (`id type put`: the leader's id, -1
// none, its type, whether the put is taken) and prints each with `| id type`,
// the control's +0xc / +0x10 after. d2d puts Taunt through put_state
// (fight/states.cpp); a refused put is one onto an attracted monster.
// Without arguments: a few of those cases, and the leader kept by a curse
// that isn't Taunt. Typed-in skills; no game data.

#include <ai.hpp>
#include <ai/units.hpp>
#include <gamedata.hpp>
#include <skills.hpp>
#include <test_data.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace d2d::game;

namespace {

constexpr int kAttract = 86, kAmplify = 66, kTaunt = 137;

// Typed-in skill rows: Taunt (srvdofunc 71, FUN_005d8570), Attract (to make its put refused) and Amplify Damage (the control curse).
d2d::test::TestData skills() {
    d2d::test::TestData test;
    test.skill(kAttract, "Attract").auratarget = "attract";
    test.skill(kAmplify, "Amplify Damage").auratarget = "amplifydamage";
    auto& taunt = test.skill(kTaunt, "Taunt");
    taunt.auratarget = "taunt";
    taunt.srvdofunc = d2d::rules::ServerDoFunction::kTaunt;
    for (const char* curse : { "attract", "amplifydamage", "taunt" }) test.state(curse, true);
    return test;
}

// One case: a monster whose AI control names leader `ident` of `kind` (d2d holds the id as Monster::leader, and
// none as -1 or leader_lost), attracted first when the put is to be refused, then `skill` put on it. The control
// after: `id type`, none being -1 1 (FUN_005dd330's).
std::pair<int, int> after(const GameData& game_data, int ident, int kind, bool taken, int skill) {
    Monster monster;
    monster.leader = ident;
    const bool attracted = !taken && put_state(game_data, monster, { kAttract, 1, 4000 }, 0);
    const bool put = put_state(game_data, monster, { skill, 1, 4000 }, 40);
    if (attracted == taken || put != taken) throw std::runtime_error("the put went other than the case asks");
    if (monster.leader < 0 || monster.leader_lost) return { -1, 1 };
    return { ident, kind };
}

// --taunt-leader: reads `ident kind put` rows (tools/emu/taunt_leader.py gen) and prints the AI control after.
int taunt_cases(const GameData& game_data) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int ident = 0, kind = 0, put = 0;
        if (!(fields >> ident >> kind >> put)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        const auto [id_after, kind_after] = after(game_data, ident, kind, put != 0, kTaunt);
        std::printf("%s | %d %d\n", line.c_str(), id_after, kind_after);
    }
    return 0;
}

// The fixed cases: Taunt clears the leader (FUN_005dd330(monster, null)), put taken or refused; another curse doesn't.
int checks(const GameData& game_data) {
    assert(after(game_data, 7, 1, true, kTaunt) == std::make_pair(-1, 1));      // a minion taunted: its leader gone
    assert(after(game_data, 7, 0, false, kTaunt) == std::make_pair(-1, 1));     // the put refused (Attract): gone all the same
    assert(after(game_data, -1, 1, true, kTaunt) == std::make_pair(-1, 1));     // none: none
    assert(after(game_data, 7, 1, true, kAmplify) == std::make_pair(7, 1));     // another curse: the leader stays
    std::puts("OK: Taunt clears the taunted monster's leader, put or not");
    return 0;
}

}  // namespace

// The oracle's case mode with --taunt-leader, else the fixed checks.
int main(int argc, char** argv) try {
    const auto test = skills();
    if (argc > 1 && std::string_view(argv[1]) == "--taunt-leader") return taunt_cases(test.data);
    return checks(test.data);
} catch (const std::exception& error) {
    std::printf("test_taunt_leader: %s\n", error.what());
    return 1;
}
