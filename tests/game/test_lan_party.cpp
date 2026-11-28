// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Party screen's buttons on the host (world/party.cpp, party.md "The party
// buttons on the host"): invite (FUN_005a5be0), cancel (FUN_005a5b30), accept
// (FUN_005a5a30 with FUN_0055b650 and FUN_005a5970 / FUN_005a5900 for every
// player), leave (FUN_005a5d20 -> FUN_005407a0, FUN_005a5cb0, the disband), the
// party ids (FUN_005400b0), the boxes (FUN_005a5840 / FUN_005a58a0 / FUN_005a57b0),
// a join's auto-party (deviations.md 7), the party lines (0x5a event 7) and the
// roster and the click over the wire. tools/emu/party_buttons.py runs game.exe's
// functions on the same steps and checks the same states.
#include <character.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <player_relations.hpp>
#include <protocol.hpp>
#include <replication.hpp>
#include <world.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <string>
#include <variant>
#include <vector>

using namespace d2d::game;

namespace {

// The party button's state `self` shows toward `other` (node +0xc).
int button(const World::Player& self, const World::Player& other) { return self.relations.party_state(self.id, other.id); }

// The party lines a player was told since the last look: {type, name}; the list cleared.
struct Line { int sub; std::string name; };
std::vector<Line> lines(World::Player& player) {
    std::vector<Line> told;
    for (const auto& event : player.events)
        if (const auto* message = std::get_if<ev::Message>(&event); message && message->kind == ev::Message::party) told.push_back({ message->sub, message->name });
    player.events.clear();
    return told;
}
bool told(World::Player& player, std::initializer_list<Line> expected) {
    const auto got = lines(player);
    if (got.size() != expected.size()) return false;
    std::size_t index = 0;
    for (const auto& line : expected) {
        if (got[index].sub != line.sub || got[index].name != line.name) return false;
        ++index;
    }
    return true;
}

}  // namespace

