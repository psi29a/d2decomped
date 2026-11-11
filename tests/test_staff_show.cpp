// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The Horadric Staff's light show (rules/staff_show.hpp): missile 338's
// client function 31 (FUN_004d6820) over its 440 frames, and the heights
// its motion record gives the staff and the lightning (FUN_004da350).
// No game data needed.
#include <rules.hpp>
#include <staff_show.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    namespace show = d2d::rules::staff_show;
    // Over the staff's life (remaining 440 down to 1): seven beams at 325,
    // 300, ... 175, in the table's order; one shake, at 150; lightning on
    // the even frames under 165; dust on every frame under 150.
    int beams = 0, shakes = 0, lightning = 0, dust = 0, first_lightning = 0, first_dust = 0;
    for (int remaining = show::kStaffRange; remaining > 0; --remaining) {
        const auto step = show::step(remaining);
        if (step.beam >= 0) {
            assert(step.beam == beams && remaining == show::kFirstBeam - beams * show::kBeamStep);
            ++beams;
        }
        if (step.shake) { assert(remaining == 150); ++shakes; }
        if (step.lightning) { if (!first_lightning) first_lightning = remaining; ++lightning; }
        if (step.dust) { if (!first_dust) first_dust = remaining; ++dust; }
    }
    assert(beams == 7 && shakes == 1 && first_lightning == 164 && lightning == 82 && first_dust == 149 && dust == 149);
    assert(show::kBeams[0].dx == 3 && show::kBeams[0].dy == -2 && show::kBeams[0].dir == 4 && show::kBeams[6].dir == 31);
    assert(show::kShakeStrength == 25 && show::kShakeHoldMs == 4000);
    // On the staff's own clock: the beams from frame 115 to 265, the shake at 290.
    assert(show::kStaffRange - show::kFirstBeam == 115 && show::kStaffRange - (show::kFirstBeam - 6 * show::kBeamStep) == 265);
    assert(show::kStaffRange - show::kShakeAt == 290);

    // The dust's spread: across 1..5, down -5..4, two draws on the seed.
    d2d::rules::Rng seed(12345);
    d2d::rules::Rng copy = seed;
    for (int draw = 0; draw < 200; ++draw) {
        const auto spot = show::dust_target(seed);
        assert(spot.dx >= 1 && spot.dx <= 5 && spot.dy >= -5 && spot.dy <= 4);
    }
    const int across = 1 + copy(5), down = copy(10) - 5;
    d2d::rules::Rng again(12345);
    const auto first = show::dust_target(again);
    assert(first.dx == across && first.dy == down);

    // Heights: the staff 40 up, a quarter more a frame, 70 from frame 120;
    // the lightning 70 up, one less a frame, 0 from frame 70.
    static_assert(show::staff_height(0) == 40.f && show::staff_height(4) == 41.f && show::staff_height(119) == 69.75f);
    static_assert(show::staff_height(120) == 70.f && show::staff_height(400) == 70.f);
    static_assert(show::lightning_height(0) == 70.f && show::lightning_height(30) == 40.f && show::lightning_height(70) == 0.f && show::lightning_height(90) == 0.f);
    std::puts("staff_show: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_staff_show: %s\n", error.what());
    return 1;
}
