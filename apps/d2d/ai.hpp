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
    std::uint32_t stuck_since = 0;    // ms the merc last got blocked, 0 = moving
    float goal_x = 0, goal_y = 0;     // where the merc's route was planned to
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

// A walkable route from (x, y) to (gx, gy), in cells: rules::find_path
// over subtiles with the unit collision test, then string-pulled (a turn
// is dropped while the straight line past it stays clear). It ends on
// the goal itself when that's walkable, else as close as it gets.
std::vector<std::pair<float, float>> walk_path(const Scene& s, float x, float y, float gx, float gy) {
    auto sub = [](float v) { return int(std::floor(v * 5)); };
    auto centre = [](int v) { return (float(v) + 0.5f) / 5; };
    const auto steps = d2d::rules::find_path(sub(x), sub(y), sub(gx), sub(gy),
        [&](int sx, int sy) { return s.unit_blocked(centre(sx), centre(sy)); });
    std::vector<std::pair<float, float>> pts;
    for (const auto& [px, py] : steps) pts.emplace_back(centre(px), centre(py));
    if (!steps.empty() && steps.back() == std::pair(sub(gx), sub(gy)) && !s.unit_blocked(gx, gy)) pts.back() = { gx, gy };
    auto clear = [&](float ax, float ay, float bx, float by) {
        const int n = int(std::hypot(bx - ax, by - ay) / 0.05f) + 1;
        for (int i = 1; i <= n; ++i)
            if (s.unit_blocked(ax + (bx - ax) * float(i) / float(n), ay + (by - ay) * float(i) / float(n))) return false;
        return true;
    };
    std::vector<std::pair<float, float>> out;
    float cx = x, cy = y;
    for (std::size_t i = 0; i < pts.size();) {
        std::size_t j = i;
        while (j + 1 < pts.size() && clear(cx, cy, pts[j + 1].first, pts[j + 1].second)) ++j;
        out.push_back(pts[j]);
        std::tie(cx, cy) = pts[j];
        i = j + 1;
    }
    return out;
}

// Moves (x, y) along `path` by `step` cells, dropping reached points;
// the heading goes into dir. False once there (or stuck).
bool follow_path(const Scene& s, std::vector<std::pair<float, float>>& path, float& x, float& y, int& dir, float step) {
    while (!path.empty() && step > 0) {
        const auto [tx, ty] = path.front();
        const float dx = tx - x, dy = ty - y, dist = std::hypot(dx, dy);
        if (dist > 0.05f) dir = direction16(dx, dy);
        if (dist <= step) { x = tx; y = ty; step -= dist; path.erase(path.begin()); continue; }
        const float nx = x + dx / dist * step, ny = y + dy / dist * step;
        if (s.unit_blocked(nx, ny)) { path.clear(); break; }
        x = nx; y = ny;
        break;
    }
    return !path.empty();
}

// The mercenary follows the player: it sets off when more than 3 cells
// behind and stops within 1.5, at `speed` cells/s along a walk_path;
// more than 12 behind (a warp), or stuck for 1.5 s (no route), it's put
// next to the player.
// ponytail: D2's follow distances aren't traced.
void merc_follow(const Scene& s, NpcState& m, std::vector<std::pair<float, float>>& path,
                 float px, float py, float speed, std::uint32_t ms, float dt) {
    const float dist = std::hypot(px - m.x, py - m.y);
    if (dist > 12) { m.x = px + 1; m.y = py + 1; m.walking = false; path.clear(); return; }
    const bool go = m.walking ? dist > 1.5f : dist > 3;
    if (go != m.walking) { m.walking = go; m.mode_ms = ms; }
    if (!go) { path.clear(); return; }
    // Re-plan when the player has moved a cell from where it was planned.
    if (path.empty() || std::hypot(m.goal_x - px, m.goal_y - py) > 1.f) {
        path = walk_path(s, m.x, m.y, px, py);
        m.goal_x = px; m.goal_y = py;
    }
    const float ox = m.x, oy = m.y;
    follow_path(s, path, m.x, m.y, m.dir, speed * dt);
    if (std::hypot(m.x - ox, m.y - oy) > speed * dt * 0.3f) m.stuck_since = 0;
    else if (!m.stuck_since) m.stuck_since = ms;
    else if (ms - m.stuck_since > 1500) { m.x = px + 1; m.y = py + 1; m.walking = false; m.stuck_since = 0; path.clear(); }
}

// The merc's name: its hireling row's NameFirst key (merc01, merca201,
// MercX101, ...) counted on by the save's name index.
std::string merc_name(const Scene& s, const Scene::Merc& m, int index) {
    const auto& f = m.name_first;
    if (f.size() < 2) return f;
    const int first = std::atoi(f.substr(f.size() - 2).c_str());
    const auto key = f.substr(0, f.size() - 2) + std::format("{:02}", first + index);
    const auto v = lookup_string(s, key);
    return v ? u16_to_latin1(*v) : key;
}

}  // namespace
