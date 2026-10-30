// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace d2d::d2s {

// The create screen's input callback (FUN_00430590).
inline auto character_name_allows(std::string_view name, char letter) -> bool {
    if ((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z')) return true;
    if ((letter != '-' && letter != '_') || name.empty()) return false;
    return name.find_first_of("-_") == std::string_view::npos;
}

// The create screen's combined input and OK-button rules (FUN_00430590 /
// FUN_00430620). Unlike the TCP host check, apostrophes are not accepted.
inline auto valid_character_name(std::string_view name) -> bool {
    if (name.size() < 2 || name.size() > 15 || name.front() == '-' || name.front() == '_'
        || name.back() == '-' || name.back() == '_') return false;
    int marks = 0;
    for (const char letter : name) {
        if ((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z')) continue;
        if ((letter != '-' && letter != '_') || ++marks > 1) return false;
    }
    return true;
}

} // namespace d2d::d2s
