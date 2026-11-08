// SPDX-License-Identifier: GPL-3.0-or-later
// Turn game.exe's one-dash switches into the argv CLI11 should see.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CLI { class App; }

namespace d2d::switches {

// `argv` is what CLI11 parses. A wired switch is renamed, and its value
// is left for CLI11. `ignored` is one warning line per known switch or
// game.exe mode word (-expand) that d2d accepted and did not translate.
struct Translated {
    std::vector<std::string> argv;
    std::vector<std::string> ignored;
};

// Call after registering the app's options. Required values of d2d's
// scalar options are copied verbatim, even when they start with a dash.
// CLI11 still handles conversion, validation and missing-value errors.
Translated translate(std::span<const std::string_view> arguments, const CLI::App& app);

}  // namespace d2d::switches
