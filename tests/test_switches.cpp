// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: switches CLI11::CLI11
// game.exe's one-dash switches, before CLI11 sees them.
#include <switches.hpp>
#include <CLI/CLI.hpp>

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
    CLI::App app;
    app.add_option("--seed");
    app.add_option("--data");
    app.add_option("--start-name");
    app.add_flag("--headless");
    app.add_flag("--no-save");
    return d2d::switches::translate(list, app);
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
        // The same required-value parsing that d2d uses: a leading dash
        // is legal in a path/name, even when it spells a legacy switch.
        CLI::App app;
        std::string data;
        app.add_option("--data", data);
        auto translated = run({"d2d", "--data", "-direct"});
        expect_argv(translated, {"d2d", "--data", "-direct"});
        expect_ignored(translated, {});
        std::vector<char*> arguments;
        for (auto& argument : translated.argv) arguments.push_back(argument.data());
        app.parse(static_cast<int>(arguments.size()), arguments.data());
        assert(data == "-direct");
    }
    {
        const auto translated = run({"d2d", "--start-name", "-NOSAVE", "-nosave"});
        expect_argv(translated, {"d2d", "--start-name", "-NOSAVE", "--no-save"});
        expect_ignored(translated, {});
        const auto inline_value = run({"d2d", "--data=-direct", "-nosave"});
        expect_argv(inline_value, {"d2d", "--data=-direct", "--no-save"});
        const auto empty_inline = run({"d2d", "--data=", "-direct", "-nosave"});
        expect_argv(empty_inline, {"d2d", "--data=", "-direct", "--no-save"});
        const auto flag = run({"d2d", "--headless", "-nosave"});
        expect_argv(flag, {"d2d", "--headless", "--no-save"});
        const auto unknown = run({"d2d", "--unknown", "-nosave"});
        expect_argv(unknown, {"d2d", "--unknown", "--no-save"});
        const auto end = run({"d2d", "--", "-nosave", "-direct"});
        expect_argv(end, {"d2d", "--", "-nosave", "-direct"});
        expect_ignored(end, {});
        const auto missing = run({"d2d", "--data"});
        expect_argv(missing, {"d2d", "--data"});
    }
    {
        // Required values also stay intact for CLI11's type/range errors.
        CLI::App app;
        int scale = 1;
        app.add_option("--scale", scale)->check(CLI::Range(1, 8));
        for (const auto value : {"-5", "-nosave", "9"}) {
            const std::vector<std::string_view> raw{"d2d", "--scale", value};
            auto translated = d2d::switches::translate(raw, app);
            expect_argv(translated, {"d2d", "--scale", value});
            expect_ignored(translated, {});
            std::vector<char*> arguments;
            for (auto& argument : translated.argv) arguments.push_back(argument.data());
            bool rejected = false;
            try { app.parse(static_cast<int>(arguments.size()), arguments.data()); }
            catch (const CLI::ParseError&) { rejected = true; }
            assert(rejected);
        }
    }
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
    {
        const auto negative = run({"d2d", "-seed", "-5"});
        expect_argv(negative, {"d2d"});
        expect_ignored(negative, {ignored_exact("-seed")});
        const auto kept = run({"d2d", "-seed", "-nosave"});
        expect_argv(kept, {"d2d", "--no-save"});
        expect_ignored(kept, {ignored_exact("-seed")});
        const auto act = run({"d2d", "-act", "-2"});
        expect_argv(act, {"d2d"});
        expect_ignored(act, {ignored_exact("-act")});
    }
    std::cout << "test_switches: ok\n";
}
