// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A normal item's fix-ups (FUN_00556f30 -> FUN_00556e80): a scroll's or a
// tome's Books.txt row (FUN_005c2540) goes into the save's 5 bits, which
// game.exe reads back as the book's spell (FUN_0055bff0). Hand-made tables
// always; with the 1.14d data, every Items row against
// tools/emu/books_row.py's cases (books_row_cases.inc).
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace d2d::rules;

namespace {
struct Case { const char* code; int tome; };
constexpr Case kCases[] = {
#include "books_row_cases.inc"
};
}  // namespace

int main() try {
    {   // FUN_005c2540: ScrollSpellCode (EDX 1) or BookSpellCode, the row count when neither is.
        Tables tables;
        tables.books = { { "tsc", "tbk" }, { "isc", "ibk" }, { "", "" } };
        assert(books_row(tables, "tsc", true) == 0 && books_row(tables, "tbk", false) == 0);
        assert(books_row(tables, "isc", true) == 1 && books_row(tables, "ibk", false) == 1);
        assert(books_row(tables, "tbk", true) == 3 && books_row(tables, "isc", false) == 3 && books_row(tables, "0sc", true) == 3);
        assert(books_row(Tables{}, "tbk", false) == 0);
        // generate_item: a normal scroll / tome gets it, with or without a unit seed; anything else keeps -1.
        for (const char* code : { "tsc", "isc", "0sc" }) { tables.item_info[code] = { .type = "scro" }; tables.item_base[code] = {}; }
        for (const char* code : { "tbk", "ibk" }) { tables.item_info[code] = { .type = "book" }; tables.item_base[code] = { .stackable = true, .max_stack = 20 }; }
        tables.item_info["key"] = { .type = "key" };
        tables.item_base["key"] = {};
        Rng rng(7), unit_seed(9);
        assert(generate_item(tables, "ibk", 1, 2, rng).tome == 1 && generate_item(tables, "tbk", 1, 2, rng).tome == 0);
        assert(generate_item(tables, "isc", 5, 2, rng, &unit_seed).tome == 1 && generate_item(tables, "0sc", 5, 2, rng, &unit_seed).tome == 3);
        assert(generate_item(tables, "key", 5, 2, rng, &unit_seed).tome == -1);
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: the real tables (no d2data.mpq in %s)\n", data_dir.string().c_str()); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = d2d::game::load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::printf("FAIL: load_game_data failed\n"); return 1; }
    if (!data->patched) { std::printf("SKIP: the real tables need the 1.14d data (D2_PATCH_INSTALLER)\n"); return 0; }
    int wrong = 0;
    for (const auto& [code, tome] : kCases) {
        Rng rng(1), unit_seed(2);
        const int made = generate_item(data->rules, code, 1, 2, rng, &unit_seed).tome;
        if (made != tome) { std::printf("%s: tome %d, game.exe %d\n", code, made, tome); ++wrong; }
    }
    std::printf("%zu items, %d differ\n", std::size(kCases), wrong);
    return wrong == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
