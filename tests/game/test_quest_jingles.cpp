// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The four jingles game.exe's client starts by itself (rules::QuestMusic's
// object rows): the Horadric Staff's as it goes into the orifice (S->C 0x89
// event 3, FUN_0059dd80), the Hellforge's on the third blow (event 14,
// 0x5b5dfc), the Compelling Orb's and Izual's as the orb monster (0x16e) and
// Izual (0x100) die (the questcomplete mod's client hook, 0x4ad2f0). Each
// plays only for a player whose room or one next to it holds the unit
// (FUN_0065a620 over FUN_00619790's list); none on making the staff.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <object_ids.hpp>
#include <quest_music.hpp>
#include <quests_act3.hpp>
#include <quests_act4.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    namespace level_ids = d2d::rules::level_ids;

    d2d::rules::Rng rng(7);
    const auto hero = [&] {
        constexpr int kSorceress = 1;
        auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
        Character character;
        character.character_class = kSorceress;
        character.name = "Headless";
        character.header = made.header;
        character.stats = made.stats;
        character.items = made.items;
        character.panel = panel_stats(*data, character.header, character.items, character.stats);
        return character;
    };
    World world(&*data, -1, -1);
    world.enter(hero());
    auto& second = world.add_player();
    second.character = hero();
    World::Player& first = *world.actor;

    // `level` with its objects, the second player away from (x, y): a room
    // whose near list doesn't hold it.
    const auto enter_level = [&](const Level& level) {
        populate_level(*data, world.fight.spawning, level);
        world.level = &level;
        world.npc_states = npc_start(level);
    };
    const auto far_from = [&](float x, float y) {
        for (const auto& room : world.level->rooms) {
            const float room_x = float(room.x) + float(room.width) / 2, room_y = float(room.y) + float(room.height) / 2;
            if (!world.near_room(room_x, room_y, x, y)) return std::pair{ room_x, room_y };
        }
        throw std::runtime_error("no room away from the spot");
    };
    const auto stand = [&](float x, float y) {
        first.player.x = x; first.player.y = y;
        const auto [away_x, away_y] = far_from(x, y);
        second.player.x = away_x; second.player.y = away_y;
        first.events.clear(); second.events.clear();
    };
    // The jingles `player` was sent: each with its sequence (FUN_004dcd40's).
    const auto heard = [&](const World::Player& player, std::string_view jingle) {
        int count = 0;
        const auto row = d2d::rules::quest_music_jingle(jingle);
        for (const auto& event : player.events)
            if (const auto* music = std::get_if<ev::MusicCue>(&event); music && music->sound == data->sound_index.at(std::string(jingle))) {
                assert(row && music->back_ms == row->back * 40 && music->jingle_ms == 0 && music->from_block == row->from_block);
                ++count;
            }
        return count;
    };
    const auto give = [&](World::Player& player, std::string_view code, bool in_hand) {
        d2d::d2s::Item item;
        item.code = std::string(code); item.identified = true;
        if (in_hand) { item.location = d2d::d2s::item_location::kEquipped; item.slot = d2d::d2s::body_location::kRightArm; }
        else { item.location = d2d::d2s::item_location::kStored; item.panel = d2d::d2s::item_panel::kInventory; }
        player.character.items.push_back(std::move(item));
    };
    const auto find_object = [&](const Level& level, int operate_fn) {
        const auto found = std::ranges::find(level.npcs, operate_fn, &Npc::operate_fn);
        return found == level.npcs.end() ? -1 : int(found - level.npcs.begin());
    };

    // The Horadric Staff into the orifice (whichever tomb holds it).
    const Level* tomb = nullptr;
    int orifice = -1;
    for (int id = level_ids::kFirstTalRashasTomb; id <= level_ids::kLastTalRashasTomb && orifice < 0; ++id)
        if (const auto* level = data->level(id)) {
            enter_level(*level);
            if ((orifice = find_object(*level, d2d::rules::operate_fn::kOrifice)) >= 0) tomb = level;
        }
    if (!tomb) { std::printf("SKIP: no orifice in Tal Rasha's tombs\n"); return D2D_TEST_SKIPPED; }
    enter_level(*tomb);
    const auto& hole = tomb->npcs[std::size_t(orifice)];
    stand(hole.x, hole.y);
    give(first, "hst", false);
    world.operate(orifice, 1000);
    assert(heard(first, "music_quest_horadric") == 1 && heard(second, "music_quest_horadric") == 0);
    second.player.x = hole.x; second.player.y = hole.y + 1;   // both by it: both hear it
    first.events.clear(); second.events.clear();
    world.operated.erase({ tomb, orifice });
    give(first, "hst", false);
    world.operate(orifice, 2000);
    assert(heard(first, "music_quest_horadric") == 1 && heard(second, "music_quest_horadric") == 1);
    std::puts("OK: the Horadric Staff's jingle at the orifice, by it");

    // The Hellforge: the third blow, the hammer in hand.
    const auto* river = data->level(level_ids::kRiverOfFlame);
    if (!river) { std::printf("SKIP: no River of Flame\n"); return D2D_TEST_SKIPPED; }
    enter_level(*river);
    const int forge_index = find_object(*river, d2d::rules::operate_fn::kHellforge);
    if (forge_index < 0) { std::printf("SKIP: no Hellforge in the River of Flame\n"); return D2D_TEST_SKIPPED; }
    const auto& forge = river->npcs[std::size_t(forge_index)];
    stand(forge.x, forge.y);
    world.hellforge.open = true; world.hellforge.forge_mode = 2;
    give(first, d2d::rules::HellforgeQuest::kHammer, true);
    for (int blow = 1; blow <= d2d::rules::HellforgeQuest::kBlows; ++blow) {
        world.operate(forge_index, 3000 + std::uint32_t(blow) * 1000);
        assert(heard(first, "music_quest_forge") == (blow == d2d::rules::HellforgeQuest::kBlows ? 1 : 0));
    }
    assert(heard(second, "music_quest_forge") == 0);
    std::puts("OK: the Hellforge's jingle on the third blow, by it");

    // The Compelling Orb: the second hit with Khalim's Will kills the orb monster.
    const auto* travincal = data->level(level_ids::kTravincal);
    if (!travincal) { std::printf("SKIP: no Travincal\n"); return D2D_TEST_SKIPPED; }
    enter_level(*travincal);
    const int orb_index = find_object(*travincal, d2d::rules::operate_fn::kCompellingOrb);
    if (orb_index < 0) { std::printf("SKIP: no compelling orb in Travincal\n"); return D2D_TEST_SKIPPED; }
    const auto& orb = travincal->npcs[std::size_t(orb_index)];
    Monster orb_monster;
    orb_monster.type = int(data->mon_bin[0x16e]);
    orb_monster.unit.x = orb.x; orb_monster.unit.y = orb.y;
    world.fight.monsters.push_back(std::move(orb_monster));
    stand(orb.x, orb.y);
    give(first, d2d::rules::KhalimQuest::kWillCode, true);
    world.operate(orb_index, 8000);
    assert(heard(first, "music_quest_compelling") == 0);   // the first hit
    world.operate(orb_index, 9000);
    assert(heard(first, "music_quest_compelling") == 1 && heard(second, "music_quest_compelling") == 0);
    assert(world.fight.monsters.back().gone);
    std::puts("OK: the Compelling Orb's jingle as the orb monster goes, by it");

    // Izual's death.
    const auto* plains = data->level(level_ids::kPlainsOfDespair);
    if (!plains || plains->rooms.empty()) { std::printf("SKIP: no Plains of Despair\n"); return D2D_TEST_SKIPPED; }
    world.level = plains;
    const auto& spot = plains->rooms.front();
    const float izual_x = float(spot.x) + float(spot.width) / 2, izual_y = float(spot.y) + float(spot.height) / 2;
    stand(izual_x, izual_y);
    world.act4_kill({ .type = d2d::rules::IzualQuest::kIzual, .x = izual_x, .y = izual_y, .level = plains->id, .killer = first.id }, 10000);
    assert(heard(first, "music_quest_izual") == 1 && heard(second, "music_quest_izual") == 0);
    std::puts("OK: Izual's jingle at his death, by him");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_jingles: %s\n", error.what());
    return 1;
}
