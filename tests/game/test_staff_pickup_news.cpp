// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// a2q2's pickup (FUN_00599a30, record 9's event 4): Items.txt quest 10 binds tr1, vip, box, msf and hst to it.
// The scroll with bit 3 unset, the amulet, the Cube or the shaft sends the log to the picker (FUN_00544190 at
// 0x599acb, or FUN_00599570 for the shaft) when FUN_00599510 passes (rules::quest_log_goes_to): World::item_event
// raises ev::QuestNews{10}. The staff, a read scroll, a done quest, a record FUN_00544410 closed and a corpse's
// event send none. tools/emu/staff_pickup_news.py checks FUN_00599a30's side. Needs the game data.

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string_view>
#include <variant>

namespace fs = std::filesystem;
using namespace d2d::game;

// Drives World::item_event (FUN_00599a30) over the a2q2 parts and gates.
int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::printf("FAIL: game data\n"); return 1; }
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

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
    namespace quest = d2d::rules::quest;
    using Staff = d2d::rules::HoradricStaffQuest;

    // The number of a2q2 news events one pickup of `code` raises.
    const auto news = [&](std::string_view code, bool corpse = false) {
        world.actor->events.clear();
        world.item_event(code, 0, corpse);
        return std::ranges::count_if(world.actor->events, [](const Event& event) {
            const auto* sent = std::get_if<ev::QuestNews>(&event);
            return sent && sent->quest == Staff::kQuest;
        });
    };
    int failures = 0;
    const auto expect = [&](std::string_view what, long got, long want) {
        if (got == want) return;
        std::printf("FAIL: %.*s: %ld news, want %ld\n", int(what.size()), what.data(), got, want);
        ++failures;
    };

    expect("vip", news("vip"), 1);
    expect("box", news("box"), 1);
    expect("msf", news("msf"), 1);
    expect("tr1 unread", news("tr1"), 1);
    expect("hst", news("hst"), 0);
    expect("bks (another record's)", news("bks"), 0);
    expect("vip on a corpse", news("vip", true), 0);
    quest::set(world.quests(), Staff::kQuest, 3);
    expect("tr1 read (bit 3)", news("tr1"), 0);
    expect("vip, bit 3", news("vip"), 1);
    quest::set(world.quests(), Staff::kQuest, 0);
    expect("vip, done (bit 0)", news("vip"), 0);
    quest::set(world.quests(), Staff::kQuest, 13);
    expect("vip, done here (bits 0, 13)", news("vip"), 1);
    quest::set(world.quests(), Staff::kQuest, 0, false);
    quest::set(world.quests(), Staff::kQuest, 13, false);
    world.closed_at_join[std::size_t(Staff::kQuest)] = true;
    expect("vip, record closed at the join (FUN_00544410)", news("vip"), 0);
    if (failures) return 1;
    std::printf("OK: a2q2's pickups send its log as FUN_00599a30 does\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
