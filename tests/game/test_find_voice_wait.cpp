// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The class's find line (FUN_004cc270) waits 62 ticks after the class last
// spoke; a quest line still due (class_cue's later at_ms, unit event 0x22
// and the like) counts as speaking, so no find line goes ahead of it.

#include <character.hpp>
#include <character_store.hpp>
#include <cues.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
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
    const auto* den = data->level(d2d::rules::level_ids::kDenOfEvil);
    if (!den) { std::printf("SKIP: no Den of Evil\n"); return D2D_TEST_SKIPPED; }
    const auto found_line = data->sound_index.find("sorceress_act1_find_den");
    if (found_line == data->sound_index.end()) { std::printf("SKIP: no sorceress_act1_find_den\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    constexpr std::uint32_t kWait = 62 * 40;
    // Into the Den at 1000 ms with the class's last line due at `voice_at`:
    // the find lines cued once the 62 ticks are up.
    const auto find_lines = [&](std::uint32_t voice_at) {
        World world(&*data, -1, -1);
        world.enter(character);
        world.level = den;
        world.actor->voice_at = voice_at;
        world.find_voice(1000);
        world.find_voice(1000 + kWait);
        return std::ranges::count(world.cues.due, found_line->second, &Cues::Cue::sound);
    };
    assert(find_lines(0) == 1);
    assert(find_lines(1000 + kWait + 5000) == 0);   // a quest line still due
    std::puts("OK: a quest line still due holds the find line");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_find_voice_wait: %s\n", error.what());
    return 1;
}
