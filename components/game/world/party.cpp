// SPDX-License-Identifier: GPL-3.0-or-later
// The Party screen's buttons on the host: game.exe 1.14d's C->S 0x5d / 0x5e
// handlers (FUN_0054c2a0 / FUN_0054c300 -> FUN_005a6000, table 0x6e2218),
// PartyScreen.cpp's invite / cancel / accept / leave and .\UNIT\Party.cpp's
// party control (docs/research/re/party.md "The party buttons on the host").
#include "world.hpp"

#include <player_relations.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2d::game {

namespace {

// The table's entries (0x6e2218, by the packet's action byte).
constexpr int kLoot = 1, kHear = 2, kSquelch = 3, kHostile = 4, kInvite = 6, kCancel = 7, kAccept = 8, kLeave = 9;
// The party button's states (node +0xc, S->C 0x8b): 0 INVITE, 1 none, 2 ACCEPT, 4 our invitation waiting.
constexpr int kCanInvite = 0, kNoButton = 1, kCanAccept = 2, kAsked = 4;
// FUN_005a5690's types (S->C 0x5a event 7, the client's FUN_0049e8f0 table).
constexpr int kInvitesYou = 5, kCancelled = 6, kJoinedYourParty = 7, kYouAllied = 8, kLeftYourParty = 9;
// FUN_005a57b0: the declarer's class 0, 1 or 6 (Amazon, Sorceress, Assassin) words it "her".
constexpr bool her(int cls) { return cls == 0 || cls == 1 || cls == 6; }

// FUN_005a5690 through FUN_005531c0: a 0x5a event 7 line to `told`'s client, naming `about`.
void party_line(World::Player& told, int type, const World::Player& about) {
    told.events.push_back(ev::Message{ ev::Message::party, about.character.name, {}, type });
}

// FUN_0055b140 one way (no node for self or one already there): its state from `other`'s party.
void link(World::Player& self, const World::Player& other) { self.relations.add(self.id, other.id, other.party >= 0); }
int state(const World::Player& self, const World::Player& other) { return self.relations.party_state(self.id, other.id); }
void set_state(World::Player& self, const World::Player& other, int value) { self.relations.party_state(self.id, other.id, value); }

}  // namespace

auto World::player_by_id(int id) -> Player* {
    for (auto& player : players) if (player->id == id) return player.get();
    for (Player* player : elsewhere) if (player->id == id) return player;
    return nullptr;
}

// FUN_005a6000 resolves the other player (FUN_00552f60) and calls the table's entry with the
// actor in EDX, the other and the packet's `on` byte on the stack. Entry 0, 5 and 10 do nothing
// here (0 and 5 are null; 10 reads past the table). The handlers refuse first when bit 16 of
// game +0x1d28's +8 is set (FUN_0053fd10): not modelled, as d2d's games never set it.
// d2d's own (deviations.md 7): while two or more share the host's screen, local co-op stays one
// party, so a screen player neither leaves, nor turns hostile on a partner, nor declares on
// anyone in its party (toggle_hostility would leave it, and turn the partner hostile with the
// target's party).
auto World::party_action(Player& other, int action, std::uint32_t bits, std::uint32_t now_ms) -> void {
    Player& self = *actor;
    if (!self.own_screen && screen_players() > 1
        && (action == kLeave || (action == kHostile && (!other.own_screen || (bits != 0 && same_party(self, other))))))
        return;
    switch (action) {
    case kLoot:
    case kHear:
    case kSquelch: {
        // FUN_005a5750 / FUN_005a5840 / FUN_005a58a0: a node each way, then the flag on the
        // actor's node toward the other when its masked bits aren't `bits` (the packet's `on`) already (FUN_0055b260).
        // Loot (FUN_005a57b0) only when the actor's client +0xa has 4, the save's hardcore bit
        // (FUN_00538670; net-join.md), and the other is told (type 1 / 2 on, 10 / 11 off).
        const std::uint32_t mask = action == kLoot ? d2d::rules::kRelationLoot : action == kHear ? d2d::rules::kRelationNotHeard : d2d::rules::kRelationSquelched;
        if (action == kLoot && !self.character.header.hardcore()) return;
        link(other, self);
        link(self, other);
        if (self.relations.get(self.id, other.id, mask) != bits) self.relations.set(self.id, other.id, mask, bits != 0);
        if (action == kLoot) party_line(other, (bits != 0 ? 1 : 10) + (her(self.character.header.cls) ? 1 : 0), self);
        return;
    }
    case kHostile:
        (void)toggle_hostility(other, bits, now_ms);
        return;
    case kInvite:
        // FUN_005a5be0: from INVITE, ours waits (4) and theirs offers ACCEPT (2); the other is told.
        link(self, other);
        link(other, self);
        if (state(self, other) != kCanInvite) return;
        set_state(self, other, kAsked);
        set_state(other, self, kCanAccept);
        party_line(other, kInvitesYou, self);
        return;
    case kCancel:
        // FUN_005a5b30: a waiting invitation back to what each side's party allows.
        link(self, other);
        link(other, self);
        if (state(self, other) != kAsked) return;
        set_state(self, other, other.party >= 0 ? kNoButton : kCanInvite);
        set_state(other, self, self.party >= 0 ? kNoButton : kCanInvite);
        party_line(other, kCancelled, self);
        return;
    case kAccept: {
        // FUN_005a5a30: the actor takes the other's invitation (ours must show ACCEPT). The
        // inviter's party, or a new one with the inviter first; FUN_0055b650 settles the
        // invitations each joiner still had; then every player's buttons (FUN_005a5970).
        int party = other.party;
        link(self, other);
        link(other, self);
        if (state(self, other) != kCanAccept) return;
        const auto settle_invitations = [&](Player& joined) {      // FUN_0055b650
            for (const auto& relation : joined.relations.entries()) {
                if (relation.party_state != kCanAccept) continue;
                if (const Player* inviter = player_by_id(relation.player))
                    joined.relations.party_state(joined.id, inviter->id, inviter->party >= 0 ? kNoButton : kCanInvite);
            }
        };
        if (party < 0) {
            party = new_party();
            add_to_party(party, other);
            add_to_party(party, self);
            settle_invitations(other);
        } else {
            add_to_party(party, self);
        }
        settle_invitations(self);
        party_line(self, kYouAllied, other);
        // FUN_005538d0 runs FUN_005a5970 for every player in the game, {inviter, accepter, party}.
        const auto buttons_toward = [](Player& player, Player& joined) {   // FUN_005a5900 (ESI player, EDI joined)
            if (&player == &joined) return;
            if (player.party < 0) {
                if (state(joined, player) == kAsked) { set_state(player, joined, kCanAccept); return; }
                set_state(joined, player, kCanInvite);
                set_state(player, joined, kNoButton);
                return;
            }
            set_state(joined, player, kNoButton);
            set_state(player, joined, kNoButton);
        };
        each_player_anywhere([&](Player& player) {
            const int theirs = player.party;
            link(player, other);
            link(other, player);
            link(player, self);
            link(self, player);
            buttons_toward(player, other);
            buttons_toward(player, self);
            if (theirs == party && theirs >= 0 && &player != &self) party_line(player, kJoinedYourParty, self);
        });
        return;
    }
    case kLeave:
        // FUN_005a5d20: the actor's party told (FUN_00540510 with FUN_005a5c70), then out of it.
        if (self.party >= 0)
            each_player_anywhere([&](Player& member) { if (&member != &self && member.party == self.party) party_line(member, kLeftYourParty, self); });
        party_remove(self);
        return;
    default:
        return;
    }
}

