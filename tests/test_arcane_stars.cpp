// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules palette
// The Arcane Sanctuary's stars (rules::Stars: FUN_00476290 / FUN_00476190)
// against stock 1.14d's own, run by tools/emu/arcane_stars.py: each call's
// draws (x, y, colour per star, in order) hashed, for 300 runs of calls on
// made-up palettes and screen sizes. No game data needed at runtime.
// With a file argument (arcane_stars.py's `gen` cases) it prints
// `case frame hash` per call as `arcane_stars.py game` does, to diff.
#include <palette.hpp>
#include <rules.hpp>
#include <stars.hpp>

#include <array>
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

// arcane_stars.py's palette: 1024 bytes of Rng(seed).next() & 0xff, byte 0
// red, 1 green, 2 blue (the PL2 layout game.exe copies to 0x81e668).
d2d::palette::Palette made_palette(std::uint32_t seed) {
    d2d::rules::Rng rng(seed);
    std::array<std::byte, 1024> bytes{};
    for (auto& byte : bytes) byte = std::byte(rng.next() & 0xff);
    return d2d::palette::Palette(bytes);
}

std::uint64_t fnv(const std::vector<std::uint32_t>& words) {
    std::uint64_t value = 0xcbf29ce484222325ull;
    for (const std::uint32_t word : words)
        for (int shift = 0; shift < 32; shift += 8) value = (value ^ ((word >> shift) & 0xff)) * 0x100000001b3ull;
    return value;
}

// One call of FUN_00476290: made on the first, the draws, then the step.
struct Run {
    d2d::rules::Stars stars;
    d2d::palette::Palette palette;
    std::uint32_t mix;
    int width, height;
    Run(std::uint32_t mix_in, int width_in, int height_in, std::uint32_t palette_seed)
        : palette(made_palette(palette_seed)), mix(mix_in), width(width_in), height(height_in) {}
    std::uint64_t call(std::uint32_t now_ms) {
        if (!stars.made) stars.make(mix, width, height, [&](int red, int green, int blue) { return palette.nearest(red, green, blue); });
        std::vector<std::uint32_t> draws;
        for (const auto& star : stars.stars) draws.insert(draws.end(), { std::uint32_t(star.x), std::uint32_t(star.y), star.colour });
        stars.step(now_ms, width, height);
        return fnv(draws);
    }
};

struct Frame { std::uint32_t now; std::uint64_t hash; };
struct Case { std::uint32_t mix; int width, height; std::uint32_t palette; std::vector<Frame> frames; };

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        std::string line;
        while (std::getline(input, line)) {
            std::istringstream fields(line);
            int case_id = 0, width = 0, height = 0;
            std::uint32_t mix = 0, palette = 0, now = 0;
            fields >> case_id >> mix >> width >> height >> palette;
            Run run(mix, width, height, palette);
            for (int frame = 0; fields >> now; ++frame) std::printf("%d %d %016llx\n", case_id, frame, static_cast<unsigned long long>(run.call(now)));
        }
        return 0;
    }
    const std::vector<Case> cases = {
#include "arcane_stars_cases.inc"
    };
    int failed = 0, frames = 0;
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto& row = cases[index];
        Run run(row.mix, row.width, row.height, row.palette);
        for (std::size_t frame = 0; frame < row.frames.size(); ++frame, ++frames) {
            const std::uint64_t hash = run.call(row.frames[frame].now);
            if (hash == row.frames[frame].hash) continue;
            if (++failed <= 10) std::fprintf(stderr, "FAIL: case %zu frame %zu (mix %u, %d x %d): d2d %016llx, game.exe %016llx\n", index, frame, row.mix, row.width, row.height,
                                             static_cast<unsigned long long>(hash), static_cast<unsigned long long>(row.frames[frame].hash));
        }
    }
    std::printf("arcane stars: %d of %d calls match game.exe\n", frames - failed, frames);
    return failed == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_arcane_stars: %s\n", error.what());
    return 1;
}
