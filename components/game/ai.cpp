// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for ai.hpp: monsters, NPCs and the merc: spawning, paths, the AI step.
#include "ai.hpp"

#include "gamedata.hpp"

#include <combat.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::game {

std::vector<std::pair<float, float>> walk_path(const Level& level, float x, float y, float goal_x, float goal_y,
                                               const Crowd& crowd , const UnitState* self) {
    auto blocked = [&](float at_x, float at_y) { return level.unit_blocked(at_x, at_y) || crowd.at(at_x, at_y, self); };
    auto sub = [](float value) { return int(std::floor(value * 5)); };
    auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
    const auto steps = d2d::rules::find_path(sub(x), sub(y), sub(goal_x), sub(goal_y),
        [&](int subtile_x, int subtile_y) { return blocked(centre(subtile_x), centre(subtile_y)); });
    std::vector<std::pair<float, float>> pts;
    for (const auto& [step_x, step_y] : steps) pts.emplace_back(centre(step_x), centre(step_y));
    if (!steps.empty() && steps.back() == std::pair(sub(goal_x), sub(goal_y)) && !blocked(goal_x, goal_y)) pts.back() = { goal_x, goal_y };
    // No wall in any subtile one of the unit's five probes (unit_blocked's
    // plus) crosses on the way: every subtile the line touches, not
    // samples along it, so wherever follow_path's steps land on it they
    // stand clear (samples let a corner through that stopped the walk).
    auto walls_clear = [&](float from_x, float from_y, float to_x, float to_y) {
        for (const auto& [offset_x, offset_y] : { std::pair{ 0.f, 0.f }, { -0.2f, 0.f }, { 0.2f, 0.f }, { 0.f, -0.2f }, { 0.f, 0.2f } }) {
            const float ax = (from_x + offset_x) * 5, ay = (from_y + offset_y) * 5, bx = (to_x + offset_x) * 5, by = (to_y + offset_y) * 5;
            int cx = int(std::floor(ax)), cy = int(std::floor(ay));
            const int step_x = bx > ax ? 1 : -1, step_y = by > ay ? 1 : -1;
            const float delta_x = bx != ax ? 1 / std::abs(bx - ax) : HUGE_VALF, delta_y = by != ay ? 1 / std::abs(by - ay) : HUGE_VALF;
            float next_x = bx != ax ? (step_x > 0 ? float(cx + 1) - ax : ax - float(cx)) * delta_x : HUGE_VALF;
            float next_y = by != ay ? (step_y > 0 ? float(cy + 1) - ay : ay - float(cy)) * delta_y : HUGE_VALF;
            for (int left = std::abs(int(std::floor(bx)) - cx) + std::abs(int(std::floor(by)) - cy);; --left) {
                if (level.blocked(centre(cx), centre(cy))) return false;
                if (left <= 0) break;
                if (next_x < next_y) { cx += step_x; next_x += delta_x; }
                else if (next_y < next_x) { cy += step_y; next_y += delta_y; }
                else {                                                     // through a corner: both sides
                    if (level.blocked(centre(cx + step_x), centre(cy)) || level.blocked(centre(cx), centre(cy + step_y))) return false;
                    cx += step_x; cy += step_y; next_x += delta_x; next_y += delta_y; --left;
                }
            }
        }
        return true;
    };
    auto clear = [&](float from_x, float from_y, float to_x, float to_y) {
        if (!walls_clear(from_x, from_y, to_x, to_y)) return false;
        const int step_count = int(std::hypot(to_x - from_x, to_y - from_y) / 0.05f) + 1;
        for (int i = 1; i <= step_count; ++i)
            if (blocked(from_x + (to_x - from_x) * float(i) / float(step_count), from_y + (to_y - from_y) * float(i) / float(step_count))) return false;
        return true;
    };
    std::vector<std::pair<float, float>> out;
    float corner_x = x, corner_y = y;
    for (std::size_t i = 0; i < pts.size();) {
        std::size_t last_clear = i;
        while (last_clear + 1 < pts.size() && clear(corner_x, corner_y, pts[last_clear + 1].first, pts[last_clear + 1].second)) ++last_clear;
        out.push_back(pts[last_clear]);
        std::tie(corner_x, corner_y) = pts[last_clear];
        i = last_clear + 1;
    }
    return out;
}

std::string unique_name(const GameData& game_data, int name_seed) {
    const auto& [pre, suf, app] = game_data.unique_names;
    if (pre.empty() || suf.empty()) return "?";
    auto fill = [](std::string format, std::initializer_list<std::string> args) {   // "%0 %1 %2": positional
        int index = 0;
        for (const auto& arg : args) {
            const std::string placeholder = "%" + std::to_string(index++);
            if (const auto found = format.find(placeholder); found != std::string::npos) format.replace(found, placeholder.size(), arg);
        }
        return format;
    };
    d2d::rules::Rng rng{ std::uint32_t(name_seed) };
    const auto& suffix = suf[std::size_t(rng(int(suf.size())))];
    const auto& prefix = pre[std::size_t(rng(int(pre.size())))];
    auto name = fill(game_data.unique_formats[0].empty() ? "%0 %1" : game_data.unique_formats[0], { prefix, suffix });
    if (rng(100) < 50 && !app.empty()) {
        const auto& appellation = app[std::size_t(rng(int(app.size())))];
        const auto& second_suffix = suf[std::size_t(rng(int(suf.size())))];
        const auto& second_prefix = pre[std::size_t(rng(int(pre.size())))];
        name = fill(game_data.unique_formats[1].empty() ? "%0 %1 %2" : game_data.unique_formats[1], { second_prefix, second_suffix, appellation });
    }
    return name;
}

std::vector<Monster> spawn_monsters(const GameData& game_data, std::span<const d2d::rules::Spawn> spawns, const d2d::rules::Region& region,
                                    d2d::rules::Rng& rng, int difficulty) {
    std::vector<Monster> out;
    for (const auto& spawn : spawns) {
        if (spawn.type < 0 || std::size_t(spawn.type) >= game_data.mon_npc.size()) continue;
        const bool boss = spawn.boss != d2d::rules::Boss::none;
        auto monster = make_monster(game_data, spawn.type, (float(spawn.x) + 0.5f) / 5, (float(spawn.y) + 0.5f) / 5, rng, difficulty, !boss);
        const auto& type_info = game_data.monsters.types[std::size_t(spawn.type)];
        const std::vector<d2d::rules::Components>* sets = nullptr;
        for (std::size_t i = 0; i < region.types.size() && i < region.components.size(); ++i)
            if (region.types[i].first == spawn.type) sets = &region.components[i];
        d2d::rules::Rng unit_seed{ spawn.seed };
        const auto look = d2d::rules::monster_look(sets, type_info.choices, unit_seed);
        for (std::size_t layer = 0; layer < 16; ++layer)
            if (look[layer] < type_info.parts[layer].size()) monster.npc.comp[layer] = type_info.parts[layer][look[layer]];
        if (boss) make_boss(game_data, monster, spawn.boss, spawn.mods, spawn.super, spawn.name_seed, difficulty, rng);
        monster.leader = spawn.leader;
        monster.seed = unit_seed;
        monster.path = spawn.path;
        out.push_back(std::move(monster));
    }
    return out;
}

void set_mode(const GameData& game_data, Monster& monster, std::string_view mode, std::uint32_t now_ms) {
    if (monster.mode != "NU") monster.left_mode = monster.mode;
    monster.mode = mode;
    monster.unit.mode_ms = now_ms;
    monster.unit.walking = mode == "WL" || mode == "RN";
    monster.mode_until = mode == "NU" || mode == "WL" || mode == "DD" ? 0 : now_ms + game_data.npc_timing(monster.npc, mode).length_ms();
}

std::vector<UnitState> npc_start(const Level& level) {
    std::vector<UnitState> out;
    for (std::size_t i = 0; i < level.npcs.size(); ++i) {
        const auto& npc = level.npcs[i];
        out.push_back({ .x = npc.x, .y = npc.y, .wait_until = std::uint32_t(1000 + 700 * i % 3000) });
    }
    return out;
}

void npc_patrol(const Level& level, std::vector<UnitState>& npcs, std::array<int, 3> busy,
                std::uint32_t now_ms, float elapsed, const Crowd& crowd) {
    for (std::size_t i = 0; i < npcs.size(); ++i) {
        const auto& path = level.npcs[i].path;
        if (path.empty()) continue;
        auto& state = npcs[i];
        if (std::ranges::find(busy, int(i)) != busy.end()) {   // busy: stand still
            if (state.walking) { state.walking = false; state.mode_ms = now_ms; }
            state.wait_until = now_ms + 2000;
            continue;
        }
        if (!state.walking) {
            if (now_ms >= state.wait_until) { state.walking = true; state.mode_ms = now_ms; }
            continue;
        }
        const auto [target_x, target_y] = path[state.next % path.size()];
        const float dx = target_x - state.x, dy = target_y - state.y;
        const float dist = std::hypot(dx, dy),
                    step = cells_per_sec(level.npcs[i].velocity) * elapsed;
        if (dist > 0.05f) state.dir = direction16(dx, dy);
        if (dist <= step) {
            state.x = target_x; state.y = target_y; state.walking = false; state.mode_ms = now_ms;
            state.next = (state.next + 1) % path.size();
            state.wait_until = now_ms + 2000 + std::uint32_t((i * 1237 + state.next * 911) % 3000);
        } else if (const float next_x = state.x + dx / dist * step, next_y = state.y + dy / dist * step; !crowd.at(next_x, next_y, &state)) {
            state.x = next_x; state.y = next_y;                                     // else someone's in the way: wait
        }
    }
}

