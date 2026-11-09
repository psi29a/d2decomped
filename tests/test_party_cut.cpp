// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// The Party screen's cuts (apps/d2d/party_cut.hpp) against stock 1.14d's
// FUN_0049a2c0 (a row's name) and FUN_0049a830 (a member's area), run by
// tools/emu/party_cut.py (party_cut_cases.inc) with its font: a letter c
// measures 5 + c % 9 px.
#include "party_cut.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <string>

int main() try {
    const auto measure = [](const std::string& text) {
        int width = 0;
        for (const unsigned char letter : text) width += 5 + letter % 9;
        return width;
    };
    struct Case { int kind, in_party; const char* text; const char* drawn; };
    const Case cases[] = {
#include "party_cut_cases.inc"
    };
    for (const auto& row : cases) {
        const std::string cut = row.kind == 0 ? d2d::client::cut_party_name(row.text, row.in_party != 0, measure) : d2d::client::cut_party_area(row.text, measure);
        if (cut != row.drawn) std::fprintf(stderr, "FAIL: kind %d party %d \"%s\": d2d \"%s\", game.exe \"%s\"\n", row.kind, row.in_party, row.text, cut.c_str(), row.drawn);
        assert(cut == row.drawn);
    }
    std::printf("OK: %zu party cuts as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_party_cut: %s\n", error.what());
    return 1;
}
