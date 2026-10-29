// SPDX-License-Identifier: GPL-3.0-or-later
// game.exe 1.14d's switch names (docs/research/re/switches.md, the table
// at 0x00705040). Each entry is the token. A wired token also names the
// d2d option. Kind only separates a value word from the next switch:
// a wired switch passes that word through, and an ignored switch drops
// it. This is not game.exe's struct: no offsets, no ini defaults.
#include "switches.hpp"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::switches {
namespace {

enum class Kind { boolean, integer, text };

struct Token {
    std::string_view name;
    Kind kind;
    // Set when this token is wired. Empty means log it and drop it.
    std::string_view long_option{};
};

constexpr Token kTokens[] = {
    {"crash", Kind::boolean},
    {"w", Kind::boolean}, {"window", Kind::boolean}, {"windowed", Kind::boolean},
    {"nofixaspect", Kind::boolean},
    {"3dfx", Kind::boolean}, {"opengl", Kind::boolean}, {"d3d", Kind::boolean},
    {"rave", Kind::boolean}, {"per", Kind::boolean},
    {"lq", Kind::boolean},
    {"gamma", Kind::integer},
    {"vsync", Kind::boolean},
    {"fr", Kind::integer},
    {"s", Kind::text},
    {"gametype", Kind::integer},
    {"arena", Kind::integer},
    {"joinid", Kind::integer},
    {"gamename", Kind::text},
    {"bn", Kind::text},
    {"mcpip", Kind::text},
    {"ama", Kind::boolean}, {"pal", Kind::boolean}, {"sor", Kind::boolean},
    {"nec", Kind::boolean}, {"bar", Kind::boolean},
    {"i", Kind::boolean},
    {"name", Kind::text},
    {"realm", Kind::text},
    {"ctemp", Kind::integer},
    {"nm", Kind::boolean},
    {"m", Kind::integer},
    {"minfo", Kind::boolean},
    {"md", Kind::integer},
    {"rare", Kind::boolean}, {"unique", Kind::boolean},
    {"act", Kind::integer},
    {"log", Kind::boolean}, {"msglog", Kind::boolean}, {"safe", Kind::boolean},
    {"nosave", Kind::boolean, "--no-save"},
    // Ignored on purpose. switches.md: DEBUG/SEED is not traced into the
    // generator, so this is not d2d's --seed.
    {"seed", Kind::integer},
    {"nopk", Kind::boolean},
    {"cheats", Kind::boolean}, {"teen", Kind::boolean},
    {"ns", Kind::boolean},
    {"npl", Kind::boolean},
    {"direct", Kind::boolean},
    {"lem", Kind::boolean},
    {"questall", Kind::boolean},
    {"comint", Kind::integer},
    {"skiptobnet", Kind::boolean},
    {"openc", Kind::boolean},
    {"nocompress", Kind::boolean},
    {"txt", Kind::boolean},
    {"build", Kind::boolean},
    {"nosound", Kind::boolean},
    {"sndbkg", Kind::boolean},
};

static_assert(sizeof(kTokens) / sizeof(kTokens[0]) == 58);

std::string lower_ascii(std::string_view word) {
    std::string lowered(word);
    for (char& character : lowered) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    return lowered;
}

// The argument skips a leading space, tab, newline, or colon (switches.md).
std::string_view trim_argument(std::string_view value) {
    while (!value.empty()) {
        const char character = value.front();
        if (character != ' ' && character != '\t' && character != '\n' && character != ':') break;
        value.remove_prefix(1);
    }
    return value;
}

// Longest token that is a prefix. A one-letter token matches that way too,
// so -music is -m, the same as game.exe. The warning names what was typed.
const Token* match_token(std::string_view word, std::string_view& leftover) {
    const std::string lowered = lower_ascii(word);
    const Token* found = nullptr;
    for (const Token& token : kTokens) {
        if (!lowered.starts_with(token.name)) continue;
        if (found && found->name.size() >= token.name.size()) continue;
        found = &token;
    }
    if (!found) return nullptr;
    leftover = trim_argument(word.substr(found->name.size()));
    return found;
}

bool same_token(std::string_view typed, std::string_view token_name) {
    if (typed.empty() || typed[0] != '-') return false;
    typed.remove_prefix(1);
    if (typed.size() != token_name.size()) return false;
    for (std::size_t index = 0; index < typed.size(); ++index) {
        char character = typed[index];
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
        if (character != token_name[index]) return false;
    }
    return true;
}

// A negative integer value, such as -5. Not a switch: -nosave and --seed stay.
bool negative_integer(std::string_view word) {
    if (word.size() < 2 || word[0] != '-' || word[1] == '-') return false;
    for (std::size_t index = 1; index < word.size(); ++index)
        if (word[index] < '0' || word[index] > '9') return false;
    return true;
}

// True when `next` is this switch's value rather than another switch.
// A glued tail is already the value, so the next word is not. An integer
// value may be negative. The word itself is not read.
bool following_is_value(const Token& token, std::string_view leftover, std::string_view next) {
    if (token.kind == Kind::boolean || !leftover.empty()) return false;
    if (next.empty() || next[0] != '-') return true;
    return token.kind == Kind::integer && negative_integer(next);
}

std::string ignored_line(std::string_view typed, std::string_view token_name) {
    if (same_token(typed, token_name))
        return std::format("game.exe switch {} ignored (not supported yet)", typed);
    return std::format("game.exe switch {} read as -{}, ignored (not supported yet)", typed, token_name);
}

}  // namespace

