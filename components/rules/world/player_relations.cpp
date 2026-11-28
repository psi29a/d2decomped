// SPDX-License-Identifier: GPL-3.0-or-later
#include "player_relations.hpp"

#include <algorithm>
#include <cstdint>

namespace d2d::rules {

// FUN_0055b140: game.exe prepends a five-dword node after its state-7 and
// list-disable gates. Those gates have no rules-side inputs; callers apply them.
// Its +0xc, the party button's state, starts at 1 when the other is in a party
// (FUN_00554630 of the other, 0x55b1bb), else 0.
void PlayerRelations::add(int self, int other, bool other_in_party) {
    if (self == other || find(self, other)) return;
    entries_.insert(entries_.begin(), PlayerRelation{ other, 0, 0, other_in_party ? 1 : 0 });
}

// FUN_0055b1e0: unlink the first node with the other player's id.
void PlayerRelations::remove(int self, int other) {
    if (self == other) return;
    const auto found = std::ranges::find(entries_, other, &PlayerRelation::player);
    if (found != entries_.end()) entries_.erase(found);
}

// FUN_0055b260: set or clear every requested relation bit. The stock function
// also broadcasts S->C 0x8c; transport emission belongs to the hosted client.
void PlayerRelations::set(int self, int other, std::uint32_t mask, bool enabled) {
    auto* relation = find(self, other);
    if (!relation) return;
    relation->flags = enabled ? relation->flags | mask : relation->flags & ~mask;
}

// FUN_0055b300: return only the requested bits, or zero for self / no node.
auto PlayerRelations::get(int self, int other, std::uint32_t mask) const -> std::uint32_t {
    const auto* relation = find(self, other);
    return relation ? relation->flags & mask : 0;
}

// FUN_0055b6c0 with a non-null other: the pair node's +8 stamp.
auto PlayerRelations::hostile_stamp(int self, int other) const -> std::uint32_t {
    const auto* relation = find(self, other);
    return relation ? relation->hostile_until : 0;
}

// FUN_0055b720's pair-node store. Its separate player-data +0x160 store is
// World::Player::declare_hostile.
void PlayerRelations::hostile_stamp(int self, int other, std::uint32_t stamp) {
    if (auto* relation = find(self, other)) relation->hostile_until = stamp;
}

// FUN_0055b3f0: the node's +0xc (a short), 1 for self, 0 without a node.
auto PlayerRelations::party_state(int self, int other) const -> int {
    if (self == other) return 1;
    const auto* relation = find(self, other);
    return relation ? relation->party_state : 0;
}

// FUN_0055b3a0: the node's +0xc; no node (or self): nothing.
void PlayerRelations::party_state(int self, int other, int state) {
    if (auto* relation = find(self, other)) relation->party_state = state;
}

// FUN_0055b110: the node keyed by the other player's id; none for self.
auto PlayerRelations::find(int self, int other) -> PlayerRelation* {
    if (self == other) return nullptr;
    const auto found = std::ranges::find(entries_, other, &PlayerRelation::player);
    return found == entries_.end() ? nullptr : &*found;
}

// FUN_0055b110, read-only.
auto PlayerRelations::find(int self, int other) const -> const PlayerRelation* {
    if (self == other) return nullptr;
    const auto found = std::ranges::find(entries_, other, &PlayerRelation::player);
    return found == entries_.end() ? nullptr : &*found;
}

}  // namespace d2d::rules
