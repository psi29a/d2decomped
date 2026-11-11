// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's burner (missile do 5) on a foe is a record like any hit (FUN_005ad730 -> FUN_0057c6c0): its field in 256ths
// loses the flat MDR scaled by DamageRate / 1024 (FUN_0057c1e0, 0x57c291), then the resistance, and the life takes the record
// total as it is (0057c852): the fraction stays (Foe::frac, the blow's frac), no draw rounds it. Its record has result
// flag 1 alone (no GetHit: FUN_005ad730), so no get-hit; pSrvDmgFunc 3 (FUN_005ad220) makes it a soft hit (0x4000) when
// a draw's low 7 bits are under dParam1: the struck sound. Its hit class goes on the unit either way (flag 1).

#include <ai.hpp>
#include <combat.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    GameData::MissileInfo burner;
    burner.srv_do = 5;
    burner.damage_rate = 41;
    Level open;
    open.ds1 = d2d::ds1::Map(8, 8, 1, 1);           // 40 x 40 subtiles, nothing blocked
    open.walk.assign(40 * 40, 0);
    const auto burn = [&](int mdr, Foe* out = nullptr) {
        Missile fire;
        fire.info = &burner;
        fire.x = fire.y = 4.f;
        fire.born = 0; fire.dies = 100000;
        fire.row = d2d::rules::MissileDamage{ .etype = 0, .elo = 0x1a0, .ehi = 0x1a0 };
        std::vector<Missile> missiles;
        missiles.push_back(std::move(fire));
        std::array<Foe, 1> foes{};
        foes[0].x = foes[0].y = 4.f;
        foes[0].fighter.mdr = mdr;
        d2d::rules::Rng rng(7);
        const auto before = rng;
        missiles_update(open, missiles, foes, rng, 4000, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                        [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
        assert(burner.dmg_func == 3 || (rng.low == before.low && rng.high == before.high));   // no rounding draw
        assert(foes[0].blows.size() == 1 && foes[0].blows[0].frac == foes[0].frac && foes[0].blows[0].damage == foes[0].damage);
        assert(!foes[0].blows[0].landed && foes[0].hits == 0);   // flag 1 alone: no get-hit test
        if (out) *out = foes[0];
        return (foes[0].damage << 8) + foes[0].frac;
    };
    assert(burn(0) == 0x1a0);                       // 1 point and 0xa0 kept
    assert(burn(3) == 0x1a0 - 3 * 256 * 41 / 1024);   // MDR 3 at DamageRate 41: 30 of the 256ths off
    burner.dmg_func = 3;
    burner.hit_class = 0x24;
    Foe struck;
    burner.dmg_par1 = 128;                          // every draw's low 7 bits are under 128: a soft hit
    (void)burn(0, &struck);
    assert(struck.soft_hits == 1 && struck.hit_class == 0x24);
    burner.dmg_par1 = 0;                            // none under 0
    (void)burn(0, &struck);
    assert(struck.soft_hits == 0 && struck.hit_class == 0x24);   // flag 1: every burn puts its class on the unit (FUN_0057cee0)
    std::puts("OK: a burn keeps its 256ths (0057c852), its MDR at DamageRate / 1024; no get-hit, dParam1 soft hits");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_burn_fraction: %s\n", error.what());
    return 1;
}
