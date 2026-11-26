// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Whirlwind's hands. rules::two_melee (FUN_005d92d0) and rules::weapsel2_pick (FUN_00535bc0's weapsel 2, its
// default FUN_0063c9b0, the cells by component FUN_0063c050, the usable test FUN_0062a4e0) against game.exe's own
// run of them (tools/emu/whirl_hands.py inc writes the cases); always runs. With the game data (D2_MPQ_DIR):
// worn_items reads each base as game.exe does, and Fight::move_event takes its gap from the weapon game.exe
// picks: a broken right hand's isn't it, the usable left one is; with both broken, none (10 frames).

#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <hand_pick.hpp>
#include <item_text.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <wear.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// One base as game.exe reads it.
struct WhirlBase { std::string_view code; int melee, weapon, class_key, component; };
// One worn row: cell, class key, component, melee, weapon, usable, wielded.
using WhirlRow = std::array<int, 7>;
// One case: the class, the flag 0x2000, the rows, game.exe's FUN_005d92d0 and FUN_00535bc0's cell (-1 none).
struct WhirlCase { int player_class, left_flag, count; std::array<WhirlRow, 3> rows; int dual, picked; };

#include "../whirl_hands_cases.inc"

// The fixture's cases through two_melee and weapsel2_pick; the count that differ.
int check_cases() {
    int differ = 0;
    for (const auto& sample : kWhirlCases) {
        std::vector<d2d::rules::WornItem> worn;
        for (const auto& row : std::span(sample.rows).first(std::size_t(sample.count)))
            worn.push_back({ .cell = row[0], .class_key = row[1], .component = row[2], .melee = row[3] != 0, .weapon = row[4] != 0,
                             .usable = row[5] != 0, .wielded = row[6] != 0 });
        const bool dual = d2d::rules::two_melee(worn);
        const auto* pick = d2d::rules::weapsel2_pick(worn, sample.player_class == 4 || sample.player_class == 6, sample.left_flag != 0);
        const int cell = pick ? pick->cell : -1;
        if ((int(dual) != sample.dual || cell != sample.picked) && ++differ <= 10)
            std::fprintf(stderr, "case class %d flag %d: dual %d / %d, pick %d / %d\n", sample.player_class, sample.left_flag, int(dual), sample.dual, cell, sample.picked);
    }
    return differ;
}

}  // namespace

int main() try {
    const int differ = check_cases();
    std::printf("%zu FUN_005d92d0 / FUN_00535bc0 cases, %d differ\n", std::size(kWhirlCases), differ);
    assert(differ == 0);

    using namespace d2d::game;
    const char* data_dir = std::getenv("D2_MPQ_DIR");
    if (!data_dir) { std::puts("SKIP: the bases and the gap (set D2_MPQ_DIR)"); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? std::filesystem::path(patch) : std::filesystem::path{}, 3397892852u);
    assert(data);
    if (!data->patched) { std::puts("SKIP: the bases and the gap, 1.14d's tables (set D2_PATCH_INSTALLER)"); return 0; }
    for (const auto& base : kWhirlBases) {
        d2d::d2s::Item item;
        item.code = std::string(base.code);
        item.location = d2d::d2s::item_location::kEquipped;
        item.slot = d2d::d2s::body_location::kHead;
        const auto worn = d2d::rules::worn_items(data->rules, { item });
        assert(worn.size() == 1);
        if (int(worn[0].melee) != base.melee || int(worn[0].weapon) != base.weapon || worn[0].class_key != base.class_key || worn[0].component != base.component) {
            std::fprintf(stderr, "FAIL: %s reads %d %d %d %d\n", item.code.c_str(), int(worn[0].melee), int(worn[0].weapon), worn[0].class_key, worn[0].component);
            return 1;
        }
    }
    std::printf("OK: %zu bases read as game.exe reads them\n", std::size(kWhirlBases));

    d2d::rules::Rng rng(7);
    auto made = new_character(*data, 4, "Headless", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kBarbarian;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    World world(&*data, -1, -1);
    world.enter(character);
    const auto* whirlwind = data->skills.get(data->skills.by_name.at("Whirlwind"));
    assert(whirlwind);
    auto& actor = *world.actor;
    auto& items = actor.character.items;
    const auto in_hand = [&](int slot) { return std::ranges::find_if(items, [&](const d2d::d2s::Item& item) {
        return item.location == d2d::d2s::item_location::kEquipped && item.slot == slot; }); };
    std::erase_if(items, [&](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == d2d::d2s::body_location::kLeftArm; });
    auto right = in_hand(d2d::d2s::body_location::kRightArm);
    assert(right != items.end() && d2d::rules::weapon_item(data->rules, *right));
    auto left = *right;
    left.slot = d2d::d2s::body_location::kLeftArm;
    left.wielded = false;
    left.props.push_back({ d2d::d2s::kFasterAttackRate, 0, 60 });
    items.push_back(left);
    right = in_hand(d2d::d2s::body_location::kRightArm);
    const int plain = d2d::rules::whirlwind_gap(weapon_attack_frames(*data, *right, 4));
    const int quick = d2d::rules::whirlwind_gap(weapon_attack_frames(*data, items.back(), 4));
    assert(quick < plain);
    // A broken right hand's axe (flag 0x100) can't be the wielded one (FUN_0063d1d0): the left's is, so it is
    // component 5 and the pick under flag clear; the broken one, component 6, fails FUN_0062a4e0 under the flag.
    right->flags |= 0x100u;
    right->wielded = false;
    items.back().wielded = true;
    const auto gap_after = [&](std::uint32_t when) {
        world.fight.start_move(*whirlwind, when);
        world.fight.move_event(*whirlwind, when);                   // the first pass: 4 frames, flips to the left flag
        world.fight.move_event(*whirlwind, actor.whirl_next);       // flag set: component 6's, unusable -> FUN_0063c9b0's
        const int first = int(actor.whirl_next - (when + 4 * 40)) / 40;
        const std::uint32_t next = actor.whirl_next;
        world.fight.move_event(*whirlwind, next);                   // flag clear: component 5's
        return std::array<int, 2>{ first, int(actor.whirl_next - next) / 40 };
    };
    assert(world.fight.monsters.empty());
    const auto broken_right = gap_after(5000);
    std::printf("broken right hand: gaps %d, %d (left %d, right %d)\n", broken_right[0], broken_right[1], quick, plain);
    assert(broken_right[0] == quick && broken_right[1] == quick);
    // Both broken: no weapon is wielded or usable, so FUN_00535bc0 has none and the gap is 10.
    in_hand(d2d::d2s::body_location::kLeftArm)->flags |= 0x100u;
    in_hand(d2d::d2s::body_location::kLeftArm)->wielded = false;
    const auto both_broken = gap_after(9000);
    std::printf("both broken: gaps %d, %d\n", both_broken[0], both_broken[1]);
    assert(both_broken[0] == 10 && both_broken[1] == 10);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
