// NPC behaviour: town NPCs patrolling their DS1 paths.
#pragma once

#include "world.hpp"

namespace {

// Where each world NPC is right now (index-aligned with Level::npcs):
// patrolling NPCs walk their DS1 path, pausing at each point.
struct UnitState {
    float x = 0, y = 0;
    int dir = 0;
    bool walking = false;
    std::size_t next = 0;             // path point being walked to
    std::uint32_t wait_until = 0;     // ms; idle until then
    std::uint32_t mode_ms = 0;        // when the current mode (walk/idle) started
    std::uint32_t stuck_since = 0;    // ms the merc last got blocked, 0 = moving
    float goal_x = 0, goal_y = 0;     // where the merc's route was planned to
    bool hidden = false;              // a quest-gated NPC who isn't here (yet)
    std::vector<std::pair<float, float>> path;   // a walk_path route being followed
};

std::vector<UnitState> npc_start(const Level& L) {
    std::vector<UnitState> out;
    for (std::size_t i = 0; i < L.npcs.size(); ++i) {
        const auto& n = L.npcs[i];
        out.push_back({ .x = n.x, .y = n.y, .wait_until = std::uint32_t(1000 + 700 * i % 3000) });
    }
    return out;
}

// The units moving about (the player, the merc, patrolling NPCs): each
// blocks the others within 0.3 cells (1.5 subtiles) of its centre, so
// routes go round them and walkers wait. Standing NPCs are already in
// the walk grid.
// ponytail: centres only — D2 stamps each unit's collision pattern with
// its own bits (0x800 monsters, 0x1000 players) and walkers test mask
// 0x1c09.
struct Crowd {
    std::vector<const UnitState*> units;
    [[nodiscard]] bool at(float x, float y, const UnitState* self) const {
        for (const auto* u : units)
            if (u != self && std::abs(u->x - x) < 0.3f && std::abs(u->y - y) < 0.3f) return true;
        return false;
    }
};

// Patrolling NPCs: walk to the next DS1 path point, idle a few seconds
// there, move on. NPCs in `busy` (menu, speech or store open on them)
// stand still. ponytail: the per-point action (1..4 — likely S1
// specials like Charsi's hammering) isn't interpreted; pauses are 2-5 s.
void npc_patrol(const Level& L, std::vector<UnitState>& npcs, std::array<int, 3> busy,
                std::uint32_t ms, float dt, const Crowd& crowd = {}) {
    for (std::size_t i = 0; i < npcs.size(); ++i) {
        const auto& path = L.npcs[i].path;
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
                    step = cells_per_sec(L.npcs[i].velocity) * dt;
        if (dist > 0.05f) st.dir = direction16(dx, dy);
        if (dist <= step) {
            st.x = tx; st.y = ty; st.walking = false; st.mode_ms = ms;
            st.next = (st.next + 1) % path.size();
            st.wait_until = ms + 2000 + std::uint32_t((i * 1237 + st.next * 911) % 3000);
        } else if (const float nx = st.x + dx / dist * step, ny = st.y + dy / dist * step; !crowd.at(nx, ny, &st)) {
            st.x = nx; st.y = ny;                                     // else someone's in the way: wait
        }
    }
}

// A walkable route from (x, y) to (gx, gy), in cells: rules::find_path
// over subtiles with the unit collision test, then string-pulled (a turn
// is dropped while the straight line past it stays clear). It ends on
// the goal itself when that's walkable, else as close as it gets.
std::vector<std::pair<float, float>> walk_path(const Level& L, float x, float y, float gx, float gy,
                                               const Crowd& crowd = {}, const UnitState* self = nullptr) {
    auto blocked = [&](float bx, float by) { return L.unit_blocked(bx, by) || crowd.at(bx, by, self); };
    auto sub = [](float v) { return int(std::floor(v * 5)); };
    auto centre = [](int v) { return (float(v) + 0.5f) / 5; };
    const auto steps = d2d::rules::find_path(sub(x), sub(y), sub(gx), sub(gy),
        [&](int sx, int sy) { return blocked(centre(sx), centre(sy)); });
    std::vector<std::pair<float, float>> pts;
    for (const auto& [px, py] : steps) pts.emplace_back(centre(px), centre(py));
    if (!steps.empty() && steps.back() == std::pair(sub(gx), sub(gy)) && !blocked(gx, gy)) pts.back() = { gx, gy };
    auto clear = [&](float ax, float ay, float bx, float by) {
        const int n = int(std::hypot(bx - ax, by - ay) / 0.05f) + 1;
        for (int i = 1; i <= n; ++i)
            if (blocked(ax + (bx - ax) * float(i) / float(n), ay + (by - ay) * float(i) / float(n))) return false;
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

// Moves u along its path by `step` cells, dropping reached points and
// turning it to face the way; blocked by a wall or another unit, it
// drops the route. False once there (or stuck).
bool follow_path(const Level& L, UnitState& u, float step, const Crowd& crowd = {}) {
    auto& path = u.path;
    auto& x = u.x;
    auto& y = u.y;
    auto& dir = u.dir;
    while (!path.empty() && step > 0) {
        const auto [tx, ty] = path.front();
        const float dx = tx - x, dy = ty - y, dist = std::hypot(dx, dy);
        if (dist > 0.05f) dir = direction16(dx, dy);
        if (dist <= step) { x = tx; y = ty; step -= dist; path.erase(path.begin()); continue; }
        const float nx = x + dx / dist * step, ny = y + dy / dist * step;
        if (L.unit_blocked(nx, ny) || crowd.at(nx, ny, &u)) { path.clear(); break; }
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
void merc_follow(const Level& L, UnitState& m, float px, float py, float speed, std::uint32_t ms, float dt,
                 const Crowd& crowd = {}) {
    auto& path = m.path;
    const float dist = std::hypot(px - m.x, py - m.y);
    if (dist > 12) { m.x = px + 1; m.y = py + 1; m.walking = false; path.clear(); return; }
    const bool go = m.walking ? dist > 1.5f : dist > 3;
    if (go != m.walking) { m.walking = go; m.mode_ms = ms; }
    if (!go) { path.clear(); return; }
    // Re-plan when the player has moved a cell from where it was planned.
    if (path.empty() || std::hypot(m.goal_x - px, m.goal_y - py) > 1.f) {
        path = walk_path(L, m.x, m.y, px, py, crowd, &m);
        m.goal_x = px; m.goal_y = py;
    }
    const float ox = m.x, oy = m.y;
    follow_path(L, m, speed * dt, crowd);
    if (std::hypot(m.x - ox, m.y - oy) > speed * dt * 0.3f) m.stuck_since = 0;
    else if (!m.stuck_since) m.stuck_since = ms;
    else if (ms - m.stuck_since > 1500) { m.x = px + 1; m.y = py + 1; m.walking = false; m.stuck_since = 0; path.clear(); }
}

// A monster in the level: its type (MonStats row), composite recipe with
// the components it rolled, where it is, its stats and what it's doing.
struct Monster {
    int type = -1;
    Npc npc;
    UnitState u;
    d2d::rules::MonStats st;
    int hp = 1;
    int leader = -1;                          // index of its group's leader
    float home_x = 0, home_y = 0;             // where it spawned: wandering stays near
    std::string_view mode = "NU";             // animation mode token
    std::uint32_t mode_until = 0;             // ms: an attack / get-hit / death ends
    std::uint32_t next_act = 0;               // ms: may attack again (aidel)
    std::uint32_t flee_until = 0;             // ms: running from the player
    bool struck = false;                      // this attack's hit is resolved
    bool aware = false;                       // has noticed the player
    [[nodiscard]] bool alive() const { return hp > 0; }
};

// Whoever the monsters are after (the player), and what they did to it
// this frame.
struct Foe {
    float x = 0, y = 0;
    int defense = 0, level = 1;
    bool alive = true;
    int damage = 0;                           // life lost this frame, whole points
};

constexpr float kMeleeReach = 1.1f;           // cells between centres

// The level's spawns as monsters: each rolls its components (one of
// MonStats2's HDv..S8v per layer) and its stats.
// ponytail: components from our own roll, not game.exe's
// (the monster's seed at spawn isn't traced).
std::vector<Monster> spawn_monsters(const Scene& s, const Level& L, d2d::rules::Rng& rng) {
    std::vector<Monster> out;
    for (const auto& sp : L.spawns) {
        if (sp.type < 0 || std::size_t(sp.type) >= s.mon_npc.size()) continue;
        const auto& t = s.monsters.types[std::size_t(sp.type)];
        Monster m;
        m.type = sp.type;
        m.npc = s.mon_npc[std::size_t(sp.type)];
        for (std::size_t l = 0; l < 16; ++l)
            if (!t.parts[l].empty()) m.npc.comp[l] = t.parts[l][std::size_t(rng(int(t.parts[l].size())))];
        m.u.x = m.home_x = (float(sp.x) + 0.5f) / 5;
        m.u.y = m.home_y = (float(sp.y) + 0.5f) / 5;
        m.u.dir = rng(16);
        m.u.wait_until = std::uint32_t(rng(4000));
        m.st = d2d::rules::monster_stats(s.monsters, sp.type, 0, rng);
        m.hp = m.st.hp;
        m.leader = sp.leader;
        out.push_back(std::move(m));
    }
    return out;
}

void set_mode(const Scene& s, Monster& m, std::string_view mode, std::uint32_t ms) {
    m.mode = mode;
    m.u.mode_ms = ms;
    m.u.walking = mode == "WL";
    m.mode_until = mode == "NU" || mode == "WL" || mode == "DD" ? 0 : ms + s.npc_anim(m.npc, mode).length_ms();
}

// Damage to a monster: it dies (DT, then its corpse, DD) or recoils (GH)
// and, the first time, notices. True when this killed it.
// ponytail: D2 plays get-hit only past a share of max life; always here.
bool hurt(const Scene& s, Monster& m, int damage, std::uint32_t ms) {
    if (!m.alive()) return false;
    m.hp -= damage;
    m.aware = true;
    set_mode(s, m, m.alive() ? "GH" : "DT", ms);
    return !m.alive();
}

// One step toward (tx, ty) at `speed` cells/s, straight on; false when
// something's in the way.
bool monster_step(const Level& L, Monster& m, float tx, float ty, float step, const Crowd& crowd) {
    auto& u = m.u;
    const float dx = tx - u.x, dy = ty - u.y, dist = std::hypot(dx, dy);
    if (dist < 0.01f) return true;
    u.dir = direction16(dx, dy);
    const float k = std::min(step, dist) / dist, nx = u.x + dx * k, ny = u.y + dy * k;
    if (L.unit_blocked(nx, ny)) return false;
    for (const auto* o : crowd.units)                   // into someone: blocked; out of an overlap: fine
        if (o != &u && std::abs(o->x - nx) < 0.3f && std::abs(o->y - ny) < 0.3f
            && std::hypot(o->x - nx, o->y - ny) < std::hypot(o->x - u.x, o->y - u.y)) return false;
    u.x = nx; u.y = ny;
    return true;
}

// A monster's frame: finish an attack / get-hit / death; flee; notice the
// player within 8 cells (then keep after them within 16), walk up and
// attack (A1: the hit lands on AnimData's event frame, MonStats A1TH vs
// the foe's defense, A1MinD..MaxD), wait aidel ticks between attacks;
// otherwise wander near home (Levels.txt MonWndr): stand 2-5 s, walk to a
// random spot within 3 cells.
// ponytail: one melee think for every AI type (MonStats AI / aip1..8 and
// game.exe's per-AI think functions not traced); distances and timings
// by eye; chasing goes straight at the player, sliding to a stop at walls.
void monster_update(const Scene& s, const Level& L, Monster& m, Foe& foe, d2d::rules::Rng& rng,
                    std::uint32_t ms, float dt, const Crowd& crowd) {
    auto& u = m.u;
    const auto& t = s.monsters.types[std::size_t(m.type)];
    if (!m.alive()) {
        if (m.mode == "DT" && ms >= m.mode_until) set_mode(s, m, "DD", ms);
        return;
    }
    if (m.mode == "GH") {
        if (ms < m.mode_until) return;
        set_mode(s, m, "NU", ms);
    }
    const float dx = foe.x - u.x, dy = foe.y - u.y, dist = std::hypot(dx, dy);
    if (m.mode == "A1") {
        if (!m.struck && ms >= u.mode_ms + s.npc_anim(m.npc, "A1").action_ms()) {
            m.struck = true;
            if (foe.alive && dist <= kMeleeReach + 0.3f
                && rng(100) < d2d::rules::hit_chance(m.st.th, foe.defense, m.st.level, foe.level))
                foe.damage += rng.range(m.st.a1_min, m.st.a1_max);
        }
        if (ms < m.mode_until) return;
        set_mode(s, m, "NU", ms);
        m.next_act = ms + std::uint32_t(t.diff[0].aidel) * 40;
    }
    const float walk = cells_per_sec(float(t.velocity)) * dt;
    if (ms < m.flee_until) {
        if (m.mode != "WL") set_mode(s, m, "WL", ms);
        if (!monster_step(L, m, u.x - dx, u.y - dy, cells_per_sec(float(t.run)) * dt, crowd)) m.flee_until = 0;
        return;
    }
    if (foe.alive && (dist < 8 || (m.aware && dist < 16))) {
        m.aware = true;
        if (dist <= kMeleeReach) {
            u.dir = direction16(dx, dy);
            if (ms >= m.next_act) { set_mode(s, m, "A1", ms); m.struck = false; }
            else if (m.mode != "NU") set_mode(s, m, "NU", ms);
            return;
        }
        // Straight at the player, else sidestep round whoever's in the way.
        bool moved = false;
        for (const float turn : { 0.f, 0.785f, -0.785f, 1.571f, -1.571f }) {
            const float c = std::cos(turn), sn = std::sin(turn);
            if ((moved = monster_step(L, m, u.x + dx * c - dy * sn, u.y + dx * sn + dy * c, walk, crowd))) break;
        }
        if (moved != (m.mode == "WL")) set_mode(s, m, moved ? "WL" : "NU", ms);
        return;
    }
    m.aware = false;
    if (!u.walking) {
        if (ms < u.wait_until || !L.mon.wander) return;
        const float a = float(rng(360)) * 3.14159265f / 180, r = float(rng(300)) / 100;
        u.goal_x = m.home_x + std::cos(a) * r;
        u.goal_y = m.home_y + std::sin(a) * r;
        set_mode(s, m, "WL", ms);
        return;
    }
    if (std::hypot(u.goal_x - u.x, u.goal_y - u.y) <= walk || !monster_step(L, m, u.goal_x, u.goal_y, walk, crowd)) {
        if (std::hypot(u.goal_x - u.x, u.goal_y - u.y) <= walk) { u.x = u.goal_x; u.y = u.goal_y; }
        set_mode(s, m, "NU", ms);
        u.wait_until = ms + 2000 + std::uint32_t(rng(3000));
    }
}

// Fallen scatter when one of their pack dies (MonStats AI "Fallen"):
// the others of its group within 10 cells run for 2-3 s.
// ponytail: the Fallen think function isn't traced; group = spawn group.
void fallen_scatter(const Scene& s, std::vector<Monster>& ms_, std::size_t dead, d2d::rules::Rng& rng, std::uint32_t ms) {
    const auto& d = ms_[dead];
    if (s.monsters.types[std::size_t(d.type)].ai != "Fallen") return;
    for (auto& m : ms_)
        if (&m != &d && m.alive() && m.leader == d.leader && std::hypot(m.u.x - d.u.x, m.u.y - d.u.y) < 10
            && s.monsters.types[std::size_t(m.type)].ai == "Fallen")
            m.flee_until = ms + 2000 + std::uint32_t(rng(1000));
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
