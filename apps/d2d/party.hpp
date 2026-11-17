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
#include <iterator>
#include <string>
#include <type_traits>
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
    // The roster's regroup (FUN_00479ab0, party.md "The party groups"), on `rows` in the roster
    // list's order (0x7bb5c0 from its head), as game.exe runs it after each roster event:
    // 1. each party word's records into a group, taken in list order and each put first, so a
    //    group holds them backwards (FUN_00479940, FUN_00479800);
    // 2. each group sorted, an insertion that puts a record before the first it beats and a tie
    //    after the rows it ties with: by the name at record +0, a byte-wise strcmp (FUN_004795f0),
    //    or with bit 1 of the game flags (0x712efc, S->C 0x01) by the sort key, most first
    //    (FUN_004796d0); the groups by word, unsigned, so 0xffff (none) last (FUN_00479860);
    // 3. our own party's group first, our own record first in it (FUN_004798c0);
    // 4. the groups joined back into the list (FUN_00479a00); with no party of our own (0xffff),
    //    our own record first (FUN_00479a60).
    // So with the sort key, rows that tie trade places at each regroup. NetGame keeps the roster
    // list and regroups it where game.exe does (NetGame::roster).
    static void regroup(std::vector<Row>& rows, std::uint32_t own_id, int own_party, bool by_sort_key) {
        struct Group { int word; std::vector<Row> rows; };
        std::vector<Group> chain;                          // from its head (0x7bb5c8)
        std::vector<Row> left = std::move(rows);
        rows.clear();
        while (!left.empty()) {
            const int word = left.front().party;
            if (std::ranges::none_of(chain, [word](const Group& group) { return group.word == word; })) chain.insert(chain.begin(), Group{ word, {} });
            auto& group = *std::ranges::find(chain, word, &Group::word);
            std::vector<Row> kept;
            for (auto& row : left) {
                if (row.party == word) group.rows.insert(group.rows.begin(), std::move(row));
                else kept.push_back(std::move(row));
            }
            left = std::move(kept);
        }
        // Each item goes before the first already placed that it comes before, else last.
        const auto insertion = [](auto& items, auto&& before) {
            std::remove_reference_t<decltype(items)> out;
            for (auto& item : items) {
                const auto place = std::ranges::find_if(out, [&](const auto& placed) { return before(item, placed); });
                out.insert(place, std::move(item));
            }
            items = std::move(out);
        };
        const auto by_name = [](const Row& lhs, const Row& rhs) {
            return std::lexicographical_compare(lhs.name.begin(), lhs.name.end(), rhs.name.begin(), rhs.name.end(),
                                                [](char left, char right) { return std::uint8_t(left) < std::uint8_t(right); });
        };
        for (auto& group : chain) {
            if (by_sort_key) insertion(group.rows, [](const Row& lhs, const Row& rhs) { return lhs.sort_key > rhs.sort_key; });
            else insertion(group.rows, by_name);
        }
        insertion(chain, [](const Group& lhs, const Group& rhs) { return std::uint16_t(lhs.word) < std::uint16_t(rhs.word); });
        const auto own_first = [own_id](std::vector<Row>& list) {
            if (const auto own = std::ranges::find(list, own_id, &Row::id); own != list.end()) std::rotate(list.begin(), own, std::next(own));
        };
        if (std::uint16_t(own_party) != 0xffff)
            if (const auto own_group = std::ranges::find(chain, own_party, &Group::word); own_group != chain.end()) {
                std::rotate(chain.begin(), own_group, std::next(own_group));
                own_first(chain.front().rows);
            }
        for (auto& group : chain)
            for (auto& row : group.rows) rows.push_back(std::move(row));
        if (std::uint16_t(own_party) == 0xffff) own_first(rows);
    }
    bool open = false;
    bool close_down = false;               // 0x7bf030
    Click input(const Mouse& mouse, const std::vector<Row>& rows, const Own& own);
    void draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<Row>& rows, const Own& own, int mouse_x, int mouse_y) const;
};

// The Party roster's list (0x7bb5c0, from its head; party.md "The roster"): its records are its
// own, apart from the world's units. S->C 0x5b makes one (FUN_0047a6f0) and 0x5c frees it
// (0x47a8b0). A unit's 0x0a (FUN_0045cc10) only unlinks and frees the unit (FUN_00465e80,
// FUN_00465870), so a player out of sight keeps its record.
struct PartyRoster {
    // A record (0x84 bytes, zeroed when made): +0x10 id, +0x66 name, +0x1c class, +0x20 level,
    // +0x22 party (0xffff none), +0x24 area, +0x30 the party button's state, our relation flags
    // toward it (its +0x34 nodes), +0x18 sort key.
    struct Record { std::uint32_t id = 0; std::string name; int cls = 0, level = 0, party = 0xffff, area = 0, relation = 0, flags = 0, sort_key = 0; };
    std::vector<Record> records;
    [[nodiscard]] Record* find(std::uint32_t id) {
        const auto found = std::ranges::find(records, id, &Record::id);
        return found == records.end() ? nullptr : &*found;
    }
    [[nodiscard]] const Record* find(std::uint32_t id) const {
        const auto found = std::ranges::find(records, id, &Record::id);
        return found == records.end() ? nullptr : &*found;
    }
    // 0x5b (FUN_0047a6f0): a new record, zeroed, at the list's head; one already there is kept.
    Record& join(std::uint32_t id) {
        if (auto* known = find(id)) return *known;
        return *records.insert(records.begin(), Record{ .id = id });
    }
    void leave(std::uint32_t id) { std::erase_if(records, [id](const Record& record) { return record.id == id; }); }
    // FUN_00479ab0 on the records: our own party from our own record (FUN_004792e0), else 0xffff.
    void regroup(std::uint32_t own_id, bool by_sort_key) {
        const auto* own = find(own_id);
        std::vector<PartyPanel::Row> rows;
        rows.reserve(records.size());
        for (const auto& record : records) rows.push_back({ .id = record.id, .name = record.name, .party = record.party, .sort_key = record.sort_key });
        PartyPanel::regroup(rows, own_id, own ? own->party : 0xffff, by_sort_key);
        std::vector<Record> sorted;
        sorted.reserve(records.size());
        for (const auto& row : rows) sorted.push_back(std::move(*std::ranges::find(records, row.id, &Record::id)));
        records = std::move(sorted);
    }
};

}  // namespace d2d::client
