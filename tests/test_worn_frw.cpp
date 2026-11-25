// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: d2s
// test-includes: apps/d2d
// Another player's faster run / walk on the client (apps/d2d/session/worn_frw.hpp): stat 96 off the items it wears in
// the active set, from their 0x9d props, plus its states'. FUN_00623f50 reads it on the client unit. This does not run
// ClientSession.
#include "session/worn_frw.hpp"

#include <d2s_items.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <unordered_map>

int main() try {
    std::unordered_map<std::uint32_t, d2d::d2s::Item> worn;
    d2d::d2s::Item boots;
    boots.slot = 9;
    boots.props = { { .stat = d2d::d2s::kFasterMoveVelocity, .value = 30 }, { .stat = d2d::d2s::kFasterAttackRate, .value = 20 } };
    worn[7] = boots;
    d2d::d2s::Item ring;
    ring.slot = 6;
    ring.props = { { .stat = d2d::d2s::kFasterMoveVelocity, .value = 10 } };
    worn[8] = ring;
    d2d::d2s::Item switched;                                         // the other weapon set: not active
    switched.slot = 0xb;
    switched.props = { { .stat = d2d::d2s::kFasterMoveVelocity, .value = 40 } };
    worn[9] = switched;
    assert(d2d::client::worn_frw(worn, 0) == 40);
    assert(d2d::client::worn_frw(worn, 15) == 55);                   // a state's own FRW
    assert(d2d::client::worn_frw({}, 0) == 0);
    std::puts("OK: another player's FRW is its active gear's stat 96 and its states' (FUN_00623f50)");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
