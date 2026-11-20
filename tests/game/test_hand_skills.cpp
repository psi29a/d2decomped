// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The hand-skill pass after a change of gear (FUN_0055c5c0, World::hand_skills):
// a throwing potion (ItemTypes tpot: thro, not mele) in the wielded hand puts
// the left skill on Throw (skill 2) and keeps the old one; in the other hand,
// the right skill. A hand skill then refused (FUN_00647960 status 2 or 7)
// takes the kept one back. No wielded weapon: nothing changes. Needs the game
// data.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::printf("FAIL: load_game_data failed\n"); return 1; }
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng(7);
    constexpr int kAmazon = 0;
    auto made = new_character(*data, kAmazon, "Headless", false, true, rng);
    Character character;
    character.character_class = kAmazon;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto& actor = *world.actor;
    constexpr int kAttack = 0, kThrow = 2, kInnerSight = 8;
    world.fight.skill_level = [&](int id) { return id == kAttack || id == kThrow || id == kInnerSight ? 1 : 0; };
    auto& items = actor.character.items;
    auto& header = actor.character.header;
    const auto hold = [&](const std::string& code, int slot, bool wielded) {
        d2d::d2s::Item item;
        item.code = code;
        item.id = std::uint32_t(100 + items.size());
        item.location = d2d::d2s::item_location::kEquipped;
        item.slot = slot;
        item.wielded = wielded;
        item.quantity = 5;
        items.push_back(std::move(item));
    };
    constexpr std::uint32_t now = 1000;

    // No wielded weapon: the pass stops at once (0x55c5ec).
    items.clear();
    header.left_skill = kAttack;
    header.right_skill = kInnerSight;
    world.hand_skills(now);
    assert(header.left_skill == std::uint32_t(kAttack) && header.right_skill == std::uint32_t(kInnerSight));

    // A throwing potion wielded: the left skill goes to Throw, Attack kept.
    hold("gpl", d2d::d2s::body_location::kRightArm, true);
    world.hand_skills(now);
    assert(header.left_skill == std::uint32_t(kThrow) && actor.kept_left == std::uint32_t(kAttack));
    // Already a throw (itypea1 thro, range rng, usable): it stays.
    world.hand_skills(now);
    assert(header.left_skill == std::uint32_t(kThrow) && actor.kept_left == std::uint32_t(kAttack));

    // A sword instead: Throw is refused (status 2), Attack comes back.
    items.clear();
    hold("ssd", d2d::d2s::body_location::kRightArm, true);
    world.hand_skills(now);
    assert(header.left_skill == std::uint32_t(kAttack));

    // The potion in the other hand: the right skill goes to Throw, Inner Sight kept;
    // Throw's gate reads the wielded sword (FUN_00647640, throw_ready), so it's
    // refused at once and Inner Sight comes back.
    hold("gpl", d2d::d2s::body_location::kLeftArm, false);
    world.hand_skills(now);
    assert(actor.kept_right == std::uint32_t(kInnerSight) && header.right_skill == std::uint32_t(kInnerSight));
    assert(header.left_skill == std::uint32_t(kAttack));   // the sword's hand: Attack isn't refused
    std::puts("OK: FUN_0055c5c0's hands: a throwing potion takes Throw, a refused skill the kept one");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
