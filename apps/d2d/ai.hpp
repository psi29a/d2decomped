// NPC behaviour: town NPCs patrolling their DS1 paths.
#pragma once

#include "world.hpp"

namespace {

// Where each world NPC is right now (index-aligned with Scene::world_npcs):
// patrolling NPCs walk their DS1 path, pausing at each point.
struct NpcState {
    float x = 0, y = 0;
    int dir = 0;
    bool walking = false;
    std::size_t next = 0;             // path point being walked to
    std::uint32_t wait_until = 0;     // ms; idle until then
    std::uint32_t mode_ms = 0;        // when the current mode (walk/idle) started
};

std::vector<NpcState> npc_start(const Scene& s) {
    std::vector<NpcState> out;
    for (std::size_t i = 0; i < s.world_npcs.size(); ++i) {
        const auto& n = s.world_npcs[i];
        out.push_back({ n.x, n.y, 0, false, 0, std::uint32_t(1000 + 700 * i % 3000) });
    }
    return out;
}

// Patrolling NPCs: walk to the next DS1 path point, idle a few seconds
// there, move on. NPCs in `busy` (menu, speech or store open on them)
// stand still. ponytail: the per-point action (1..4 — likely S1
// specials like Charsi's hammering) isn't interpreted; pauses are 2-5 s.
void npc_patrol(const Scene& s, std::vector<NpcState>& npcs, std::array<int, 3> busy,
                std::uint32_t ms, float dt) {
    for (std::size_t i = 0; i < npcs.size(); ++i) {
        const auto& path = s.world_npcs[i].path;
        if (path.empty()) continue;
        auto& st = npcs[i];
        if (std::ranges::find(busy, int(i)) != busy.end()) {   // busy: stand still
            if (st.walking) { st.walking = false; st.mode_ms = ms; }
            st.wait_until = ms + 2000;
            continue;
        }
        if (!st.walking) {
            if (ms >= st.wait_until) { st.walking = true; st.mode_ms = ms; }
            continue;
        }
        const auto [tx, ty] = path[st.next % path.size()];
        const float dx = tx - st.x, dy = ty - st.y;
        const float dist = std::hypot(dx, dy),
                    step = cells_per_sec(s.world_npcs[i].velocity) * dt;
        if (dist > 0.05f) st.dir = direction16(dx, dy);
        if (dist <= step) {
            st.x = tx; st.y = ty; st.walking = false; st.mode_ms = ms;
            st.next = (st.next + 1) % path.size();
            st.wait_until = ms + 2000 + std::uint32_t((i * 1237 + st.next * 911) % 3000);
        } else {
            st.x += dx / dist * step; st.y += dy / dist * step;
        }
    }
}

}  // namespace
