// SPDX-License-Identifier: GPL-3.0-or-later
// game.exe's second UTF-8 reader, the one with surrogate pairs
// (FUN_006c7410), and the bounded copy built on it (FUN_00526e40).
// tbl.hpp's utf8_units is the other reader (FUN_00526100).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace d2d::tbl {

// What FUN_006c7410 gives back: the units written (a 0 among them when it
// stops on one), the bytes taken before where it stopped, and its status:
// 0 at a 0 unit, 1 with the output full, -1 at the end of the input,
// -1 - n when an n-byte tail runs past the input.
struct Utf8Read {
    std::int32_t status = -1;
    std::u16string units;
    std::size_t taken = 0;
};

// FUN_006c7410: `text` into at most `capacity` 16-bit units.
auto read_utf8(std::span<const std::uint8_t> text, std::size_t capacity) -> Utf8Read;

// FUN_00526e40: NUL-terminated UTF-8 `text` as at most `capacity` units
// (capacity 1 or more); "" for text the reader stops on or can't fit.
auto copy_utf8(std::string_view text, std::int32_t capacity) -> std::u16string;

}  // namespace d2d::tbl
