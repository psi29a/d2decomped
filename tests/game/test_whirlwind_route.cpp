// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Whirlwind's path (FUN_005d8f50): a type 7 path to the point with mask 0xc01
// (FUN_00648ce0: walls, no units), walked to its last point. On the camp's
// grid, a point the straight line can't reach through a wall gets a route
// round it, every point open.
// Its passes (FUN_005d9580 -> FUN_005d9320): the first strikes once and comes
// back 4 frames on; then the gap of the weapon weapsel 2 picks (the right, the
// left under the node's flag 0x2000): its own attack frames (FUN_0062a710:
// AnimData's A1 for its class, the item's IAS and WSM), banded 4..16; 10 frames
// with no weapon. The flag flips after each strike and on a pass that finds no
// unit, so with a weapon in each hand the empty passes alternate the gaps.
// Needs the game data (D2_MPQ_DIR); SKIPs without it.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <item_text.hpp>
#include <level.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <wear.hpp>
#include <world.hpp>

#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    const char* data_dir = std::getenv("D2_MPQ_DIR");
    if (!data_dir) return D2D_TEST_SKIPPED;
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? std::filesystem::path(patch) : std::filesystem::path{}, 3397892852u);
    assert(data);
    if (!data->patched) { std::puts("SKIP: 1.14d's camp (set D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng(7);
    auto made = new_character(*data, 4, "Headless", false, true, rng);
    Character character;
    character.character_class = 4;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    World world(&*data, -1, -1);
    world.enter(character);
    const auto* whirlwind = data->skills.get(data->skills.by_name.at("Whirlwind"));
    assert(whirlwind);
    auto& actor = *world.actor;
    const auto& level = *world.level;
    const auto open = [&](float x, float y) { auto shape = actor.player.shape; shape.mask = 0xc01; return !level.unit_blocked(x, y, shape); };
    // A free point 2..6 cells off whose straight line crosses a wall.
    std::optional<std::pair<float, float>> behind;
    const float from_x = actor.player.x, from_y = actor.player.y;
    for (int dy = -30; dy <= 30 && !behind; ++dy)
        for (int dx = -30; dx <= 30 && !behind; ++dx) {
            const float to_x = from_x + float(dx) / 5, to_y = from_y + float(dy) / 5, length = std::hypot(to_x - from_x, to_y - from_y);
            if (length < 2 || !open(to_x, to_y)) continue;
            bool walled = false;
            const auto centre = [](float cells) { return (std::floor(cells * 5) + 0.5f) / 5; };   // the route's subtile centres
            for (float part = 0; part <= 1 && !walled; part += 0.02f) walled = !open(centre(from_x + (to_x - from_x) * part), centre(from_y + (to_y - from_y) * part));
            if (walled) behind = std::pair{ to_x, to_y };
        }
    if (!behind) { std::puts("SKIP: no walled point near the start"); return D2D_TEST_SKIPPED; }
    actor.move_x = behind->first; actor.move_y = behind->second;
    world.fight.start_move(*whirlwind, 1000);
    const auto& route = actor.smove.route;
    assert(actor.smove.active && route.size() >= 2);
    for (const auto& [point_x, point_y] : route) assert(open(point_x, point_y));
    assert(actor.smove.target_x == route.back().first && actor.smove.target_y == route.back().second);
    std::printf("OK: Whirlwind goes round the wall in %zu points\n", route.size());

    // The hit gap: the weapon's A1 frames << 8 over its speed x (100 + the item's IAS - WSM) / 100.
    const auto weapon_at = [&]() -> d2d::d2s::Item* {
        const auto* wielded = d2d::rules::wielded_weapon(data->rules, actor.character.items);
        for (auto& item : actor.character.items) if (&item == wielded) return &item;
        return nullptr;
    };
    auto* weapon = weapon_at();
    assert(weapon);
    const auto gap_for = [&](int ias) {
        std::string wclass = data->item_desc.at(weapon->code).wclass;
        for (auto& letter : wclass) letter = char(std::toupper(static_cast<unsigned char>(letter)));
        const auto& anim = data->anim_data.at(std::string("BAA1") + wclass);
        const int rate = (100 + ias - data->rules.item_base.at(weapon->code).speed) * int(anim.speed) / 100;
        return d2d::rules::whirlwind_gap(int((std::int64_t(anim.frames) << 8) / rate));
    };
    world.fight.move_event(*whirlwind, 1500);                          // the first pass: 4 frames, whatever the weapon
    assert(actor.whirl_begun && actor.whirl_next == 1500 + 4 * 40);
    const auto gap_now = [&](std::uint32_t when) { actor.whirl_next = 0; world.fight.move_event(*whirlwind, when); return int(actor.whirl_next - when) / 40; };
    const int plain = gap_now(2000);
    assert(plain == gap_for(0) && plain == d2d::rules::whirlwind_gap(weapon_attack_frames(*data, *weapon, 4)));
    weapon->props.push_back({ d2d::d2s::kFasterAttackRate, 0, 60 });
    const int quick = gap_now(3000);
    assert(quick == gap_for(60) && quick < plain);
    weapon->props.pop_back();
    const std::vector<d2d::d2s::Item> worn = actor.character.items;   // a copy on purpose: put back below
    std::erase_if(actor.character.items, [&](const d2d::d2s::Item& item) { return &item == weapon; });
    assert(gap_now(4000) == 10);
    actor.character.items = worn;
    std::printf("OK: Whirlwind hits every %d frames, %d with +60%% IAS on the weapon, 10 bare-handed\n", plain, quick);

    // A weapon in each hand, the left's with +60% IAS: a new whirl's first pass (4 frames) flips to the left, and with
    // no unit about each later pass flips once, so the gaps go left, right, left (no monsters in the camp).
    assert(world.fight.monsters.empty());
    std::erase_if(actor.character.items, [](const d2d::d2s::Item& item) {
        return item.location == d2d::d2s::item_location::kEquipped && item.slot == d2d::d2s::body_location::kLeftArm; });
    auto second = *weapon_at();
    second.slot = d2d::d2s::body_location::kLeftArm;
    second.wielded = false;                                           // the inventory names one wielded weapon (+0x1c)
    second.props.push_back({ d2d::d2s::kFasterAttackRate, 0, 60 });
    actor.character.items.push_back(std::move(second));
    world.fight.start_move(*whirlwind, 5000);
    assert(!actor.whirl_begun && !actor.whirl_left);
    world.fight.move_event(*whirlwind, 5000);
    assert(actor.whirl_next == 5000 + 4 * 40 && actor.whirl_left);
    std::uint32_t when = actor.whirl_next;
    std::vector<int> gaps;
    for (int pass = 0; pass < 3; ++pass) {
        world.fight.move_event(*whirlwind, when);
        gaps.push_back(int(actor.whirl_next - when) / 40);
        when = actor.whirl_next;
    }
    std::printf("two weapons: gaps %d, %d, %d (left %d, right %d)\n", gaps[0], gaps[1], gaps[2], quick, plain);
    assert(gaps[0] == quick && gaps[1] == plain && gaps[2] == quick);

    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
