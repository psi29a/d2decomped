// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// ReEquip, the weapon wielded before put back on (FUN_0057ff70) against
// stock 1.14d's own, run by tools/emu/reequip.py: rules::reequip_slot. No
// game data needed at runtime. With a file argument (reequip.py's `game`
// output, or its `gen` cases) it prints each line with d2d's answer
// instead, to diff.
#include <ammo.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using namespace d2d::rules;

struct Case { std::uint32_t kept; int of_game, page, ours, found, slot, spent; };
struct Answer {
    int slot = -1;
    std::uint32_t kept_after = 0;
    friend bool operator==(const Answer&, const Answer&) = default;
};

// The slot it goes on, -1 none; the kept id is cleared whenever one was kept
// (FUN_006233a0 at 0x58001d), so it is 0 after every case. The spent slot
// doesn't count.
Answer answer(const Case& row) {
    const int slot = reequip_slot({ .id = std::int32_t(row.kept), .of_game = row.of_game != 0, .page = row.page, .ours = row.ours != 0,
                                    .found_slot = row.found != 0 ? row.slot : -1 });
    return { slot, 0 };
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        if (!input) {
            std::fprintf(stderr, "test_reequip: cannot open %s\n", argv[1]);
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
            if (!(fields >> id >> row.kept >> row.of_game >> row.page >> row.ours >> row.found >> row.slot >> row.spent)) {
                std::fprintf(stderr, "test_reequip: %s:%d: not a case line (want 8 numbers)\n", argv[1], line_number);
                return 1;
            }
            const auto got = answer(row);
            std::printf("%lld\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%u\n", id, row.kept, row.of_game, row.page, row.ours, row.found, row.slot, row.spent, got.slot,
                        got.kept_after);
        }
        if (input.bad() || !input.eof()) {
            std::fprintf(stderr, "test_reequip: %s: read error after line %d\n", argv[1], line_number);
            return 1;
        }
        return 0;
    }

    struct Row { Case in; Answer out; };
    const Row cases[] = {
#include "reequip_cases.inc"
    };
    for (const auto& row : cases) {
        const auto got = answer(row.in);
        if (got != row.out)
            std::fprintf(stderr, "FAIL: kept %u of_game %d page %d ours %d found %d slot %d spent %d: got %d %u, want %d %u\n", row.in.kept, row.in.of_game,
                         row.in.page, row.in.ours, row.in.found, row.in.slot, row.in.spent, got.slot, got.kept_after, row.out.slot, row.out.kept_after);
        assert(got == row.out);
    }
    std::printf("OK: %zu ReEquips as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_reequip: %s\n", error.what());
    return 1;
}
