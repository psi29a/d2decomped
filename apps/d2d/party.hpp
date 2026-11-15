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
#include <utility>
#include <vector>

namespace d2d::client {

struct PartyPanel {
    // A row (the roster, FUN_0049a1d0): the player, its class (d2s), level,
    // party (0xffff none), the party button's state, our flags toward it,
    // where it is (a party member's level name, "" not shown), hostile to us.
    struct Row { std::uint32_t id = 0; std::string name; int cls = 0, level = 0, party = 0xffff, relation = 0, flags = 0; std::string area; bool hostile = false; };
    // `fixed`: local co-op's one party, everyone in it from joining: no
    // LEAVE, party buttons or boxes, only the close button takes a click.
    struct Own { std::string name; int level = 0, party = 0xffff; bool in_town = false, fixed = false; };
    // What a click asks for.
    struct Click { enum Kind { kNone, kClose, kButton, kFlag, kLeave } kind = kNone; std::uint32_t player = 0; int flag = 0; };
    // The roster's order (FUN_00479ab0's regroup, party.md): by party word
    // ascending, 0xffff (none) last, our own party's group first; inside a
    // group by unit id.
    // ponytail: game.exe sorts inside a group by record +0x18 or the 16-bit
    // string at +0 (0x712efc bit 1, FUN_004799c0); neither field is traced.
    static void order(std::vector<Row>& rows, int own_party) {
        const auto group = [own_party](const Row& row) { return own_party != 0xffff && row.party == own_party ? -1 : row.party; };
        std::ranges::sort(rows, [&group](const Row& lhs, const Row& rhs) { return std::pair(group(lhs), lhs.id) < std::pair(group(rhs), rhs.id); });
    }
    bool open = false;
    bool close_down = false;               // 0x7bf030
    Click input(const Mouse& mouse, const std::vector<Row>& rows, const Own& own);
    void draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<Row>& rows, const Own& own, int mouse_x, int mouse_y) const;
};

}  // namespace d2d::client
