// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for ai.hpp: monsters, NPCs and the merc: spawning, paths, the AI step.
#include "ai.hpp"

#include "game.hpp"
#include "gamedata.hpp"
#include "log.hpp"

#include <combat.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <town_npcs.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::game {

std::vector<std::pair<float, float>> player_walk(const Level& level, float x, float y, float goal_x, float goal_y, bool to_unit,
                                                 const Crowd& crowd, const UnitState* self) {
    auto sub = [](float value) { return int(std::floor(value * 5)); };
    auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
    const std::pair from{ sub(x), sub(y) };
    const auto steps = d2d::rules::player_path(from.first, from.second, sub(goal_x), sub(goal_y), to_unit ? 1 : 0, to_unit, [&](int subtile_x, int subtile_y) {
        return level.unit_blocked(centre(subtile_x), centre(subtile_y), self ? self->shape : UnitShape{}) || crowd.at(centre(subtile_x), centre(subtile_y), self);
    });
    // ponytail: a point given twice (the toward pather's cut-short subtile)
    // is dropped; game.exe spends a frame on it (FUN_00650090).
    std::vector<std::pair<float, float>> points;
    std::pair last = from;
    for (const auto& step : steps)
        if (step != last) { points.emplace_back(centre(step.first), centre(step.second)); last = step; }
    return points;
}

namespace {

std::vector<std::pair<float, float>> monster_walk(const Level& level, const UnitState& unit, const Crowd& crowd) {
    auto sub = [](float value) { return int(std::floor(value * 5)); };
    auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
    const auto steps = d2d::rules::monster_path(sub(unit.x), sub(unit.y), sub(unit.goal_x), sub(unit.goal_y), unit.to_unit, [&](int subtile_x, int subtile_y) {
        return level.unit_blocked(centre(subtile_x), centre(subtile_y), unit.shape) || crowd.at(centre(subtile_x), centre(subtile_y), &unit);
    });
    std::vector<std::pair<float, float>> points;
    std::pair last{ sub(unit.x), sub(unit.y) };
    for (const auto& step : steps)                                // ponytail: a repeated point is dropped (game.exe: a frame on it)
        if (step != last) { points.emplace_back(centre(step.first), centre(step.second)); last = step; }
    return points;
}

// FUN_00648eb0: a door's 0x800 (BlocksVis) under the unit's test shape
// (FUN_0064d9b0) — what an opendoors monster (mask 0x3401) walks onto.
bool on_door(const Level& level, const UnitState& unit) {
    UnitShape door_test = unit.shape;
    door_test.mask = 0x0800;
    return level.unit_blocked(unit.x, unit.y, door_test);
}

}  // namespace

bool set_off(const Level& level, UnitState& unit, float goal_x, float goal_y, bool to_unit, const Crowd& crowd) {
    unit.goal_x = goal_x; unit.goal_y = goal_y; unit.to_unit = to_unit;
    unit.budget = 0x14;
    unit.path = monster_walk(level, unit, crowd);
    unit.path_points = int(unit.path.size());
    return !unit.path.empty();
}

bool walk_on(const Level& level, UnitState& unit, float step, const Crowd& crowd) {
    if (unit.path.empty()) return false;
    const auto end = unit.path.back();
    if (follow_path(level, unit, step, crowd)) return true;
    if (unit.x != end.first || unit.y != end.second) return false;      // blocked on the way
    auto sub = [](float value) { return int(std::floor(value * 5)); };
    if ((sub(unit.x) == sub(unit.goal_x) && sub(unit.y) == sub(unit.goal_y)) || unit.budget == 0) return false;
    unit.budget = std::max(unit.budget - unit.path_points, 0);
    unit.path = monster_walk(level, unit, crowd);
    unit.path_points = int(unit.path.size());
    return !unit.path.empty();
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
                                    d2d::rules::Rng& rng, int difficulty, int players) {
    std::vector<Monster> out;
    for (const auto& spawn : spawns) {
        if (spawn.type < 0 || std::size_t(spawn.type) >= game_data.mon_npc.size()) continue;
        const bool boss = spawn.boss != d2d::rules::Boss::none;
        auto monster = make_monster(game_data, spawn.type, (float(spawn.x) + 0.5f) / 5, (float(spawn.y) + 0.5f) / 5, rng, difficulty, !boss, players);
        const auto& type_info = game_data.monsters.types[std::size_t(spawn.type)];
        const std::vector<d2d::rules::Components>* sets = nullptr;
        for (std::size_t i = 0; i < region.types.size() && i < region.components.size(); ++i)
            if (region.types[i].first == spawn.type) sets = &region.components[i];
        monster.cage = spawn.cage;
        d2d::rules::Rng unit_seed{ spawn.seed };
        const auto look = d2d::rules::monster_look(sets, type_info.choices, unit_seed);
        monster.variant = d2d::rules::monster_variant(spawn.type, unit_seed);
        monster.props = d2d::rules::monster_props(game_data.rules, type_info, difficulty, unit_seed);   // FUN_00573cb0's end, after the look
        for (std::size_t layer = 0; layer < 16; ++layer)
            if (look[layer] < type_info.parts[layer].size()) monster.npc.comp[layer] = type_info.parts[layer][look[layer]];
        if (boss) make_boss(game_data, monster, spawn.boss, spawn.mods, spawn.super, spawn.name_seed, difficulty, rng, players);
        else if (auto act_boss = d2d::rules::act_boss(spawn.type, type_info.base)) {   // FUN_005b1cf0: flag 8, no unique stats
            monster.boss = d2d::rules::Boss::unique;
            monster.mods = std::move(act_boss->mods);
            monster.half_freeze = act_boss->half_freeze;
            if (std::ranges::contains(monster.mods, d2d::rules::umod::poisonhit)) {   // its init, FUN_005a1e00(1): then FUN_005a1370's +75
                poison_hit(game_data, monster, difficulty, true);
                int immune = 0;
                for (std::size_t i = 0; i < 6; ++i) immune += type_info.diff[std::size_t(std::clamp(difficulty, 0, 2))].res[i] + monster.boss_res[i] > 99;
                if (immune < 2) monster.boss_res[5] += 75;
            }
        }
        monster.leader = spawn.leader;
        if (spawn.dead) { monster.hit_points = 0; monster.mode = "DD"; }    // act 5's dead MonPlace codes: a corpse from the start
        monster.seed = unit_seed;
        monster.path = spawn.path;
        out.push_back(std::move(monster));
    }
    return out;
}

void set_mode(const GameData& game_data, Monster& monster, std::string_view mode, std::uint32_t now_ms) {
    if (monster.mode != "NU") monster.left_mode = monster.mode;
    monster.mode = mode;
    if (mode == "A1" || mode == "A2" || mode == "SC" || mode == "S1" || mode == "S2" || mode == "S3" || mode == "S4") monster.swing_mode = mode;
    monster.seq_frame = -1;                                   // a sequence's row: drawn again as it plays
    monster.unit.mode_ms = now_ms;
    monster.unit.walking = mode == "WL" || mode == "RN";
    // ponytail: a monster's FHR is its MonProp's (props.fhr); stat 99 from
    // a state or aura (FUN_00625500 reads the unit's full stat) isn't kept.
    // ponytail: the GH hold is now the server's (game.exe's length), but the
    // renderer still plays the GH animation at the base rate and loops it, so
    // a long hold (a Zombie's 17 ticks over 5 frames) replays it; game.exe's
    // player client tick (FUN_00463390) steps the animation by the rate the
    // length comes from (+0x4c) and draws one frame fewer than the hold; a
    // monster's client tick is not
    // read. Not modelled: the renderer spreading the animation over the hold.
    // What the client shows after its last frame is U (not read).
    monster.mode_until = mode == "NU" || mode == "WL" || mode == "DD" ? 0
        : mode == "GH"                                                ? now_ms + get_hit_ms(game_data.npc_timing(monster.npc, mode), monster.props.fhr)
                                                                      : now_ms + game_data.npc_timing(monster.npc, mode).length_ms() * 100 / std::uint32_t(100 + anim_rate_pct(monster, mode));
}

std::vector<UnitState> npc_start(const Level& level) {
    std::vector<UnitState> out;
    for (const auto& npc : level.npcs) out.push_back({ .x = npc.x, .y = npc.y, .shape = npc.shape });
    return out;
}

namespace {

// FUN_00648820's 64 directions (0 screen south, clockwise; rules::direction64)
// as a 16-direction facing: a screen vector that way through direction16.
int facing16(int dir64) {
    const float angle = float(dir64) * (2 * 3.14159265f / 64);
    const float screen_x = -std::sin(angle) / (kIsoW / 2), screen_y = std::cos(angle) / (kIsoH / 2);
    return direction16((screen_x + screen_y) / 2, (screen_y - screen_x) / 2);
}

}  // namespace

void npc_patrol(const GameData& game_data, const Level& level, std::vector<UnitState>& npcs, std::array<int, 3> busy,
                std::uint32_t now_ms, float elapsed, const Crowd& crowd, const UnitState* player, bool guard_aside) {
    constexpr std::uint32_t kFrameMs = 40;
    for (std::size_t i = 0; i < npcs.size() && i < level.npcs.size(); ++i) {
        const auto& npc = level.npcs[i];
        if (npc.town_ai == TownAi::none || ((npc.town_ai == TownAi::npc || npc.town_ai == TownAi::towner) && npc.path.empty())) continue;
        auto& state = npcs[i];
        // A talk starting (FUN_00548b00): its path stopped, AI control +0x14
        // = 40, the think next frame.
        const bool listed = std::ranges::find(busy, int(i)) != busy.end();
        if (listed && !state.brain.listed) {
            if (state.walking) { state.walking = false; state.path.clear(); state.mode_ms = now_ms; }
            state.brain.held = 40;
            state.wait_until = now_ms + kFrameMs;
        }
        state.brain.listed = listed;
        if (state.walking) {                                    // a walk's end (or a block) thinks at once
            if (!walk_on(level, state, cells_per_sec(npc.velocity) * elapsed, crowd)) {
                state.walking = false; state.path.clear(); state.mode_ms = now_ms; state.wait_until = now_ms;
            }
            continue;
        }
        if (!state.mode.empty()) {                              // a special: NU at its end, the think aidel on
            if (now_ms < state.wait_until) continue;
            state.mode = {}; state.mode_ms = now_ms; state.wait_until = now_ms + 15 * kFrameMs;
            continue;
        }
        if (now_ms < state.wait_until) continue;
        std::vector<d2d::rules::NpcPoint> points;
        for (std::size_t k = 0; k < npc.path.size(); ++k)
            points.push_back({ k < npc.actions.size() ? npc.actions[k] : 0, int(npc.path[k].first * 5), int(npc.path[k].second * 5) });
        const int x = int(std::floor(state.x * 5)), y = int(std::floor(state.y * 5));
        d2d::rules::NpcVisitor visitor{ .talking = listed };
        if (player && state.alert) {                            // FUN_005dde80: within 16, with a "!" for them
            visitor.x = int(std::floor(player->x * 5)); visitor.y = int(std::floor(player->y * 5));
            visitor.distance = d2d::rules::npc_reach(x, y, visitor.x, visitor.y, 2);
            visitor.present = visitor.distance < 16;
            visitor.talking = listed || (visitor.present && busy[0] >= 0);
        }
        // The palace guard (JarJar): aside as record 0xb's data+0x40 is 2 (rules::ArcaneQuest::guard_state).
        const auto act = npc.town_ai == TownAi::jarjar ? d2d::rules::jarjar_think(state.brain, x, y, int(now_ms / kFrameMs), guard_aside, guard_aside, visitor)
                       : npc.town_ai == TownAi::vendor ? d2d::rules::vendor_think(npc.seed)
                                                       : d2d::rules::npc_think(state.brain, npc.seed, points, x, y, npc.hc_idx, npc.modes, 1, visitor, npc.town_ai == TownAi::towner);
        if (act.face >= 0) state.dir = facing16(act.face);
        state.greets = state.greets || act.greet;          // unit sound 0x12: the client's greeting (FUN_004e0590 kind 1)
        switch (act.kind) {
        case d2d::rules::NpcAct::Kind::stand: state.wait_until = now_ms + std::uint32_t(act.frames) * kFrameMs; break;
        case d2d::rules::NpcAct::Kind::walk:
            state.walking = set_off(level, state, (float(act.x) + 0.5f) / 5, (float(act.y) + 0.5f) / 5, false, crowd);   // FUN_005ded90
            state.mode_ms = now_ms;
            if (!state.walking) state.wait_until = now_ms + std::uint32_t(act.fail) * kFrameMs;
            break;
        case d2d::rules::NpcAct::Kind::mode:
            state.mode = act.mode == 8 ? "S1" : act.mode == 9 ? "S2" : act.mode == 10 ? "S3" : "S4";
            state.mode_ms = now_ms;
            state.wait_until = now_ms + game_data.npc_timing(npc, state.mode).length_ms();
            break;
        }
    }
}

namespace {

constexpr int kAndrialSpray = 164, kAndyPoisonBolt = 201;
int subtile(float cells) { return int(std::floor(cells * 5)); }
// Diablo's Bone Prison round (0x73d4f0): boneprison1..4 at the target's
// diagonals, (1, 1) first.
constexpr std::array<std::pair<int, int>, 4> kPrison{ { { 1, 1 }, { 1, -1 }, { -1, -1 }, { -1, 1 } } };
int room_at(const Level& level, float x, float y);
// FUN_005fd350 (ECX the caster's class, x, y, flag): where a nest's young
// come out, by its MonStats BaseId: crownest (0xce) 3 below, sarcophagus
// (0xe4) 2 below, vile mother (0x12a) the spot itself (with `flag`: always
// free), 0x14e 2 up and left (mask 0x1c0), 0x210 2 right, 4 below; that
// spot inside a room (FUN_00463740) with a plus (FUN_0064d9b0, size 2)
// clear of 0x3c01 there, the units' bits the Crowd's. Any other family:
// free.
bool nest_spot_free(const GameData& game_data, const Level& level, const Crowd& crowd, int type, int x, int y, bool flag) {
    const auto& types = game_data.monsters.types;
    const int base = type >= 0 && std::size_t(type) < types.size() ? types[std::size_t(type)].base : -1;
    auto bin = [&](int row) { return int(game_data.mon_bin[std::size_t(row)]); };
    std::uint16_t mask = 0x3c01;
    if (base == bin(0xce)) y += 3;
    else if (base == bin(0xe4)) y += 2;
    else if (base == bin(0x12a)) { if (flag) return true; }
    else if (base == bin(0x14e)) { x -= 2; y -= 2; mask = 0x1c0; }
    else if (base == bin(0x210)) { x += 2; y += 4; }
    else return true;
    if (room_at(level, (float(x) + 0.5f) / 5, (float(y) + 0.5f) / 5) < 0) return false;
    for (const auto& [off_x, off_y] : { std::pair{ 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } }) {
        const float at_x = (float(x + off_x) + 0.5f) / 5, at_y = (float(y + off_y) + 0.5f) / 5;
        if (level.blocked(at_x, at_y, mask & 0x0fff) || ((mask & 0x1000) && crowd.at(at_x, at_y, nullptr))) return false;
    }
    return true;
}

}  // namespace

bool spawn_spot(const GameData& game_data, const Level& level, const Crowd& crowd, Spawning::LevelState* rooms_in_play, int type, int x, int y, int radius,
                int room, int& out_x, int& out_y) {
    const auto index = std::size_t(room);
    const auto rect = room < 0 ? std::nullopt : level_room(level, index);
    if (!rect || !rooms_in_play || index >= rooms_in_play->up.size() || !rooms_in_play->up[index] || index >= rooms_in_play->room_seeds.size()) return false;
    d2d::rules::SpawnRoom spawn_room{ rect->x * 5, rect->y * 5, rect->width * 5, rect->height * 5, rooms_in_play->room_seeds[index] };
    const auto& shape = game_data.mon_npc[std::size_t(type)].shape;
    const bool placed = d2d::rules::monster_detail::place(spawn_room, x, y, radius, [&](int at_x, int at_y) {
        const UnitState probe{ .x = (float(at_x) + 0.5f) / 5, .y = (float(at_y) + 0.5f) / 5, .shape = shape };
        return !level.unit_blocked(probe.x, probe.y, probe.shape) && !crowd.at(probe.x, probe.y, &probe);
    }, out_x, out_y);
    rooms_in_play->room_seeds[index] = spawn_room.seed;
    return placed;
}

std::optional<std::pair<int, int>> free_spot_near(const Level& level, const Crowd& crowd, const UnitState* self, int x, int y, int size, std::uint16_t mask) {
    if (size > 3) return std::nullopt;                      // FUN_0064d9b0's default: 0xffff
    const UnitShape probe = size <= 1 ? UnitShape{ 0, std::uint8_t(size), mask } : size == 2 ? UnitShape{ 1, 2, mask } : UnitShape{ 2, 3, mask };
    return nearest_spot(x, y, [&](int at_x, int at_y) {
        const float cell_x = (float(at_x) + 0.5f) / 5, cell_y = (float(at_y) + 0.5f) / 5;
        return !level.unit_blocked(cell_x, cell_y, probe) && !crowd.at(cell_x, cell_y, probe, self);
    });
}

