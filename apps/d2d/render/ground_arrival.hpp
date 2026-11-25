// SPDX-License-Identifier: GPL-3.0-or-later
// The order the client's room lists hold the ground items in: the order they arrived, last first.
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::client {

// The client heads its room's list with each item that arrives (FUN_004c1910 modes 3 / 5 -> d2d_unit_link_in). An item
// dropped while its room is in play arrives alone; a room coming into play (the player's room changed: FUN_00537b50 ->
// FUN_0053a8e0) sends its items from the server room's head, newest first, so the client lists those oldest first; a
// room leaving play takes its items off the client (FUN_0053a9b0). `stamps`: each item id's arrival, highest the head.
struct GroundArrival {
    std::unordered_map<int, std::uint32_t> stamps;
    std::uint32_t next = 1;
    std::vector<int> rooms;                       // the rooms in play as last seen

    // `items`: (id, room) for each ground item, View::ground's order (oldest first); `in_play`: the near list of the
    // player's room now. A level change: clear() first.
    void step(std::span<const std::pair<int, int>> items, std::span<const int> in_play) {
        const auto holds = [](std::span<const int> rooms_in, int room) { return std::ranges::find(rooms_in, room) != rooms_in.end(); };
        std::erase_if(stamps, [&](const auto& entry) {
            const auto item = std::ranges::find(items, entry.first, &std::pair<int, int>::first);
            return item == items.end() || !holds(in_play, item->second);
        });
        for (const auto& [id, room] : items)                            // drops in rooms already in play, as they came
            if (holds(in_play, room) && holds(rooms, room) && !stamps.contains(id)) stamps[id] = next++;
        for (auto item = items.rbegin(); item != items.rend(); ++item)  // rooms come into play: newest first
            if (holds(in_play, item->second) && !holds(rooms, item->second) && !stamps.contains(item->first)) stamps[item->first] = next++;
        rooms.assign(in_play.begin(), in_play.end());
    }
    void clear() { stamps.clear(); rooms.clear(); }
    [[nodiscard]] std::uint32_t of(int id) const {
        const auto found = stamps.find(id);
        return found == stamps.end() ? 0 : found->second;
    }
};

}  // namespace d2d::client
