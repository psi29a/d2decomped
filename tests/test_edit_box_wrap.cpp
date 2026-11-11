// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: font
// The chat edit box wraps with FUN_00502970 (game.exe 1.14d), not the
// chat lines' space break. A trailing space stays on the line when that
// space fits and the next word does not.
#include <font.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

static auto lines_of(std::string_view text, int box_width, auto glyph_width) -> std::vector<std::string> {
    return d2d::font::wrap_edit_box(text, box_width, glyph_width);
}

static auto unit_width(unsigned char code) -> int {
    return code == 0 ? 0 : 1;
}

int main() try {
    const auto spaced = lines_of("aa bb", 3, unit_width);
    assert(spaced.size() == 2 && spaced[0] == "aa " && spaced[1] == "bb");

    const auto both_spaces = lines_of("aa  bb", 4, unit_width);
    assert(both_spaces.size() == 2 && both_spaces[0] == "aa  " && both_spaces[1] == "bb");

    const auto leading = lines_of(" hi", 10, unit_width);
    assert(leading.size() == 1 && leading[0] == " hi");

    const auto fitting = lines_of("aa", 2, unit_width);
    assert(fitting.size() == 1 && fitting[0] == "aa");

    const auto cut = lines_of("abcd", 2, unit_width);
    assert(cut.size() == 2 && cut[0] == "ab" && cut[1] == "cd");

    // A glyph wider than the box is its own line, then an empty one
    // (the search continues while the index is still the length, 0x502b45).
    const auto over = lines_of("a", 0, unit_width);
    assert(over.size() == 2 && over[0] == "a" && over[1].empty());

    const auto newline = lines_of("a\nb", 2, [](unsigned char code) { return code == '\n' ? 9 : 2; });
    assert(newline.size() == 2 && newline[0] == "a\n" && newline[1] == "b");

    const char coded[] = { static_cast<char>(0xff), 'c', '1', 'a', 'a' };
    const char coded_line[] = { static_cast<char>(0xff), 'c', '1', 'a' };
    const auto colour = lines_of(std::string_view(coded, 5), 2, [](unsigned char code) { return code == 'a' ? 2 : 9; });
    assert(colour.size() == 2 && colour[0] == std::string(coded_line, 4) && colour[1] == "a");

    std::puts("edit-box wrap checks passed");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
