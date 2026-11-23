// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of shake.hpp's functions; their notes are in the header.
#include "shake.hpp"

#include <algorithm>
#include <cstdint>

namespace d2d::rules {

int quake_volume(int current, int strength) {
    const int target = int(std::clamp<std::int64_t>(std::int64_t(strength) * 255 / 20, 0, 255));
    return current + std::clamp(target - current, -6, 6);
}

}
