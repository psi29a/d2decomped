// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: font
// The chat edit box wraps with FUN_00502970 (game.exe 1.14d), not the
// chat lines' space break. A trailing space stays on the line when that
// space fits and the next word does not. Language ids 6, 8 and 9
// (FUN_00526d30) stop on the unit that would start the next line when
// that unit is not an ASCII letter or digit. Id 7 stays on the Latin break.
#include <font.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

static auto lines_of(std::string_view text, int box_width, auto glyph_width, int language_id = 0) -> std::vector<std::string> {
    return d2d::font::wrap_edit_box(text, box_width, glyph_width, language_id);
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

    // Ids 6, 8 and 9: a letter is not a break, so the space that Latin
    // keeps moves to the next line and the leading-space drop removes it.
    const auto language_six = lines_of("aa bb", 3, unit_width, 6);
    assert(language_six.size() == 2 && language_six[0] == "aa" && language_six[1] == "bb");
    const auto language_eight = lines_of("aa bb", 3, unit_width, 8);
    assert(language_eight.size() == 2 && language_eight[0] == "aa" && language_eight[1] == "bb");
    const auto language_nine = lines_of("aa bb", 3, unit_width, 9);
    assert(language_nine.size() == 2 && language_nine[0] == "aa" && language_nine[1] == "bb");
    const auto language_seven = lines_of("aa bb", 3, unit_width, 7);
    assert(language_seven.size() == 2 && language_seven[0] == "aa " && language_seven[1] == "bb");
    const auto language_ten = lines_of("aa bb", 3, unit_width, 10);
    assert(language_ten.size() == 2 && language_ten[0] == "aa " && language_ten[1] == "bb");

    // A period is not in the byte half of that set, so the walk stops
    // with the period starting the next line. Latin has no space, so it
    // cuts on the longest prefix.
    const auto period = lines_of("xx.y", 3, unit_width, 6);
    assert(period.size() == 2 && period[0] == "xx" && period[1] == ".y");
    const auto period_latin = lines_of("xx.y", 3, unit_width, 0);
    assert(period_latin.size() == 2 && period_latin[0] == "xx." && period_latin[1] == "y");

    const auto cut_six = lines_of("abcd", 2, unit_width, 6);
    assert(cut_six.size() == 2 && cut_six[0] == "ab" && cut_six[1] == "cd");

    std::puts("edit-box wrap checks passed");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
