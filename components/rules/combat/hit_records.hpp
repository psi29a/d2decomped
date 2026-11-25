// SPDX-License-Identifier: GPL-3.0-or-later
// A unit's melee hit records (game.exe 1.14d unit +0xac, SUnitDmg.cpp): the
// damage record a skill's start builds against one unit, kept until its do
// resolves it. combat.md "A unit's hit records".
#pragma once

#include <algorithm>
#include <vector>

namespace d2d::rules {

// A unit as the list names it: its type (+0, 0 a player, 1 a monster) and id
// (+0xc). FUN_0044be50 / FUN_00451f50 read a null unit as type 6, id -1.
struct UnitKey {
    int type = 6, id = -1;
    friend bool operator==(const UnitKey&, const UnitKey&) = default;
};

// The list one unit keeps, newest first (the node's +0x84 the next). Each node
// names the unit that made it (+4 / +8), the unit struck (+0xc / +0x10) and
// carries the record (+0x14, 0x70 bytes in game.exe; `Record` here).
// tools/emu/hit_records.py runs game.exe's four on the same operations.
template <class Record>
struct HitRecords {
    struct Node {
        UnitKey owner, target;
        Record record;
    };
    std::vector<Node> nodes;   // [0] the head

    // FUN_0057ca80 (from FUN_0057dbf0, the build's end): a new head, for a
    // player or monster target only (an object, a missile or an item none).
    void add(UnitKey owner, UnitKey target, const Record& record) {
        if (target.type == 0 || target.type == 1) nodes.insert(nodes.begin(), Node{ owner, target, record });
    }
    // FUN_0057d690: the first (newest) record whose node names this pair.
    [[nodiscard]] Record* find(UnitKey owner, UnitKey target) {
        const auto found = std::ranges::find_if(nodes, [&](const Node& node) { return node.owner == owner && node.target == target; });
        return found == nodes.end() ? nullptr : &found->record;
    }
    // FUN_0057c9f0 (from FUN_0057d4f0, the resolve's end): every record of this pair.
    void drop(UnitKey owner, UnitKey target) {
        std::erase_if(nodes, [&](const Node& node) { return node.owner == owner && node.target == target; });
    }
    // FUN_0057c980 (a unit's removal, FUN_005349d0; freeze, FUN_00553570):
    // every record its owner made, whatever it struck.
    void drop_self(UnitKey owner) {
        std::erase_if(nodes, [&](const Node& node) { return node.owner == owner; });
    }
};

}  // namespace d2d::rules
