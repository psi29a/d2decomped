// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The client's state-inferno list (cltstfunc 15, LAB_004e3060: Inferno, Arctic Blast). While state 0xc is off on the unit the
// prgsound plays on no unit; the start then makes the client's list (FUN_006251f0 flags 2, timeout 0x15 frames) or, with a list
// on, sets its timeout to 7 (FUN_006260b0); FUN_00480810 -> FUN_00627460 counts it down a frame at a time. So a second start
// while the list is on is silent, and one after it ran out plays again.
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;

// Checks the client's state-inferno list: the prgsound plays once, a start while it is on holds 7 frames (LAB_004e3060).
int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir) { std::printf("SKIP: D2_MPQ_DIR\n"); return D2D_TEST_SKIPPED; }
    const fs::path data_dir = mpq_dir;
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->patched);

    int skill_id = -1;   // Arctic Blast: the cltstfunc 15 row with a prgsound
    for (int id = 0; id < 400 && skill_id < 0; ++id)
        if (const auto* skill = data->skills.get(id); skill && skill->clt_st_func == 15 && skill->prg_sound > 0) skill_id = id;
    assert(skill_id >= 0);
    const int sound = data->skills.get(skill_id)->prg_sound;

    d2d::rules::Rng rng(7);
    auto made = new_character(*data, d2d::d2s::kSorceress, "Headless", false, true, rng);
    Character hero;
    hero.character_class = d2d::d2s::kSorceress;
    hero.name = "Headless";
    hero.header = made.header;
    hero.stats = made.stats;
    hero.items = made.items;
    hero.panel = panel_stats(*data, hero.header, hero.items, hero.stats);
    World world(&*data, -1, -1);
    world.enter(hero);
    world.fight.update_fighters(1000 * kTickMs);
    auto& actor = *world.actor;
    actor.swing_skill = skill_id;

    const auto plays = [&] { return std::ranges::count_if(world.cues.due, [&](const auto& cue) { return cue.sound == sound && cue.unit == 0; }); };
    const std::uint32_t start = 1000 * kTickMs;
    const auto cast = [&](int frame) { world.fight.set_pmode(kModeSC, start + std::uint32_t(frame) * kTickMs); return plays(); };
    world.cues.due.clear();
    const auto first = cast(0);       // state off: the sound, then the list (0x15 frames)
    const auto second = cast(10);     // the list is on: nothing, its timeout set to 7 (to frame 17)
    const auto third = cast(16);      // still on: 7 more frames (to frame 23)
    const auto fourth = cast(24);     // it ran out at frame 23: the sound again
    // The server's state (Hero::burning) on counts as the state on too: the start is silent.
    actor.inferno_list = 0;
    actor.burning = start + 100 * kTickMs;
    const auto fifth = cast(50);
    assert(first == 1 && second == 1 && third == 1 && fourth == 2 && fifth == 2);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
