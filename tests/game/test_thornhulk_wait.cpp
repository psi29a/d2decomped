// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The ThornHulk's frenzy (FUN_005f4850, 0x5f4850) starts its cast first,
// then waits aip5 past FUN_005415a0(1) (0x5415a0): the cast mode's end
// event, queued at the start frame + the mode's frames + 1 (FUN_0054fe10).
// So the next think is aip5 + the cast's frames + 1 on. Needs the game data.

#include <ai.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <think_scratch.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

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
    const auto hulk = data->monsters.by_id.find("thornhulk1");
    assert(hulk != data->monsters.by_id.end());
    const auto& type_info = data->monsters.types[hulk->second];
    assert(type_info.ai_name == "ThornHulk");
    d2d::rules::Rng rng{ 3 };
    const Monster monster = make_monster(*data, int(hulk->second), 10.f, 10.f, rng, 0);
    const auto& timing = data->npc_timing(monster.npc, type_info.sk_mode[0]);
    const int frames = int(timing.length_ms() / 40);
    assert(frames > 0);
    const int frame = 1000;
    const int end_event = mode_end_event(*data, monster, type_info.sk_mode[0], frame);
    assert(end_event == frame + frames + 1);
    // In melee, no cooldown: aip1 % fights, aip4 % frenzies; aip5 = 7.
    int state = 0, state2 = 0;
    d2d::rules::ThinkIn input{ .aip = { 100, 0, 0, 100, 7, 3 }, .state = &state };
    input.in_melee = true; input.state2 = &state2; input.frame = frame; input.next_event = end_event;
    input.skill[0] = true;
    ThinkScratch scratch;
    scratch.point(input);
    d2d::rules::Rng roll{ 5 };
    const auto act = d2d::rules::mon_think("ThornHulk", input, roll, [](int, bool) { return false; });
    assert(act.act == d2d::rules::MonAct::skill && act.n == 0);
    assert(act.wait == 7 + frames + 1);
    std::printf("thornhulk wait: %d frames (cast %d)\n", act.wait, frames);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_thornhulk_wait: %s\n", error.what());
    return 1;
}
