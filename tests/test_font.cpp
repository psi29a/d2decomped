// SPDX-License-Identifier: GPL-3.0-or-later
// Load font16 (metrics + glyph sheet) and verify glyph lookup + measure.
#include <dc6.hpp>
#include <font.hpp>
#include <mpq.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

int main() {
    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path dir = env ? fs::path(env)
        : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "")
            / "Workspace/private/diablo2";
    const auto d2data = dir / "d2data.mpq";
    if (!fs::exists(d2data)) {
        std::printf("SKIP: %s not found\n", d2data.string().c_str());
        return 0;
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
    for (char letter : std::string_view("Hello")) by_hand += font.find(letter)->width;
    assert(width == by_hand);
    assert(width > 0);

    std::printf("measure(\"Hello\") = %d px\n", width);

    // A missing code returns nullptr.
    assert(font.find(0xFFFF) == nullptr);

    std::printf("OK\n");
    return 0;
}
