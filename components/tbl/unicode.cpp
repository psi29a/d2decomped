// SPDX-License-Identifier: GPL-3.0-or-later
// docs/research/re/tbl-strings.md "The second UTF-8 reader".
#include "unicode.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::tbl {

namespace {

// 0x6fcca0: the bytes after each lead byte, 0 for 0x00..0xbf (a stray
// continuation byte reads as itself), 5 from 0xfc.
auto tail_of(std::uint8_t lead) -> std::uint32_t {
    if (lead < 0xc0) return 0;
    if (lead < 0xe0) return 1;
    if (lead < 0xf0) return 2;
    if (lead < 0xf8) return 3;
    return lead < 0xfc ? 4 : 5;
}

// 0x6fcc88: what each length's lead and continuation bits add up to.
constexpr std::uint32_t kOffsets[] = { 0, 0x3080, 0xe2080, 0x3c82080, 0xfa082080, 0x82082080 };

// FUN_00526e40's scratch: 0x3ff units on its stack (0x526e79).
constexpr std::size_t kScratch = 0x3ff;

}  // namespace

// FUN_006c7410 checks a sequence fits the input (0x6c746f) before the
// output (0x6c74cf). It sums each byte in, shifting 6 between, 32-bit,
// and takes the length's offset off: continuation bits aren't checked,
// so a bad sequence or an overlong form reads as whatever that sum is
// (C0 80 is a 0, which ends the read). Up to 0xffff is one unit, up to
// 0x10ffff a surrogate pair if two units fit, anything above (a sum that
// went under 0 among them) 0xfffd.
auto read_utf8(std::span<const std::uint8_t> text, std::size_t capacity) -> Utf8Read {
    Utf8Read out;
    std::size_t place = 0;
    while (place < text.size()) {
        const std::uint32_t tail = tail_of(text[place]);
        if (place + tail >= text.size()) {
            out.status = -1 - std::int32_t(tail);
            break;
        }
        std::uint32_t code = 0;
        for (std::uint32_t step = 0; step <= tail; ++step) code = (code << 6) + text[place + step];
        code -= kOffsets[tail];
        if (out.units.size() >= capacity) {
            out.status = 1;
            break;
        }
        if (code <= 0xffff) {
            out.units.push_back(char16_t(code));
            if (code == 0) {
                out.status = 0;
                break;
            }
        } else if (code > 0x10ffff) {
            out.units.push_back(char16_t(0xfffd));
        } else {
            if (out.units.size() + 1 >= capacity) {
                out.status = 1;
                break;
            }
            code -= 0x10000;
            out.units.push_back(char16_t(0xd800 + (code >> 10)));
            out.units.push_back(char16_t(0xdc00 + (code & 0x3ff)));
        }
        place += tail + 1;
    }
    out.taken = place;
    return out;
}

// FUN_00526e40 (fastcall: ECX the output, EDX the text, the capacity on
// the stack) reads strlen + 1 bytes into its scratch; any status but 0
// leaves the scratch empty. It copies units while they aren't 0 and fewer
// than the capacity were copied, then puts the 0 after them if the next
// scratch unit is 0, else over the last one copied. So text of exactly
// `capacity` units keeps them all, its 0 one unit past the capacity
// (bugs.md), and longer text keeps capacity - 1. A capacity under 1
// stores a 0 before the output for any text but "" (d2d gives "").
auto copy_utf8(std::string_view text, std::int32_t capacity) -> std::u16string {
    text = text.substr(0, text.find('\0'));
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    const Utf8Read read = read_utf8(bytes, kScratch);
    std::u16string units = read.status == 0 ? read.units : std::u16string();
    units.resize(units.find(u'\0') == std::u16string::npos ? units.size() : units.find(u'\0'));
    if (capacity < 1) return {};
    if (units.size() <= std::size_t(capacity)) return units;
    units.resize(std::size_t(capacity) - 1);
    return units;
}

}  // namespace d2d::tbl
