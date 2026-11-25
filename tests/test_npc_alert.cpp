// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A joined game's "!" over an NPC (FUN_004b3380 puts overlay 0x48 on as a kind-3 loop that
// FUN_00470c30 never ends): it stays past 16 * 111 ms and goes only when an NPC's talk opens
// (FUN_004b6dd0 runs FUN_004b2390 over the monsters, which removes row 0x48 from each NPC).
#include <npc_alert.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    d2d::rules::NpcAlertBoard board;
    assert(!board.start(7));
    board.ring(7, 100);
    board.ring(9, 150);
    assert(board.start(7) == 100u && board.start(9) == 150u && !board.start(8));
    board.ring(7, 5000);                       // a repeat starts it over
    assert(board.start(7) == 5000u);
    assert(board.start(7) && 5000u + 16 * 111 < 100000u);   // no local expiry: still there long after
    board.talk_opened();
    assert(!board.start(7) && !board.start(9));
    board.ring(7, 1);
    board.reset();
    assert(!board.start(7));
    std::puts("ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
