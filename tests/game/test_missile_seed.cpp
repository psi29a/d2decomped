// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// pSrvDmgFunc 3 (FUN_005ad220) steps the missile's own seed (unit +0x20, {low, high}: low * 0x6ac690c5 + high),
// and the record gets flag 0x4000 when the new low's low 7 bits are under dParam1 (+0x78). The step is kept
// whether the roll passes or not, and the fight's rng takes no part once the missile has its seed.

#include <ai.hpp>
#include <combat.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    GameData::MissileInfo burner;
    burner.srv_do = 5;
    burner.dmg_func = 3;
    burner.dmg_par1 = 64;
    Level open;
    open.ds1 = d2d::ds1::Map(8, 8, 1, 1);
    open.walk.assign(40 * 40, 0);

    // The expected draw by hand (FUN_005ad220's step), from a seed {low, high}.
    std::uint32_t low = 0x1234, high = 666;
    const auto step = [&] {
        const std::uint64_t product = std::uint64_t(low) * 0x6ac690c5u + high;
        low = std::uint32_t(product);
        high = std::uint32_t(product >> 32);
        return int(low & 0x7f) < burner.dmg_par1;
    };

    Missile fire;
    fire.info = &burner;
    fire.x = fire.y = 4.f;
    fire.born = 0; fire.dies = 100000;
    fire.row = d2d::rules::MissileDamage{ .etype = 0, .elo = 0x100, .ehi = 0x100 };
    fire.seed = d2d::rules::Rng{ 0x1234 };
    fire.seed->high = 666;
    d2d::rules::Rng rng(7);
    for (int frame = 0; frame < 6; ++frame) {
        std::vector<Missile> missiles;
        missiles.push_back(fire);
        std::array<Foe, 1> foes{};
        foes[0].x = foes[0].y = 4.f;
        missiles_update(open, missiles, foes, rng, 4000 + std::uint32_t(frame) * 40, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                        [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
        const bool want = step();
        assert(foes[0].soft_hits == (want ? 1 : 0));
        assert(missiles[0].seed && missiles[0].seed->low == low && missiles[0].seed->high == high);   // the missile's own seed stepped, pass or fail
        fire.seed = missiles[0].seed;
    }

    // A soft hit on a missile of another function never touches its seed.
    burner.dmg_func = 2;
    Missile other = std::move(fire);
    const auto kept = *other.seed;
    assert(!soft_hit(other, rng) && other.seed->low == kept.low && other.seed->high == kept.high);
    std::puts("OK: pSrvDmgFunc 3 steps the missile's own seed (FUN_005ad220), pass or fail");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_missile_seed: %s\n", error.what());
    return 1;
}
