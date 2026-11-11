// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The quest timers step on the quest clock through World::tick: a step
// every 20th frame (FUN_0052d870 -> FUN_00543e10), and a timer of n ticks
// runs on the (n + 1)th step (issue #383). Each case sets one timer as its
// quest would, then counts the frames and steps till it runs.

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
#include <functional>

namespace fs = std::filesystem;
using namespace d2d::game;
using d2d::rules::quest_timer_steps;

namespace {

// The frames and quest steps World::tick takes till `done`; the steps must
// be `steps` exactly, the last on a step's frame.
void runs_after(World& world, std::uint32_t& now_ms, int steps, const char* what, const std::function<bool()>& done) {
    const std::uint32_t count_at = world.quest_clock.count;
    int frames = 0;
    while (!done() && frames < 4000) {
        world.tick({}, now_ms + kTickMs, now_ms);
        now_ms += kTickMs;
        ++frames;
    }
    const auto taken = int(world.quest_clock.count - count_at);
    std::printf("%s: %d frames, %d quest steps\n", what, frames, taken);
    assert(done() && taken == steps && world.quest_clock.frame % d2d::rules::QuestClock::kFrames == 0);
    assert(frames > (steps - 1) * 20 && frames <= steps * 20);
}

}  // namespace

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

    // Sisters' Burial Grounds: Blood Raven's 0xf-tick log (LAB_00590bf0).
    world.burial.state = 4;
    world.burial.log = 2;
    world.burial.log_in = quest_timer_steps(0xf);
    runs_after(world, now_ms, 16, "Blood Raven's log", [&] { return world.burial.log == 3; });

    // The Den of Evil cleared: "Return to Akara" 8 ticks on (0x5903d9 -> LAB_00590230),
    // only while it's still cleared, state 4 (0x590232): Akara's reward first keeps 0xd.
    world.den.state = 4;
    world.den.log = 2;
    world.den_log_in = quest_timer_steps(8);
    runs_after(world, now_ms, 9, "the Den's log", [&] { return world.den.log == 5; });
    world.den.state = 5;
    world.den.log = 0xd;
    world.den_log_in = quest_timer_steps(8);
    runs_after(world, now_ms, 9, "the Den's log after the reward", [&] { return world.den_log_in == 0; });
    assert(world.den.log == 0xd);

    // The Gidbinn's altar used: a3q3's 7-tick timer (0x5b9c22 -> FUN_005b9a30).
    world.gidbinn_in = quest_timer_steps(7);
    runs_after(world, now_ms, 8, "the Gidbinn's timer", [&] { return world.gidbinn_due; });
    world.gidbinn_due = false;

    // The Dark Wanderer gone: a3q7's afterglow 2 ticks on (0x5bd4ce -> FUN_005bd390).
    world.afterglow_made = true;                                  // nothing to make here: only the timer
    world.afterglow_in = quest_timer_steps(2);
    runs_after(world, now_ms, 3, "the afterglow's timer", [&] { return world.afterglow_due; });

    // Izual dead: a4q1's 3-tick timer, his ghost (0x5b4126 -> 0x5b3e60).
    world.izual_ghost_in = quest_timer_steps(3);
    runs_after(world, now_ms, 4, "Izual's ghost's timer", [&] { return world.izual_ghost_in == 0; });
    world.izual_ghost_due = false;

    // Nihlathak dead: a5q4's 8-tick log (0x58b84a -> 0x58b770: log 4).
    world.betrayal.log = 2;
    world.betrayal_log_in = quest_timer_steps(8);
    runs_after(world, now_ms, 9, "Nihlathak's log", [&] { return world.betrayal.log == 4; });

    // An Ancient fallen: a5q5's statues' return timer, 2 ticks (0x58c9da -> FUN_0058bd50).
    world.statues_back_in = quest_timer_steps(2);
    runs_after(world, now_ms, 3, "the statues' return timer", [&] { return world.statues_back_due; });
    world.statues_back_due = false;

    std::printf("OK: the quest timers step every 20 frames\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_clock_world: %s\n", error.what());
    return 1;
}