namespace {

constexpr int kAndrialSpray = 164, kAndyPoisonBolt = 201;
int subtile(float cells) { return int(std::floor(cells * 5)); }

// A move's path to subtile (to_x, to_y) (FUN_00649970): none at its own
// subtile or over 100 off; the toward pather (type 0xd, rules::toward_path:
// 5 steps, near 1 at a foe), else the search pather (type 0xf: FUN_00650350,
// FUN_005a6290). Points whose centre it stands on are skipped (FUN_0064fe40).
// ponytail: the search pather (FUN_0067c2d0, 0x28 steps) as
// rules::find_path's turns over its first 0x28 subtiles; FUN_006483a0 and
// path flag 0x1000 left out.
bool path_to(const Level& level, Monster& monster, int to_x, int to_y, bool at_foe, const Crowd& crowd) {
    auto& unit = monster.unit;
    const int x = subtile(unit.x), y = subtile(unit.y);
    auto centre = [](int at) { return (float(at) + 0.5f) / 5; };
    auto blocked = [&](int at_x, int at_y) { return level.unit_blocked(centre(at_x), centre(at_y)) || crowd.at(centre(at_x), centre(at_y), &unit); };
    auto& steps = monster.steps;
    steps.clear();
    monster.step = 0;
    monster.end_x = to_x; monster.end_y = to_y;
    if ((to_x == x && to_y == y) || std::abs(to_x - x) > 100 || std::abs(to_y - y) > 100) return false;
    for (const bool search : { false, true }) {
        if (!search) steps = d2d::rules::toward_path(x, y, to_x, to_y, 5, at_foe ? 1 : 0, blocked);
        else {
            const auto found = d2d::rules::find_path(x, y, to_x, to_y, blocked, 400);
            const std::size_t count = std::min<std::size_t>(found.size(), 0x28);
            std::pair prev{ x, y };
            for (std::size_t n = 0; n < count; ++n) {
                if (n + 1 == count || found[n + 1].first - found[n].first != found[n].first - prev.first
                    || found[n + 1].second - found[n].second != found[n].second - prev.second) steps.push_back(found[n]);
                prev = found[n];
            }
        }
        while (monster.step < int(steps.size()) && centre(steps[std::size_t(monster.step)].first) == unit.x
               && centre(steps[std::size_t(monster.step)].second) == unit.y) ++monster.step;
        if (monster.step < int(steps.size())) return true;
        steps.clear();
        monster.step = 0;
    }
    return false;
}

// A move's frame (FUN_00650840): a chase checks its foe first
// (rules::chase_check: stop at it, re-path when it's moved), then the unit
// steps `step` cells toward its point's centre, taking the next when it
// gets there (one a frame, FUN_00650090). False when the move ends: at the
// foe, the budget spent, no re-path, blocked (FUN_00650150 sets idx =
// count) or past its last point; it stands at its subtile's centre then
// (FUN_006507b0). `spot`: a move to a spot; a chase whose foe is gone ends.
// ponytail: floats in cells for 16.16 subtiles, blocked as monster_step
// has it (not FUN_0064ff90 per subtile crossed); the foe's spot without
// FUN_00679250's +0x68 offset.
bool move_frame(const Level& level, Monster& monster, const Foe* chased, bool spot, int size, float step, const Crowd& crowd) {
    auto& unit = monster.unit;
    bool going = !monster.steps.empty() && (spot || chased);
    if (going) {
        const int x = subtile(unit.x), y = subtile(unit.y), foe_x = chased ? subtile(chased->x) : 0, foe_y = chased ? subtile(chased->y) : 0;
        const int distance = chased ? d2d::rules::unit_distance(foe_x - x, foe_y - y, size, chased->size) : 0;
        const int check = d2d::rules::chase_check(chased, distance, 0, true, monster.end_x - foe_x, monster.end_y - foe_y, monster.step,
                                                  int(monster.steps.size()), x == monster.end_x && y == monster.end_y, monster.budget);
        going = check == 1 || (check == 2 && path_to(level, monster, chased ? foe_x : monster.end_x, chased ? foe_y : monster.end_y, chased, crowd));
    }
    if (going) {
        const auto [point_x, point_y] = monster.steps[std::size_t(monster.step)];
        const float to_x = (float(point_x) + 0.5f) / 5, to_y = (float(point_y) + 0.5f) / 5;
        const bool there = std::hypot(to_x - unit.x, to_y - unit.y) <= step;
        if (!monster_step(level, monster, to_x, to_y, step, crowd)) going = false;
        else if (there) { unit.x = to_x; unit.y = to_y; ++monster.step; }
        going = going && monster.step < int(monster.steps.size());
    }
    if (going) return true;
    const float centre_x = (float(subtile(unit.x)) + 0.5f) / 5, centre_y = (float(subtile(unit.y)) + 0.5f) / 5;
    if (!level.unit_blocked(centre_x, centre_y)) { unit.x = centre_x; unit.y = centre_y; }
    monster.steps.clear();
    monster.step = 0;
    return false;
}

// One of Andariel's skill missiles (FUN_0059fa30, flags 0x20): from her
// toward (dx, dy) cells, its Missiles.txt row's damage at skill level `lvl`
// (rules::monster_skill_level): physical in 256ths (SrcDamage 0: none of hers), its
// poison per frame over ELen as the total, another element in 256ths.
// ToHit 0: it always hits. A Fallen Shaman's fire bolts too.
// ponytail: velocity as cells_per_sec(Vel) like every missile here, not
// FUN_0059fa30's x75/100.
void andariel_missile(const GameData& game_data, const Monster& monster, const std::string& name, float dx, float dy, std::uint32_t now_ms,
                      std::vector<Missile>& missiles, int lvl) {
    const auto found = game_data.missiles.find(name);
    if (found == game_data.missiles.end()) return;
    const auto& missile_info = found->second;
    const auto poison = d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev,
                                               missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
    d2d::rules::MonStats stats;
    stats.level = monster.stats.level;
    stats.to_hit = 1 << 20;
    stats.a2_min = missile_info.min >> 8; stats.a2_max = std::max(missile_info.max >> 8, stats.a2_min);
    const int frames = poison.etype == 3 ? poison.elen : 1;
    if (poison.etype >= 0)
        stats.elements[0] = { poison.etype, 100, int(std::int64_t(poison.elo) * frames >> 8), int(std::int64_t(poison.ehi) * frames >> 8), poison.elen, "A2" };
    const float speed = cells_per_sec(float(missile_info.vel)), distance = std::max(std::hypot(dx, dy), 0.01f);
    missiles.push_back({ &missile_info, monster.unit.x, monster.unit.y, dx / distance * speed, dy / distance * speed, direction32(dx, dy), now_ms,
                         now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, stats });
}

// A Skills.txt row by name, -1 none.
int skill_id(const GameData& game_data, const std::string& name) {
    const auto found = game_data.skills.by_name.find(name);
    return name.empty() || found == game_data.skills.by_name.end() ? -1 : found->second;
}
// The Act 1 skills' MonSeq sequences (their Sk1mode..3): the mode played,
// its frames and the frame of its event.
struct Seq { std::string_view name, mode; std::uint32_t frames, event; };
constexpr std::array<Seq, 5> kSeqs{ { { "seq_shamanresurrect", "A2", 17, 12 }, { "seq_nestlay", "S1", 31, 25 },
                                      { "seq_bloodravencast", "S1", 20, 15 }, { "seq_brquickstrike", "A1", 7, 6 },
                                      { "seq_gargoyletrap", "A1", 5, 0 } } };
// Its skill `id`'s level (Sk*lvl + the difficulty's bonus), 1 when not its own.
int skill_level(const GameData& game_data, const Monster& monster, int id) {
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    for (std::size_t n = 0; n < type_info.skill.size(); ++n)
        if (id >= 0 && skill_id(game_data, type_info.skill[n]) == id) return d2d::rules::monster_skill_level(type_info.sk_lvl[n], monster.difficulty);
    return 1;
}
// The sequence a monster's skill `id` plays, else nullptr.
const Seq* skill_seq(const GameData& game_data, const d2d::rules::MonType& type_info, int id) {
    for (std::size_t n = 0; n < type_info.skill.size(); ++n)
        if (id >= 0 && skill_id(game_data, type_info.skill[n]) == id)
            for (const auto& seq : kSeqs) if (seq.name == type_info.sk_mode[n]) return &seq;
    return nullptr;
}

// The level room holding (x, y) cells, -1 none.
int room_at(const Level& level, float x, float y) {
    for (std::size_t i = 0; i < level.rooms.size(); ++i) {
        const auto& r = level.rooms[i];
        if (x >= float(r.x) && y >= float(r.y) && x < float(r.x + r.width) && y < float(r.y + r.height)) return int(i);
    }
    return -1;
}

// The target search (FUN_005dd7f0): rules::search_pick over the player and
// its pets, best under aidist (blank: 35) by the AI's distance. Line of
// sight (sight_blocked, mask 4, each unit's size) counts only in a preset
// room whose LvlPrest isn't Outdoors (FUN_0061aa40 -> FUN_0066ba70: plain
// rooms and 0x80000 ones need none), and only until the monster first
// finds a target (AI control flag 8, never cleared). Flag 0x40
// (force_sight, cleared here) makes this one search test sight anyway.
// Its spawn area (monster data +0x50, FUN_0066ceb0 at placement; gone once
// it leaves that room, FUN_00554670) shares a flag (+0x24): an unaware
// monster skips the sight test while it's set; finding a target sets it
// when the finder was aware or saw for itself, clears it when it got in on
// the flag. So in caves one that sees you wakes its area; outdoors all see.
// The area is the room_areas rect holding the spawn tile: FUN_0066ceb0
// looks it up in the room's 5x5-subtile grid, where FUN_0066ca50 wrote each
// rect into the cells it covers, and the rects don't overlap, so it's the
// one holding the tile (a room with flag 1 has one, the whole room).
// A player in a town level is refused, with its pets (FUN_0061ab00).
// First the skill-set target (FUN_005dd610, no sight): Attract's monster by
// id while it lives; Confuse's search as confuse_align over FUN_005dd0b0
// mode 5, its primary only. None: it's cleared. Then an evil monster tries
// the player lists and list 9 (neutral monsters: Confuse's, Attract's), a
// neutral one rules::search_near (primary, else secondary). A good one
// (2) finding a target doesn't set flag 8 or the area's flag.
// tools/emu/search.py checks the pick and the flags against game.exe.
// ponytail: a dead pet is skipped (game.exe drops it from the list);
// mode 5's candidates are every foe in the fight, in foes order (game.exe:
// the near rooms' units, room by room), and the area FUN_0061b130 reads is
// the room_areas rect at the unit; a dead monster leaves list 9.
int area_at(const Level& level, float x, float y) {
    const int room = room_at(level, x, y);
    if (room >= 0 && std::size_t(room) < level.room_areas.size())
        for (std::size_t i = 0; i < level.room_areas[std::size_t(room)].size(); ++i)
            if (const auto& a = level.room_areas[std::size_t(room)][i]; int(x) >= a.left && int(y) >= a.top && int(x) < a.right && int(y) < a.bottom)
                return level.id << 20 | room << 8 | int(std::min<std::size_t>(i, 255));
    return -1;
}
struct Search { Foe* target = nullptr; int best = 0, nearest = 0x7fffffff; };
Search search_target(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, std::uint32_t now_ms, AreaSeen* seen) {
    auto& unit = monster.unit;
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    const int aidist = type_info.diff[std::size_t(monster.difficulty)].aidist;
    const int room = room_at(level, unit.x, unit.y);
    if (monster.area == -2) monster.area = area_at(level, monster.home_x, monster.home_y);   // FUN_005b2a00 -> FUN_00552d60: where it was placed
    if (monster.area >= 0 && (monster.area >> 8 & 0xfff) != room) monster.area = -1;
    const auto* in_room = room >= 0 ? &level.rooms[std::size_t(room)] : nullptr;
    const bool indoor = in_room && in_room->kind == 2
        && !(std::size_t(in_room->def) < game_data.prest_outdoors.size() && game_data.prest_outdoors[std::size_t(in_room->def)]);
    const bool force = std::exchange(monster.force_sight, false);
    const bool shared = indoor && !force && monster.area >= 0 && seen;
    const bool flag = shared && !monster.sighted && (*seen)[monster.area];
    const bool need_sight = force || (indoor && !monster.sighted && !flag);
    const bool town = d2d::rules::level_ids::is_town(level.id);
    const int x = subtile(unit.x), y = subtile(unit.y);
    const int best = aidist > 0 ? aidist : 35;
    const auto wall = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x04); };
    const auto nearby = [&](const Foe& foe) { return d2d::rules::near_distance(subtile(foe.x) - x, subtile(foe.y) - y, foe.size); };
    const int here = area_at(level, unit.x, unit.y);
    const auto mode5 = [&](int align) {                             // FUN_005dd0b0 mode 5
        std::vector<d2d::rules::NearFoe> nears;
        for (const auto& foe : foes) {
            auto& n = nears.emplace_back(d2d::rules::NearFoe{ nearby(foe), foe.of ? foe.of->align : 2, foe.threat, foe.of == &monster, foe.of || foe.pet, !foe.alive, town });
            n.blocked = need_sight && n.distance <= 0x23 && d2d::rules::sight_blocked(subtile(foe.x), subtile(foe.y), foe.size, x, y, type_info.size, wall);
            n.waking = foe.of && foe.alive && foe.of->sighted && here >= 0 && area_at(level, foe.x, foe.y) == here;
        }
        return d2d::rules::search_near(nears, align, need_sight);
    };
    Search found;
    if (monster.set_kind == 2) {                                    // FUN_005dd610: Attract's monster
        for (auto& foe : foes)
            if (foe.of && foe.of->id == monster.set_id) { if (foe.alive) found = { &foe, nearby(foe) }; break; }
    } else if (monster.set_kind == 3) {                             // Confuse's: as a random alignment, the primary only
        const auto pick = mode5(d2d::rules::confuse_align(monster.align, monster.seed.next() & 1));
        if (pick.target >= 0) found = { &foes[std::size_t(pick.target)], pick.best };
    }
    if (!found.target) monster.set_kind = 0;                        // FUN_00573120
    if (!found.target && monster.align == 0) {
        std::vector<d2d::rules::SearchFoe> picks;
        std::vector<Foe*> at;
        for (auto& foe : foes) {
            if (foe.of && !(foe.of->align == 1 && foe.alive)) continue;   // list 9: neutral monsters
            const int foe_x = subtile(foe.x), foe_y = subtile(foe.y);
            auto& pick = picks.emplace_back(d2d::rules::SearchFoe{ d2d::rules::ai_distance(foe_x - x, foe_y - y), foe.pet, town && !foe.of, !foe.alive && !foe.of, false, foe.of ? 9 : 0 });
            if (now_ms < monster.blind_until && std::hypot(foe.x - unit.x, foe.y - unit.y) >= 1.5f) pick.dead = true;   // blind: arm's length
            if (pick.pet && pick.dead) pick.distance = 0x7fffffff;
            pick.blocked = need_sight && (pick.list || pick.distance < best) && d2d::rules::sight_blocked(x, y, type_info.size, foe_x, foe_y, foe.size, wall);
            at.push_back(&foe);
        }
        const auto pick = d2d::rules::search_pick(picks, best, need_sight);
        found = { pick.target >= 0 ? at[std::size_t(pick.target)] : nullptr, pick.best, pick.nearest };
    } else if (!found.target) {
        const auto pick = mode5(monster.align);
        if (const int i = pick.target >= 0 ? pick.target : pick.second; i >= 0) found = { &foes[std::size_t(i)], pick.target >= 0 ? pick.best : pick.second_best };
    }
    if (found.target && monster.align != 2) {
        monster.sighted = true;
        if (shared) (*seen)[monster.area] = !flag;
    }
    return found;
}

