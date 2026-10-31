// SPDX-License-Identifier: GPL-3.0-or-later
// palette-dump < pal.dat — print d2d's decoded palette as "index R G B" lines.
#include <palette.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <iterator>
#include <vector>

int main() try {
    const std::vector<char> input(std::istreambuf_iterator<char>(std::cin), {});
    std::vector<std::byte> bytes;
    bytes.reserve(input.size());
    for (const char value : input) bytes.push_back(std::byte(value));
    const d2d::palette::Palette palette(bytes);
    for (int index = 0; index < 256; ++index) {
        const auto colour = palette[std::uint8_t(index)];
        std::printf("%02x %02x %02x %02x\n", index, colour.r, colour.g, colour.b);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
