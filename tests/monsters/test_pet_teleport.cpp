// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A pet's follow mode 3 (FUN_005e3ea0): the spot in the owner's room
// (FUN_0054dc40) or the put (FUN_00554ea0) failing fails the follow.
#include <pets.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

int main() try {
    PetView view;
    view.x = 100; view.y = 100; view.owner_x = 200; view.owner_y = 200;   // over 0x32 apart
    view.end_x = 200; view.end_y = 200;
    auto area = [](int, int, int, int) { return 1; };
    auto move = [](const MercMove&) { return true; };
    for (const bool lands : { false, true }) {
        Rng rng(1);
        int asked = 0;
        const auto act = necropet_think(view, rng, area, move, [&] { ++asked; return lands; });
        assert(asked == 1);
        assert(act.kind == (lands ? PetAct::Kind::teleport : PetAct::Kind::failed));
        Rng again(1);
        PetFollow follower(view, again, area, move);
        follower.teleport = [&] { return lands; };
        assert(follower.follow(3, false, 0, 0) == lands);
    }
    Rng rng(1);
    assert(necropet_think(view, rng, area, move).kind == PetAct::Kind::teleport);   // no teleport given: it lands
    std::puts("test_pet_teleport: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_pet_teleport: %s\n", error.what());
    return 1;
}
