// SPDX-License-Identifier: GPL-3.0-or-later
// One-way player relation nodes from game.exe 1.14d's FUN_0055b140 family.
#pragma once

#include <cstdint>
#include <vector>

namespace d2d::rules {

inline constexpr std::uint32_t kRelationLoot = 1;
inline constexpr std::uint32_t kRelationNotHeard = 2;
inline constexpr std::uint32_t kRelationSquelched = 4;
inline constexpr std::uint32_t kRelationHostile = 8;

struct PlayerRelation {
    int player = -1;
    std::uint32_t flags = 0;
    std::uint32_t hostile_until = 0;
    int party_state = 0;   // node +0xc: the party button as `player` sees it (0 invite, 1 none, 2 accept, 4 asked)
};

// A player's one-way relations, keyed by the other player's unit id.
class PlayerRelations {
public:
    void add(int self, int other, bool other_in_party);
    void remove(int self, int other);
    void set(int self, int other, std::uint32_t mask, bool enabled);
    [[nodiscard]] std::uint32_t get(int self, int other, std::uint32_t mask) const;
    [[nodiscard]] std::uint32_t hostile_stamp(int self, int other) const;
    void hostile_stamp(int self, int other, std::uint32_t stamp);
    // The party button's state toward `other` (FUN_0055b3f0: 1 for self, 0 without a node).
    [[nodiscard]] int party_state(int self, int other) const;
    // FUN_0055b3a0: a node's state; nothing without one.
    void party_state(int self, int other, int state);
    // The nodes in list order (newest first, as FUN_0055b140 prepends).
    [[nodiscard]] const std::vector<PlayerRelation>& entries() const { return entries_; }

private:
    PlayerRelation* find(int self, int other);
    [[nodiscard]] const PlayerRelation* find(int self, int other) const;
    std::vector<PlayerRelation> entries_;
};

}  // namespace d2d::rules
