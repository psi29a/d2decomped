// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: mpq palette
// The PL2 blend tables blit_dcc_frame draws in RGB: each cell
// T[screen * 256 + sprite] is the nearest base entry (FUN_00605210) of
// palette::alpha_blend (FUN_00605880, weights 0xbf / 0x7f / 0x3f),
// additive_blend (FUN_006059b0) or multiply_blend (FUN_00605aa0). Rebuilt
// for each act's Pal.PL2 and compared with the file, all 65,536 cells of
// each table. The max-component table is test_pl2_max_blend's. Then the
// Blender (blend.hpp): each blend drawn is the table's entry for the screen's
// and sprite's colours found back as indices, an off-palette one by its nearest;
// none installed, palette::blend is the RGB formula.
#include <blend.hpp>
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
#include <functional>
#include <string>
#include <utility>

namespace fs = std::filesystem;
using d2d::palette::Rgba;

int main() try {
    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path dir = env ? fs::path(env) : fs::path();
    const auto d2data = dir / "d2data.mpq";
    if (!env || !fs::exists(d2data)) {
        std::printf("SKIP: d2data.mpq not found (set D2_MPQ_DIR)\n");
        return D2D_TEST_SKIPPED;
    }
    struct Table { const char* name; std::size_t offset; std::function<Rgba(const Rgba&, const Rgba&)> mix; };
    const auto alpha = [](int weight) { return [weight](const Rgba& screen, const Rgba& sprite) { return d2d::palette::alpha_blend(screen, sprite, weight); }; };
    const Table tables[]{
        { "alpha 0xbf", 0x3500, alpha(d2d::palette::kAlphaThreeQuarters) },
        { "alpha 0x7f", 0x13500, alpha(d2d::palette::kAlphaHalf) },
        { "alpha 0x3f", 0x23500, alpha(d2d::palette::kAlphaQuarter) },
        { "additive", 0x33500, d2d::palette::additive_blend },
        { "multiply", 0x43500, d2d::palette::multiply_blend },
    };
    const d2d::mpq::Archive mpq(d2data);
    for (int act = 1; act <= 4; ++act) {
        const auto raw = mpq.read("data\\global\\palette\\ACT" + std::to_string(act) + "\\Pal.pl2");
        assert(raw.size() == d2d::palette::Pl2::kFileSize);
        const d2d::palette::Pl2 pl2(raw);
        const auto& entries = pl2.base_palette();
        const d2d::palette::Palette base(entries);
        for (const auto& table : tables) {
            int differ = 0;
            for (std::size_t screen = 0; screen < 256; ++screen) {
                for (std::size_t sprite = 0; sprite < 256; ++sprite) {
                    const auto mixed = table.mix(entries[screen], entries[sprite]);
                    if (base.nearest(mixed.r, mixed.g, mixed.b) != std::uint8_t(raw[table.offset + screen * 256 + sprite])) ++differ;
                }
            }
            std::printf("act %d %s: %d of 65536 cells differ\n", act, table.name, differ);
            assert(differ == 0);
        }
        using d2d::palette::Blend;
        d2d::palette::Blender blender(pl2);
        const Blend modes[]{ Blend::kAlpha75, Blend::kAlpha50, Blend::kAlpha25, Blend::kAdditive, Blend::kMultiply, Blend::kMaxComponent };
        const std::size_t offsets[]{ 0x3500, 0x13500, 0x23500, 0x33500, 0x43500, 0x5b500 };
        int wrong = 0;
        // The frame keeps the palette's own colours (the display ramp goes on as it's shown), so each blend
        // is the table's cell for the ORIGINAL indices, whatever the ramp (the default gamma 155, a flat one,
        // another): the lookup never sees a ramped colour.
        for (const auto& [gamma, contrast] : { std::pair{ d2d::palette::kDefaultGamma, d2d::palette::kDefaultContrast }, std::pair{ 100, 100 }, std::pair{ 205, 60 } }) {
            d2d::palette::Palette::set_ramp(gamma, contrast);
            for (std::size_t mode = 0; mode < 6; ++mode)
                for (std::size_t screen = 0; screen < 256; ++screen)
                    for (std::size_t sprite = 0; sprite < 256; sprite += 7) {
                        const auto want = entries[std::uint8_t(raw[offsets[mode] + screen * 256 + sprite])];
                        const auto got = blender.mix(modes[mode], base[std::uint8_t(screen)], base[std::uint8_t(sprite)]);
                        if (got.r != want.r || got.g != want.g || got.b != want.b) ++wrong;
                    }
        }
        d2d::palette::Palette::set_ramp(d2d::palette::kDefaultGamma, d2d::palette::kDefaultContrast);
        // What makes the lookup exact: an entry sharing another's colour has its rows and columns.
        for (std::size_t mode = 0; mode < 6; ++mode)
            for (std::size_t entry = 0; entry < 256; ++entry) {
                const std::size_t found = blender.index_of(entries[entry]);
                for (std::size_t other = 0; other < 256; ++other) {
                    if (raw[offsets[mode] + entry * 256 + other] != raw[offsets[mode] + found * 256 + other]) ++wrong;
                    if (raw[offsets[mode] + other * 256 + entry] != raw[offsets[mode] + other * 256 + found]) ++wrong;
                }
            }
        const Rgba off{ 1, 2, 250, 0xFF };                 // no entry's: its nearest
        assert(blender.index_of(off) == base.nearest(1, 2, 250));
        std::printf("act %d Blender: %d wrong\n", act, wrong);
        assert(wrong == 0);
    }
    assert(d2d::palette::installed_blender() == nullptr);
    const Rgba red{ 200, 10, 10, 0xFF }, grey{ 100, 100, 100, 0xFF };
    const auto plain = d2d::palette::blend(d2d::palette::Blend::kAdditive, grey, red);
    assert(plain.r == 255 && plain.g == 110 && plain.b == 110);
    // The weights are game.exe's, not exact quarters: a quarter of black over
    // white keeps 255 * 192 / 255 = 192 of it, where 255 * 3 / 4 is 191.
    const Rgba black{ 0, 0, 0, 0xFF }, white{ 255, 255, 255, 0xFF };
    assert(d2d::palette::alpha_blend(white, black, d2d::palette::kAlphaQuarter).r == 192);
    assert(d2d::palette::alpha_blend(black, white, d2d::palette::kAlphaHalf).r == 127);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_pl2_blends: %s\n", error.what());
    return 1;
}