int main() try {
    GameData data;
    Level town;
    town.id = 1;
    town.ds1 = d2d::ds1::Map(8, 8, 1, 1);
    World world(&data, -1, -1);
    world.level = &town;
    auto& amy = world.lead();
    auto& bob = world.add_player();
    auto& cid = world.add_player();
    auto& dan = world.add_player();
    amy.character.name = "Amy"; bob.character.name = "Bob"; cid.character.name = "Cid"; dan.character.name = "Dan";
    const auto click = [&](World::Player& from, World::Player& toward, int action, std::uint32_t bits = 0) {
        world.act_as(from);
        world.party_action(toward, action, bits, 1000);
        world.act_as(amy);
    };

    // Invite: ours waits (4), theirs offers ACCEPT (2); Bob hears of it (type 5).
    click(amy, bob, 6);
    assert(button(amy, bob) == 4 && button(bob, amy) == 2);
    assert(told(bob, { { 5, "Amy" } }) && told(amy, {}));
    // A second invite does nothing.
    click(amy, bob, 6);
    assert(button(amy, bob) == 4 && told(bob, {}));
    // Cancel: back to INVITE both ways (neither in a party); type 6.
    click(amy, bob, 7);
    assert(button(amy, bob) == 0 && button(bob, amy) == 0);
    assert(told(bob, { { 6, "Amy" } }));
    // Accepting with no invitation waiting does nothing.
    click(bob, amy, 8);
    assert(amy.party < 0 && bob.party < 0);

    // Invite and accept: party 3 (the first id issued), the inviter first in, so listed last.
    click(amy, bob, 6);
    lines(bob);
    click(bob, amy, 8);
    assert(amy.party == 3 && bob.party == 3 && world.parties.size() == 1);
    assert((world.parties[0].members == std::vector<int>{ bob.id, amy.id }));
    assert(button(amy, bob) == 1 && button(bob, amy) == 1);               // members: no button
    assert(button(amy, cid) == 0 && button(cid, amy) == 1);               // FUN_005a5900: they can invite Cid, Cid can't invite them
    assert(button(bob, dan) == 0 && button(dan, bob) == 1);
    assert(told(bob, { { 8, "Amy" } }) && told(amy, { { 7, "Bob" } }) && told(cid, {}));

    // Cid invited by Amy while Dan invites Cid too; Cid takes Amy's: Dan's invitation
    // to Cid turns to what Dan's party allows (FUN_0055b650: Dan has none, INVITE).
    click(amy, cid, 6);
    click(dan, cid, 6);
    assert(button(cid, amy) == 2 && button(cid, dan) == 2 && button(dan, cid) == 4);
    lines(cid);
    click(cid, amy, 8);
    assert(cid.party == 3 && (world.parties[0].members == std::vector<int>{ cid.id, bob.id, amy.id }));
    assert(button(cid, amy) == 1 && button(cid, bob) == 1 && button(bob, cid) == 1);
    assert(button(cid, dan) == 0 && button(dan, cid) == 1);               // Dan's offer withdrawn by the new party
    assert(told(cid, { { 8, "Amy" } }) && told(amy, { { 7, "Cid" } }) && told(bob, { { 7, "Cid" } }) && told(dan, {}));

    // The boxes: hear and squelch set our flag on our node toward them, one way.
    click(bob, dan, 2, 2);
    click(bob, dan, 3, 4);
    assert(bob.relations.get(bob.id, dan.id, 0xffff) == 6 && dan.relations.get(dan.id, bob.id, 0xffff) == 0);
    click(bob, dan, 3, 0);
    assert(bob.relations.get(bob.id, dan.id, 0xffff) == 2);
    // Loot: only a hardcore declarer (the client's +0xa bit 4); the other told (type 1 him, 2 her).
    click(bob, dan, 1, 1);
    assert(bob.relations.get(bob.id, dan.id, 1) == 0 && told(dan, {}));
    bob.character.header.status |= 4;
    bob.character.header.cls = 1;                                        // a Sorceress
    click(bob, dan, 1, 1);
    assert(bob.relations.get(bob.id, dan.id, 1) == 1 && told(dan, { { 2, "Bob" } }));
    click(bob, dan, 1, 0);
    assert(bob.relations.get(bob.id, dan.id, 1) == 0 && told(dan, { { 11, "Bob" } }));

    // Leave: the party told (type 9), Cid out; everyone's buttons toward Cid INVITE, Cid's
    // toward the party none, toward Dan INVITE (FUN_005a5cb0).
    click(cid, cid, 9);
    assert(cid.party < 0 && (world.parties[0].members == std::vector<int>{ bob.id, amy.id }));
    assert(told(amy, { { 9, "Cid" } }) && told(bob, { { 9, "Cid" } }) && told(dan, {}) && told(cid, {}));
    assert(button(amy, cid) == 0 && button(bob, cid) == 0 && button(dan, cid) == 0);
    assert(button(cid, amy) == 1 && button(cid, bob) == 1 && button(cid, dan) == 0);

    // Bob leaves too: a party of one is disbanded (FUN_00540220), Amy out with it.
    click(bob, bob, 9);
    assert(amy.party < 0 && bob.party < 0 && world.parties.empty());
    for (auto* one : { &amy, &bob, &cid, &dan })
        for (auto* other : { &amy, &bob, &cid, &dan })
            if (one != other) assert(button(*one, *other) == 0);

    // The next party's id: the counter, one past the last issued (FUN_005400b0).
    click(dan, cid, 6);
    click(cid, dan, 8);
    assert(dan.party == 4 && cid.party == 4);

    // A join (World::join): a LAN joiner with auto_party into the first player's party;
    // without it, alone. Local co-op's (own_screen false) always.
    {
        GameData joined_data;
        World joined(&joined_data, -1, -1);
        joined.level = &town;
        Character joiner;
        joiner.name = "Eve";
        joined.lead().character.name = "Host";
        auto& eve = joined.join(joiner, true);
        assert(eve.party == 3 && joined.lead().party == 3);
        joined.auto_party = false;
        auto& fay = joined.join(joiner, true);
        assert(fay.party < 0);
        auto& pad = joined.join(joiner, false);
        assert(pad.party == 3);
        // A leaver: out of the party and every node toward them gone (FUN_0055b790).
        const int eve_id = eve.id;
        (void)joined.leave(eve_id);
        assert(joined.lead().party == 3 && joined.lead().relations.party_state(joined.lead().id, eve_id) == 0);
        for (const auto& player : joined.players)
            for (const auto& relation : player->relations.entries()) assert(relation.player != eve_id);
    }

    // The click and the roster over the wire (LAN protocol 17).
    for (const cmd::PartyScreen sent : { cmd::PartyScreen{ 4, 7, 8 }, cmd::PartyScreen{ 6, 9, 0 } }) {
        const auto decoded = decode(encode(sent));
        assert(decoded && std::holds_alternative<cmd::PartyScreen>(*decoded));
        const auto& got = std::get<cmd::PartyScreen>(*decoded);
        assert(got.action == sent.action && got.player == sent.player && got.bits == sent.bits);
    }
    // A portrait's life % (0x7f, FUN_00621f20): whole points, 30 of 80 is 37; no maximum, 0.
    cid.character.stats.values[d2d::d2s::kMaxLife] = 80 << 8;
    cid.character.stats.values[d2d::d2s::kLife] = (30 << 8) + 200;
    world.act_as(dan);
    const View view = world.view();
    assert(view.party == 4 && view.roster.size() == 3);
    for (const auto& row : view.roster) {
        const auto* other = world.player_by_id(int(row.id));
        assert(other && row.name == other->character.name && row.party == other->party);
        assert(row.button == button(dan, *other));
        assert(row.area == (other == &cid ? town.id : 0));                 // a party member's level alone
        assert(row.life == (other == &cid ? 37 : 0));
    }
    {   // the roster, our party and a party line through the view codec
        GameData wire_data;
        View sent;
        sent.level = &wire_data.town;
        sent.party = 4;
        sent.roster = view.roster;
        sent.roster.front().life = 37;
        sent.events.push_back(ev::Message{ ev::Message::party, "Cid", {}, 7 });
        ViewEncoder encoder;
        View received;
        assert(apply_view(wire_data, encode_view(wire_data, sent, encoder), received));
        assert(received.party == 4 && received.roster.size() == sent.roster.size());
        for (std::size_t i = 0; i < sent.roster.size(); ++i) {
            const auto& got = received.roster[i];
            const auto& had = sent.roster[i];
            assert(got.id == had.id && got.name == had.name && got.cls == had.cls && got.level == had.level && got.party == had.party
                   && got.area == had.area && got.button == had.button && got.life == had.life && got.flags == had.flags);
        }
        assert(received.events.size() == 1);
        const auto* line = std::get_if<ev::Message>(&received.events[0]);
        assert(line && line->kind == ev::Message::party && line->sub == 7 && line->name == "Cid");
    }
    std::printf("OK: the party buttons, ids, boxes, joins and the roster\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_lan_party: %s\n", error.what());
    return 1;
}
