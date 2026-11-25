// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A do-2 skill's blow comes from the record its start built (FUN_005d7ea0 ->
// FUN_0057dbf0 -> FUN_0057ca80; its do FUN_0056f1f0 finds it with FUN_0057d690
// and FUN_0057d4f0 drops it): through the REAL Fight, a Barbarian's
// Concentrate at a Fallen in the Blood Moor. The start keeps one record against
// the monster; the action frame lands it and drops it; a landed one plays the
// skill's srvoverlay (doubledamage1) on the monster, a missed one doesn't. A
// monster out of reach at the start gets a miss with no roll; with the record
// gone before the action frame there is no blow. Needs the game data.

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <hit_records.hpp>
#include <overlays.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, d2d::d2s::kBarbarian, "Headless", false, true, made_rng);
    Character character;
    character.character_class = d2d::d2s::kBarbarian;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.level = data->level(2);                                 // the Blood Moor: out of town
    assert(world.level);
    auto& fight = world.fight;
    auto& actor = *world.actor;
    const int concentrate = data->skills.by_name.at("Concentrate");
    const auto* skill = data->skills.get(concentrate);
    assert(skill && skill->srvdofunc == ServerDoFunction::kResolveHit && skill->srvoverlay == "doubledamage1");
    const int overlay = data->overlay_row(skill->srvoverlay);
    assert(overlay > 0);
    fight.skill_level = [](int) { return 5; };
    const auto fallen = data->monsters.by_id.find("fallen1");
    assert(fallen != data->monsters.by_id.end());
    d2d::rules::Rng rng{ 3 };
    const auto place = [&](float offset) {
        fight.monsters.clear();
        Monster made = make_monster(*data, fallen->second, 10, 10, rng, 0);
        made.stats.hit_points = 50000; made.hit_points = 50000;
        made.unit.x = actor.player.x + offset; made.unit.y = actor.player.y;
        fight.add_monster(std::move(made));
    };
    const auto start = [&](std::uint32_t now) {
        actor.character.stats.values[d2d::d2s::kMana] = 100 << 8;
        actor.attack_skill = concentrate; actor.attack_mon = 0; actor.pmode = -1;
        assert(fight.start_swing(now));
        assert(actor.swing_skill == concentrate);
    };
    const auto play_out = [&](std::uint32_t now) {
        for (int tick = 0; tick < 400 && actor.pmode >= 0 && !actor.pstruck; ++tick) { now += 40; fight.player_modes(true, now, 0.04f); }
        assert(actor.pstruck);
    };
    const auto flashed = [&] { return std::ranges::any_of(fight.monsters[0].one_shots, [&](const d2d::rules::OneShot& shot) { return shot.row == overlay; }); };
    const d2d::rules::UnitKey player{ 0, actor.id };

    // In reach: one record, against the monster; the action frame lands it and drops it.
    int landed = 0, missed = 0;
    std::uint32_t now = 1000;
    for (int swing = 0; swing < 60 && (landed < 3 || missed < 3); ++swing) {
        place(0.6f);
        start(now);
        assert(actor.hit_records.nodes.size() == 1);
        const d2d::rules::UnitKey struck{ 1, fight.monsters[0].id };
        const auto* record = actor.hit_records.find(player, struck);
        assert(record);
        const bool hit = record->blow.hit;
        play_out(now);
        assert(actor.hit_records.nodes.empty());
        assert(flashed() == hit);
        assert((fight.monsters[0].hit_points < 50000) == hit);
        ++(hit ? landed : missed);
        now += 10000;
    }
    std::printf("in reach: %d landed (overlay on), %d missed (none)\n", landed, missed);
    assert(landed >= 3 && missed >= 3);

    // Out of reach at the start: a miss, built with no roll; it lands as a miss.
    place(8.f);
    const auto low = fight.rng.low, high = fight.rng.high;
    start(now);
    assert(fight.rng.low == low && fight.rng.high == high);
    const auto* beyond = actor.hit_records.find(player, { 1, fight.monsters[0].id });
    assert(beyond && !beyond->blow.hit && !beyond->blow.blocked && beyond->blow.damage == 0);
    actor.hit_records.nodes.clear();
    now += 10000;

    // The record gone before the action frame: no blow.
    for (int swing = 0; swing < 10; ++swing) {
        place(0.6f);
        start(now);
        actor.hit_records.nodes.clear();
        play_out(now);
        assert(fight.monsters[0].hit_points == 50000 && !flashed());
        now += 10000;
    }
    std::puts("OK: a do-2 skill lands the blow its start built");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_hit_records_swing: %s\n", error.what());
    return 1;
}