namespace {

// A move's path to subtile (to_x, to_y) (FUN_00649970): rules::monster_path,
// the toward pather then the wall pather (FUN_00650350, FUN_005a6290).
// ponytail: FUN_006483a0 and path flag 0x1000 left out.
bool path_to(const Level& level, Monster& monster, int to_x, int to_y, bool at_foe, const Crowd& crowd) {
    auto& unit = monster.unit;
    auto centre = [](int coord) { return (float(coord) + 0.5f) / 5; };
    monster.step = 0;
    monster.end_x = to_x; monster.end_y = to_y;
    monster.steps = d2d::rules::monster_path(subtile(unit.x), subtile(unit.y), to_x, to_y, at_foe, [&](int at_x, int at_y) {
        return level.unit_blocked(centre(at_x), centre(at_y), unit.shape) || crowd.at(centre(at_x), centre(at_y), &unit);
    });
    return !monster.steps.empty();
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
    if (!level.unit_blocked(centre_x, centre_y, unit.shape)) { unit.x = centre_x; unit.y = centre_y; }
    monster.steps.clear();
    monster.step = 0;
    return false;
}

// One of Andariel's skill missiles (FUN_0059fa30, flags 0x20): from her
// toward (dx, dy) cells, its Missiles.txt row's damage at skill level `lvl`
// (rules::monster_skill_level): physical in 256ths (SrcDamage 0: none of hers), its
// poison per frame over ELen as the total, another element in 256ths.
// ToHit 0: it always hits. A Fallen Shaman's fire bolts too. A row naming a
// Skill (`carried`: the Summoner's Fire Ball, Glacial Spike, Frost Nova)
// carries that skill's element at the level instead (FUN_0064b860).
// ponytail: a carried skill's physical damage (MinDam..) left out; the
// Act 2 casters' skills have none.
void andariel_missile(const GameData& game_data, const Monster& monster, const std::string& name, float dx, float dy, std::uint32_t now_ms,
                      std::vector<Missile>& missiles, int lvl, const d2d::rules::Skill* carried = nullptr) {
    const auto found = game_data.missiles.find(name);
    if (found == game_data.missiles.end()) return;
    const auto& missile_info = found->second;
    const auto poison = carried && !missile_info.skill.empty()
        ? d2d::rules::row_damage(carried->etype, carried->emin, carried->emax, carried->emin_lev, carried->emax_lev, carried->hitshift, carried->elen, carried->elen_lev, lvl)
        : d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev,
                                 missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
    const auto stats = missile_blow(missile_info, poison, monster.stats.level);
    const float speed = missile_speed(missile_info, lvl), distance = std::max(std::hypot(dx, dy), 0.01f);
    missiles.push_back({ &missile_info, monster.unit.x, monster.unit.y, dx / distance * speed, dy / distance * speed, missile_facing(monster.unit.x, monster.unit.y, dx, dy), now_ms,
                         now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, stats });
    missiles.back().level = lvl; missiles.back().to = { monster.unit.x + dx, monster.unit.y + dy }; missiles.back().owner = monster.id;
    if (carried) missiles.back().skill = carried->id;
}

// A Skills.txt row by name, -1 none.
int skill_id(const GameData& game_data, const std::string& name) {
    const auto found = game_data.skills.by_name.find(name);
    return name.empty() || found == game_data.skills.by_name.end() ? -1 : found->second;
}
// Its skill `id`'s MonSeq sequence (its Sk*mode names one; MonSeq.txt,
// GameData::mon_seqs): the rows played, a frame each; null for a plain mode.
using SeqRows = std::vector<GameData::MonSeqFrame>;
// A plain-mode skill d2d casts for a monster (no sequence; FUN_0056f7f0
// at its mode's action frame, SC, S1, A1 or A2): a srvmissile at the
// target, a nova (22), a fire wall (24), a curse (30: Weaken), scattered
// bolts (17), a missile at the target (28), a breath (95), Bestow (96), the
// monsters' teleport (98), the poison nova (99), Hydra (144), Baal's cold
// missiles (139) and tentacles (140), the imps' Inferno (126), Teleport
// (129) and Fire Missile (132), the overseer's Cry Help (128) and Whip
// (131), Impregnate (133), Siege Beast Stomp (134: its mode, nothing else),
// a war cry (68: Madawc's Shout).
// ponytail: other do-funcs stand.
bool castable(const d2d::rules::Skill& skill) {
    using enum d2d::rules::ServerDoFunction;
    return !skill.srvmissile.empty() || std::ranges::contains(std::array{ kNova, kMissileWall, kCurse, kScatteredBolts, kMissileAtTarget, kMonsterBreath, kBestow,
                                                                          kMonTeleport, kMonPoisonNova, kHydra, kDiabLight, kDiabCold, kFingerMageSpider, kDoomKnightMissile,
                                                                          kGuidedMissile, kSelfState, kRegurgitatorEat, kMonCurseCast, kDiabPrison, kDiabWall,
                                                                          kBaalColdMissiles, kBaalTentacle, kImpInferno, kImpTeleport, kImpFireMissile,
                                                                          kCryHelp, kOverseerWhip, kImpregnate, kSiegeBeastStomp, kWarCry, kCorpseExplosion, kMinionSpawner }, skill.srvdofunc);
}
// A monster's skill strike (Whirlwind's do 76, Leap Attack's FUN_005da660):
// its A1 swing with calc1 % more damage, the skill's to-hit and its element
// (FUN_0056e0c0) at the level.
// ponytail: the to-hit is the swing's plus ToHit + LevToHit a level, as %.
d2d::rules::MonStats skill_strike(const GameData& game_data, const Monster& monster, const d2d::rules::Skill& skill, int lvl) {
    auto stats = monster.stats;
    const int more = d2d::rules::eval_calc(game_data.skills, skill.calc[0], {}, skill.id, lvl);
    for (int* damage : { &stats.a1_min, &stats.a1_max }) *damage = std::max(*damage * (100 + more + monster.dmg_pct) / 100, 0);
    stats.to_hit += stats.to_hit * (skill.tohit + skill.levtohit * (lvl - 1)) / 100;
    const auto added = d2d::rules::row_damage(skill.etype, skill.emin, skill.emax, skill.emin_lev, skill.emax_lev, skill.hitshift, skill.elen, skill.elen_lev, lvl);
    if (added.etype >= 0)
        for (auto& element : stats.elements)
            if (element.type < 0) { element = { added.etype, 100, added.elo >> 8, added.ehi >> 8, added.elen, "A1" }; break; }
    return stats;
}
// Impregnate's test (FUN_005d2140): a live monster, not a putrid defiler
// (0x222) or painworm (0x227), not impregnated (state 0x6e), not a
// player's pet (FUN_006259b0 2).
bool impregnable(const GameData& game_data, const Monster& host) {
    if (!host.alive() || host.impregnated || host.align == 2 || host.type < 0) return false;
    const auto& types = game_data.monsters.types;
    return host.type != int(game_data.mon_bin[0x222]) && host.type != int(game_data.mon_bin[0x227]) && std::size_t(host.type) < types.size();
}
// Its skill `id`'s level (Sk*lvl + the difficulty's bonus), 1 when not its own.
int skill_level(const GameData& game_data, const Monster& monster, int id) {
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    for (std::size_t slot = 0; slot < type_info.skill.size(); ++slot)
        if (id >= 0 && skill_id(game_data, type_info.skill[slot]) == id) return d2d::rules::monster_skill_level(type_info.sk_lvl[slot], monster.difficulty);
    return 1;
}
// The sequence a monster's skill `id` plays, else nullptr.
const SeqRows* skill_seq(const GameData& game_data, const d2d::rules::MonType& type_info, int id) {
    for (std::size_t slot = 0; slot < type_info.skill.size(); ++slot)
        if (id >= 0 && skill_id(game_data, type_info.skill[slot]) == id)
            if (const auto found = game_data.mon_seqs.find(type_info.sk_mode[slot]); found != game_data.mon_seqs.end() && !found->second.empty()) return &found->second;
    return nullptr;
}

// The level room holding (x, y) cells, -1 none.
int room_at(const Level& level, float x, float y) {
    for (std::size_t i = 0; i < level.rooms.size(); ++i) {
        const auto& room = level.rooms[i];
        if (x >= float(room.x) && y >= float(room.y) && x < float(room.x + room.width) && y < float(room.y + room.height)) return int(i);
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
// the near rooms' units, room by room); a dead monster leaves list 9.
// Mode 5's "in the searcher's area" compares area ids (FUN_0061b130,
// area_id_at), which carry across rooms; the flag (+0x24) is the record's.
int area_at(const Level& level, float x, float y) {
    const int room = room_at(level, x, y);
    if (room >= 0 && std::size_t(room) < level.room_areas.size())
        for (std::size_t i = 0; i < level.room_areas[std::size_t(room)].size(); ++i)
            if (const auto& area = level.room_areas[std::size_t(room)][i]; int(x) >= area.left && int(y) >= area.top && int(x) < area.right && int(y) < area.bottom)
                return level.id << 20 | room << 8 | int(std::min<std::size_t>(i, 255));
    return -1;
}
struct Search { Foe* target = nullptr; int best = 0, nearest = 0x7fffffff; };
Search search_target(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, AreaSeen* seen) {
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
    const auto here = area_id_at(game_data, level, unit.x, unit.y, unit.x, unit.y);   // FUN_0061b130: mode 5 compares area ids
    const auto mode5 = [&](int align) {                             // FUN_005dd0b0 mode 5
        std::vector<d2d::rules::NearFoe> nears;
        for (const auto& foe : foes) {
            auto& entry = nears.emplace_back(d2d::rules::NearFoe{ nearby(foe), foe.of ? foe.of->align : 2, foe.threat, foe.of == &monster, foe.of || foe.pet, !foe.alive, town });
            entry.blocked = need_sight && entry.distance <= 0x23 && d2d::rules::sight_blocked(subtile(foe.x), subtile(foe.y), foe.size, x, y, type_info.size, wall);
            entry.waking = foe.of && foe.alive && foe.of->sighted && area_id_at(game_data, level, foe.x, foe.y, foe.x, foe.y) == here;
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
        std::vector<Foe*> pick_foes;
        for (auto& foe : foes) {
            // List 9: neutral monsters; a converted one is in its caster's list,
            // with the player's pets (FUN_005b1900).
            const bool converted = foe.of && foe.of->align == 2 && foe.alive;
            if (foe.of && !(foe.of->align == 1 && foe.alive) && !converted) continue;
            const int foe_x = subtile(foe.x), foe_y = subtile(foe.y);
            picks.emplace_back(d2d::rules::SearchFoe{ d2d::rules::ai_distance(foe_x - x, foe_y - y), foe.pet || converted, town && !foe.of, !foe.alive && !foe.of, false,
                                                      foe.of && !converted ? 9 : 0 });
            auto& pick = picks.back();
            if (pick.pet && pick.dead) pick.distance = 0x7fffffff;
            pick.blocked = need_sight && (pick.list || pick.distance < best) && d2d::rules::sight_blocked(x, y, type_info.size, foe_x, foe_y, foe.size, wall);
            pick_foes.push_back(&foe);
        }
        const auto pick = d2d::rules::search_pick(picks, best, need_sight);
        found = { pick.target >= 0 ? pick_foes[std::size_t(pick.target)] : nullptr, pick.best, pick.nearest };
    } else if (!found.target) {
        const auto pick = mode5(monster.align);
        if (const int choice = pick.target >= 0 ? pick.target : pick.second; choice >= 0) found = { &foes[std::size_t(choice)], pick.target >= 0 ? pick.best : pick.second_best };
    }
    if (found.target && monster.align != 2) {
        monster.sighted = true;
        if (shared) (*seen)[monster.area] = !flag;
    }
    return found;
}

// Charge's swing (FUN_005cf6b0 in melee, FUN_005cf900 at the charge's end):
// A2 for Duriel and class 0x49, else A1, with the skill (its hit:
// skill_hit).
void charge_swing(const GameData& game_data, Monster& monster, const Foe& foe, int skill, std::uint32_t now_ms, d2d::rules::Rng& rng) {
    const bool second = monster.type == int(game_data.mon_bin[std::size_t(d2d::rules::monster_ids::kDuriel)]) || monster.type == int(game_data.mon_bin[0x49]);
    const std::string_view mode = second ? "A2" : "A1";
    monster.unit.dir = direction16(foe.x - monster.unit.x, foe.y - monster.unit.y);
    set_mode(game_data, monster, mode, now_ms);
    monster.skill = skill; monster.struck = false; monster.wandering = false; monster.seq_done = 0; monster.seq_once = false;
    monster.skill_frame = 0; monster.skill_x = subtile(foe.x); monster.skill_y = subtile(foe.y);
    attack_starts(game_data, monster, mode, rng);
}

// A traced MonAI's frame outside an attack (the driver FUN_005b1740:
// target search FUN_005de890 / FUN_005dd7f0, then the AI's think; Andariel
// MonAI 34 FUN_005f5830 -> rules::andariel_think, the rest
// rules::mon_think). A move goes on between thinks — at a foe (a walk or
// run at it) or to a spot (a wander, back-off, keep-off, circle). At a
// think it takes search_target's foe; with none it stands (10 frames,
// the nearest - 10 from 25 subtiles off, 25 from 35), or wanders 5 when
// hit. In melee is unit_distance within
// MeleeRng + 1. A walk with flags 2 that can't set off: 70 % a random
// walk of 4 subtiles (FUN_005de200), else stand 10. Returns false for an
// AI whose think isn't traced. A special AI (the Countess's) thinks in
// place of its MonAI's (FUN_005b15d0). Thinks draw the unit's own seed.
// ponytail: the seed as its look left it (FUN_00573cb0's draws and the
// rest between aren't taken; world.cpp's spawns keep the default);
// first-sight speech (FUN_005b1140) left out; FUN_0064d910's 0x40 test on
// the one subtile under it, not its collision pattern; in melee
// skips the path test (FUN_00622aa0, mask 0x804); paths
// (FUN_005de190) as path_to's, a circle as a walk to the point n
// subtiles to the side of the target; a back-off sets off when its end
// and first step are open; no teleporting mod.
bool think(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, d2d::rules::Rng& rng, std::uint32_t now_ms,
           float walk, float run, const Crowd& crowd, std::span<Monster> pack, AreaSeen* seen, const OpenDoor* open_door, std::vector<Monster>* born,
           Spawning::LevelState* rooms_in_play) {
    auto& unit = monster.unit;
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    const auto& per_difficulty = type_info.diff[std::size_t(monster.difficulty)];
    const bool andariel = type_info.ai_name == "Andariel", nest = type_info.ai_name == "FoulCrowNest" || type_info.ai_name == "GargoyleTrap" || type_info.ai_name == "Sarcophagus";
    const bool cursed = monster.special == 10 || monster.special == 11;   // the curse thinks run whatever its own
    if (!andariel && !cursed && !d2d::rules::traced_ai(type_info.ai_name)) return false;
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
    auto set_off = [&](bool running, const Foe* foe) {
        monster.budget = 0x14;
        monster.wandering = !foe;
        if (!path_to(level, monster, foe ? subtile(foe->x) : subtile(unit.goal_x), foe ? subtile(foe->y) : subtile(unit.goal_y), foe, crowd)) {
            if (!foe) idle(per_difficulty.aidel ? per_difficulty.aidel : 15);
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
        // A charge's end (srvdofunc 67, FUN_005cf900): at its target it
        // swings with the skill (charge_swing).
        if (const int with = std::exchange(monster.charging, -1); with >= 0 && chased && std::hypot(chased->x - unit.x, chased->y - unit.y) <= kMeleeReach + 0.3f) {
            charge_swing(game_data, monster, *chased, with, now_ms, rng);
            return true;
        }
    }
    if (now_ms < monster.next_act) return true;
    // The driver's first step (FUN_005b10e0 → FUN_005b0f50): with opendoors
    // (MonStats flags +0xc & 8) and the monster bit 0x800 under it
    // (FUN_00648eb0), its door (OpenDoor) operated, it stands 5.
    // ponytail: the word read now, not a moving path's cached +0x54.
    if (type_info.open_doors && on_door(level, unit) && open_door && *open_door && (*open_door)(monster, now_ms)) { idle(5); return true; }
    const int x = subtile(unit.x), y = subtile(unit.y);
    auto [target, best, nearest] = search_target(game_data, level, monster, foes, seen);
    // A minion under Cry Help (AI command 1, FUN_0058f730): the unit it's sent
    // at, while that lives, is its target (the Minion think, command kind 1).
    const bool cried_at = type_info.ai_name == "Minion" && monster.ai_command == 1 && monster.command_foe >= 0;
    if (cried_at)
        if (const auto sent = std::ranges::find(foes, monster.command_foe, &Foe::id); sent != foes.end() && sent->alive) {
            target = &*sent;
            best = d2d::rules::ai_distance(int(std::floor(sent->x * 5)) - int(std::floor(unit.x * 5)), int(std::floor(sent->y * 5)) - int(std::floor(unit.y * 5)));
        }
    monster.aware = target != nullptr;
    // Search type 0 with a think (Coldworm's, MonAI 66): no target looked for,
    // the think runs anyway; it reads none, so any live foe stands in.
    if (!target && type_info.ai_name == "SandMaggotQueen")
        for (auto& foe : foes) if (foe.alive) { target = &foe; break; }
    // Diablo's (FUN_005e8f20, once his search found someone): the unit
    // lists' best by his score (FUN_005e8530), above 0, the first on a tie.
    // Lists 0..7: a player of his act under 0x3fc (FUN_005e8eb0: FUN_005dc530)
    // and the pets after it (a dead player scores 0); list 8, the good
    // monsters of his act. List 9's best (neutral: Confuse, Attract) is the
    // fallback: within 5 of him, with the best out of reach (FUN_00622c40)
    // and no path to it (type 2, FUN_00649970), it's taken. None: the
    // search's stays.
    // Each foe's pick: Fight::world's (a pet's or monster's, unit_pick).
    // Baal's and his clone's (FUN_005fbdd0, score FUN_005fbb00) are the same
    // with the point bonus at 0x4b.
    const bool baal = type_info.ai_name == "BaalCrab" || type_info.ai_name == "BaalCrabClone";
    const int point_far = baal ? 0x4b : 0x55;
    if (target && (type_info.ai_name == "Diablo" || baal)) {
        const auto wall = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x04); };
        const int point_x = monster.has_point ? monster.point_x : x, point_y = monster.has_point ? monster.point_y : y;
        const auto value = [&](const Foe& foe) {
            const int foe_x = subtile(foe.x), foe_y = subtile(foe.y);
            if (!foe.alive && !foe.pet && !foe.of) return 0;
            const bool reach = d2d::rules::unit_distance(foe_x - x, foe_y - y, type_info.size, foe.size) <= type_info.melee_rng + 1;
            const bool line = !d2d::rules::sight_blocked(x, y, type_info.size, foe_x, foe_y, foe.size, wall);
            const bool point_bonus = d2d::rules::point_apart(x - point_x, y - point_y, (type_info.size >> 1) + 1) > point_far
                                  && d2d::rules::point_apart(foe_x - point_x, foe_y - point_y, (foe.size >> 1) + 1) < point_far;
            return d2d::rules::diablo_score(foe.pick, foe.life_pct, reach, line, point_bonus);
        };
        Foe* top = nullptr; int top_score = 0;
        Foe* spare = nullptr; int spare_score = 0;
        const bool player_in = std::ranges::any_of(foes, [&](const Foe& foe) {
            return !foe.pet && !foe.of && d2d::rules::ai_distance(subtile(foe.x) - x, subtile(foe.y) - y) < 0x3fc; });
        for (auto& foe : foes) {
            const bool neutral = foe.of && foe.of->align == 1, good = foe.of && foe.of->align == 2;   // list 9; a converted one is a pet
            if (foe.of && !neutral && !good) continue;
            if (!foe.of && !player_in) continue;                    // the player failed FUN_005e8eb0: it and its pets skipped
            if ((foe.of || foe.pet) && !foe.alive) continue;
            const int score = value(foe);
            if (neutral) { if (score > spare_score) { spare = &foe; spare_score = score; } }
            else if (score > top_score) { top = &foe; top_score = score; }
        }
        if (spare && d2d::rules::ai_distance(subtile(spare->x) - x, subtile(spare->y) - y) < 5
            && (!top || d2d::rules::unit_distance(subtile(top->x) - x, subtile(top->y) - y, type_info.size, top->size) > type_info.melee_rng + 1)) {
            const bool path = top && !d2d::rules::monster_path(x, y, subtile(top->x), subtile(top->y), true, [&](int at_x, int at_y) {
                const float centre_x = (float(at_x) + 0.5f) / 5, centre_y = (float(at_y) + 0.5f) / 5;
                return level.unit_blocked(centre_x, centre_y, unit.shape) || crowd.at(centre_x, centre_y, &unit); }).empty();
            if (!path) top = spare;
        }
        if (top) target = top;
    }
    auto walk_to = [&](int off_x, int off_y, bool running) {       // to a spot, subtiles off
        unit.goal_x = (float(x + off_x) + 0.5f) / 5; unit.goal_y = (float(y + off_y) + 0.5f) / 5;
        set_off(running, nullptr);
    };
    // Wussie (FUN_005ee3c0; a5q2's prisoners): no target. Its cage's portal
    // open: unless one of its cage has gone through, 4 or more off it it
    // walks at it 70 % (FUN_005dec80 flags 7), else rand(100) < 10 wanders 4,
    // else stands 10; there (or after the first) it's gone through. Else a
    // player within 16: its door broken, the portal opens (FUN_005888d0),
    // still caged it cries (FUN_00588dd0; rand(1000) < 100 the overhead
    // 0x14ff). Then it stands 25.
    // ponytail: the walk at the portal as one to its spot; the overhead
    // line, and FUN_005b1990 at +0xd0 == 11, aren't sent.
    if (type_info.ai_name == "Wussie") {
        auto& view = monster.wussie;
        if (view.portal) {
            if (!view.cage_left && d2d::rules::ai_distance(view.portal_x - x, view.portal_y - y) >= 4) {   // FUN_005dc5c0
                if (monster.seed(100) < 70) { walk_to(view.portal_x - x, view.portal_y - y, false); return true; }
                if (monster.seed(100) < 10) { const auto wander = d2d::rules::think_wander(monster.seed, 4); walk_to(wander.x, wander.y, false); return true; }
                idle(10);
                return true;
            }
            view.left = true;
            monster.gone = true;
            return true;
        }
        if (view.player) {
            if (view.door_broken) view.open_portal = true;
            else { view.cried = true; (void)monster.seed(1000); }
        }
        idle(25);
        return true;
    }
    // BaalThrone (134, FUN_005ef320; AI control +0x14 the waves sent, +0x18
    // flags, +0x1c the frame of the next think): till then it stands. With
    // no other living monster within 64 subtiles (FUN_005dd0b0 kind 3):
    // flag 1 set, the next of five waves (FUN_005ef210: skill 286 in S3 at
    // 13 subtiles south, its Subject the wave's superunique 61..65; 100
    // frames on, flag 2, flag 1 off), or after the fifth he turns into
    // baalcrabstairs (FUN_00574370(0x22f), think BaalToStairs) and stands 5;
    // flag 1 unset, skill 285 (Baal Corpse Explode) in S3, flag 1, 250
    // frames on. Else stands 10. Its srvdo 141 (FUN_005d2e80): each corpse
    // within Param5 (40) + (lvl - 1) x Param6 (0) subtiles (FUN_0056dcc0,
    // flags 0x3002) goes up as Corpse Explosion (FUN_005c50b0 ->
    // FUN_005c4df0), whose damage is calc1..calc2 % of its life: the row
    // has neither, so it hurts no one (bugs.md); the corpses are spent.
    // ponytail: spent as the cast starts, not at its action frame, by
    // ai_distance, not FUN_0056dcc0's own area; the
    // skill1 at a foe while a wave fights isn't made; a wave is its
    // superunique's group.
    if (type_info.ai_name == "BaalThrone") {
        const int frame = int(now_ms / 40);
        if (frame < monster.ai_state3) { idle(monster.ai_state3 - frame); return true; }
        const bool others = std::ranges::any_of(pack, [&](const Monster& other) {
            return &other != &monster && other.alive() && other.align == 0 && d2d::rules::ai_distance(subtile(other.unit.x) - x, subtile(other.unit.y) - y) < 0x40; });
        if (others) { idle(10); return true; }
        if (monster.ai_state2 & 1) {
            if (monster.ai_state >= 5) { monster.baal_leaves = true; idle(5); return true; }
            monster.baal_wave = 61 + monster.ai_state;
            set_mode(game_data, monster, "S3", now_ms);
            monster.next_act = monster.mode_until;                 // the cast plays out before the next think
            ++monster.ai_state;
            monster.ai_state3 = frame + 100;
            monster.ai_state2 = (monster.ai_state2 | 2) & ~1;
            return true;
        }
        set_mode(game_data, monster, "S3", now_ms);
        monster.next_act = monster.mode_until;
        monster.ai_state2 |= 1;
        monster.ai_state3 = frame + 250;
        for (auto& corpse : pack)
            if (!corpse.alive() && !corpse.corpse_used && d2d::rules::ai_distance(subtile(corpse.unit.x) - x, subtile(corpse.unit.y) - y) <= 40) corpse.corpse_used = true;
        return true;
    }
    // BaalToStairs (138, FUN_005ef620): the Chamber's portal (object 563)
    // within 25 (FUN_005ef5e0), a straight walk to it (FUN_005deca0); within
    // aip1 (FUN_006416d0) he's gone in (FUN_0058e600, state 0x92). None:
    // stands 25.
    if (type_info.ai_name == "BaalToStairs") {
        if (!monster.chamber_portal) { idle(25); return true; }
        const auto [portal_x, portal_y] = *monster.chamber_portal;
        const int dist = d2d::rules::ai_distance(portal_x - x, portal_y - y);
        if (dist >= 25) { idle(25); return true; }
        if (dist < per_difficulty.aip[0]) { monster.baal_in = true; monster.gone = true; return true; }
        walk_to(portal_x - x, portal_y - y, false);
        return true;
    }
    if (!target) {
        monster.chase = -1;
        // FUN_005de890 with none found: with a WL mode (MonStats2 mode bits,
        // FUN_0046c140(class, 2)), one just hit (its last mode GH,
        // FUN_005dd2b0) or standing on collision bit 0x40 (FUN_0064d910)
        // wanders 5 (FUN_005de200); else it stands.
        const bool has_walk = game_data.npc_timing(monster.npc, "WL").directions > 0;
        if (has_walk && (monster.left_mode == "GH" || level.blocked(unit.x, unit.y, 0x40))) {
            const auto wander = d2d::rules::think_wander(monster.seed, 5);
            walk_to(wander.x, wander.y, false);
            return true;
        }
        idle(nearest < 25 ? 10 : nearest < 35 ? nearest - 10 : 25);
        return true;
    }
    // MonAI 91, DarkWanderer (FUN_005ea130; a3q7's record 0x1c): his spot
    // (FUN_005bd0d0, from the marker's x + 7 he's made on) is 20 subtiles
    // north, else 11 north, 2 east or west of that, else 8 north, else 3
    // north, the first clear of 0x3c01 (the last taken as it is). AI control
    // +0x14 0 -> 1; a foe 20 or more off: stand 40. Under 20, state 1: walk
    // there, state 2. State 2 at its end: within 2 of it, or after 3 more
    // tries (+0x18), he's gone (unit flag 0x20000, FUN_005ddfc0) and a3q7
    // marks every player in act 3 (FUN_005bd4a0 -> FUN_005bd390, word 0x20
    // bit 0); else walk again.
    // ponytail: a spot's test is one subtile, not his shape (FUN_006229f0);
    // the next move's path type (FUN_005de190) isn't set. The afterglow
    // he leaves: World::tick.
    if (type_info.ai_name == "DarkWanderer") {
        const int home_x = subtile(monster.home_x), home_y = subtile(monster.home_y);
        auto clear = [&](int at_x, int at_y) { return !level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x3c01); };
        int spot_x = home_x, spot_y = home_y - 20;
        if (!clear(spot_x, spot_y)) {
            spot_y = home_y - 11;
            if (!clear(spot_x, spot_y) && !clear(spot_x = home_x + 2, spot_y) && !clear(spot_x = home_x - 2, spot_y)) {
                spot_x = home_x; spot_y = home_y - 8;
                if (!clear(spot_x, spot_y)) spot_y = home_y - 3;
            }
        }
        if (monster.ai_state == 0) { monster.ai_state = 1; monster.ai_state2 = 0; }
        if (best >= 0x14) { idle(0x28); return true; }
        auto head_off = [&] { unit.goal_x = (float(spot_x) + 0.5f) / 5; unit.goal_y = (float(spot_y) + 0.5f) / 5; set_off(false, nullptr); };
        if (monster.ai_state == 1) { head_off(); monster.ai_state = 2; return true; }
        if (d2d::rules::ai_distance(spot_x - x, spot_y - y) < 2 || monster.ai_state2 >= 3) { monster.gone = true; return true; }
        ++monster.ai_state2;
        head_off();
        return true;
    }
    const int target_x = subtile(target->x), target_y = subtile(target->y);
    const float dx = target->x - unit.x, dy = target->y - unit.y;
    d2d::rules::ThinkIn input{ .aip = per_difficulty.aip, .dist = best, .difficulty = monster.difficulty, .level = level.id, .state = &monster.ai_state };
    input.in_melee = d2d::rules::unit_distance(target_x - x, target_y - y, type_info.size, target->size) <= type_info.melee_rng + 1;
    input.got_hit = monster.left_mode == "GH";
    input.life_pct = int(std::int64_t(monster.hit_points) * 100 / std::max(monster.stats.hit_points, 1));   // FUN_00621f20
    for (std::size_t skill = 0; skill < 3; ++skill) input.skill[skill] = !type_info.skill[skill].empty();
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
    input.command = &monster.ai_command; input.rally = &rally;
    input.leader = !pack.empty() && monster.leader == int(self);
    using d2d::rules::Boss;
    auto unique = [](const Monster& other) { return other.boss == Boss::unique || other.boss == Boss::superunique; };
    for (std::size_t i = 0; i < pack.size(); ++i) {
        const auto& other = pack[i];
        const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y;
        if (i != self && other.mode == "DT" && d2d::rules::ai_distance(off_x, off_y) < 15) input.dying = true;
        if (type_info.ai_name != "FallenShaman" || other.alive() || other.mode != "DD" || other.corpse_used) continue;
        const int base = game_data.monsters.types[std::size_t(other.type)].base;
        if (unique(monster) ? (base == 19 || base == 58) && !unique(other) && d2d::rules::ai_distance(off_x, off_y) < 80
                            : other.leader == int(self) && off_x * off_x + off_y * off_y <= input.aip[3] * input.aip[3])
            corpse = int(i);
    }
    if (type_info.ai_name == "Fallen") {
        if (!input.dying) monster.fleeing = false;
        else if (!monster.fleeing) { monster.fleeing = true; monster.flee_sound = true; }
    }
    input.corpse = corpse >= 0;
    // A nest's: the frame (its init's taken at its first think), the spot
    // spawnx / spawny off (FUN_005fd350 tests crownest's only).
    const int frame = int(now_ms / 40);
    if (nest && monster.ai_state == 0) monster.ai_state = frame;
    input.frame = frame; input.state2 = &monster.ai_state2;
    input.spot_free = nest_spot_free(game_data, level, crowd, monster.type, x, y, false);
    input.home_dist = d2d::rules::ai_distance(subtile(monster.home_x) - x, subtile(monster.home_y) - y);
    input.off_x = target_x - x; input.off_y = target_y - y;
    int pace = 0;
    input.pace = &pace; input.state3 = &monster.ai_state3;
    input.target_life_pct = target->life_pct;
    input.laying = now_ms < monster.laid_until;
    input.velocity = type_info.velocity; input.run = type_info.run;
    input.aidel = per_difficulty.aidel ? per_difficulty.aidel : 15;
    // Act 2's: Skill4 / 5; the target's fire / cold resists (stats 0x27 /
    // 0x2b); the second target (FUN_005ddc30) as the target; Duriel's aura
    // on; the searches (FUN_005dd0b0) over the monsters within 80 subtiles:
    // the nearest living kin of its class (FUN_005b0d00, the Panthers'),
    // any other living one (FUN_005f06b0, the Sand Raider's), by squared
    // distance (FUN_005b0bd0); the Greater Mummy's (FUN_005f2a00) within
    // aip5: the count, a corpse (DD, not raised) and a hurt one, the last
    // found; an lUndead (MonStats +0xd & 8) not a unique (FUN_005a0180, 8).
    // Radament's (FUN_005f2b10, class 0xe5): within aip5 + 10, uniques
    // too, on Normal any undead (FUN_0063e990).
    // ponytail: regen (stat 0x4a) is 0, d2d's monsters having none; a
    // skill is always usable (FUN_005fd470); the near rooms as 80 subtiles.
    input.skill45 = { !type_info.skill[3].empty(), !type_info.skill[4].empty() };
    input.target_fire_res = target->fighter.res[0]; input.target_cold_res = target->fighter.res[2];
    input.second = true; input.second_dist = best;
    input.aura_on = monster.aura != 0;
    std::vector<d2d::rules::ThinkEffect> effects;
    input.effects = &effects;
    int kin = -1, aid = -1, imp_found = -1;
    std::vector<int> wisp_finds;
    {
        const bool panther = type_info.ai_name == "PantherWoman" || type_info.ai_name == "PantherJavelin";
        const bool raider = type_info.ai_name == "SandRaider", mummy = type_info.ai_name == "GreaterMummy";
        int kin_dist = std::numeric_limits<int>::max();
        for (std::size_t i = 0; (panther || raider || mummy) && i < pack.size(); ++i) {
            const auto& other = pack[i];
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y, squared = off_x * off_x + off_y * off_y;
            if (&other == &monster || d2d::rules::ai_distance(off_x, off_y) >= 80) continue;
            if (mummy) {
                const bool radament = monster.type == d2d::rules::monster_ids::kRadament;
                const auto& other_type = game_data.monsters.types[std::size_t(other.type)];
                if (!(radament && monster.difficulty == 0 ? other_type.undead : other_type.low_undead)) continue;
                if (!radament && (other.boss == d2d::rules::Boss::unique || other.boss == d2d::rules::Boss::superunique)) continue;
                const int reach = input.aip[4] + (radament ? 10 : 0);
                if (squared > reach * reach) continue;
                ++input.in_reach;
                if (!other.alive() && other.mode == "DD" && !other.corpse_used) corpse = int(i);
                else if (other.alive() && other.hit_points < other.stats.hit_points) aid = int(i);
            } else if (other.alive() && (raider || other.type == monster.type) && squared < kin_dist) { kin = int(i); kin_dist = squared; }
        }
        if (mummy) { input.corpse = corpse >= 0; input.aid = aid >= 0; }
        else { input.kin = kin >= 0; input.kin_dist = kin_dist; }
    }
    // The Vulture's: the spots, the rooms, a weak unit within 11 (squared
    // 0x79) at most aip3 % life (FUN_005f30f0: a player or a monster, not
    // itself, alive, life <= max / 100 x aip3), its group, the free spot
    // nearest its circling point (FUN_005f3170: FUN_0064e7b0 at its size,
    // mask 0xffff, from the target's room then its own; none keeps the
    // point). d2d's grid is the level's, so the two searches are one
    // (deviations.md approximation 14).
    if (type_info.ai_name == "Vulture") {
        input.self_x = x; input.self_y = y; input.target_x = target_x; input.target_y = target_y;
        input.same_room = room_at(level, unit.x, unit.y) == room_at(level, target->x, target->y);
        input.can_land = monster.aloft;                            // FUN_005f2fc0 fails on a landed one (pattern 1)
        // In a group: an owner (FUN_0058f0d0: AI control +0x28 / +0x2c, set by
        // FUN_0058f030 on a boss's minions, FUN_005a0c00; never on the boss),
        // not a plain pack's. A MonStats SetBoss party (FUN_005b2830, +0xc &
        // 0x10) would group too, but no Vulture row has SetBoss or minion1.
        input.grouped = monster.boss == d2d::rules::Boss::minion;
        for (const auto& foe : foes) {
            const int off_x = subtile(foe.x) - x, off_y = subtile(foe.y) - y;
            if (foe.alive && off_x * off_x + off_y * off_y <= 0x79 && foe.life_pct <= input.aip[2]) input.victim = true;
        }
        for (const auto& other : pack) {
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y;
            if (&other != &monster && other.alive() && off_x * off_x + off_y * off_y <= 0x79 && other.hit_points <= other.stats.hit_points / 100 * input.aip[2])
                input.victim = true;
        }
        if (monster.ai_state2 != 0 && monster.ai_state3 != 0) {
            input.free_spot[0] = free_spot_near(level, crowd, &unit, monster.ai_state2, monster.ai_state3, unit.shape.size, 0xffff);
            if (!input.free_spot[0] && room_at(level, (float(monster.ai_state2) + 0.5f) / 5, (float(monster.ai_state3) + 0.5f) / 5) >= 0)
                input.free_spot[0] = std::pair{ monster.ai_state2, monster.ai_state3 };   // keep 1: the point, in a room
        }
    }
    // Act 3's (rules::mon_think, monster-ai.md "Act 3's thinks"): the
    // tentacles' head (their leader) and its mode; Skill1's level; its own
    // spot's collision (the frogs' 0xc01, the zealots' 0x40); the zealots'
    // spare: the player's quest 0x15 bit 0 (a3q5 done) in act 3; the line to
    // the target (mask 4); MissS1's range; Skill6..8; Mephisto's weakest
    // foe within 32 (FUN_005f7700); the spots (the priests' and the
    // Council's cast points).
    // ponytail: no pending think event is seen (FUN_005415a0: 0); the
    // shaman's Inferno state (0xc) is never on; a fetish sent to a corpse
    // (command 14) drops it (the corpse isn't kept); a think always has a
    // target (search types 2 / 5 think without one in game.exe).
    {
        static constexpr std::array<std::string_view, 16> kModes{ "DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN" };
        const bool has_leader = monster.leader >= 0 && std::size_t(monster.leader) < pack.size() && &pack[std::size_t(monster.leader)] != &monster;
        if (type_info.ai_name == "Tentacle") {
            const auto& head = has_leader ? pack[std::size_t(monster.leader)] : monster;
            input.grouped = has_leader && (head.alive() || head.mode == "DD");
            input.leader_mode = int(std::ranges::find(kModes, head.mode) - kModes.begin());
        }
        input.command_unit = *input.command == 1;
        input.state_c = monster.breath_state;   // the breath's state 0xc (FUN_00639df0)
        input.skill_level = d2d::rules::monster_skill_level(type_info.sk_lvl[0], monster.difficulty);
        if (type_info.ai_name == "FrogDemon") input.spot_clear = !level.blocked(unit.x, unit.y, 0xc01);
        if (type_info.ai_name == "ZakarumZealot") {
            input.spot_clear = !level.blocked(unit.x, unit.y, 0x40);
            input.spared = target->a3q5_done && d2d::rules::level_act(level.id) == 2;
        }
        const std::uint16_t line_mask = type_info.ai_name == "UberIzual" ? 0x06 : 0x04;   // UberIzual's teleport test: FUN_00622aa0 mask 6
        const auto wall = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, line_mask); };
        input.line_clear = !d2d::rules::sight_blocked(x, y, type_info.size, target_x, target_y, target->size, wall);
        if (const auto missile = game_data.missiles.find(type_info.miss_s1); !type_info.miss_s1.empty() && missile != game_data.missiles.end()) input.s1_range = missile->second.range;
        for (std::size_t slot = 0; slot < 3; ++slot) input.skill678[slot] = !type_info.skill[slot + 5].empty();
        for (const auto& foe : foes)
            if (const int off_x = subtile(foe.x) - x, off_y = subtile(foe.y) - y; foe.alive && off_x * off_x + off_y * off_y <= 0x400) input.weak = true;
        input.self_x = x; input.self_y = y; input.target_x = target_x; input.target_y = target_y;
        // The heals' searches: the priests' (FUN_005f7240) a zakarum (BaseId
        // zealot1 / cantor1) within aip6 (squared) under 61 % life, the last
        // found (its best is never kept, bugs.md #22); the Council's
        // (FUN_005e0430) the weakest under 76 % within 50 (squared 2500).
        // ponytail: the pack in its order, not the near rooms' units.
        const bool priest = type_info.ai_name == "ZakarumPriest", council = type_info.ai_name == "HighPriest";
        int weakest = 76;
        for (std::size_t i = 0; (priest || council) && i < pack.size(); ++i) {
            const auto& other = pack[i];
            if (&other == &monster || !other.alive()) continue;
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y, squared = off_x * off_x + off_y * off_y;
            const int life = int(std::int64_t(other.hit_points) * 100 / std::max(other.stats.hit_points, 1));
            const int base = game_data.monsters.types[std::size_t(other.type)].base;
            if (priest && (base == 235 || base == 238) && squared <= input.aip[5] * input.aip[5] && life < 61) { aid = int(i); input.kin_dist = squared; }
            if (council && squared <= 2500 && life < weakest) { aid = int(i); weakest = life; input.kin_dist = squared; }
        }
        if (priest || council) input.aid = aid >= 0;
        // The wisps (FUN_0065a950 / FUN_0065ac70, radius 0x20): its room alone
        // when the square 32 round it lies inside it (FUN_0065a6b0), else its
        // near list; FUN_0065a710 lets every room of it through (bugs.md #23);
        // each live (FUN_0063ea40: not DT / DD) unit of BaseId 0x76 in them
        // (FUN_005f3960, FUN_00463860), itself too. The blessing's target a player.
        // ponytail: a room's units in the pack's order (game.exe: its unit
        // list); FUN_005541b0 taken as clear.
        if (type_info.ai_name == "WillOWisp") {
            auto rects = missile_rooms(game_data, level, unit.x, unit.y);
            if (!rects.empty()) {
                const auto own = rects.front();
                const bool inside = own[0] < x - 0x20 && own[1] < y - 0x20 && x + 0x20 < own[0] + own[2] && y + 0x20 < own[1] + own[3];
                rects.erase(rects.begin());
                if (inside) rects = { own };
                for (const auto& rect : rects)
                    for (std::size_t i = 0; i < pack.size(); ++i) {
                        const auto& other = pack[i];
                        const int other_x = subtile(other.unit.x), other_y = subtile(other.unit.y);
                        if (other_x < rect[0] || other_y < rect[1] || other_x >= rect[0] + rect[2] || other_y >= rect[1] + rect[3]) continue;
                        if (other.alive() && other.mode != "DT" && other.mode != "DD" && game_data.monsters.types[std::size_t(other.type)].base == 118
                            && !std::ranges::contains(wisp_finds, int(i)))
                            wisp_finds.push_back(int(i));
                    }
            }
            input.wisps = int(wisp_finds.size());
            input.bless_ok = !target->pet && !target->of;
        }
    }
    // Act 4's (monster-ai.md "Act 4's MonAIs"): the vile mother's young within
    // 25 (its MonStats spawn's class) and a spot to lay one; the
    // Regurgitator's corpse (a dead monster, DD, not raised) within aip6, its
    // id the one it's after (state2); the Oblivion Knight's knight (class
    // 0x136) within 50; Diablo's kept point and his chooser's inputs.
    // The states its skills name on a unit (the knights' armor and curse,
    // the finger mage's 0x54): armor since its cast, the others the foe's.
    int lay_x = 0, lay_y = 0;
    {
        const auto& name = type_info.ai_name;
        auto nearest_kin = [&](auto&& wanted, int limit_squared) {
            int found = -1, found_squared = std::numeric_limits<int>::max();
            for (std::size_t i = 0; i < pack.size(); ++i) {
                const auto& other = pack[i];
                if (&other == &monster || !wanted(other)) continue;
                const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y, squared = off_x * off_x + off_y * off_y;
                if (squared <= limit_squared && squared < found_squared) { found = int(i); found_squared = squared; }
            }
            return std::pair{ found, found_squared };
        };
        auto take = [&](int found, int found_squared) {
            if (found < 0) return;
            const auto& other = pack[std::size_t(found)];
            input.kin_x = subtile(other.unit.x); input.kin_y = subtile(other.unit.y); input.kin_id = other.id; input.kin_dist = found_squared;
        };
        if (name == "VileMother") {
            const auto young = game_data.monsters.by_id.find(type_info.spawn);
            for (const auto& other : pack)
                if (young != game_data.monsters.by_id.end() && other.alive() && other.type == young->second
                    && d2d::rules::ai_distance(subtile(other.unit.x) - x, subtile(other.unit.y) - y) <= 25) ++input.young;
            // FUN_005f9e30: eight tries from the target's eighth (DAT_00745600 of
            // FUN_00621dc0) on, each 3 steps of DAT_006ea998 / DAT_006ea978 by
            // {6, 4, 2, 2, 2, 0, 6, 6}[eighth], the first its young can come out
            // on (FUN_005fd350) takes Skill1 (Nest) there.
            static constexpr std::array<int, 8> kLayDir{ 6, 4, 2, 2, 2, 0, 6, 6 };
            static constexpr std::array<int, 8> kDirX{ 0, -1, -1, -1, 0, 1, 1, 1 }, kDirY{ -1, -1, 0, 1, 1, 1, 0, -1 };
            const int eighth = ((d2d::rules::direction64(x, y, target_x, target_y) + 4) >> 3) & 7;
            for (int turn = 0; turn < 8 && !input.lay_ok; ++turn) {
                const int dir = kLayDir[std::size_t((eighth + turn) & 7)];
                lay_x = x + kDirX[std::size_t(dir)] * 3; lay_y = y + kDirY[std::size_t(dir)] * 3;
                input.lay_ok = input.skill[0] && nest_spot_free(game_data, level, crowd, monster.type, lay_x, lay_y, false);
            }
        }
        if (name == "Regurgitator") {
            const auto [found, found_squared] = nearest_kin([](const Monster& other) { return !other.alive() && other.mode == "DD" && !other.corpse_used; }, input.aip[5] * input.aip[5]);
            corpse = found;
            input.corpse = found >= 0;
            take(found, found_squared);
            const auto after = std::ranges::find(pack, monster.ai_state2, &Monster::id);   // FUN_00552f60(state2)
            input.command_unit = after != pack.end() && !after->corpse_used;
            input.leader_mode = after != pack.end() && after->mode == "DD" ? 12 : 0;
            if (after != pack.end()) { input.kin_x = subtile(after->unit.x); input.kin_y = subtile(after->unit.y); }
        }
        if (name == "OblivionKnight") {
            const auto [found, found_squared] = nearest_kin([&](const Monster& other) { return other.alive() && other.type == 0x136; }, 2500);
            kin = found;
            input.kin = found >= 0;
            take(found, found_squared);
        }
        if (name == "AbyssKnight") input.skill_state = monster.armored;
        if (name == "UberIzual") input.skill_state = now_ms < monster.self_state_until;
        if (name == "Ancient") input.skill_state = monster.cry.skill >= 0 && monster.cry.skill == skill_id(game_data, type_info.skill[0]) && now_ms < monster.cry.until;   // Madawc's Shout on him
        // The knight's curse (Skill4) and the finger mage's spider (Skill1):
        // their auratargetstate (skill +0x82) on the target (FUN_006256b0).
        if (const std::size_t slot = name == "OblivionKnight" ? 3 : 0; name == "OblivionKnight" || name == "FingerMage")
            if (const auto* own = game_data.skills.get(skill_id(game_data, type_info.skill[slot])))
                input.skill_state = !own->auratarget.empty() && std::ranges::contains(target->states, own->auratarget);
        if (name == "Diablo" || baal) {
            input.has_word = monster.has_point; input.word_x = monster.point_x; input.word_y = monster.point_y;
            input.target = true;
            input.players_looked = target->pet || target->of ? 0 : 1;
            const auto& pick = target->pick;
            input.left_skill = pick.left_skill; input.left_level = pick.left_level;
            input.right_skill = pick.right_skill; input.right_level = pick.right_level;
            input.target_state_b = pick.state_b;
            const int point_x = monster.has_point ? monster.point_x : x, point_y = monster.has_point ? monster.point_y : y;
            const bool point_bonus = d2d::rules::point_apart(x - point_x, y - point_y, (type_info.size >> 1) + 1) > point_far
                                  && d2d::rules::point_apart(target_x - point_x, target_y - point_y, (target->size >> 1) + 1) < point_far;
            if (input.players_looked > 0) input.score = d2d::rules::diablo_score(pick, target->life_pct, input.in_melee, input.line_clear, point_bonus);
            input.stuck = level.blocked(unit.x, unit.y, 0x40);
            input.reach = input.in_melee;
            input.skill_bonus = d2d::rules::monster_skill_level(0, input.difficulty);   // FUN_00573930 +8
            input.in_chaos = level.id == d2d::rules::level_ids::kChaosSanctuary;
            input.target_light_res = target->fighter.res[1];
            input.target_player = !target->pet;
            input.command_unit = input.target_player;               // FUN_005353f0 -> FUN_00552f60: the player himself
            input.kin_x = target_x; input.kin_y = target_y;
            input.self_size = type_info.size; input.target_size = target->size;
            // FUN_005fd470(199): FUN_005b34c0's dry run (flag 1), every bone of
            // the round fits on its spot (each try three draws on the target's
            // room's seed); usable zeroes his Prison (act 9).
            const int target_room = room_at(level, target->x, target->y);
            input.usable = !d2d::rules::level_ids::is_town(level.id) && std::ranges::all_of(std::views::iota(0, 4), [&](int bone) {
                int at_x = 0, at_y = 0;
                return spawn_spot(game_data, level, crowd, rooms_in_play, 0x154 + bone, target_x + kPrison[std::size_t(bone)].first, target_y + kPrison[std::size_t(bone)].second,
                                  -1, target_room, at_x, at_y); });
        }
    }
    // Act 5's (monster-ai.md "Act 5's MonAIs"): three lines to the target
    // clear of 0x805 (FUN_005dc640), Skill1's Param1 (its reach), the dead
    // test (mode DT / DD), its own life %, the target a player, the command's
    // unit (the Minion's: in reach by melee).
    // ponytail: one line stands for FUN_005dc640's three; the siege beast's
    // rider, state 0x69 and imps to take on, the target's state 0x20 and its
    // FUN_00625d10 / 60 values aren't kept (no rider, state off, values 0);
    // a command's last frame is the frame after.
    {
        const auto wall805 = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x805); };
        input.lines_clear = !d2d::rules::sight_blocked(x, y, type_info.size, target_x, target_y, target->size, wall805);
        if (const auto* own = game_data.skills.get(skill_id(game_data, type_info.skill[0]))) input.skill1_range = own->par[0];
        input.self_dead = monster.mode == "DT" || monster.mode == "DD";
        input.self_life_pct = int(std::int64_t(monster.hit_points) * 100 / std::max(monster.stats.hit_points, 1));
        input.target_life_pct = target->life_pct;
        if (type_info.ai_name == "Succubus" || type_info.ai_name == "SuccubusWitch") input.target_player = !target->pet && !target->of;
        input.command_until = input.frame + 1;
        if (cried_at) {
            input.command_unit = std::ranges::any_of(foes, [&](const Foe& sent) { return sent.id == monster.command_foe && sent.alive; });
            input.command_until = monster.command_until;
        }
        input.reach = input.in_melee;
        // The Overseer's search (FUN_005e26d0) and the spawner's (FUN_005e2b70): live
        // minions (base 0x1c5; the spawner's suicide minions 0x1cd too), the first under
        // half life within 24 (squared 0x240), the first within 20 (0x190); the count.
        // ponytail: the pack stands for the near rooms' units; FUN_005a0180's test
        // (the free one) and state 0x8d aren't kept; the one that last hit it is
        // taken as the target, when it got hit.
        const auto& name = type_info.ai_name;
        if (name == "Overseer" || name == "MinionSpawner" || name == "Nihlathak") {
            const int minion = int(game_data.mon_bin[0x1c5]), suicide = int(game_data.mon_bin[0x1cd]);
            for (std::size_t i = 0; i < pack.size(); ++i) {
                const auto& other = pack[i];
                if (&other == &monster || !other.alive()) continue;
                const int base = game_data.monsters.types[std::size_t(other.type)].base;
                if (base != minion && !(name == "MinionSpawner" && base == suicide)) continue;
                ++input.young;
                const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y, squared = off_x * off_x + off_y * off_y;
                const int life = int(std::int64_t(other.hit_points) * 100 / std::max(other.stats.hit_points, 1));
                if (kin < 0 && squared <= 0x240 && life < 50) kin = int(i);
                if (aid < 0 && squared <= (name == "Nihlathak" ? 0x271 : 0x190)) aid = int(i);
            }
            input.hurt_minion = kin >= 0; input.free_minion = aid >= 0;
            input.attacker = input.got_hit;
        }
        // Nihlathak's Corpse Explosion (FUN_0056e390): a corpse (DD, not used) about.
        // ponytail: the nearest in the pack, not FUN_0056e390's own search by its radius.
        if (name == "Nihlathak") {
            for (std::size_t i = 0; i < pack.size() && corpse < 0; ++i)
                if (!pack[i].alive() && pack[i].mode == "DD" && !pack[i].corpse_used) corpse = int(i);
            input.corpse = corpse >= 0;
            input.self_x = x; input.self_y = y;
        }
        // The Ancients: which by class, a foe picked (the target), awake
        // (a5q5's record, set by World::act5_update).
        // ponytail: Talic's state 0x36 isn't kept.
        if (name == "Ancient") {
            input.asleep = monster.asleep;
            input.ancient = type_info.id == "ancientbarb2" ? 1 : type_info.id == "ancientbarb3" ? 2 : 0;
            input.picked = true;
            input.self_size = type_info.size; input.target_size = target->size;
            input.self_x = x; input.self_y = y; input.target_x = target_x; input.target_y = target_y;
        }
        // ponytail: a player swinging or casting counts as still (a Foe keeps
        // no mode, only its walk or run).
        if (name == "BaalTaunt") {
            input.target_still = target->of ? target->of->mode == "NU" : target->motion() == d2d::rules::Motion::standing;
            input.target_x = target_x; input.target_y = target_y;
        }
        // Baal's tentacles live on their master (FUN_00552fd0).
        // ponytail: the master's taken as there.
        if (name == "BaalTentacle") input.rider = true;
        // The putrid defiler's find (FUN_005dd0b0 with FUN_005efa50): a monster
        // Impregnate takes (FUN_005d2140) within 25 (FUN_005dc380); in reach
        // (FUN_00622c40) it casts at it.
        // ponytail: the nearest, not FUN_005dd0b0 mode 1's list order.
        if (name == "PutridDefiler") {
            int nearest_host = 26;
            for (std::size_t k = 0; k < pack.size(); ++k) {
                const auto& host = pack[k];
                if (&host == &monster || !impregnable(game_data, host)) continue;
                const int apart = d2d::rules::ai_distance(subtile(host.unit.x) - x, subtile(host.unit.y) - y);
                if (apart < nearest_host) { nearest_host = apart; kin = int(k); }
            }
            input.defile = kin >= 0;
            if (kin >= 0) {
                const auto& host = pack[std::size_t(kin)];
                input.reach = d2d::rules::unit_distance(subtile(host.unit.x) - x, subtile(host.unit.y) - y, type_info.size, game_data.monsters.types[std::size_t(host.type)].size) <= type_info.melee_rng + 1;
            }
        }
        // Baal's Vile Effigy lives while he does (FUN_00552fd0: its master, FUN_005541b0).
        if (name == "BaalCrabClone") input.rider = std::ranges::any_of(pack, [&](const Monster& other) { return other.alive() && game_data.monsters.types[std::size_t(other.type)].ai_name == "BaalCrab"; });
        // The Imp reads imp1..imp4's aips (MonStats 0x1ec..0x1ef). Its ride
        // word (+0x14) names the siege beast or tower that took it (FUN_005e17e0):
        // found (FUN_00552f60), dead (FUN_005541b0), ridden (FUN_00552fd0), its spot.
        // Riding, its mount found and dead.
        if (name == "Imp") {
            if (monster.ai_state == 0 && monster.special != 16) monster.ai_state = -1;
            for (std::size_t k = 0; k < 4; ++k)
                input.imp_aip[k] = game_data.monsters.types[std::size_t(game_data.mon_bin[0x1ec + k])].diff[std::size_t(std::clamp(monster.difficulty, 0, 2))].aip;
            input.self_x = x; input.self_y = y;
            int mount = -1;
            if (monster.special == 16) mount = monster.bound >= 0 && std::size_t(monster.bound) < pack.size() ? monster.bound : -1;
            else if (const auto found = std::ranges::find(pack, monster.ai_state, &Monster::id); monster.ai_state != -1 && found != pack.end()) mount = int(found - pack.begin());
            if (mount >= 0) {
                const auto& ride = pack[std::size_t(mount)];
                input.command_unit = true;
                input.command_busy = !ride.alive();
                input.rider = monster.special == 16 || ride.bound >= 0;
                kin = mount;
                input.kin_x = subtile(ride.unit.x); input.kin_y = subtile(ride.unit.y);
            }
            input.riding = monster.riding;
        }
        // The siege beast's and the barricade tower's rider (FUN_00552fd0) and
        // whether it's dead (FUN_005541b0); an imp to take on (FUN_005dd0b0
        // with FUN_005e1720: alive, BaseId imp1, unbound, its ride word -1,
        // within the beast's aip1 / the tower's 20).
        // ponytail: the nearest such imp, its sight (FUN_00650d70) and pet test unread.
        if (name == "SiegeBeast" || name == "SiegeTower") {
            const Monster* rider = monster.bound >= 0 && std::size_t(monster.bound) < pack.size() ? &pack[std::size_t(monster.bound)] : nullptr;
            input.rider = rider != nullptr;
            input.command_busy = rider && !rider->alive();
            const int reach = name == "SiegeTower" ? 20 : input.aip[0];
            int found_squared = reach * reach + 1;
            for (std::size_t i = 0; i < pack.size(); ++i) {
                const auto& other = pack[i];
                const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y;
                if (&other != &monster && other.alive() && game_data.monsters.types[std::size_t(other.type)].base == 0x1ec && other.bound < 0 && other.ai_state == -1
                    && off_x * off_x + off_y * off_y < found_squared) { imp_found = int(i); found_squared = off_x * off_x + off_y * off_y; }
            }
            input.imp_near = imp_found >= 0;
        }
    }
    // The rest the levels place: the spots. The arrow traps' level kind is
    // drawn when one first fires there (in range, lined up, past its frame;
    // FUN_005fb650): Act 1's (levels below 40) the spike, else rand(3) on
    // that trap's seed. The desert turret's lines (FUN_005fd470 skill 0xcb:
    // FUN_00645910 from it, mask 0x805, no size) to each step's point and to
    // the target.
    {
        const auto& name = type_info.ai_name;
        const bool arrow = name == "Trap-RightArrow" || name == "Trap-LeftArrow";
        if (arrow || name == "HellMeteor" || name == "DesertTurret") {
            input.self_x = x; input.self_y = y; input.target_x = target_x; input.target_y = target_y;
        }
        if (arrow) {
            const int off = name == "Trap-RightArrow" ? target_x - x : target_y - y;
            if (level.trap_kind < 0 && input.aip[0] <= input.dist && input.dist <= input.aip[1] && std::abs(off) < 3 && monster.ai_state < frame)
                level.trap_kind = level.id < 40 ? 0 : monster.seed(3);
            input.trap_kind = level.trap_kind;
        }
        if (name == "DesertTurret") {
            const auto wall805 = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x805); };
            for (std::size_t step = 0; step < input.ray_clear.size(); ++step) {
                const auto& [step_x, step_y] = d2d::rules::kTurretStep[step];
                input.ray_clear[step] = !d2d::rules::sight_blocked(x, y, 0, x + step_x * input.aip[4], y + step_y * input.aip[4], 0, wall805);
            }
            input.usable = !d2d::rules::sight_blocked(x, y, 0, target_x, target_y, 0, wall805);
        }
    }
    auto use = [&](std::string_view mode, int skill) {             // FUN_005dead0 / FUN_005ddf90: against the target (mode ctx +8)
        monster.chase = int(target - foes.data());
        unit.dir = direction16(dx, dy);
        set_mode(game_data, monster, mode, now_ms);
        monster.skill = skill; monster.struck = false; monster.wandering = false; monster.seq_done = 0; monster.seq_once = false;
        monster.skill_frame = 0; monster.skill_x = target_x; monster.skill_y = target_y; monster.at_point = false;
        attack_starts(game_data, monster, mode, rng);
    };
    // FUN_005defe0 / FUN_005df140: n subtiles on from the target, by axis.
    auto away = [&](int subtiles, bool running) {
        const int off_x = subtiles * ((x > target_x) - (x < target_x)), off_y = subtiles * ((y > target_y) - (y < target_y));
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
        switch (d2d::rules::andariel_think(input.in_melee, input.aip, monster.seed)) {
            case AndarielAct::spray: use("SC", kAndrialSpray); return true;   // SQ: seq_andarielspray plays SC
            case AndarielAct::bolt: use("A1", kAndyPoisonBolt); return true;
            case AndarielAct::melee: act = { MonAct::a1 }; break;
            case AndarielAct::idle: act = { MonAct::idle, 5 }; break;
            case AndarielAct::walk: act = { MonAct::walk, 7 }; break;
        }
    } else if (cursed) {
        input.swings = !type_info.interact && game_data.npc_timing(monster.npc, "A1").directions > 0;
        input.runs = has_run; input.afraid = monster.afraid;
        act = monster.special == 10 ? d2d::rules::dim_vision_think(input, monster.seed) : d2d::rules::terror_think(input, monster.seed, away);
    } else if (monster.other) {
        int word = 0;                                              // the SandMaggot's command 14 +0x18 (never set)
        input.in_s4 = monster.mode == "S4"; input.cast_word = &word;
        act = d2d::rules::other_think(type_info.ai_name, input);
    } else if (monster.special == 16) {                           // a riding imp (FUN_005e2d80)
        act = d2d::rules::mon_think("ImpRide", input, monster.seed, away);
    } else if (monster.special == 0xd) {                          // the Countess: rooms are the level's (FUN_0061b130)
        auto room = [&](float at_x, float at_y) { return room_at(level, at_x, at_y); };
        const int home = room(monster.home_x, monster.home_y);
        const d2d::rules::CountessIn where{ room(unit.x, unit.y) != home, room(target->x, target->y) != home,
                                            x == subtile(monster.home_x) && y == subtile(monster.home_y) };
        act = d2d::rules::countess_think(input, where, monster.path, monster.seed);
    } else {
        act = d2d::rules::mon_think(type_info.ai_name, input, monster.seed, away);
    }
    monster.move_pct = pace;                                       // the next mode takes it (FUN_005a63f0)
    if (type_info.ai_name == "AbyssKnight" && act.act == MonAct::skill && act.n == 1) monster.armored = true;   // its Bone Armor up
    // Its effects: the leader's command done; Duriel's aura (Skill4) on.
    // ponytail: states 0x5a / 0x5b and the overlays (looks) and stat 0x4a
    // (regen) left out; the Summoner's quest reads his first think (ai_state).
    for (const auto& effect : effects) {
        if (effect.kind == d2d::rules::ThinkEffect::command_done) monster.ai_command = 0;
        if (effect.kind == d2d::rules::ThinkEffect::unset) monster.set_kind = 0;   // FUN_00573120
        if (effect.kind == d2d::rules::ThinkEffect::state && effect.id == 0xc && !effect.value) monster.breath_state = false;   // FUN_00639db0(0xc, 0)
        if (effect.kind == d2d::rules::ThinkEffect::toggle) {
            const int kept = monster.ai_state3;
            ai_toggle(game_data, monster, effect.id);
            if (effect.value) monster.ai_state3 = kept;
        }
        // The Vulture's take-off (FUN_005f2eb0) and landing (FUN_005f2fc0:
        // at the free spot nearest, FUN_00554ea0; pattern 1, mask 0x3c01).
        if (effect.kind == d2d::rules::ThinkEffect::take_off) { monster.aloft = true; unit.shape.pattern = 5; unit.shape.mask = 0; }
        if (effect.kind == d2d::rules::ThinkEffect::land) {
            std::tie(unit.x, unit.y) = level.nearest_free(unit.x, unit.y);
            monster.aloft = false; unit.shape.pattern = 1; unit.shape.mask = 0x3c01;
        }
        // The fetish shaman's command to its group (FUN_0058f730: 1 at the
        // target, 14 to a corpse, dropped: see above).
        if (effect.kind == d2d::rules::ThinkEffect::rally && effect.id == 1)
            for (auto& other : pack)
                if (&other != &monster && other.alive() && other.leader == int(&monster - pack.data())) other.ai_command = 1;
        // The wisps' dance: its finds' AI words (FUN_0058ec00), the blessing.
        if (effect.kind == d2d::rules::ThinkEffect::set_word && std::size_t(effect.id / 4) < wisp_finds.size()) {
            auto& wisp = pack[std::size_t(wisp_finds[std::size_t(effect.id / 4)])];
            (effect.id % 4 == 1 ? wisp.ai_state : effect.id % 4 == 2 ? wisp.ai_state2 : wisp.ai_state3) = effect.value;
        }
        if (effect.kind == d2d::rules::ThinkEffect::bless) target->blessed = effect.value;
        if (effect.kind == d2d::rules::ThinkEffect::mark) { monster.has_point = true; monster.point_x = effect.id; monster.point_y = effect.value; }
        if (effect.kind == d2d::rules::ThinkEffect::greet) monster.greeted = true;
        if (effect.kind == d2d::rules::ThinkEffect::mount && imp_found >= 0) pack[std::size_t(imp_found)].ai_state = monster.id;   // FUN_005e17e0
        // Baal's Vile Effigy (FUN_005fc860): none while one lives; baalclone
        // at his spot +- 12 (two draws of his seed), a third of his life,
        // made in NU; his AI control +0x18 counts it.
        // ponytail: FUN_00553540's other spot, the clone's regen off (stat
        // 0x4a) and its flags 0x4020000 aren't.
        if (effect.kind == d2d::rules::ThinkEffect::clone && born
            && std::ranges::none_of(pack, [&](const Monster& other) { return other.alive() && game_data.monsters.types[std::size_t(other.type)].ai_name == "BaalCrabClone"; }))
            if (const auto made = game_data.monsters.by_id.find("baalclone"); made != game_data.monsters.by_id.end()) {
                const int clone_x = x + int(monster.seed.next() % 24) - 12, clone_y = y + int(monster.seed.next() % 24) - 12;
                auto clone = make_monster(game_data, made->second, (float(clone_x) + 0.5f) / 5, (float(clone_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes));
                std::tie(clone.unit.x, clone.unit.y) = level.nearest_free(clone.unit.x, clone.unit.y);
                clone.stats.hit_points = std::max(monster.stats.hit_points / 3, 1);
                clone.hit_points = std::max(monster.hit_points / 3, 1);
                clone.aware = true;
                clone.seed = d2d::rules::Rng{ rng.next() };
                born->push_back(std::move(clone));
                ++monster.ai_state2;
            }   // a4q1's FUN_005b4390 (World::act4_update)
        if (effect.kind == d2d::rules::ThinkEffect::aura) { monster.aura = skill_id(game_data, type_info.skill[std::size_t(effect.id)]); monster.aura_lvl = std::max(input.aip[0], 1); }
    }
    // The unit it's at: its kin (a walk to it), the hurt one or corpse it
    // casts on; the second target is the target.
    using d2d::rules::ThinkUnit;
    const Monster* at_unit = act.unit == ThinkUnit::kin ? (kin >= 0 ? &pack[std::size_t(kin)] : corpse >= 0 ? &pack[std::size_t(corpse)] : nullptr)
                           : act.unit == ThinkUnit::aid && aid >= 0 ? &pack[std::size_t(aid)] : nullptr;
    // FUN_005de0f0 / FUN_005de130: its next think n frames on (a wait after its mode).
    // ponytail: a wait counts from now, not the mode's end.
    if (act.wait > 0 || act.event > 0) monster.next_act = std::max(monster.next_act, now_ms + std::uint32_t(std::max(act.wait, act.event)) * 40);
    // FUN_0058f730 / FUN_0058ef40: its leader's group, itself too. One made
    // outside a group (leader -1) has none to look up, so nothing is sent.
    if (rally && monster.leader >= 0)
        for (auto& other : pack)
            if (other.alive() && other.leader == monster.leader) other.ai_command = 1;
    for (;;) switch (act.act) {
        case MonAct::idle: idle(act.n); return true;
        case MonAct::a1: use("A1", -1); return true;
        case MonAct::a2: use("A2", -1); return true;
        case MonAct::s2: use("S2", -1); return true;
        case MonAct::s3: use("S3", -1); return true;
        case MonAct::s4: use("S4", -1); return true;
        case MonAct::s1: use("S1", -1); return true;               // the Council's MissS1
        case MonAct::sc: use("SC", -1); return true;               // the wisps' MissC
        case MonAct::mode_at:                                      // FUN_005ddfc0: mode n at a point (the wisps' SC)
            use("SC", -1);
            monster.skill_x = act.x; monster.skill_y = act.y; monster.at_point = true;
            unit.dir = direction16(float(act.x - x), float(act.y - y));
            return true;
        case MonAct::move_to: walk_to(act.x - x, act.y - y, false); return true;   // FUN_005de490: to the point
        case MonAct::warp:                                         // FUN_00554ea0: at once to the free spot nearest the point
            std::tie(unit.x, unit.y) = level.nearest_free((float(act.x) + 0.5f) / 5, (float(act.y) + 0.5f) / 5);
            idle(25);
            return true;
        case MonAct::gone: monster.gone = true; return true;       // FUN_005ddfc0: Mephisto's hydras at their end
        case MonAct::spawn:                                        // Coldworm's (FUN_005f9cf0); 2: the vile mother's lay
            if (act.n == 2) {                                      // FUN_005f9e30: Skill1 (Nest) at the spot, no unit; its sequence lays (do 91)
                const int lay = skill_id(game_data, type_info.skill[0]);
                const SeqRows* seq = skill_seq(game_data, type_info, lay);
                const std::string_view played = seq ? std::string_view(seq->front().mode) : std::string_view(type_info.sk_mode[0]);
                use(played, lay);
                if (seq) monster.mode_until = now_ms + std::uint32_t(seq->size()) * game_data.npc_timing(monster.npc, played).ms_per_frame() * 100 / std::uint32_t(100 + monster.anim_pct);
                monster.skill_x = lay_x; monster.skill_y = lay_y;
                return true;
            }
            if (act.n == 0) { set_mode(game_data, monster, "S1", now_ms); return true; }   // FUN_005de440: mode 8 where she stands
            // FUN_0063efa0 (BaseId 0x11c): her MonStats spawn 8 subtiles east, in
            // S1; FUN_005b2f20 within 2 of it (flags 0x42: no family mods, no
            // party), flag 0x4000000 (no experience); counted if made.
            if (const auto young = game_data.monsters.by_id.find(type_info.spawn); born && young != game_data.monsters.by_id.end()) {
                const int room = room_at(level, (float(x + 8) + 0.5f) / 5, (float(y) + 0.5f) / 5);
                if (int at_x = 0, at_y = 0; spawn_spot(game_data, level, crowd, rooms_in_play, young->second, x + 8, y, 2, room, at_x, at_y)) {
                    auto laid = make_monster(game_data, young->second, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes));
                    laid.aware = true; laid.no_exp = true;
                    laid.seed = d2d::rules::Rng{ rng.next() };
                    set_mode(game_data, laid, "S1", now_ms);
                    born->push_back(std::move(laid));
                    ++monster.ai_state;
                }
            }
            return true;
        case MonAct::walk: case MonAct::approach: case MonAct::run:  // FUN_005deb60 at the target
            if (at_unit) { walk_to(subtile(at_unit->unit.x) - x, subtile(at_unit->unit.y) - y, act.act == MonAct::run); return true; }
            if (set_off(act.act == MonAct::run, target)) {
                monster.chase = int(target - foes.data());
                return true;
            }
            if (act.act == MonAct::walk && act.n == 0) monster.ai_command = 0;   // the Fallen's charge ends (FUN_0058ed10)
            if (act.act == MonAct::walk && (act.n & 1)) monster.force_sight = true;   // FUN_005dd230: flag 0x40
            if (act.act != MonAct::walk || !(act.n & 2)) { idle(0); return true; }
            act = d2d::rules::walk_failed(monster.seed);
            break;
        case MonAct::direct:                                       // FUN_005deca0: straight at it (its kin: the defiler's find)
            if (act.unit == ThinkUnit::kin && at_unit) walk_to(subtile(at_unit->unit.x) - x, subtile(at_unit->unit.y) - y, false);
            else walk_to(target_x - x, target_y - y, false);
            return true;
        case MonAct::wander: walk_to(act.x, act.y, false); return true;
        case MonAct::around: walk_to(target_x - x + act.x, target_y - y + act.y, false); return true;
        case MonAct::home: walk_to(subtile(monster.home_x) - x, subtile(monster.home_y) - y, false); return true;
        case MonAct::point:                                        // FUN_005dead0(Sk1mode, Skill1, no unit, x, y)
            use(type_info.sk_mode[0], skill_id(game_data, type_info.skill[0]));
            monster.skill_x = act.x; monster.skill_y = act.y;
            unit.dir = direction16(float(act.x - x), float(act.y - y));
            return true;
        case MonAct::fly: walk_to(act.x - x, act.y - y, false); return true;   // FUN_005de440: to the point (mode n: the Vulture's S1)
        case MonAct::keep: {                                       // FUN_005de4e0: toward, or from, the target to keep x off
            if (act.unit == ThinkUnit::self) { idle(0); return true; }   // the Vulture's landing: a move onto itself
            const int stride = std::min(std::abs(best - act.x), act.n), sign = best < act.x ? -1 : 1;
            const int across = std::abs(target_x - x), down = std::abs(target_y - y), sum = std::max(across + down, stride);
            int step_x = sum ? across * stride / sum : 0, step_y = sum ? down * stride / sum : 0;
            while (sum && step_x + step_y < stride) { ++step_x; ++step_y; }
            walk_to(((target_x > x) - (target_x < x)) * step_x * sign, ((target_y > y) - (target_y < y)) * step_y * sign, false);
            return true;
        }
        case MonAct::circle: {                                     // FUN_005df7d0
            const float side = act.x ? 1.f : -1.f, length = std::max(std::hypot(dx, dy), 0.01f), off = float(act.n) / 5;
            unit.goal_x = target->x - dy / length * off * side; unit.goal_y = target->y + dx / length * off * side;
            set_off(false, nullptr);
            return true;
        }
        case MonAct::seq:                                          // FUN_005de000: the skill's own sequence (the Greater Mummy's raise)
        case MonAct::skill: {                                      // its Sk mode's sequence (MonSeq) plays out
            // Act 5's raw skill ids: the catapult spotter's strike (A1 at a point);
            // the generic spawner's young (FUN_005de000: 0xa7 Nest's sequence,
            // seq_genericspawner) is its slot's, below.
            if (type_info.ai_name == "CatapultSpotter" && act.act == MonAct::skill) {
                use("A1", act.n);
                monster.skill_x = act.x; monster.skill_y = act.y; monster.at_point = true;
                return true;
            }
            // A skill neither plain nor in a sequence stands: none a placed
            // monster's think casts (every MonStats Skill1..8 checked; only the
            // diablo clone's Diablogeddon, and no level places it).
            // A raw Skills.txt id past the slots (the putrid defiler's 300): its slot.
            std::size_t slot = std::size_t(std::max(act.n, 0));
            if (slot >= type_info.skill.size())
                slot = std::size_t(std::ranges::find_if(type_info.skill, [&](const std::string& own) { return !own.empty() && skill_id(game_data, own) == act.n; }) - type_info.skill.begin());
            if (slot >= type_info.skill.size()) { idle(0); return true; }
            const auto& name = type_info.skill[slot];
            const int id = skill_id(game_data, name);
            // Spider Lay and the vampires' shots play their Sk mode itself;
            // VampireFirewall / VampireMeteor (do 24 / 28) are plain casts.
            const SeqRows* seq = skill_seq(game_data, type_info, id);
            const auto* plain_skill = seq ? nullptr : game_data.skills.get(id);
            const auto* start_skill = game_data.skills.get(id);
            const bool emerge = start_skill && start_skill->srvstfunc == d2d::rules::ServerStartFunction::kEmerge;   // a plain S1
            const bool plain = emerge || name == "SpiderLay" || name == "VampireFireball" || name == "VampireMissile"
                            || (plain_skill && (castable(*plain_skill) || plain_skill->srvdofunc == d2d::rules::ServerDoFunction::kMonFrenzy   // MonFrenzy: an A2 strike
                                                || plain_skill->srvdofunc == d2d::rules::ServerDoFunction::kSmite                  // the Baal minions' (A2)
                                                || plain_skill->srvstfunc == d2d::rules::ServerStartFunction::kElementalStrike)); // MonPowerStrike / MonIceSpear (A1)
            // ponytail: a charge (its sequence walks first: Charge's srvstfunc 31
            // moves it) stands.
            // Charge (srvstfunc 31, FUN_005cf6b0): in melee it swings at once;
            // else it runs at the target (MonStats Run), swinging at the end.
            // ponytail: its charge path (type 0x14) and state 0x12 are the
            // run's; a sequence that walks first and isn't Charge stands.
            // DiabRun (srvst 54 / do 103, FUN_005cd2c0 / FUN_005cd380): his
            // sequence at the target's spot as it is now (monster_update plays it).
            if (const auto* run_skill = game_data.skills.get(id); run_skill && run_skill->srvdofunc == d2d::rules::ServerDoFunction::kDiabRun) {
                monster.chase = int(target - foes.data());
                unit.dir = direction16(dx, dy);
                set_mode(game_data, monster, "RN", now_ms);
                monster.skill = id; monster.skill_x = target_x; monster.skill_y = target_y; monster.wandering = false;
                monster.diab_run = 1; monster.diab_run_ms = now_ms;
                monster.seq_mode = "RN"; monster.seq_frame = 0;
                return true;
            }
            if (seq && seq->front().mode == "WL") {
                const auto* start = game_data.skills.get(id);
                if (start && start->srvstfunc == d2d::rules::ServerStartFunction::kCharge) {
                    if (input.in_melee) { charge_swing(game_data, monster, *target, id, now_ms, rng); return true; }
                    if (set_off(true, target)) { monster.chase = int(target - foes.data()); monster.charging = id; return true; }
                }
                idle(0);
                return true;
            }
            if ((!seq && !plain) || ((name == "Resurrect" || name == "Resurrect2") && corpse < 0)) { idle(0); return true; }
            // MagottUp (srvstfunc 44, FUN_005cb170): it comes up at the free
            // spot nearest (FUN_00554ea0 -> FUN_0064e7b0, its size, mask
            // 0x1c09), its flags 0xe back; none: the start fails.
            // MagottDown (45, FUN_005cb270) buries it.
            if (const auto* start = game_data.skills.get(id); start && start->srvstfunc == d2d::rules::ServerStartFunction::kMaggotUp && monster.buried) {
                const auto spot = free_spot_near(level, crowd, &unit, x, y, unit.shape.size, 0x1c09);
                if (!spot) { idle(0); return true; }
                unit.x = (float(spot->first) + 0.5f) / 5; unit.y = (float(spot->second) + 0.5f) / 5;
                monster.buried = false;
            } else if (start && (start->srvstfunc == d2d::rules::ServerStartFunction::kMaggotDown || start->srvstfunc == d2d::rules::ServerStartFunction::kSubmerge)) {
                monster.buried = true;
            } else if (start && start->srvstfunc == d2d::rules::ServerStartFunction::kEmerge) {
                monster.buried = false;
            }
            // Leap (srvstfunc 40, FUN_005d9d50): a monster lands at twice the
            // target's spot less its own, on the free spot nearest
            // (FUN_0064e7b0, its size, mask 0x3c01); none: the start fails.
            // Leap Attack (srvstfunc 41, FUN_005da540; Korlic's
            // seq_ancientleap): its spot by the target (FUN_00645ca0): two
            // subtiles past it, over FUN_006417f0's distance (ai_distance);
            // where its own test (mask 0x1c09) is blocked, the free spot
            // nearest the target (FUN_0064e7b0); then a clear line to it
            // (FUN_00645910 -> FUN_0064e260, mask 0x804) and not in town
            // (FUN_00645c60). None: in reach a plain attack (FUN_0056e230).
            std::optional<std::pair<int, int>> landing;
            if (const auto* leap = game_data.skills.get(id); leap && leap->srvstfunc == d2d::rules::ServerStartFunction::kLeap) {
                landing = free_spot_near(level, crowd, &unit, 2 * target_x - x, 2 * target_y - y, unit.shape.size, 0x3c01);
                if (!landing) { idle(0); return true; }
            } else if (leap && leap->srvstfunc == d2d::rules::ServerStartFunction::kLeapAttack && seq) {
                if (const int span = d2d::rules::ai_distance(target_x - x, target_y - y); span != 0) {
                    std::pair spot{ (target_x - x) * 2 / span + target_x, (target_y - y) * 2 / span + target_y };
                    const float spot_cx = (float(spot.first) + 0.5f) / 5, spot_cy = (float(spot.second) + 0.5f) / 5;
                    landing = spot;
                    if (level.unit_blocked(spot_cx, spot_cy, unit.shape) || crowd.at(spot_cx, spot_cy, &unit))
                        landing = free_spot_near(level, crowd, &unit, target_x, target_y, unit.shape.size, 0x1c09);
                    const auto wall804 = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x804); };
                    if (landing && (d2d::rules::sight_blocked(x, y, 0, landing->first, landing->second, 0, wall804) || in_town(&level))) landing.reset();
                }
                if (!landing) {
                    if (input.in_melee) { use("A1", -1); return true; }
                    idle(0);
                    return true;
                }
            }
            // Its rows' own modes are drawn row by row (Monster::seq_mode);
            // the first row's mode stands for it in the logic.
            const std::string_view played = seq ? std::string_view(seq->front().mode) : std::string_view(type_info.sk_mode[slot]);
            use(played, id);
            if (seq) monster.mode_until = now_ms + std::uint32_t(seq->size()) * game_data.npc_timing(monster.npc, played).ms_per_frame() * 100 / std::uint32_t(100 + monster.anim_pct);
            // The leap's flight: Leap's at once, Leap Attack's by row 11 (do 78
            // on the takeoff and landing events), the strike on the next
            // (FUN_005da660).
            // ponytail: the flight is a line, not the path FUN_0064ea90 lays.
            if (landing) {
                monster.leaping = true;
                monster.leap_from_x = unit.x; monster.leap_from_y = unit.y;
                monster.leap_to_x = (float(landing->first) + 0.5f) / 5; monster.leap_to_y = (float(landing->second) + 0.5f) / 5;
            }
            if (const auto* leap = game_data.skills.get(id); landing && leap && leap->srvstfunc == d2d::rules::ServerStartFunction::kLeapAttack) {
                monster.leap_until = unit.mode_ms + 11 * game_data.npc_timing(monster.npc, played).ms_per_frame() * 100 / std::uint32_t(100 + monster.anim_pct);
            }
            // Whirlwind (srvstfunc 38, FUN_005d8f50; Talic's seq_ancientwhirlwind):
            // in reach a plain attack (FUN_0056e230); else a path to its point
            // (FUN_0064ea90) at its walk velocity (FUN_0056e5b0: MonStats
            // Velocity), its sequence looping till it's there (FUN_005d9460).
            if (const auto* spin = game_data.skills.get(id); spin && spin->srvstfunc == d2d::rules::ServerStartFunction::kWhirlwind && seq) {
                const int to_x = act.x || act.y ? act.x : target_x, to_y = act.x || act.y ? act.y : target_y;
                if (input.in_melee) use("A1", -1);
                else if (path_to(level, monster, to_x, to_y, false, crowd)) {
                    monster.whirl = true; monster.whirl_ms = now_ms; monster.whirl_row = 0; monster.whirl_last = -1;
                    d2d::log::info("{} whirls to ({}, {})", monster.npc.name, to_x, to_y);
                } else { set_mode(game_data, monster, "NU", now_ms); monster.skill = -1; idle(0); return true; }
            }
            const bool explode = plain_skill && plain_skill->srvdofunc == d2d::rules::ServerDoFunction::kCorpseExplosion;   // Nihlathak's, at the corpse he found
            monster.skill_unit = name == "Resurrect" || name == "Resurrect2" || explode ? corpse : act.unit == ThinkUnit::aid ? aid : (type_info.ai_name == "Imp" || type_info.ai_name == "PutridDefiler") && act.unit == ThinkUnit::kin ? kin : -1;
            // Where a Nest's young come out: Blood Raven's spot off the
            // target, else spawnx / spawny off itself.
            monster.skill_x = act.x || act.y ? target_x + act.x : x + type_info.spawn_x;
            monster.skill_y = act.x || act.y ? target_y + act.y : y + type_info.spawn_y;
            // The priests' teleport point, the Council's corner, Baal's
            // teleport (25 off, away from the target) and the imps' and
            // Nihlathak's (FUN_005df850, off themselves) are the map's.
            if (act.spot) { monster.skill_x = target_x; monster.skill_y = target_y; }   // at the target's spot (UberIzual's teleport)
            if ((type_info.ai_name == "ZakarumPriest" || type_info.ai_name == "HighPriest" || type_info.ai_name == "Imp" || type_info.ai_name == "Nihlathak" || baal) && (act.x || act.y)) { monster.skill_x = act.x; monster.skill_y = act.y; }
            // The hell meteor's and the turret's points are the map's too, aimed at (do 28 / 105 read it, FUN_0056d2c0).
            if ((type_info.ai_name == "HellMeteor" || type_info.ai_name == "DesertTurret") && act.unit == d2d::rules::ThinkUnit::none) {
                monster.skill_x = act.x || act.y ? act.x : x; monster.skill_y = act.x || act.y ? act.y : y; monster.at_point = true;
            }
            if (monster.skill_unit >= 0) unit.dir = direction16(pack[std::size_t(monster.skill_unit)].unit.x - unit.x, pack[std::size_t(monster.skill_unit)].unit.y - unit.y);
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
void countess_firewall(const GameData& game_data, const Monster& monster, std::uint32_t now_ms, std::vector<Missile>& missiles,
                       const std::string& maker_name = "countessfirewallmaker") {
    const auto maker = game_data.missiles.find(maker_name);
    if (maker == game_data.missiles.end()) return;
    const auto fire = game_data.missiles.find(maker->second.sub);       // its SubMissile1 (countessfirewall, vampirefirewall)
    if (fire == game_data.missiles.end()) return;
    const int lvl = skill_level(game_data, monster, monster.skill);
    const auto& fire_info = fire->second;
    const float x = (float(monster.skill_x) + 0.5f) / 5, y = (float(monster.skill_y) + 0.5f) / 5;
    const float across_x = -(monster.unit.y - y), across_y = monster.unit.x - x, length = std::max(std::hypot(across_x, across_y), 0.01f);
    const float speed = missile_speed(maker->second, lvl);
    Missile burner{ &fire_info, x, y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(fire_info.range + fire_info.lev_range * lvl, 1)) * 40, monster.stats };
    burner.level = lvl;
    burner.row = d2d::rules::row_damage(fire_info.etype, fire_info.emin, fire_info.emax, fire_info.emin_lev, fire_info.emax_lev, fire_info.hitshift, fire_info.elen,
                                        fire_info.elen_lev, lvl);
    for (const float side : { 1.f, -1.f }) {
        Missile made = burner;
        made.info = &maker->second; made.sub = &fire_info;
        made.velocity_x = across_x / length * speed * side; made.velocity_y = across_y / length * speed * side;
        made.dir = missile_heading(made.velocity_x, made.velocity_y);
        made.dies = now_ms + std::uint32_t(std::max(maker->second.range, 1)) * 40;
        missiles.push_back(std::move(made));
    }
    missiles.push_back(std::move(burner));
}

}  // namespace

