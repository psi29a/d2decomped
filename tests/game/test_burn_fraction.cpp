// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's burner (missile do 5) on a foe is a record like any hit (FUN_005ad730 -> FUN_0057c6c0): its field in 256ths
// loses the flat MDR scaled by DamageRate / 1024 (FUN_0057c1e0, 0x57c291), then the resistance, and the life takes the record
// total as it is (0057c852): the fraction stays (Foe::frac, the blow's frac), no draw rounds it. Its record has result
// flag 1 alone (no GetHit: FUN_005ad730), so no get-hit; pSrvDmgFunc 3 (FUN_005ad220) makes it a soft hit (0x4000) when
// a draw's low 7 bits are under dParam1: the struck sound. Its hit class goes on the unit either way (flag 1).

#include <ai/units.hpp>
#include <combat.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <missile_flight.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
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
    // One flame over two foes burns one a frame (FUN_005ae1f0 returns at the first unit found); two flames burn both.
    {
        const auto flames = [&](int count) {
            std::vector<Missile> missiles;
            for (int k = 0; k < count; ++k) {
                Missile fire;
                fire.info = &burner;
                fire.x = fire.y = 4.f;
                fire.born = 0; fire.dies = 100000;
                fire.row = d2d::rules::MissileDamage{ .etype = 0, .elo = 0x1a0, .ehi = 0x1a0 };
                missiles.push_back(std::move(fire));
            }
            std::array<Foe, 2> two{};
            for (auto& foe : two) foe.x = foe.y = 4.f;
            d2d::rules::Rng rng(7);
            missiles_update(open, missiles, two, rng, 4000, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                            [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
            return int(two[0].blows.size() + two[1].blows.size());
        };
        assert(flames(1) == 1 && flames(2) == 2);
    }
    // Of two foes on one flame the first FUN_00641cb0 finds burns: its room's near list in order (the
    // reach's rects past its own), each room's units newest first (the higher room stamp).
    {
        const auto first_burnt = [&](std::array<float, 2> spots_x, std::array<std::uint32_t, 2> stamps) {
            Missile fire;
            fire.info = &burner;
            fire.x = fire.y = 4.1f;
            fire.born = 0; fire.dies = 100000;
            fire.row = d2d::rules::MissileDamage{ .etype = 0, .elo = 0x1a0, .ehi = 0x1a0 };
            std::vector<Missile> missiles;
            missiles.push_back(std::move(fire));
            std::array<Foe, 2> two{};
            for (std::size_t k = 0; k < 2; ++k) { two[k].x = spots_x[k]; two[k].y = 4.1f; two[k].room_stamp = stamps[k]; }
            const auto reach = [](float, float) {   // its own room, then the near list: x 21.. first, then ..20
                return std::vector<std::array<int, 4>>{ { 0, 0, 40, 40 }, { 21, 0, 19, 40 }, { 0, 0, 21, 40 } };
            };
            d2d::rules::Rng rng(7);
            missiles_update(open, missiles, two, rng, 4000, reach, [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
            assert(two[0].blows.size() + two[1].blows.size() == 1);
            return two[0].blows.empty() ? 1 : 0;
        };
        assert(first_burnt({ 4.1f, 4.1f }, { 50, 10 }) == 0 && first_burnt({ 4.1f, 4.1f }, { 10, 50 }) == 1);   // one room: the newest
        assert(first_burnt({ 4.05f, 4.25f }, { 50, 10 }) == 1);                                                 // the room listed first
    }
    std::puts("OK: a burn keeps its 256ths (0057c852), its MDR at DamageRate / 1024; no get-hit, dParam1 soft hits; the first unit in room order");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_burn_fraction: %s\n", error.what());
    return 1;
}
