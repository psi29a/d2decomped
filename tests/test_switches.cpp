// SPDX-License-Identifier: GPL-3.0-or-later
// game.exe's one-dash switches, before CLI11 sees them.
#include <switches.hpp>

#include <cassert>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

d2d::switches::Translated run(std::initializer_list<std::string_view> arguments) {
    const std::vector<std::string_view> list(arguments);
    return d2d::switches::translate(list);
}

void expect_argv(const d2d::switches::Translated& translated, std::initializer_list<std::string_view> expected) {
    const std::vector<std::string_view> want(expected);
    assert(translated.argv.size() == want.size());
    for (std::size_t index = 0; index < want.size(); ++index) assert(translated.argv[index] == want[index]);
}

std::string ignored_exact(std::string_view typed) {
    return std::format("game.exe switch {} ignored (not supported yet)", typed);
}

std::string ignored_as(std::string_view typed, std::string_view token) {
    return std::format("game.exe switch {} read as {}, ignored (not supported yet)", typed, token);
}

void expect_ignored(const d2d::switches::Translated& translated, std::initializer_list<std::string_view> expected) {
    const std::vector<std::string_view> want(expected);
    assert(translated.ignored.size() == want.size());
    for (std::size_t index = 0; index < want.size(); ++index) assert(translated.ignored[index] == want[index]);
}

}  // namespace

int main() {
    {
        const auto translated = run({"d2d", "-nosave"});
        expect_argv(translated, {"d2d", "--no-save"});
        expect_ignored(translated, {});
    }
    {
        const auto translated = run({"d2d", "-NOSAVE"});
        expect_argv(translated, {"d2d", "--no-save"});
        expect_ignored(translated, {});
    }
    {
        const auto translated = run({"d2d", "-windowed", "-window", "-w"});
        expect_argv(translated, {"d2d"});
        expect_ignored(translated, {ignored_exact("-windowed"), ignored_exact("-window"), ignored_exact("-w")});
    }
    {
        const auto glued = run({"d2d", "-act2"});
        expect_argv(glued, {"d2d"});
        expect_ignored(glued, {ignored_as("-act2", "-act")});
        const auto spaced = run({"d2d", "-act", "2"});
        expect_argv(spaced, {"d2d"});
        expect_ignored(spaced, {ignored_exact("-act")});
    }
    {
        const auto translated = run({"d2d", "-name", "Bob"});
        expect_argv(translated, {"d2d"});
        expect_ignored(translated, {ignored_exact("-name")});
    }
    {
        const auto translated = run({"d2d", "-seed", "5"});
        expect_argv(translated, {"d2d"});
        expect_ignored(translated, {ignored_exact("-seed")});
    }
    {
        const auto translated = run({"d2d", "--seed", "5"});
        expect_argv(translated, {"d2d", "--seed", "5"});
        expect_ignored(translated, {});
    }
    {
        const auto translated = run({"d2d", "-zzz"});
        expect_argv(translated, {"d2d", "-zzz"});
        expect_ignored(translated, {});
    }
    {
        const auto translated = run({"d2d", "-ns", "-nosound"});
        expect_argv(translated, {"d2d"});
        expect_ignored(translated, {ignored_exact("-ns"), ignored_exact("-nosound")});
    }
    {
        const auto repeated = run({"d2d", "-nosave", "-nosave"});
        expect_argv(repeated, {"d2d", "--no-save", "--no-save"});
        expect_ignored(repeated, {});
        const auto mixed = run({"d2d", "-nosave", "--no-save"});
        expect_argv(mixed, {"d2d", "--no-save", "--no-save"});
        expect_ignored(mixed, {});
    }
    {
        const auto translated = run({"d2d", "-music"});
        expect_argv(translated, {"d2d"});
        expect_ignored(translated, {ignored_as("-music", "-m")});
    }
    std::cout << "test_switches: ok\n";
}
