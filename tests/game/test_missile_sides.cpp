// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's missile and the units it strikes (FUN_005a8730, its unit filter): with an owner,
// only a unit hostile to it (FUN_00554200), unless the row has CollideFriend (+0x187); never the
// owner itself. An evil owner's passes an evil monster by and strikes the player's side or a good
// monster; a good owner's (rogue2's) the other way round. With no owner, every foe. No game data:
// rows typed in, the foes laid on the missile's subtile.

#include <ai/units.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <missile_flight.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    Level open;
    open.ds1 = d2d::ds1::Map(8, 8, 1, 1);           // 40 x 40 subtiles, nothing blocked
    open.walk.assign(40 * 40, 0);
    Monster evil, good, owner;
    evil.id = 11; evil.align = 0;
    good.id = 12; good.align = 2;
    owner.id = 13;
    // Whether a standing missile owned by `owner` (alignment `owner_align`, -1: none) strikes a foe
    // standing on it: the player's side (no monster) or `target`.
    const auto strikes = [&](int owner_align, const Monster* target, bool collide_friend) {
        GameData::MissileInfo row;
        row.size = 1;
        row.collide_friend = collide_friend;
        Missile standing;
        standing.info = &row;
        standing.x = standing.y = 4.1f;             // subtile (20, 20)
        standing.born = 0; standing.dies = 100000;
        if (owner_align >= 0) { standing.owner = owner.id; standing.owner_align = owner_align; }
        std::vector<Missile> missiles;
        missiles.push_back(std::move(standing));
        std::array<Foe, 1> foes{};
        foes[0].x = foes[0].y = 4.15f; foes[0].size = 1; foes[0].alive = true;
        foes[0].monster = target;
        d2d::rules::Rng rng(7);
        missiles_update(open, missiles, foes, rng, 4000, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                        [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
        return missiles.empty();                    // spent on the foe it struck
    };
    // An evil owner's: the player's side and a good monster, not an evil one (but with CollideFriend).
    assert(strikes(0, nullptr, false) && strikes(0, &good, false));
    assert(!strikes(0, &evil, false) && strikes(0, &evil, true));
    // A good owner's: an evil monster, not the player's side nor a good monster (but with CollideFriend).
    assert(strikes(2, &evil, false));
    assert(!strikes(2, nullptr, false) && !strikes(2, &good, false) && strikes(2, nullptr, true));
    // No owner: every foe.
    assert(strikes(-1, &evil, false) && strikes(-1, nullptr, false));
    // Never its owner, CollideFriend or not (FUN_00554200: one unit with itself).
    Monster self = std::move(owner);
    self.align = 0;
    assert(!strikes(0, &self, true));
    std::puts("OK: a monster's missile strikes the units hostile to its owner (FUN_005a8730)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_missile_sides: %s\n", error.what());
    return 1;
}
