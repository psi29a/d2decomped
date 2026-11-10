// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The hire window's rows (FUN_004b5c60, rules/hire_list.hpp): the numbers
// as "%2u" widened to 9 characters at most (FUN_00526f20), the labels cut
// at 20 (FUN_00526790), the description by HireDesc (FUN_00663720's table
// 0x6ee410) cut at a '[' (FUN_0048be00). Then, with the game's MPQs
// (D2_MPQ_DIR), each hireling's HireDesc as its hiredesc.txt row.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <hire_list.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;

int main() try {
    using d2d::rules::hire_number;
    assert(hire_number(0) == " 0" && hire_number(9) == " 9" && hire_number(10) == "10" && hire_number(2475) == "2475");
    assert(hire_number(123456789) == "123456789");                   // 9 fit
    assert(hire_number(1234567890) == "12345678");                   // 10: the 9th place cut, 8 left
    assert(d2d::rules::hire_label("Lvl") == "Lvl" && d2d::rules::hire_label("abcdefghijklmnopqrstuvwxyz") == "abcdefghijklmnopqrst");
    static_assert(d2d::rules::hire_desc_string(0) == 0 && d2d::rules::hire_desc_string(1) == 0xd2b && d2d::rules::hire_desc_string(8) == 0xd2f);
    static_assert(d2d::rules::hire_desc_string(9) == 0 && d2d::rules::hire_desc_string(-1) == 0);
    assert(d2d::rules::hire_description("Fire Arrow") == "Fire Arrow" && d2d::rules::hire_description("Feuer[fs]Pfeil") == "Feuer");
    std::printf("OK: the rows' numbers, labels and descriptions\n");

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && !data->mercs.empty());
    if (!data->patched) { std::printf("SKIP: these rows are 1.14d's hireling.txt (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    // hireling.txt's HireDesc codes in hiredesc.txt's order: farw 1, carw 2,
    // comb 3, def 4, off 5, fire 6, cold 7, ltng 8; a Barbarian's none, 0.
    auto desc = [&](int id) { return data->mercs.at(id).hire_desc; };
    assert(desc(0) == 1 && desc(1) == 2);                            // Rogue Scout: Fire, Ice (Normal)
    assert(desc(6) == 3 && desc(7) == 4 && desc(8) == 5);            // Desert Mercenary: Comb, Def, Off
    assert(desc(15) == 6 && desc(16) == 7 && desc(17) == 8);         // Eastern Sorceror: Fire, Cold, Ltng
    assert(desc(24) == 0);                                           // Barbarian
    for (const auto& [id, merc] : data->mercs) assert(merc.hire_desc >= 0 && merc.hire_desc <= 8);
    std::printf("OK: %zu hirelings' HireDesc rows\n", data->mercs.size());
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hire_list: %s\n", error.what());
    return 1;
}