bool monster_update(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, d2d::rules::Rng& rng,
                    std::uint32_t now_ms, float elapsed, const Crowd& crowd, std::vector<Missile>& missiles, std::span<Monster> pack,
                    std::vector<Monster>* born, AreaSeen* seen, const OpenDoor* open_door, Spawning::LevelState* rooms_in_play) {
    auto& unit = monster.unit;
    // Its foe: the one its last think's mode or move was set against
    // (FUN_005ddf90 / FUN_005dead0 / FUN_005deb60 put the target in the mode
    // ctx, FUN_005a7c20 on the path, +0x58): an attack's hit, a shot's aim
    // and a flight go at it. With none alive, the nearest of the player's side.
    Foe* pick = &foes[0];
    for (auto& foe : foes)
        if (!foe.of && foe.alive && (!pick->alive || std::hypot(foe.x - unit.x, foe.y - unit.y) < std::hypot(pick->x - unit.x, pick->y - unit.y))) pick = &foe;
    if (monster.chase >= 0 && std::size_t(monster.chase) < foes.size() && foes[std::size_t(monster.chase)].alive) pick = &foes[std::size_t(monster.chase)];
    Foe& foe = *pick;
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    if (!monster.alive()) {
        if (monster.mode == "DT" && now_ms >= monster.mode_until) set_mode(game_data, monster, "DD", now_ms);
        return false;
    }
    // A riding imp sits on its mount (FUN_00621ce0's bind).
    if (monster.riding && monster.bound >= 0 && std::size_t(monster.bound) < pack.size()) { unit.x = pack[std::size_t(monster.bound)].unit.x; unit.y = pack[std::size_t(monster.bound)].unit.y; }
    // A breath under way (FUN_005cc4e0 re-run, FUN_005417d0 type 0): a flame
    // every calc3 frames (at least 1) at its foe till its end; the mode held.
    // Imp Inferno's (FUN_005d1350, event 1): every 3 frames, each flame's
    // range calc2 + its row's Param2 (FUN_0064a2b0 / FUN_0064a330).
    // ponytail: the flame's range is its Missiles.txt row's (FUN_005cc2e0 sets calc1's when it has one).
    if (monster.breath >= 0) {
        if (now_ms >= monster.breath_until) monster.breath = -1;
        else if (now_ms >= monster.breath_next) {
            if (const auto* cast = game_data.skills.get(monster.breath)) {
                const int lvl = skill_level(game_data, monster, monster.breath);
                const bool inferno = cast->srvdofunc == d2d::rules::ServerDoFunction::kImpInferno;
                const std::size_t sent = missiles.size();
                andariel_missile(game_data, monster, cast->srvmissilea, foe.x - unit.x, foe.y - unit.y, now_ms, missiles, lvl, cast);
                if (inferno && missiles.size() > sent)
                    missiles.back().dies = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data.skills, cast->calc[1], {}, monster.breath, lvl) + missiles.back().info->param2, 1)) * 40;
                monster.breath_next = now_ms + std::uint32_t(inferno ? 3 : std::max(d2d::rules::eval_calc(game_data.skills, cast->calc[2], {}, monster.breath, lvl), 1)) * 40;
            }
            return false;
        }
    }
    if (monster.mode == "KB") {                                     // knocked back (Fight::land)
        if (knock_slide(monster, now_ms)) return false;
        if (type_info.base == 0x4e) {                                  // FUN_005a8520: a Sand Leaper thinks on, else GH
            set_mode(game_data, monster, "NU", now_ms);
            monster.next_act = now_ms + (now_ms < monster.stun_until ? 45 : 15) * 40;
        } else set_mode(game_data, monster, "GH", now_ms);
        return false;
    }
    if (now_ms < monster.stun_until && monster.mode != "GH" && monster.mode != "BL") {   // stunned: stands (a get-hit plays out first)
        if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
        return false;
    }
    if (monster.mode == "GH" || monster.mode == "BL") {
        monster.leaping = false;                                       // struck mid-leap: it stays where it is
        if (now_ms < monster.mode_until) return false;
        set_mode(game_data, monster, "NU", now_ms);
    }
    // DiabRun's sequence (seq_diablorun, RN rows; FUN_005cd380 on its event
    // rows): rows 0..Param3 - 1 wind up; at Param3 (5) he runs at the spot
    // (path type 1) at max(calc1 << 8, 0x100) x velocitypercent (stat 0x43)
    // / 100 (FUN_00483360 into the path's +0x7c, a MonStats Velocity's
    // unit), the rows looping Param6..Param4 (6..13) on the way; the run
    // over (record flag 2: there, or blocked), rows Param2.. (14..21) play
    // out. Unit +0x4e is the frame's event (FUN_00623e.., the sequence's
    // event column): on an event-1 row (18) the hit lands when the target
    // is in reach (FUN_00622c40): the skill's damage (FUN_0056e680, flags 0x20).
    // ponytail: the hit is rolled as a swing (monster_blow).
    if (monster.diab_run > 0) {
        const auto* run_skill = game_data.skills.get(monster.skill);
        const SeqRows* seq = skill_seq(game_data, type_info, monster.skill);
        if (!run_skill || !seq || monster.mode != "RN") {                      // struck out of it (GH): over
            monster.diab_run = 0; monster.seq_frame = -1;
            if (monster.mode == "RN") set_mode(game_data, monster, "NU", now_ms);
            return false;
        }
        const auto& par = run_skill->par;
        const int lvl = skill_level(game_data, monster, monster.skill);
        const std::uint32_t frame_ms = std::max<std::uint32_t>(game_data.npc_timing(monster.npc, "RN").ms_per_frame() * 100 / std::uint32_t(100 + monster.anim_pct), 1);
        const int frames = int((now_ms - monster.diab_run_ms) / frame_ms);
        const auto contact = [&] {
            if (!foe.alive || d2d::rules::unit_distance(subtile(foe.x) - subtile(unit.x), subtile(foe.y) - subtile(unit.y), type_info.size, foe.size) > type_info.melee_rng + 1) return;
            auto stats = monster.stats;
            stats.a1_min = d2d::rules::skill_phys(game_data.skills, *run_skill, {}, lvl, false) >> 8;
            stats.a1_max = std::max(d2d::rules::skill_phys(game_data.skills, *run_skill, {}, lvl, true) >> 8, stats.a1_min);
            for (auto& element : stats.elements) element.mode = "";
            const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), stats, false, rng);
            foe.take(taken);
            if (taken.hit) foe.hit_class = type_info.hit_class;
        };
        if (monster.diab_run == 1) {
            monster.seq_frame = std::min(frames, par[2]);
            if (frames >= par[2]) {
                monster.diab_run = path_to(level, monster, monster.skill_x, monster.skill_y, false, crowd) ? 2 : 3;
                monster.diab_run_ms = now_ms;
            }
            return false;
        }
        if (monster.diab_run == 2) {
            const int loop = std::max(par[3] - par[5] + 1, 1);
            monster.seq_frame = par[2] + frames <= par[3] ? par[2] + frames : par[5] + (par[2] + frames - par[5]) % loop;
            const float chill = now_ms < monster.chill_until ? float(100 + type_info.diff[std::size_t(monster.difficulty)].cold_effect) / 100.f : 1.f;
            const float pace = float(std::max(100 + monster.speed_pct + monster.boss_speed, 10)) / 100;
            const float step = cells_per_sec(float(std::max(d2d::rules::eval_calc(game_data.skills, run_skill->calc[0], {}, monster.skill, lvl), 1))) * pace * elapsed * chill;
            if (!move_frame(level, monster, nullptr, true, type_info.size, step, crowd)) { monster.diab_run = 3; monster.diab_run_ms = now_ms; }
            return false;
        }
        const int row = par[1] + frames;
        for (int k = std::max(monster.seq_frame + 1, par[1]); k <= row && k < int(seq->size()); ++k)
            if ((*seq)[std::size_t(k)].event == 1) contact();                     // the stop's event-1 row (18)
        monster.seq_frame = row;
        if (row < int(seq->size())) return false;
        monster.diab_run = 0; monster.seq_frame = -1;
        set_mode(game_data, monster, "NU", now_ms);
        monster.skill = -1;
        monster.next_act = now_ms + std::uint32_t(type_info.diff[std::size_t(monster.difficulty)].aidel) * 40;
        return false;
    }
    // A whirl (do 76, FUN_005d9580): its A1 rows loop as it goes; on each
    // event row a monster's coin (FUN_005d9320: its seed's low bit clear)
    // gives one hit at the next unit within 5 by id from the last
    // (FUN_0056bd10), rolled to hit with the skill's to-hit, calc1 % more
    // damage, the skill's element (FUN_0056e0c0). There (or blocked) it ends.
    if (monster.whirl) {
        const auto* spin = game_data.skills.get(monster.skill);
        const SeqRows* seq = spin ? skill_seq(game_data, type_info, monster.skill) : nullptr;
        if (!spin || !seq || seq->empty() || monster.mode != "A1") {           // struck out of it: over
            monster.whirl = false; monster.seq_frame = -1;
            return false;
        }
        const int lvl = skill_level(game_data, monster, monster.skill);
        const std::uint32_t frame_ms = std::max<std::uint32_t>(game_data.npc_timing(monster.npc, "A1").ms_per_frame() * 100 / std::uint32_t(100 + anim_rate_pct(monster, "A1")), 1);
        const int rows = int((now_ms - monster.whirl_ms) / frame_ms);
        for (; monster.whirl_row <= rows; ++monster.whirl_row) {
            const auto& row = (*seq)[std::size_t(monster.whirl_row) % seq->size()];
            if (row.event != 1 || (monster.seed.next() & 1) != 0) continue;
            const Foe* next = nullptr;
            const Foe* lowest = nullptr;
            for (const auto& near_foe : foes) {
                if (!near_foe.alive || d2d::rules::ai_distance(subtile(near_foe.x) - subtile(unit.x), subtile(near_foe.y) - subtile(unit.y)) > 5) continue;
                if (!lowest || near_foe.id < lowest->id) lowest = &near_foe;
                if (near_foe.id > monster.whirl_last && (!next || near_foe.id < next->id)) next = &near_foe;
            }
            if (!next) next = lowest;
            if (!next) { monster.whirl_last = -1; continue; }
            monster.whirl_last = next->id;
            auto& struck = const_cast<Foe&>(*next);
            const auto taken = d2d::rules::monster_blow(struck.fighter, struck.level, struck.motion(), skill_strike(game_data, monster, *spin, lvl), false, rng);
            struck.take(taken);
            if (taken.hit) struck.hit_class = type_info.hit_class;
            if (taken.hit) monprop_hit(monster, struck, false);         // a melee blow: MonProp knock / crush, as the strike's
            struck.melee_by.push_back(monster.id);
        }
        monster.seq_frame = (*seq)[std::size_t(rows) % seq->size()].frame;
        monster.seq_mode = (*seq)[std::size_t(rows) % seq->size()].mode;
        const float chill = now_ms < monster.chill_until ? float(100 + type_info.diff[std::size_t(monster.difficulty)].cold_effect) / 100.f : 1.f;
        const float pace = float(std::max(100 + monster.speed_pct + monster.boss_speed, 10)) / 100;
        const float step = cells_per_sec(float(type_info.velocity)) * pace * elapsed * chill;
        if (move_frame(level, monster, nullptr, false, type_info.size, step, crowd)) return false;
        monster.whirl = false; monster.seq_frame = -1;
        set_mode(game_data, monster, "NU", now_ms);
        monster.skill = -1;
        monster.next_act = now_ms + std::uint32_t(type_info.diff[std::size_t(monster.difficulty)].aidel) * 40;
        return false;
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
    // An attack, a taunt (the Fallen's S2), a skill's sequence (MonSeq: its
    // event rows), a young one coming out (spawnmode S1), a skill cast in S3
    // (Diablo's Wall, Prison and Prime Firewall: Sk*mode S3).
    const bool attack = monster.mode == "A1" || monster.mode == "A2";
    if (attack || monster.mode == "S1" || monster.mode == "S2" || monster.mode == "SC" || (monster.mode == "S3" && monster.skill >= 0)
        || (monster.mode == "DT" && monster.skill >= 0)) {   // DT alive: a sequence (the egg's hatch)
        const auto& timing = game_data.npc_timing(monster.npc, monster.mode);
        const SeqRows* seq = skill_seq(game_data, type_info, monster.skill);
        if (seq && seq->front().mode == "WL") seq = nullptr;               // a charge's swing: one hit (its flag 0x40 after the first)
        const auto skill = [&](const char* name) { return seq && monster.skill == skill_id(game_data, name); };
        // A sequence's rows, one a frame (FUN_005a8670, SQ's event function):
        // event 4, or 1 / 2 till a shot has marked it done (unit flag 0x40),
        // runs its skill; a plain mode strikes on its action frame.
        int due = 0;
        if (seq) {
            const int reached = std::min(int((now_ms - unit.mode_ms) / std::max<std::uint32_t>(timing.ms_per_frame() * 100 / std::uint32_t(100 + monster.anim_pct), 1)), int(seq->size()) - 1);
            monster.seq_mode = (*seq)[std::size_t(reached)].mode;
            monster.seq_frame = (*seq)[std::size_t(reached)].frame;
            for (; monster.seq_done <= reached; ++monster.seq_done) {
                const int event = (*seq)[std::size_t(monster.seq_done)].event;
                if (event == 4 || ((event == 1 || event == 2) && !monster.seq_once)) ++due;
                // Jab's client do (cltdofunc 16, FUN_004f4590) on an event-3 row: Duriel's.
                if (const auto* jab = event == 3 ? game_data.skills.get(monster.skill) : nullptr; jab && jab->clt_do_func == 16) {
                    monster.seq_sound = jab->do_sound_b;
                    ++monster.seq_sounds;
                }
            }
        } else if (!monster.struck && now_ms >= unit.mode_ms + timing.action_ms() * 100 / std::uint32_t(100 + anim_rate_pct(monster, monster.mode))) {
            monster.struck = true;
            due = 1;
        }
        for (; due > 0; --due) {
            const bool s1_shot = monster.mode == "S1" && monster.skill < 0;   // the Council's S1 (MonAct::s1)
            const auto& shot = monster.mode == "A1" ? type_info.miss_a1 : monster.mode == "SC" ? type_info.miss_c : s1_shot ? type_info.miss_s1 : type_info.miss_a2;   // MissA1 / MissA2 / MissC / MissS1
            const auto fired = shot.empty() || !(attack || monster.mode == "SC" || s1_shot) ? game_data.missiles.end() : game_data.missiles.find(shot);
            // Quick Strike (srvdofunc 92, FUN_005cbf90 -> FUN_0056ecb0): its
            // srvmissilea raven1, Blood Raven's MissA1 too.
            if ((monster.skill < 0 || skill("Quick Strike")) && fired != game_data.missiles.end()) {   // fire: at the foe, from here
                const auto& missile_info = fired->second;
                // MissA's level: MonsterSkillBonus + 1 (FUN_005a7670, objects.md "Trap-Missile").
                const int lvl = d2d::rules::monster_skill_level(1, monster.difficulty);
                // At its point instead when its mode was set there (the wisps' dance).
                const float aim_x = monster.at_point ? (float(monster.skill_x) + 0.5f) / 5 - unit.x : dx, aim_y = monster.at_point ? (float(monster.skill_y) + 0.5f) / 5 - unit.y : dy;
                const float speed = missile_speed(missile_info, lvl), distance = std::max(std::hypot(aim_x, aim_y), 0.01f);
                Missile x{ &missile_info, unit.x, unit.y, aim_x / distance * speed, aim_y / distance * speed, missile_facing(unit.x, unit.y, aim_x, aim_y), now_ms,
                           now_ms + std::uint32_t(missile_info.range) * 40, monster.stats };
                x.level = lvl; x.to = { unit.x + aim_x, unit.y + aim_y }; x.owner = monster.id;
                const bool first = monster.mode == "A1", cast = monster.mode == "SC";   // an A1 shot carries A1's damage; S1's S1's; SC's only its elements (El SC)
                const int src_min = first ? monster.stats.a1_min : s1_shot ? monster.stats.s1_min : monster.stats.a2_min;
                const int src_max = first ? monster.stats.a1_max : s1_shot ? monster.stats.s1_max : monster.stats.a2_max;
                x.src.a2_min = (cast ? 0 : src_min * missile_info.src_damage / 128) + missile_info.min;
                x.src.a2_max = (cast ? 0 : src_max * missile_info.src_damage / 128) + missile_info.max;
                if (s1_shot) x.src.to_hit = monster.stats.to_hit_s1;           // S1TH (monster_blow rolls to_hit)
                if (first || cast || s1_shot)                                  // and its elements (a Skeleton Mage's El1 A1): the blow rolls A2's
                    for (auto& element : x.src.elements) element.mode = element.mode == (cast ? "SC" : s1_shot ? "S1" : "A1") ? "A2" : "";
                missiles.push_back(x);
                // Multishot (FUN_005a3610, the missile hook): two more, aimed a
                // subtile to either side.
                using d2d::rules::Boss;
                if ((monster.boss == Boss::unique || monster.boss == Boss::superunique) && std::ranges::contains(monster.mods, d2d::rules::umod::multishot))
                    for (const float side : { 0.2f, -0.2f }) {
                        const float side_x = dx - dy / std::max(dist, 0.01f) * side, side_y = dy + dx / std::max(dist, 0.01f) * side;
                        const float side_distance = std::max(std::hypot(side_x, side_y), 0.01f);
                        Missile y = x;
                        y.velocity_x = side_x / side_distance * speed; y.velocity_y = side_y / side_distance * speed; y.dir = missile_facing(unit.x, unit.y, side_x, side_y);
                        y.to = { unit.x + side_x, unit.y + side_y };
                        missiles.push_back(std::move(y));
                    }
            } else if (const auto* fired_skill = seq ? game_data.skills.get(monster.skill) : nullptr;
                       fired_skill && fired_skill->srvdofunc == d2d::rules::ServerDoFunction::kTransMissile) {
                // srvdofunc 85 (FUN_005cb0c0; ShamanFire, UnHolyBolt): its
                // srvmissilea's row + TransLvl at the target, once (flag 0x40).
                const auto& base = fired_skill->srvmissilea;
                const auto stem = base.substr(0, base.find_last_not_of("0123456789") + 1);
                const int first = base.size() > stem.size() ? std::atoi(base.c_str() + stem.size()) : 1;
                andariel_missile(game_data, monster, stem + std::to_string(first + type_info.trans_lvl), dx, dy, now_ms, missiles, skill_level(game_data, monster, monster.skill));
                monster.seq_once = true;
            } else if (skill("DesertTurret")) {                               // srvdofunc 105 (FUN_005cd6a0): calc1 desertfireballs fanned at its point
                const int lvl = skill_level(game_data, monster, monster.skill);
                const auto* fan = game_data.skills.get(monster.skill);
                const int count = fan ? d2d::rules::eval_calc(game_data.skills, fan->calc[0], {}, monster.skill, lvl) : 0;
                for (const auto& [from_x, from_y, off_x, off_y] : d2d::rules::turret_fan(subtile(unit.x), subtile(unit.y), monster.skill_x, monster.skill_y, count)) {
                    const std::size_t had = missiles.size();
                    andariel_missile(game_data, monster, fan->srvmissilea, float(off_x) / 5, float(off_y) / 5, now_ms, missiles, lvl, fan);
                    if (missiles.size() > had) {
                        auto& launched = missiles.back();
                        launched.x = (float(from_x) + 0.5f) / 5; launched.y = (float(from_y) + 0.5f) / 5;
                        launched.to = { launched.x + float(off_x) / 5, launched.y + float(off_y) / 5 };
                    }
                }
                monster.seq_once = true;
            } else if (skill("ArcaneTower")) {                                // srvdofunc 106 (FUN_005cd870): with a target, the nova of its srvmissilea
                const int lvl = skill_level(game_data, monster, monster.skill);
                if (const auto* tower = game_data.skills.get(monster.skill); tower && foe.alive)
                    for (int k = 0; k < 64; ++k) {
                        const float angle = float(k) * 2 * 3.14159265f / 64;
                        const std::size_t had = missiles.size();
                        andariel_missile(game_data, monster, tower->srvmissilea, std::cos(angle), std::sin(angle), now_ms, missiles, lvl, tower);
                        if (missiles.size() > had) missiles.back().to.reset();
                    }
                monster.seq_once = true;
            } else if (skill("GargoyleTrap")) {                               // srvdofunc 93 (FUN_005cc050): srvmissilea shafire3 square on at its target
                const auto [from_x, from_y, off_x, off_y] = d2d::rules::gargoyle_shot(subtile(unit.x), subtile(unit.y), subtile(foe.x), subtile(foe.y));
                const std::size_t had = missiles.size();
                andariel_missile(game_data, monster, "shafire3", float(off_x) / 5, float(off_y) / 5, now_ms, missiles, skill_level(game_data, monster, monster.skill));
                if (missiles.size() > had) {
                    auto& launched = missiles.back();
                    launched.x = (float(from_x) + 0.5f) / 5; launched.y = (float(from_y) + 0.5f) / 5;
                    launched.to = { launched.x + float(off_x) / 5, launched.y + float(off_y) / 5 };
                }
            } else if (skill("Nest")) {                                       // srvdofunc 91 (FUN_005cbe00): its spawn at the skill's spot, in spawnmode
                // Through FUN_005b2f20 within 1 of the spot, flags 0x4020000 (no
                // experience, no drop); Skills +0xe6's overlay is none for Nest.
                const int room = room_at(level, (float(monster.skill_x) + 0.5f) / 5, (float(monster.skill_y) + 0.5f) / 5);
                const auto young = game_data.monsters.by_id.find(type_info.spawn);
                if (int at_x = 0, at_y = 0; born && young != game_data.monsters.by_id.end()
                    && spawn_spot(game_data, level, crowd, rooms_in_play, young->second, monster.skill_x, monster.skill_y, 1, room, at_x, at_y)) {
                    auto laid = make_monster(game_data, young->second, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes));
                    laid.aware = true; laid.no_exp = true; laid.no_drop = true;
                    laid.seed = d2d::rules::Rng{ rng.next() };                 // its own seed, drawn off the game's
                    if (type_info.spawn_mode != "NU" && !type_info.spawn_mode.empty()) set_mode(game_data, laid, type_info.spawn_mode, now_ms);
                    born->push_back(std::move(laid));
                }
            } else if (const auto* fire = seq ? game_data.skills.get(monster.skill) : nullptr; fire && fire->srvdofunc == d2d::rules::ServerDoFunction::kImpInferno) {
                // imp6..8's Inferno (seq_impinferno, its event-4 row 12): do 126 as the
                // plain SC cast's, its end calc1 frames on (rand(par3, par4), its seed).
                // ponytail: the end counts from the event, not the sequence's start (srvst 59).
                const int lvl = skill_level(game_data, monster, monster.skill);
                d2d::rules::CalcEnv env;
                env.rng = &monster.seed;
                monster.breath = monster.skill;
                monster.breath_state = true;
                monster.breath_until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data.skills, fire->calc[0], env, monster.skill, lvl), 1)) * 40;
                monster.breath_next = now_ms;
                monster.next_act = std::max(monster.next_act, monster.breath_until);
                d2d::log::info("{} breathes its Inferno (sequence)", monster.npc.name);
            } else if (const auto* leap = seq ? game_data.skills.get(monster.skill) : nullptr; leap && leap->srvdofunc == d2d::rules::ServerDoFunction::kLeapAttack) {
                // Leap Attack's events (do 78, FUN_005da7e0): in the air they fly
                // (FUN_005d9f70); down, the next strikes once (FUN_005da660): the
                // target in reach, knockback (flags | 8).
                if (!monster.leaping) {
                    if (foe.alive && dist <= kMeleeReach + 0.3f) {
                        const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), skill_strike(game_data, monster, *leap, skill_level(game_data, monster, monster.skill)), false, rng);
                        foe.take(taken);
                        if (taken.hit) { foe.knocked_from = std::pair{ unit.x, unit.y }; foe.hit_class = type_info.hit_class; monprop_hit(monster, foe, false); }
                        d2d::log::info("{} lands its leap attack ({})", monster.npc.name, taken.hit ? "hit" : "missed");
                        foe.melee_by.push_back(monster.id);
                    }
                    monster.seq_once = true;
                }
            } else if (const auto* maul = seq ? game_data.skills.get(monster.skill) : nullptr; maul && maul->srvdofunc == d2d::rules::ServerDoFunction::kDeathMaul) {
                // DeathMaul (do 136, FUN_005d25b0; seq_deathmaulerdig's one event):
                // its srvmissilea at the target's spot (FUN_0056ede0: flag 1, none
                // past 100), its range by the distance d (at least 1): d x Range / 12
                // past 9, else d x Range / 24 + Range / 2; calc1 its anim rate.
                // ponytail: d is ai_distance (FUN_006417f0 not traced); the anim rate isn't kept.
                const int lvl = skill_level(game_data, monster, monster.skill);
                const int apart = std::max(d2d::rules::ai_distance(subtile(foe.x) - subtile(unit.x), subtile(foe.y) - subtile(unit.y)), 1);
                const std::size_t had = missiles.size();
                if (apart <= 100) andariel_missile(game_data, monster, maul->srvmissilea, dx, dy, now_ms, missiles, lvl, maul);
                if (missiles.size() > had) {
                    auto& erupt = missiles.back();
                    const int reach = erupt.info ? erupt.info->range : 1;
                    const int frames = apart > 9 ? apart * reach / 12 : apart * reach / 24 + reach / 2;
                    erupt.x = (float(subtile(foe.x)) + 0.5f) / 5; erupt.y = (float(subtile(foe.y)) + 0.5f) / 5;
                    erupt.velocity_x = erupt.velocity_y = 0;
                    erupt.dies = now_ms + std::uint32_t(std::max(frames, 1)) * 40;
                    d2d::log::info("{} mauls from below ({} frames)", monster.npc.name, frames);
                }
                monster.seq_once = true;
            } else if (const auto* done = seq ? game_data.skills.get(monster.skill) : nullptr; done && done->srvdofunc == d2d::rules::ServerDoFunction::kMaggotLay) {
                // MagottLay (srvdofunc 87, FUN_005cb3c0): its MonStats spawn (an
                // egg) in spawnmode two subtiles toward the target: the 64-way
                // direction (FUN_0064fdc0) in 8 (DAT_00745600), the ring-2 step
                // (DAT_006e3138 into DAT_006ea998 / DAT_006ea978).
                // Made exactly there (FUN_005b2f20 radius -1, none where it won't
                // fit), flag 0x4000000 (no experience).
                static constexpr std::array<int, 8> kRing{ 10, 8, 22, 20, 18, 16, 14, 12 };
                static constexpr std::array<int, 24> kStepX{ 0, -1, -1, -1, 0, 1, 1, 1, 0, -1, -2, -2, -2, -2, -2, -1, 0, 1, 2, 2, 2, 2, 2, 1 };
                static constexpr std::array<int, 24> kStepY{ -1, -1, 0, 1, 1, 1, 0, -1, -2, -2, -2, -1, 0, 1, 2, 2, 2, 2, 2, 1, 0, -1, -2, -2 };
                const int at_x = subtile(unit.x), at_y = subtile(unit.y);
                const int ring = kRing[std::size_t(((d2d::rules::direction64(at_x, at_y, monster.skill_x, monster.skill_y) + 4) >> 3) & 7)];
                const int egg_x = at_x + kStepX[std::size_t(ring)], egg_y = at_y + kStepY[std::size_t(ring)];
                const int room = room_at(level, (float(egg_x) + 0.5f) / 5, (float(egg_y) + 0.5f) / 5);
                const auto young = game_data.monsters.by_id.find(type_info.spawn);
                if (int put_x = 0, put_y = 0; born && young != game_data.monsters.by_id.end()
                    && spawn_spot(game_data, level, crowd, rooms_in_play, young->second, egg_x, egg_y, -1, room, put_x, put_y)) {
                    auto laid = make_monster(game_data, young->second, (float(put_x) + 0.5f) / 5, (float(put_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes));
                    laid.aware = true; laid.no_exp = true;
                    laid.seed = d2d::rules::Rng{ rng.next() };
                    if (type_info.spawn_mode != "NU" && !type_info.spawn_mode.empty()) set_mode(game_data, laid, type_info.spawn_mode, now_ms);
                    born->push_back(std::move(laid));
                }
            } else if (done && done->srvdofunc == d2d::rules::ServerDoFunction::kHatch) {
                // MaggotEgg (srvdofunc 84, FUN_005cafa0): calc1 of its MonStats
                // spawn (FUN_0056e620), each made by FUN_005b23c0 ->
                // FUN_005b2a00 (spawn_spot) round the egg in its room: the
                // first at its spot alone (radius -1) in the spawn's mode
                // (FUN_0063ea70: MonStats spawnmode), then while fewer than
                // calc1, the radius under 5 and under 2 x calc1 tries, one in
                // mode 8 (S1) at radius 1, the radius up one each time none
                // fits; each flag 0x4000000 (no experience). Then it dies
                // (FUN_0057ccb0). The egg still stands in the crowd: FUN_005b23c0
                // hands FUN_005b2a00 only its room and spot (0x5b2451), no unit
                // to leave out, and it dies after.
                const int lvl = skill_level(game_data, monster, monster.skill);
                const int count = d2d::rules::eval_calc(game_data.skills, done->calc[0], {}, monster.skill, lvl);
                if (const auto young = game_data.monsters.by_id.find(type_info.spawn); born && young != game_data.monsters.by_id.end() && count > 0) {
                    const int egg_x = subtile(unit.x), egg_y = subtile(unit.y), egg_room = room_at(level, unit.x, unit.y);
                    std::vector<Monster> larvae;
                    larvae.reserve(std::size_t(count));
                    Crowd with_larvae = crowd;
                    auto hatch = [&](int radius, std::string_view larva_mode) {
                        int put_x = 0, put_y = 0;
                        if (!spawn_spot(game_data, level, with_larvae, rooms_in_play, young->second, egg_x, egg_y, radius, egg_room, put_x, put_y)) return false;
                        auto& larva = larvae.emplace_back(make_monster(game_data, young->second, (float(put_x) + 0.5f) / 5, (float(put_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes)));
                        larva.aware = true; larva.no_exp = true;
                        larva.seed = d2d::rules::Rng{ rng.next() };
                        if (larva_mode != "NU" && !larva_mode.empty()) set_mode(game_data, larva, larva_mode, now_ms);
                        with_larvae.units.push_back(&larva.unit);
                        return true;
                    };
                    int made = hatch(-1, type_info.spawn_mode) ? 1 : 0;
                    for (int radius = 1, tries = 0; made < count && radius < 5 && tries < count * 2; ++tries) {
                        if (hatch(radius, "S1")) ++made;
                        else ++radius;
                    }
                    for (auto& larva : larvae) born->push_back(std::move(larva));
                }
                monster.hit_points = 0;                                        // no loot, no experience
            } else if (done && done->srvdofunc == d2d::rules::ServerDoFunction::kMaggotDown) {
                // MagottDown (srvdofunc 86, FUN_005cb300): calc1 % of its life back, once (flag 0x40).
                const int lvl = skill_level(game_data, monster, monster.skill);
                const int back = d2d::rules::eval_calc(game_data.skills, done->calc[0], {}, monster.skill, lvl);
                if (back > 0) monster.hit_points = std::min(monster.hit_points + monster.stats.hit_points * back / 100, monster.stats.hit_points);
                monster.seq_once = true;
            } else if (done && done->srvdofunc == d2d::rules::ServerDoFunction::kBestow) {
                // Bestow (srvdofunc 96, FUN_005cc840): the one it's at gets
                // lo + rand(hi - lo) % of its life back on the caster's seed (hi:
                // calc2 in 0..100, lo: calc1 in 0..hi), within 1..its life; once.
                const int lvl = skill_level(game_data, monster, monster.skill);
                const int high = std::min(std::max(d2d::rules::eval_calc(game_data.skills, done->calc[1], {}, monster.skill, lvl), 0), 100);
                const int low = std::min(std::max(d2d::rules::eval_calc(game_data.skills, done->calc[0], {}, monster.skill, lvl), 0), high);
                const int pct = monster.seed(high - low) + low;
                if (monster.skill_unit >= 0 && std::size_t(monster.skill_unit) < pack.size()) {
                    heal(pack[std::size_t(monster.skill_unit)], pack[std::size_t(monster.skill_unit)].stats.hit_points * pct / 100);
                }
                monster.seq_once = true;
            } else if (skill("Resurrect") || skill("Resurrect2")) {           // srvdofunc 97 (FUN_005ccb10 / FUN_005cc960): the corpse back at full life
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
            } else if (const auto* cast = !seq && monster.skill >= 0 ? game_data.skills.get(monster.skill) : nullptr; cast && castable(*cast)) {
                // A plain SC cast (FUN_0056f7f0: its do-func, then its
                // srvmissile): the nova's ring of 64 srvmissilea out to its
                // range + LevRange a level + calc1 (FUN_005c9b50 ->
                // FUN_0056d400); the fire wall (FUN_005c9ea0, the Countess's);
                // else its srvmissile at the target.
                using d2d::rules::ServerDoFunction;
                const int lvl = skill_level(game_data, monster, monster.skill);
                const auto self_index = std::size_t(&monster - pack.data());
                if (cast->srvdofunc == ServerDoFunction::kCryHelp) {
                    // Cry Help (do 128, FUN_005d1800): its group (FUN_0058f730: its
                    // leader's members) gets AI command 1 at the unit it's at till
                    // calc1 (at least 1) frames on; srvoverlay curseattract on it.
                    // ponytail: the unit is the foe it's at, not the think's second
                    // target; the overlay isn't drawn.
                    const int frames = std::max(d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl), 1);
                    const int group = monster.leader >= 0 ? monster.leader : int(self_index);
                    for (std::size_t k = 0; k < pack.size(); ++k)
                        if (auto& member = pack[k]; member.alive() && (member.leader == group || int(k) == group)) {
                            member.ai_command = 1; member.command_foe = foe.id; member.command_until = int(now_ms / 40) + frames;
                        }
                    d2d::log::info("{} cries for help ({} frames)", monster.npc.name, frames);
                } else if (cast->srvdofunc == ServerDoFunction::kOverseerWhip) {
                    // Overseer Whip (do 131, FUN_005d1f70) at a live monster: a minion
                    // (BaseId 0x1c5) not bloodlusted, with rand(100) at least calc1
                    // (65), turns suicide minion (FUN_005d1e10: its summon, summode,
                    // special AI 15, umod 33); else Bloodlust (FUN_005d1d70: its
                    // auratargetstate for auralen frames, aurastats velocity,
                    // attack rate, armor and damage %).
                    // ponytail: the roll is the caster's seed (FUN_0045c390's isn't
                    // read); the new minion stands where the old one was, aware.
                    if (monster.skill_unit >= 0 && std::size_t(monster.skill_unit) < pack.size()) {
                        auto& whipped = pack[std::size_t(monster.skill_unit)];
                        const bool minion = whipped.type >= 0 && game_data.monsters.types[std::size_t(whipped.type)].base == 0x1c5;
                        const bool lusted = whipped.cry.skill == monster.skill && now_ms < whipped.cry.until;
                        const int chance = d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl);
                        const auto summon = game_data.monsters.by_id.find(cast->summon);
                        if (whipped.alive() && minion && !lusted && chance <= monster.seed(100) && born && summon != game_data.monsters.by_id.end()) {
                            auto turned = make_monster(game_data, summon->second, whipped.unit.x, whipped.unit.y, rng, monster.difficulty, true, players_in(foes));
                            turned.aware = true;
                            turned.seed = d2d::rules::Rng{ rng.next() };
                            set_mode(game_data, turned, cast->summode.empty() ? "NU" : cast->summode, now_ms);
                            born->push_back(std::move(turned));
                            whipped.gone = true;
                            d2d::log::info("{} whips a minion into a suicide minion", monster.npc.name);
                        } else if (whipped.alive()) {
                            const int frames = std::max(d2d::rules::eval_calc(game_data.skills, cast->auralen, {}, monster.skill, lvl), 0);
                            put_state(game_data, whipped, whipped.cry, { monster.skill, lvl, now_ms + std::uint32_t(frames) * 40 }, now_ms);
                            d2d::log::info("{} #{} whips {} #{} into a bloodlust ({} frames)", monster.npc.name, monster.id, whipped.npc.name, whipped.id, frames);
                        }
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kImpregnate) {
                    // Impregnate (do 133, FUN_005d2250) at the monster it found
                    // (FUN_005d2140 again): state 0x6e, no end; when it's off at the
                    // unit's death, its summon (painworm1) in summode (FUN_005d21b0).
                    if (monster.skill_unit >= 0 && std::size_t(monster.skill_unit) < pack.size())
                        if (auto& host = pack[std::size_t(monster.skill_unit)]; impregnable(game_data, host)) {
                            host.impregnated = true;
                            d2d::log::info("{} impregnates {}", monster.npc.name, host.npc.name);
                        }
                } else if (cast->srvdofunc == ServerDoFunction::kWarCry) {
                    // A war cry from a monster (do 68, FUN_005d83e0; Madawc's Shout):
                    // the nova of its srvmissilea (FUN_0056d400) and its aurastate on
                    // itself for auralen frames (FUN_005d8290); the nova reaching an
                    // ally (hit 18, FUN_005ab0b0) puts it there too.
                    // ponytail: an ally within the missile's reach (speed x Range), as the
                    // player's war cries; the nova's missiles aren't drawn.
                    const auto shout = game_data.missiles.find(cast->srvmissilea);
                    const float reach = shout == game_data.missiles.end() ? 0.f : missile_speed(shout->second, lvl) * float(shout->second.range) * 0.04f;
                    const int frames = std::max(d2d::rules::eval_calc(game_data.skills, cast->auralen, {}, monster.skill, lvl), 1);
                    const Monster::SkillState state{ monster.skill, lvl, now_ms + std::uint32_t(frames) * 40 };
                    if (!cast->aurastate.empty()) put_state(game_data, monster, monster.cry, state, now_ms);
                    for (auto& ally : pack)
                        if (&ally != &monster && ally.alive() && ally.align == monster.align && std::hypot(ally.unit.x - unit.x, ally.unit.y - unit.y) <= reach)
                            put_state(game_data, ally, ally.cry, state, now_ms);
                    d2d::log::info("{} shouts ({} frames)", monster.npc.name, frames);
                } else if (cast->srvdofunc == ServerDoFunction::kSiegeBeastStomp) {
                    // Siege Beast Stomp (do 134, FUN_005d2320): it needs a srvmissilea
                    // (record +0x48 at least 1) and row 301 has none, so the stomp is its
                    // A2 alone (bugs.md); the shake and dust are the client's (do 75).
                } else if (cast->srvdofunc == ServerDoFunction::kCurse) {
                    // do 30 (FUN_005c37c0) from a monster: its curse on each of
                    // the player's side within aurarangecalc subtiles of the
                    // target's spot (FUN_0056e780, enemies only), the newest in
                    // place of the last (Fight::curses, a pet's Pet::curse).
                    const int radius = d2d::rules::eval_calc(game_data.skills, cast->aurarange, {}, monster.skill, lvl);
                    for (auto& near_foe : foes)
                        if (near_foe.alive && !near_foe.of && !foe.of && std::hypot(near_foe.x - foe.x, near_foe.y - foe.y) * 5 <= float(radius))
                            near_foe.cursed = { monster.skill, lvl };
                } else if (cast->srvdofunc == ServerDoFunction::kNova) {
                    const auto ring = game_data.missiles.find(cast->srvmissilea);
                    const int reach = ring == game_data.missiles.end() ? 0
                        : ring->second.range + ring->second.lev_range * lvl + d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl);
                    for (int k = 0; k < 64; ++k) {
                        const float angle = float(k) * 2 * 3.14159265f / 64;
                        const std::size_t had = missiles.size();
                        andariel_missile(game_data, monster, cast->srvmissilea, std::cos(angle), std::sin(angle), now_ms, missiles, lvl, cast);
                        if (missiles.size() > had) { missiles.back().dies = now_ms + std::uint32_t(std::max(reach, 1)) * 40; missiles.back().to.reset(); }
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kMissileWall) {
                    countess_firewall(game_data, monster, now_ms, missiles, cast->srvmissilea);
                } else if (cast->srvdofunc == ServerDoFunction::kScatteredBolts) {
                    // FUN_005c9300: calc1 of its srvmissilea (FUN_00646ca0) from it toward the target.
                    // ponytail: each aimed off by up to a quarter turn on the game's seed, not FUN_005c9290's own steering.
                    for (int bolts = std::max(d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl), 1); bolts > 0; --bolts) {
                        const float turn = (float(int(rng.next() % 101) - 50) / 100.f) * 1.5707963f;
                        andariel_missile(game_data, monster, cast->srvmissilea, dx * std::cos(turn) - dy * std::sin(turn), dx * std::sin(turn) + dy * std::cos(turn), now_ms, missiles, lvl, cast);
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kMonCurseCast) {
                    // FUN_005ce2b0: rand(5) on its seed picks 0x6e3228's curse (Amplify
                    // Damage, Weaken, Life Tap, Decrepify, Lower Resist), cast at its level
                    // round the target as do 30.
                    // ponytail: the curse's own stat list (by the pick) is the curse skill's.
                    static constexpr std::array<int, 5> kCurses{ 66, 72, 82, 87, 91 };
                    const int curse = kCurses[std::size_t(monster.seed(5))];
                    const auto* cursed = game_data.skills.get(curse);
                    const int radius = cursed ? d2d::rules::eval_calc(game_data.skills, cursed->aurarange, {}, curse, lvl) : 0;
                    for (auto& near_foe : foes)
                        if (near_foe.alive && !near_foe.of && !foe.of && std::hypot(near_foe.x - foe.x, near_foe.y - foe.y) * 5 <= float(radius))
                            near_foe.cursed = { curse, lvl };
                } else if (cast->srvdofunc == ServerDoFunction::kCorpseExplosion) {
                    // do 55 (FUN_005c4df0, the Necromancer's too) at the corpse its
                    // think found (FUN_00553540: its path's unit; FUN_00645680 a
                    // corpse): it's used (state 0x68), and each of the caster's foes
                    // within aurarange / 2 of it (FUN_0056b7e0 with FUN_005c4d10)
                    // takes calc1..calc2 % of the corpse's life, half physical, half
                    // fire, a sure hit (as Fight::corpse_blast).
                    if (monster.skill_unit >= 0 && std::size_t(monster.skill_unit) < pack.size() && !pack[std::size_t(monster.skill_unit)].corpse_used) {
                        auto& corpse = pack[std::size_t(monster.skill_unit)];
                        corpse.corpse_used = true;
                        const int low = d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl);
                        const int high = std::max(d2d::rules::eval_calc(game_data.skills, cast->calc[1], {}, monster.skill, lvl), low);
                        const int dmg = int(std::int64_t(corpse.stats.hit_points) * rng.range(low, high) / 100);
                        const int radius = std::max(d2d::rules::eval_calc(game_data.skills, cast->aurarange, {}, monster.skill, lvl) / 2, 1);
                        auto stats = monster.stats;
                        stats.a1_min = stats.a1_max = dmg / 2;
                        stats.to_hit = 1 << 20;
                        for (auto& element : stats.elements) element = {};
                        stats.elements[0] = { 1, 100, dmg - dmg / 2, dmg - dmg / 2, 0, "A1" };   // fire
                        for (auto& near_foe : foes)
                            if (near_foe.alive && std::hypot(near_foe.x - corpse.unit.x, near_foe.y - corpse.unit.y) * 5 <= float(radius)) {
                                const auto taken = d2d::rules::monster_blow(near_foe.fighter, near_foe.level, near_foe.motion(), stats, false, rng);
                                near_foe.take(taken);
                            }
                        d2d::log::info("{} explodes a corpse ({} damage)", monster.npc.name, dmg);
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kMinionSpawner) {
                    // Start 62 (FUN_005d2420 -> FUN_0056e620 -> FUN_0063ea70): the
                    // caster's MonStats spawn at its spot + spawnx / spawny, in
                    // spawnmode (out of 0..15: NU); no spawn row (Nihlathak's), the
                    // class his think kept (AI control +0x3c: FUN_0063ec70, the
                    // level's own minion). Do 135 (FUN_005d2490) makes it through
                    // FUN_005b2f20: exactly there, else within 4.
                    // Flags 0x4020000 (no experience, no drop); MinionSpawner's Skills
                    // +0xe6 overlay is none. FUN_00621ce0 records the caster as it's
                    // made; nothing reads that for these.
                    const auto spawn_row = game_data.monsters.by_id.find(type_info.spawn);
                    const int young = spawn_row != game_data.monsters.by_id.end() ? spawn_row->second
                                    : type_info.ai_name == "Nihlathak" ? level_monster(game_data, level, int(game_data.mon_bin[0x1c5])) : -1;
                    const int at_x0 = subtile(unit.x) + (spawn_row != game_data.monsters.by_id.end() ? type_info.spawn_x : 0), at_y0 = subtile(unit.y) + (spawn_row != game_data.monsters.by_id.end() ? type_info.spawn_y : 0);
                    const int room = room_at(level, (float(at_x0) + 0.5f) / 5, (float(at_y0) + 0.5f) / 5);
                    if (int at_x = 0, at_y = 0; born && young >= 0
                        && (spawn_spot(game_data, level, crowd, rooms_in_play, young, at_x0, at_y0, -1, room, at_x, at_y) || spawn_spot(game_data, level, crowd, rooms_in_play, young, at_x0, at_y0, 4, room, at_x, at_y))) {
                        auto made = make_monster(game_data, young, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes));
                        made.aware = true; made.no_exp = true; made.no_drop = true;
                        made.seed = d2d::rules::Rng{ rng.next() };
                        const bool spawned_mode = spawn_row != game_data.monsters.by_id.end() && !type_info.spawn_mode.empty() && type_info.spawn_mode != "NU";
                        if (spawned_mode) set_mode(game_data, made, type_info.spawn_mode, now_ms);
                        born->push_back(std::move(made));
                        d2d::log::info("{} spawns {}", monster.npc.name, game_data.monsters.types[std::size_t(young)].id);
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kDiabCold && foe.alive) {
                    // FUN_005cce80: one sure hit (hit flags | 1) of the skill's own damage at the unit (a live one).
                    auto stats = monster.stats;
                    stats.a1_min = stats.a1_max = 0;
                    stats.to_hit = 1 << 20;
                    for (auto& element : stats.elements) element = {};
                    const auto added = d2d::rules::row_damage(cast->etype, cast->emin, cast->emax, cast->emin_lev, cast->emax_lev, cast->hitshift, cast->elen, cast->elen_lev, lvl);
                    if (added.etype >= 0) stats.elements[0] = { added.etype, 100, added.elo >> 8, added.ehi >> 8, added.elen, "A1" };
                    const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), stats, false, rng);
                    foe.take(taken);
                    if (taken.hit) foe.hit_class = type_info.hit_class;
                } else if (cast->srvdofunc == ServerDoFunction::kFingerMageSpider || cast->srvdofunc == ServerDoFunction::kDoomKnightMissile
                           || cast->srvdofunc == ServerDoFunction::kGuidedMissile) {
                    // FUN_005ccfa0 / FUN_005cdfb0 / do 10: one srvmissilea at the target;
                    // the knights' the row on by their variant (monster data +0xe: undeadmissile1..4).
                    std::string name = cast->srvmissilea;
                    if (cast->srvdofunc == ServerDoFunction::kDoomKnightMissile && monster.variant > 0 && !name.empty() && name.back() == '1') name.back() = char('1' + std::min(monster.variant, 3));
                    const std::size_t sent = missiles.size();
                    andariel_missile(game_data, monster, name, dx, dy, now_ms, missiles, lvl, cast);
                    if (cast->srvdofunc == ServerDoFunction::kGuidedMissile && missiles.size() > sent) { missiles.back().target = int(&foe - foes.data()); missiles.back().target_id = foe.id; }   // its unit (flags 0x20)
                } else if (cast->srvdofunc == ServerDoFunction::kDiabPrison) {
                    // FUN_005cd5d0 -> FUN_005b34c0 -> FUN_005b3270 (0x73d4f0, flag 1: the
                    // class steps): boneprison1..4 (0x154..0x157) at the target's diagonals,
                    // (1, 1) first, the other three only once it stood; each on its spot
                    // exactly (FUN_005b3130 / FUN_005b31b0 at radius -1), none where it won't fit.
                    const int foe_room = room_at(level, foe.x, foe.y);
                    if (born && !d2d::rules::level_ids::is_town(level.id))
                        for (std::size_t k = 0; k < kPrison.size(); ++k) {
                            int at_x = 0, at_y = 0;
                            if (!spawn_spot(game_data, level, crowd, rooms_in_play, 0x154 + int(k), subtile(foe.x) + kPrison[k].first, subtile(foe.y) + kPrison[k].second, -1, foe_room, at_x, at_y)) {
                                if (k == 0) break;
                                continue;
                            }
                            auto bone = make_monster(game_data, 0x154 + int(k), (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes));
                            bone.seed = d2d::rules::Rng{ rng.next() };
                            born->push_back(std::move(bone));
                        }
                } else if (cast->srvdofunc == ServerDoFunction::kDiabWall) {
                    // FUN_005cd1c0: calc1 diabwallmakers (srvmissilea, flags 0x21) from him at the
                    // target's spot (FUN_0056d2c0); each (FUN_005cd110, its index) 77 frames at
                    // most, seeded {target x + index, 666}, and on a draw of 20+ of 100 Charged
                    // Bolt's wiggle (path type 10), else straight.
                    const int count = d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl);
                    for (int k = 0; k < count; ++k) {
                        const auto made = missiles.size();
                        andariel_missile(game_data, monster, cast->srvmissilea, dx, dy, now_ms, missiles, lvl, cast);
                        if (missiles.size() > made) { missiles.back().bolt = k; missiles.back().wall_maker = true; }
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kSelfState) {
                    // MonBoneArmor (do 18, FUN_005c9480): its state on itself (Monster::armored,
                    // set as it's thought) and the absorb pool from its aurastat calc.
                    for (std::size_t k = 0; k < cast->aurastat.size(); ++k)
                        if (cast->aurastat[k] == d2d::d2s::kBoneArmor) {
                            monster.armor_pool = d2d::rules::eval_calc(game_data.skills, cast->aura_calc[k], {}, monster.skill, lvl) >> 8;
                            monster.armored = monster.armor_pool > 0;
                        }
                    // Another's (UberIzual's Chilling Armor): its state for auralen frames.
                    // ponytail: the state's stats (armor %) and its ReturnFire aren't put on the monster.
                    if (!cast->aurastate.empty() && !std::ranges::contains(cast->aurastat, d2d::d2s::kBoneArmor))
                        monster.self_state_until = now_ms + std::uint32_t(std::max(0, d2d::rules::eval_calc(game_data.skills, cast->auralen, {}, monster.skill, lvl))) * 40;
                } else if (cast->srvdofunc == ServerDoFunction::kRegurgitatorEat) {
                    // FUN_005cdc10: a corpse (DD) it's at eaten (gone), its life back by the corpse's.
                    // ponytail: calc1 % of the corpse's life (FUN_00483360's args are register-passed, not read).
                    if (monster.skill_unit >= 0 && std::size_t(monster.skill_unit) < pack.size())
                        if (auto& eaten = pack[std::size_t(monster.skill_unit)]; !eaten.alive() && eaten.mode == "DD" && !eaten.corpse_used) {
                            eaten.corpse_used = true;
                            const int pct = d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl);
                            monster.hit_points = std::min(monster.hit_points + std::max(eaten.stats.hit_points * pct / 100, 1), monster.stats.hit_points);
                        }
                } else if (cast->srvdofunc == ServerDoFunction::kMonsterBreath || cast->srvdofunc == ServerDoFunction::kDiabLight || cast->srvdofunc == ServerDoFunction::kImpInferno) {
                    // DiabLight: the same start (srvst 53) and its bolts while state 0xc's on (FUN_005cc690).
                    // FUN_005cc240 / FUN_005cc4e0: state 0xc, its end calc2 frames on; a
                    // flame now and every calc3 frames after (the breath below).
                    // Imp Inferno (FUN_005d1280 / FUN_005d1350): its end calc1 frames
                    // on (rand(par3, par4) on its seed), a flame every 3 frames.
                    monster.breath = monster.skill;
                    monster.breath_state = true;
                    d2d::rules::CalcEnv env;
                    env.rng = &monster.seed;
                    const auto& end_calc = cast->srvdofunc == ServerDoFunction::kImpInferno ? cast->calc[0] : cast->calc[1];
                    monster.breath_until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data.skills, end_calc, env, monster.skill, lvl), 1)) * 40;
                    monster.breath_next = now_ms;
                    monster.next_act = std::max(monster.next_act, monster.breath_until);
                } else if (cast->srvdofunc == ServerDoFunction::kMissileAtTarget) {
                    // FUN_005ca3e0: its srvmissilea at the target (MonBlizzard's centre),
                    // or at its point (the hell meteor's), when the missile's Size
                    // square there is clear of mask 5 (FUN_0056e450 -> FUN_0064d800).
                    // ponytail: a blizzard's centre flies there.
                    // One that doesn't move (Baal Taunt's control) stands at the target.
                    const float aim_x = monster.at_point ? (float(monster.skill_x) + 0.5f) / 5 - unit.x : dx, aim_y = monster.at_point ? (float(monster.skill_y) + 0.5f) / 5 - unit.y : dy;
                    const auto meteor = game_data.missiles.find(cast->srvmissilea);
                    const int size = meteor == game_data.missiles.end() ? 1 : std::max(meteor->second.size, 1);
                    const int point_x = subtile(unit.x + aim_x), point_y = subtile(unit.y + aim_y);
                    bool clear = true;
                    for (int at_y = point_y - (size >> 1); at_y < point_y - (size >> 1) + size; ++at_y)
                        for (int at_x = point_x - (size >> 1); at_x < point_x - (size >> 1) + size; ++at_x)
                            clear = clear && !level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 5);
                    const std::size_t before = missiles.size();
                    if (clear) andariel_missile(game_data, monster, cast->srvmissilea, aim_x, aim_y, now_ms, missiles, lvl, cast);
                    if (missiles.size() > before && missiles.back().velocity_x == 0 && missiles.back().velocity_y == 0) {
                        missiles.back().x += aim_x; missiles.back().y += aim_y;
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kMonPoisonNova) {
                    // FUN_005ccd10: srvmissilea out 8 ways (0x6e31e8 / 0x6e31a8), and the 8 between with calc2 2.
                    const int between = d2d::rules::eval_calc(game_data.skills, cast->calc[1], {}, monster.skill, lvl) > 1 ? 2 : 1;
                    for (int way = 0; way < 8 * between; ++way) {
                        const float angle = float(way) * 2 * 3.14159265f / float(8 * between);
                        andariel_missile(game_data, monster, cast->srvmissilea, std::cos(angle), std::sin(angle), now_ms, missiles, lvl, cast);
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kBestow) {
                    // ZakarumHeal (do 96, FUN_005cc840): as Bestow (the sequence's, below).
                    const int high = std::min(std::max(d2d::rules::eval_calc(game_data.skills, cast->calc[1], {}, monster.skill, lvl), 0), 100);
                    const int low = std::min(std::max(d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl), 0), high);
                    const int pct = monster.seed(high - low) + low;
                    if (monster.skill_unit >= 0 && std::size_t(monster.skill_unit) < pack.size())
                        heal(pack[std::size_t(monster.skill_unit)], pack[std::size_t(monster.skill_unit)].stats.hit_points * pct / 100);
                } else if (cast->srvdofunc == ServerDoFunction::kMonTeleport) {
                    // FUN_005ccc80: to the free spot nearest its skill's point
                    // (FUN_00554ea0 -> FUN_0064e7b0, its size, mask 0x1c09); none: it stays.
                    if (const auto spot = free_spot_near(level, crowd, &unit, monster.skill_x, monster.skill_y, unit.shape.size, 0x1c09))
                        std::tie(unit.x, unit.y) = std::pair{ (float(spot->first) + 0.5f) / 5, (float(spot->second) + 0.5f) / 5 };
                } else if (cast->srvdofunc == ServerDoFunction::kImpTeleport) {
                    // FUN_005d1ab0. Riding: off at the free spot nearest its point
                    // (FUN_005d19d0: state 0x8f off, unbound; under 10 % life it dies
                    // (FUN_0057ccb0), else flags 0xe back, its own think). At a unit
                    // (its mount, BaseId siegebeast1 / barricadetower, unbound, alive):
                    // onto its spot and up (FUN_005d18e0: state 0x8f, bound, flags 0xe
                    // off, special 16 and its init, rules::imp_ride_pick). Else MonTeleport.
                    // Off: FUN_00554ea0's search (FUN_0064e7b0, its size, mask
                    // 0x1c09; none: it stays, off all the same). Up: exactly onto
                    // the mount's spot (FUN_00554ea0 with flag 1: no search).
                    auto* mount = monster.skill_unit >= 0 && std::size_t(monster.skill_unit) < pack.size() ? &pack[std::size_t(monster.skill_unit)] : nullptr;
                    const int mount_base = mount ? game_data.monsters.types[std::size_t(mount->type)].base : -1;
                    if (monster.riding) {
                        if (const auto spot = free_spot_near(level, crowd, &unit, monster.skill_x, monster.skill_y, unit.shape.size, 0x1c09))
                            std::tie(unit.x, unit.y) = std::pair{ (float(spot->first) + 0.5f) / 5, (float(spot->second) + 0.5f) / 5 };
                        if (monster.bound >= 0 && std::size_t(monster.bound) < pack.size()) pack[std::size_t(monster.bound)].bound = -1;
                        monster.bound = -1; monster.riding = false;
                        if (std::int64_t(monster.hit_points) * 100 / std::max(monster.stats.hit_points, 1) < 10) {
                            monster.hit_points = 0;                            // no loot, no experience
                            set_mode(game_data, monster, "DT", now_ms);
                        } else ai_toggle(game_data, monster, 0);
                    } else if (mount && mount->alive() && mount->bound < 0 && type_info.base == 0x1ec && (mount_base == 0x1b3 || mount_base == 0x1b9)) {
                        unit.x = mount->unit.x; unit.y = mount->unit.y;
                        mount->bound = int(&monster - pack.data()); monster.bound = monster.skill_unit;
                        monster.riding = true;
                        ai_toggle(game_data, monster, 16);
                        monster.ai_state2 = d2d::rules::imp_ride_pick(monster.seed, !type_info.skill[2].empty(), !type_info.skill[4].empty());
                    } else if (!mount) {
                        std::tie(unit.x, unit.y) = level.nearest_free((float(monster.skill_x) + 0.5f) / 5, (float(monster.skill_y) + 0.5f) / 5);
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kImpFireMissile) {
                    // FUN_005d2090 -> FUN_0056ecb0: its srvmissilea's row + TransLvl
                    // (MonStats +0x4b: impmiss21..25) at the target, once (flag 0x40),
                    // with its A1 damage by the row's SrcDamage, as an A1 shot.
                    const auto& base = cast->srvmissilea;
                    const auto stem = base.substr(0, base.find_last_not_of("0123456789") + 1);
                    const int first = base.size() > stem.size() ? std::atoi(base.c_str() + stem.size()) : 1;
                    const std::size_t sent = missiles.size();
                    andariel_missile(game_data, monster, stem + std::to_string(first + type_info.trans_lvl), dx, dy, now_ms, missiles, lvl);
                    if (missiles.size() > sent) {
                        auto& imp_shot = missiles.back();
                        imp_shot.src = monster.stats;
                        imp_shot.src.a2_min = monster.stats.a1_min * imp_shot.info->src_damage / 128 + imp_shot.info->min;
                        imp_shot.src.a2_max = monster.stats.a1_max * imp_shot.info->src_damage / 128 + imp_shot.info->max;
                        for (auto& element : imp_shot.src.elements) element.mode = element.mode == "A1" ? "A2" : "";
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kBaalColdMissiles) {
                    // FUN_005d2940: its srvmissilea (baal cold maker) from him at
                    // the target's spot; the maker lays its SubMissile1 as it goes
                    // (missile do 31), each with the skill's cold.
                    andariel_missile(game_data, monster, cast->srvmissilea, foe.x - unit.x, foe.y - unit.y, now_ms, missiles, lvl);
                    const auto maker = game_data.missiles.find(cast->srvmissilea);
                    const auto trail = maker != game_data.missiles.end() ? game_data.missiles.find(maker->second.sub) : game_data.missiles.end();
                    if (trail != game_data.missiles.end() && !missiles.empty()) {
                        auto& made = missiles.back();
                        made.sub = &trail->second;
                        const auto cold = d2d::rules::row_damage(cast->etype, cast->emin, cast->emax, cast->emin_lev, cast->emax_lev, cast->hitshift, cast->elen, cast->elen_lev, lvl);
                        if (cold.etype >= 0) made.src.elements[0] = { cold.etype, 100, cold.elo >> 8, cold.ehi >> 8, cold.elen, "A2" };
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kBaalTentacle) {
                    // FUN_005d2c20: 2 + rand(3) + the difficulty of them, each at the
                    // target +- 9 (two draws of his seed), baaltentacle1..5 (FUN_0063efa0:
                    // a step down the class chain), made in A1, bound to him.
                    // ponytail: the chain's step is rand(5) on his seed.
                    const int count = int(monster.seed.next() % 3) + 2 + monster.difficulty;
                    for (int made = 0; born && made < count; ++made) {
                        const int at_x = subtile(foe.x) + int(monster.seed.next() % 18) - 9, at_y = subtile(foe.y) + int(monster.seed.next() % 18) - 9;
                        const auto kind = game_data.monsters.by_id.find("baaltentacle" + std::to_string(1 + monster.seed(5)));
                        if (kind == game_data.monsters.by_id.end()) continue;
                        auto tentacle = make_monster(game_data, kind->second, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes));
                        std::tie(tentacle.unit.x, tentacle.unit.y) = level.nearest_free(tentacle.unit.x, tentacle.unit.y);
                        tentacle.aware = true;
                        tentacle.seed = d2d::rules::Rng{ rng.next() };
                        set_mode(game_data, tentacle, "A1", now_ms);
                        born->push_back(std::move(tentacle));
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kHydra) {
                    // FUN_005ca910: three of its summon (hydra1) at the target, each gone Param1 frames on (AI control +0x14).
                    // ponytail: side by side, not FUN_0056d2c0's spots.
                    if (const auto summoned = game_data.monsters.by_id.find(cast->summon); born && summoned != game_data.monsters.by_id.end())
                        for (int head = 0; head < 3; ++head) {
                            auto hydra = make_monster(game_data, summoned->second, foe.x + float(head - 1) * 0.6f, foe.y, rng, monster.difficulty, true, players_in(foes));
                            std::tie(hydra.unit.x, hydra.unit.y) = level.nearest_free(hydra.unit.x, hydra.unit.y);
                            hydra.aware = true;
                            hydra.seed = d2d::rules::Rng{ rng.next() };
                            hydra.ai_state = int(now_ms / 40) + cast->par[0];
                            born->push_back(std::move(hydra));
                        }
                } else {
                    andariel_missile(game_data, monster, cast->srvmissile, dx, dy, now_ms, missiles, lvl, cast);
                }
            } else if (monster.skill >= 0 && monster.skill == skill_id(game_data, "SpiderLay")) {
                // srvdofunc 23 (FUN_005c9c10): its aurastate spiderlay for
                // auralen 300 frames, aurastat velocitypercent -100.
                // ponytail: the slowed state on those about (auratargetstate) left out.
                monster.laid_until = now_ms + 300 * 40;
            } else if (attack && foe.alive && dist <= kMeleeReach + 0.3f) {
                // Melee: block, reductions, resistances; a hit that lands
                // pays the foe's thorns (lightning ones less its resistance).
                auto stats = monster.stats;                                  // Weaken, Decrepify, Battle Cry, Taunt, a boss's aura: its damage %
                // Fire Hit (srvstfunc 42, FUN_005cae40, the Sand Raiders'): the hit
                // is its S1 damage and S1 elements (FUN_005a4f50 mode 8).
                // ponytail: the skill's ResultFlags / HitFlags / HitClass unread.
                const auto* struck_with = monster.skill >= 0 ? game_data.skills.get(monster.skill) : nullptr;
                if (monster.leaping) monster.seq_once = true;               // Leap's hit at its start (FUN_005d9c80), its later rows only fly (do 77)
                // FUN_005a4f50 builds the hit by the mode: A2's damage, to-hit
                // and elements for an A2 swing and Smite (do 150, FUN_005ce9f0:
                // mode 5, a normal hit roll, calc2 frames of stun).
                // On a hit Smite ORs in its ResultFlags (Skills.txt 8: knockback)
                // and HitFlags (none), hit class 0x65 (FUN_005ce9f0).
                // Jab's (do 7, FUN_005db2d0) is mode 8's, S1's; Charge's the mode it swings in.
                const bool smite = struck_with && struck_with->srvdofunc == d2d::rules::ServerDoFunction::kSmite;
                const bool jab = struck_with && struck_with->srvdofunc == d2d::rules::ServerDoFunction::kJab;
                if (jab || (seq && struck_with && struck_with->srvstfunc == d2d::rules::ServerStartFunction::kFireHit)) {
                    stats.a1_min = stats.s1_min; stats.a1_max = stats.s1_max; stats.to_hit = stats.to_hit_s1;
                    for (auto& element : stats.elements) element.mode = element.mode == "S1" ? "A1" : "";
                } else if (smite || monster.mode == "A2") {
                    stats.a1_min = stats.a2_min; stats.a1_max = stats.a2_max; stats.to_hit = stats.to_hit2;
                    for (auto& element : stats.elements) element.mode = element.mode == "A2" ? "A1" : "";
                }
                // Charge's hit (FUN_005cf900): sure to hit (flags 9), calc1 % more
                // damage, the skill's element at its level (FUN_0056e0c0). Jab's
                // (do 7, FUN_005db2d0): the same on a hit it rolls.
                // Power Strike's (start 6, FUN_005da940; MonPowerStrike / MonIceSpear):
                // a hit it rolls with no to-hit bonus, calc1 % more, the element.
                const bool power = struck_with && struck_with->srvstfunc == d2d::rules::ServerStartFunction::kElementalStrike;
                if (struck_with && (jab || power || struck_with->srvdofunc == d2d::rules::ServerDoFunction::kCharge)) {
                    const int lvl = skill_level(game_data, monster, monster.skill);
                    const int more = d2d::rules::eval_calc(game_data.skills, struck_with->calc[0], {}, monster.skill, lvl);
                    for (int* damage : { &stats.a1_min, &stats.a1_max }) *damage = std::max(*damage * (100 + more) / 100, 0);
                    if (!jab && !power) stats.to_hit = 1 << 20;
                    const auto added = d2d::rules::row_damage(struck_with->etype, struck_with->emin, struck_with->emax, struck_with->emin_lev, struck_with->emax_lev,
                                                              struck_with->hitshift, struck_with->elen, struck_with->elen_lev, lvl);
                    if (added.etype >= 0)
                        for (auto& element : stats.elements)
                            if (element.type < 0) { element = { added.etype, 100, added.elo >> 8, added.ehi >> 8, added.elen, "A1" }; break; }
                }
                for (int* damage : { &stats.a1_min, &stats.a1_max }) *damage = std::max(*damage * (100 + monster.dmg_pct + monster.aura_dmg) / 100, 0);
                stats.to_hit += stats.to_hit * monster.aura_th / 100;
                const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), stats, false, rng);
                // MonFrenzy's strike (do 109, FUN_005cdf10): a hit puts on its state
                // (FUN_005d8c70) for ln12 frames, one more charge up to its level.
                if (struck_with && struck_with->srvdofunc == d2d::rules::ServerDoFunction::kMonFrenzy && taken.hit) {
                    const int lvl = skill_level(game_data, monster, monster.skill);
                    monster.frenzy.skill = monster.skill;
                    monster.frenzy.level = std::min(monster.frenzy.level + 1, lvl);
                    monster.frenzy.until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data.skills, struck_with->auralen, {}, monster.skill, lvl), 1)) * 40;
                }
                foe.take(taken);
                if (taken.hit) foe.hit_class = type_info.hit_class;
                if (taken.hit) monprop_hit(monster, foe, false);
                if (taken.hit && smite && (struck_with->result_flags & 8)) foe.knocked_from = std::pair{ unit.x, unit.y };
                if (taken.hit && smite) foe.stun_ticks = std::max(foe.stun_ticks, d2d::rules::eval_calc(game_data.skills, struck_with->calc[1], {}, monster.skill, skill_level(game_data, monster, monster.skill)));
                if (taken.hit && monster.mana_hi > 0) foe.mana_burn += rng.range(monster.mana_lo, monster.mana_hi);   // Mana Burn
                // Cursed (FUN_005a2530, the hit hook): 3 in 4 on its own seed
                // (a draw's low 2 bits), Amplify Damage (skill 66) at level
                // mlvl / 5 + 1 on every foe within its aurarangecalc (1..40
                // subtiles) of the one struck (FUN_0056dbc0 round
                // FUN_0056d2c0's target; FUN_005a23d0, enemies only).
                // ponytail: melee only (the hook runs on a missile's hit too).
                if (taken.hit && (monster.boss == d2d::rules::Boss::unique || monster.boss == d2d::rules::Boss::superunique)
                    && std::ranges::contains(monster.mods, d2d::rules::umod::curse) && (monster.seed.next() & 3) != 0) {
                    const int curse_level = std::max(monster.stats.level / 5 + 1, 1);
                    const auto* amplify = game_data.skills.get(66);
                    const int radius = std::clamp(amplify ? d2d::rules::eval_calc(game_data.skills, amplify->aurarange, {}, 66, curse_level) : 1, 1, 40);
                    for (auto& near_foe : foes)
                        if (near_foe.alive && !near_foe.of && std::hypot(near_foe.x - foe.x, near_foe.y - foe.y) * 5 <= float(radius))
                            near_foe.cursed = { 66, curse_level };
                }
                foe.melee_by.push_back(monster.id);
                if (taken.damage > 0) foe.damaged_by.push_back(monster.id);
                const auto& res = type_info.diff[std::size_t(monster.difficulty)].res;
                const int thorns = foe.fighter.thorns + d2d::rules::resisted(foe.fighter.thorns_light, res[3]) + taken.damage * foe.fighter.thorns_pct / 100
                                 + taken.damage * monster.reflect_pct / 100;  // Iron Maiden
                if (taken.hit && thorns > 0 && hurt(game_data, monster, thorns, now_ms)) return true;
            }
        }
        if (monster.leaping) {                                          // Leap's flight (do 77, FUN_005d9f70): along the sequence to its landing
            const std::uint32_t lands = monster.leap_until ? monster.leap_until : monster.mode_until;
            const float span = float(std::max<std::uint32_t>(lands - unit.mode_ms, 1));
            const float done = std::clamp(float(now_ms - unit.mode_ms) / span, 0.f, 1.f);
            unit.x = monster.leap_from_x + (monster.leap_to_x - monster.leap_from_x) * done;
            unit.y = monster.leap_from_y + (monster.leap_to_y - monster.leap_from_y) * done;
        }
        if (monster.leaping && monster.leap_until && now_ms >= monster.leap_until) {   // Leap Attack down at row 11
            monster.leaping = false; monster.leap_until = 0; unit.x = monster.leap_to_x; unit.y = monster.leap_to_y;
        }
        if (now_ms < monster.mode_until) return false;
        if (monster.leaping) { monster.leaping = false; monster.leap_until = 0; unit.x = monster.leap_to_x; unit.y = monster.leap_to_y; }
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
    const float run = cells_per_sec(float(type_info.run)) * elapsed * chill * float(std::max(100 + monster.speed_pct + monster.boss_speed + pace, 10)) / 100;
    if (think(game_data, level, monster, foes, rng, now_ms, walk, run, crowd, pack, seen, open_door, born, rooms_in_play)) return false;
    // AI Idle (MonAI row 1: the cow, chickens, rats, birds, the doors and
    // barricades; search type 0, no target looked for): FUN_005b0cd0 stands
    // 200 frames when it's in a room (FUN_00620bb0), and that's all.
    if (type_info.ai_name == "Idle") {
        if (now_ms >= monster.next_act) {
            if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
            monster.next_act = now_ms + 200 * 40;
        }
        return false;
    }
    // An AI not traced finds its foe as the traced ones do (search_target),
    // at its thinks: when next_act is due and no chase is under way (a chase
    // runs as think()'s does, move_frame). With none it thinks again aidel
    // on (FUN_005a73e0).
    // The foe it found is the frame's foe from the next frame on. (No placed
    // monster comes here: see monster_update.)
    const bool chasing = monster.mode == "WL" && !monster.steps.empty() && monster.aware;
    if (now_ms >= monster.next_act && !chasing) {
        if (type_info.open_doors && on_door(level, unit) && open_door && *open_door && (*open_door)(monster, now_ms)) {   // its door first, as think's
            if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
            monster.next_act = now_ms + 5 * 40;
            return false;
        }
        const Foe* found = search_target(game_data, level, monster, foes, seen).target;
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
                    if (level.unit_blocked(next_x, next_y, unit.shape)) continue;
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
    if (level.unit_blocked(next_x, next_y, unit.shape)) return false;
    if (crowd.at(next_x, next_y, &unit) && !crowd.at(unit.x, unit.y, &unit)) return false;   // into someone: blocked; out of an overlap: fine
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

void pet_follow(const Level& level, UnitState& unit, float player_x, float player_y, float speed, std::uint32_t now_ms, float elapsed,
                 const Crowd& crowd) {
    auto& path = unit.path;
    const float dist = std::hypot(player_x - unit.x, player_y - unit.y);
    if (dist > 12) { unit.x = player_x + 1; unit.y = player_y + 1; unit.walking = false; path.clear(); return; }
    const bool moving = unit.walking ? dist > 1.5f : dist > 3;
    if (moving != unit.walking) { unit.walking = moving; unit.mode_ms = now_ms; }
    if (!moving) { path.clear(); return; }
    // Re-plan when the player has moved a cell from where it was planned.
    if (path.empty() || std::hypot(unit.goal_x - player_x, unit.goal_y - player_y) > 1.f) set_off(level, unit, player_x, player_y, true, crowd);
    const float old_x = unit.x, old_y = unit.y;
    walk_on(level, unit, speed * elapsed, crowd);
    if (std::hypot(unit.x - old_x, unit.y - old_y) > speed * elapsed * 0.3f) unit.stuck_since = 0;
    else if (!unit.stuck_since) unit.stuck_since = now_ms;
    else if (now_ms - unit.stuck_since > 1500) { unit.x = player_x + 1; unit.y = player_y + 1; unit.walking = false; unit.stuck_since = 0; path.clear(); }
}

int players_in(std::span<const Foe> foes) {
    return int(std::ranges::count_if(foes, [](const Foe& foe) { return !foe.pet && !foe.of; }));
}

Monster make_monster(const GameData& game_data, int type, float x, float y, d2d::rules::Rng& rng, int difficulty, bool stats, int players) {
    const auto& type_info = game_data.monsters.types[std::size_t(type)];
    Monster monster;
    monster.type = type;
    monster.players = players;
    monster.killable = type_info.killable;
    // FUN_005b2a00: Align 1 good (stat 0xac 2), 2 neutral (1), else evil.
    monster.align = type_info.align == 1 ? 2 : type_info.align == 2 ? 1 : 0;
    monster.npc = game_data.mon_npc[std::size_t(type)];
    for (std::size_t layer = 0; layer < 16; ++layer)
        if (!type_info.parts[layer].empty()) monster.npc.comp[layer] = type_info.parts[layer][std::size_t(rng(int(type_info.parts[layer].size())))];
    monster.unit.x = monster.home_x = x;
    monster.unit.y = monster.home_y = y;
    monster.unit.dir = rng(16);
    monster.unit.shape = monster.npc.shape;
    monster.unit.wait_until = std::uint32_t(rng(4000));
    if (stats) monster.stats = d2d::rules::monster_stats(game_data.monsters, type, difficulty, rng, 0, players);
    monster.hit_points = monster.last_hp = monster.stats.hit_points;
    monster.difficulty = std::clamp(difficulty, 0, 2);
    // Its MonProp row; spawn_monsters takes it again on the unit seed.
    // ponytail: here a min..max slot draws on the seed it has yet (no shipped
    // row with one is made this way).
    monster.props = d2d::rules::monster_props(game_data.rules, type_info, monster.difficulty, monster.seed);
    return monster;
}

// Poison hit (FUN_005a1e00): poisonmindam / maxdam (stats 57 / 58) =
// MonLvl damage x MonUMod constants rows 0x10 / 0x13 + difficulty, or
// 0x1c / 0x1f with the hook's unique flag; poisonlength (59) = (mlvl x 5 +
// 150) x 2.
bool swaps_ai(const GameData& game_data, const Monster& monster) {
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    using d2d::rules::Boss;
    return game_data.npc_timing(monster.npc, "WL").directions > 0 && !type_info.boss_column && type_info.switch_ai
        && (monster.targetable() || !monster.alive()) && monster.boss != Boss::unique && monster.boss != Boss::superunique;
}

void ai_toggle(const GameData& game_data, Monster& monster, int special) {
    d2d::rules::ai_toggle(monster.special, monster.other, { &monster.ai_state, &monster.ai_state2, &monster.ai_state3 }, special,
                          d2d::rules::has_other_think(game_data.monsters.types[std::size_t(monster.type)].ai_name));
}

void poison_hit(const GameData& game_data, Monster& monster, int difficulty, bool unique) {
    if (game_data.monsters.lvl.empty()) return;
    const auto level = std::clamp<std::size_t>(std::size_t(std::max(monster.stats.level, 1)), 1, game_data.monsters.lvl.size() - 1);
    const int difficulty_index = std::clamp(difficulty, 0, 2), damage_pct = game_data.monsters.lvl[level].damage[std::size_t(difficulty_index)];
    const int base = unique ? 28 : 16;
    monster.stats.poison_min = damage_pct * game_data.umods.constants[std::size_t(base + difficulty_index)] / 100;
    monster.stats.poison_max = damage_pct * game_data.umods.constants[std::size_t(base + 3 + difficulty_index)] / 100;
    monster.stats.poison_len = (int(level) * 5 + 150) * 2;
}

void make_boss(const GameData& game_data, Monster& monster, d2d::rules::Boss kind, const std::vector<int>& mods, int super, int name_seed,
               int difficulty, d2d::rules::Rng& rng, int players) {
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    const auto boss = d2d::rules::boss_stats(game_data.umods, type_info, kind, mods, difficulty);
    monster.stats = d2d::rules::monster_stats(game_data.monsters, monster.type, difficulty, rng, boss.level_add, players);
    monster.boss = kind;
    monster.mods = mods;
    monster.super = super;
    monster.stats.hit_points += monster.stats.hit_points * boss.hp_pct / 100;
    monster.stats.hit_points = std::max(monster.stats.hit_points + monster.stats.hit_points * boss.life_after_pct / 100, 1);
    monster.stats.armor_class += monster.stats.armor_class * boss.defense_pct / 100;
    if (boss.phys_resist >= 0) monster.boss_res[0] = boss.phys_resist - type_info.diff[std::size_t(std::clamp(difficulty, 0, 2))].res[0];
    monster.stats.exp *= boss.exp_mult;
    for (int* damage : { &monster.stats.a1_min, &monster.stats.a1_max, &monster.stats.a2_min, &monster.stats.a2_max }) *damage += *damage * boss.dmg_pct / 100;
    for (int* to_hit : { &monster.stats.to_hit, &monster.stats.to_hit2, &monster.stats.to_hit_s1 }) *to_hit += *to_hit * boss.tohit_pct / 100;   // every mode's
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
    // damage x MonUMod constants (FUN_005a00f0) rows 0x10 / 0x13 +
    // difficulty, or 0x1c / 0x1f with the hook's unique flag (a champion
    // never gets the mod).
    if (std::ranges::contains(mods, d2d::rules::umod::manahit) && !game_data.monsters.lvl.empty()) {
        const auto& level_row = game_data.monsters.lvl[std::min<std::size_t>(std::size_t(monster.stats.level), game_data.monsters.lvl.size() - 1)];
        const int damage_pct = level_row.damage[std::size_t(std::clamp(difficulty, 0, 2))], difficulty_index = std::clamp(difficulty, 0, 2);
        const int base = kind == d2d::rules::Boss::minion ? 16 : 28;
        monster.mana_lo = damage_pct * game_data.umods.constants[std::size_t(base + difficulty_index)] / 100;
        monster.mana_hi = std::max(damage_pct * game_data.umods.constants[std::size_t(base + 3 + difficulty_index)] / 100, monster.mana_lo);
    }
    if (std::ranges::contains(mods, d2d::rules::umod::poisonhit)) poison_hit(game_data, monster, difficulty, kind != d2d::rules::Boss::minion);
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
        const std::pair<std::string, std::string> words[] = { { "%0", string_id(game_data, d2d::rules::champion_word(mods)) },
                                                              { "%1", monster.npc.name } };
        for (const auto& [placeholder, value] : words)
            if (const auto found = format.find(placeholder); found != std::string::npos) format.replace(found, placeholder.size(), value);
        monster.npc.name = std::move(format);
    }
}

