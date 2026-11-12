// SPDX-License-Identifier: GPL-3.0-or-later
// A monster that dies under a state with the States.txt shatter flag
// (freeze 1, shatter 107; the loader FUN_00618100 puts it in the bit-31
// list, data tables +0x148): the client's death (FUN_004aff60, unit
// command 8) hides the corpse for the hide flag (bit 2, +0xd4,
// FUN_0063a320: +0xc4 loses 2 and gains 0x20, +0xc8 |= 0x40000;
// both states have it) and, for the shatter flag (FUN_0063a6f0), breaks
// it into ice (FUN_004afc10) and sets its hit class's element to 0xa0
// (LAB_004b05be).
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace d2d::rules {

// FUN_004afc10's missiles (FUN_004cd540, create flags 0x2020 from the
// unit), on a monster (unit type 1) only: Missiles.txt row 0x10f + its
// MonStats2 Height (+0xb, `FUN_00451fe0`), then icebreaksmoke (0x113).
// Under uberminion (184) without freeze it makes 0x1ab and 0x1aa instead
// (`uberminion`: Act 5's, not classic). Rows 0x10f..0x113 by name: every
// MonStats2 Height in the 1.14d tables is 0..4.
inline constexpr int kShatterFirstRow = 0x10f;
inline constexpr std::array<std::string_view, 5> kShatterRows = { "monglacialspike", "icebreaksmall", "icebreakmedium", "icebreaklarge", "icebreaksmoke" };
inline constexpr int kShatterSmokeRow = 0x113;
inline constexpr std::array<int, 2> kUberShatterRows = { 0x1ab, 0x1aa };
struct ShatterRows { int first = -1, second = -1; };
inline ShatterRows shatter_rows(int unit_type, int height, bool uberminion, bool frozen) {
    if (unit_type != 1) return {};
    if (uberminion && !frozen) return { kUberShatterRows[0], kUberShatterRows[1] };
    return { kShatterFirstRow + (height & 0xff), kShatterSmokeRow };
}
// A row's name in kShatterRows, empty past it.
inline std::string_view shatter_row_name(int row) {
    const int index = row - kShatterFirstRow;
    return index >= 0 && std::size_t(index) < kShatterRows.size() ? kShatterRows[std::size_t(index)] : std::string_view{};
}

}  // namespace d2d::rules
