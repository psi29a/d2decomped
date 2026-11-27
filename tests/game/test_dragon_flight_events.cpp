// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Dragon Flight in play (do 52, FUN_005d7850 on each event of its sequence 21, rows 9 and 14): the row
// the event is read on (unit +0x38 >> 8, rules::seq_step_row) odd kicks the target where it stands, even
// flies to it; at the plain rate the kick comes first, then the flight, to the target's corpse when the
// kick killed it. A Shadow Master's plays its MonSeq sequence the same way, through its own think too.
#include <ai/shadows.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
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
#include <memory>
#include <string_view>
#include <utility>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && !data->town.walk.empty());

    // An Assassin on an open level, a zombie 3 cells east.
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kAssassin, "Headless", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kAssassin;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    auto world = std::make_unique<World>(&*data, -1, -1);
    world->enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    std::uint32_t now = 1000 * kTickMs;
    world->tick({}, now, now - kTickMs);
    auto& fight = world->fight;
    auto* actor = world->actor;
    fight.skill_level = [](int) { return 1; };
    now += 100 * kTickMs;
    using MonType = decltype(data->monsters.types)::value_type;
    const auto zombie = std::ranges::find(data->monsters.types, std::string_view("zombie1"), &MonType::id);
    assert(zombie != data->monsters.types.end());
    Monster foe;
    foe.type = int(zombie - data->monsters.types.begin());
    foe.id = 0x7ffff0;
    foe.hit_points = foe.stats.hit_points = 100000;
    foe.stats.armor_class = -1000;                                  // hit every time
    fight.monsters.clear();
    fight.monsters.push_back(std::move(foe));
    fight.monsters[0].unit.x = actor->player.x + 3.f;
    fight.monsters[0].unit.y = actor->player.y;
    actor->character.stats.values[d2d::d2s::kMana] = 100 << 8;
    const int flight = data->skills.by_name.at("Dragon Flight");
    const float start_x = actor->player.x;
    const Order order{ actor->id, cmd::UseSkill{ flight, fight.monsters[0].unit.x, fight.monsters[0].unit.y, fight.monsters[0].id, true, -1, false } };
    world->tick({ &order, 1 }, now, now - kTickMs);
    assert(actor->swing_skill == flight && !actor->seq.empty());
    int kicked_at = -1, flown_at = -1;
    for (int frame = 1; frame < 60 && (kicked_at < 0 || flown_at < 0); ++frame) {
        now += kTickMs;
        world->tick({}, now, now - kTickMs);
        if (kicked_at < 0 && fight.monsters[0].hit_points < 100000) kicked_at = frame;
        if (flown_at < 0 && actor->player.x - start_x > 1.f) flown_at = frame;
    }
    std::printf("Dragon Flight: kick at frame %d, flight at frame %d (speed %d)\n", kicked_at, flown_at, actor->seq_speed);
    assert(kicked_at > 0 && flown_at > kicked_at);
    assert(d2d::rules::seq_step_row(9, actor->seq_speed) % 2 == 1);

    // The kick on row 9 kills it: the flight on row 14 still goes to the corpse, a unit until it is freed
    // (FUN_00553490 drops the path's target only then; FUN_0056d2c0 gives its spot).
    auto doomed = fight.monsters[0];
    doomed.id = 0x7ffff1;
    doomed.hit_points = 1;
    doomed.unit.x = actor->player.x - 3.f; doomed.unit.y = actor->player.y;
    const auto killed = fight.monsters.size();                     // past the level's own
    fight.monsters.push_back(doomed);
    now += 100 * kTickMs;
    actor->character.stats.values[d2d::d2s::kMana] = 100 << 8;
    const float back_x = actor->player.x;
    const Order again{ actor->id, cmd::UseSkill{ flight, fight.monsters[killed].unit.x, fight.monsters[killed].unit.y, fight.monsters[killed].id, true, -1, false } };
    world->tick({ &again, 1 }, now, now - kTickMs);
    assert(actor->swing_skill == flight && !actor->seq.empty());
    for (int frame = 1; frame < 60; ++frame) {
        now += kTickMs;
        world->tick({}, now, now - kTickMs);
    }
    std::printf("Dragon Flight at one it kills: %s, moved %.1f\n", fight.monsters[killed].alive() ? "alive" : "dead", double(back_x - actor->player.x));
    assert(!fight.monsters[killed].alive() && back_x - actor->player.x > 1.f);

    // A Shadow Master's (Sk5mode seq_swdragonflight, events on rows 9 and 14 too, FUN_00663310's monster
    // branch): the same do on each event, at its SQ rate, the kick first, then the flight, on two frames.
    fight.monsters[0].hit_points = 100000;
    fight.summon(*data->skills.get(data->skills.by_name.at("Shadow Master")), 1, actor->player.x, actor->player.y, now);
    assert(!actor->pets.empty() && actor->pets.back().mirror);
    auto& shadow = actor->pets.back();
    Crowd crowd;
    // Through its own think: FUN_005eb970 asks for a skill (FUN_005dead0), not a sequence; the skill's
    // start reads the slot's sequence all the same.
    std::erase_if(shadow.skill_list, [&](const d2d::rules::ShadowListed& listed) { return listed.id != flight; });
    assert(!shadow.skill_list.empty());
    shadow.monster.unit.x = fight.monsters[0].unit.x - 1.f; shadow.monster.unit.y = fight.monsters[0].unit.y;
    actor->attack_mon = 0;
    bool sequenced = false;
    for (int frame = 0; frame < 500 && !sequenced; ++frame) {
        now += kTickMs;
        fight.crowd(crowd);
        fight.pets_turn(now, float(kTickMs) / 1000.f, crowd);
        sequenced = actor->pets.back().seq != nullptr;
    }
    std::printf("the Shadow Master's think: %s\n", sequenced ? "Dragon Flight's sequence" : "no sequence");
    assert(sequenced);
    now += 100 * kTickMs;
    fight.monsters[0].hit_points = 100000;
    shadow.monster.unit.x = actor->player.x; shadow.monster.unit.y = actor->player.y + 6.f;
    shadow.target = 0;
    shadow.cast = flight;
    fight.shadow_sequence(shadow, now);
    assert(shadow.seq && shadow.seq->size() == 23 && (*shadow.seq)[9].event == 1 && (*shadow.seq)[14].event == 1);
    const float shadow_y = shadow.monster.unit.y;
    kicked_at = flown_at = -1;
    for (int frame = 1; frame < 60 && (kicked_at < 0 || flown_at < 0); ++frame) {
        now += kTickMs;
        fight.crowd(crowd);
        fight.pets_turn(now, float(kTickMs) / 1000.f, crowd);
        if (kicked_at < 0 && fight.monsters[0].hit_points < 100000) kicked_at = frame;
        if (flown_at < 0 && shadow_y - actor->pets.back().monster.unit.y > 3.f) flown_at = frame;
    }
    std::printf("the Shadow's Dragon Flight: kick at frame %d, flight at frame %d (rate %d)\n", kicked_at, flown_at, shadow.seq_rate);
    assert(kicked_at > 0 && flown_at > kicked_at);
    assert(d2d::rules::seq_step_row(9, shadow.seq_rate) % 2 == 1);

    // The Shadow's, the same: killed by the kick, flown to all the same.
    doomed.id = 0x7ffff2;
    doomed.unit.x = shadow.monster.unit.x; doomed.unit.y = shadow.monster.unit.y + 6.f;
    const auto killed_too = fight.monsters.size();
    fight.monsters.push_back(std::move(doomed));
    now += 100 * kTickMs;
    const float shadow_back = shadow.monster.unit.y;
    shadow.target = int(killed_too);
    shadow.cast = flight;
    fight.shadow_sequence(shadow, now);
    assert(shadow.seq);
    // To its end: its think may start another flight at once (at the zombie, by its unit seed's draws), so the
    // move is the one this sequence made, read as it ends (its row back past the last).
    float moved = 0;
    for (int frame = 1, last_row = 0; frame < 60 && shadow.seq && shadow.seq_done >= last_row; ++frame) {
        last_row = shadow.seq_done;
        moved = shadow.monster.unit.y - shadow_back;
        now += kTickMs;
        fight.crowd(crowd);
        fight.pets_turn(now, float(kTickMs) / 1000.f, crowd);
        if (shadow.seq_done >= last_row) moved = shadow.monster.unit.y - shadow_back;
    }
    std::printf("the Shadow's at one it kills: %s, moved %.1f\n", fight.monsters[killed_too].alive() ? "alive" : "dead", double(moved));
    assert(!fight.monsters[killed_too].alive() && moved > 3.f);
    std::printf("test_dragon_flight_events: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_dragon_flight_events: %s\n", error.what());
    return 1;
}
