// SPDX-License-Identifier: GPL-3.0-or-later
// Item text: an item's hover lines in D2's quality colours, its property
// lines, D2's string formatting. The World names ground items with them;
// the client draws them (items.hpp).
#pragma once

#include "character.hpp"
#include "gamedata.hpp"

#include <d2s_items.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::game {

// An item's hover text, top line first, in D2's quality colours. Name IDs
// resolve as verified against 19 real saves (test data): unique/set ID =
// UniqueItems/SetItems row without "Expansion" separators; magic
// prefix/suffix ID = MagicPrefix/MagicSuffix raw data row (row 0 is the
// blank "none"); rare names = RarePrefix row (id - 156) + RareSuffix row
// (id - 1); runeword ID = rank of its RunewordN + 27.
// A line can change colour once, at `split` (game.exe's mid-line "\xffc3"
// before a modified number): text[split..] is drawn in `tail`.
struct TextLine {
    std::string text;
    std::array<std::uint8_t, 3> rgb;
    std::size_t split = std::string::npos;
    std::array<std::uint8_t, 3> tail{};
};

// D2's printf subset in its strings: %d, %+d, %s, %%. Args in order.
std::string d2_format(std::string_view format, std::initializer_list<std::variant<std::int64_t, std::string>> args);

// An item's property list as the tooltip's text lines, by ItemStatCost
// descfunc (the column semantics the game's item-description code uses),
// highest descpriority first. dgrp groups (all resistances, all
// attributes) collapse into one line when every member is present with
// the same value; min/max damage pairs become "Adds X-Y ..." and 17/18
// "+X% Enhanced Damage", as the game hard-codes them.
// No descfunc 17/18 (time-of-day) or 22 (monster types): no 1.14d item
// rolls them (of Dawn / of Sunlight aren't spawnable; att-mon% / dmg-mon%
// are on nothing).
std::vector<std::string> prop_lines(const GameData& game_data, std::vector<d2d::d2s::ItemProp> props, int clvl);
constexpr std::array<std::uint8_t, 3> kTxtWhite{ 255, 255, 255 }, kTxtBlue{ 105, 105, 255 },
    kTxtGreen{ 0, 255, 0 }, kTxtGold{ 199, 179, 119 }, kTxtYellow{ 255, 255, 100 },
    kTxtOrange{ 255, 168, 0 }, kTxtGrey{ 105, 105, 105 }, kTxtRed{ 255, 77, 77 };

// A store's price on an item's hover text ("Cost: " / "Sell value: ",
// FUN_004b2ad0): FUN_0048dd90 adds it after the name (FUN_0048cea0 at
// 0x48ec27), and the belt's hover after its lines (FUN_00497a40 at
// 0x497bf1). The string is drawn bottom-up, so it shows on top, and it
// sets no colour of its own, so it keeps the one the top line ends in.
void add_price_line(std::vector<TextLine>& lines, std::string text);

// game.exe's attack speed bands (DAT_00721f10, FUN_004861d0): row speed
// 10..27 (frames, below), column by class and bow/crossbow (0x722078);
// 1 Very Fast .. 5 Very Slow. Under 10 is 1, 28 and over 5.
inline constexpr std::array<std::uint8_t, 90> kSpeedBand{
    1, 1, 1, 1, 1,  1, 1, 1, 1, 1,  1, 1, 1, 1, 1,  1, 1, 2, 1, 1,  2, 1, 2, 2, 1,  2, 1, 2, 2, 2,
    2, 2, 3, 2, 2,  3, 2, 3, 3, 2,  3, 2, 3, 3, 3,  3, 2, 4, 3, 3,  4, 3, 4, 4, 3,  4, 3, 4, 4, 4,
    4, 3, 5, 4, 4,  5, 4, 5, 5, 4,  5, 4, 5, 5, 5,  5, 4, 5, 5, 5,  5, 5, 5, 5, 5,  5, 5, 5, 5, 5 };
inline constexpr std::uint8_t kSpeedColumn[7][2] = { { 0, 2 }, { 1, 4 }, { 1, 4 }, { 0, 3 }, { 0, 3 }, { 1, 4 }, { 0, 3 } };

// A weapon's attack length in frames as FUN_0062a710 gives it: class `cls`'s
// A1 frames (AnimData, the weapon's class) << 8 over the animation speed x
// (100 + the item's IAS, stat 93 with its sockets', - its WSM) / 100; 45
// without the animation. The weapon-speed line and Whirlwind's hit gap
// (FUN_005d9320) read it.
int weapon_attack_frames(const GameData& game_data, const d2d::d2s::Item& item, int cls);