// A traced MonAI's frame outside an attack (the driver FUN_005b1740:
// target search FUN_005de890 / FUN_005dd7f0, then the AI's think; Andariel
// MonAI 34 FUN_005f5830 -> rules::andariel_think, the rest
// rules::mon_think). A move goes on between thinks — at a foe (a walk or
// run at it) or to a spot (a wander, back-off, keep-off, circle). At a
// think it takes search_target's foe; with none it stands (10 frames,
// the nearest - 10 from 25 subtiles off, 25 from 35), or wanders near home 2-5 s apart where the
// level lets its monsters wander. In melee is unit_distance within
// MeleeRng + 1. A walk with flags 2 that can't set off: 70 % a random
// walk of 4 subtiles (FUN_005de200), else stand 10. Returns false for an
// AI whose think isn't traced. A special AI (the Countess's) thinks in
// place of its MonAI's (FUN_005b15d0). Thinks draw the unit's own seed.
// ponytail: the seed as its look left it (FUN_00573cb0's draws and the
// rest between aren't taken; world.cpp's spawns keep the default); the
// no-target wander on the fight's rng;
// first-sight speech (FUN_005b1140) and the no-target
// wander (FUN_0064d910; the old home wander stands in) left out; in melee
// skips the path test (FUN_00622aa0, mask 0x804); paths
// (FUN_005de190) as path_to's, a circle as a walk to the point n
// subtiles to the side of the target; a back-off sets off when its end
// and first step are open; no teleporting mod.
bool think(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, d2d::rules::Rng& rng, std::uint32_t now_ms,
           float walk, float run, const Crowd& crowd, std::span<Monster> pack, AreaSeen* seen, const OpenDoor* open_door) {
    auto& unit = monster.unit;
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    const auto& per_difficulty = type_info.diff[std::size_t(monster.difficulty)];
    const bool andariel = type_info.ai_name == "Andariel", nest = type_info.ai_name == "FoulCrowNest" || type_info.ai_name == "GargoyleTrap";
    if (!andariel && !d2d::rules::traced_ai(type_info.ai_name)) return false;
    auto idle = [&](int frames) {                                  // FUN_005de080
        if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
        monster.wandering = false;
        monster.next_act = now_ms + std::uint32_t(std::max(frames, 1)) * 40;
    };
    const bool has_run = game_data.npc_timing(monster.npc, "RN").directions > 0;   // RN falls back to WL
    // A move sets off (FUN_005a7c20) with a re-path budget of 0x14
    // (FUN_006490e0) and its path (path_to), at a foe or to unit.goal; one
    // with no path can't start, and a spot move then thinks aidel on
    // (FUN_005a73e0).
    auto set_off = [&](bool running, const Foe* at) {
        monster.budget = 0x14;
        monster.wandering = !at;
        if (!path_to(level, monster, at ? subtile(at->x) : subtile(unit.goal_x), at ? subtile(at->y) : subtile(unit.goal_y), at, crowd)) {
            if (!at) idle(per_difficulty.aidel ? per_difficulty.aidel : 15);
            return false;
        }
        const std::string_view mode = running && has_run ? "RN" : "WL";
        if (monster.mode != mode) set_mode(game_data, monster, mode, now_ms);
        monster.next_act = now_ms;                                 // its end thinks at once (FUN_005a8030)
        return true;
    };
    // A move runs to its end (move_frame), then it thinks: a walk or run's
    // end does so at once (FUN_005a8030: DAT_0073c6d0 flags WL and RN).
    if (monster.mode == "WL" || monster.mode == "RN") {
        const Foe* chased = monster.chase >= 0 && std::size_t(monster.chase) < foes.size() && foes[std::size_t(monster.chase)].alive ? &foes[std::size_t(monster.chase)] : nullptr;
        if (move_frame(level, monster, monster.wandering ? nullptr : chased, monster.wandering, type_info.size, monster.mode == "RN" ? run : walk, crowd)) return true;
        set_mode(game_data, monster, "NU", now_ms);
        monster.wandering = false;
    }
    if (now_ms < monster.next_act) return true;
    // The driver's first step (FUN_005b10e0 → FUN_005b0f50): with opendoors
    // (MonStats flags +0xc & 8) and the monster bit 0x800 under it
    // (FUN_00648eb0), its door (OpenDoor) operated, it stands 5.
    // ponytail: the 0x800 test taken as true (its own footprint stamps it;
    // a moving path's cached word, path +0x54, isn't traced).
    if (type_info.open_doors && open_door && *open_door && (*open_door)(monster, now_ms)) { idle(5); return true; }
    const int x = subtile(unit.x), y = subtile(unit.y);
    const auto [target, best, nearest] = search_target(game_data, level, monster, foes, now_ms, seen);
    monster.aware = target != nullptr;
    auto walk_to = [&](int off_x, int off_y, bool running) {       // to a spot, subtiles off
        unit.goal_x = (float(x + off_x) + 0.5f) / 5; unit.goal_y = (float(y + off_y) + 0.5f) / 5;
        set_off(running, nullptr);
    };
    if (!target) {
        if (andariel || nest || !level.mon.wander || monster.mode != "NU") { idle(nearest < 25 ? 10 : nearest < 35 ? nearest - 10 : 25); return true; }
        const float angle = float(rng(360)) * 3.14159265f / 180, radius = float(rng(300)) / 100;
        unit.goal_x = monster.home_x + std::cos(angle) * radius;
        unit.goal_y = monster.home_y + std::sin(angle) * radius;
        set_off(false, nullptr);
        monster.next_act = now_ms + 2000 + std::uint32_t(rng(3000));
        return true;
    }
    const int target_x = subtile(target->x), target_y = subtile(target->y);
    const float dx = target->x - unit.x, dy = target->y - unit.y;
    d2d::rules::ThinkIn in{ .aip = per_difficulty.aip, .dist = best, .difficulty = monster.difficulty, .level = level.id, .state = &monster.ai_state };
    in.in_melee = d2d::rules::unit_distance(target_x - x, target_y - y, type_info.size, target->size) <= type_info.melee_rng + 1;
    in.got_hit = monster.left_mode == "GH";
    in.life_pct = int(std::int64_t(monster.hit_points) * 100 / std::max(monster.stats.hit_points, 1));   // FUN_00621f20
    for (std::size_t skill = 0; skill < 3; ++skill) in.skill[skill] = !type_info.skill[skill].empty();
    // The Fallen's inputs: a unit in DT within 15 (FUN_005dc530), its lead.
    // A Shaman's corpse (FUN_005dd0b0, the last found): a unique's
    // (FUN_005a0180 mask 8, not 4; FUN_005f1380) any Fallen's or Shaman's
    // not unique; else its own group's (FUN_005dcda0) within aip4 (squared,
    // FUN_005dc380). Either way in DD, not yet gone.
    // ponytail: "the rooms" as within 80 subtiles; the corpse flag
    // (+0xc4 bit 1) and FUN_0063a770 as corpse_used; units are monsters.
    const auto self = pack.empty() ? pack.size() : std::size_t(&monster - pack.data());
    bool rally = false;
    int corpse = -1;
    in.command = &monster.ai_command; in.rally = &rally;
    in.leader = !pack.empty() && monster.leader == int(self);
    using d2d::rules::Boss;
    auto unique = [](const Monster& other) { return other.boss == Boss::unique || other.boss == Boss::superunique; };
    for (std::size_t i = 0; i < pack.size(); ++i) {
        const auto& other = pack[i];
        const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y;
        if (i != self && other.mode == "DT" && d2d::rules::ai_distance(off_x, off_y) < 15) in.dying = true;
        if (type_info.ai_name != "FallenShaman" || other.alive() || other.mode != "DD" || other.corpse_used) continue;
        const int base = game_data.monsters.types[std::size_t(other.type)].base;
        if (unique(monster) ? (base == 19 || base == 58) && !unique(other) && d2d::rules::ai_distance(off_x, off_y) < 80
                            : other.leader == int(self) && off_x * off_x + off_y * off_y <= in.aip[3] * in.aip[3])
            corpse = int(i);
    }
    in.corpse = corpse >= 0;
    // A nest's: the frame (its init's taken at its first think), the spot
    // spawnx / spawny off (FUN_005fd350 tests crownest's only).
    const int frame = int(now_ms / 40);
    if (nest && monster.ai_state == 0) monster.ai_state = frame;
    in.frame = frame; in.state2 = &monster.ai_state2;
    in.spot_free = !level.unit_blocked(unit.x + float(type_info.spawn_x) / 5, unit.y + float(type_info.spawn_y) / 5);
    in.home_dist = d2d::rules::ai_distance(subtile(monster.home_x) - x, subtile(monster.home_y) - y);
    in.off_x = target_x - x; in.off_y = target_y - y;
    int pace = 0;
    in.pace = &pace; in.state3 = &monster.ai_state3;
    in.target_life_pct = target->life_pct;
    in.laying = now_ms < monster.laid_until;
    in.velocity = type_info.velocity; in.run = type_info.run;
    in.aidel = per_difficulty.aidel ? per_difficulty.aidel : 15;
    auto use = [&](std::string_view mode, int skill) {             // FUN_005dead0 / FUN_005ddf90
        unit.dir = direction16(dx, dy);
        set_mode(game_data, monster, mode, now_ms);
        monster.skill = skill; monster.struck = false; monster.wandering = false;
        monster.skill_frame = 0; monster.skill_x = target_x; monster.skill_y = target_y;
        attack_starts(game_data, monster, mode, rng);
    };
    // FUN_005defe0 / FUN_005df140: n subtiles on from the target, by axis.
    auto away = [&](int n, bool running) {
        const int off_x = n * ((x > target_x) - (x < target_x)), off_y = n * ((y > target_y) - (y < target_y));
        const float goal_x = (float(x + off_x) + 0.5f) / 5, goal_y = (float(y + off_y) + 0.5f) / 5;
        const float span = std::max(std::hypot(goal_x - unit.x, goal_y - unit.y), 0.01f), probe = std::min(0.2f, span);
        if (level.unit_blocked(goal_x, goal_y) || level.unit_blocked(unit.x + (goal_x - unit.x) / span * probe, unit.y + (goal_y - unit.y) / span * probe)) return false;
        walk_to(off_x, off_y, running);
        return true;
    };
    using d2d::rules::MonAct;
    d2d::rules::Think act;
    if (andariel) {
        using d2d::rules::AndarielAct;
        switch (d2d::rules::andariel_think(in.in_melee, in.aip, monster.seed)) {
            case AndarielAct::spray: use("SC", kAndrialSpray); return true;   // SQ: seq_andarielspray plays SC
            case AndarielAct::bolt: use("A1", kAndyPoisonBolt); return true;
            case AndarielAct::melee: act = { MonAct::a1 }; break;
            case AndarielAct::idle: act = { MonAct::idle, 5 }; break;
            case AndarielAct::walk: act = { MonAct::walk, 7 }; break;
        }
    } else if (monster.special == 0xd) {                          // the Countess: rooms are the level's (FUN_0061b130)
        auto room = [&](float at_x, float at_y) { return room_at(level, at_x, at_y); };
        const int home = room(monster.home_x, monster.home_y);
        const d2d::rules::CountessIn where{ room(unit.x, unit.y) != home, room(target->x, target->y) != home,
                                            x == subtile(monster.home_x) && y == subtile(monster.home_y) };
        act = d2d::rules::countess_think(in, where, monster.path, monster.seed);
    } else {
        act = d2d::rules::mon_think(type_info.ai_name, in, monster.seed, away);
    }
    monster.move_pct = pace;                                       // the next mode takes it (FUN_005a63f0)
    if (rally)                                                     // FUN_0058f730 / FUN_0058ef40: its leader's group, itself too
        for (auto& other : pack)
            if (other.alive() && other.leader == monster.leader) other.ai_command = 1;
    for (;;) switch (act.act) {
        case MonAct::idle: idle(act.n); return true;
        case MonAct::a1: use("A1", -1); return true;
        case MonAct::a2: use("A2", -1); return true;
        case MonAct::s2: use("S2", -1); return true;
        case MonAct::walk: case MonAct::approach: case MonAct::run:  // FUN_005deb60 at the target
            if (set_off(act.act == MonAct::run, target)) {
                monster.chase = int(target - foes.data());
                return true;
            }
            if (act.act == MonAct::walk && act.n == 0) monster.ai_command = 0;   // the Fallen's charge ends (FUN_0058ed10)
            if (act.act == MonAct::walk && (act.n & 1)) monster.force_sight = true;   // FUN_005dd230: flag 0x40
            if (act.act != MonAct::walk || !(act.n & 2)) { idle(0); return true; }
            act = d2d::rules::walk_failed(monster.seed);
            break;
        case MonAct::wander: walk_to(act.x, act.y, false); return true;
        case MonAct::around: walk_to(target_x - x + act.x, target_y - y + act.y, false); return true;
        case MonAct::home: walk_to(subtile(monster.home_x) - x, subtile(monster.home_y) - y, false); return true;
        case MonAct::point:                                        // FUN_005dead0(Sk1mode, Skill1, no unit, x, y)
            use(type_info.sk_mode[0], skill_id(game_data, type_info.skill[0]));
            monster.skill_x = act.x; monster.skill_y = act.y;
            unit.dir = direction16(float(act.x - x), float(act.y - y));
            return true;
        case MonAct::keep: {                                       // FUN_005de4e0: toward, or from, the target to keep x off
            const int go = std::min(std::abs(best - act.x), act.n), sign = best < act.x ? -1 : 1;
            const int across = std::abs(target_x - x), down = std::abs(target_y - y), sum = std::max(across + down, go);
            int step_x = sum ? across * go / sum : 0, step_y = sum ? down * go / sum : 0;
            while (sum && step_x + step_y < go) { ++step_x; ++step_y; }
            walk_to(((target_x > x) - (target_x < x)) * step_x * sign, ((target_y > y) - (target_y < y)) * step_y * sign, false);
            return true;
        }
        case MonAct::circle: {                                     // FUN_005df7d0
            const float side = act.x ? 1.f : -1.f, length = std::max(std::hypot(dx, dy), 0.01f), off = float(act.n) / 5;
            unit.goal_x = target->x - dy / length * off * side; unit.goal_y = target->y + dx / length * off * side;
            set_off(false, nullptr);
            return true;
        }
        case MonAct::skill: {                                      // its Sk mode's sequence (kSeqs) plays out
            // ponytail: a skill without a sequence here stands instead (none
            // in Act 1: cr_archer / cr_lancer carry no Skill1..3, so their
            // thinks' skill tests draw nothing).
            const auto& name = type_info.skill[std::size_t(act.n)];
            const int id = skill_id(game_data, name);
            // Spider Lay and the vampires' shots play their Sk mode itself.
            // ponytail: VampireFirewall / VampireMeteor (srvdofunc 24 / 28)
            // stand, as no Act 1 vampire's aip5 lets it cast them.
            const Seq* seq = skill_seq(game_data, type_info, id);
            const bool plain = name == "SpiderLay" || name == "VampireFireball" || name == "VampireMissile";
            if ((!seq && !plain) || (name == "Resurrect" && corpse < 0)) { idle(0); return true; }
            use(seq ? seq->mode : std::string_view(type_info.sk_mode[std::size_t(act.n)]), id);
            if (seq) monster.mode_until = now_ms + seq->frames * game_data.npc_timing(monster.npc, seq->mode).ms_per_frame();
            monster.skill_unit = name == "Resurrect" ? corpse : -1;
            // Where a Nest's young come out: Blood Raven's spot off the
            // target, else spawnx / spawny off itself.
            monster.skill_x = act.x || act.y ? target_x + act.x : x + type_info.spawn_x;
            monster.skill_y = act.x || act.y ? target_y + act.y : y + type_info.spawn_y;
            if (monster.skill_unit >= 0) unit.dir = direction16(pack[std::size_t(corpse)].unit.x - unit.x, pack[std::size_t(corpse)].unit.y - unit.y);
            if (type_info.ai_name == "GargoyleTrap") {                 // square on (FUN_005f9490): along the axis it's nearer the target's
                const bool down = std::abs(target_x - x) < std::abs(target_y - y);
                monster.skill_x = down ? x : target_x; monster.skill_y = down ? target_y : y;
                unit.dir = direction16(float(monster.skill_x - x), float(monster.skill_y - y));
            }
            return true;
        }
        case MonAct::die:                                          // no loot, no experience
            monster.hit_points = 0;
            set_mode(game_data, monster, "DT", now_ms);
            return true;
        case MonAct::none: case MonAct::untraced: return true;
    }
}

