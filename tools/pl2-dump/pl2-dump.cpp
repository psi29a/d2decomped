// SPDX-License-Identifier: GPL-3.0-or-later
// pl2-dump <additive|blend50|maxblend> < Pal.PL2 — print d2d's table as index/value lines.
#include <palette.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

int main(int argc, char** argv) try {
    const std::string_view table = argc == 2 ? std::string_view(argv[1]) : std::string_view();
    if (table != "additive" && table != "blend50" && table != "maxblend") {
        std::fprintf(stderr, "usage: pl2-dump <additive|blend50|maxblend> < Pal.PL2\n");
        return 2;
    }
    const std::vector<char> input(std::istreambuf_iterator<char>(std::cin), {});
    std::vector<std::byte> bytes;
    bytes.reserve(input.size());
    for (const char value : input) bytes.push_back(std::byte(value));
    const d2d::palette::Pl2 pl2(bytes);
    for (unsigned foreground = 0; foreground < 256; ++foreground) {
        for (unsigned background = 0; background < 256; ++background) {
            const auto fg_byte = std::uint8_t(foreground);
            const auto bg_byte = std::uint8_t(background);
            const auto value = table == "additive" ? pl2.additive(fg_byte, bg_byte)
                             : table == "blend50"  ? pl2.blend50(fg_byte, bg_byte)
                                                   : pl2.max_blend(fg_byte, bg_byte);   // row = screen, column = sprite
            std::printf("%04x %02x\n", foreground * 256 + background, value);
        }
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
