// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A fan side missile's flag 0x10000 becomes record flag 0x80 and suppresses
// the shooter's event 6 (FUN_0059fa30, FUN_005ad730, FUN_0057c6c0).

#include <combat.hpp>

#include <cassert>
#include <cstdio>

namespace d2d::game {

auto fan_side_event_tag(bool side) -> int;
auto missile_owner_event_fighter(d2d::rules::Fighter fighter, int event_tag) -> d2d::rules::Fighter;

}  // namespace d2d::game

int main() {
    d2d::rules::Fighter fighter;
    fighter.crushing = 100;
    fighter.open_wounds = 100;
    fighter.item_slow = 37;
    fighter.knockback = true;
    fighter.deadly = 41;
    fighter.critical = 23;
    fighter.life_steal = 8;
    fighter.elem[0] = { 256, 512 };

    const int middle_tag = d2d::game::fan_side_event_tag(false);
    const auto middle = d2d::game::missile_owner_event_fighter(fighter, middle_tag);
    assert(middle.crushing == 100 && middle.open_wounds == 100 && middle.item_slow == 37 && middle.knockback);

    const int side_tag = d2d::game::fan_side_event_tag(true);
    assert(side_tag != middle_tag);
    const auto side = d2d::game::missile_owner_event_fighter(fighter, side_tag);
    assert(side.crushing == 0 && side.open_wounds == 0 && side.item_slow == 0 && !side.knockback);
    assert(side.deadly == fighter.deadly && side.critical == fighter.critical);
    assert(side.life_steal == fighter.life_steal && side.elem == fighter.elem);

    std::puts("OK: a fan side missile skips the shooter's event-6 item effects");
}
