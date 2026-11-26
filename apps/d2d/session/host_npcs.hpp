// SPDX-License-Identifier: GPL-3.0-or-later
// A joined game's town NPCs: which host unit each of d2d's level NPCs is drawn as.
#pragma once

#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

namespace d2d::client {

struct NpcSpot { std::string_view id; float x = 0, y = 0; bool skip = false; };   // MonStats Id, act subtiles; skip: an object or hidden
struct HostNpc { std::string_view id; float x = 0, y = 0; };                       // the host unit's class's Id, its act subtiles

// For each NPC in order, the index of the nearest host unit of its class not taken by an earlier one, -1 none.
inline std::vector<int> pair_host_npcs(const std::vector<NpcSpot>& npcs, const std::vector<HostNpc>& hosts) {
    std::vector<int> paired(npcs.size(), -1);
    std::vector<bool> taken(hosts.size(), false);
    for (std::size_t index = 0; index < npcs.size(); ++index) {
        if (npcs[index].skip) continue;
        float best = 1e9f;
        for (std::size_t host = 0; host < hosts.size(); ++host) {
            if (taken[host] || hosts[host].id != npcs[index].id) continue;
            if (const float distance = std::hypot(hosts[host].x - npcs[index].x, hosts[host].y - npcs[index].y); distance < best) { best = distance; paired[index] = int(host); }
        }
        if (paired[index] >= 0) taken[std::size_t(paired[index])] = true;
    }
    return paired;
}

}  // namespace d2d::client
