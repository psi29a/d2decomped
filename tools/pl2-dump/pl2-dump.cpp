// SPDX-License-Identifier: GPL-3.0-or-later
// pl2-dump <additive|blend50> < Pal.PL2 — print d2d's table as index/value lines.
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
    if (argc != 2 || (std::string_view(argv[1]) != "additive" && std::string_view(argv[1]) != "blend50")) {
        std::fprintf(stderr, "usage: pl2-dump <additive|blend50> < Pal.PL2\n");
        return 2;
    }
    const std::vector<char> input(std::istreambuf_iterator<char>(std::cin), {});
    std::vector<std::byte> bytes;
    bytes.reserve(input.size());
    for (const char value : input) bytes.push_back(std::byte(value));
    const d2d::palette::Pl2 pl2(bytes);
    const bool additive = std::string_view(argv[1]) == "additive";
    for (int index = 0; index < 256 * 256; ++index) {
        const auto foreground = std::uint8_t(index / 256);
        const auto background = std::uint8_t(index % 256);
        const auto value = additive ? pl2.additive(foreground, background) : pl2.blend50(foreground, background);
        std::printf("%04x %02x\n", index, value);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