// The Countess's firewall at (skill_x, skill_y) subtiles (srvdofunc 24,
// FUN_005c9ea0, flags 0x21 / 1): a countessfirewallmaker each way across
// the line from her, the countessfirewall on the point, at her skill level.
void countess_firewall(const GameData& game_data, const Monster& monster, std::uint32_t now_ms, std::vector<Missile>& missiles) {
    const auto maker = game_data.missiles.find("countessfirewallmaker"), fire = game_data.missiles.find("countessfirewall");
    if (maker == game_data.missiles.end() || fire == game_data.missiles.end()) return;
    const int lvl = skill_level(game_data, monster, monster.skill);
    const auto& fire_info = fire->second;
    const float x = (float(monster.skill_x) + 0.5f) / 5, y = (float(monster.skill_y) + 0.5f) / 5;
    const float across_x = -(monster.unit.y - y), across_y = monster.unit.x - x, length = std::max(std::hypot(across_x, across_y), 0.01f);
    const float speed = cells_per_sec(float(maker->second.vel));
    Missile burner{ &fire_info, x, y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(fire_info.range + fire_info.lev_range * lvl, 1)) * 40, monster.stats };
    burner.level = lvl;
    burner.row = d2d::rules::row_damage(fire_info.etype, fire_info.emin, fire_info.emax, fire_info.emin_lev, fire_info.emax_lev, fire_info.hitshift, fire_info.elen,
                                        fire_info.elen_lev, lvl);
    for (const float side : { 1.f, -1.f }) {
        Missile made = burner;
        made.info = &maker->second; made.sub = &fire_info;
        made.velocity_x = across_x / length * speed * side; made.velocity_y = across_y / length * speed * side;
        made.dir = direction32(made.velocity_x, made.velocity_y);
        made.dies = now_ms + std::uint32_t(std::max(maker->second.range, 1)) * 40;
        missiles.push_back(made);
    }
    missiles.push_back(burner);
}

}  // namespace

