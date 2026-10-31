// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Whether a sound starts against the playing ones, and the ducks
// (rules/sound_start.hpp; game.exe FUN_004b9a00 / FUN_004ba020).
#include <sound_mix.hpp>
#include <sound_start.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using d2d::rules::SoundInstance;
using d2d::rules::SoundRule;

int main() {
    constexpr std::uint64_t kHero = 1, kZombie = 2;
    // Compound: a playing member of the group started within Compound ticks takes the play.
    const std::vector<SoundInstance> hits{ { 100, 100, kZombie, 40, false }, { 101, 100, kHero, 45, false } };
    const SoundRule hit{ 102, 100, 8, false, false };
    assert(d2d::rules::compound_into(hits, hit, 48) == 0u);             // 8 ticks after the first
    assert(d2d::rules::compound_into(hits, hit, 49) == 1u);             // the first is 9 old: the second
    assert(!d2d::rules::compound_into(hits, hit, 54));                  // both too old
    assert(!d2d::rules::compound_into(hits, { 102, 100, 0, false, false }, 41));   // Compound 0: never
    assert(d2d::rules::compound_into(hits, { 102, 100, -1, false, false }, 9000) == 0u);   // < 0: any age
    assert(!d2d::rules::compound_into(hits, hit, 39));                  // not started yet (unsigned)
    const std::vector<SoundInstance> stopping{ { 100, 100, kZombie, 40, true } };
    assert(!d2d::rules::compound_into(stopping, hit, 41));              // one stopping doesn't take it

    // Defer: dropped while its group plays on the same unit; another unit's doesn't count.
    const std::vector<SoundInstance> cry{ { 200, 200, kZombie, 10, false }, { 201, 200, kHero, 5, false } };
    const SoundRule defer{ 202, 200, 0, true, false };
    auto clash = d2d::rules::start_clash(cry, defer, kZombie, true);
    assert(clash.other == 0u && !clash.play && !clash.stop_other);
    // No unit: the oldest of the group anywhere, unless it has a position (then it plays over).
    clash = d2d::rules::start_clash(cry, defer, 0, false);
    assert(clash.other == 1u && !clash.play);
    clash = d2d::rules::start_clash(cry, defer, 0, true);
    assert(!clash.other && clash.play);
    // Stop Inst: the old one stops, the new one plays.
    clash = d2d::rules::start_clash(cry, { 202, 200, 0, false, true }, kHero, true);
    assert(clash.other == 1u && clash.play && clash.stop_other);
    // Neither flag: no clash.
    assert(!d2d::rules::start_clash(cry, { 202, 200, 0, false, false }, kZombie, true).other);

    // Speech: any line the unit is saying, whatever group or flags; dropped unless Stop Inst.
    const std::vector<SoundInstance> talk{ { 0xb80, 0xb80, kHero, 3, false }, { 0xc00, 0xc00, kZombie, 2, false } };
    clash = d2d::rules::start_clash(talk, { 0xd00, 0xd00, 0, false, false }, kHero, true);
    assert(clash.other == 0u && !clash.play);
    clash = d2d::rules::start_clash(talk, { 0xd00, 0xd00, 0, false, true }, kHero, true);
    assert(clash.other == 0u && clash.play && clash.stop_other);
    assert(!d2d::rules::start_clash(talk, { 0xd00, 0xd00, 0, false, false }, 3, true).other);   // a silent unit
    assert(!d2d::rules::start_clash(talk, { 0x1231, 0x1231, 0, false, false }, kHero, true).other);   // past speech

    // The ducks: Solo 2 a tick to 70 and back to 100; the menu 5 a tick to 0 and back.
    int solo = 100;
    for (int tick = 0; tick < 20; ++tick) solo = d2d::rules::solo_duck_step(solo, true);
    assert(solo == 70);
    solo = d2d::rules::solo_duck_step(d2d::rules::solo_duck_step(solo, false), false);
    assert(solo == 74);
    int menu = 100;
    for (int tick = 0; tick < 19; ++tick) menu = d2d::rules::menu_duck_step(menu, true);
    assert(menu == 5 && d2d::rules::menu_duck_step(menu, true) == 0 && d2d::rules::menu_duck_step(0, true) == 0);
    assert(d2d::rules::menu_duck_step(98, false) == 100);
    // Applied to the volume: the menu spares cursor, ambience and music rows; Solo spares Solo rows.
    assert(d2d::rules::ducked_volume(200, 500, false, 50, 100) == 100);
    assert(d2d::rules::ducked_volume(200, 0x40, false, 0, 100) == 200);      // scene_* ambience
    assert(d2d::rules::ducked_volume(200, 0x1240, false, 0, 100) == 200);    // music
    assert(d2d::rules::ducked_volume(200, 500, false, 100, 70) == 140);
    assert(d2d::rules::ducked_volume(200, 0xb80, true, 100, 70) == 200);     // the Solo line itself
    assert(d2d::rules::ducked_volume(255, 500, false, 50, 70) == 70 * (50 * 255 / 100) / 100);
    // A world sound's mix (FUN_004dfc20): the ducks on its 0..255 volume first, each step truncated,
    // then distance and pan on that; not a gain multiplier after the mix (255 x .5 x .7 = 89.25).
    using d2d::rules::ducked_mix;
    assert(ducked_mix(255, 1, 0.f, 0.f, 500, false, 50, 70).volume == 88);
    assert(ducked_mix(10, 1, 0.f, 0.f, 500, false, 50, 70).volume == 3);       // 10 -> 5 -> 3, not 3.5
    assert(ducked_mix(255, 1, 0.f, 0.f, 0x40, false, 0, 70).volume == 178);    // ambience: Solo only (255 x 70 / 100)
    assert(ducked_mix(255, 1, 0.f, 0.f, 0xb80, true, 50, 70).volume == 127);   // a Solo row: the menu only
    assert(ducked_mix(255, 1, 0.f, 0.f, 500, false, 100, 100).volume == 255);
    for (const float far_x : { 300.f, 650.f }) {                              // away, and near the far range
        const auto ducked = ducked_mix(200, 1, far_x, 40.f, 500, false, 50, 70);
        const auto plain = d2d::rules::sound_mix_pixels(70 * (50 * 200 / 100) / 100, 1, far_x, 40.f);
        assert(ducked.audible && ducked.volume == plain.volume && ducked.pan == plain.pan);
    }
    assert(!ducked_mix(255, 1, 800.f, 0.f, 500, false, 50, 70).audible);       // past Falloff 1's 700 px
    std::puts("test_sound_start: ok");
    return 0;
}
