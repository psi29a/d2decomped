// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_00485410's first branch: a throwing potion (tpot) shows one damage
// line, "Throw Damage:" white and its thrown missile's level 1 damage in
// EType's colour, and no one-hand / two-hand line. The numbers are
// game.exe's own (tools/emu/throw_potion_text.py on the same tables). Needs
// the game data.

#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <item_text.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

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
    const std::string throw_label = string_id(*data, 3467) + " ", to_text = " " + string_id(*data, 3464) + " ";
    const auto damage_line = [&](const char* code) -> TextLine {   // a copy: `lines` ends with the call
        d2d::d2s::Item potion;
        potion.code = code;
        potion.identified = true; potion.quality = 2; potion.defense = -1; potion.quantity = 5;
        const auto lines = item_lines(*data, potion, 1);
        assert(std::ranges::none_of(lines, [&](const TextLine& line) {
            return line.text.starts_with(string_id(*data, 3465)) || line.text.starts_with(string_id(*data, 3466));
        }));
        const auto found = std::ranges::find_if(lines, [&](const TextLine& line) { return line.text.starts_with(throw_label); });
        assert(found != lines.end() && found->rgb == kTxtWhite && found->split == throw_label.size());
        return *found;
    };
    const auto gas = damage_line("gpl");                // Strangling Gas Potion: poison over 2 seconds
    assert(gas.text == throw_label + "24" && gas.tail == kTxtGreen);
    const auto rancid = damage_line("gps");
    assert(rancid.text == throw_label + "96" && rancid.tail == kTxtGreen);
    const auto oil = damage_line("opl");                // Fulminating Potion: fire, element + physical
    assert(oil.text == throw_label + "5" + to_text + "15" && oil.tail == kTxtRed);
    const auto exploding = damage_line("opm");
    assert(exploding.text == throw_label + "16" + to_text + "30" && exploding.tail == kTxtRed);
    std::printf("throw potion text: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_throw_potion_text: %s\n", error.what());
    return 1;
}
