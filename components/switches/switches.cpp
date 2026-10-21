// SPDX-License-Identifier: GPL-3.0-or-later
// game.exe 1.14d's switch names (docs/research/re/switches.md, the table
// at 0x00705040). This is our list of the token and whether it takes a
// value. It is not game.exe's struct: no offsets, no ini defaults.
#include "switches.hpp"

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
    {"nosave", Kind::boolean},
    // -seed stays ignored until Bret answers #62. DEBUG/SEED may not be
    // the map seed d2d's --seed sets.
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

std::string ignored_line(std::string_view typed, std::string_view token_name) {
    if (same_token(typed, token_name))
        return std::format("game.exe switch {} ignored (not supported yet)", typed);
    return std::format("game.exe switch {} read as -{}, ignored (not supported yet)", typed, token_name);
}

}  // namespace

Translated translate(std::span<const std::string_view> arguments) {
    Translated result;
    if (arguments.empty()) return result;
    result.argv.emplace_back(arguments[0]);
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
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
        const bool takes_value = token->kind != Kind::boolean && leftover.empty();
        const bool next_is_value = index + 1 < arguments.size()
            && (arguments[index + 1].empty() || arguments[index + 1][0] != '-');
        // The value is not passed on. This pass only accepts the switch.
        if (takes_value && next_is_value) ++index;
        // ponytail: -nosave is the only switch wired (d2d's --no-save). The
        // rest are accepted and logged; the next change maps the ones d2d
        // already does. CLI11 2.6.2 keeps the last copy of a flag, so a
        // repeated --no-save is legal and this may emit it more than once.
        if (token->name == "nosave") result.argv.emplace_back("--no-save");
        else result.ignored.push_back(ignored_line(argument, token->name));
    }
    return result;
}

}  // namespace d2d::switches
