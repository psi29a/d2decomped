// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: mpq dt1 palette rules
// test-includes: apps/d2d
// Where a wall's packed buffer goes on screen (tile_blit.hpp wall_screen_y):
// game.exe draws every wall's signed block y from one origin, upper and lower
// walls alike (FUN_004df1c0 / FUN_004dedf0 -> FUN_004f6920; dt1.md). A made-up
// pair always; with d2data.mpq, the Arcane Sanctuary's own tiles
// (ACT2/Arcane/Sanctuary.dt1): its upper walls where the old rule had them,
// its lower walls' block y 0 on the upper walls' row, 32 px above the old
// rule's (the gap under its walkways).
#include <dt1.hpp>
#include <mpq.hpp>
#include <render/tile_blit.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

constexpr int kIsoY = 300;

// The rules this replaces (world_view.cpp before): an upper wall's buffer
// from its height, a lower wall's from its y_shift, both 16 px up.
int old_upper(const d2d::dt1::Tile& tile) { return kIsoY - ((tile.height < 0 ? -tile.height : tile.height) - 80) - 16; }
int old_lower(const d2d::dt1::Tile& tile) { return kIsoY + 80 - tile.y_shift - 16; }

}  // namespace

int main() try {
    {
        d2d::dt1::Tile upper, lower;
        upper.type = 1; upper.height = -288; upper.y_shift = 256;     // blocks y -256..-64
        lower.type = 16; lower.height = -288; lower.y_shift = 64;     // blocks y -64..192
        assert(d2d::client::wall_screen_y(kIsoY, upper) + upper.y_shift == d2d::client::wall_screen_y(kIsoY, lower) + lower.y_shift);
        assert(d2d::client::wall_screen_y(kIsoY, upper) == old_upper(upper));
        assert(d2d::client::wall_screen_y(kIsoY, lower) == old_lower(lower) - 32);
    }
    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path d2data = fs::path(env ? env : "") / "d2data.mpq";
    if (!env || !fs::exists(d2data)) {
        std::printf("SKIP: the Arcane Sanctuary's DT1 (d2data.mpq not found)\n");
        return 0;
    }
    d2d::mpq::Archive mpq(d2data);
    const d2d::dt1::Archive archive(mpq.read(R"(data\global\tiles\ACT2\Arcane\Sanctuary.dt1)"));
    int uppers = 0, lowers = 0;
    for (const auto& tile : archive.tiles()) {
        if (tile.type == 0 || tile.type == 13 || tile.type == 15 || tile.blocks.empty()) continue;
        const int origin = d2d::client::wall_screen_y(kIsoY, tile) + tile.y_shift;   // block y 0's row
        assert(origin == kIsoY + 32);
        if (tile.type < 16) {
            assert(d2d::client::wall_screen_y(kIsoY, tile) == old_upper(tile));
            ++uppers;
        } else {
            assert(d2d::client::wall_screen_y(kIsoY, tile) == old_lower(tile) - 32);
            ++lowers;
        }
    }
    std::printf("Sanctuary.dt1: %d upper walls unmoved, %d lower walls 32 px up\n", uppers, lowers);
    assert(uppers > 0 && lowers > 0);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_wall_origin: %s\n", error.what());
    return 1;
}