bool monster_update(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, d2d::rules::Rng& rng,
                    std::uint32_t now_ms, float elapsed, const Crowd& crowd, std::vector<Missile>& missiles, std::span<Monster> pack,
                    std::vector<Monster>* born, AreaSeen* seen, const OpenDoor* open_door) {
    auto& unit = monster.unit;
    // After the nearest one alive (the player or the merc), or the monster
    // its search took (Confuse, Attract).
    Foe* pick = &foes[0];
    for (auto& foe : foes)
        if (!foe.of && foe.alive && (!pick->alive || std::hypot(foe.x - unit.x, foe.y - unit.y) < std::hypot(pick->x - unit.x, pick->y - unit.y))) pick = &foe;
    if (monster.chase >= 0 && std::size_t(monster.chase) < foes.size() && foes[std::size_t(monster.chase)].of && foes[std::size_t(monster.chase)].alive) pick = &foes[std::size_t(monster.chase)];
    Foe& foe = *pick;
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    if (!monster.alive()) {
        if (monster.mode == "DT" && now_ms >= monster.mode_until) set_mode(game_data, monster, "DD", now_ms);
        return false;
    }
    if (now_ms < monster.stun_until && monster.mode != "GH" && monster.mode != "BL") {   // stunned: stands (a get-hit plays out first)
        if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
        return false;
    }
    if (monster.mode == "GH" || monster.mode == "BL") {
        if (now_ms < monster.mode_until) return false;
        set_mode(game_data, monster, "NU", now_ms);
    }
    const float dx = foe.x - unit.x, dy = foe.y - unit.y, dist = std::hypot(dx, dy);
    const auto miss = type_info.miss_a2.empty() ? game_data.missiles.end() : game_data.missiles.find(type_info.miss_a2);
    // Andariel's spray (AndrialSpray: srvstfunc 46 FUN_005cb4d0 keeps where
    // the target stood, srvdofunc 88 FUN_005cb580 fires): SC frames 4..12
    // (MonSeq seq_andarielspray's events; AnimData ANSCHTH's agree) each
    // send one andarielspray from her at rules::andariel_spray_aim.
    if (monster.mode == "SC" && monster.skill == kAndrialSpray) {
        const int frame = int((now_ms - unit.mode_ms) / game_data.npc_timing(monster.npc, "SC").ms_per_frame());
        const int x = subtile(unit.x), y = subtile(unit.y);
        while (monster.skill_frame < std::min(frame, 12))
            if (++monster.skill_frame >= 4) {
                const auto [aim_x, aim_y] = d2d::rules::andariel_spray_aim(d2d::rules::direction64(x, y, monster.skill_x, monster.skill_y), monster.skill_frame);
                andariel_missile(game_data, monster, "andarielspray", float(aim_x) / 5, float(aim_y) / 5, now_ms, missiles, skill_level(game_data, monster, kAndrialSpray));
            }
        if (now_ms < monster.mode_until) return false;
        set_mode(game_data, monster, "NU", now_ms);
        monster.skill = -1;
        monster.next_act = now_ms + std::uint32_t(type_info.diff[std::size_t(monster.difficulty)].aidel) * 40;
    }
    // An attack, a taunt (the Fallen's S2), a skill's sequence (kSeqs, its
    // event frame), a young one coming out (spawnmode S1).
    const bool attack = monster.mode == "A1" || monster.mode == "A2";
    if (attack || monster.mode == "S1" || monster.mode == "S2" || monster.mode == "SC") {
        const auto& timing = game_data.npc_timing(monster.npc, monster.mode);
        const Seq* seq = skill_seq(game_data, type_info, monster.skill);
        const auto skill = [&](const char* name) { return seq && monster.skill == skill_id(game_data, name); };
        if (!monster.struck && now_ms >= unit.mode_ms + (seq ? seq->event * timing.ms_per_frame() : timing.action_ms())) {
            monster.struck = true;
            const auto& shot = monster.mode == "A1" ? type_info.miss_a1 : type_info.miss_a2;   // MissA1 / MissA2
            const auto fired = shot.empty() || !attack ? game_data.missiles.end() : game_data.missiles.find(shot);
            // Quick Strike (srvdofunc 92, FUN_005cbf90 -> FUN_0056ecb0): its
            // srvmissilea raven1, Blood Raven's MissA1 too.
            if ((monster.skill < 0 || skill("Quick Strike")) && fired != game_data.missiles.end()) {   // fire: at the foe, from here
                const auto& missile_info = fired->second;
                const float speed = cells_per_sec(float(missile_info.vel)), distance = std::max(dist, 0.01f);
                Missile x{ &missile_info, unit.x, unit.y, dx / distance * speed, dy / distance * speed, direction32(dx, dy), now_ms,
                           now_ms + std::uint32_t(missile_info.range) * 40, monster.stats };
                const bool first = monster.mode == "A1";                       // an A1 shot carries A1's damage
                x.src.a2_min = (first ? monster.stats.a1_min : monster.stats.a2_min) * missile_info.src_damage / 128 + missile_info.min;
                x.src.a2_max = (first ? monster.stats.a1_max : monster.stats.a2_max) * missile_info.src_damage / 128 + missile_info.max;
                if (first)                                                     // and its elements (a Skeleton Mage's El1 A1): the blow rolls A2's
                    for (auto& element : x.src.elements) element.mode = element.mode == "A1" ? "A2" : "";
                missiles.push_back(x);
                // Multishot (FUN_005a3610, the missile hook): two more, aimed a
                // subtile to either side.
                using d2d::rules::Boss;
                if ((monster.boss == Boss::unique || monster.boss == Boss::superunique) && std::ranges::contains(monster.mods, d2d::rules::umod::multishot))
                    for (const float side : { 0.2f, -0.2f }) {
                        const float side_x = dx - dy / std::max(dist, 0.01f) * side, side_y = dy + dx / std::max(dist, 0.01f) * side;
                        const float side_distance = std::max(std::hypot(side_x, side_y), 0.01f);
                        Missile y = x;
                        y.velocity_x = side_x / side_distance * speed; y.velocity_y = side_y / side_distance * speed; y.dir = direction32(side_x, side_y);
                        missiles.push_back(y);
                    }
            } else if (skill("ShamanFire")) {                                 // srvdofunc 85: srvmissilea shafire1, + TransLvl
                andariel_missile(game_data, monster, "shafire" + std::to_string(1 + type_info.trans_lvl), dx, dy, now_ms, missiles, skill_level(game_data, monster, monster.skill));
            } else if (skill("GargoyleTrap")) {                               // srvdofunc 93 (FUN_005cc050): srvmissilea shafire3 square on at the target
                // ponytail: from the trap itself, not a sixth of the way on less a subtile.
                const float to_x = float(monster.skill_x - subtile(unit.x)) / 5, to_y = float(monster.skill_y - subtile(unit.y)) / 5;
                andariel_missile(game_data, monster, "shafire3", to_x, to_y, now_ms, missiles, skill_level(game_data, monster, monster.skill));
            } else if (skill("Nest")) {                                       // srvdofunc 91 (FUN_005cbe00): its spawn at the skill's spot, in spawnmode
                // ponytail: the young's flags (0x4020000) and the skill's
                // state on them (Skills +0xe6) unread; a normal monster.
                if (const auto young = game_data.monsters.by_id.find(type_info.spawn); born && young != game_data.monsters.by_id.end()) {
                    auto laid = make_monster(game_data, young->second, (float(monster.skill_x) + 0.5f) / 5, (float(monster.skill_y) + 0.5f) / 5, rng, monster.difficulty);
                    laid.aware = true;
                    laid.seed = d2d::rules::Rng{ rng.next() };                 // its own seed, drawn off the game's
                    if (type_info.spawn_mode != "NU" && !type_info.spawn_mode.empty()) set_mode(game_data, laid, type_info.spawn_mode, now_ms);
                    born->push_back(std::move(laid));
                }
            } else if (skill("Resurrect")) {                                  // srvdofunc 97 (FUN_005ccb10 / FUN_005cc960): the corpse back at full life
                // ponytail: the revived state's look (FUN_005cc960's state) left out.
                if (monster.skill_unit >= 0 && std::size_t(monster.skill_unit) < pack.size()) {
                    auto& raised = pack[std::size_t(monster.skill_unit)];
                    if (!raised.alive() && raised.mode == "DD" && !raised.corpse_used) {
                        raised.hit_points = raised.last_hp = raised.stats.hit_points;
                        raised.ai_state = raised.ai_command = 0; raised.skill = -1; raised.left_mode = "NU";
                        set_mode(game_data, raised, "NU", now_ms);
                    }
                }
            } else if (monster.skill == kAndyPoisonBolt) {                     // FUN_0056ecb0: one andypoisonbolt at the target
                andariel_missile(game_data, monster, "andypoisonbolt", dx, dy, now_ms, missiles, skill_level(game_data, monster, kAndyPoisonBolt));
            } else if (monster.skill >= 0 && monster.skill == skill_id(game_data, "CountessFirewall")) {
                countess_firewall(game_data, monster, now_ms, missiles);
            } else if (const int fireball = skill_id(game_data, "VampireFireball"); monster.skill >= 0 && (monster.skill == fireball || monster.skill == skill_id(game_data, "VampireMissile"))) {
                // srvmissile vampirefireball / firehead at the target.
                andariel_missile(game_data, monster, monster.skill == fireball ? "vampirefireball" : "firehead", dx, dy, now_ms, missiles, skill_level(game_data, monster, monster.skill));
            } else if (monster.skill >= 0 && monster.skill == skill_id(game_data, "SpiderLay")) {
                // srvdofunc 23 (FUN_005c9c10): its aurastate spiderlay for
                // auralen 300 frames, aurastat velocitypercent -100.
                // ponytail: the slowed state on those about (auratargetstate) left out.
                monster.laid_until = now_ms + 300 * 40;
            } else if (attack && foe.alive && dist <= kMeleeReach + 0.3f) {
                // Melee: block, reductions, resistances; a hit that lands
                // pays the foe's thorns (lightning ones less its resistance).
                auto stats = monster.stats;                                  // Weaken, Decrepify, Battle Cry, Taunt, a boss's aura: its damage %
                for (int* damage : { &stats.a1_min, &stats.a1_max }) *damage = std::max(*damage * (100 + monster.dmg_pct + monster.aura_dmg) / 100, 0);
                stats.to_hit += stats.to_hit * monster.aura_th / 100;
                const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.moving, stats, false, rng);
                foe.take(taken);
                if (taken.hit && monster.mana_hi > 0) foe.mana_burn += rng.range(monster.mana_lo, monster.mana_hi);   // Mana Burn
                // Cursed (FUN_005a2530, the hit hook): 3 in 4, Amplify Damage
                // (skill 66) at level mlvl / 5 + 1.
                // ponytail: on the one struck, not everyone in the skill's
                // radius; the monster's rng, not its own seed; melee only.
                if (taken.hit && (monster.boss == d2d::rules::Boss::unique || monster.boss == d2d::rules::Boss::superunique)
                    && std::ranges::contains(monster.mods, d2d::rules::umod::curse) && rng(4) != 0)
                    foe.amplify = std::max(foe.amplify, monster.stats.level / 5 + 1);
                foe.melee_by.push_back(&monster);
                const auto& res = type_info.diff[std::size_t(monster.difficulty)].res;
                const int thorns = foe.fighter.thorns + d2d::rules::resisted(foe.fighter.thorns_light, res[3]) + taken.damage * foe.fighter.thorns_pct / 100
                                 + taken.damage * monster.reflect_pct / 100;  // Iron Maiden
                if (taken.hit && thorns > 0 && hurt(game_data, monster, thorns, now_ms)) return true;
            }
        }
        if (now_ms < monster.mode_until) return false;
        set_mode(game_data, monster, "NU", now_ms);
        monster.skill = -1;
        monster.next_act = now_ms + std::uint32_t(type_info.diff[std::size_t(monster.difficulty)].aidel) * 40;
    }
    // Chilled, it moves at coldeffect % slower; its think's pace on, Spider
    // Lay's -100 % off (velocitypercent, stat 0x43).
    // ponytail: laid, it creeps at the 10 % floor, not rooted.
    const float chill = now_ms < monster.chill_until ? float(100 + type_info.diff[std::size_t(monster.difficulty)].cold_effect) / 100.f : 1.f;
    const int pace = monster.move_pct - (now_ms < monster.laid_until ? 100 : 0);
    const float walk = cells_per_sec(float(type_info.velocity)) * elapsed * chill * float(std::max(100 + monster.speed_pct + monster.boss_speed + pace, 10)) / 100;
    if (now_ms < monster.flee_until) {
        if (monster.mode != "WL") set_mode(game_data, monster, "WL", now_ms);
        if (!monster_step(level, monster, unit.x - dx, unit.y - dy, cells_per_sec(float(type_info.run)) * elapsed * chill, crowd)) monster.flee_until = 0;
        return false;
    }
    const float run = cells_per_sec(float(type_info.run)) * elapsed * chill * float(std::max(100 + monster.speed_pct + monster.boss_speed + pace, 10)) / 100;
    if (think(game_data, level, monster, foes, rng, now_ms, walk, run, crowd, pack, seen, open_door)) return false;
    // An AI not traced finds its foe as the traced ones do (search_target),
    // at its thinks: when next_act is due and no chase is under way (a chase
    // runs as think()'s does, move_frame). With none it thinks again aidel
    // on (FUN_005a73e0).
    // ponytail: it chases the nearest foe whichever it found.
    const bool chasing = monster.mode == "WL" && !monster.steps.empty() && monster.aware;
    if (now_ms >= monster.next_act && !chasing) {
        if (type_info.open_doors && open_door && *open_door && (*open_door)(monster, now_ms)) {   // its door first, as think's
            if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
            monster.next_act = now_ms + 5 * 40;
            return false;
        }
        const Foe* found = search_target(game_data, level, monster, foes, now_ms, seen).target;
        monster.aware = foe.alive && found;
        if (found) monster.chase = int(found - foes.data());
        if (!monster.aware) monster.next_act = now_ms + std::uint32_t(type_info.diff[std::size_t(monster.difficulty)].aidel) * 40;
    }
    if (foe.alive && monster.aware) {
        // Teleportation (mod 26: MonTeleport, AI flag 0x20; FUN_005b11f0):
        // at a think, 40 %, then — under 30 % life, or a shooter with the
        // foe within 10 subtiles — 15 %: to a free spot in its room, and
        // hurt, 1 in 4 to heal its level in life.
        // ponytail: the room as its 8x8-cell block, the spot by 20 tries
        // (game.exe: FUN_0054dc40); "within 10" read as the foe's distance;
        // no teleport animation or skill cast.
        if (now_ms >= monster.next_act && (monster.boss == d2d::rules::Boss::unique || monster.boss == d2d::rules::Boss::superunique)
            && std::ranges::contains(monster.mods, d2d::rules::umod::teleport) && rng(100) < 40) {
            const bool low = std::int64_t(monster.hit_points) * 100 < std::int64_t(monster.stats.hit_points) * 30;
            if ((low || (miss != game_data.missiles.end() && dist * 5 < 10)) && rng(100) < 15) {
                const float region_x = std::floor(unit.x / 8) * 8, region_y = std::floor(unit.y / 8) * 8;
                for (int tries = 0; tries < 20; ++tries) {
                    const float next_x = region_x + float(rng(80)) / 10, next_y = region_y + float(rng(80)) / 10;
                    if (level.unit_blocked(next_x, next_y)) continue;
                    unit.x = next_x; unit.y = next_y; unit.walking = false;
                    if (low && rng(100) < 25) monster.hit_points = std::min(monster.stats.hit_points, monster.hit_points + monster.stats.level);
                    monster.next_act = now_ms + std::uint32_t(type_info.diff[std::size_t(monster.difficulty)].aidel) * 40;
                    set_mode(game_data, monster, "NU", now_ms);
                    return false;
                }
            }
        }
        if (dist <= kMeleeReach) {
            unit.dir = direction16(dx, dy);
            if (now_ms >= monster.next_act) { set_mode(game_data, monster, "A1", now_ms); monster.struck = false; attack_starts(game_data, monster, "A1", rng); }
            else if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
            return false;
        }
        // Shooters (MissA2) shoot from up to 7 cells: each think (aidel)
        // the aip2 chance to fire, else close in.
        // ponytail: aip2 read as the shoot chance; for AIs not traced.
        if (miss != game_data.missiles.end() && dist < 7 && now_ms >= monster.next_act) {
            monster.next_act = now_ms + std::uint32_t(type_info.diff[std::size_t(monster.difficulty)].aidel) * 40;
            if (rng(100) < std::max(type_info.diff[std::size_t(monster.difficulty)].aip[1], 1)) {
                unit.dir = direction16(dx, dy);
                set_mode(game_data, monster, "A2", now_ms);
                monster.struck = false;
                attack_starts(game_data, monster, "A2", rng);
                return false;
            }
        }
        if (monster.mode != "WL") {                                // sets off at a think (FUN_005a7c20), else aidel on
            if (now_ms < monster.next_act) return false;
            monster.budget = 0x14;
            if (!path_to(level, monster, subtile(foe.x), subtile(foe.y), true, crowd)) {
                monster.next_act = now_ms + std::uint32_t(type_info.diff[std::size_t(monster.difficulty)].aidel) * 40;
                if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
                return false;
            }
            set_mode(game_data, monster, "WL", now_ms);
        }
        if (!move_frame(level, monster, &foe, false, type_info.size, walk, crowd)) { set_mode(game_data, monster, "NU", now_ms); monster.next_act = now_ms; }
        return false;
    }
    monster.aware = false;
    if (!unit.walking) {
        if (now_ms < unit.wait_until || !level.mon.wander) return false;
        const float angle = float(rng(360)) * 3.14159265f / 180, radius = float(rng(300)) / 100;
        unit.goal_x = monster.home_x + std::cos(angle) * radius;
        unit.goal_y = monster.home_y + std::sin(angle) * radius;
        set_mode(game_data, monster, "WL", now_ms);
        return false;
    }
    if (std::hypot(unit.goal_x - unit.x, unit.goal_y - unit.y) <= walk || !monster_step(level, monster, unit.goal_x, unit.goal_y, walk, crowd)) {
        if (std::hypot(unit.goal_x - unit.x, unit.goal_y - unit.y) <= walk) { unit.x = unit.goal_x; unit.y = unit.goal_y; }
        set_mode(game_data, monster, "NU", now_ms);
        unit.wait_until = now_ms + 2000 + std::uint32_t(rng(3000));
    }
    return false;
}