int soak(Monster& monster, int physical, int damage) {
    if (monster.armor_pool <= 0 || physical <= 0 || damage <= 0) return damage;
    const int absorbed = std::min({ monster.armor_pool, physical, damage });
    monster.armor_pool -= absorbed;
    if (monster.armor_pool <= 0) monster.armored = false;
    return damage - absorbed;
}

bool hurt(const GameData& game_data, Monster& monster, int damage, std::uint32_t now_ms, int fraction) {
    if (!monster.targetable() || (damage <= 0 && fraction <= 0)) return false;
    // Life is in 256ths in game.exe (0057c852): carry the part of a point,
    // and a life under one point after a hit is dead (0057c865).
    monster.life_frac += std::max(fraction, 0);
    damage += monster.life_frac >> 8;
    monster.life_frac &= 255;
    monster.hit_points -= damage;
    if (monster.hit_points < 1 || (monster.hit_points == 1 && monster.life_frac > 0)) { monster.hit_points = 0; monster.life_frac = 0; }   // dead is 0 (0057c86c)
    monster.aware = true;
    if (!monster.alive()) set_mode(game_data, monster, "DT", now_ms);
    else if (damage * 8 >= monster.stats.hit_points) set_mode(game_data, monster, "GH", now_ms);
    return !monster.alive();
}

void heal(Monster& monster, int points) {
    if (points <= 0 || !monster.alive()) return;
    const int healed = monster.hit_points + points;
    if (healed > monster.stats.hit_points) { monster.hit_points = monster.stats.hit_points; monster.life_frac = 0; }
    else monster.hit_points = healed;
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
        if (level.unit_blocked(next_x, next_y, unit.shape) || crowd.at(next_x, next_y, &unit)) { path.clear(); break; }
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
