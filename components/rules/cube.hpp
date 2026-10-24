// SPDX-License-Identifier: GPL-3.0-or-later
// The Horadric Cube's transmute — game.exe 1.14d (docs/research/re/cube.md).
// cubemain.txt compiled as game.exe compiles it (FUN_00669130: inputs
// FUN_00668600, outputs FUN_00668a90), the first enabled row whose inputs
// the Cube holds (FUN_005665f0 -> FUN_005658c0 / FUN_00565010), and what it
// makes (FUN_00565ab0).
#pragma once

#include "drops.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::rules {

// An input (8 bytes at row +0x14): flags, the item (its code; empty "any")
// or type code, a named unique / set (row + 1), a quality, a count.
namespace cube_in {
inline constexpr int kItem = 1, kType = 2, kNoSocket = 4, kSocketed = 8, kEthereal = 0x10, kNotEthereal = 0x20, kNamed = 0x40,
                     kUpgrade = 0x80, kBase = 0x100, kExceptional = 0x200, kElite = 0x400, kNoRuneword = 0x800;
}
// An output (0x54 bytes from row +0x4c): flags, its kind, code / type,
// named row + 1, quality, a count (or sockets), lvl / plvl / ilvl, forced
// prefixes / suffixes, five property mods.
namespace cube_out {
inline constexpr int kMod = 1, kSock = 2, kEthereal = 4, kNamed = 8, kUnsocket = 0x10, kRemove = 0x20, kRegular = 0x40, kExceptional = 0x80,
                     kElite = 0x100, kRepair = 0x200, kRecharge = 0x400;
inline constexpr int kCowPortal = 1, kPandemonium = 2, kFinale = 3, kCode = 0xfc, kType = 0xfd, kUseItem = 0xfe, kUseType = 0xff;
}
struct CubeInput { int flags = 0; std::string code; int named = 0, quality = 0, qty = 0; };
struct CubeMod { Mod mod; int chance = 0; };
struct CubeOutput {
    int flags = 0, kind = 0;
    std::string code;
    int named = 0, quality = 0, qty = 0, lvl = 0, plvl = 0, ilvl = 0;
    std::vector<int> prefixes, suffixes;
    std::vector<CubeMod> mods;
};
struct CubeRecipe {
    std::string description;
    bool enabled = false, ladder = false;
    int min_diff = 0, op = 0, param = 0, value = 0, cls = -1, numinputs = 0, version = 0;
    std::array<CubeInput, 7> inputs;
    std::array<CubeOutput, 3> outputs;
};

namespace cube_detail {
inline std::string trim_quotes(std::string_view text) {
    std::string out;
    for (const char letter : text) if (letter != '"') out += letter;
    return out;
}
inline std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> parts;
    std::string part;
    for (const char letter : text) {
        if (letter == ',') { parts.push_back(part); part.clear(); }
        else part += letter;
    }
    parts.push_back(std::move(part));
    return parts;
}
inline bool same_letters(std::string_view first, std::string_view second) {
    return first.size() == second.size() && std::ranges::equal(first, second, [](char left, char right) { return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right)); });
}
inline int named_row(const std::vector<Special>& rows, std::string_view name) {
    for (std::size_t row = 0; row < rows.size(); ++row) if (same_letters(rows[row].index, name)) return int(row);
    return -1;
}
inline int quality_word(std::string_view word) {
    static constexpr std::array<std::string_view, 9> kWords{ "low", "nor", "hiq", "mag", "set", "rar", "uni", "crf", "tmp" };
    const auto found = std::ranges::find(kWords, word);
    return found == kWords.end() ? 0 : int(found - kWords.begin()) + 1;
}
}  // namespace cube_detail

