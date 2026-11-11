// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The quest timers step on the quest clock through World::tick: a step
// every 20th frame (FUN_0052d870 -> FUN_00543e10), so Sisters' Burial
// Grounds' 0xf-tick log (LAB_00590bf0) comes on the 16th step, 301..320
// frames after the kill, not 15 frames on (issue #383).

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <quest_clock.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

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

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Clock", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Clock";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    World world(&*data, -1, -1);
    world.enter(character);
    std::uint32_t now_ms = 60 * kTickMs;
    for (int warm = 0; warm < 7; ++warm, now_ms += kTickMs) world.tick({}, now_ms + kTickMs, now_ms);

    // Blood Raven just died (BurialQuest::killed: state 4, its timer set).
    world.burial.state = 4;
    world.burial.log = 2;
    world.burial.log_in = d2d::rules::quest_timer_steps(0xf);
    const std::uint32_t count_at = world.quest_clock.count;
    int frames = 0;
    while (world.burial.log != 3 && frames < 1000) {
        world.tick({}, now_ms + kTickMs, now_ms);
        now_ms += kTickMs;
        ++frames;
    }
    std::printf("the log after %d frames, %u quest steps\n", frames, world.quest_clock.count - count_at);
    assert(world.burial.log == 3);
    assert(world.quest_clock.count - count_at == 16 && world.quest_clock.frame % d2d::rules::QuestClock::kFrames == 0);
    assert(frames > 15 * 20 && frames <= 16 * 20);
    std::printf("OK: the quest timers step every 20 frames\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_clock_world: %s\n", error.what());
    return 1;
}
