// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// The client's room lists hold ground items in arrival order, last first (cursor-units.md "The walk"): a drop heads
// its room's list; a room coming into play lists its items oldest first (the server sent them newest first,
// FUN_0053a8e0); a room leaving play drops them (FUN_0053a9b0). This does not run ClientSession.
#include "render/ground_arrival.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

int main() try {
    d2d::client::GroundArrival arrival;
    // Room 1 in play holds items 10, 11 (older first); room 2 not.
    const std::array<std::pair<int, int>, 3> first{ { { 10, 1 }, { 11, 1 }, { 20, 2 } } };
    const std::array<int, 1> play1{ 1 };
    arrival.step(first, play1);                  // the game's first look: room 1 comes into play, newest first
    assert(arrival.of(10) > arrival.of(11) && arrival.of(20) == 0);
    const std::array<std::pair<int, int>, 4> dropped{ { { 10, 1 }, { 11, 1 }, { 20, 2 }, { 12, 1 } } };
    arrival.step(dropped, play1);                // a drop heads the list
    assert(arrival.of(12) > arrival.of(10));
    const std::array<int, 2> play12{ 1, 2 };
    arrival.step(dropped, play12);               // room 2 comes in: its one item heads
    assert(arrival.of(20) > arrival.of(12));
    const std::array<int, 1> play2{ 2 };
    arrival.step(dropped, play2);                // room 1 leaves: its items go
    assert(arrival.of(10) == 0 && arrival.of(12) == 0 && arrival.of(20) != 0);
    arrival.step(dropped, play12);               // and come back oldest first
    assert(arrival.of(10) > arrival.of(11) && arrival.of(11) > arrival.of(12) && arrival.of(10) > arrival.of(20));
    std::puts("OK: the client lists ground items as they arrived, a room's batch oldest first (FUN_0053a8e0)");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
