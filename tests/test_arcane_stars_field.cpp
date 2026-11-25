// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules palette
// The stars' field (FUN_00476190 reads DAT_007a5220 / DAT_007a521c, which FUN_0044ba20 sets to the
// screen's width and its height less 0x28): no star is placed in the bottom 40 rows, at either
// resolution the function knows (640 x 480, 800 x 600).
#include <stars.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    using d2d::rules::Stars;
    static_assert(Stars::field_height(480) == 0x1b8);
    static_assert(Stars::field_height(600) == 0x230);
    for (const int screen : { 480, 600 }) {
        Stars stars;
        const int width = screen == 480 ? 640 : 800;
        stars.make(1234u, width, Stars::field_height(screen), [](int, int, int) { return 1; });
        for (const auto& star : stars.stars) assert(star.y >= 0 && star.y < screen - 40);
        stars.step(1000, width, Stars::field_height(screen));
        for (const auto& star : stars.stars) assert(star.y < screen - 40);
    }
    std::puts("ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
