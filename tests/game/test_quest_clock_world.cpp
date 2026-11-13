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
#include <level_ids.hpp>
#include <object_ids.hpp>
#include <quest_clock.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <quests_act4.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <tuple>
#include <variant>

namespace filesystem = std::filesystem;
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
    std::fflush(stdout);   // seen before an assert fails
    assert(done() && taken == steps && world.quest_clock.frame % d2d::rules::QuestClock::kFrames == 0);
    assert(frames > (steps - 1) * 20 && frames <= steps * 20);
}

}  // namespace

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const filesystem::path data_dir = mpq_dir ? filesystem::path(mpq_dir) : filesystem::path(home ? home : "") / "Workspace/private/diablo2";
    if (!filesystem::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? filesystem::path(patch) : filesystem::path{}, 0x1234);
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
    // Its guardians come at the altar's level only (FUN_005b9a30: Act 3's map at the
    // altar's spot), not by whatever object has the altar's index where the player is.
    world.gidbinn_altar = 0;
    world.gidbinn_level = data->level(d2d::rules::BladeQuest::kFlayerJungle);
    world.actor->player.x = world.level->npcs[0].x;
    world.actor->player.y = world.level->npcs[0].y;
    world.gidbinn_in = quest_timer_steps(7);
    runs_after(world, now_ms, 8, "the Gidbinn's timer", [&] { return world.gidbinn_in == 0; });
    // One look (#394): every path of FUN_005b9a30 returns 1, so with no player
    // there it's spent; a player by the altar later brings nothing.
    assert(!world.gidbinn_due && !world.blade.spawned);
    world.gidbinn_level = world.level;   // the player by "the altar" now
    for (int frame = 0; frame < 5; ++frame, now_ms += kTickMs) world.tick({}, now_ms + kTickMs, now_ms);
    assert(!world.blade.spawned);
    world.gidbinn_altar = -1;
    world.gidbinn_level = nullptr;

    // The Dark Wanderer gone: a3q7's afterglow 2 ticks on (0x5bd4ce -> FUN_005bd390).
    world.afterglow_made = true;                                  // nothing to make here: only the timer
    world.afterglow_in = quest_timer_steps(2);
    runs_after(world, now_ms, 3, "the afterglow's timer", [&] { return world.afterglow_due; });

    // Izual dead: a4q1's 3-tick timer, his ghost (0x5b4126 -> 0x5b3e60).
    world.izual_ghost_in = quest_timer_steps(3);
    // He died on the Plains of Despair and the player went to town: the ghost goes
    // where he died (0x5b3eaf: Act 4's map at his spot), not where the player is.
    const auto* plains = data->level(d2d::rules::level_ids::kPlainsOfDespair);
    assert(plains);
    const auto& ghost_code = data->mon_npc[std::size_t(d2d::rules::IzualQuest::kGhost)].code;
    const auto ghosts = [&](const Level& where) { return std::ranges::count(where.npcs, ghost_code, &Npc::code); };
    const auto on_plains = ghosts(*plains), in_town = ghosts(*world.level);
    world.izual.state = 4;
    world.izual.ghost_due = true;
    world.izual.spot_x = float(plains->ds1.width()) / 2;
    world.izual.spot_y = float(plains->ds1.height()) / 2;
    world.izual_level = plains;
    world.izual_ghost_in = quest_timer_steps(3);
    runs_after(world, now_ms, 4, "Izual's ghost's timer", [&] { return world.izual_ghost_in == 0; });
    for (int frame = 0; frame < 2; ++frame, now_ms += kTickMs) world.tick({}, now_ms + kTickMs, now_ms);
    assert(ghosts(*plains) == on_plains + 1 && ghosts(*world.level) == in_town && world.izual.log == 3);

    // Mephisto dead: a3q6's 12-tick timer (0x5bc9d6 -> 0x5bc720), the log 4 sent again.
    world.guardian.log = 2;
    world.guardian.arm_log();
    world.actor->events.clear();
    runs_after(world, now_ms, 13, "Mephisto's log", [&] { return world.guardian.log == 4; });
    assert(std::ranges::any_of(world.actor->events, [](const auto& event) {
        const auto* news = std::get_if<ev::QuestNews>(&event);
        return news && news->quest == d2d::rules::GuardianQuest::kQuest;
    }));

    // The seals broken: a4q2's release timer (1 tick, again till Diablo:
    // FUN_005b4be0), its 10th run 20 steps on.
    world.terror.arm();
    runs_after(world, now_ms, 20, "Diablo's release", [&] { return world.terror.release_runs == d2d::rules::TerrorQuest::kReleaseRuns; });
    world.terror.armed = false;
    world.terror.diablo_made = false;

    // The Cairn Stones up: their portal's 1-tick timer (0x59366e -> FUN_00592d50).
    // Stony Field isn't built here: a StoneAlpha stands in for its preset.
    auto* stony = const_cast<Level*>(data->level(d2d::rules::CainQuest::kStony));   // GameData owns its levels mutable
    assert(stony);
    Npc stone_alpha;
    stone_alpha.object_id = d2d::rules::object_ids::kStoneAlpha;
    stone_alpha.x = 20;
    stone_alpha.y = 20;
    stony->npcs.push_back(stone_alpha);
    world.portal[2] = {};
    world.stones_portal_in = quest_timer_steps(1);
    runs_after(world, now_ms, 2, "the stones' portal", [&] { return world.stones_portal_in == 0; });
    assert(world.portal[2].level == stony);   // FUN_0056d130 at StoneAlpha + 4 (its nearest free spot)
    world.portal[2] = {};
    stony->npcs.pop_back();

    // Nihlathak dead: a5q4's 8-tick log (0x58b84a -> 0x58b770: log 4).
    world.betrayal.log = 2;
    world.betrayal_log_in = quest_timer_steps(8);
    runs_after(world, now_ms, 9, "Nihlathak's log", [&] { return world.betrayal.log == 4; });

    // An Ancient fallen: a5q5's statues' return timer, 2 ticks (0x58c9da -> FUN_0058bd50).
    world.statues_back_in = quest_timer_steps(2);
    runs_after(world, now_ms, 3, "the statues' return timer", [&] { return world.statues_back_due; });
    world.statues_back_due = false;

    // Split levels: the quest step runs on the first turn (a second player's,
    // on Blood Moor), and the Gidbinn's look waits for the altar's level's
    // turn (the lead's, last), where the lead stands by "the altar".
    const Level* town = world.level;
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    assert(moor && !world.blade.spawned);
    auto wanderer = character;
    wanderer.name = "Wanderer";
    auto& second = world.join(wanderer);
    second.on_level = moor;
    std::tie(second.player.x, second.player.y) = moor->nearest_free(float(moor->ds1.width()) / 2, float(moor->ds1.height()) / 2);
    world.lead().on_level = town;
    world.split_levels = true;
    world.gidbinn_altar = 0;
    world.gidbinn_level = town;
    world.actor->player.x = town->npcs[0].x;
    world.actor->player.y = town->npcs[0].y;
    world.gidbinn_in = quest_timer_steps(7);
    runs_after(world, now_ms, 8, "the Gidbinn's timer, split levels", [&] { return world.gidbinn_in == 0; });
    assert(world.blade.spawned && !world.gidbinn_due);
    world.split_levels = false;
    world.gidbinn_altar = -1;
    world.gidbinn_level = nullptr;

    // A new game on the same World (World::new_game): its frame and quest
    // count start over (a game's own +0xa8 and +0x10f4 +0x14), so its first
    // step is a full 20 frames in.
    assert(world.quest_clock.frame > 0 && world.quest_clock.count > 0);
    world.enter(character);
    assert(world.quest_clock.frame == 0 && world.quest_clock.count == 0);

    std::printf("OK: the quest timers step every 20 frames\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_clock_world: %s\n", error.what());
    return 1;
}
