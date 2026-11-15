// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: mpq font
// Load font16 (metrics + glyph sheet) and verify glyph lookup + measure.
#include <dc6.hpp>
#include <font.hpp>
#include <mpq.hpp>
#include <palette.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

int main() try {
    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path dir = env ? fs::path(env)
        : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "")
            / "Workspace/private/diablo2";
    const auto d2data = dir / "d2data.mpq";
    if (!fs::exists(d2data)) {
        std::printf("SKIP: %s not found\n", d2data.string().c_str());
        return D2D_TEST_SKIPPED;
    }

    d2d::mpq::Archive mpq(d2data);

    // font16 is one of the mid-size UI fonts.
    auto tbl_bytes = mpq.read(R"(data\local\FONT\LATIN\font16.tbl)");
    auto dc6_bytes = mpq.read(R"(data\local\FONT\LATIN\font16.dc6)");
    d2d::dc6::Sprite sheet(dc6_bytes);

    d2d::font::Font font(tbl_bytes, std::move(sheet));
    std::printf("font16: glyphs=%zu line_height=%d sheet_frames=%zu\n",
                font.glyph_count(), font.line_height(),
                font.sheet().total_frames());

    assert(font.glyph_count() == 256);      // one record per Latin-1 byte

    // Every printable ASCII char should have a glyph with non-zero width.
    for (int letter = 'A'; letter <= 'Z'; ++letter) {
        const auto* glyph = font.find(std::uint16_t(letter));
        assert(glyph);
        assert(glyph->width > 0);
        assert(glyph->height > 0);
        assert(glyph->frame < font.sheet().total_frames());
    }

    // "Hello" should have a positive measured width equal to the sum of
    // per-char advance widths.
    const auto width = font.measure("Hello");
    int by_hand = 0;
    for (char letter : std::string_view("Hello")) { const auto* glyph = font.find(letter); assert(glyph); by_hand += glyph->width; }
    assert(width == by_hand);
    assert(width > 0);

    std::printf("measure(\"Hello\") = %d px\n", width);

    // A missing code returns nullptr.
    assert(font.find(0xFFFF) == nullptr);

    // The button label fonts' .tbl heights (byte 10, FUN_00501a40), and
    // their multiply draw (mode 4): onto white it is the plain draw, onto
    // black it leaves black, onto grey it never lightens.
    auto load = [&](const char* name) {
        const std::string base = std::string(R"(data\local\FONT\LATIN\)") + name;
        return d2d::font::Font(mpq.read(base + ".tbl"), d2d::dc6::Sprite(mpq.read(base + ".dc6")));
    };
    assert(font.tbl_height() == 10);
    const auto exocet10 = load("FontExocet10");
    assert(exocet10.tbl_height() == 10);
    assert(load("FontRidiculous").tbl_height() == 7);
    std::array<d2d::palette::Rgba, 256> ramp{};
    for (std::size_t index = 0; index < 256; ++index)
        ramp[index] = { std::uint8_t(index), std::uint8_t(index), std::uint8_t(index), 255 };
    const d2d::palette::Palette grey(ramp);
    constexpr std::uint32_t kSide = 40;
    auto drawn = [&](std::uint8_t under, bool multiply) {
        std::vector<std::uint8_t> pixels(std::size_t(kSide) * kSide * 4, under);
        exocet10.draw_tinted_baseline(pixels, kSide, kSide, grey, 2, 30, "SO", 255, 255, 255, multiply);
        return pixels;
    };
    assert(drawn(255, true) == drawn(255, false));
    for (std::uint8_t value : drawn(0, true)) assert(value == 0);
    const auto plain = drawn(128, false);
    const auto multiplied = drawn(128, true);
    bool changed = false;
    for (std::size_t index = 0; index < plain.size(); index += 4) {
        assert(multiplied[index] <= 128);
        if (multiplied[index] != 128) {
            changed = true;
            assert(multiplied[index] == plain[index] * 128 / 255);
        }
    }
    assert(changed);

    std::printf("OK\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
