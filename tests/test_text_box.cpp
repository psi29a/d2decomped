// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: font
#include <dc6.hpp>
#include <font.hpp>
#include <palette.hpp>
#include <text_box.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static auto read_bytes(const std::filesystem::path& path) -> std::vector<std::byte> {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.string());
    std::vector<char> bytes{ std::istreambuf_iterator<char>(input), {} };
    return { reinterpret_cast<const std::byte*>(bytes.data()),
             reinterpret_cast<const std::byte*>(bytes.data() + bytes.size()) };
}

static void dump(const std::filesystem::path& directory) {
    std::string input;
    int case_index = 0;
    while (std::getline(std::cin, input)) {
        std::istringstream fields(input);
        std::string name, text;
        int width{}, height{}, margin_x{}, margin_y{};
        fields >> name >> width >> height >> margin_x >> margin_y;
        if (!fields || fields.get() != '\t') throw std::runtime_error("bad case");
        std::getline(fields, text);
        const auto bytes = read_bytes(directory / (name + ".tbl"));
        const d2d::font::Font font(bytes, d2d::dc6::Sprite{});
        const auto rows = d2d::font::text_box_rows(text, 300, 250, width, height, font.line_height(),
            [&](std::string_view line) { return font.measure(line); }, margin_x, margin_y);
        int row_index = 0;
        for (const auto& row : rows) {
            std::printf("%d %d %d %d ", case_index, row_index++, row.x, row.baseline);
            for (unsigned char letter : row.text) std::printf("%02x", letter);
            std::printf("\n");
        }
        ++case_index;
    }
}

static void baseline_pixels() {
    // One solid 1x3 glyph, offset (+2,-1). No Blizzard assets needed.
    std::vector<std::byte> sprite(28 + 32 + 9);
    auto write32 = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned int index = 0; index < 4; ++index)
            sprite[offset + index] = std::byte((value >> (8 * index)) & 255);
    };
    write32(0, 6); write32(16, 1); write32(20, 1); write32(24, 28);
    write32(32, 1); write32(36, 3); write32(40, 2); write32(44, 0xffffffff);
    write32(56, 9);
    for (int index = 0; index < 3; ++index) {
        sprite[std::size_t(60 + 3 * index)] = std::byte{1};
        sprite[std::size_t(61 + 3 * index)] = std::byte{1};
        sprite[std::size_t(62 + 3 * index)] = std::byte{0x80};
    }
    std::vector<std::byte> metrics(26);
    metrics[0] = std::byte{'W'}; metrics[1] = std::byte{'o'};
    metrics[2] = std::byte{'o'}; metrics[3] = std::byte{'!'}; metrics[4] = std::byte{1};
    metrics[10] = std::byte{2}; metrics[12] = std::byte{'A'};
    metrics[15] = std::byte{2}; metrics[16] = std::byte{2};
    const d2d::font::Font font(metrics, d2d::dc6::Sprite(sprite));
    std::array<d2d::palette::Rgba, 256> colours{};
    colours[1] = { 255, 255, 255, 255 };
    const d2d::palette::Palette palette(colours);
    std::vector<std::uint8_t> framebuffer(16 * 16 * 4);
    assert(font.draw_tinted_baseline(framebuffer, 16, 16, palette, 4, 10, "AA", 255, 255, 255) == 8);
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        const bool expected = (x == 6 || x == 8) && y >= 7 && y <= 9;
        assert((framebuffer[std::size_t((y * 16 + x) * 4 + 3)] != 0) == expected);
    }
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--dump") { dump(argv[2]); return 0; }
    const auto measure = [](std::string_view text) { return int(text.size()) * 5; };
    const auto exact = d2d::font::text_box_rows("abc def", 10, 100, 35, 50, 10, measure);
    assert(exact.size() == 2 && exact[0].text == "abc" && exact[1].text == "def");
    assert(exact[0].x == 20 && exact[0].baseline == 60 && exact[1].baseline == 74);
    const auto long_word = d2d::font::text_box_rows("abcdefgh", 0, 100, 20, 100, 10, measure);
    assert(long_word.size() == 3 && long_word[0].text == "abc" && long_word[1].text == "def" && long_word[2].text == "gh");
    const auto spaces = d2d::font::text_box_rows("  abc  def", 0, 100, 35, 100, 10, measure);
    assert(spaces.size() == 2 && spaces[0].text == "abc " && spaces[1].text == "def");
    const auto margins = d2d::font::text_box_rows("abc def ghi", 0, 100, 50, 40, 10, measure, 10, 8);
    assert(margins.size() == 1 && margins[0].text == "ghi" && margins[0].baseline == 78);
    const auto empty = d2d::font::text_box_rows("", 0, 100, 20, 1, 10, measure);
    assert(empty.size() == 1 && empty[0].text.empty());
    const auto tiny = d2d::font::text_box_rows("abc", 0, 100, 1, 100, 10, measure);
    assert(tiny.size() == 3 && tiny[0].text == "a" && tiny[2].text == "c");
    baseline_pixels();
    std::puts("text-box regression checks passed");
}
