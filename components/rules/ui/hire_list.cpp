// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of hire_list.hpp's functions; their notes are in the header.
#include "hire_list.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace d2d::rules {

std::string hire_number(std::uint32_t value) {
    std::array<char, 16> text{};
    std::snprintf(text.data(), text.size(), "%2u", value);
    std::string number(text.data());
    if (number.size() > 9) number.resize(8);
    return number;
}

}
