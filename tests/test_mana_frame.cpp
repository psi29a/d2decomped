// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A player's mana a frame (FUN_005806f0 up to 0x58079e, uncapped), with
// state 0x55 (nomanaregen) off and on, against stock 1.14d's own, run by
// tools/emu/mana_frame.py: rules::mana_per_frame. No game data needed at
// runtime. With a file argument (mana_frame.py's `game` output, or its
// `gen` cases) it prints each line with d2d's amount instead, to diff.
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <string>

namespace {

std::int64_t amount(int state, std::int64_t max_mana, int regen, int bonus, int flat) {
    return d2d::rules::mana_per_frame(max_mana, regen, bonus, flat, state != 0);
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        long long id = 0, max_mana = 0;
        int state = 0, regen = 0, bonus = 0, flat = 0;
        std::string rest;
        while (input >> id >> state >> max_mana >> regen >> bonus >> flat) {
            std::getline(input, rest);
            std::printf("%lld\t%d\t%lld\t%d\t%d\t%d\t%lld\n", id, state, max_mana, regen, bonus, flat, static_cast<long long>(amount(state, max_mana, regen, bonus, flat)));
        }
        return 0;
    }
    struct Case { int state; long long max_mana; int regen, bonus, flat; long long amount; };
    const Case cases[] = {
#include "mana_frame_cases.inc"
    };
    for (const auto& row : cases) {
        const auto got = amount(row.state, row.max_mana, row.regen, row.bonus, row.flat);
        if (got != row.amount)
            std::fprintf(stderr, "FAIL: state %d max %lld regen %d bonus %d flat %d: d2d %lld, game.exe %lld\n", row.state, row.max_mana, row.regen, row.bonus, row.flat,
                         static_cast<long long>(got), row.amount);
        assert(got == row.amount);
    }
    std::printf("OK: %zu frames' mana as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_mana_frame: %s\n", error.what());
    return 1;
}