// FUN_00668600: "any", a type code (tried first, four letters at most),
// an item code, else a unique's or a set item's name; then qty=, a quality
// word, nos / sock / noe / eth / upg / bas / exc / eli / nru. An unknown
// word ends it.
inline CubeInput compile_cube_input(const Tables& tables, std::string_view text) {
    using namespace cube_in;
    CubeInput input;
    const auto parts = cube_detail::split(cube_detail::trim_quotes(text));
    const std::string& head = parts[0];
    if (head.empty()) return input;
    if (head.size() < 5 && cube_detail::same_letters(head, "any")) input.flags |= kItem;
    else if (head.size() < 5 && tables.types.contains(head)) { input.flags |= kType; input.code = head; }
    else if (head.size() < 5 && tables.item_base.contains(head)) { input.flags |= kItem; input.code = head; }
    else if (const int unique = cube_detail::named_row(tables.uniques, head); unique >= 0) {
        input.flags |= kItem | kNamed; input.quality = 7; input.named = unique + 1; input.code = tables.uniques[std::size_t(unique)].code;
    } else if (const int set = cube_detail::named_row(tables.sets, head); set >= 0) {
        input.flags |= kItem | kNamed; input.quality = 5; input.named = set + 1; input.code = tables.sets[std::size_t(set)].code;
    } else return {};
    for (std::size_t index = 1; index < parts.size(); ++index) {
        const auto equals = parts[index].find('=');
        const std::string word = parts[index].substr(0, equals);
        if (word == "qty") { input.qty = std::atoi(parts[index].substr(equals + 1).c_str()); continue; }
        if (const int quality = cube_detail::quality_word(word)) { input.quality = quality; continue; }
        static constexpr std::array<std::pair<std::string_view, int>, 10> kWords{ { { "nos", kNoSocket }, { "sock", kSocketed }, { "noe", kNotEthereal },
            { "eth", kEthereal }, { "upg", kUpgrade }, { "bas", kBase }, { "exc", kExceptional }, { "eli", kElite }, { "nru", kNoRuneword } } };
        const auto found = std::ranges::find(kWords, std::string_view(word), &std::pair<std::string_view, int>::first);
        if (found == kWords.end() || found->first.empty()) break;
        input.flags |= found->second;
    }
    return input;
}

// FUN_00668a90: the portals, usetype / useitem, an item code (tried
// first), a type, a unique's or set item's name; then qty=, pre= / suf=
// (up to three each), a quality word, eth, sock=, mod, uns, rem, reg
// (usetype), exc, eli, rep, rch. A set item's name takes nothing after it.
inline CubeOutput compile_cube_output(const Tables& tables, std::string_view text) {
    using namespace cube_out;
    CubeOutput output;
    const auto parts = cube_detail::split(cube_detail::trim_quotes(text));
    const std::string& head = parts[0];
    if (head.empty()) return output;
    if (cube_detail::same_letters(head, "Cow Portal")) output.kind = kCowPortal;
    else if (cube_detail::same_letters(head, "Pandemonium Portal")) output.kind = kPandemonium;
    else if (cube_detail::same_letters(head, "Pandemonium Finale Portal")) output.kind = kFinale;
    else if (head == "usetype") output.kind = kUseType;
    else if (head == "useitem") output.kind = kUseItem;
    else if (head.size() < 5 && tables.item_base.contains(head)) { output.kind = kCode; output.code = head; }
    else if (head.size() < 5 && tables.types.contains(head)) { output.kind = kType; output.code = head; }
    else if (const int unique = cube_detail::named_row(tables.uniques, head); unique >= 0) {
        output.flags |= kNamed; output.kind = kCode; output.quality = 7; output.named = unique + 1; output.code = tables.uniques[std::size_t(unique)].code;
        output.ilvl = tables.uniques[std::size_t(unique)].level;
    } else {
        if (const int set = cube_detail::named_row(tables.sets, head); set >= 0) {
            output.flags |= kNamed; output.kind = kCode; output.quality = 5; output.named = set + 1; output.code = tables.sets[std::size_t(set)].code;
            output.ilvl = tables.sets[std::size_t(set)].level;
        }
        return output;
    }
    for (std::size_t index = 1; index < parts.size(); ++index) {
        const auto equals = parts[index].find('=');
        const std::string word = parts[index].substr(0, equals);
        const int value = equals == std::string::npos ? 0 : std::atoi(parts[index].substr(equals + 1).c_str());
        if (word == "qty") { output.qty = value; continue; }
        if (word == "pre") { if (output.prefixes.size() < 3) output.prefixes.push_back(value); continue; }
        if (word == "suf") { if (output.suffixes.size() < 3) output.suffixes.push_back(value); continue; }
        if (const int quality = cube_detail::quality_word(word)) { output.quality = quality; continue; }
        if (word == "sock") { output.flags |= kSock; output.qty = value; continue; }
        if (word == "reg") { output.flags |= kRegular; output.kind = kUseType; continue; }
        static constexpr std::array<std::pair<std::string_view, int>, 8> kWords{ { { "eth", kEthereal }, { "mod", kMod }, { "uns", kUnsocket }, { "rem", kRemove },
            { "exc", kExceptional }, { "eli", kElite }, { "rep", kRepair }, { "rch", kRecharge } } };
        const auto found = std::ranges::find(kWords, std::string_view(word), &std::pair<std::string_view, int>::first);
        if (found == kWords.end()) break;
        output.flags |= found->second;
    }
    return output;
}

