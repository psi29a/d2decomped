// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A knockback's pace: FUN_00623f50 gives a KB unit (a player's mode 0x13, a
// monster's 0xd) path velocity 0x1000, one subtile a frame (FUN_00554ca0 ->
// FUN_00650840), whatever its speed. A monster slides there and stands till
// KB ends (LAB_005a8630); a player's KB is the walk's frame (FUN_00580c20)
// and ends with the path (FUN_0057f020). Through the REAL Fight in the Blood
// Moor (needs the game data); `--knock-pace CASES` is the d2d side of
// tools/emu/knock_pace.py.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

using namespace d2d::game;

namespace {

bool close_to(float lhs, float rhs) { return std::fabs(lhs - rhs) < 1e-4f; }

// `num kind sx sy ex ey vp frw` (subtiles) -> `num frames step_x step_y`: the
// frames knock_ms gives and the first frame's step (1/65536 subtile).
int knock_cases(const char* path) {
    std::ifstream cases(path);
    long long num = 0;
    int kind = 0, start_x = 0, start_y = 0, end_x = 0, end_y = 0, speed = 0, faster = 0;
    while (cases >> num >> kind >> start_x >> start_y >> end_x >> end_y >> speed >> faster) {
        const float dx = float(end_x - start_x) / 5, dy = float(end_y - start_y) / 5, length = std::hypot(dx, dy);
        const float done = knock_moved(40, length) / length;
        std::printf("%lld %u %ld %ld\n", num, knock_ms(length) / 40, std::lround(dx * done * 5 * 65536), std::lround(dy * done * 5 * 65536));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc >= 3 && std::string_view(argv[1]) == "--knock-pace") return knock_cases(argv[2]);
    // The pace: a subtile (0.2 cells) a frame; 5 subtiles take 5 frames, a Sand Leaper's 10 take 10.
    assert(knock_ms(1.f) == 200 && knock_ms(2.f) == 400 && knock_ms(0.f) == 0);
    assert(knock_ms(0.7f) == 160);                                       // 3.5 subtiles: the 4th frame snaps on (FUN_00650090)
    assert(close_to(knock_moved(40, 1.f), 0.2f) && close_to(knock_moved(120, 1.f), 0.6f) && close_to(knock_moved(400, 1.f), 1.f));

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    constexpr int kBarbarian = 4;
    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, kBarbarian, "Headless", false, true, made_rng);
    Character character;
    character.character_class = kBarbarian;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.level = data->level(2);                                         // the Blood Moor
    assert(world.level);
    auto& fight = world.fight;
    auto& hero = *world.actor;
    // A way one cell clear of the hero: the knockback's direction.
    std::pair<float, float> away{};
    for (const auto& [step_x, step_y] : { std::pair{ 1.f, 0.f }, { -1.f, 0.f }, { 0.f, 1.f }, { 0.f, -1.f } })
        if (!world.level->unit_blocked(hero.player.x + step_x, hero.player.y + step_y, hero.player.shape)
            && !world.level->unit_blocked(hero.player.x + 2 * step_x, hero.player.y + 2 * step_y, hero.player.shape)) { away = { step_x, step_y }; break; }
    assert(away.first != 0 || away.second != 0);

    // A monster with MonStats2 mGH and mKB (LAB_005a77d0's FUN_0046c140 bits 3 / 13) goes into
    // KB, drawn and timed by its GH COF (MonMode.txt's token; no monster has a KB one). It
    // slides a subtile a frame till the path's end or the mode's (LAB_005a8630 steps, then
    // asks FUN_006217c0), so a KB of n frames moves min(n, 5) subtiles. One without mKB doesn't.
    d2d::rules::Rng rng{ 3 };
    const auto knock = [&](std::string_view type_id) -> Monster& {
        const auto type = data->monsters.by_id.find(std::string(type_id));
        assert(type != data->monsters.by_id.end());
        Monster made = make_monster(*data, type->second, 10, 10, rng, 0);
        made.unit.x = hero.player.x + away.first * 0.6f; made.unit.y = hero.player.y + away.second * 0.6f;
        fight.monsters.clear();
        fight.add_monster(std::move(made));
        fight.knock_back(fight.monsters.back(), hero.player.x, hero.player.y, 1000);
        return fight.monsters.back();
    };
    for (const std::string_view type_id : { "zombie1", "fallen1", "skeleton1" }) {
        auto& monster = knock(type_id);
        assert(data->monsters.types[std::size_t(monster.type)].can_knock_back && monster.mode == "KB");
        const float from_x = hero.player.x + away.first * 0.6f, from_y = hero.player.y + away.second * 0.6f;
        const std::uint32_t length = monster.mode_until - 1000;
        std::fprintf(stderr, "%s KB: %u ms\n", std::string(type_id).c_str(), length);
        assert(length > 0);
        for (std::uint32_t frame = 1; frame * 40 < length; ++frame) {
            assert(knock_slide(monster, 1000 + frame * 40));
            assert(close_to(std::hypot(monster.unit.x - from_x, monster.unit.y - from_y), std::min(0.2f * float(frame), 1.f)));
        }
        assert(!knock_slide(monster, monster.mode_until));                // KB over where the slide got to
        assert(close_to(std::hypot(monster.unit.x - from_x, monster.unit.y - from_y), std::min(0.2f * float(length) / 40, 1.f)));
    }
    const auto unknocked = std::ranges::find_if(data->monsters.types, [](const auto& type) { return type.can_get_hit && !type.can_knock_back; });
    assert(unknocked != data->monsters.types.end());
    assert(knock(unknocked->id).mode != "KB");
    fight.monsters.clear();

    // The player: KB (drawn as GH) lasts the slide, 5 frames, whatever FHR's get-hit.
    const std::uint32_t hit_ms = 5000;
    hero.pmode = -1;
    auto knocked = std::array{ fight.player_foe(hit_ms), fight.merc_foe(hit_ms) };
    knocked[0].hits = 1;
    knocked[0].damage = knocked[0].physical = 1;
    knocked[0].knocked_from = std::pair{ hero.player.x - away.first, hero.player.y - away.second };
    const float start_x = hero.player.x, start_y = hero.player.y;
    fight.hurt_side(knocked, hit_ms);
    assert(hero.pmode == kModeGH && hero.knock);
    std::printf("player KB: %u ms\n", hero.pmode_until - hit_ms);
    assert(hero.pmode_until - hit_ms == 200);
    for (int frame = 1; frame <= 5; ++frame) {
        auto quiet = std::array{ fight.player_foe(hit_ms + std::uint32_t(frame) * 40), fight.merc_foe(hit_ms + std::uint32_t(frame) * 40) };
        fight.hurt_side(quiet, hit_ms + std::uint32_t(frame) * 40);
        const float moved = std::hypot(hero.player.x - start_x, hero.player.y - start_y);
        assert(close_to(moved, 0.2f * float(frame)));
    }
    assert(!hero.knock);                                                 // the slide over, the knock with it
    std::puts("OK: a knockback slides a subtile a frame; a player's lasts the slide");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_knockback: %s\n", error.what());
    return 1;
}
