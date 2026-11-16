// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A hand's item out of ammunition (FUN_00580030) against stock 1.14d's
// own, run by tools/emu/ammo_out.py: rules::spent_fate and
// rules::reload_pick. No game data needed at runtime. With a file argument
// (ammo_out.py's `game` output, or its `gen` cases) it prints each line
// with d2d's answer instead, to diff.
#include <ammo.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fstream>
#include <span>
#include <sstream>
#include <string>

namespace {

using namespace d2d::rules;

struct Case { int stackable, throwable, throw_stat, quantity, weapon, magic, broken, reload, reequip, slot_full, wears, pack_count; std::array<int, 3> pack; };
struct Answer {
    int fate, zeroed, left, asked, took, reequip;
    friend bool operator==(const Answer&, const Answer&) = default;
};

// game.exe asks the pack for one item after another (FUN_0063cd80) until
// its pick or the pack's end; the pick takes the slot when that is empty
// and FUN_00562a30 puts it on. ReEquip follows unless it did.
Answer answer(const Case& row) {
    const auto spent = spent_fate({ row.stackable != 0, row.throwable != 0, row.throw_stat != 0, row.quantity, row.weapon != 0, row.magic != 0, row.broken != 0,
                                    row.reload != 0, row.reequip != 0 });
    Answer out{ 0, 0, 0, 0, -1, 0 };
    if (spent.fate == SpentFate::emptied) { out.fate = 1; out.zeroed = 1; }
    if (spent.fate == SpentFate::breaks) out.fate = 2;
    if (spent.fate != SpentFate::leaves) return out;
    out.fate = 3; out.left = 1;
    if (spent.reload) {
        std::array<PackItem, 3> pack{};
        for (std::size_t at = 0; at < 3; ++at) pack[at] = { (row.pack[at] & 1) != 0, (row.pack[at] & 2) != 0 };
        const int pick = reload_pick(std::span(pack).first(std::size_t(row.pack_count)));
        out.asked = pick >= 0 ? pick + 1 : row.pack_count + 1;
        if (pick >= 0 && !row.slot_full && row.wears) out.took = pick;
    }
    out.reequip = spent.reequip && out.took < 0 ? 1 : 0;
    return out;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        if (!input) {
            std::fprintf(stderr, "test_ammo_out: cannot open %s\n", argv[1]);
            return 1;
        }
        std::string line;
        int line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            std::istringstream fields(line);
            long long id = 0;
            Case row{};
            if (!(fields >> id >> row.stackable >> row.throwable >> row.throw_stat >> row.quantity >> row.weapon >> row.magic >> row.broken >> row.reload >> row.reequip
                         >> row.slot_full >> row.wears >> row.pack_count >> row.pack[0] >> row.pack[1] >> row.pack[2])) {
                std::fprintf(stderr, "test_ammo_out: %s:%d: not a case line (want 16 numbers)\n", argv[1], line_number);
                return 1;
            }
            const auto got = answer(row);
            std::printf("%lld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", id, row.stackable, row.throwable, row.throw_stat,
                        row.quantity, row.weapon, row.magic, row.broken, row.reload, row.reequip, row.slot_full, row.wears, row.pack_count, row.pack[0], row.pack[1],
                        row.pack[2], got.fate, got.zeroed, got.left, got.asked, got.took, got.reequip);
        }
        if (input.bad() || !input.eof()) {
            std::fprintf(stderr, "test_ammo_out: %s: read error after line %d\n", argv[1], line_number);
            return 1;
        }
        return 0;
    }
    struct Row { Case in; Answer out; };
    const Row cases[] = {
#include "ammo_out_cases.inc"
    };
    for (const auto& row : cases) {
        const auto got = answer(row.in);
        if (!(got == row.out))
            std::fprintf(stderr, "FAIL: stackable %d throwable %d stat 0x7d %d quantity %d weapon %d magic %d broken %d reload %d reequip %d slot full %d wears %d pack %d (%d %d %d): "
                                 "d2d %d %d %d %d %d %d, game.exe %d %d %d %d %d %d\n",
                         row.in.stackable, row.in.throwable, row.in.throw_stat, row.in.quantity, row.in.weapon, row.in.magic, row.in.broken, row.in.reload, row.in.reequip,
                         row.in.slot_full, row.in.wears, row.in.pack_count, row.in.pack[0], row.in.pack[1], row.in.pack[2], got.fate, got.zeroed, got.left, got.asked, got.took,
                         got.reequip, row.out.fate, row.out.zeroed, row.out.left, row.out.asked, row.out.took, row.out.reequip);
        assert(got == row.out);
    }
    std::printf("OK: %zu spent items as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_ammo_out: %s\n", error.what());
    return 1;
}
