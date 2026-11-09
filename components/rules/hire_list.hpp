// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace d2d::rules {

// The hire window's rows (FUN_004b5c60; docs/research/re/npc-menu.md
// "Hire"): two lines an offer. The first: the merc's name, " - " (0xf9b,
// 0xf9c, 0xf9b), then Lvl (0xd28), Life (0xd26), Def (0xd27) and Cost
// (0xd29), each its label (at most 20 characters, FUN_00526790), ':' and
// ' ' (0xf9d, 0xf9b) and its value (hire_number), two spaces between. The
// second: two spaces and the hireling's description (hire_description).

// A row's number: "%2u" (wsprintfA), widened at most 9 characters
// (FUN_00526f20: a longer one is cut to 8).
inline std::string hire_number(std::uint32_t value) {
    std::array<char, 16> text{};
    std::snprintf(text.data(), text.size(), "%2u", value);
    std::string number(text.data());
    if (number.size() > 9) number.resize(8);
    return number;
}

// A row's label: its first 20 characters (FUN_00526790(.., 0x14)).
inline std::string hire_label(std::string_view label) { return std::string(label.substr(0, 20)); }

// The description's string (FUN_00663720: table 0x6ee410) by the hireling's
// HireDesc, its hiredesc.txt row (0 None .. 8 Lightning Spells; hireling.txt
// row +0xd2, FUN_00655720's link): 0 none.
inline constexpr std::array<std::uint16_t, 9> kHireDescStrings{ 0, 0xd2b, 0xd2c, 0x2b52, 0x2b50, 0x2b51, 0xd31, 0xd30, 0xd2f };
constexpr std::uint16_t hire_desc_string(int hire_desc) {
    return hire_desc >= 0 && hire_desc < int(kHireDescStrings.size()) ? kHireDescStrings[std::size_t(hire_desc)] : 0;
}
// The description as the row shows it: cut at its first '[' (FUN_0048be80
// with "a0:%0", FUN_0048be00: a grammar code's start).
inline std::string hire_description(std::string_view text) { return std::string(text.substr(0, text.find('['))); }

}  // namespace d2d::rules