bool monster_step(const Level& level, Monster& monster, float target_x, float target_y, float step, const Crowd& crowd) {
    auto& unit = monster.unit;
    const float dx = target_x - unit.x, dy = target_y - unit.y, dist = std::hypot(dx, dy);
    if (dist < 0.01f) return true;
    unit.dir = direction16(dx, dy);
    const float fraction = std::min(step, dist) / dist, next_x = unit.x + dx * fraction, next_y = unit.y + dy * fraction;
    if (level.unit_blocked(next_x, next_y)) return false;
    for (const auto* other : crowd.units)                   // into someone: blocked; out of an overlap: fine
        if (other != &unit && std::abs(other->x - next_x) < 0.3f && std::abs(other->y - next_y) < 0.3f
            && std::hypot(other->x - next_x, other->y - next_y) < std::hypot(other->x - unit.x, other->y - unit.y)) return false;
    unit.x = next_x; unit.y = next_y;
    return true;
}

std::string merc_name(const GameData& game_data, const GameData::Merc& merc, int index) {
    const auto& first_name = merc.name_first;
    if (first_name.size() < 2) return first_name;
    const int first = std::atoi(first_name.substr(first_name.size() - 2).c_str());
    const auto key = first_name.substr(0, first_name.size() - 2) + std::format("{:02}", first + index);
    const auto found = lookup_string(game_data, key);
    return found ? u16_to_latin1(*found) : key;
}

