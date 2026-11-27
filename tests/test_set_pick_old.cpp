// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A classic item's set pick (FUN_005c2740, tail LAB_005c276a) against game.exe's own answers
// (tools/emu/set_pick_old.py inc): the durabilities x2 at most 255, the start rand(n) on the item's
// seed, the last row's find winning, and the seed put back to (its low, 666) when none holds the code.
// A find clears the item's flag 0x10 (identified, FUN_006280d0(item, 0x10, 0)), checked through generate_item.
#include <item_gen.hpp>
#include <rules.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace d2d::rules;

namespace {

struct Case {
    const char* sets;
    const char* code;
    std::uint32_t low, high;
    int durability, max_durability, picked, durability_after, max_after;
    std::uint32_t low_after, high_after;
};

constexpr Case kCases[] = {
#include "set_pick_old_cases.inc"
};

// "v:abc,def;v:" -> Sets rows by version, SetItems rows numbered in order (each member's set and code).
void load(Tables& tables, const std::string& text) {
    std::stringstream rows(text);
    std::string row;
    int set = 0;
    while (std::getline(rows, row, ';')) {
        const auto colon = row.find(':');
        SetBonus bonus;
        bonus.version = std::atoi(row.substr(0, colon).c_str());
        tables.set_bonuses.push_back(std::move(bonus));
        std::stringstream members(row.substr(colon + 1));
        std::string code;
        while (std::getline(members, code, ',')) {
            Special member;
            member.code = code;
            member.set = set;
            member.member = 0;
            tables.sets.push_back(std::move(member));
        }
        ++set;
    }
}

}   // namespace

int main() try {
    int failed = 0;
    for (const auto& one : kCases) {
        Tables tables;
        load(tables, one.sets);
        Rng rng;
        rng.low = one.low;
        rng.high = one.high;
        int durability = one.durability, max_durability = one.max_durability;
        const int picked = classic_set_pick(tables, one.code, rng, durability, max_durability);
        if (picked != one.picked || durability != one.durability_after || max_durability != one.max_after || rng.low != one.low_after ||
            rng.high != one.high_after) {
            if (failed++ < 5) std::fprintf(stderr, "case \"%s\" %s: got %d %d %d %u %u\n", one.sets, one.code, picked, durability, max_durability, rng.low, rng.high);
        }
    }
    // A classic set made by generate_item: found, its identified flag is cleared; a normal item keeps it.
    Tables tables;
    tables.item_info["cap"].type = "helm";
    tables.item_info["cap"].kind = 1;
    tables.item_base["cap"].level = 1;
    tables.item_base["cap"].durability = 30;
    tables.set_bonuses.resize(1);
    Special member;
    member.code = "cap";
    member.set = 0;
    member.member = 0;
    tables.sets.push_back(std::move(member));
    Rng unit_seed{ 7919 }, item_seed{ 1 }, normal_seed{ 1 }, normal_unit{ 7919 };
    const auto set_item = generate_item(tables, "cap", 5, 5, item_seed, &unit_seed, nullptr, false, 0, 0, true);
    const auto normal = generate_item(tables, "cap", 5, 2, normal_seed, &normal_unit, nullptr, false, 0, 0, true);
    if (set_item.quality != 5 || set_item.identified || !normal.identified) { std::fprintf(stderr, "identified flag: set %d normal %d\n", set_item.identified, normal.identified); ++failed; }
    if (failed) { std::fprintf(stderr, "%d cases differ\n", failed); return 1; }
    std::printf("OK: %zu classic set picks match game.exe (FUN_005c2740)\n", sizeof(kCases) / sizeof(kCases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_set_pick_old: %s\n", error.what());
    return 1;
}
