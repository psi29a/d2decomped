// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A skill's aurafilter on a monster (rules/aura_filter.hpp, FUN_0056b3e0),
// with the 1.14d Skills.txt values: a friendly aura (0x12003) never takes
// a monster that isn't the caster's; Holy Fire (0xa783) takes the living;
// Sanctuary (0xe786) undead that aren't bosses; Holy Freeze (0x4a283) no
// prime evil.
#include <aura_filter.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

using d2d::rules::aura_filter_takes;
using d2d::rules::AuraFilterMonster;

int main() {
    constexpr std::uint32_t kMight = 0x12003, kMeditation = 0x12001, kHolyFire = 0xa783, kConviction = 0xa583, kHolyFreeze = 0x4a283, kSanctuary = 0xe786;
    const AuraFilterMonster zombie{ .undead = true };
    const AuraFilterMonster fallen{};
    const AuraFilterMonster corpse{ .dead = true };
    const AuraFilterMonster diablo{ .boss = true, .prime_evil = true };
    const AuraFilterMonster pet{ .ally = true };

    // Friendly: an ally only (0x10000), never a wild monster; Meditation's
    // 0x12001 has no monster bit at all.
    assert(!aura_filter_takes(kMight, fallen) && !aura_filter_takes(kMight, zombie) && !aura_filter_takes(kMight, diablo));
    assert(aura_filter_takes(kMight, pet));
    assert(!aura_filter_takes(kMeditation, pet));

    // Enemy: the living, not an ally.
    for (const auto filter : { kHolyFire, kConviction }) {
        assert(aura_filter_takes(filter, fallen) && aura_filter_takes(filter, zombie) && aura_filter_takes(filter, diablo));
        assert(!aura_filter_takes(filter, corpse) && !aura_filter_takes(filter, pet));
    }
    // Sanctuary: undead (0x4), no boss (0x4000).
    assert(aura_filter_takes(kSanctuary, zombie) && !aura_filter_takes(kSanctuary, fallen));
    assert(!aura_filter_takes(kSanctuary, AuraFilterMonster{ .undead = true, .boss = true }));
    // Holy Freeze: no prime evil (0x40000); a boss that isn't one is taken.
    assert(!aura_filter_takes(kHolyFreeze, diablo) && aura_filter_takes(kHolyFreeze, AuraFilterMonster{ .boss = true }));
    assert(aura_filter_takes(kHolyFreeze, fallen));

    // 0 is FUN_0056b7e0's 0x583: the living, monsters included.
    assert(aura_filter_takes(0, fallen) && !aura_filter_takes(0, corpse));
    // Corpses only (0x1000): the dead.
    assert(aura_filter_takes(0x1002, corpse) && !aura_filter_takes(0x1002, fallen));
    std::puts("ok");
    return 0;
}
