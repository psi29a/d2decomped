// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The skills' weapon-type gate (FUN_00643f80 / FUN_00643d90, FUN_00647960's status 2) with the real Skills.txt and ItemTypes:
// one hand fits set a (itypea / etypea) and the other set b (itypeb / etypeb), either way round. Holy Shield (itypea1 shld,
// no set b) needs a shield in either hand; Double Swing (mele / mele) two melee weapons, or bare hands; Dragon Claw (h2h / h2h)
// two claws; a skill with no set (Fire Bolt) takes anything.

#include <character.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>

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
    const auto gate = [&](const char* skill, std::initializer_list<std::pair<const char*, int>> worn) {
        Character character;
        for (const auto& [code, slot] : worn) {
            d2d::d2s::Item item;
            item.code = code; item.location = d2d::d2s::item_location::kEquipped; item.slot = slot;
            character.items.push_back(item);
        }
        return weapon_gate(*data, character, *data->skills.get(data->skills.by_name.at(skill)));
    };
    constexpr int kRight = d2d::d2s::body_location::kRightArm, kLeft = d2d::d2s::body_location::kLeftArm;
    // Holy Shield: a shield in either hand.
    assert(gate("Holy Shield", { { "ssd", kRight }, { "buc", kLeft } }) && gate("Holy Shield", { { "buc", kRight } }));
    assert(!gate("Holy Shield", { { "ssd", kRight } }) && !gate("Holy Shield", { { "ssd", kRight }, { "ssd", kLeft } }) && !gate("Holy Shield", {}));
    // Double Swing: two melee weapons; one with an empty hand fails (D_b(none) needs neither hand a weapon); bare hands pass.
    assert(gate("Double Swing", { { "ssd", kRight }, { "hax", kLeft } }) && !gate("Double Swing", { { "ssd", kRight } }) && gate("Double Swing", {}));
    // Dragon Claw: two claws.
    assert(gate("Dragon Claw", { { "ktr", kRight }, { "ktr", kLeft } }) && !gate("Dragon Claw", { { "ktr", kRight }, { "buc", kLeft } }));
    // Smite: a shield too; Fire Bolt: no set.
    assert(gate("Smite", { { "buc", kLeft } }) && !gate("Smite", { { "ssd", kRight } }) && gate("Fire Bolt", {}));
    std::puts("OK: the weapon-type gate as FUN_00643f80 / FUN_00643d90");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_weapon_gate: %s\n", error.what());
    return 1;
}
