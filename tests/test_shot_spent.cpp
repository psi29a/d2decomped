// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A shot spends one of the hand's ammunition (FUN_0056c310) against stock
// 1.14d's own, run by tools/emu/shot_spent.py: rules::shot_spent. No game
// data needed at runtime. With a file argument (shot_spent.py's `game`
// output, or its `gen` cases) it prints each line with d2d's answer
// instead, to diff.
#include <ammo.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using namespace d2d::rules;

struct Case { int none, quantity, durability, most; };

// A null item (FUN_0056c310's first test): nothing written, 0 answered.
ShotSpent answer(const Case& row) { return row.none != 0 ? ShotSpent{ row.quantity, row.durability, 0 } : shot_spent(row.quantity, row.most); }

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        if (!input) {
            std::fprintf(stderr, "test_shot_spent: cannot open %s\n", argv[1]);
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
            if (!(fields >> id >> row.none >> row.quantity >> row.durability >> row.most)) {
                std::fprintf(stderr, "test_shot_spent: %s:%d: not a case line (want 5 numbers)\n", argv[1], line_number);
                return 1;
            }
            const auto got = answer(row);
            std::printf("%lld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", id, row.none, row.quantity, row.durability, row.most, got.quantity, got.durability, got.answer);
        }
        if (input.bad() || !input.eof()) {
            std::fprintf(stderr, "test_shot_spent: %s: read error after line %d\n", argv[1], line_number);
            return 1;
        }
        return 0;
    }

    struct Row { Case in; ShotSpent out; };
    const Row cases[] = {
#include "shot_spent_cases.inc"
    };
    for (const auto& row : cases) {
        const auto got = answer(row.in);
        if (got != row.out)
            std::fprintf(stderr, "FAIL: none %d quantity %d durability %d most %d: got %d %d %d, want %d %d %d\n", row.in.none, row.in.quantity, row.in.durability, row.in.most,
                         got.quantity, got.durability, got.answer, row.out.quantity, row.out.durability, row.out.answer);
        assert(got == row.out);
    }
    std::printf("OK: %zu shots spent as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_shot_spent: %s\n", error.what());
    return 1;
}
