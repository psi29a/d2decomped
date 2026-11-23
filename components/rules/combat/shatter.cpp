// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of shatter.hpp's functions; their notes are in the header.
#include "shatter.hpp"

#include <cstddef>
#include <string_view>

namespace d2d::rules {

ShatterRows shatter_rows(int unit_type, int height, bool uberminion, bool frozen) {
    if (unit_type != 1) return {};
    if (uberminion && !frozen) return { kUberShatterRows[0], kUberShatterRows[1] };
    return { kShatterFirstRow + (height & 0xff), kShatterSmokeRow };
}

std::string_view shatter_row_name(int row) {
    const int index = row - kShatterFirstRow;
    return index >= 0 && std::size_t(index) < kShatterRows.size() ? kShatterRows[std::size_t(index)] : std::string_view{};
}

}