// FUN_005400b0 (from FUN_00540100, which prepends the party): the counter, at least 3, past
// any id a party has; the counter left one past it. 16 bits, as game.exe's.
auto World::new_party() -> int {
    std::uint16_t id = std::max<std::uint16_t>(party_counter, 3);
    while (std::ranges::contains(parties, int(id), &Party::id)) {
        ++id;
        if (id < 3) id = 3;
    }
    party_counter = std::uint16_t(id + 1);
    parties.insert(parties.begin(), Party{ id, {} });
    return id;
}

// FUN_00540320: a member already there stays; else first in the party, and the hostility
// between them and each member there before cleared both ways (FUN_005a6080 twice).
auto World::add_to_party(int party, Player& player) -> void {
    const auto found = std::ranges::find(parties, party, &Party::id);
    if (found == parties.end() || std::ranges::contains(found->members, player.id)) return;
    found->members.insert(found->members.begin(), player.id);
    player.party = party;
    for (std::size_t i = 1; i < found->members.size(); ++i) {
        Player* member = player_by_id(found->members[i]);
        if (!member) continue;
        link(*member, player);
        member->relations.set(member->id, player.id, d2d::rules::kRelationHostile, false);
        link(player, *member);
        player.relations.set(player.id, member->id, d2d::rules::kRelationHostile, false);
    }
}

// FUN_005407a0: out of the party, every player's buttons toward them (FUN_005a5cb0 through
// FUN_005a5ff0); a party left under two is disbanded (FUN_00540220 -> FUN_00540180): the
// party unlinked first, then the same for its last member.
auto World::party_remove(Player& player) -> void {
    if (player.party < 0) return;
    const auto found = std::ranges::find(parties, player.party, &Party::id);
    player.party = -1;
    const auto buttons_toward = [this](Player& left) {           // FUN_005a5cb0 for every player
        each_player_anywhere([&](Player& other) {
            if (&other == &left) return;
            set_state(other, left, kCanInvite);
            set_state(left, other, other.party >= 0 ? kNoButton : kCanInvite);
        });
    };
    if (found == parties.end()) { buttons_toward(player); return; }
    std::erase(found->members, player.id);
    buttons_toward(player);
    if (found->members.size() >= 2) return;
    const auto rest = found->members;
    parties.erase(found);
    for (const int id : rest)
        if (Player* last = player_by_id(id)) { last->party = -1; buttons_toward(*last); }
}

}  // namespace d2d::game
