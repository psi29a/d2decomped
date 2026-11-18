// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: dcc font game
// test-includes: apps/d2d
// The Party screen's roster regroup (FUN_00479ab0, party.md "The party groups"), PartyPanel::regroup
// against stock 1.14d's run by tools/emu/party_regroup.py (party_regroup_cases.inc): party groups by
// word ascending, no party (0xffff) last, our own party's group and our own record first
// (FUN_004798c0); inside a group by name, a byte-wise strcmp (FUN_004795f0): "Eve" before "amy"; with
// the game flags' bit 1, by the sort key (S->C 0x65's word), most first (FUN_004796d0, signed). Each
// group takes its records backwards first (FUN_00479800), so sort-key ties trade places. With a case
// file (`party_regroup.py gen`) it prints `id ids...` for each line, as the oracle's `game` does.
#include "party.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using d2d::client::PartyPanel;

namespace {

// The local record's party word (FUN_00479ab0 reads it from the record FUN_004792e0 finds), else 0xffff.
int own_party(const std::vector<PartyPanel::Row>& rows, std::uint32_t own_id) {
    for (const auto& row : rows)
        if (row.id == own_id) return row.party;
    return 0xffff;
}

std::vector<std::uint32_t> regrouped(std::vector<PartyPanel::Row> rows, std::uint32_t own_id, bool by_sort_key) {
    const int party = own_party(rows, own_id);
    PartyPanel::regroup(rows, own_id, party, by_sort_key);
    std::vector<std::uint32_t> order;
    for (const auto& row : rows) order.push_back(row.id);
    return order;
}

std::vector<std::uint32_t> ids(int own_word, bool by_sort_key = false) {
    std::vector<PartyPanel::Row> rows{
        { .id = 5, .name = "Eve", .party = 0xffff, .sort_key = -2 }, { .id = 4, .name = "Dan", .party = 2, .sort_key = 1 },
        { .id = 3, .name = "Cid", .party = 1 },                   { .id = 2, .name = "Zed", .party = 2, .sort_key = 7 },
        { .id = 1, .name = "amy", .party = 0xffff },               { .id = 6, .name = "Bob", .party = 0xffff },
    };
    PartyPanel::regroup(rows, 99, own_word, by_sort_key);     // our own record isn't among them (Town::party_rows)
    std::vector<std::uint32_t> order;
    for (const auto& row : rows) order.push_back(row.id);
    return order;
}

int print_file(const char* path) {
    std::ifstream file(path);
    if (!file) {
        std::fprintf(stderr, "can't read %s\n", path);
        return 1;
    }
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        int case_id = 0, local = 0, by_key = 0, count = 0;
        if (!(fields >> case_id >> local >> by_key >> count)) continue;
        std::vector<PartyPanel::Row> rows(std::size_t(count > 0 ? count : 0));
        for (auto& row : rows) {
            std::string name;
            fields >> row.id >> row.party >> row.sort_key >> name;
            if (name != "-")
                for (std::size_t at = 0; at + 1 < name.size(); at += 2) row.name.push_back(char(std::stoi(name.substr(at, 2), nullptr, 16)));
        }
        std::printf("%d", case_id);
        for (const auto id : regrouped(rows, std::uint32_t(local), by_key != 0)) std::printf(" %d", int(id));
        std::printf("\n");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) return print_file(argv[1]);
    struct Record { std::uint32_t id; int party, sort_key; const char* name; };
    struct Case {
        int local, by_key, count;
        std::array<Record, 8> records;
        std::array<int, 8> after;
    };
    static const Case cases[]{
#include "party_regroup_cases.inc"
    };
    int differ = 0;
    for (const auto& sample : cases) {
        std::vector<PartyPanel::Row> rows;
        for (int index = 0; index < sample.count; ++index) {
            const auto& record = sample.records[std::size_t(index)];
            rows.push_back({ .id = record.id, .name = record.name, .party = record.party, .sort_key = record.sort_key });
        }
        const auto got = regrouped(rows, std::uint32_t(sample.local), sample.by_key != 0);
        bool same = got.size() == std::size_t(sample.count);
        for (std::size_t index = 0; same && index < got.size(); ++index) same = int(got[index]) == sample.after[index];
        if (!same) {
            std::printf("differ: local %d by_key %d, %d records\n", sample.local, sample.by_key, sample.count);
            ++differ;
        }
    }
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    assert(differ == 0);
    assert((ids(0xffff) == std::vector<std::uint32_t>{ 3, 4, 2, 6, 5, 1 }));
    assert((ids(2) == std::vector<std::uint32_t>{ 4, 2, 3, 6, 5, 1 }));
    assert((ids(1) == std::vector<std::uint32_t>{ 3, 4, 2, 6, 5, 1 }));
    // 7 before 1; the 0xffff group is taken backwards (6, 1, 5), so the two 0s come 6 then 1, then -2.
    assert((ids(0xffff, true) == std::vector<std::uint32_t>{ 3, 2, 4, 6, 1, 5 }));
    // As NetGame keeps it: three players join (S->C 0x5b puts each first, FUN_0047a6f0, then the
    // regroup), all at sort key 0, no party; then two regroups (0x65s). The ties trade places each time.
    {
        std::vector<PartyPanel::Row> roster;
        const auto regroup = [&roster] {
            PartyPanel::regroup(roster, 99, 0xffff, true);
            std::vector<std::uint32_t> order;
            for (const auto& row : roster) order.push_back(row.id);
            return order;
        };
        const auto join = [&](std::uint32_t id) {
            roster.insert(roster.begin(), PartyPanel::Row{ .id = id, .party = 0xffff });
            return regroup();
        };
        assert((join(1) == std::vector<std::uint32_t>{ 1 }));
        assert((join(2) == std::vector<std::uint32_t>{ 1, 2 }));      // (2, 1) taken backwards
        assert((join(3) == std::vector<std::uint32_t>{ 2, 1, 3 }));   // (3, 1, 2) backwards
        assert((regroup() == std::vector<std::uint32_t>{ 3, 1, 2 }));
        assert((regroup() == std::vector<std::uint32_t>{ 2, 1, 3 }));
    }
    // NetGame's PartyRoster: the records are the roster's own, apart from the world's units, so a
    // player whose unit the host removes (0x0a, FUN_0045cc10) keeps its row, party and place
    // until 0x5c. join puts a new record first (FUN_0047a6f0); a known one stays where it is.
    {
        d2d::client::PartyRoster roster;
        const auto order = [&roster] {
            std::vector<std::uint32_t> out;
            for (const auto& record : roster.records) out.push_back(record.id);
            return out;
        };
        for (const std::uint32_t id : { 1u, 2u, 3u }) {
            auto& record = roster.join(id);
            record.name = std::string(1, char('a' + id));
            record.party = id == 2 ? 0xffff : 4;
            roster.regroup(99, false);
        }
        assert((order() == std::vector<std::uint32_t>{ 1, 3, 2 }));    // party 4 ("b", "d"), then no party
        roster.join(3).level = 9;                                         // a known record: kept, not moved
        assert((order() == std::vector<std::uint32_t>{ 1, 3, 2 }) && roster.find(3)->level == 9);
        roster.find(2)->party = 4;                                        // 0x8d, then the regroup
        roster.regroup(99, false);
        assert((order() == std::vector<std::uint32_t>{ 1, 2, 3 }));
        roster.regroup(99, false);                                        // nothing comes from the units: the same rows
        assert((order() == std::vector<std::uint32_t>{ 1, 2, 3 }) && roster.find(2)->name == "c" && roster.find(2)->party == 4);
        roster.join(99).party = 0xffff;                                   // ours: no party, so our record first
        roster.regroup(99, false);
        assert((order() == std::vector<std::uint32_t>{ 99, 1, 2, 3 }));
        roster.leave(2);                                                  // 0x5c
        roster.regroup(99, false);
        assert((order() == std::vector<std::uint32_t>{ 99, 1, 3 }) && roster.find(2) == nullptr);
    }
    std::puts("party order ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
