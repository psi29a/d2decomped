// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The orifice's item panel. A click with the Horadric Staff carried
// (FUN_0059dc70): the player busy on it, mode 1, S->C 0x58 kind 0; without
// it, nothing opens. C->S 0x44 (FUN_005852e0): action 2 answers kind 1 and
// sets mode 0; action 3 with another item answers 4; with the staff, 5, and
// the staff goes in (FUN_0059dd80). In mode 1 a click by the busy player
// lets them go, mode 2. ev::ObjectPanel survives the view codec.

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <object_ids.hpp>
#include <protocol.hpp>
#include <quests.hpp>
#include <replication.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    {   // The view codec carries it (event 12).
        GameData empty_data;
        View sent;
        sent.level = &empty_data.town;
        sent.events = { ev::ObjectPanel{ 9, 0 }, ev::ObjectPanel{ 9, 5 } };
        ViewEncoder encoder;
        View received;
        assert(apply_view(empty_data, encode_view(empty_data, sent, encoder), received));
        assert(received.events.size() == 2);
        const auto& opened = std::get<ev::ObjectPanel>(received.events[0]);
        const auto& taken = std::get<ev::ObjectPanel>(received.events[1]);
        assert(opened.object == 9 && opened.kind == 0 && taken.object == 9 && taken.kind == 5);
        std::printf("OK: ObjectPanel round-trips the view codec\n");
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1);
    namespace level_ids = d2d::rules::level_ids;

    d2d::rules::Rng rng(7);
    constexpr int kPaladin = 3;
    auto made = new_character(*data, kPaladin, "Bearer", false, true, rng);
    Character character;
    character.character_class = kPaladin;
    character.name = "Bearer";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto& player = *world.actor;

    const Level* tomb = nullptr;
    int orifice = -1;
    for (int id = level_ids::kFirstTalRashasTomb; id <= level_ids::kLastTalRashasTomb && !tomb; ++id)
        if (const auto* level = data->level(id)) {
            populate_level(*data, world.fight.spawning, *level);
            const auto found = std::ranges::find(level->npcs, d2d::rules::operate_fn::kOrifice, &Npc::operate_fn);
            if (found != level->npcs.end()) { tomb = level; orifice = int(found - level->npcs.begin()); }
        }
    if (!tomb) { std::printf("SKIP: no orifice in Tal Rasha's tombs\n"); return D2D_TEST_SKIPPED; }
    world.level = tomb;
    world.npc_states = npc_start(*tomb);
    const auto& hole = tomb->npcs[std::size_t(orifice)];
    player.player.x = hole.x; player.player.y = hole.y + 1;

    const auto answered = [&]() -> int {
        int kind = -1;
        for (const auto& event : player.events)
            if (const auto* panel = std::get_if<ev::ObjectPanel>(&event); panel && panel->object == orifice) kind = panel->kind;
        player.events.clear();
        return kind;
    };
    const auto mode = [&] {
        const auto found = world.doors.find({ tomb, orifice });
        return found == world.doors.end() ? 0 : found->second.mode;
    };
    const auto carry = [&](std::string_view code, int id) {
        d2d::d2s::Item item;
        item.code = std::string(code); item.identified = true; item.id = id;
        item.location = d2d::d2s::item_location::kStored; item.panel = d2d::d2s::item_panel::kInventory;
        player.character.items.push_back(std::move(item));
    };
    const auto& quest_bits = world.quests();

    // No staff: nothing opens.
    player.events.clear();
    world.operate(orifice, 1000);
    assert(answered() == -1 && mode() == 0 && player.busy_object == -1);
    // The staff carried: the panel opens, mode 1, busy on it.
    carry("hst", 8001);
    carry("rin", 8002);
    world.operate(orifice, 2000);
    assert(answered() == 0 && mode() == 1 && player.busy_object == orifice);
    // Shut with none (action 2): kind 1, mode 0, let go.
    world.apply(cmd::ItemToObject{ orifice, -1, 2 }, 3000);
    assert(answered() == 1 && mode() == 0 && player.busy_object == -1);
    // Clicked again in mode 1 by the busy player: let go, mode 2, no answer.
    world.operate(orifice, 4000);
    assert(answered() == 0 && mode() == 1);
    world.operate(orifice, 5000);
    assert(answered() == -1 && mode() == 2 && player.busy_object == -1);
    world.apply(cmd::ItemToObject{ orifice, -1, 2 }, 6000);
    assert(answered() == 1 && mode() == 0);
    // Another item put in: not taken (kind 4), nothing changes.
    world.operate(orifice, 7000);
    assert(answered() == 0);
    world.apply(cmd::ItemToObject{ orifice, 8002, 3 }, 8000);
    assert(answered() == 4 && mode() == 1 && !d2d::rules::quest::bit(quest_bits, 10, 0));
    // The staff: taken (kind 5), placed: the Horadric Staff done, the staff gone.
    world.apply(cmd::ItemToObject{ orifice, 8001, 3 }, 9000);
    assert(answered() == 5 && player.busy_object == -1 && world.operated.contains({ tomb, orifice }));
    assert(d2d::rules::quest::bit(quest_bits, 10, 0) && d2d::rules::quest::bit(quest_bits, 10, 13) && !world.carries("hst"));
    // Placed: a click does nothing more.
    world.operate(orifice, 10000);
    assert(answered() == -1);
    std::printf("OK: the orifice opens its panel, shuts on 0x44 2, takes only the staff on 0x44 3\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_orifice_panel: %s\n", error.what());
    return 1;
}
