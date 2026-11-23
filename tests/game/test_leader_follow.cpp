// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's walk back to its leader, as tools/emu/leader_follow.py runs
// game.exe's FUN_005b0ff0: `--leader-follow` reads its cases (`id type pick
// found dying gap backed`, the row `pick` modulo the MonStats rows) and prints
// each with `| acted id back path walk`: rules::leader_follow.
// Without arguments: rules::leader_follow's branches and spot_about, then,
// with the game data: a unique zombie's minion 8 cells off
// it, a player beside the minion, walks back to its leader through
// monster_update; one cell off it goes for the player; its leader dead, it
// never walks back.

#include <ai.hpp>
#include <ai/units.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
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

int follow_cases(const GameData& data) {
    const auto& types = data.monsters.types;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        long long ident = 0, kind = 0, pick = 0, found = 0, dying = 0, gap = 0, backed = 0;
        if (!(fields >> ident >> kind >> pick >> found >> dying >> gap >> backed)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        const auto& type = types[std::size_t(pick) % types.size()];
        const auto step = d2d::rules::leader_follow(int(ident), int(kind), type.can_walk, found != 0, dying != 0, int(gap), backed != 0);
        std::printf("%s | %d %lld %d %d %d\n", line.c_str(), step.acted ? 1 : 0, step.cleared ? -1LL : ident, step.back ? 1 : 0, step.path ? 1 : 0,
                    step.walk ? 1 : 0);
    }
    return 0;
}

// rules::leader_follow's branches and spot_about's reach, no game data.
void rule_check() {
    using d2d::rules::leader_follow;
    const auto none = leader_follow(-1, 1, true, true, false, 40, false);
    assert(!none.acted && !none.cleared && !none.walk);
    assert(leader_follow(5, 1, false, true, false, 40, false).cleared && leader_follow(5, 1, true, false, false, 40, false).cleared
           && leader_follow(5, 1, true, true, true, 40, false).cleared);
    const auto walk = leader_follow(5, 1, true, true, false, 0x15, false);
    assert(walk.acted && walk.path && walk.walk && !walk.back && !leader_follow(5, 1, true, true, false, 0x14, false).acted);
    const auto backed = leader_follow(5, 0, true, true, false, 1, true), stuck = leader_follow(5, 0, true, true, false, 1, false);
    assert(backed.acted && backed.back && !backed.walk && stuck.back && stuck.walk && !stuck.path && !leader_follow(5, 1, true, true, false, 1, false).acted);
    d2d::rules::Rng seed{ 9 };
    for (int draw = 0; draw < 200; ++draw) {
        const auto [spot_x, spot_y] = d2d::rules::spot_about(seed, 100, 100, 0x13);
        const int off_x = std::abs(spot_x - 100), off_y = std::abs(spot_y - 100);
        assert(std::max(off_x, off_y) == 0x13 && std::min(off_x, off_y) < 0x13);
    }
}

int walk_check(const GameData& data) {
    const auto row = data.monsters.by_id.find("zombie1");
    assert(row != data.monsters.by_id.end() && data.monsters.types[row->second].can_walk);
    Level here;
    here.id = d2d::rules::level_ids::kBloodMoor;
    here.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    here.walk.assign(80 * 80, 0);
    const Crowd crowd;
    std::vector<Missile> shots;
    d2d::rules::Rng rng(7);
    const auto minion_after = [&](float apart, bool leader_alive) -> Monster {
        d2d::rules::Rng spawn{ 0x5eed };
        std::vector<Monster> pack{ make_monster(data, int(row->second), 4.1f, 8.1f, spawn, 0), make_monster(data, int(row->second), 4.1f + apart, 8.1f, spawn, 0) };
        pack[0].boss = d2d::rules::Boss::unique; pack[0].leader = 0;
        pack[1].boss = d2d::rules::Boss::minion; pack[1].leader = 0;
        if (!leader_alive) { pack[0].hit_points = 0; pack[0].mode = "DD"; }
        std::array<Foe, 1> player{};
        player[0].x = pack[1].unit.x + 0.4f; player[0].y = pack[1].unit.y;   // beside the minion
        pack[1].aware = true;
        monster_update(data, here, pack[1], player, rng, 40000, 0.04f, crowd, shots, pack);
        return std::move(pack[1]);
    };
    // 8 cells is 40 subtiles: over 0x14, it walks to a spot 0x13 about its leader, not at the player beside it.
    const auto distant = minion_after(8.f, true);
    assert(distant.mode == "WL" && std::abs(distant.unit.goal_x * 5 - 4.1f * 5) <= 0x13 + 1 && distant.unit.goal_x < distant.unit.x
           && distant.path_type == 7 && distant.path_steps == 0x28 && distant.chase != 0);
    // 1 cell: the step doesn't act, the player beside it is its target.
    const auto close_by = minion_after(1.f, true);
    assert(close_by.chase == 0 && !close_by.leader_lost);
    // A dead leader: +0xc cleared, never again.
    const auto lost = minion_after(8.f, false);
    assert(lost.leader_lost);
    std::puts("OK: a minion 8 cells off its leader walks back to it; 1 off it doesn't; its leader dead, it's lost");
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const bool cases = argc > 1 && std::string_view(argv[1]) == "--leader-follow";
    if (!cases) rule_check();
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) {
        if (cases) { std::fputs("--leader-follow needs D2_MPQ_DIR\n", stderr); return 1; }
        std::puts("OK: the rule's branches; SKIP: the walk check, no d2data.mpq (D2_MPQ_DIR)");
        return 0;
    }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) {
        if (cases) { std::fputs("--leader-follow needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        std::puts("OK: the rule's branches; SKIP: the walk check, needs the 1.14d data (D2_PATCH_INSTALLER)");
        return 0;
    }
    return cases ? follow_cases(*data) : walk_check(*data);
} catch (const std::exception& error) {
    std::printf("test_leader_follow: %s\n", error.what());
    return 1;
}
