// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: mpq palette
// COF draw effect 6's table (PL2 +0x5b500, FUN_006c8250 mode 6) is
// FUN_00605b70's blend: each cell T[screen * 256 + sprite] is the nearest
// base entry (FUN_00605210) of max_component_blend, the sprite weighted by
// its greatest channel. Rebuilt for each act's Pal.PL2 and compared with
// the file, all 65,536 cells. tools/emu pl2.py maxblend shows game.exe
// installs the file's table as it is (docs/research/formats/pl2.md).
#include <mpq.hpp>
#include <palette.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

int main() try {
    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path dir = env ? fs::path(env) : fs::path();
    const auto d2data = dir / "d2data.mpq";
    if (!env || !fs::exists(d2data)) {
        std::printf("SKIP: d2data.mpq not found (set D2_MPQ_DIR)\n");
        return D2D_TEST_SKIPPED;
    }
    const d2d::mpq::Archive mpq(d2data);
    for (int act = 1; act <= 4; ++act) {
        const auto raw = mpq.read("data\\global\\palette\\ACT" + std::to_string(act) + "\\Pal.pl2");
        assert(raw.size() == d2d::palette::Pl2::kFileSize);
        const d2d::palette::Pl2 pl2(raw);
        const d2d::palette::Palette base(pl2.base_palette());
        int differ = 0;
        for (int screen = 0; screen < 256; ++screen) {
            for (int sprite = 0; sprite < 256; ++sprite) {
                const auto& over = pl2.base_palette()[std::size_t(sprite)];
                const auto mixed = d2d::palette::max_component_blend(pl2.base_palette()[std::size_t(screen)], over,
                                                                     d2d::palette::max_component_weight(over));
                if (base.nearest(mixed.r, mixed.g, mixed.b) != pl2.max_blend(std::uint8_t(screen), std::uint8_t(sprite))) ++differ;
            }
        }
        std::printf("act %d: %d of 65536 cells differ\n", act, differ);
        assert(differ == 0);
    }
    // Black is see-through, a full channel covers the screen, and the weight
    // is the greatest channel, not each channel's own.
    const d2d::palette::Rgba screen{ 200, 40, 10, 0xFF };
    const d2d::palette::Rgba black{ 0, 0, 0, 0xFF }, white{ 255, 255, 255, 0xFF }, blue{ 0, 0, 128, 0xFF };
    assert((d2d::palette::max_component_blend(screen, black, 0) == screen));
    assert((d2d::palette::max_component_blend(screen, white, 255) == white));
    const auto dim = d2d::palette::max_component_blend(screen, blue, d2d::palette::max_component_weight(blue));
    assert(dim.r == 200 * 127 / 255 && dim.g == 40 * 127 / 255 && dim.b == (10 * 127 + 128 * 128) / 255);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_pl2_max_blend: %s\n", error.what());
    return 1;
}
