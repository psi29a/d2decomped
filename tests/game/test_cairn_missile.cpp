// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Cairn Stones open (FUN_00593710): its last act before the 0x89 event is
// FUN_0056ede0(.., 1, 0x120, x + 6, y - 3), missile 288 (cairnstones) made at
// the way to Tristram, standing for its Range (300 frames), hitting nothing.
// Like every server missile (FUN_0059fa30 -> FUN_00555230 type 3 ->
// FUN_00552df0), it takes one game seed step for its unit seed {low, 666};
// The deciphered scroll's read shows the order the stones then take.
// Fight::missile_tick seeds the others the same way.

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <npc.hpp>
#include <protocol.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <spawning.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
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
    const auto stones = data->missiles.find("cairnstones");
    assert(stones != data->missiles.end() && stones->second.range == 300);
    const auto* stony_field = data->level(d2d::rules::level_ids::kStonyField);
    if (!stony_field) { std::printf("SKIP: no Stony Field\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Stones", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Stones";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    World world(&*data, -1, -1);
    world.enter(character);
    populate_level(*data, world.fight.spawning, *stony_field);
    world.level = stony_field;
    world.npc_states = npc_start(*stony_field);
    world.cain.active = true;
    world.actor->character.items.push_back({ .code = "bkd", .id = 7001 });   // the deciphered scroll
    // Read (C->S 0x3e): FUN_00593cb0 draws the order and sends it, each stone 0..4.
    world.apply(cmd::ReadScroll{ 7001 }, 900);
    const auto shown = std::ranges::find_if(world.actor->events, [](const Event& event) { return std::holds_alternative<ev::StoneOrder>(event); });
    assert(shown != world.actor->events.end() && world.cain.ordered);
    for (std::size_t place = 0; place < 5; ++place) assert(std::get<ev::StoneOrder>(*shown).order[place] == world.cain.order[place] - 17);
    constexpr std::uint32_t kStart = 1000;
    std::uint32_t now_ms = kStart;
    d2d::rules::Rng game_before;
    for (int step = 0; step < 5; ++step, now_ms += 40) {
        game_before = world.fight.spawning.game;
        const auto stone = std::ranges::find(stony_field->npcs, world.cain.order[std::size_t(step)], &Npc::object_id);
        if (stone == stony_field->npcs.end()) { std::printf("SKIP: Stony Field lacks a stone\n"); return D2D_TEST_SKIPPED; }
        assert(std::ranges::none_of(world.fight.missiles, [&](const Missile& missile) { return missile.info == &stones->second; }));
        world.cain_operate(int(stone - stony_field->npcs.begin()), now_ms);
    }
    assert(world.cain.stones_done);
    const auto flash = std::ranges::find(world.fight.missiles, &stones->second, &Missile::info);
    assert(flash != world.fight.missiles.end() && flash->visual_only);
    assert(flash->velocity_x == 0 && flash->velocity_y == 0);
    assert(flash->x == world.portal[2].x && flash->y == world.portal[2].y);
    // x + 6, y - 3 are subtiles: inside the ring, not 6 tiles out of it.
    const auto lambda = std::ranges::find(stony_field->npcs, 21, &Npc::object_id);
    assert(lambda != stony_field->npcs.end() && std::hypot(world.portal[2].x - (lambda->x + 1.2f), world.portal[2].y - (lambda->y - 0.6f)) < 1.f);
    assert(flash->dies - flash->born == 300 * kTickMs);
    const std::uint32_t stepped = game_before.next();
    assert(flash->seed && flash->seed->low == stepped && flash->seed->high == 666);
    assert(world.fight.spawning.game.low == game_before.low && world.fight.spawning.game.high == game_before.high);

    // Any other missile in flight: its seed at the frame's first missile tick.
    Missile shot{ &stones->second, flash->x, flash->y, 0, 0, 0, now_ms, now_ms + 400, {} };
    world.fight.missiles.push_back(std::move(shot));
    world.fight.missile_tick(now_ms);
    const std::uint32_t shot_seed = game_before.next();
    assert(world.fight.missiles.back().seed && world.fight.missiles.back().seed->low == shot_seed);
    assert(world.fight.spawning.game.low == game_before.low);
    std::printf("OK: the Cairn Stones' missile stands at the way to Tristram for 300 frames\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
