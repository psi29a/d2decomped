// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A skill's aurafilter on a monster (rules/aura_filter.hpp, FUN_0056b3e0),
// with the 1.14d Skills.txt values: a friendly aura (0x12003) never takes
// a monster that isn't the caster's, nor an npc; Holy Fire (0xa783) takes
// the living in a clear line; Sanctuary (0xe786) undead that aren't
// bosses; Holy Freeze (0x4a283) no prime evil. Then game.exe's answers
// (tests/aura_filter_cases.inc, tools/emu/aura_filter.py).
//
//   test_aura_filter [cases.tsv]   with a case file, prints each case and d2d's answer
#include <aura_filter.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

using d2d::rules::aura_filter_takes;
using d2d::rules::aura_walk_takes;
using d2d::rules::AuraFilterMonster;

namespace {

struct Case { std::uint32_t filter; int mode, undead, boss, prime, ally, npc, flag4, flag8, town, melee, just_hit, line, taken; };

const Case kCases[] = {
#include "aura_filter_cases.inc"
};

AuraFilterMonster monster_of(const Case& one) {
    return { .dead = one.mode == 2, .dying = one.mode == 1, .undead = one.undead != 0, .boss = one.boss != 0, .prime_evil = one.prime != 0,
             .ally = one.ally != 0, .npc = one.npc != 0, .unit_flag_4 = one.flag4 != 0, .unit_flag_8 = one.flag8 != 0, .in_town = one.town != 0,
             .in_melee = one.melee != 0, .just_hit = one.just_hit != 0, .clear_line = one.line != 0 };
}

int print_cases(const char* path) {
    std::ifstream input(path);
    if (!input) {
        std::fprintf(stderr, "test_aura_filter: can't read %s\n", path);
        return 1;
    }
    std::string text;
    int line_number = 0;
    while (std::getline(input, text)) {
        ++line_number;
        std::istringstream fields(text);
        long long seq = 0;
        long long filter = 0;
        Case one{};
        if (!(fields >> seq >> filter >> one.mode >> one.undead >> one.boss >> one.prime >> one.ally >> one.npc >> one.flag4 >> one.flag8 >> one.town
                     >> one.melee >> one.just_hit >> one.line)) {
            std::fprintf(stderr, "test_aura_filter: %s:%d: bad case\n", path, line_number);
            return 1;
        }
        one.filter = std::uint32_t(filter);
        std::printf("%lld %lld %d %d %d %d %d %d %d %d %d %d %d %d %d\n", seq, filter, one.mode, one.undead, one.boss, one.prime, one.ally, one.npc,
                    one.flag4, one.flag8, one.town, one.melee, one.just_hit, one.line, int(aura_filter_takes(one.filter, monster_of(one))));
    }
    if (input.bad() || !input.eof()) {
        std::fprintf(stderr, "test_aura_filter: %s: read error after line %d\n", path, line_number);
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) return print_cases(argv[1]);
    constexpr std::uint32_t kMight = 0x12003, kMeditation = 0x12001, kHolyFire = 0xa783, kConviction = 0xa583, kHolyFreeze = 0x4a283, kSanctuary = 0xe786;
    constexpr std::uint32_t kRedemption = 0x1102, kStaticField = 0x8783;
    const AuraFilterMonster zombie{ .undead = true };
    const AuraFilterMonster fallen{};
    const AuraFilterMonster corpse{ .dead = true };
    const AuraFilterMonster dying{ .dying = true };
    const AuraFilterMonster diablo{ .boss = true, .prime_evil = true };
    const AuraFilterMonster pet{ .ally = true };

    // Friendly: an ally only (0x10000), never a wild monster nor an npc;
    // Meditation's 0x12001 has no monster bit at all.
    assert(!aura_filter_takes(kMight, fallen) && !aura_filter_takes(kMight, zombie) && !aura_filter_takes(kMight, diablo));
    assert(aura_filter_takes(kMight, pet));
    assert(!aura_filter_takes(kMight, AuraFilterMonster{ .ally = true, .npc = true }));
    assert(!aura_filter_takes(kMeditation, pet));

    // Enemy: the living, not an ally.
    for (const auto filter : { kHolyFire, kConviction }) {
        assert(aura_filter_takes(filter, fallen) && aura_filter_takes(filter, zombie) && aura_filter_takes(filter, diablo));
        assert(!aura_filter_takes(filter, corpse) && !aura_filter_takes(filter, dying) && !aura_filter_takes(filter, pet));
        // Unit flags 4 and 8 (0x80, 0x400), and out of town (0x100).
        assert(!aura_filter_takes(filter, AuraFilterMonster{ .unit_flag_4 = false }) && !aura_filter_takes(filter, AuraFilterMonster{ .unit_flag_8 = false }));
        assert(!aura_filter_takes(filter, AuraFilterMonster{ .in_town = true }));
    }
    // Holy Fire's 0x200: a clear line to it; Conviction (0xa583) has none.
    const AuraFilterMonster walled{ .clear_line = false };
    assert(!aura_filter_takes(kHolyFire, walled) && aura_filter_takes(kConviction, walled));
    assert(!aura_filter_takes(kStaticField, walled) && aura_filter_takes(kStaticField, fallen));
    // Sanctuary: undead (0x4), no boss (0x4000).
    assert(aura_filter_takes(kSanctuary, zombie) && !aura_filter_takes(kSanctuary, fallen));
    assert(!aura_filter_takes(kSanctuary, AuraFilterMonster{ .undead = true, .boss = true }));
    // Holy Freeze: no prime evil (0x40000); a boss that isn't one is taken.
    assert(!aura_filter_takes(kHolyFreeze, diablo) && aura_filter_takes(kHolyFreeze, AuraFilterMonster{ .boss = true }));
    assert(aura_filter_takes(kHolyFreeze, fallen));
    // Corpses only (0x1000): mode 0xc, not one still dying (mode 0).
    assert(aura_filter_takes(kRedemption, corpse) && !aura_filter_takes(kRedemption, dying) && !aura_filter_takes(kRedemption, fallen));
    // 0x20000 (in melee) and 0x80000 (not in state 0x56), no Skills.txt row's.
    assert(!aura_filter_takes(0x20002, fallen) && aura_filter_takes(0x20002, AuraFilterMonster{ .in_melee = true }));
    assert(aura_filter_takes(0x80002, fallen) && !aura_filter_takes(0x80002, AuraFilterMonster{ .just_hit = true }));

    // The walk: 0 is FUN_0056b7e0's 0x583 (FUN_0056b3e0 alone takes nothing);
    // 0x2000 passes over a town room.
    assert(aura_walk_takes(0, fallen) && !aura_walk_takes(0, corpse) && !aura_filter_takes(0, fallen));
    assert(!aura_walk_takes(0x2002, AuraFilterMonster{ .in_town = true }) && aura_filter_takes(0x2002, AuraFilterMonster{ .in_town = true }));

    // game.exe's answers.
    int checked = 0;
    for (const auto& one : kCases) {
        if (aura_filter_takes(one.filter, monster_of(one)) != (one.taken != 0)) {
            std::fprintf(stderr, "test_aura_filter: filter %#x mode %d: game.exe %d\n", one.filter, one.mode, one.taken);
            return 1;
        }
        ++checked;
    }
    std::printf("ok: %d cases from game.exe\n", checked);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_aura_filter: %s\n", error.what());
    return 1;
}
