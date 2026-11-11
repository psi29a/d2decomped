// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A quest's music sequence (rules::QuestMusic): the client's FUN_004dcd40
// arguments for each unit event in FUN_004cb9c0 and each object in
// FUN_004ad1a0, the class line's delay, and World::music_cue filling
// MusicCue from the table (needs the game data); the wire is test_game_rules'.

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <quest_music.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;

int main() try {
    using d2d::rules::quest_music_event, d2d::rules::quest_music_jingle, d2d::rules::quest_line_due;
    // {event, jingle, EDX, back (1st), line delay (4th), speaks (5th), from_block (2nd)}
    const auto row = [](int event) { return *quest_music_event(event); };
    assert(row(0x21).jingle == "music_quest_andariel" && row(0x21).jingle_at == 0 && row(0x21).back == 475 && row(0x21).line_at == 425 && !row(0x21).from_block);
    assert(row(0x22).jingle == "music_quest_bloodraven" && row(0x22).jingle_at == 475 && row(0x22).back == 800 && row(0x22).line_at == 525 && row(0x22).from_block);
    assert(row(0x23).jingle == "music_quest_den" && row(0x23).back == 300 && row(0x23).line_at == 250 && row(0x23).from_block);
    assert(row(0x25).jingle.empty() && row(0x25).back == 1500 && !row(0x25).speaks);
    assert(row(0x32).jingle_at == 475 && row(0x32).back == 1100 && row(0x32).line_at == 525);
    assert(row(0x34).back == 425 && row(0x34).line_at == 375 && row(0x42).back == 425 && !row(0x42).from_block);
    assert(row(0x4b).jingle == "music_quest_diablo" && row(0x4b).back == 475 && row(0x4b).line_at == 325);
    assert(!quest_music_event(0x24) && !quest_music_event(0));
    assert(quest_music_jingle("music_quest_horadric")->back == 725 && quest_music_jingle("music_quest_horadric")->from_block);
    assert(quest_music_jingle("music_quest_forge")->back == 350 && quest_music_jingle("music_quest_compelling")->back == 350);
    assert(quest_music_jingle("music_quest_izual")->back == 250 && !quest_music_jingle(""));
    assert(quest_line_due(0x23, 1000) == 1000 + 250 * 40 && quest_line_due(0x25, 1000) == 1000 && quest_line_due(0x24, 1000) == 1000);

    std::puts("quest music: the table");

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    if (!moor || moor->rooms.empty()) { std::printf("SKIP: no Blood Moor\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, 2, "Headless", false, true, made_rng);
    Character character;
    character.character_class = 2;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.level = moor;
    world.actor->events.clear();
    world.music_cue("music_quest_bloodraven");
    world.music_cue("music_quest_compelling");
    assert(world.actor->events.size() == 2);
    const auto& raven = std::get<ev::MusicCue>(world.actor->events[0]);
    const auto& orb = std::get<ev::MusicCue>(world.actor->events[1]);
    assert(raven.sound == data->sound_index.at("music_quest_bloodraven") && raven.jingle_ms == 475 * 40 && raven.back_ms == 800 * 40 && raven.from_block);
    assert(orb.jingle_ms == 0 && orb.back_ms == 350 * 40 && !orb.from_block);
    std::puts("quest music: World::music_cue");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_music: %s\n", error.what());
    return 1;
}
