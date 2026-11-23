// SPDX-License-Identifier: GPL-3.0-or-later
// The character the World plays: what a save holds (header, stats,
// items, corpse), its look, and what the char panel computes from them.
// The client's CharCreateUI (ui.hpp) adds the create screen's controls.
#pragma once

#include "gamedata.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::game {

// Is item type `t` (or an Equiv ancestor) `want`?
bool type_is(const GameData& game_data, const std::string& type, std::string_view want);

// What a filled socket adds to `parent`: a jewel's own properties, or the
// gem/rune's gems.txt bonus for the parent's kind (weapon, shield, else
// helm/armour).
std::vector<d2d::d2s::ItemProp> socket_props(const GameData& game_data, const d2d::d2s::Item& parent,
                                             const d2d::d2s::Item& filled);

// A worn set item's bonus lists that are on (FUN_004e6560 over FUN_0062a370's
// mask of its set's worn members, not broken): add func 2, list i
// (aprop(i+1)a / b, FUN_0065fec0 kind 4) with i + 2 pieces of its set worn;
// add func 1, list i while the member in place i (its own skipped) is worn.
std::vector<d2d::d2s::ItemProp> set_bonus_props(const GameData& game_data, const std::vector<d2d::d2s::Item>& items,
                                                const d2d::d2s::Item& item);

// Sets.txt's bonuses for the sets worn (FUN_00663b40 -> FUN_00660120: a
// stat list per set on the player): with n of its pieces worn, the
// partial bonuses 2a, 2b .. na, nb (at most pieces - 1's); all of them
// worn, the full bonuses FCode1..8 too. Set item rows count as pieces
// once each.
// A row's min / max are its property's own: a damage range (Vidala's Rig
// dmg-cold 15-20), gethit-skill's chance and level (Cow King's 25, 5).
// `only`: that Sets.txt row's alone (the hover text's, FUN_004e6680).
// ponytail: game.exe's list goes on with the set piece that last refreshed
// it (FUN_00663b40 -> FUN_00660120, the item in ESI), which picks a dmg-min /
// dmg-max / dmg% bonus's hands; here with none (one-hand), as d2d keeps no
// order of the pieces' changes.
std::vector<d2d::d2s::ItemProp> set_props(const GameData& game_data, const std::vector<d2d::d2s::Item>& items, int only = -1);

// An item whose stats reach the character: worn (slots 1..10, the weapon
// set in use) or an inventory charm that met its requirements at the last
// settle (FUN_0062ff70: `charm_short`), and usable: not broken (0x100) nor
// short of its requirements (0x4000, rules::settle_requirements).
bool gear_active(const d2d::d2s::Item& item);

// Everything the character's gear gives: worn items (slots 1..10), what's
// socketed in them, their set bonuses on, inventory charms.
std::vector<d2d::d2s::ItemProp> gear_props(const GameData& game_data, const std::vector<d2d::d2s::Item>& items);

// A skill an item's charges give (stat 204, item_charged_skill: param
// skill << 6 | level, value max << 8 | left). game.exe keeps one skill
// entry per item and skill (SKILLS_AddOrRemoveItemSkill, keyed by the
// item's unit id, skill entry +0x34), added by the stat hook while the
// item's stats reach its owner and it has charges left, removed at 0.
struct ChargedSkill {
    int item = -1;                         // the holder's Item::id (its socketed items' charges are its own, FUN_0065c820)
    int skill = -1, level = 0, left = 0, max = 0;
    friend bool operator==(const ChargedSkill&, const ChargedSkill&) = default;
};
[[nodiscard]] ChargedSkill charged_skill(int item_id, const d2d::d2s::ItemProp& prop);
// The character's charged skill entries, in item order: each active item's
// (gear_active) and its socketed items' stat 204 with a level of 1..63
// (FUN_0065c820) and charges left.
std::vector<ChargedSkill> charged_skills(const std::vector<d2d::d2s::Item>& items);

// A cast from charged entry `entry` spends one charge (FUN_0056bec0, the
// skill's cost in place of mana, FUN_0056bfe0): the stat on the entry's
// item, else on an item socketed in it (FUN_0065c820), goes to max << 8 |
// max(left - 1, 0). False when nothing is left to spend.
bool use_charge(std::vector<d2d::d2s::Item>& items, const ChargedSkill& entry);

// The char panel's computed values (stat 30 next level, 31 defence,
// resistances 39/43/41/45), as FUN_004a7d00 shows them. From the save's
// base stats and gear.
// Equipped slots 1..10: the weapon set in use (a switch moves its hands
// into 4 / 5, weapon-switch.md).
// Worn items, what's socketed in them, their set bonuses, charms and the
// passives with no weapon type (Iron Skin's defense %, Natural Resistance).
// Fight::update_fighters adds the states' part (auras, curses, shrines).
struct PanelStats {
    std::int64_t next = -1, defense = 0;
    std::array<std::int64_t, 4> res{};           // fire, cold, lightning, poison
    std::array<std::int64_t, 4> res_cap{ 75, 75, 75, 75 };   // 75 + max resist, at most 95: gold at it
    // What items and passives add to the character's own stats 0..11
    // (strength .. max stamina, whole points): the panel shows the sum,
    // blue when it's more, red when less (FUN_004a7d00).
    std::array<std::int64_t, 12> bonus{};
    // item_maxhp_percent / item_maxmana_percent (76, 77; ItemStatCost op 11):
    // a share of the own maximum plus the items' flat +life / +mana, not of
    // the attributes' (op 8) share. Fight::update_fighters adds it.
    std::array<std::int64_t, 2> max_percent{}, max_flat{};
    std::array<d2d::rules::AttackLine, 2> attack{};   // the left / right skill's block, as the server sends it
    int block = 0;                               // the chance to block (FUN_00622720): the defence hover's first line (FUN_004a7180)
    std::uint8_t blue = 0, red = 0;              // the states' colours (rules::PanelColour bits)
};

PanelStats panel_stats(const GameData& game_data, const d2d::d2s::Header& header,
                       const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Stats& stats,
                       const std::vector<d2d::rules::PassiveStat>* passives = nullptr);

struct Character {
    int character_class = -1;    // the save's class (0 Amazon .. 6 Assassin, d2s order), -1 none yet
    // Gear the in-game character wears: a loaded save's appearance bytes,
    // or unset for a fresh character (starting gear).
    std::optional<std::array<std::uint8_t, 32>> appearance;   // + the tints
    std::vector<d2d::d2s::Item> items;   // a loaded save's items
    d2d::d2s::Stats stats;               // ... and attributes
    std::vector<d2d::d2s::Item> corpse;  // ... and its corpse's items (d2s corpse list)
    std::vector<d2d::d2s::Item> merc_items;   // ... and what its merc wears (d2s "jf" list)
    PanelStats panel;                    // ... and what the char panel computes
    bool expansion = true;               // the save's expansion flag (stash size)
    d2d::d2s::Header header;             // the loaded save's header (quest flags ...)
    std::string name;                    // the character's name
    bool fresh = false;                  // made, not loaded: its first game runs no quest joins (FUN_00546270's `fresh`)
};

}  // namespace d2d::game
