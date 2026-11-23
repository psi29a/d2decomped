// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A skill start's cast overlay (FUN_004c6140 reads castoverlay +0x106, ItemCastOverlay +0x124
// from an item): played once on its caster (FUN_00470390 kind 2) unless that row is already
// playing on it (FUN_0046e100). Through the REAL Fight: a Sorceress's Fire Bolt shows
// fire_cast_1 as it starts, a second start while it plays doesn't restart it, one after it
// ended does; a monster's Resurrect shows its own. The unit's one-shots are a list (+0x2c,
// rules::add_one_shot): another row plays beside it, its own row restarts in place.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <overlays.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>

using namespace d2d::game;

namespace {

void check_rules() {
    using d2d::rules::cast_overlay_row;
    using d2d::rules::cast_overlay_starts;
    assert(cast_overlay_row(5, 9, false) == 5);       // castoverlay
    assert(cast_overlay_row(5, 9, true) == 9);        // ItemCastOverlay, from an item
    assert(cast_overlay_row(5, -1, true) == 5);       // none set: castoverlay still
    assert(cast_overlay_row(0, -1, false) == -1);     // row 0 never plays (0 < row)
    assert(cast_overlay_row(-1, -1, true) == -1);
    using d2d::rules::OneShots;
    assert(cast_overlay_starts(5, {}, 0, 0, 0));                                  // nothing on it
    assert(!cast_overlay_starts(5, OneShots{ { 5, 0 } }, 13 * 40, 16, 14));       // its 14th frame still to draw
    assert(cast_overlay_starts(5, OneShots{ { 5, 0 } }, 14 * 40, 16, 14));        // done
    assert(cast_overlay_starts(5, OneShots{ { 6, 0 } }, 0, 16, 14));              // another row playing
    assert(!cast_overlay_starts(5, OneShots{ { 6, 0 }, { 5, 40 } }, 80, 16, 14)); // its row among others
    assert(!cast_overlay_starts(-1, {}, 0, 0, 0));
    OneShots shots;
    d2d::rules::add_one_shot(shots, 5, 100);
    d2d::rules::add_one_shot(shots, 0x2e, 200);                                   // a Cry Help flash beside the cast
    assert((shots == OneShots{ { 0x2e, 200 }, { 5, 100 } }));
    d2d::rules::add_one_shot(shots, 5, 300);                                      // its row off first, the new one at the head
    assert((shots == OneShots{ { 5, 300 }, { 0x2e, 200 } }));
    d2d::rules::add_one_shot(shots, 0x8c, 400);
    d2d::rules::add_one_shot(shots, 0x8c, 500);                                   // 0x8c..0x8e and 0x9e stack
    assert(shots.size() == 4 && shots[0].row == 0x8c && shots[1].row == 0x8c);
    d2d::rules::add_one_shot(shots, -1, 600);
    assert(shots.size() == 4);
}

}  // namespace

int main() try {
    check_rules();
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const std::filesystem::path data_dir = mpq_dir ? std::filesystem::path(mpq_dir) : std::filesystem::path(home ? home : "") / "Workspace/private/diablo2";
    if (!std::filesystem::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? std::filesystem::path(patch) : std::filesystem::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    const auto* fire_bolt = data->skills.get(36);
    assert(fire_bolt && fire_bolt->cast_overlay == "fire_cast_1");
    const int fire_cast = data->overlay_row("fire_cast_1");
    assert(fire_cast > 0 && data->overlay_rows[std::size_t(fire_cast)]);
    const auto& overlay = *data->overlay_rows[std::size_t(fire_cast)];
    assert(overlay.frames == 14 && overlay.rate == 16);               // 14 ticks long
    assert(data->overlay_row("") == -1 && data->overlay_row("no such overlay") == -1);

    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(*data, 1, "Headless", false, true, rng);
    character.character_class = 1;
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
    world->fight.skill_level = [](int) { return 1; };
    world->fight.skill_base = world->fight.skill_level;
    std::uint32_t now = 1000 * kTickMs;
    world->tick({}, now, now - kTickMs);
    auto* actor = world->actor;
    actor->character.stats.values[d2d::d2s::kMana] = actor->character.stats.values[d2d::d2s::kMaxMana] = std::int64_t(1000) << 8;

    const auto cast_at = [&](std::uint32_t when) {
        actor->pmode = -1; actor->swing_skill = 0; actor->attack_mon = -1; actor->pstruck = false;
        return world->fight.cast_missile(36, actor->player.x + 3, actor->player.y, when);
    };
    now += 100 * kTickMs;
    using d2d::rules::OneShot;
    assert(actor->one_shots.empty());
    assert(cast_at(now));
    assert(actor->one_shots.size() == 1 && actor->one_shots[0] == (OneShot{ fire_cast, now }));
    const std::uint32_t first = now;
    assert(cast_at(now + 13 * kTickMs));                                // its last frame still to come
    assert(actor->one_shots.size() == 1 && actor->one_shots[0] == (OneShot{ fire_cast, first }));
    assert(cast_at(now + 14 * kTickMs));                                // over: a new one in its place
    assert(actor->one_shots.size() == 1 && actor->one_shots[0].start == now + 14 * kTickMs);
    std::printf("OK: Fire Bolt's start plays fire_cast_1 once, not over itself\n");

    const auto* resurrect = data->skills.by_name.contains("Resurrect") ? data->skills.get(data->skills.by_name.at("Resurrect")) : nullptr;
    assert(resurrect && !resurrect->cast_overlay.empty());
    const int raise = data->overlay_row(resurrect->cast_overlay);
    assert(raise > 0);
    const int type = int(data->mon_bin[std::size_t(d2d::rules::monster_ids::kDuriel)]);
    auto monster = make_monster(*data, type, actor->player.x + 2, actor->player.y, rng, 0);
    monster.sounded = true;
    monster.last_mode = monster.mode;
    monster.skill_sound = resurrect->id;
    world->fight.monster_sounds(monster, now + 100 * kTickMs);
    assert(!monster.one_shots.empty() && monster.one_shots[0] == (OneShot{ raise, now + 100 * kTickMs }) && monster.skill_sound == -1);
    std::printf("OK: a monster's Resurrect start plays %s\n", resurrect->cast_overlay.c_str());

    // The merc's and a pet's starts (merc_turn, pet_think_turn) take the same helper on their own
    // one-shot: an Act 3 merc's Fire Ball, and none for a swing (no skill, -1).
    const auto* fire_ball = data->skills.get(47);
    assert(fire_ball && fire_ball->cast_overlay == "fire_cast_2");
    const int fire_cast_2 = data->overlay_row("fire_cast_2");
    world->actor->merc = Monster{};
    auto& merc = *world->actor->merc;
    world->fight.start_cast_overlay(*fire_ball, false, merc.one_shots, now + 200 * kTickMs);
    assert((merc.one_shots == d2d::rules::OneShots{ { fire_cast_2, now + 200 * kTickMs } }));
    assert(actor->one_shots.size() == 1 && actor->one_shots[0].row == fire_cast);   // the player's own is untouched
    const auto* attack = data->skills.get(0);
    assert(attack && attack->cast_overlay.empty());
    Monster pet;
    world->fight.start_cast_overlay(*attack, false, pet.one_shots, now + 200 * kTickMs);
    assert(pet.one_shots.empty());
    std::printf("OK: the merc's Fire Ball start plays fire_cast_2 on the merc\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_cast_overlay: %s\n", error.what());
    return 1;
}
