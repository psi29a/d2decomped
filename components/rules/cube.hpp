// SPDX-License-Identifier: GPL-3.0-or-later
// The Horadric Cube's transmute — game.exe 1.14d (docs/research/re/cube.md).
// cubemain.txt compiled as game.exe compiles it (FUN_00669130: inputs
// FUN_00668600, outputs FUN_00668a90), the first enabled row whose inputs
// the Cube holds (FUN_005665f0 -> FUN_005658c0 / FUN_00565010), and what it
// makes (FUN_00565ab0).
#pragma once

#include "drops.hpp"
#include "replenish.hpp"
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
std::string trim_quotes(std::string_view text);
std::vector<std::string> split(const std::string& text);
bool same_letters(std::string_view first, std::string_view second);
int named_row(const std::vector<Special>& rows, std::string_view name);
int quality_word(std::string_view word);
}  // namespace cube_detail

// FUN_00668600: "any", a type code (tried first, four letters at most),
// an item code, else a unique's or a set item's name; then qty=, a quality
// word, nos / sock / noe / eth / upg / bas / exc / eli / nru. An unknown
// word ends it.
CubeInput compile_cube_input(const Tables& tables, std::string_view text);

// FUN_00668a90: the portals, usetype / useitem, an item code (tried
// first), a type, a unique's or set item's name; then qty=, pre= / suf=
// (up to three each), a quality word, eth, sock=, mod, uns, rem, reg
// (usetype), exc, eli, rep, rch. A set item's name takes nothing after it.
CubeOutput compile_cube_output(const Tables& tables, std::string_view text);

// The item types an item is: its type, else its type2 (FUN_00629a90).
bool item_of_type(const Tables& tables, const std::string& code, const std::string& want);
// The sockets an item can hold (FUN_0062bc20): its type's MaxSock by
// ilvl (1..25, 26..40, 41..), capped by the base's gemsockets.
int max_sockets(const Tables& tables, const d2d::d2s::Item& item);

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
std::optional<CubeMatch> cube_match(const Tables& tables, const CubeRecipe& recipe, const std::vector<const d2d::d2s::Item*>& cube, int difficulty, bool expansion);

// The first row that takes the Cube's contents (FUN_005665f0): enabled,
// not ladder-only (d2d's games aren't), the expansion's in an expansion
// game, its difficulty, class and input count, then its inputs. Rows with
// a date or stat test (op 1..14) don't take: none are enabled in 1.14d.
int cube_recipe(const Tables& tables, const std::vector<CubeRecipe>& recipes, const std::vector<const d2d::d2s::Item*>& cube, int difficulty,
                bool expansion, int cls, CubeMatch& match);

// A random item of `type` up to `ilvl` (FUN_00565930): from a random Items
// row on the game seed, one lap of spawnable rows of the type (version
// allowed), up to 256, then one of them on the game seed.
std::string cube_type_item(const Tables& tables, const std::string& type, int ilvl, bool expansion, Rng& game_seed);

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
// suf=, crafted (made rare, then called crafted), mod's new base stats
// and the transmute's sound aren't game.exe's yet.
struct CubeMade { std::vector<d2d::d2s::Item> items; bool made = false; };
CubeMade cube_make(const Tables& tables, const CubeRecipe& recipe, const CubeMatch& match, const std::vector<const d2d::d2s::Item*>& cube, int clvl,
                   bool expansion, int difficulty, Rng& game_seed, std::vector<bool>* found_uniques);

}  // namespace d2d::rules