void merc_follow(const Level& level, UnitState& unit, float player_x, float player_y, float speed, std::uint32_t now_ms, float elapsed,
                 const Crowd& crowd) {
    auto& path = unit.path;
    const float dist = std::hypot(player_x - unit.x, player_y - unit.y);
    if (dist > 12) { unit.x = player_x + 1; unit.y = player_y + 1; unit.walking = false; path.clear(); return; }
    const bool moving = unit.walking ? dist > 1.5f : dist > 3;
    if (moving != unit.walking) { unit.walking = moving; unit.mode_ms = now_ms; }
    if (!moving) { path.clear(); return; }
    // Re-plan when the player has moved a cell from where it was planned.
    if (path.empty() || std::hypot(unit.goal_x - player_x, unit.goal_y - player_y) > 1.f) {
        path = walk_path(level, unit.x, unit.y, player_x, player_y, crowd, &unit);
        unit.goal_x = player_x; unit.goal_y = player_y;
    }
    const float old_x = unit.x, old_y = unit.y;
    follow_path(level, unit, speed * elapsed, crowd);
    if (std::hypot(unit.x - old_x, unit.y - old_y) > speed * elapsed * 0.3f) unit.stuck_since = 0;
    else if (!unit.stuck_since) unit.stuck_since = now_ms;
    else if (now_ms - unit.stuck_since > 1500) { unit.x = player_x + 1; unit.y = player_y + 1; unit.walking = false; unit.stuck_since = 0; path.clear(); }
}

