// SPDX-License-Identifier: GPL-3.0-or-later
// The Party screen (UI 0x16, FUN_0049c3d0; docs/research/re/party.md): a
// joined game's other players, each a row with its party button and the
// hostile / loot / hear / squelch boxes; the player's own name and LEAVE
// on top.
#pragma once

#include "scene.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace d2d::client {

struct PartyPanel {
    // A row (the roster, FUN_0049a1d0): the player, its class (d2s), level,
    // party (0xffff none), the party button's state, our flags toward it,
    // where it is (a party member's level name, "" not shown), hostile to us,
    // its sort key (record +0x18: S->C 0x65's signed word, set at 0x479b10).
    struct Row { std::uint32_t id = 0; std::string name; int cls = 0, level = 0, party = 0xffff, relation = 0, flags = 0; std::string area; bool hostile = false; int sort_key = 0; };
    // `fixed`: local co-op's one party, everyone in it from joining: no
    // LEAVE, party buttons or boxes, only the close button takes a click.
    struct Own { std::string name; int level = 0, party = 0xffff; bool in_town = false, fixed = false; };
    // What a click asks for.
    struct Click { enum Kind { kNone, kClose, kButton, kFlag, kLeave } kind = kNone; std::uint32_t player = 0; int flag = 0; };
    // The roster's order (FUN_00479ab0's regroup, party.md): by party word
    // ascending, 0xffff (none) last, our own party's group first; inside a
    // group by the player name at record +0 (FUN_004793c0 copies it there),
    // a byte-wise strcmp insertion sort (FUN_004795f0). With bit 1 of the
    // game flags (0x712efc, S->C 0x01; FUN_0047a980) FUN_004799c0 sorts by
    // the sort key instead, most first (FUN_004796d0: one goes before the
    // first with less, signed), a tie after the rows it ties with.
    // NetGame keeps both (Town::party_rows passes them).
    // ponytail: a tie keeps d2d's row order, not the group list's
    // (FUN_00479800's).
    static void order(std::vector<Row>& rows, int own_party, bool by_sort_key = false) {
        const auto group = [own_party](const Row& row) { return own_party != 0xffff && row.party == own_party ? -1 : row.party; };
        std::ranges::stable_sort(rows, [&group, by_sort_key](const Row& lhs, const Row& rhs) {
            if (group(lhs) != group(rhs)) return group(lhs) < group(rhs);
            return by_sort_key ? lhs.sort_key > rhs.sort_key : lhs.name < rhs.name;
        });
    }
    bool open = false;
    bool close_down = false;               // 0x7bf030
    Click input(const Mouse& mouse, const std::vector<Row>& rows, const Own& own);
    void draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<Row>& rows, const Own& own, int mouse_x, int mouse_y) const;
};

}  // namespace d2d::client