Translated translate(std::span<const std::string_view> arguments, const CLI::App& app) {
    Translated result;
    if (arguments.empty()) return result;
    result.argv.emplace_back(arguments[0]);
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        // CLI11's end-of-options marker also ends legacy translation.
        if (argument == "--") {
            for (; index < arguments.size(); ++index) result.argv.emplace_back(arguments[index]);
            break;
        }
        if (argument.starts_with("--")) {
            result.argv.emplace_back(argument);
            const auto equals = argument.find('=');
            const auto* option = app.get_option_no_throw(std::string(argument.substr(0, equals)));
            // All current d2d value options are scalar. Use the parser's
            // registered arity, not a second list of names or value types.
            // CLI11 also takes the next word for an empty --option=.
            if (option && (equals == std::string_view::npos || equals + 1 == argument.size())) {
                const int required = std::min(option->get_type_size_min(), option->get_items_expected_min());
                for (int count = 0; count < required && index + 1 < arguments.size(); ++count)
                    result.argv.emplace_back(arguments[++index]);
            }
            continue;
        }
        // Two dashes are d2d's own. Anything else that isn't one dash plus
        // a word is left for CLI11, including an unknown -zzz.
        if (argument.size() < 2 || argument[0] != '-' || argument[1] == '-') {
            result.argv.emplace_back(argument);
            continue;
        }
        std::string_view leftover;
        const Token* token = match_token(argument.substr(1), leftover);
        if (!token) {
            result.argv.emplace_back(argument);
            continue;
        }
        const bool next_is_value = index + 1 < arguments.size()
            && following_is_value(*token, leftover, arguments[index + 1]);
        // -nosave is the only switch wired (d2d's --no-save); switches.md's
        // d2d column says where each other one stands. A wired name is rewritten and its value is passed through for
        // CLI11, which checks the type. -seed stays ignored until
        // DEBUG/SEED is traced (switches.md). Anything else is logged and
        // dropped, value included, so an old command line still starts.
        // CLI11 2.6.2 keeps the last copy of a flag, so a repeated
        // --no-save is legal and this may emit it more than once.
        if (!token->long_option.empty()) {
            result.argv.emplace_back(token->long_option);
            if (token->kind == Kind::boolean) continue;
            if (!leftover.empty()) result.argv.emplace_back(leftover);
            else if (next_is_value) {
                result.argv.emplace_back(arguments[index + 1]);
                ++index;
            }
            continue;
        }
        if (next_is_value) ++index;
        result.ignored.push_back(ignored_line(argument, token->name));
    }
    return result;
}

}  // namespace d2d::switches
