// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: net
// test-includes: apps/d2d
// Retail close coordinates from FUN_004ab530; packet decode FUN_0054c870.
#include "skill_tree_close.hpp"

#include <d2gs/c2s.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string_view>
#include <vector>

/**
 * Check the frozen retail close-button coordinates and inclusive hit bounds, plus hotkey packet
 * bytes. Optional dump modes emit comparison rows for the committed executable oracles.
 */
int main(int count, char** values) try {
    const std::string_view mode = count > 1 ? values[1] : "";
    struct Case { int character_class, tab, left, bottom; };
    constexpr Case cases[] = {
#include "retail_tree_close_cases.inc"
    };
    for (const auto& entry : cases) {
        const auto close = d2d::client::skill_tree_close(entry.character_class, entry.tab);
        assert(close.left == entry.left && close.bottom == entry.bottom);
        if (mode == "--dump-close") std::printf("%d\t%d\t%d\t%d\n", entry.character_class, entry.tab, close.left, close.bottom);
        assert(close.contains(close.left, close.bottom));
        assert(close.contains(close.left + 34, close.bottom - 34));
        assert(!close.contains(close.left - 1, close.bottom));
        assert(!close.contains(close.left, close.bottom + 1));
    }
    const std::vector<std::uint8_t> expected{0x51, 54, 0x80, 7, 0, 0xff, 0xff, 0xff, 0xff};
    assert(d2d::net::d2gs::c2s::bind_hotkey(7, 54, true) == expected);
    const std::vector<std::uint8_t> normal{0x51, 36, 0, 0, 0, 0xff, 0xff, 0xff, 0xff};
    assert(d2d::net::d2gs::c2s::bind_hotkey(0, 36, false) == normal);
    const std::vector<std::uint8_t> charged{0x51, 39, 0, 15, 0, 0x78, 0x56, 0x34, 0x12};
    assert(d2d::net::d2gs::c2s::bind_hotkey(15, 39, false, 0x12345678) == charged);
    const std::vector<std::uint8_t> cleared{0x51, 0xff, 0xff, 3, 0, 0xff, 0xff, 0xff, 0xff};
    assert(d2d::net::d2gs::c2s::bind_hotkey(3, 0x7fff, true) == cleared);
    if (mode == "--dump-packets") {
        const std::vector<std::vector<std::uint8_t>> packets{
            d2d::net::d2gs::c2s::bind_hotkey(0, 36, false),
            d2d::net::d2gs::c2s::bind_hotkey(7, 54, true),
            d2d::net::d2gs::c2s::bind_hotkey(15, 39, false, 0x12345678),
            d2d::net::d2gs::c2s::bind_hotkey(3, 0x7fff, true)};
        for (const auto& packet : packets) {
            for (const auto byte : packet) std::printf("%02x", unsigned(byte));
            std::putchar('\n');
        }
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_retail_keyboard: %s\n", error.what());
    return 1;
}
