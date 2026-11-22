// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: tbl
// game.exe's second UTF-8 reader (FUN_006c7410) and the bounded copy on it
// (FUN_00526e40) against stock 1.14d's own, run by tools/emu/utf8_copy.py:
// d2d::tbl::read_utf8 / copy_utf8. No game data needed at runtime. With a
// file argument (utf8_copy.py's `game` output, or its `gen` cases) it
// prints each line with d2d's answer instead, to diff.
#include <unicode.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* const kFixture[] = {
#include "utf8_copy_cases.inc"
};

using d2d::tbl::copy_utf8;
using d2d::tbl::read_utf8;

auto from_hex(const std::string& text) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> bytes;
    if (text == "-") return bytes;
    for (std::size_t at = 0; at + 1 < text.size(); at += 2) bytes.push_back(std::uint8_t(std::stoul(text.substr(at, 2), nullptr, 16)));
    return bytes;
}

auto words(const std::u16string& units) -> std::string {
    std::string out;
    char word[5];
    for (const char16_t unit : units) {
        std::snprintf(word, sizeof word, "%04x", unsigned(unit));
        out += word;
    }
    return out.empty() ? "-" : out;
}

// One case's answers, as utf8_copy.py's `game` prints them.
auto answer(const std::vector<std::string>& fields) -> std::vector<std::string> {
    const std::vector<std::uint8_t> bytes = from_hex(fields[3]);
    const int capacity = std::stoi(fields[2]);
    if (fields[1] == "r") {
        const auto read = read_utf8(bytes, std::size_t(capacity));
        return { std::to_string(read.status), std::to_string(read.taken), std::to_string(read.units.size()), words(read.units) };
    }
    return { words(copy_utf8(std::string(bytes.begin(), bytes.end()), capacity)) };
}

// The case part of a line (before `|`) and the whole line as d2d answers it.
auto line_of(const std::string& text) -> std::string {
    std::stringstream stream(text);
    std::vector<std::string> fields;
    std::string field;
    while (stream >> field && field != "|") fields.push_back(field);
    std::string line;
    for (const auto& part : fields) line += part + '\t';
    line += '|';
    for (const auto& part : answer(fields)) line += '\t' + part;
    return line;
}

auto spaced(std::string text) -> std::string {
    for (char& letter : text)
        if (letter == '\t') letter = ' ';
    return text;
}

void hand_checks() {
    // e9 alone is a 3-byte lead whose tail is the NUL: the reader stops
    // short (-3) and the copy is empty. C3 A9 is U+00E9.
    assert(copy_utf8("caf\xc3\xa9", 0x400) == u"caf\x00e9");
    assert(copy_utf8("caf\xe9", 0x400).empty());
    // An overlong NUL ends the text; a supplementary code point is a pair.
    assert(copy_utf8("ab\xc0\x80" "cd", 0x400) == u"ab");
    assert(copy_utf8("\xf0\x9f\x98\x80", 0x400) == u"\xd83d\xde00");
    // Exactly the capacity keeps it all; one more keeps capacity - 1.
    assert(copy_utf8("abcd", 4) == u"abcd");
    assert(copy_utf8("abcde", 4) == u"abc");
    // Past the 0x3ff-unit scratch: nothing.
    assert(copy_utf8(std::string(0x3ff, 'x'), 0x400).empty());
    assert(copy_utf8(std::string(0x3fe, 'x'), 0x400).size() == 0x3fe);
    // A pair needs two units of room.
    const std::vector<std::uint8_t> pair = { 'a', 0xf0, 0x9f, 0x98, 0x80 };
    const auto read = read_utf8(pair, 2);
    assert(read.status == 1 && read.units == u"a" && read.taken == 1);
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream file(argv[1]);
        std::string text;
        while (std::getline(file, text))
            if (!text.empty()) std::printf("%s\n", line_of(text).c_str());
        return 0;
    }
    hand_checks();
    int matched = 0;
    for (const char* row : kFixture) {
        const std::string want = row;
        const std::string got = spaced(line_of(want));
        if (got != want) {
            std::fprintf(stderr, "game.exe: %s\nd2d:      %s\n", want.c_str(), got.c_str());
            return 1;
        }
        ++matched;
    }
    std::printf("utf8 copy: %d of game.exe's cases match\n", matched);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_utf8_copy: %s\n", error.what());
    return 1;
}
