// SPDX-License-Identifier: GPL-3.0-or-later
// The body of state_death.hpp's function; its notes are in the header.
#include "state_death.hpp"

#include <algorithm>
#include <cstddef>

namespace d2d::rules {

// FUN_00639fb0's dword loop (0x639fe0..0x63a0f2): the states a death does not
// keep leave the first bitmap (list +0x58) for the second behind it.
void states_leave_on_death(std::span<std::uint32_t> first, std::span<std::uint32_t> second, std::span<const std::uint32_t> keep) {
    const std::size_t words = std::min({ first.size(), second.size(), keep.size() });
    for (std::size_t index = 0; index < words; ++index) {
        second[index] |= ~keep[index] & first[index];
        first[index] &= keep[index];
    }
}

}   // namespace d2d::rules
