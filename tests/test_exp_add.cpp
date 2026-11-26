// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A player's experience award added (FUN_0057e510, SUnitDmg.cpp: a 32-bit
// add capped at Experience.txt's row MaxLvl - 1) against stock 1.14d's own,
// run by tools/emu/exp_add.py: d2d::rules::exp_after_award on the same
// inputs, and rules::gain_exp's cap. No game data needed at runtime. With a
// file argument (exp_add.py's `game` output, or its `gen` cases) it prints
// each case line with d2d's answer instead, to diff.
#include <combat.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kFixture[] = {
#include "exp_add_cases.inc"
};

auto split(const std::string& text, char separator) -> std::vector<std::string> {
    std::vector<std::string> parts;
    std::stringstream stream(text);
    for (std::string part; std::getline(stream, part, separator);) parts.push_back(part);
    return parts;
}

// One case line (`case class exp award max_level cap level`, maybe with
// game.exe's `|lastexp exp levelup` after it): the line with d2d's stat 29
// and 13. The level-up flag is game.exe's, passed through (d2d's gain_exp
// levels up by its own loop); a `gen` line without it gets `-`.
auto line_of(const std::string& text) -> std::string {
    const auto bar = text.find('|');
    const std::string case_text = text.substr(0, bar);
    const auto fields = split(case_text, '\t');
    if (fields.size() != 7) throw std::runtime_error("malformed case: " + text);
    const auto exp = std::uint32_t(std::stoull(fields[2]));
    const auto award = std::int32_t(std::stoll(fields[3]));
    const auto cap = std::uint32_t(std::stoull(fields[5]));
    const std::uint32_t after = d2d::rules::exp_after_award(exp, award, cap);
    const auto last = std::int32_t(after - exp);
    std::string level_up = "-";
    if (bar != std::string::npos) {
        const auto answer = split(text.substr(bar + 1), ' ');
        if (answer.size() != 3) throw std::runtime_error("malformed answer: " + text);
        level_up = answer[2];
    }
    return case_text + "|" + std::to_string(last) + " " + std::to_string(after) + " " + level_up;
}

void hand_checks() {
    // gain_exp keeps the experience at Experience.txt's row MaxLvl - 1.
    std::vector<std::int64_t> next(100, 0);
    for (std::size_t level = 1; level < next.size(); ++level) next[level] = std::int64_t(level) * 1000;
    next[98] = 3520485254;
    d2d::d2s::Stats stats;
    stats.values[d2d::d2s::kLevel] = 99;
    stats.values[d2d::d2s::kExp] = 3520485000;
    const d2d::rules::ClassGains gains{};
    assert(d2d::rules::gain_exp(stats, 5000, next, gains) == 0);
    assert(stats.values[d2d::d2s::kExp] == 3520485254);
    assert(d2d::rules::exp_after_award(0xffffffffu, 2, 0xffffffffu) == 1);   // the sum wraps first
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream file(argv[1]);
        std::string text;
        while (std::getline(file, text)) {
            if (!text.empty() && text.back() == '\r') text.pop_back();
            if (!text.empty()) std::printf("%s\n", line_of(text).c_str());
        }
        return 0;
    }
    hand_checks();
    int matched = 0;
    for (const char* row : kFixture) {
        const std::string want = row;
        const std::string got = line_of(want);
        if (got != want) {
            std::fprintf(stderr, "game.exe: %s\nd2d:      %s\n", want.c_str(), got.c_str());
            return 1;
        }
        ++matched;
    }
    std::printf("experience add: %d of game.exe's cases match\n", matched);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_exp_add: %s\n", error.what());
    return 1;
}
