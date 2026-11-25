// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: dcc font game net
// test-includes: apps/d2d
// A joined client's search for the harem blocker (LAB_004b3360: the client's objects, class 318 in mode 2), which
// keeps the palace guard quiet: NetGame::object_in_mode over the host objects' modes (0x51 +0xc, then 0x0e). This
// does not run ClientSession.
#include "netgame.hpp"

#include <quests.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <unordered_map>

int main() try {
    using d2d::rules::ArcaneQuest;
    std::unordered_map<std::uint64_t, NetGame::Unit> known;
    auto& blocker = known[std::uint64_t(2) << 32 | 40];
    blocker.type = 2;
    blocker.cls = ArcaneQuest::kHaremBlocker;
    assert(!NetGame::object_in_mode(known, ArcaneQuest::kHaremBlocker, 2));   // shut (mode 0): the guard halts
    blocker.object_mode = 2;
    assert(NetGame::object_in_mode(known, ArcaneQuest::kHaremBlocker, 2));
    auto& monster = known[std::uint64_t(1) << 32 | 41];                        // a monster of that row isn't an object
    monster.type = 1;
    monster.cls = ArcaneQuest::kHaremBlocker;
    monster.object_mode = 2;
    blocker.object_mode = 1;
    assert(!NetGame::object_in_mode(known, ArcaneQuest::kHaremBlocker, 2));
    std::puts("OK: the harem blocker is open only as an object of row 318 in mode 2 (LAB_004b3360)");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