// A quest consumable its quest doesn't let the player use now (FUN_00483f80,
// 0x483f80, on the client's quest words, FUN_004b32d0): the Book of Skill
// (ass) unless Radament's word (9) has bit 5, the Potion of Life (xyz)
// unless the Golden Bird's (0x14) has bit 5, the Scroll of Resistance (tr2)
// unless the Prison of Ice's (0x25) has bit 8 and not bit 7. The grid tints
// it red (FUN_00483ff0). Any other code: false.
bool quest_item_held_back(std::string_view code, const d2d::d2s::Header& header);

// An item's required level (FUN_0062b5b0): its affixes' / set item's /
// unique's levelreq (crafted: the highest affix + 10 + 3 per affix, at
// most 98; magic and rare also their class automod's, item data +0x36,
// AutoMagic row class_affix - 1: 0062b61b, 0062b7c0), at least the base's
// and each socketed item's, plus
// item_levelreq (stat 92); never below 0. An affix of the wearer's class
// (`cls`, a player's; -1 none) takes its classlevelreq. A +skill (stat 107)
// needs that skill's reqlevel (Skills +0x174), an other class's skill
// (stat 97, item_nonclassskill) its reqlevel + 6 unless it's the wearer's
// class's.
int required_level(const GameData& game_data, const d2d::d2s::Item& item, int cls = -1);

// An item's drop sound (FUN_004c1d60, put down) or use sound (FUN_004c17f0,
// picked up): misc / armor / weapons.txt's, then a unique's or set item's
// own row instead. A row counts only when its dropsound is set, for the
// usesound too.
std::string item_sound(const GameData& game_data, const d2d::d2s::Item& item, bool use);
// The flippy's drop sound and the frame it plays at (FUN_004c16d0): the
// same rows, each row's dropsfxframe where it's set (12 by default).
std::pair<std::string, int> flippy_sound(const GameData& game_data, const d2d::d2s::Item& item);

// An item's hover text as the client's FUN_0048dd90 (UI\inv.cpp) builds
// it. The string is drawn bottom-up, so its segments, top line first:
// name (FUN_0048c060), defense (FUN_00485ee0), block (FUN_00485be0),
// smite/kick (FUN_00485d40), damage (FUN_00485410), quantity / spelldesc
// (FUN_00486100 / FUN_00486370), charm, socket filler (FUN_004865d0),
// durability (FUN_00484e90), class only, required dexterity / strength /
// level (FUN_00485170 / FUN_004850a0 / FUN_00484ff0, red when unmet,
// FUN_0062eaf0), weapon speed (FUN_004861d0), "Unidentified" (red),
// properties (blue, FUN_004e6410), ethereal / sockets (blue, FUN_00484b10).
// A number the item's mods changed is blue. `wearer` is the hovering
// player (class, strength, dexterity, level); without one there are no
// red requirements, class bonuses, speed or spelldesc lines, as game.exe
// without a player unit. docs/research/re/item-names.md.
// The socketed runes' letters (FUN_00486670) under the name.
// No time-of-day stats (268-303, Properties "*/time"): no 1.14d UniqueItems,
// SetItems, Sets, Runes, CubeMain, Automagic, Gems or QualityItems row has one,
// and MagicSuffix "of Dawn" / "of Sunlight" aren't spawnable.
// An identified set item (FUN_0048d1d0) adds, under those: its own set
// bonuses that are on (green, FUN_004e6560 for the owner), a blank line,
// while it's worn its set's bonuses the owner has (gold, FUN_004e6680) and a
// blank line, the set's name (gold, Sets.txt record +2) and its members from
// the last row to the first (record +0x110 in SetItems order, each its +0x24
// name through "%0", 0x2769), green when the owner has an identified piece
// of that row worn or in the inventory, cube or stash (FUN_00486770), else
// red. `owner_items`: the hovering unit's items (the player's, the merc's);
// none, every member is red and no bonus shows.
std::vector<TextLine> item_lines(const GameData& game_data, const d2d::d2s::Item& item, int clvl,
                                        const d2d::rules::Wearer* wearer = nullptr, const std::vector<d2d::d2s::Item>* owner_items = nullptr);
}  // namespace d2d::game