Monster make_monster(const GameData& game_data, int type, float x, float y, d2d::rules::Rng& rng, int difficulty, bool stats) {
    const auto& type_info = game_data.monsters.types[std::size_t(type)];
    Monster monster;
    monster.type = type;
    monster.npc = game_data.mon_npc[std::size_t(type)];
    for (std::size_t layer = 0; layer < 16; ++layer)
        if (!type_info.parts[layer].empty()) monster.npc.comp[layer] = type_info.parts[layer][std::size_t(rng(int(type_info.parts[layer].size())))];
    monster.unit.x = monster.home_x = x;
    monster.unit.y = monster.home_y = y;
    monster.unit.dir = rng(16);
    monster.unit.wait_until = std::uint32_t(rng(4000));
    if (stats) monster.stats = d2d::rules::monster_stats(game_data.monsters, type, difficulty, rng);
    monster.hit_points = monster.last_hp = monster.stats.hit_points;
    monster.difficulty = std::clamp(difficulty, 0, 2);
    return monster;
}

void make_boss(const GameData& game_data, Monster& monster, d2d::rules::Boss kind, const std::vector<int>& mods, int super, int name_seed,
               int difficulty, d2d::rules::Rng& rng) {
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    const auto boss = d2d::rules::boss_stats(game_data.umods, type_info, kind, mods, difficulty);
    monster.stats = d2d::rules::monster_stats(game_data.monsters, monster.type, difficulty, rng, boss.level_add);
    monster.boss = kind;
    monster.mods = mods;
    monster.super = super;
    monster.stats.hit_points += monster.stats.hit_points * boss.hp_pct / 100;
    monster.stats.hit_points = std::max(monster.stats.hit_points + monster.stats.hit_points * boss.life_after_pct / 100, 1);
    monster.stats.armor_class += monster.stats.armor_class * boss.defense_pct / 100;
    if (boss.phys_resist >= 0) monster.boss_res[0] = boss.phys_resist - type_info.diff[std::size_t(std::clamp(difficulty, 0, 2))].res[0];
    monster.stats.exp *= boss.exp_mult;
    for (int* damage : { &monster.stats.a1_min, &monster.stats.a1_max, &monster.stats.a2_min, &monster.stats.a2_max }) *damage += *damage * boss.dmg_pct / 100;
    monster.stats.to_hit += monster.stats.to_hit * boss.tohit_pct / 100;
    if (boss.double_defense) monster.stats.armor_class *= 2;
    if (boss.phys_resist < 0) monster.boss_res = boss.res_add;
    else { const int set = monster.boss_res[0]; monster.boss_res = boss.res_add; monster.boss_res[0] = set; }
    monster.boss_speed = boss.velocity_pct;
    if (boss.elem >= 0 && !game_data.monsters.lvl.empty()) {                      // enchanted: MonLvl damage x %
        const auto& level_row = game_data.monsters.lvl[std::min<std::size_t>(std::size_t(monster.stats.level), game_data.monsters.lvl.size() - 1)];
        const int damage_pct = level_row.damage[std::size_t(std::clamp(difficulty, 0, 2))];
        int put = 0;
        for (auto& element : monster.stats.elements)
            if (element.type < 0 && put < 2) element = { boss.elem, 100, damage_pct * boss.elem_min_pct / 100, std::max(damage_pct * boss.elem_max_pct / 100, damage_pct * boss.elem_min_pct / 100), boss.elem_len, put++ ? "A2" : "A1" };
    }
    // Mana burn (FUN_005a1f90): manadrainmin / max (stats 62 / 63) = MonLvl
    // damage x the elemental % rows for its kind.
    // ponytail: FUN_005a00f0's rows read as the enchanted ones'.
    if (std::ranges::contains(mods, d2d::rules::umod::manahit) && !game_data.monsters.lvl.empty()) {
        const auto& level_row = game_data.monsters.lvl[std::min<std::size_t>(std::size_t(monster.stats.level), game_data.monsters.lvl.size() - 1)];
        const int damage_pct = level_row.damage[std::size_t(std::clamp(difficulty, 0, 2))], difficulty_index = std::clamp(difficulty, 0, 2);
        const int base = kind == d2d::rules::Boss::minion ? 16 : kind == d2d::rules::Boss::champion ? 22 : 28;
        monster.mana_lo = damage_pct * game_data.umods.constants[std::size_t(base + difficulty_index)] / 100;
        monster.mana_hi = std::max(damage_pct * game_data.umods.constants[std::size_t(base + 3 + difficulty_index)] / 100, monster.mana_lo);
    }
    if (std::ranges::contains(mods, d2d::rules::umod::aura)) {
        const auto aura = d2d::rules::boss_aura(monster.stats.level, name_seed, super);
        monster.aura = aura.skill; monster.aura_lvl = aura.level;
    }
    if (super == 6) { monster.half_freeze = true; monster.special = 0xd; }   // FUN_005a49b0 case 6, the Countess: stat 0x76, FUN_005b0e00(0xd)
    monster.hit_points = monster.stats.hit_points;
    monster.last_hp = monster.hit_points;
    if (super >= 0 && std::size_t(super) < game_data.superuniques.size()) monster.npc.name = game_data.superuniques[std::size_t(super)].name;
    else if (kind == d2d::rules::Boss::unique) monster.npc.name = unique_name(game_data, name_seed);
    else if (kind == d2d::rules::Boss::champion) {                      // "Champion Zombie", "Ghostly Fallen" (FUN_004ac870)
        auto format = string_id(game_data, d2d::rules::kChampionFormat);
        if (format.empty()) format = "%0 %1";
        for (const auto& [placeholder, value] : { std::pair{ std::string("%0"), string_id(game_data, d2d::rules::champion_word(mods)) }, { std::string("%1"), monster.npc.name } })
            if (const auto found = format.find(placeholder); found != std::string::npos) format.replace(found, placeholder.size(), value);
        monster.npc.name = format;
    }
}

bool hurt(const GameData& game_data, Monster& monster, int damage, std::uint32_t now_ms) {
    if (!monster.alive() || damage <= 0) return false;
    monster.hit_points -= damage;
    monster.aware = true;
    if (!monster.alive()) set_mode(game_data, monster, "DT", now_ms);
    else if (damage * 8 >= monster.stats.hit_points) set_mode(game_data, monster, "GH", now_ms);
    return !monster.alive();
}

bool follow_path(const Level& level, UnitState& unit, float step, const Crowd& crowd) {
    auto& path = unit.path;
    auto& x = unit.x;
    auto& y = unit.y;
    auto& dir = unit.dir;
    while (!path.empty() && step > 0) {
        const auto [target_x, target_y] = path.front();
        const float dx = target_x - x, dy = target_y - y, dist = std::hypot(dx, dy);
        if (dist > 0.05f) dir = direction16(dx, dy);
        if (dist <= step) { x = target_x; y = target_y; step -= dist; path.erase(path.begin()); continue; }
        const float next_x = x + dx / dist * step, next_y = y + dy / dist * step;
        if (level.unit_blocked(next_x, next_y) || crowd.at(next_x, next_y, &unit)) { path.clear(); break; }
        x = next_x; y = next_y;
        break;
    }
    return !path.empty();
}

void block_anim(const GameData& game_data, Monster& monster, std::uint32_t now_ms) {
    if (monster.alive() && monster.mode != "A1" && monster.mode != "A2" && game_data.npc_timing(monster.npc, "BL").directions) set_mode(game_data, monster, "BL", now_ms);
}

void attack_starts(const GameData& game_data, Monster& monster, std::string_view mode, d2d::rules::Rng& rng) {
    using d2d::rules::Boss;
    if ((monster.boss != Boss::unique && monster.boss != Boss::superunique) || !std::ranges::contains(monster.mods, d2d::rules::umod::spectralhit)
        || game_data.monsters.lvl.empty()) return;
    const auto& level_row = game_data.monsters.lvl[std::min<std::size_t>(std::size_t(monster.stats.level), game_data.monsters.lvl.size() - 1)];
    const int damage_pct = level_row.damage[std::size_t(monster.difficulty)];
    const int element = d2d::rules::kSpectralElement[std::size_t(rng(5))];
    const int low = damage_pct * game_data.umods.constants[28] / 100;
    monster.stats.elements[2] = { element, 100, low, std::max(damage_pct * game_data.umods.constants[31] / 100, low), element == 2 || element == 3 ? 40 : 0, mode };
}

}  // namespace d2d::game
