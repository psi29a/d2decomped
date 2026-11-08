// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// An NPC with a talk record greets as its menu opens (FUN_004b66b0, kind 0)
// only while the record's +0x15 is set: it clears as it greets, and
// FUN_004b3e10 sets it again when the player leaves town. So Charsi greets
// once, not again till the player's been out of town. Needs the data.

#include <character.hpp>
#include <character_store.hpp>
#include <cues.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <npc_talk.hpp>
#include <protocol.hpp>
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
namespace fs = std::filesystem;

int main() try {
    assert(has_talk_record(d2d::rules::monster_ids::kCharsi) && has_talk_record(d2d::rules::monster_ids::kFlavie) && !has_talk_record(0));
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

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
    World world(&*data, -1, -1);
    world.enter(character);
    auto* actor = world.actor;
    auto& town = data->town;
    assert(world.level == &town);
    const auto charsi = std::ranges::find(town.npcs, d2d::rules::monster_ids::kCharsi, &Npc::hc_idx);
    assert(charsi != town.npcs.end());
    const int npc = int(charsi - town.npcs.begin());
    const std::uint64_t voice = unit_key(kSoundOnMonster, -1 - npc);

    std::uint32_t now = 1000 * kTickMs;
    const auto talk = [&] {   // stand by her, open her menu: the greetings it cued
        actor->player.x = world.npc_states[std::size_t(npc)].x + 0.4f;
        actor->player.y = world.npc_states[std::size_t(npc)].y;
        actor->player.walking = false;
        world.cues.due.clear();
        world.apply(cmd::Interact{ npc }, now);
        for (int tick = 0; tick < 200 && actor->interact_npc >= 0; ++tick) {
            world.tick({}, now + kTickMs, now);
            now += kTickMs;
        }
        assert(actor->interact_npc == -1);
        return std::ranges::count(world.cues.due, voice, &Cues::Cue::unit);
    };
    assert(talk() == 1);
    assert(talk() == 0);                  // +0x15 cleared: no greeting
    Level outside = town;                 // out of town for a tick (FUN_00460e70 -> FUN_004b3e10)
    outside.id = d2d::rules::level_ids::kBloodMoor;
    world.level = &outside;
    world.tick({}, now + kTickMs, now);
    now += kTickMs;
    world.level = &town;
    assert(talk() == 1);
    std::printf("OK: a talk record greets once a stay in town\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_npc_greet_once: %s\n", error.what());
    return 1;
}
