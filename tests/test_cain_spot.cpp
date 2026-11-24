// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Tristram Cain's AI notes his spot once for the quest record (FUN_005e77a0 -> FUN_005944b0: +0x9c / +0xa0, flag +0x67);
// a first think with it noted already takes FUN_005944f0 (camp Cain due) and leaves the spot as it was.
#include <quests.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;

// Checks CainQuest::note_spot over two first thinks.
int main() {
    CainQuest cain;
    assert(!cain.camp_due);
    assert(cain.note_spot(40, 50) && cain.spot_noted && cain.spot_x == 40 && cain.spot_y == 50 && !cain.camp_due);   // FUN_005944b0 returns 1
    assert(!cain.note_spot(90, 95) && cain.camp_due && cain.spot_x == 40 && cain.spot_y == 50);                      // returns 0: FUN_005944f0
    std::puts("ok");
    return 0;
}