// The item types an item is: its type, else its type2 (FUN_00629a90).
inline bool item_of_type(const Tables& tables, const std::string& code, const std::string& want) {
    const auto info = tables.item_info.find(code);
    const auto base = tables.item_base.find(code);
    return (info != tables.item_info.end() && type_is(tables, info->second.type, want))
           || (base != tables.item_base.end() && !base->second.type2.empty() && type_is(tables, base->second.type2, want));
}
// The sockets an item can hold (FUN_0062bc20): its type's MaxSock by
// ilvl (1..25, 26..40, 41..), capped by the base's gemsockets.
inline int max_sockets(const Tables& tables, const d2d::d2s::Item& item) {
    const auto info = tables.item_info.find(item.code);
    const auto base = tables.item_base.find(item.code);
    if (info == tables.item_info.end() || base == tables.item_base.end()) return 0;
    const auto type = tables.types.find(info->second.type);
    if (type == tables.types.end()) return 0;
    return std::min(base->second.gem_sockets, type->second.max_sock[std::size_t(item.ilvl <= 25 ? 0 : item.ilvl <= 40 ? 1 : 2)]);
}

// Which items answer a row (FUN_005658c0 over the seven inputs, each
// FUN_00565010), and what the first input gives each output (param_5:
// the item to use or modify, the base for usetype, its ilvl).
struct CubeMatch {
    std::array<int, 3> source{ -1, -1, -1 };   // the Cube item (index) an output uses / modifies
    std::array<std::string, 3> base;           // usetype's base code
    std::array<int, 3> ilvl{};
};
// `cube`: the Cube's items in the player's list order. `difficulty`:
// the game's, for op 28 (a quest item from that difficulty on).
inline std::optional<CubeMatch> cube_match(const Tables& tables, const CubeRecipe& recipe, const std::vector<const d2d::d2s::Item*>& cube, int difficulty, bool expansion) {
    using namespace cube_in;
    CubeMatch match;
    std::vector<bool> used(cube.size(), false);
    for (std::size_t slot = 0; slot < recipe.inputs.size(); ++slot) {
        const auto& input = recipe.inputs[slot];
        if ((input.flags & 3) == 0) continue;
        const int need = std::max(input.qty, 1);
        bool no_stack = true;
        int count = 0;
        for (std::size_t index = 0; index < cube.size(); ++index) {
            const auto& item = *cube[index];
            const auto found = tables.item_base.find(item.code);
            if (found == tables.item_base.end()) continue;
            const auto& base = found->second;
            if (input.flags & kItem) {
                if (input.code.empty()) {
                } else if (input.flags & kUpgrade) {               // the input's tier or one under the item's
                    const bool ok = item.code == base.normcode ? input.code == base.normcode
                                    : item.code == base.ubercode ? (input.code == base.normcode || input.code == base.ubercode)
                                    : item.code == base.ultracode && (input.code == base.normcode || input.code == base.ubercode || input.code == base.ultracode);
                    if (!ok) continue;
                } else if (item.code != input.code) continue;
            } else if (!item_of_type(tables, item.code, input.code)) continue;
            if (input.quality && item.quality != input.quality) continue;
            if ((input.flags & kNamed) && (item.quality == 5 ? item.set_id : item.unique_id) != input.named - 1) continue;
            if ((input.flags & kNoSocket) && item.socketed) continue;
            if (!(input.flags & kNoSocket) && (input.flags & kSocketed) && !item.socketed) continue;
            if ((input.flags & kNotEthereal) && item.ethereal) continue;
            if (!(input.flags & kNotEthereal) && (input.flags & kEthereal) && !item.ethereal) continue;
            if ((input.flags & kBase) ? item.code != base.normcode : (input.flags & kExceptional) ? item.code != base.ubercode
                : (input.flags & kElite) && item.code != base.ultracode) continue;
            if ((input.flags & kNoRuneword) && item.runeword) continue;
            // N-player gap: op 28's quest item difficulty (stat 0x164) is taken as met.
            (void)difficulty;
            const auto& first_out = recipe.outputs[0];
            if (slot == 0 && (first_out.kind == cube_out::kUseType || first_out.kind == cube_out::kUseItem) && (first_out.flags & cube_out::kSock)
                && max_sockets(tables, item) == 0) continue;
            if (used[index]) continue;
            if (slot == 0) {
                // The first input feeds every output, by output a's flags (as game.exe reads them).
                for (std::size_t out = 0; out < 3; ++out) {
                    match.ilvl[out] = item.ilvl;
                    if (first_out.flags & cube_out::kMod) match.source[out] = int(index);
                    if (first_out.kind != cube_out::kUseType && first_out.kind != cube_out::kUseItem) continue;
                    match.source[out] = int(index);
                    std::string upgraded = first_out.kind == cube_out::kUseType ? item.code : std::string{};
                    const std::string& tier = (first_out.flags & cube_out::kExceptional) ? base.ubercode : (first_out.flags & cube_out::kElite) ? base.ultracode : upgraded;
                    if (const auto tier_base = tables.item_base.find(tier); !tier.empty() && tier_base != tables.item_base.end()
                        && (expansion || tier_base->second.version < 100)) upgraded = tier;
                    match.base[out] = std::move(upgraded);
                }
            }
            used[index] = true;
            ++count;
            if (base.stackable) no_stack = false;
        }
        if (no_stack ? count != need : count < need) return std::nullopt;
    }
    return match;
}

