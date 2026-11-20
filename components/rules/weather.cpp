// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of weather.hpp's functions; their notes are in the header.
#include "weather.hpp"

#include <array>
#include <cstdint>

namespace d2d::rules {

std::array<std::uint8_t, 3> rain_rgb(int kind, int tone) {
    if (kind == 0) { const int shade = tone * 80 / 12; return { std::uint8_t(98 - shade), std::uint8_t(123 - shade), std::uint8_t(98 - shade) }; }
    if (kind == 2) { const int shade = tone * 2; return { std::uint8_t(25 - shade), std::uint8_t(30 - shade), std::uint8_t(25 - shade) }; }
    const int shade = tone * 40 / 12;
    return { std::uint8_t(45 - shade), std::uint8_t(55 - shade), std::uint8_t(45 - shade) };
}

int flake_tone(int kind, int size) {
    const int index = size * 12 / 8;
    return kind == 0 ? index : kind == 2 ? index / 4 : index / 2;
}

std::array<std::uint8_t, 3> snow_rgb(int tone) {
    const auto grey = std::uint8_t(120 + tone * 80 / 12);
    return { grey, grey, grey };
}

}
