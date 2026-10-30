// SPDX-License-Identifier: GPL-3.0-or-later
// Units sliding between ticks (unit_slide.hpp, deviations.md #13): a unit
// the last View had moves to the blend while drawn and back after; a jump
// and a new unit stay put; a pet or missile that goes doesn't move the
// others' starts.
#include "unit_slide.hpp"

#include <world.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

using namespace d2d::client;

namespace {

bool about(float value, float want) { return std::abs(value - want) < 1e-4f; }

d2d::game::View::Shot shot_at(float x, std::uint32_t born) {
    d2d::game::View::Shot shot;
    shot.x = x; shot.y = 0; shot.born = born;
    return shot;
}

d2d::game::View::Pet pet_at(float x) {
    d2d::game::View::Pet pet;
    pet.unit.x = x; pet.unit.y = 0;
    return pet;
}

// Keyed units: a step slides, a teleport snaps, a new id stays put, and the
// View goes back when the guard ends.
void keyed_units() {
    d2d::game::View view;
    view.monsters.resize(3);
    view.monsters[0].id = 7;  view.monsters[0].unit.x = 10; view.monsters[0].unit.y = 20;
    view.monsters[1].id = 8;  view.monsters[1].unit.x = 30; view.monsters[1].unit.y = 30;
    view.monsters[2].id = 9;  view.monsters[2].unit.x = 50; view.monsters[2].unit.y = 50;
    view.others.push_back({});
    view.others[0].id = 2; view.others[0].unit.x = 5; view.others[0].unit.y = 5;
    view.npc_states.resize(2);
    view.npc_states[0].x = 60; view.npc_states[1].x = 70;
    SlideFrom from;
    remember_slides(view, from);
    assert(from.keyed.size() == 6 && from.loose.empty());

    // The next tick: 7 walks a cell, 8 teleports, 9 dies off the list and
    // 11 appears; the other player walks; the second NPC walks.
    view.monsters[0].unit.x = 11;
    view.monsters[1].unit.x = 40;
    view.monsters[2].id = 11;
    view.others[0].unit.y = 6;
    view.npc_states[1].x = 71;
    {
        const SlidGuard slid(slide_units(view, from, 0.25f));
        assert(slid.slid.size() == 4);   // 7, the other player, both NPCs (one standing)
        assert(about(view.monsters[0].unit.x, 10.25f) && about(view.monsters[0].unit.y, 20));
        assert(about(view.monsters[1].unit.x, 40));        // a jump snaps
        assert(about(view.monsters[2].unit.x, 50));        // a new unit stays put
        assert(about(view.others[0].unit.y, 5.25f));
        assert(about(view.npc_states[0].x, 60) && about(view.npc_states[1].x, 70.25f));
    }
    assert(about(view.monsters[0].unit.x, 11) && about(view.others[0].unit.y, 6) && about(view.npc_states[1].x, 71));

    // At blend 1 the View is where the tick put it.
    {
        const SlidGuard slid(slide_units(view, from, 1.f));
        assert(about(view.monsters[0].unit.x, 11) && about(view.npc_states[1].x, 71));
    }
}

// Loose units: the first pet goes, the second keeps its own start; of two
// missiles born the same tick, the first goes and the second still slides
// from where it was, not from the first's place.
void loose_units() {
    d2d::game::View view;
    view.pets.push_back(pet_at(10));
    view.pets.push_back(pet_at(20));
    view.missiles.push_back(shot_at(1, 400));
    view.missiles.push_back(shot_at(5, 400));
    SlideFrom from;
    remember_slides(view, from);
    assert(from.keyed.empty() && from.loose.size() == 4);

    view.pets.erase(view.pets.begin());
    view.pets[0].unit.x = 21;
    view.missiles.erase(view.missiles.begin());
    view.missiles[0].x = 8;
    {
        const SlidGuard slid(slide_units(view, from, 0.5f));
        assert(slid.slid.size() == 2);
        assert(about(view.pets[0].unit.x, 20.5f));
        assert(about(view.missiles[0].x, 6.5f));
    }
    assert(about(view.pets[0].unit.x, 21) && about(view.missiles[0].x, 8));

    // A missile of another birth is a new one: it stays put.
    view.missiles[0].born = 440;
    const SlidGuard slid(slide_units(view, from, 0.5f));
    assert(about(view.missiles[0].x, 8));
}

}  // namespace

int main() {
    keyed_units();
    loose_units();
    std::puts("unit slide ok");
    return 0;
}
