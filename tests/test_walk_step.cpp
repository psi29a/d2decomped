// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A walker's frame (FUN_00650840: FUN_006502d0, FUN_00650660, FUN_0064fe40, FUN_006507b0): walk_step_cases.inc is game.exe's
// own walk on random paths, positions, velocities and blocked subtiles, frame by frame (tools/emu/walk_step.py);
// rules::walk_frame gives the same return, 16.16 position and point index each frame.

#include <missiles.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>
#include <utility>
#include <vector>

namespace {

struct Frame { int going; std::uint32_t x, y; int point; };
struct Case {
    std::uint32_t x, y;
    int velocity;
    std::vector<std::pair<int, int>> points, blocked;
    std::vector<Frame> frames;
};
const Case kCases[] = {
#include "walk_step_cases.inc"
};

}   // namespace

int main() try {
    int bad = 0;
    for (const auto& one : kCases) {
        std::uint32_t x = one.x, y = one.y;
        std::size_t point = 0;
        // FUN_0064fe40 as the path's making runs it: the points it stands on are passed.
        while (point < one.points.size() && x == (std::uint32_t(one.points[point].first) << 16) + 0x8000 && y == (std::uint32_t(one.points[point].second) << 16) + 0x8000)
            ++point;
        for (std::size_t frame = 0; frame < one.frames.size(); ++frame) {
            const auto& want = one.frames[frame];
            const bool going = d2d::rules::walk_frame(x, y, one.points, point, one.velocity,
                                                      [&](int at_x, int at_y) { return std::ranges::contains(one.blocked, std::pair{ at_x, at_y }); });
            // game.exe's stop leaves the index at 0 (FUN_006507b0); walk_frame's is past the last.
            const bool same = going == (want.going != 0) && x == want.x && y == want.y && (!going || int(point) == want.point);
            if (!same) {
                if (++bad <= 5)
                    std::printf("FAIL: from %#x,%#x velocity %d frame %zu: d2d %d %#x,%#x point %zu, game.exe %d %#x,%#x point %d\n", one.x, one.y,
                                one.velocity, frame, int(going), x, y, point, want.going, want.x, want.y, want.point);
                break;
            }
        }
    }
    if (bad) { std::printf("%d of %zu differ\n", bad, std::size(kCases)); return 1; }
    std::printf("OK: %zu walks step as FUN_00650840 steps them\n", std::size(kCases));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_step: %s\n", error.what());
    return 1;
}
