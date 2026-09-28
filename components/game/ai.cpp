// Definitions for ai.hpp: monsters, NPCs and the merc: spawning, paths, the AI step.
#include "ai.hpp"
#include "gamedata.hpp"

namespace d2d::game {

std::vector<std::pair<float, float>> walk_path(const Level& L, float x, float y, float gx, float gy,
                                               const Crowd& crowd , const UnitState* self) {
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

std::string unique_name(const GameData& s, int name_seed) {
    const auto& [pre, suf, app] = s.unique_names;
    if (pre.empty() || suf.empty()) return "?";
    auto fill = [](std::string f, std::initializer_list<std::string> args) {   // "%0 %1 %2": positional
        int i = 0;
        for (const auto& a : args) {
            const std::string k = "%" + std::to_string(i++);
            if (const auto p = f.find(k); p != std::string::npos) f.replace(p, k.size(), a);
        }
        return f;
    };
    d2d::rules::Rng r{ std::uint32_t(name_seed) };
    const auto& sx = suf[std::size_t(r(int(suf.size())))];
    const auto& px = pre[std::size_t(r(int(pre.size())))];
    auto name = fill(s.unique_formats[0].empty() ? "%0 %1" : s.unique_formats[0], { px, sx });
    if (r(100) < 50 && !app.empty()) {
        const auto& ax = app[std::size_t(r(int(app.size())))];
        const auto& s2 = suf[std::size_t(r(int(suf.size())))];
        const auto& p2 = pre[std::size_t(r(int(pre.size())))];
        name = fill(s.unique_formats[1].empty() ? "%0 %1 %2" : s.unique_formats[1], { p2, s2, ax });
    }
    return name;
}

std::vector<Monster> spawn_monsters(const GameData& s, const Level& L, d2d::rules::Rng& rng, int difficulty) {
    std::vector<Monster> out;
    for (const auto& sp : level_spawns(s, L, difficulty)) {
        if (sp.type < 0 || std::size_t(sp.type) >= s.mon_npc.size()) continue;
        const bool boss = sp.boss != d2d::rules::Boss::none;
        auto m = make_monster(s, sp.type, (float(sp.x) + 0.5f) / 5, (float(sp.y) + 0.5f) / 5, rng, difficulty, !boss);
        if (boss) make_boss(s, m, sp.boss, sp.mods, sp.super, sp.name_seed, difficulty, rng);
        m.leader = sp.leader;
        out.push_back(std::move(m));
    }
    return out;
}

void set_mode(const GameData& s, Monster& m, std::string_view mode, std::uint32_t ms) {
    m.mode = mode;
    m.u.mode_ms = ms;
    m.u.walking = mode == "WL";
    m.mode_until = mode == "NU" || mode == "WL" || mode == "DD" ? 0 : ms + s.npc_timing(m.npc, mode).length_ms();
}

std::vector<UnitState> npc_start(const Level& L) {
    std::vector<UnitState> out;
    for (std::size_t i = 0; i < L.npcs.size(); ++i) {
        const auto& n = L.npcs[i];
        out.push_back({ .x = n.x, .y = n.y, .wait_until = std::uint32_t(1000 + 700 * i % 3000) });
    }
    return out;
}

void npc_patrol(const Level& L, std::vector<UnitState>& npcs, std::array<int, 3> busy,
                std::uint32_t ms, float dt, const Crowd& crowd) {
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

bool monster_update(const GameData& s, const Level& L, Monster& m, std::span<Foe> foes, d2d::rules::Rng& rng,
                    std::uint32_t ms, float dt, const Crowd& crowd, std::vector<Missile>& missiles) {
    auto& u = m.u;
    // After the nearest one alive (the player or the merc).
    Foe* pick = &foes[0];
    for (auto& f : foes)
        if (f.alive && (!pick->alive || std::hypot(f.x - u.x, f.y - u.y) < std::hypot(pick->x - u.x, pick->y - u.y))) pick = &f;
    Foe& foe = *pick;
    const auto& t = s.monsters.types[std::size_t(m.type)];
    if (!m.alive()) {
        if (m.mode == "DT" && ms >= m.mode_until) set_mode(s, m, "DD", ms);
        return false;
    }
    if (ms < m.stun_until && m.mode != "GH" && m.mode != "BL") {   // stunned: stands (a get-hit plays out first)
        if (m.mode != "NU") set_mode(s, m, "NU", ms);
        return false;
    }
    if (m.mode == "GH" || m.mode == "BL") {
        if (ms < m.mode_until) return false;
        set_mode(s, m, "NU", ms);
    }
    const float dx = foe.x - u.x, dy = foe.y - u.y, dist = std::hypot(dx, dy);
    const auto miss = t.miss_a2.empty() ? s.missiles.end() : s.missiles.find(t.miss_a2);
    if (m.mode == "A1" || m.mode == "A2") {
        if (!m.struck && ms >= u.mode_ms + s.npc_timing(m.npc, m.mode).action_ms()) {
            m.struck = true;
            if (m.mode == "A2" && miss != s.missiles.end()) {         // fire: at the foe, from here
                const auto& mi = miss->second;
                const float speed = cells_per_sec(float(mi.vel)), d = std::max(dist, 0.01f);
                Missile x{ &mi, u.x, u.y, dx / d * speed, dy / d * speed, direction32(dx, dy), ms,
                           ms + std::uint32_t(mi.range) * 40, m.st };
                x.src.a2_min = m.st.a2_min * mi.src_damage / 128 + mi.min;
                x.src.a2_max = m.st.a2_max * mi.src_damage / 128 + mi.max;
                missiles.push_back(x);
                // Multishot (FUN_005a3610, the missile hook): two more, aimed a
                // subtile to either side.
                using d2d::rules::Boss;
                if ((m.boss == Boss::unique || m.boss == Boss::superunique) && std::ranges::contains(m.mods, d2d::rules::umod::multishot))
                    for (const float side : { 0.2f, -0.2f }) {
                        const float tx = dx - dy / std::max(dist, 0.01f) * side, ty = dy + dx / std::max(dist, 0.01f) * side;
                        const float td = std::max(std::hypot(tx, ty), 0.01f);
                        Missile y = x;
                        y.vx = tx / td * speed; y.vy = ty / td * speed; y.dir = direction32(tx, ty);
                        missiles.push_back(y);
                    }
            } else if (foe.alive && dist <= kMeleeReach + 0.3f) {
                // Melee: block, reductions, resistances; a hit that lands
                // pays the foe's thorns (lightning ones less its resistance).
                auto st = m.st;                                  // Weaken, Decrepify, Battle Cry, Taunt, a boss's aura: its damage %
                for (int* d : { &st.a1_min, &st.a1_max }) *d = std::max(*d * (100 + m.dmg_pct + m.aura_dmg) / 100, 0);
                st.th += st.th * m.aura_th / 100;
                const auto k = d2d::rules::monster_blow(foe.f, foe.level, foe.moving, st, false, rng);
                foe.take(k);
                if (k.hit && m.mana_hi > 0) foe.mana_burn += rng.range(m.mana_lo, m.mana_hi);   // Mana Burn
                // Cursed (FUN_005a2530, the hit hook): 3 in 4, Amplify Damage
                // (skill 66) at level mlvl / 5 + 1.
                // ponytail: on the one struck, not everyone in the skill's
                // radius; the monster's rng, not its own seed; melee only.
                if (k.hit && (m.boss == d2d::rules::Boss::unique || m.boss == d2d::rules::Boss::superunique)
                    && std::ranges::contains(m.mods, d2d::rules::umod::curse) && rng(4) != 0)
                    foe.amplify = std::max(foe.amplify, m.st.level / 5 + 1);
                foe.melee_by.push_back(&m);
                const auto& res = t.diff[std::size_t(m.difficulty)].res;
                const int thorns = foe.f.thorns + d2d::rules::resisted(foe.f.thorns_light, res[3]) + k.damage * foe.f.thorns_pct / 100
                                 + k.damage * m.reflect_pct / 100;  // Iron Maiden
                if (k.hit && thorns > 0 && hurt(s, m, thorns, ms)) return true;
            }
        }
        if (ms < m.mode_until) return false;
        set_mode(s, m, "NU", ms);
        m.next_act = ms + std::uint32_t(t.diff[std::size_t(m.difficulty)].aidel) * 40;
    }
    // Chilled, it moves at coldeffect % slower.
    const float chill = ms < m.chill_until ? float(100 + t.diff[std::size_t(m.difficulty)].cold_effect) / 100.f : 1.f;
    const float walk = cells_per_sec(float(t.velocity)) * dt * chill * float(std::max(100 + m.speed_pct + m.boss_speed, 10)) / 100;
    if (ms < m.flee_until) {
        if (m.mode != "WL") set_mode(s, m, "WL", ms);
        if (!monster_step(L, m, u.x - dx, u.y - dy, cells_per_sec(float(t.run)) * dt * chill, crowd)) m.flee_until = 0;
        return false;
    }
    // Blind (Dim Vision, Cloak of Shadows): it doesn't see past arm's length.
    if (foe.alive && (dist < 8 || (m.aware && dist < 16)) && (ms >= m.blind_until || dist < 1.5f)) {
        m.aware = true;
        // Teleportation (mod 26: MonTeleport, AI flag 0x20; FUN_005b11f0):
        // at a think, 40 %, then — under 30 % life, or a shooter with the
        // foe within 10 subtiles — 15 %: to a free spot in its room, and
        // hurt, 1 in 4 to heal its level in life.
        // ponytail: the room as its 8x8-cell block, the spot by 20 tries
        // (game.exe: FUN_0054dc40); "within 10" read as the foe's distance;
        // no teleport animation or skill cast.
        if (ms >= m.next_act && (m.boss == d2d::rules::Boss::unique || m.boss == d2d::rules::Boss::superunique)
            && std::ranges::contains(m.mods, d2d::rules::umod::teleport) && rng(100) < 40) {
            const bool low = std::int64_t(m.hp) * 100 < std::int64_t(m.st.hp) * 30;
            if ((low || (miss != s.missiles.end() && dist * 5 < 10)) && rng(100) < 15) {
                const float rx = std::floor(u.x / 8) * 8, ry = std::floor(u.y / 8) * 8;
                for (int tries = 0; tries < 20; ++tries) {
                    const float nx = rx + float(rng(80)) / 10, ny = ry + float(rng(80)) / 10;
                    if (L.unit_blocked(nx, ny)) continue;
                    u.x = nx; u.y = ny; u.walking = false;
                    if (low && rng(100) < 25) m.hp = std::min(m.st.hp, m.hp + m.st.level);
                    m.next_act = ms + std::uint32_t(t.diff[std::size_t(m.difficulty)].aidel) * 40;
                    set_mode(s, m, "NU", ms);
                    return false;
                }
            }
        }
        if (dist <= kMeleeReach) {
            u.dir = direction16(dx, dy);
            if (ms >= m.next_act) { set_mode(s, m, "A1", ms); m.struck = false; attack_starts(s, m, "A1", rng); }
            else if (m.mode != "NU") set_mode(s, m, "NU", ms);
            return false;
        }
        // Shooters (MissA2) shoot from up to 7 cells: each think (aidel)
        // the aip2 chance to fire, else close in.
        // ponytail: aip2 read as the shoot chance; QuillRat's think isn't traced.
        if (miss != s.missiles.end() && dist < 7 && ms >= m.next_act) {
            m.next_act = ms + std::uint32_t(t.diff[std::size_t(m.difficulty)].aidel) * 40;
            if (rng(100) < std::max(t.diff[std::size_t(m.difficulty)].aip[1], 1)) {
                u.dir = direction16(dx, dy);
                set_mode(s, m, "A2", ms);
                m.struck = false;
                attack_starts(s, m, "A2", rng);
                return false;
            }
        }
        // Straight at the player, else sidestep round whoever's in the way.
        bool moved = false;
        for (const float turn : { 0.f, 0.785f, -0.785f, 1.571f, -1.571f }) {
            const float c = std::cos(turn), sn = std::sin(turn);
            if ((moved = monster_step(L, m, u.x + dx * c - dy * sn, u.y + dx * sn + dy * c, walk, crowd))) break;
        }
        if (moved != (m.mode == "WL")) set_mode(s, m, moved ? "WL" : "NU", ms);
        return false;
    }
    m.aware = false;
    if (!u.walking) {
        if (ms < u.wait_until || !L.mon.wander) return false;
        const float a = float(rng(360)) * 3.14159265f / 180, r = float(rng(300)) / 100;
        u.goal_x = m.home_x + std::cos(a) * r;
        u.goal_y = m.home_y + std::sin(a) * r;
        set_mode(s, m, "WL", ms);
        return false;
    }
    if (std::hypot(u.goal_x - u.x, u.goal_y - u.y) <= walk || !monster_step(L, m, u.goal_x, u.goal_y, walk, crowd)) {
        if (std::hypot(u.goal_x - u.x, u.goal_y - u.y) <= walk) { u.x = u.goal_x; u.y = u.goal_y; }
        set_mode(s, m, "NU", ms);
        u.wait_until = ms + 2000 + std::uint32_t(rng(3000));
    }
    return false;
}

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

std::string merc_name(const GameData& s, const GameData::Merc& m, int index) {
    const auto& f = m.name_first;
    if (f.size() < 2) return f;
    const int first = std::atoi(f.substr(f.size() - 2).c_str());
    const auto key = f.substr(0, f.size() - 2) + std::format("{:02}", first + index);
    const auto v = lookup_string(s, key);
    return v ? u16_to_latin1(*v) : key;
}

void merc_follow(const Level& L, UnitState& m, float px, float py, float speed, std::uint32_t ms, float dt,
                 const Crowd& crowd) {
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

Monster make_monster(const GameData& s, int type, float x, float y, d2d::rules::Rng& rng, int difficulty, bool stats) {
    const auto& t = s.monsters.types[std::size_t(type)];
    Monster m;
    m.type = type;
    m.npc = s.mon_npc[std::size_t(type)];
    for (std::size_t l = 0; l < 16; ++l)
        if (!t.parts[l].empty()) m.npc.comp[l] = t.parts[l][std::size_t(rng(int(t.parts[l].size())))];
    m.u.x = m.home_x = x;
    m.u.y = m.home_y = y;
    m.u.dir = rng(16);
    m.u.wait_until = std::uint32_t(rng(4000));
    if (stats) m.st = d2d::rules::monster_stats(s.monsters, type, difficulty, rng);
    m.hp = m.last_hp = m.st.hp;
    m.difficulty = std::clamp(difficulty, 0, 2);
    return m;
}

void make_boss(const GameData& s, Monster& m, d2d::rules::Boss kind, const std::vector<int>& mods, int super, int name_seed,
               int difficulty, d2d::rules::Rng& rng) {
    const auto& t = s.monsters.types[std::size_t(m.type)];
    const auto b = d2d::rules::boss_stats(s.umods, t, kind, mods, difficulty);
    m.st = d2d::rules::monster_stats(s.monsters, m.type, difficulty, rng, b.level_add);
    m.boss = kind;
    m.mods = mods;
    m.super = super;
    m.st.hp += m.st.hp * b.hp_pct / 100;
    m.st.exp *= b.exp_mult;
    for (int* v : { &m.st.a1_min, &m.st.a1_max, &m.st.a2_min, &m.st.a2_max }) *v += *v * b.dmg_pct / 100;
    m.st.th += m.st.th * b.tohit_pct / 100;
    if (b.double_defense) m.st.ac *= 2;
    m.boss_res = b.res_add;
    m.boss_speed = b.velocity_pct;
    if (b.elem >= 0 && !s.monsters.lvl.empty()) {                      // enchanted: MonLvl damage x %
        const auto& L = s.monsters.lvl[std::min<std::size_t>(std::size_t(m.st.level), s.monsters.lvl.size() - 1)];
        const int dm = L.dm[std::size_t(std::clamp(difficulty, 0, 2))];
        int put = 0;
        for (auto& e : m.st.el)
            if (e.type < 0 && put < 2) e = { b.elem, 100, dm * b.elem_min_pct / 100, std::max(dm * b.elem_max_pct / 100, dm * b.elem_min_pct / 100), 0, put++ ? "A2" : "A1" };
    }
    // Mana burn (FUN_005a1f90): manadrainmin / max (stats 62 / 63) = MonLvl
    // damage x the elemental % rows for its kind.
    // ponytail: FUN_005a00f0's rows read as the enchanted ones'.
    if (std::ranges::contains(mods, d2d::rules::umod::manahit) && !s.monsters.lvl.empty()) {
        const auto& L = s.monsters.lvl[std::min<std::size_t>(std::size_t(m.st.level), s.monsters.lvl.size() - 1)];
        const int dm = L.dm[std::size_t(std::clamp(difficulty, 0, 2))], d = std::clamp(difficulty, 0, 2);
        const int base = kind == d2d::rules::Boss::minion ? 16 : kind == d2d::rules::Boss::champion ? 22 : 28;
        m.mana_lo = dm * s.umods.k[std::size_t(base + d)] / 100;
        m.mana_hi = std::max(dm * s.umods.k[std::size_t(base + 3 + d)] / 100, m.mana_lo);
    }
    if (std::ranges::contains(mods, d2d::rules::umod::aura)) {
        const auto a = d2d::rules::boss_aura(m.st.level, name_seed, super);
        m.aura = a.skill; m.aura_lvl = a.level;
    }
    m.hp = m.st.hp;
    m.last_hp = m.hp;
    if (super >= 0 && std::size_t(super) < s.superuniques.size()) m.npc.name = s.superuniques[std::size_t(super)].name;
    else if (kind == d2d::rules::Boss::unique) m.npc.name = unique_name(s, name_seed);
    else if (kind == d2d::rules::Boss::champion) {                      // "Champion Zombie", "Ghostly Fallen" (FUN_004ac870)
        auto f = string_id(s, d2d::rules::kChampionFormat);
        if (f.empty()) f = "%0 %1";
        for (const auto& [k, v] : { std::pair{ std::string("%0"), string_id(s, d2d::rules::champion_word(mods)) }, { std::string("%1"), m.npc.name } })
            if (const auto p = f.find(k); p != std::string::npos) f.replace(p, k.size(), v);
        m.npc.name = f;
    }
}

bool hurt(const GameData& s, Monster& m, int damage, std::uint32_t ms) {
    if (!m.alive() || damage <= 0) return false;
    m.hp -= damage;
    m.aware = true;
    if (!m.alive()) set_mode(s, m, "DT", ms);
    else if (damage * 8 >= m.st.hp) set_mode(s, m, "GH", ms);
    return !m.alive();
}

bool follow_path(const Level& L, UnitState& u, float step, const Crowd& crowd) {
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

void fallen_scatter(const GameData& s, std::vector<Monster>& ms_, std::size_t dead, d2d::rules::Rng& rng, std::uint32_t ms) {
    const auto& d = ms_[dead];
    if (s.monsters.types[std::size_t(d.type)].ai != "Fallen") return;
    for (auto& m : ms_)
        if (&m != &d && m.alive() && m.leader == d.leader && std::hypot(m.u.x - d.u.x, m.u.y - d.u.y) < 10
            && s.monsters.types[std::size_t(m.type)].ai == "Fallen")
            m.flee_until = ms + 2000 + std::uint32_t(rng(1000));
}

void block_anim(const GameData& s, Monster& m, std::uint32_t ms) {
    if (m.alive() && m.mode != "A1" && m.mode != "A2" && s.npc_timing(m.npc, "BL").directions) set_mode(s, m, "BL", ms);
}

void attack_starts(const GameData& s, Monster& m, std::string_view mode, d2d::rules::Rng& rng) {
    using d2d::rules::Boss;
    if ((m.boss != Boss::unique && m.boss != Boss::superunique) || !std::ranges::contains(m.mods, d2d::rules::umod::spectralhit)
        || s.monsters.lvl.empty()) return;
    const auto& L = s.monsters.lvl[std::min<std::size_t>(std::size_t(m.st.level), s.monsters.lvl.size() - 1)];
    const int dm = L.dm[std::size_t(m.difficulty)];
    const int e = d2d::rules::kSpectralElement[std::size_t(rng(5))];
    const int lo = dm * s.umods.k[28] / 100;
    m.st.el[2] = { e, 100, lo, std::max(dm * s.umods.k[31] / 100, lo), e == 2 || e == 3 ? 40 : 0, mode };
}

}  // namespace d2d::game