// The first row that takes the Cube's contents (FUN_005665f0): enabled,
// not ladder-only (d2d's games aren't), the expansion's in an expansion
// game, its difficulty, class and input count, then its inputs. Rows with
// a date or stat test (op 1..14) don't take: none are enabled in 1.14d.
inline int cube_recipe(const Tables& tables, const std::vector<CubeRecipe>& recipes, const std::vector<const d2d::d2s::Item*>& cube, int difficulty,
                       bool expansion, int cls, CubeMatch& match) {
    if (cube.empty()) return -1;
    for (std::size_t row = 0; row < recipes.size(); ++row) {
        const auto& recipe = recipes[row];
        if (!recipe.enabled || recipe.ladder || (!expansion && recipe.version >= 100) || recipe.min_diff > difficulty) continue;
        if ((recipe.cls >= 0 && recipe.cls != cls) || recipe.numinputs != int(cube.size()) || (recipe.op >= 1 && recipe.op <= 14)) continue;
        if (auto found = cube_match(tables, recipe, cube, difficulty, expansion)) { match = *found; return int(row); }
    }
    return -1;
}

// A random item of `type` up to `ilvl` (FUN_00565930): from a random Items
// row on the game seed, one lap of spawnable rows of the type (version
// allowed), up to 256, then one of them on the game seed.
inline std::string cube_type_item(const Tables& tables, const std::string& type, int ilvl, bool expansion, Rng& game_seed) {
    const int count = int(tables.item_order.size());
    if (count == 0) return {};
    const int start = int(game_seed(std::uint32_t(count)));
    const int stop = start - 1 < 0 ? count - 1 : start - 1;
    std::vector<std::string> found;
    for (int row = start; row != stop && found.size() < 256; row = row + 1 >= count ? 0 : row + 1) {
        const auto& code = tables.item_order[std::size_t(row)];
        const auto& base = tables.item_base.at(code);
        if (item_of_type(tables, code, type) && base.spawnable && (base.version < 100 || expansion) && base.level <= ilvl) found.push_back(code);
    }
    if (found.empty()) return {};
    return found[game_seed(std::uint32_t(found.size()))];
}

