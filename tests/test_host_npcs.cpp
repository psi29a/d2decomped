// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// A joined game's town NPCs (apps/d2d/session/host_npcs.hpp): each of d2d's level NPCs is drawn as the host's unit of
// its class nearest its spot, each host unit once. This does not run ClientSession.
#include "session/host_npcs.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

int main() try {
    using d2d::client::HostNpc;
    using d2d::client::NpcSpot;
    const std::vector<NpcSpot> npcs{ { "akara", 10, 10 }, { "rogue1", 20, 20 }, { "rogue1", 22, 20 }, { "chest", 5, 5, true }, { "kashya", 0, 0 } };
    const std::vector<HostNpc> hosts{ { "rogue1", 21, 21 }, { "akara", 30, 30 }, { "rogue1", 50, 50 }, { "chest", 5, 5 } };
    const auto paired = d2d::client::pair_host_npcs(npcs, hosts);
    assert(paired[0] == 1);                    // the only Akara, however far she walked
    assert(paired[1] == 0 && paired[2] == 2);  // the nearer rogue to the first, the other to the second
    assert(paired[3] == -1 && paired[4] == -1);
    std::puts("OK: each town NPC takes the host's nearest of its class, each once");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
