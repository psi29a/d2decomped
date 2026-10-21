// SPDX-License-Identifier: GPL-3.0-or-later
// Turn game.exe's one-dash switches into the argv CLI11 should see.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::switches {

// `argv` is what CLI11 parses. `ignored` is one warning line per known
// switch that d2d accepted and did not translate.
struct Translated {
    std::vector<std::string> argv;
    std::vector<std::string> ignored;
};

Translated translate(std::span<const std::string_view> arguments);

}  // namespace d2d::switches