// What a row makes (FUN_00565ab0), each output: its item level (lvl, else
// plvl% of clvl plus ilvl% of the input's, 1..99); a new item (by code,
// type, usetype's base, quality and named row; sock / eth) off the game
// seed as a drop's (FUN_00558d90), or the input itself (useitem / mod:
// FUN_0055a2a0, its code changed for mod); its five mods, each on its
// chance off the item's own seed (FUN_00660240); rep / rch; sock= opens
// sockets (magic up to 3; set, rare, unique, crafted 1); qty= a stack.
// `uns` / `rem` items come back out of their sockets (rem only: as items).
// The portals (kinds 1..3) are World::transmute's (rules::CainQuest::cow_portal_opens).
// ponytail (cube.md "Gaps"): forced pre= /
// suf=, crafted (made rare, then called crafted), mod's new base stats,
// rch's charges and the transmute's sound aren't game.exe's yet.
struct CubeMade { std::vector<d2d::d2s::Item> items; bool made = false; };
inline CubeMade cube_make(const Tables& tables, const CubeRecipe& recipe, const CubeMatch& match, const std::vector<const d2d::d2s::Item*>& cube, int clvl,
                          bool expansion, int difficulty, Rng& game_seed, std::vector<bool>* found_uniques) {
    using namespace cube_out;
    CubeMade out;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const auto& output = recipe.outputs[slot];
        const d2d::d2s::Item* source = match.source[slot] >= 0 ? cube[std::size_t(match.source[slot])] : nullptr;
        int level = output.lvl;
        if (!level) level = (output.plvl ? clvl * output.plvl / 100 : 0) + (output.ilvl ? match.ilvl[slot] * output.ilvl / 100 : 0);
        level = std::clamp(level, 1, 99);
        if (output.kind >= kCowPortal && output.kind <= kFinale) continue;    // World::transmute's
        std::optional<d2d::d2s::Item> made;
        Rng own{ 0 };
        auto code_of = [&]() -> std::string {
            if (output.kind == kUseType) return match.base[slot];
            if (output.kind == kCode) return output.code;
            if (output.kind == kType) return cube_type_item(tables, output.code, level, expansion, game_seed);
            return source ? source->code : std::string{};
        };
        if (!(output.flags & kMod) && output.kind == kUseItem) {
            if (source) made = *source;
        } else if (!(output.flags & kMod)) {
            if (output.kind != kUseType && output.kind != kCode && output.kind != kType) continue;
            const std::string code = code_of();
            if (code.empty()) continue;
            int quality = output.quality;
            int named = output.named;
            if ((output.flags & kRegular) && source) { quality = source->quality; named = (source->quality == 5 ? source->set_id : source->unique_id) + 1; }
            Rng unit_seed{ game_seed.next() };
            own = Rng{ game_seed.next() };
            if (!quality) quality = stand_quality(tables, code, level, own);
            auto item = generate_item(tables, code, level, quality == 8 ? 6 : quality, own, &unit_seed, found_uniques, false, difficulty);
            if (quality == 8) item.quality = 8;
            if (named && quality == 7) item.unique_id = named - 1;
            if (named && quality == 5) item.set_id = named - 1;
            if (output.flags & kEthereal) item.ethereal = true;
            made = std::move(item);
        } else if (source) {
            auto item = *source;
            if (output.kind != kUseItem) if (const std::string code = code_of(); !code.empty()) item.code = code;
            made = std::move(item);
        }
        if (!made) continue;
        auto& item = *made;
        if (output.flags & (kUnsocket | kRemove)) {
            if (output.flags & kRemove)
                for (auto gem : item.socketed_items) { gem.location = d2d::d2s::item_location::kStored; out.items.push_back(std::move(gem)); }
            item.socketed_items.clear();
        }
        for (const auto& [mod, chance] : output.mods) {
            if (mod.code.empty()) continue;
            if (chance > 0 && chance < 100 && int(own(100)) > chance) continue;
            apply_mod(tables, mod, item.props, own);
        }
        const auto base = tables.item_base.find(item.code);
        const bool stackable = base != tables.item_base.end() && base->second.stackable;
        if (output.flags & kRepair) {
            if (stackable && output.qty) item.quantity = std::min(output.qty, base->second.max_stack);
            if (item.max_durability > 0) item.durability = item.max_durability;
        }
        if (!(output.flags & kSock)) {
            if (output.qty && stackable) item.quantity = std::min(output.qty, base->second.max_stack);
        } else if (output.qty && item.sockets == 0 && !item.socketed) {
            int sockets = std::min(output.qty, max_sockets(tables, item));
            if ((item.quality == 4 || item.quality == 9) && sockets >= 4) sockets = 3;
            if (item.quality >= 5 && item.quality <= 8 && sockets > 1) sockets = 1;
            if (sockets > 0) { item.socketed = true; item.sockets = sockets; }
        }
        out.items.push_back(std::move(item));
        out.made = true;
    }
    return out;
}

}  // namespace d2d::rules
