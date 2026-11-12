// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for ai.hpp: monsters, NPCs and the merc: spawning, paths, the AI step.
#include "ai.hpp"

#include "ai/internal.hpp"
#include "game.hpp"
#include "gamedata.hpp"
#include "log.hpp"

#include <combat.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <merc.hpp>
#include <missiles.hpp>
#include <mon_equip.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>
#include <pets.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <thinks.hpp>
#include <town_npcs.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ranges>
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
    // A point given twice (the toward pather's cut-short subtile) stays: follow_path spends a frame on it (FUN_00650660).
    std::vector<std::pair<float, float>> points;
    for (const auto& step : steps) points.emplace_back(centre(step.first), centre(step.second));
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
    for (const auto& step : steps) points.emplace_back(centre(step.first), centre(step.second));
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
    bool blocked = false;
    if (follow_path(level, unit, step, crowd, &blocked)) return true;
    if (blocked) return false;
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
                                    d2d::rules::Rng& rng, int difficulty, int players, int level_id) {
    std::vector<Monster> out;
    for (const auto& spawn : spawns) {
        if (spawn.type < 0 || std::size_t(spawn.type) >= game_data.mon_npc.size()) continue;
        const bool boss = spawn.boss != d2d::rules::Boss::none;
        auto monster = make_monster(game_data, spawn.type, (float(spawn.x) + 0.5f) / 5, (float(spawn.y) + 0.5f) / 5, rng, difficulty, !boss, players, level_id);
        const auto& type_info = game_data.monsters.types[std::size_t(spawn.type)];
        const std::vector<d2d::rules::Components>* sets = nullptr;
        for (std::size_t i = 0; i < region.types.size() && i < region.components.size(); ++i)
            if (region.types[i].first == spawn.type) sets = &region.components[i];
        monster.cage = spawn.cage;
        monster.listed = spawn.cage < 0;                              // a prisoner is in no target list (Monster::listed)
        d2d::rules::Rng unit_seed{ spawn.seed };
        const auto look = d2d::rules::monster_look(sets, type_info.choices, unit_seed);
        monster.variant = d2d::rules::monster_variant(spawn.type, unit_seed);
        monster.props = d2d::rules::monster_props(game_data.rules, type_info, difficulty, unit_seed);   // FUN_00573cb0's end, after the look
        for (std::size_t layer = 0; layer < 16; ++layer)
            if (look[layer] < type_info.parts[layer].size()) monster.npc.comp[layer] = type_info.parts[layer][look[layer]];
        const auto gear = spawn_gear(game_data, spawn.gear, difficulty);
        if (!boss && !spawn.gear.empty()) wear_gear(monster.stats, gear);
        if (boss) make_boss(game_data, monster, spawn.boss, spawn.mods, spawn.super, spawn.name_seed, difficulty, rng, players, spawn.gear.empty() ? nullptr : &gear);
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
    // FUN_005a68e0: a last command 0x13 or over loses 0x10 and the new one is dropped: a hit's 0x13
    // is 3 (GH), so it reads as got hit through one more mode.
    if (monster.left_mode == "hit") monster.left_mode = "GH";
    else if (monster.mode != "NU") monster.left_mode = monster.mode;
    monster.mode = mode;
    if (mode == "A1" || mode == "A2" || mode == "SC" || mode == "S1" || mode == "S2" || mode == "S3" || mode == "S4") {
        monster.swing_mode = mode;
        // FUN_005a4f50 at 0x5a5223: each element of the mode with a chance
        // comes in under 100 % by a roll on the unit's seed; a rand one rolls
        // its kind (rand(5) + 1, 0x5a52f1), kept for swing_stats.
        for (std::size_t element = 0; element < monster.stats.elements.size(); ++element) {
            const auto& info = monster.stats.elements[element];
            monster.element_on[element] = info.type != -1 && info.mode == mode && info.pct > 0 && (info.pct > 99 || monster.seed(100) < info.pct);
            if (monster.element_on[element] && info.type == -3) monster.rand_kind[element] = monster.seed(5) + 1;
        }
    }
    monster.seq_frame = -1;                                   // a sequence's row: drawn again as it plays
    monster.unit.mode_ms = now_ms;
    monster.unit.walking = mode == "WL" || mode == "RN";
    // A monster's FHR is its MonProp's (props.fhr): FUN_00625500 reads the
    // unit's full stat 99, but no Skills.txt aurastat / passivestat gives it.
    monster.mode_until = mode == "NU" || mode == "WL" || mode == "DD" ? 0
        : mode == "GH"                                                ? now_ms + get_hit_ms(game_data.npc_timing(monster.npc, mode), monster.props.fhr)
                                                                      : now_ms + game_data.npc_timing(monster.npc, mode).length_ms() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, mode));
}

int mode_end_event(const GameData& game_data, const Monster& monster, std::string_view mode, int frame) {
    return frame + int(game_data.npc_timing(monster.npc, mode).length_ms() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, mode)) / 40) + 1;
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

void file_first_think(UnitState& state, std::uint32_t now_ms) {
    constexpr std::uint32_t kFrameMs = 40;
    state.filed = true;
    state.wait_until = now_ms + 2 * kFrameMs;                 // NU: event 2 at frame + 2 (FUN_00573780, 0x5737c0)
}

void npc_patrol(const GameData& game_data, const Level& level, std::vector<UnitState>& npcs, std::array<int, 3> busy,
                std::uint32_t now_ms, float elapsed, const Crowd& crowd, std::span<const UnitState* const> visitors, bool guard_aside) {
    constexpr std::uint32_t kFrameMs = 40;
    for (std::size_t i = 0; i < npcs.size() && i < level.npcs.size(); ++i) {
        const auto& npc = level.npcs[i];
        if (npc.town_ai == TownAi::none || ((npc.town_ai == TownAi::npc || npc.town_ai == TownAi::towner) && npc.path.empty())) continue;
        auto& state = npcs[i];
        if (!state.filed) file_first_think(state, now_ms);
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
        if (const UnitState* player = i < visitors.size() ? visitors[i] : nullptr) {   // FUN_005dde80: within 16, with a "!" for them
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
    if (room_of(level, (float(x) + 0.5f) / 5, (float(y) + 0.5f) / 5) < 0) return false;
    for (const auto& [off_x, off_y] : { std::pair{ 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } }) {
        const float at_x = (float(x + off_x) + 0.5f) / 5, at_y = (float(y + off_y) + 0.5f) / 5;
        if (level.blocked(at_x, at_y, mask & 0x0fff) || ((mask & 0x1000) && crowd.at(at_x, at_y, nullptr))) return false;
    }
    return true;
}


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

// A move's path to subtile (to_x, to_y) (FUN_00649970) by its path type:
// rules::monster_path, the toward pather then the wall pather (FUN_00650350,
// FUN_005a6290); type 7 rules::player_path at its own 5 steps, then the
// wall pather (type 0xf from then on). Leading points on its own subtile
// are dropped (FUN_0064fe40).
// FUN_006483a0 refuses a monster that isn't npc / inTown / a pet a path
// whose end room is in town; a thinking monster here is never in a town
// level, and its path stays in its level, so none is refused.
// ponytail: path flag 0x1000 (the end tested with FUN_0064d910, then
// FUN_00648120 walks it back) left out: its setter isn't found.
bool path_to(const Level& level, Monster& monster, int to_x, int to_y, bool at_foe, const Crowd& crowd) {
    auto& unit = monster.unit;
    auto centre = [](int coord) { return (float(coord) + 0.5f) / 5; };
    monster.step = 0;
    monster.end_x = to_x; monster.end_y = to_y;
    const int x = subtile(unit.x), y = subtile(unit.y);
    const auto blocked = [&](int at_x, int at_y) { return level.unit_blocked(centre(at_x), centre(at_y), unit.shape) || crowd.at(centre(at_x), centre(at_y), &unit); };
    if (monster.path_type == 0) {
        monster.steps = d2d::rules::monster_path(x, y, to_x, to_y, at_foe, blocked);
        return !monster.steps.empty();
    }
    if (monster.path_type == 7) monster.steps = d2d::rules::player_path(x, y, to_x, to_y, at_foe ? 1 : 0, at_foe, blocked, 5);
    if (monster.path_type == 7 && monster.steps.empty()) monster.path_type = 0xf;
    if (monster.path_type == 0xf) monster.steps = d2d::rules::wall_path(x, y, to_x, to_y, 5, at_foe, blocked);
    monster.steps.erase(monster.steps.begin(), std::ranges::find_if(monster.steps, [&](const std::pair<int, int>& point) { return point != std::pair(x, y); }));
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

}  // namespace

// One of Andariel's skill missiles (FUN_0059fa30, flags 0x20): from her
// toward (dx, dy) cells, its Missiles.txt row's damage at skill level `lvl`
// (rules::monster_skill_level): physical in 256ths (SrcDamage 0: none of hers), its
// poison per frame over ELen as the total, another element in 256ths.
// ToHit 0: it always hits. A Fallen Shaman's fire bolts too. A row naming a
// Skill (`carried`: the Summoner's Fire Ball, Glacial Spike, Frost Nova)
// carries that skill's element at the level instead (FUN_0064b860), and its
// physical in place of the row's (FUN_00647bc0 / FUN_00647d00: skill_phys).
void andariel_missile(const GameData& game_data, const Monster& monster, const std::string& name, float dx, float dy, std::uint32_t now_ms,
                      std::vector<Missile>& missiles, int lvl, const d2d::rules::Skill* carried) {
    const auto found = game_data.missiles.find(name);
    if (found == game_data.missiles.end()) return;
    const auto& missile_info = found->second;
    const auto poison = carried && !missile_info.skill.empty()
        ? d2d::rules::row_damage(carried->etype, carried->emin, carried->emax, carried->emin_lev, carried->emax_lev, carried->hitshift, carried->elen, carried->elen_lev, lvl)
        : d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev,
                                 missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
    auto stats = missile_blow(missile_info, poison, monster.stats.level);
    if (carried && !missile_info.skill.empty()) {
        stats.a2_min = d2d::rules::skill_phys(game_data.skills, *carried, {}, lvl, false) >> 8;
        stats.a2_max = std::max(d2d::rules::skill_phys(game_data.skills, *carried, {}, lvl, true) >> 8, stats.a2_min);
    }
    stats.to_hit = monster.stats.to_hit;                              // its stat 0x13, rolled when the row has ToHit
    const float speed = missile_speed(missile_info, lvl), distance = std::max(std::hypot(dx, dy), 0.01f);
    missiles.push_back({ &missile_info, monster.unit.x, monster.unit.y, dx / distance * speed, dy / distance * speed, missile_facing(monster.unit.x, monster.unit.y, dx, dy), now_ms,
                         now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, stats });
    missiles.back().level = lvl; missiles.back().to = { monster.unit.x + dx, monster.unit.y + dy }; missiles.back().owner = monster.id;
    if (carried) missiles.back().skill = carried->id;
}

void morph_monster(const GameData& game_data, Monster& unit, int type, d2d::rules::Rng& rng, int players) {
    auto turned = make_monster(game_data, type, unit.unit.x, unit.unit.y, rng, unit.difficulty, true, players, unit.level_id);
    turned.id = unit.id; turned.leader = unit.leader; turned.seed = unit.seed; turned.classic = unit.classic;
    turned.home_x = unit.home_x; turned.home_y = unit.home_y;
    unit = std::move(turned);
}

std::optional<Monster> vile_effigy(const GameData& game_data, const Level& level, Monster& baal, int at_x, int at_y, d2d::rules::Rng& rng, int players) {
    const auto made = game_data.monsters.by_id.find("baalclone");
    if (made == game_data.monsters.by_id.end()) return std::nullopt;
    const int clone_x = at_x + int(baal.seed.next() % 24) - 12, clone_y = at_y + int(baal.seed.next() % 24) - 12;
    auto clone = make_monster(game_data, made->second, (float(clone_x) + 0.5f) / 5, (float(clone_y) + 0.5f) / 5, rng, baal.difficulty, true, players, level.id);
    std::tie(clone.unit.x, clone.unit.y) = level.nearest_free(clone.unit.x, clone.unit.y);
    clone.stats.hit_points = std::max(baal.stats.hit_points / 3, 1);
    clone.hit_points = std::max(baal.hit_points / 3, 1);
    clone.stats.regen = 0;
    clone.aware = true;
    clone.no_exp = clone.no_drop = true;                           // unit flags |= 0x4020000 (005fca71)
    clone.seed = d2d::rules::Rng{ rng.next() };
    return clone;
}

void missile_at_target(const GameData& game_data, const Monster& monster, const std::string& name, float dx, float dy, std::uint32_t now_ms,
                       std::vector<Missile>& missiles, int lvl, const d2d::rules::Skill* carried) {
    const std::size_t before = missiles.size();
    andariel_missile(game_data, monster, name, 0.2f, 0.2f, now_ms, missiles, lvl, carried);
    if (missiles.size() == before) return;
    auto& centre = missiles.back();
    centre.x += dx; centre.y += dy;
    centre.to = std::pair{ centre.x + 0.2f, centre.y + 0.2f };
}

// A Skills.txt row by name, -1 none.
int skill_id(const GameData& game_data, const std::string& name) {
    const auto found = game_data.skills.by_name.find(name);
    return name.empty() || found == game_data.skills.by_name.end() ? -1 : found->second;
}

namespace {

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
// a war cry (68: Madawc's Shout). Any other stands, and no think casts one:
// of every traced AI's MonStats Skill1..8 not in a MonSeq sequence, with no
// srvmissile and none of the above, the rest are BaalThrone's Corpse Explode
// (its own path), the wisps' Chain Lightning (they fire MissC), FetishAura
// and Duriel's Holy Freeze (auras), Self-resurrect (at death) and
// Diablogeddon (the unplaced clone's; UberDiablo's think is `ret 4`).
bool castable(const d2d::rules::Skill& skill) {
    using enum d2d::rules::ServerDoFunction;
    return !skill.srvmissile.empty() || std::ranges::contains(std::array{ kNova, kMissileWall, kCurse, kScatteredBolts, kMissileAtTarget, kMonsterBreath, kBestow,
                                                                          kMonTeleport, kMonPoisonNova, kHydra, kDiabLight, kDiabCold, kFingerMageSpider, kDoomKnightMissile,
                                                                          kGuidedMissile, kSelfState, kRegurgitatorEat, kMonCurseCast, kDiabPrison, kDiabWall,
                                                                          kBaalColdMissiles, kBaalTentacle, kBaalCorpseExplode, kImpInferno, kImpTeleport, kImpFireMissile,
                                                                          kCryHelp, kOverseerWhip, kImpregnate, kSiegeBeastStomp, kWarCry, kCorpseExplosion, kMinionSpawner }, skill.srvdofunc);
}
}  // namespace

// A monster's skill strike (Whirlwind's do 76, Leap Attack's FUN_005da660):
// its A1 swing with calc1 % more damage, the skill's to-hit and its element
// (FUN_0056e0c0) at the level. The to-hit (FUN_006449f0: ToHitCalc, else
// ToHit + LevToHit a level) is added to the rating as points: FUN_0057d9b0's
// monster branch (0x57db93) rolls stat 19 + it + dex x 5, then stat 119's %.
d2d::rules::MonStats skill_strike(const GameData& game_data, const Monster& monster, const d2d::rules::Skill& skill, int lvl) {
    auto stats = monster.swing_stats();
    const int more = d2d::rules::eval_calc(game_data.skills, skill.calc[0], {}, skill.id, lvl);
    for (int* damage : { &stats.a1_min, &stats.a1_max }) *damage = std::max(*damage * (100 + more + monster.dmg_pct) / 100, 0);
    stats.to_hit += d2d::rules::skill_tohit(game_data.skills, skill, {}, lvl);
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

namespace {

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
// A dead pet is skipped and a dead monster leaves list 9, as game.exe
// drops them from those lists.
// Mode 5's "in the searcher's area" compares area ids (FUN_0061b130,
// area_id_at), which carry across rooms; the flag (+0x24) is the record's.
int area_at(const Level& level, float x, float y) {
    const int room = room_of(level, x, y);
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
    const int room = room_of(level, unit.x, unit.y);
    if (monster.area == -2) monster.area = area_at(level, monster.home_x, monster.home_y);   // FUN_005b2a00 -> FUN_00552d60: where it was placed
    if (monster.area >= 0 && (monster.area >> 8 & 0xfff) != room) monster.area = -1;
    const auto* in_room = room >= 0 && std::size_t(room) < level.rooms.size() ? &level.rooms[std::size_t(room)] : nullptr;   // the camp has none (room_of: its 8x8 rooms)
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
    // FUN_005dd0b0 mode 5 over the near rooms in order, each room's newest
    // first (as mode 6's): the order picks between ties and says when a
    // waking friend lifts the sight test. The pick's indexes are foes'.
    const auto mode5 = [&](int align) {
        std::vector<int> rooms;
        for (const auto& [room_level, near_room] : near_rooms(game_data, level, unit.x, unit.y))
            if (room_level == &level) rooms.push_back(near_room);
        std::vector<d2d::rules::RoomUnit> units;
        units.reserve(foes.size());
        for (const auto& foe : foes) units.push_back({ room_of(level, foe.x, foe.y), true });
        const auto near_of = d2d::rules::room_order(units, rooms);
        std::vector<d2d::rules::NearFoe> nears;
        nears.reserve(near_of.size());
        for (const int index : near_of) {
            const auto& foe = foes[std::size_t(index)];
            auto& entry = nears.emplace_back(d2d::rules::NearFoe{ nearby(foe), foe.of ? foe.of->align : 2, foe.threat, foe.of == &monster, foe.of || foe.pet, !foe.alive, town });
            entry.blocked = need_sight && entry.distance <= 0x23 && d2d::rules::sight_blocked(subtile(foe.x), subtile(foe.y), foe.size, x, y, type_info.size, wall);
            entry.waking = foe.of && foe.alive && foe.of->sighted && area_id_at(game_data, level, foe.x, foe.y, foe.x, foe.y) == here;
        }
        auto pick = d2d::rules::search_near(nears, align, need_sight);
        if (pick.target >= 0) pick.target = near_of[std::size_t(pick.target)];
        if (pick.second >= 0) pick.second = near_of[std::size_t(pick.second)];
        return pick;
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
            const bool converted = foe.of && foe.of->align == 2 && foe.of->listed && foe.alive;
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
    monster.skill = skill;                        // before set_mode: its length reads the skill's UseAttackRate
    set_mode(game_data, monster, mode, now_ms);
    monster.struck = false; monster.wandering = false; monster.seq_done = 0; monster.seq_once = false;
    monster.skill_frame = 0; monster.skill_x = subtile(foe.x); monster.skill_y = subtile(foe.y);
    attack_starts(game_data, monster, mode, rng);
}

// Teleportation (mod 26: MonTeleport, AI flag 0x20; FUN_005b11f0, the
// driver's step 3, FUN_005b13e0, with a target): under 40 on its seed
// (% 100), then - under 30 % life, or not isMelee (MonStats flag 1; BaseIds
// 10, 0x159, 0x22d count as melee) with the search's distance (AI control
// +0x14) under 10 - rand(100) < 15 on its seed: a spot in its room
// (FUN_0054dc40: 20 tries, each rand(width) / rand(height) inside the room's
// subtiles less its first row and column, on the room's seed, then
// FUN_005b3040 there at radius -1). Found: under 30 % life, rand(100) < 25
// heals its level in life; then it casts MonTeleport (0xb8) at the spot
// (FUN_005dead0 command 4) and true. No heal under state 0x34
// (Prevent Monster Heal), its roll still drawn.
bool teleport_mod(const GameData& game_data, const Level& level, Monster& monster, int distance, const Crowd& crowd, Spawning::LevelState* rooms_in_play, std::uint32_t now_ms) {
    if ((monster.boss != d2d::rules::Boss::unique && monster.boss != d2d::rules::Boss::superunique) || !std::ranges::contains(monster.mods, d2d::rules::umod::teleport)
        || monster.seed.next() % 100 >= 40) return false;
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    const bool low = std::int64_t(monster.hit_points) * 100 < std::int64_t(monster.stats.hit_points) * 30;
    const bool melee = type_info.melee || type_info.base == 10 || type_info.base == 0x159 || type_info.base == 0x22d;
    if (!low && (melee || distance >= 10)) return false;
    if (monster.seed(100) >= 15) return false;
    const int teleport = skill_id(game_data, "MonTeleport");
    const int room = room_of(level, monster.unit.x, monster.unit.y);
    const auto rect = room < 0 ? std::nullopt : level_room(level, std::size_t(room));
    if (teleport < 0 || !rect || !rooms_in_play || std::size_t(room) >= rooms_in_play->room_seeds.size()) return false;
    const int left = rect->x * 5 + 1, top = rect->y * 5 + 1, width = (rect->x + rect->width) * 5 - left, height = (rect->y + rect->height) * 5 - top;
    for (int tries = 0; tries < 20; ++tries) {
        auto& room_seed = rooms_in_play->room_seeds[std::size_t(room)];
        const int try_x = room_seed(width) + left, try_y = room_seed(height) + top;
        int at_x = 0, at_y = 0;
        if (!spawn_spot(game_data, level, crowd, rooms_in_play, monster.type, try_x, try_y, -1, room, at_x, at_y)) continue;
        if (low && monster.seed(100) < 25 && now_ms >= monster.no_heal_until) heal(monster, monster.stats.level);
        monster.skill = teleport;
        monster.skill_sound = teleport;
        monster.skill_x = at_x; monster.skill_y = at_y; monster.at_point = true;
        monster.unit.walking = false;
        set_mode(game_data, monster, "S1", now_ms);                              // MonTeleport's monanim
        monster.struck = false;
        return true;
    }
    return false;
}

}  // namespace

auto ThinkCtx::idle(int frames) -> void {                           // FUN_005de080
    if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
    monster.wandering = false;
    monster.next_act = now_ms + std::uint32_t(std::max(frames, 1)) * 40;
}

// A move sets off (FUN_005a7c20) with a re-path budget of 0x14
// (FUN_006490e0) and its path (path_to), at a foe or to unit.goal (at a
// unit there: `to_unit`); one with no path can't start, and a spot move
// then thinks aidel on (FUN_005a73e0).
auto ThinkCtx::set_off(bool running, const Foe* foe, bool to_unit) -> bool {
    monster.budget = 0x14;
    monster.path_type = std::exchange(monster.next_path_type, 0);
    monster.wandering = !foe;
    if (!path_to(level, monster, foe ? subtile(foe->x) : subtile(unit.goal_x), foe ? subtile(foe->y) : subtile(unit.goal_y), foe || to_unit, crowd)) {
        if (!foe && !to_unit) idle(per_difficulty.aidel ? per_difficulty.aidel : 15);
        return false;
    }
    const std::string_view mode = running && has_run ? "RN" : "WL";
    if (monster.mode != mode) set_mode(game_data, monster, mode, now_ms);
    return true;
}

auto ThinkCtx::walk_to(int off_x, int off_y, bool running) -> void {   // to a spot, subtiles off
    unit.goal_x = (float(x + off_x) + 0.5f) / 5; unit.goal_y = (float(y + off_y) + 0.5f) / 5;
    set_off(running, nullptr);
}

auto ThinkCtx::use(std::string_view mode, int skill) -> void {     // FUN_005dead0 / FUN_005ddf90: against the target (mode ctx +8)
    monster.chase = targeted ? int(target - foes.data()) : -1;
    unit.dir = direction16(dx, dy);
    monster.skill = skill;                        // before set_mode: its length reads the skill's UseAttackRate
    monster.skill_sound = skill;
    set_mode(game_data, monster, mode, now_ms);
    monster.struck = false; monster.wandering = false; monster.seq_done = 0; monster.seq_once = false;
    monster.skill_frame = 0; monster.skill_x = target_x; monster.skill_y = target_y; monster.at_point = false;
    attack_starts(game_data, monster, mode, rng);
    // srvst 59 (FUN_005d1280, Imp Inferno): its end calc1 frames from the start (the roll on its seed now), state 0xc on.
    if (const auto* fire = game_data.skills.get(skill); fire && fire->srvstfunc == d2d::rules::ServerStartFunction::kImpInferno) {
        d2d::rules::CalcEnv env;
        env.rng = &monster.seed;
        monster.breath_until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data.skills, fire->calc[0], env, skill, skill_level(game_data, monster, skill)), 1)) * 40;
        monster.breath_state = true;
    }
}

namespace {

// The AI's row (ai/), null for none.
const AiRow* ai_row(std::string_view ai_name) {
    for (const auto table : { kAct1Ais, kAct2Ais, kAct3Ais, kAct4Ais, kAct5Ais })
        if (const auto row = std::ranges::find(table, ai_name, &AiRow::ai_name); row != table.end()) return &*row;
    return nullptr;
}

// A traced MonAI's frame outside an attack (the driver FUN_005b1740:
// target search FUN_005de890 / FUN_005dd7f0, then the AI's think,
// rules::mon_think, and its row's parts: ai/). A move goes on between thinks — at a foe (a walk or
// run at it) or to a spot (a wander, back-off, keep-off, circle). At a
// think it takes search_target's foe; with none it stands (10 frames,
// the nearest - 10 from 25 subtiles off, 25 from 35), or wanders 5 when
// hit. In melee is unit_distance within
// MeleeRng + 1. A walk with flags 2 that can't set off: 70 % a random
// walk of 4 subtiles (FUN_005de200), else stand 10. Returns false for an
// AI whose think isn't traced. A special AI (the Countess's) thinks in
// place of its MonAI's (FUN_005b15d0). Thinks draw the unit's own seed.
// ponytail: the seed as its look left it (FUN_00573cb0's draws and the
// rest between aren't taken; the World's spawns (world/) keep the default);
// FUN_0064d910's 0x40 test on
// the one subtile under it, not its collision pattern; in melee
// skips the path test (FUN_00622aa0, mask 0x804); the path types a think
// gives its next move (FUN_005de190's EDX: 48 of its 103 calls, 1 / 2 /
// 7 / 0xd / 0xf) taken only for the Dark Wanderer's (Monster::path_type),
// the rest as path_to's; a circle as a walk to the point n
// subtiles to the side of the target; a back-off sets off when its end
// and first step are open.
bool think(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, d2d::rules::Rng& rng, std::uint32_t now_ms,
           float walk, float run, const Crowd& crowd, std::span<Monster> pack, AreaSeen* seen, const OpenDoor* open_door, std::vector<Monster>* born,
           Spawning::LevelState* rooms_in_play) {
    auto& unit = monster.unit;
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    const auto& per_difficulty = type_info.diff[std::size_t(monster.difficulty)];
    const bool cursed = monster.special == 10 || monster.special == 11;   // the curse thinks run whatever its own
    if (!cursed && !d2d::rules::traced_ai(type_info.ai_name)) return false;
    const AiRow* row = ai_row(type_info.ai_name);
    const bool has_run = game_data.npc_timing(monster.npc, "RN").directions > 0;   // RN falls back to WL
    // The think's state, which each AI's parts (ai/) see through ctx.
    int x = 0, y = 0, best = 0, nearest = 0x7fffffff, target_x = 0, target_y = 0;
    float dx = 0, dy = 0;
    Foe* target = nullptr;
    Foe* second = nullptr;
    bool targeted = false;
    d2d::rules::ThinkIn input;
    d2d::rules::Think act;
    int corpse = -1, kin = -1, aid = -1, imp_found = -1, lay_x = 0, lay_y = 0;
    std::vector<int> wisp_finds;
    const auto self = pack.empty() ? pack.size() : std::size_t(&monster - pack.data());
    const int frame = int(now_ms / 40);
    ThinkCtx ctx{ game_data, level, monster, type_info, per_difficulty, foes, pack, crowd, rooms_in_play, now_ms, rng, has_run, frame, self, unit, input,
                  x, y, target, best, targeted, target_x, target_y, dx, dy, act, second, corpse, kin, aid, imp_found, lay_x, lay_y, wisp_finds };
    // A move runs to its end (move_frame), then it thinks: a walk or run's
    // end does so at once (FUN_005a8030: DAT_0073c6d0 flags WL and RN),
    // whatever think is pending (that one still comes).
    bool move_ended = false;
    if (monster.mode == "WL" || monster.mode == "RN") {
        const Foe* chased = monster.chase >= 0 && std::size_t(monster.chase) < foes.size() && foes[std::size_t(monster.chase)].alive ? &foes[std::size_t(monster.chase)] : nullptr;
        if (move_frame(level, monster, monster.wandering ? nullptr : chased, monster.wandering, type_info.size, monster.mode == "RN" ? run : walk, crowd)) return true;
        set_mode(game_data, monster, "NU", now_ms);
        monster.wandering = false;
        move_ended = true;
        // A charge's end (srvdofunc 67, FUN_005cf900): at its target it
        // swings with the skill (charge_swing).
        if (const int with = std::exchange(monster.charging, -1); with >= 0 && chased && std::hypot(chased->x - unit.x, chased->y - unit.y) <= kMeleeReach + 0.3f) {
            charge_swing(game_data, monster, *chased, with, now_ms, rng);
            return true;
        }
    }
    if (now_ms < monster.next_act && !move_ended) return true;
    monster.thought_ms = now_ms;
    // The driver's first step (FUN_005b10e0 → FUN_005b0f50): with opendoors
    // (MonStats flags +0xc & 8) and the monster bit 0x800 under it
    // (FUN_00648eb0), its door (OpenDoor) operated, it stands 5.
    // ponytail: the word read now, not a moving path's cached +0x54.
    if (type_info.open_doors && on_door(level, unit) && open_door && *open_door && (*open_door)(monster, now_ms)) { ctx.idle(5); return true; }
    x = subtile(unit.x); y = subtile(unit.y);
    const auto searched = search_target(game_data, level, monster, foes, seen);
    target = searched.target; best = searched.best; nearest = searched.nearest;
    monster.aware = target != nullptr;
    if (row && row->search) row->search(ctx);                    // its own pick (game.exe's row +0)
    // A think the game runs itself (the prisoners', Baal's throne and stairs).
    if (row && row->think) return row->think(ctx);
    // Search types 2 and 5 (FUN_005b1650 cases 2 and 5; the MonAI table
    // 0x73ca18 + ai x 16): the think runs with no target too, type 5's
    // (FrogDemon) after its miss, on a stand-in here (its own spot, not
    // alive) and ThinkIn::target false. FUN_005dd7f0's miss hands it the
    // nearest player's distance (local_34: 0x7fffffff with none, or for a
    // searcher not evil) and not in melee.
    Foe alone;
    alone.x = unit.x; alone.y = unit.y; alone.alive = false;
    if (!target) {
        monster.chase = -1;
        if (!row || !row->thinks_alone) {
            const bool has_walk = game_data.npc_timing(monster.npc, "WL").directions > 0;
            const auto miss = d2d::rules::search_missed(type_info.ai_name, has_walk, monster.got_hit(), level.blocked(unit.x, unit.y, 0x40), nearest);
            if (miss.wander) {
                const auto wander = d2d::rules::think_wander(monster.seed, 5);
                ctx.walk_to(wander.x, wander.y, false);
            } else if (miss.mode_kept) monster.next_act = now_ms + std::uint32_t(miss.stand) * 40;
            else ctx.idle(miss.stand);
            if (!row || !row->thinks_after_miss) return true;
        }
        target = &alone;
        best = nearest;
    }
    targeted = target != &alone;
    const auto chase_of = [&] { return targeted ? int(target - foes.data()) : -1; };
    // The driver's step 3 (FUN_005b13e0). First sight (FUN_005b1140): a
    // champion, unique or superunique (data +0x16 & 8), a MonStats boss
    // (FUN_0063e9f0) or the Summoner (class 0xfa), its target a player under
    // 20 off (the search's distance), AI flag 0x10 unset: unit event 0x10
    // (FUN_00553380: the client plays its Taunt), the flag, stand 20.
    using d2d::rules::Boss;
    const bool special = (monster.boss != Boss::none && monster.boss != Boss::minion) || type_info.boss_column
                      || monster.type == int(game_data.mon_bin[0xfa]);
    if (special && targeted && best < 0x14 && !target->pet && !target->of && !monster.first_sight) {
        monster.first_sight = monster.taunt_sound = true;
        ctx.idle(0x14);
        return true;
    }
    if (targeted && teleport_mod(game_data, level, monster, best, crowd, rooms_in_play, now_ms)) return true;
    // The leash: an isMelee monster (FUN_00457490 1) with a WL mode
    // (FUN_0046c140 2), its level's MonSpcWalk (+0x2f) set and its target
    // farther with no clear line (FUN_005dc640), takes the nearest other
    // live enemy (FUN_00554200) under 49 (FUN_005dc380), threat over 1, in
    // a clear line (FUN_005dd0b0 mode 0xb: 0x5dccc0); none, it wanders 4.
    const int spc_walk = std::size_t(level.id) < game_data.level_mon.size() ? game_data.level_mon[std::size_t(level.id)].spc_walk : 0;
    const auto wall805 = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x805); };
    const auto line_clear = [&](const Foe& foe) { return d2d::rules::pet_line_clear(x, y, type_info.size, subtile(foe.x), subtile(foe.y), wall805); };
    if (targeted && type_info.melee && game_data.npc_timing(monster.npc, "WL").directions > 0 && spc_walk > 0 && best > spc_walk && !line_clear(*target)) {
        Foe* leashed = nullptr;
        int gap = 0x31;
        for (auto& foe : foes) {
            if (&foe == target || !foe.alive || foe.of == &monster || foe.threat <= 1 || d2d::rules::friends(monster.align, foe.of ? foe.of->align : 2)) continue;
            const int foe_gap = d2d::rules::merc_gap(x, y, type_info.size, subtile(foe.x), subtile(foe.y));
            if (foe_gap < gap && line_clear(foe)) { leashed = &foe; gap = foe_gap; }
        }
        if (!leashed) {
            const auto wander = d2d::rules::think_wander(monster.seed, 4);
            ctx.walk_to(wander.x, wander.y, false);
            return true;
        }
        target = leashed;
        best = gap;
    }
    if (row && row->think_targeted) return row->think_targeted(ctx);   // the Dark Wanderer's
    target_x = subtile(target->x); target_y = subtile(target->y);
    dx = target->x - unit.x; dy = target->y - unit.y;
    input = d2d::rules::ThinkIn{ .aip = per_difficulty.aip, .dist = best, .difficulty = monster.difficulty, .level = level.id, .state = &monster.ai_state };
    input.target = targeted;
    input.self_x = x; input.self_y = y;
    input.in_melee = targeted && d2d::rules::unit_distance(target_x - x, target_y - y, type_info.size, target->size) <= type_info.melee_rng + 1;
    input.got_hit = monster.got_hit();
    input.life_pct = int(std::int64_t(monster.hit_points) * 100 / std::max(monster.stats.hit_points, 1));   // FUN_00621f20
    for (std::size_t skill = 0; skill < 3; ++skill) input.skill[skill] = !type_info.skill[skill].empty();
    // A unit in DT within 15 (FUN_005dc530: the Fallen's), its lead.
    bool rally = false;
    input.command = &monster.ai_command; input.rally = &rally;
    input.leader = !pack.empty() && monster.leader == int(self);
    for (std::size_t i = 0; i < pack.size(); ++i) {
        const auto& other = pack[i];
        if (i != self && other.mode == "DT" && d2d::rules::ai_distance(subtile(other.unit.x) - x, subtile(other.unit.y) - y) < 15) input.dying = true;
    }
    // The frame (a nest's init takes it at its first think), the spot its
    // young come out on spawnx / spawny off (FUN_005fd350 tests crownest's only).
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
    // 0x2b); the second target (FUN_005ddc30, below); Duriel's aura on.
    // FUN_005fd470, a skill usable, where a think asks it (else 1): ai/.
    input.regen = monster.stats.regen;
    input.skill45 = { !type_info.skill[3].empty(), !type_info.skill[4].empty() };
    input.target_fire_res = target->fighter.res[0]; input.target_cold_res = target->fighter.res[2];
    // The second target (FUN_005ddc30): the skill-set one (FUN_005dd610)
    // when it's the target, else FUN_005dd0b0 mode 6 (FUN_005dcbd0, as a
    // merc's: rules::search_sight): the near rooms' foes (each room's
    // newest first: the list from its end), alive, out of town, not its
    // friends (FUN_005dc970), FUN_005dc380 distance, sight from them to it
    // always tested (mask 4); FUN_005dd510's path is always found, so the
    // primary when there's one.
    // Its state 0x92 (invis) skip needs no test here: only FUN_005ef620 (Baal into
    // the stairs: d2d's gone, so not alive) and FUN_0063efa0 case 0x219 (the
    // Ancients' statues: objects in d2d) set it.
    if (monster.set_kind != 0) { second = target; input.second_dist = best; }
    else {
        const auto wall = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x04); };
        const bool town = d2d::rules::level_ids::is_town(level.id);
        std::vector<d2d::rules::SightFoe> sight_foes;
        std::vector<Foe*> sight_of;
        for (const auto& [room_level, room] : near_rooms(game_data, level, unit.x, unit.y)) {
            if (room_level != &level) continue;
            for (std::size_t i = foes.size(); i-- > 0;) {
                auto& foe = foes[i];
                if (foe.of == &monster || room_of(level, foe.x, foe.y) != room) continue;
                const int foe_x = subtile(foe.x), foe_y = subtile(foe.y);
                auto& entry = sight_foes.emplace_back(d2d::rules::SightFoe{ d2d::rules::near_distance(foe_x - x, foe_y - y, foe.size), foe.threat,
                                                                            foe.alive && !town && !d2d::rules::friends(monster.align, foe.of ? foe.of->align : 2) });
                entry.blocked = entry.enemy && entry.distance < 0x31 && d2d::rules::sight_blocked(foe_x, foe_y, foe.size, x, y, type_info.size, wall);
                sight_of.push_back(&foe);
            }
        }
        int gap = 0x7fffffff;
        const int chosen = d2d::rules::sight_choice(sight_foes, d2d::rules::search_sight(sight_foes), [](int) { return true; }, gap);
        if (chosen >= 0) { second = sight_of[std::size_t(chosen)]; input.second_dist = gap; }
    }
    input.second = second != nullptr;
    input.aura_on = monster.aura != 0;
    std::vector<d2d::rules::ThinkEffect> effects;
    input.effects = &effects;
    input.kin_dist = std::numeric_limits<int>::max();          // no kin found (the searches start there: ai/)
    // Act 3's (rules::mon_think, monster-ai.md "Act 3's thinks"): its
    // command's unit; Skill1's level; the line to the target (mask 4);
    // MissS1's range; Skill6..8; Mephisto's weakest foe within 32
    // (FUN_005f7700); the spots (the priests' and the Council's cast points).
    {
        input.command_unit = *input.command == 1;
        input.state_c = monster.breath_state;   // the breath's state 0xc (FUN_00639df0)
        input.skill_level = d2d::rules::monster_skill_level(type_info.sk_lvl[0], monster.difficulty);
        const auto wall = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x04); };
        input.line_clear = !d2d::rules::sight_blocked(x, y, type_info.size, target_x, target_y, target->size, wall);
        if (const auto missile = game_data.missiles.find(type_info.miss_s1); !type_info.miss_s1.empty() && missile != game_data.missiles.end()) input.s1_range = missile->second.range;
        for (std::size_t slot = 0; slot < 3; ++slot) input.skill678[slot] = !type_info.skill[slot + 5].empty();
        for (const auto& foe : foes)
            if (const int off_x = subtile(foe.x) - x, off_y = subtile(foe.y) - y; foe.alive && off_x * off_x + off_y * off_y <= 0x400) input.weak = true;
        input.self_x = x; input.self_y = y; input.target_x = target_x; input.target_y = target_y;
    }
    // Act 5's (monster-ai.md "Act 5's MonAIs"): a line to the target clear
    // of 0x805 (FUN_005dc640: rules::pet_line_clear, any of three), Skill1's Param1 (its reach), the dead
    // test (mode DT / DD), its own life %; the target's max life and mana
    // (FUN_00625d10 / FUN_00625d60: Baal's act 8, the Succubi's Skill3) and
    // a curse on it (FUN_00625760 0x20: a States.txt curse state, shrines'
    // too).
    {
        input.lines_clear = d2d::rules::pet_line_clear(x, y, type_info.size, target_x, target_y, wall805);
        if (const auto* own = game_data.skills.get(skill_id(game_data, type_info.skill[0]))) input.skill1_range = own->par[0];
        input.self_dead = monster.mode == "DT" || monster.mode == "DD";
        input.self_life_pct = int(std::int64_t(monster.hit_points) * 100 / std::max(monster.stats.hit_points, 1));
        input.target_life_pct = target->life_pct;
        input.target_max_life = target->max_life; input.target_max_mana = target->max_mana;
        input.target_cursed = std::ranges::any_of(target->states, [&](std::string_view state) {
            const auto found = game_data.states.find(std::string(state));
            return found != game_data.states.end() && found->second.curse;
        });
        input.reach = input.in_melee;
    }
    // The inputs its own think reads beyond these (ai/act1.cpp .. act5.cpp).
    if (row && row->prep) row->prep(ctx);
    // FUN_005defe0 / FUN_005df140: n subtiles on from the target (or the
    // second target), by axis.
    d2d::rules::ThinkUnit away_from = d2d::rules::ThinkUnit::target;
    input.away_from = &away_from;
    auto away = [&](int subtiles, bool running) {
        const bool from_second = away_from == d2d::rules::ThinkUnit::second && second;
        const int from_x = from_second ? subtile(second->x) : target_x, from_y = from_second ? subtile(second->y) : target_y;
        const int off_x = subtiles * ((x > from_x) - (x < from_x)), off_y = subtiles * ((y > from_y) - (y < from_y));
        const float goal_x = (float(x + off_x) + 0.5f) / 5, goal_y = (float(y + off_y) + 0.5f) / 5;
        const float span = std::max(std::hypot(goal_x - unit.x, goal_y - unit.y), 0.01f), probe = std::min(0.2f, span);
        if (level.unit_blocked(goal_x, goal_y) || level.unit_blocked(unit.x + (goal_x - unit.x) / span * probe, unit.y + (goal_y - unit.y) / span * probe)) return false;
        ctx.walk_to(off_x, off_y, running);
        return true;
    };
    using d2d::rules::MonAct;
    if (cursed) {
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
        auto room = [&](float at_x, float at_y) { return room_of(level, at_x, at_y); };
        const int home = room(monster.home_x, monster.home_y);
        const d2d::rules::CountessIn where{ room(unit.x, unit.y) != home, room(target->x, target->y) != home,
                                            x == subtile(monster.home_x) && y == subtile(monster.home_y) };
        act = d2d::rules::countess_think(input, where, monster.path, monster.seed);
    } else {
        act = d2d::rules::mon_think(type_info.ai_name, input, monster.seed, away);
    }
    monster.move_pct = pace;                                       // the next mode takes it (FUN_005a63f0)
    // Its effects: the leader's command done; Duriel's aura (Skill4) on;
    // states 0x5a / 0x5b (looks: blue / red) on or off; an overlay played
    // once on it; stat 0x4a (regen). The Summoner's quest reads his first
    // think (ai_state).
    for (const auto& effect : effects) {
        if (effect.kind == d2d::rules::ThinkEffect::state && (effect.id == 0x5a || effect.id == 0x5b)) {
            const auto bit = std::uint8_t(effect.id == 0x5a ? 1 : 2);
            monster.looks = effect.value ? std::uint8_t(monster.looks | bit) : std::uint8_t(monster.looks & ~bit);
        }
        if (effect.kind == d2d::rules::ThinkEffect::overlay) { monster.flash_overlay = effect.id; monster.flash_at = now_ms; }
        if (effect.kind == d2d::rules::ThinkEffect::command_done) monster.ai_command = 0;
        if (effect.kind == d2d::rules::ThinkEffect::unset) monster.set_kind = 0;   // FUN_00573120
        if (effect.kind == d2d::rules::ThinkEffect::state && effect.id == 0xc && !effect.value) monster.breath_state = false;   // FUN_00639db0(0xc, 0)
        if (effect.kind == d2d::rules::ThinkEffect::stat && effect.id == 0x4a) monster.stats.regen = effect.value;              // BatDemon's regen aloft
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
        // target, 0xe at the corpse it raises).
        if (effect.kind == d2d::rules::ThinkEffect::rally && (effect.id == 1 || (effect.id == 14 && corpse >= 0)))
            for (auto& other : pack)
                if (&other != &monster && other.alive() && other.leader == int(&monster - pack.data())) {
                    other.ai_command = effect.id;
                    if (effect.id == 14) other.command_corpse = pack[std::size_t(corpse)].id;
                }
        // The wisps' dance: its finds' AI words (FUN_0058ec00), the blessing.
        if (effect.kind == d2d::rules::ThinkEffect::set_word && std::size_t(effect.id / 4) < wisp_finds.size()) {
            auto& wisp = pack[std::size_t(wisp_finds[std::size_t(effect.id / 4)])];
            (effect.id % 4 == 1 ? wisp.ai_state : effect.id % 4 == 2 ? wisp.ai_state2 : wisp.ai_state3) = effect.value;
        }
        if (effect.kind == d2d::rules::ThinkEffect::bless) target->blessed = effect.value;
        if (effect.kind == d2d::rules::ThinkEffect::mark) { monster.has_point = true; monster.point_x = effect.id; monster.point_y = effect.value; }
        if (effect.kind == d2d::rules::ThinkEffect::greet) monster.greeted = true;
        if (effect.kind == d2d::rules::ThinkEffect::mount && imp_found >= 0) pack[std::size_t(imp_found)].ai_state = monster.id;   // FUN_005e17e0
        // Baal's Vile Effigy (FUN_005fc860): none while one lives (FUN_005fc830
        // counts each other unit not in mode 0 or 0xc); baalclone
        // +- 12 (two draws of his seed) about the unit he's at (FUN_00553540;
        // his own spot without one), a third of his life, no regen (stat 0x4a
        // 0), made in NU; his AI control +0x18 counts it. Unit flags 0x4020000
        // (no experience, no drop: vile_effigy) and its first think 15 frames
        // on (005fca71 .. 005fca7f: FUN_005de0f0(15) with EDX the clone).
        if (effect.kind == d2d::rules::ThinkEffect::clone && born
            && std::ranges::none_of(pack, [&](const Monster& other) { return other.alive() && game_data.monsters.types[std::size_t(other.type)].ai_name == "BaalCrabClone"; }))
            if (auto clone = vile_effigy(game_data, level, monster, target ? target_x : x, target ? target_y : y, rng, players_in(foes))) {
                clone->next_act = now_ms + 15 * 40;
                born->push_back(std::move(*clone));
                ++monster.ai_state2;
            }   // a4q1's FUN_005b4390 (World::act4_update)
        if (effect.kind == d2d::rules::ThinkEffect::aura) { monster.aura = skill_id(game_data, type_info.skill[std::size_t(effect.id)]); monster.aura_lvl = std::max(input.aip[0], 1); }
    }
    // The unit it's at: its kin (a walk to it), the hurt one or corpse it
    // casts on; the second target, in the target's place.
    using d2d::rules::ThinkUnit;
    if (act.unit == ThinkUnit::second && second && second != target) {
        target = second;
        target_x = subtile(target->x); target_y = subtile(target->y);
        dx = target->x - unit.x; dy = target->y - unit.y;
    }
    const Monster* at_unit = act.unit == ThinkUnit::kin ? (kin >= 0 ? &pack[std::size_t(kin)] : corpse >= 0 ? &pack[std::size_t(corpse)] : nullptr)
                           : act.unit == ThinkUnit::aid && aid >= 0 ? &pack[std::size_t(aid)] : nullptr;
    // After the act, its next think (unit event 2) n frames on: `wait`
    // (FUN_005de0f0) replaces the pending one, `event` (FUN_005de130) only
    // when none is pending or it'd come sooner. It comes mid-mode or not
    // (FUN_005b1740 reads no mode; monster_update).
    const auto file_think = [&monster, now_ms, wait = act.wait, event = act.event] {
        if (wait > 0) monster.next_act = now_ms + std::uint32_t(wait) * 40;
        else if (event > 0 && (!monster.think_pending() || now_ms + std::uint32_t(event) * 40 <= monster.next_act))
            monster.next_act = now_ms + std::uint32_t(event) * 40;
    };
    // FUN_0058f730 / FUN_0058ef40: its leader's group, itself too. One made
    // outside a group (leader -1) has none to look up, so nothing is sent.
    if (rally && monster.leader >= 0)
        for (auto& other : pack)
            if (other.alive() && other.leader == monster.leader) other.ai_command = 1;
    const bool acted = [&] { for (;;) switch (act.act) {
        case MonAct::idle: ctx.idle(act.n); return true;
        case MonAct::a1: ctx.use("A1", -1); return true;
        case MonAct::a2: ctx.use("A2", -1); return true;
        case MonAct::s2: ctx.use("S2", -1); return true;
        case MonAct::s3: ctx.use("S3", -1); return true;
        case MonAct::s4: ctx.use("S4", -1); return true;
        case MonAct::s1: ctx.use("S1", -1); return true;               // the Council's MissS1
        case MonAct::sc: ctx.use("SC", -1); return true;               // the wisps' MissC
        case MonAct::mode_at:                                      // FUN_005ddfc0: mode n at a point (the wisps' SC)
            ctx.use("SC", -1);
            monster.skill_x = act.x; monster.skill_y = act.y; monster.at_point = true;
            unit.dir = direction16(float(act.x - x), float(act.y - y));
            return true;
        case MonAct::move_to: ctx.walk_to(act.x - x, act.y - y, false); return true;   // FUN_005de490: to the point
        case MonAct::warp:                                         // FUN_00554ea0: at once to the free spot nearest the point
            std::tie(unit.x, unit.y) = level.nearest_free((float(act.x) + 0.5f) / 5, (float(act.y) + 0.5f) / 5);
            ctx.idle(25);
            return true;
        case MonAct::gone: monster.gone = true; return true;       // FUN_005ddfc0: Mephisto's hydras at their end
        case MonAct::spawn:                                        // Coldworm's (FUN_005f9cf0); 2: the vile mother's lay
            if (act.n == 2) {                                      // FUN_005f9e30: Skill1 (Nest) at the spot, no unit; its sequence lays (do 91)
                const int lay = skill_id(game_data, type_info.skill[0]);
                const SeqRows* seq = skill_seq(game_data, type_info, lay);
                const std::string_view played = seq ? std::string_view(seq->front().mode) : std::string_view(type_info.sk_mode[0]);
                ctx.use(played, lay);
                if (seq) monster.mode_until = now_ms + std::uint32_t(seq->size()) * game_data.npc_timing(monster.npc, played).ms_per_frame() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, "SQ"));
                monster.skill_x = lay_x; monster.skill_y = lay_y;
                monster.nesting = true;
                return true;
            }
            if (act.n == 0) { set_mode(game_data, monster, "S1", now_ms); return true; }   // FUN_005de440: mode 8 where she stands
            // FUN_0063efa0 (BaseId 0x11c): her MonStats spawn 8 subtiles east, in
            // S1; FUN_005b2f20 within 2 of it (flags 0x42: no family mods, no
            // party), flag 0x4000000 (no experience); counted if made.
            if (const auto young = game_data.monsters.by_id.find(type_info.spawn); born && young != game_data.monsters.by_id.end()) {
                const int room = room_of(level, (float(x + 8) + 0.5f) / 5, (float(y) + 0.5f) / 5);
                if (int at_x = 0, at_y = 0; spawn_spot(game_data, level, crowd, rooms_in_play, young->second, x + 8, y, 2, room, at_x, at_y)) {
                    auto laid = make_monster(game_data, young->second, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes), level.id);
                    laid.aware = true; laid.no_exp = true;
                    laid.seed = d2d::rules::Rng{ rng.next() };
                    set_mode(game_data, laid, "S1", now_ms);
                    born->push_back(std::move(laid));
                    ++monster.ai_state;
                }
            }
            return true;
        case MonAct::walk: case MonAct::approach: case MonAct::run:  // FUN_005deb60 at the target
            if (at_unit) { ctx.walk_to(subtile(at_unit->unit.x) - x, subtile(at_unit->unit.y) - y, act.act == MonAct::run); return true; }
            if (ctx.set_off(act.act == MonAct::run, target)) {
                monster.chase = chase_of();
                return true;
            }
            if (act.act == MonAct::walk && act.n == 0) monster.ai_command = 0;   // the Fallen's charge ends (FUN_0058ed10)
            if (act.act == MonAct::walk && (act.n & 1)) monster.force_sight = true;   // FUN_005dd230: flag 0x40
            if (act.act != MonAct::walk || !(act.n & 2)) { ctx.idle(0); return true; }
            act = d2d::rules::walk_failed(monster.seed);
            break;
        case MonAct::direct:                                       // FUN_005deca0: straight at it (its kin: the defiler's find)
            if (act.unit == ThinkUnit::kin && at_unit) ctx.walk_to(subtile(at_unit->unit.x) - x, subtile(at_unit->unit.y) - y, false);
            else ctx.walk_to(target_x - x, target_y - y, false);
            return true;
        case MonAct::wander: ctx.walk_to(act.x, act.y, false); return true;
        case MonAct::around: ctx.walk_to(target_x - x + act.x, target_y - y + act.y, false); return true;
        case MonAct::home: ctx.walk_to(subtile(monster.home_x) - x, subtile(monster.home_y) - y, false); return true;
        case MonAct::point:                                        // FUN_005dead0(Sk1mode, Skill1, no unit, x, y)
            ctx.use(type_info.sk_mode[0], skill_id(game_data, type_info.skill[0]));
            monster.skill_x = act.x; monster.skill_y = act.y;
            unit.dir = direction16(float(act.x - x), float(act.y - y));
            return true;
        case MonAct::fly: ctx.walk_to(act.x - x, act.y - y, false); return true;   // FUN_005de440: to the point (mode n: the Vulture's S1)
        case MonAct::keep: {                                       // FUN_005de4e0: toward, or from, the target to keep x off
            if (act.unit == ThinkUnit::self) { ctx.idle(0); return true; }   // the Vulture's landing: a move onto itself
            const int stride = std::min(std::abs(best - act.x), act.n), sign = best < act.x ? -1 : 1;
            const int across = std::abs(target_x - x), down = std::abs(target_y - y), sum = std::max(across + down, stride);
            int step_x = sum ? across * stride / sum : 0, step_y = sum ? down * stride / sum : 0;
            while (sum && step_x + step_y < stride) { ++step_x; ++step_y; }
            ctx.walk_to(((target_x > x) - (target_x < x)) * step_x * sign, ((target_y > y) - (target_y < y)) * step_y * sign, false);
            return true;
        }
        case MonAct::circle: {                                     // FUN_005df7d0
            const float side = act.x ? 1.f : -1.f, length = std::max(std::hypot(dx, dy), 0.01f), off = float(act.n) / 5;
            unit.goal_x = target->x - dy / length * off * side; unit.goal_y = target->y + dx / length * off * side;
            ctx.set_off(false, nullptr);
            return true;
        }
        case MonAct::seq:                                          // FUN_005de000: the skill's own sequence (the Greater Mummy's raise)
        case MonAct::skill: {                                      // its Sk mode's sequence (MonSeq) plays out
            // The generic spawner's young (FUN_005de000: 0xa7 Nest's sequence,
            // seq_genericspawner) is its slot's, below. An AI's own start
            // (the catapult spotter's strike, ai/) may take the act.
            if (row && row->skill && row->skill(ctx)) return true;
            // A skill neither plain nor in a sequence stands: none a placed
            // monster's think casts (every MonStats Skill1..8 checked; only the
            // diablo clone's Diablogeddon, and no level places it).
            // A raw Skills.txt id past the slots (the putrid defiler's 300): its slot.
            std::size_t slot = std::size_t(std::max(act.n, 0));
            if (slot >= type_info.skill.size())
                slot = std::size_t(std::ranges::find_if(type_info.skill, [&](const std::string& own) { return !own.empty() && skill_id(game_data, own) == act.n; }) - type_info.skill.begin());
            if (slot >= type_info.skill.size()) { ctx.idle(0); return true; }
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
            // Charge (srvstfunc 31, FUN_005cf6b0; Charge and SerpentCharge):
            // in melee it swings at once; else it runs at the target (MonStats
            // Run), swinging at the end. The other sequence that walks first,
            // seq_swarmmove (Swarm Move, 166), no think casts: the Swarm's
            // (FUN_005f2460) has no skill, so that one stands.
            // ponytail: its charge path (type 0x14) and state 0x12 are the run's.
            // DiabRun (srvst 54 / do 103, FUN_005cd2c0 / FUN_005cd380): his
            // sequence at the target's spot as it is now (monster_update plays it).
            if (const auto* run_skill = game_data.skills.get(id); run_skill && run_skill->srvdofunc == d2d::rules::ServerDoFunction::kDiabRun) {
                monster.chase = chase_of();
                unit.dir = direction16(dx, dy);
                set_mode(game_data, monster, "RN", now_ms);
                monster.skill = id; monster.skill_x = target_x; monster.skill_y = target_y; monster.wandering = false;
                monster.skill_sound = id;
                monster.diab_run = 1; monster.diab_run_ms = now_ms;
                monster.seq_mode = "RN"; monster.seq_frame = 0;
                return true;
            }
            if (seq && seq->front().mode == "WL") {
                const auto* start = game_data.skills.get(id);
                if (start && start->srvstfunc == d2d::rules::ServerStartFunction::kCharge) {
                    if (input.in_melee) { charge_swing(game_data, monster, *target, id, now_ms, rng); monster.skill_sound = id; return true; }
                    if (ctx.set_off(true, target)) { monster.chase = chase_of(); monster.charging = id; monster.skill_sound = id; return true; }
                }
                ctx.idle(0);
                return true;
            }
            if ((!seq && !plain) || ((name == "Resurrect" || name == "Resurrect2") && corpse < 0)) { ctx.idle(0); return true; }
            // MagottUp (srvstfunc 44, FUN_005cb170): it comes up at the free
            // spot nearest (FUN_00554ea0 -> FUN_0064e7b0, its size, mask
            // 0x1c09), its flags 0xe back; none: the start fails.
            // MagottDown (45, FUN_005cb270) buries it.
            if (const auto* start = game_data.skills.get(id); start && start->srvstfunc == d2d::rules::ServerStartFunction::kMaggotUp && monster.buried) {
                const auto spot = free_spot_near(level, crowd, &unit, x, y, unit.shape.size, 0x1c09);
                if (!spot) { ctx.idle(0); return true; }
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
                if (!landing) { ctx.idle(0); return true; }
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
                    if (input.in_melee) { ctx.use("A1", -1); return true; }
                    ctx.idle(0);
                    return true;
                }
            }
            // Its rows' own modes are drawn row by row (Monster::seq_mode);
            // the first row's mode stands for it in the logic.
            const std::string_view played = seq ? std::string_view(seq->front().mode) : std::string_view(type_info.sk_mode[slot]);
            ctx.use(played, id);
            if (seq) monster.mode_until = now_ms + std::uint32_t(seq->size()) * game_data.npc_timing(monster.npc, played).ms_per_frame() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, "SQ"));
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
                monster.leap_until = unit.mode_ms + 11 * game_data.npc_timing(monster.npc, played).ms_per_frame() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, "SQ"));
            }
            // Whirlwind (srvstfunc 38, FUN_005d8f50; Talic's seq_ancientwhirlwind):
            // in reach a plain attack (FUN_0056e230); else a path to its point
            // (FUN_0064ea90) at its walk velocity (FUN_0056e5b0: MonStats
            // Velocity), its sequence looping till it's there (FUN_005d9460).
            if (const auto* spin = game_data.skills.get(id); spin && spin->srvstfunc == d2d::rules::ServerStartFunction::kWhirlwind && seq) {
                const int to_x = act.x || act.y ? act.x : target_x, to_y = act.x || act.y ? act.y : target_y;
                if (input.in_melee) ctx.use("A1", -1);
                else if (path_to(level, monster, to_x, to_y, false, crowd)) {
                    monster.whirl = true; monster.whirl_ms = now_ms; monster.whirl_row = 0; monster.whirl_last = -1;
                    d2d::log::info("{} whirls to ({}, {})", monster.npc.name, to_x, to_y);
                } else { set_mode(game_data, monster, "NU", now_ms); monster.skill = -1; ctx.idle(0); return true; }
            }
            const bool explode = plain_skill && plain_skill->srvdofunc == d2d::rules::ServerDoFunction::kCorpseExplosion;   // Nihlathak's, at the corpse he found
            monster.skill_unit = name == "Resurrect" || name == "Resurrect2" || explode ? corpse : act.unit == ThinkUnit::aid ? aid : -1;
            // Where a Nest's young come out: Blood Raven's spot off the
            // target, else spawnx / spawny off itself.
            monster.skill_x = act.x || act.y ? target_x + act.x : x + type_info.spawn_x;
            monster.skill_y = act.x || act.y ? target_y + act.y : y + type_info.spawn_y;
            if (act.spot) { monster.skill_x = target_x; monster.skill_y = target_y; }   // at the target's spot (UberIzual's teleport)
            if (monster.skill_unit >= 0) unit.dir = direction16(pack[std::size_t(monster.skill_unit)].unit.x - unit.x, pack[std::size_t(monster.skill_unit)].unit.y - unit.y);
            if (row && row->aim) row->aim(ctx);                    // its own aim (a point on the map, its find: ai/)
            return true;
        }
        case MonAct::die:                                          // no loot, no experience
            monster.hit_points = 0;
            set_mode(game_data, monster, "DT", now_ms);
            return true;
        case MonAct::none: case MonAct::untraced: return true;
    } }();
    file_think();
    return acted;
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
    burner.src.no_hit_roll = !fire_info.to_hit;
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
    // Its think event comes due mid-mode too (FUN_005b1740 reads no mode) and
    // its act replaces the mode (FUN_005a7c20 stops none), but not under
    // state 0x36 (FUN_005a7c20's error 0x753) or frozen (FUN_005a7f80 drops
    // it). A mode's end (FUN_005a8030: not WL / RN) goes to NU, whose start
    // FUN_005a73e0 files the think aidel on (0: 15; stunned 0x2d) unless one
    // is pending later.
    const bool own_think = monster.special == 10 || monster.special == 11 || d2d::rules::traced_ai(type_info.ai_name);
    const bool frozen = now_ms < monster.frozen_until;
    const auto think_due = [&] { return own_think && monster.think_pending() && now_ms >= monster.next_act && !monster.uninterruptable() && !frozen; };
    // Frozen, a mode's events due are dropped (FUN_005a7f80: 0 its strike or
    // sequence row, 1 its end): an end inside the freeze never comes, so the
    // mode holds till the think after the thaw replaces it.
    const auto mode_over = [&] {
        if (now_ms < monster.mode_until) return false;
        if (!frozen || !own_think) return true;   // an untraced AI has no think to replace it
        monster.mode_until = std::numeric_limits<std::uint32_t>::max();
        return false;
    };
    const auto mode_ends = [&] {
        set_mode(game_data, monster, "NU", now_ms);
        if (monster.think_pending() && monster.next_act > now_ms) return;
        const int aidel = type_info.diff[std::size_t(monster.difficulty)].aidel;
        monster.next_act = now_ms + std::uint32_t(now_ms < monster.stunned_until ? 0x2d : aidel > 0 ? aidel : 15) * 40;
    };
    if (!monster.alive()) {
        if (monster.mode == "DT" && now_ms >= monster.mode_until) set_mode(game_data, monster, "DD", now_ms);
        return false;
    }
    // A freeze's end (state 1's callback 0x57b170): its AI events go
    // and the next think is filed at the end + MonStats aidel (0: 15); the
    // thinks due while frozen were dropped (FUN_005a7f80). A stun's end
    // (state 21, callback 0x56e900) files none.
    // Under a longer stun too: a think due while stunned stands 3 (below).
    if (monster.frozen_until != 0 && now_ms >= monster.frozen_until) {
        const int aidel = type_info.diff[std::size_t(monster.difficulty)].aidel;
        monster.next_act = monster.frozen_until + std::uint32_t(aidel > 0 ? aidel : 15) * 40;
        monster.frozen_until = 0;
    }
    if (monster.stun_until != 0 && now_ms >= monster.stun_until) monster.stun_until = monster.stunned_until = 0;
    // A riding imp sits on its mount (FUN_00621ce0's bind).
    if (monster.riding && monster.bound >= 0 && std::size_t(monster.bound) < pack.size()) { unit.x = pack[std::size_t(monster.bound)].unit.x; unit.y = pack[std::size_t(monster.bound)].unit.y; }
    // A breath under way (FUN_005cc4e0 re-run, FUN_005417d0 type 0): a flame
    // every calc3 frames (at least 1) at its foe till its end; the mode held.
    // Imp Inferno's (FUN_005d1350, event 1): every 3 frames, each flame's
    // range calc2 + its row's Param2 (FUN_0064a2b0 / FUN_0064a330).
    // The others' (FUN_005cc4e0, DiabLight's FUN_005cc690 -> FUN_005cc2e0):
    // calc1 frames, or under 1 its row's Param2 + lvl - 1; 1..255.
    // The others' animation rate is AnimLen << 8 / range (FUN_00621780).
    // Frozen, its re-run (event 0 / 1) is dropped and the breath ends.
    if (monster.breath >= 0) {
        if (now_ms >= monster.breath_until || (frozen && now_ms >= monster.breath_next)) monster.breath = -1;
        else if (now_ms >= monster.breath_next) {
            if (const auto* cast = game_data.skills.get(monster.breath)) {
                const int lvl = skill_level(game_data, monster, monster.breath);
                const bool inferno = cast->srvdofunc == d2d::rules::ServerDoFunction::kImpInferno;
                const std::size_t sent = missiles.size();
                andariel_missile(game_data, monster, cast->srvmissilea, foe.x - unit.x, foe.y - unit.y, now_ms, missiles, lvl, cast);
                if (missiles.size() > sent) {
                    auto& flame = missiles.back();
                    int range = 0;
                    if (inferno) range = std::max(d2d::rules::eval_calc(game_data.skills, cast->calc[1], {}, monster.breath, lvl) + flame.info->param2, 1);
                    else {
                        range = d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.breath, lvl);
                        if (range < 1) range = flame.info->param2 - 1 + lvl;
                        range = std::clamp(range, 1, 255);
                    }
                    flame.dies = now_ms + std::uint32_t(range) * 40;
                    if (!inferno) flame.anim_rate = flame.info->anim_len * 256 / range;
                }
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
    // Stunned, it stands (a get-hit plays out first); frozen, a get-hit,
    // block, attack or cast holds (FUN_0057b230 sets no mode).
    const bool attack = monster.mode == "A1" || monster.mode == "A2";
    const bool casting = attack || monster.mode == "S1" || monster.mode == "S2" || monster.mode == "SC" || (monster.mode == "S3" && monster.skill >= 0)
                      || (monster.mode == "DT" && monster.skill >= 0);   // DT alive: a sequence (the egg's hatch)
    if (now_ms < monster.stun_until && monster.mode != "GH" && monster.mode != "BL" && !(frozen && casting)) {
        if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
        if (now_ms < monster.stunned_until && think_due()) {          // stunned (state 0x15): the driver's first step stands 3 (FUN_005b10e0)
            monster.thought_ms = now_ms;
            monster.next_act = now_ms + 3 * 40;
        }
        return false;
    }
    if (monster.mode == "GH" || monster.mode == "BL") {
        monster.leaping = false;                                       // struck mid-leap: it stays where it is
        if (mode_over()) mode_ends();
        else if (!think_due()) return false;
        else if (now_ms < monster.stunned_until) {                     // stunned (state 0x15): the driver's first step stands 3 (FUN_005b10e0)
            monster.thought_ms = now_ms;
            set_mode(game_data, monster, "NU", now_ms);
            monster.next_act = now_ms + 3 * 40;
            return false;
        }
    }
    // DiabRun's sequence (seq_diablorun, RN rows; FUN_005cd380 on its event
    // rows): rows 0..Param3 - 1 wind up; at Param3 (5) he runs at the spot
    // (path type 1) at max(calc1 << 8, 0x100) x velocitypercent (stat 0x43)
    // / 100 (FUN_00483360 into the path's +0x7c, a MonStats Velocity's
    // unit), the rows looping Param6..Param4 (6..13) on the way; the run
    // over (record flag 2: there, or blocked), rows Param2.. (14..21) play
    // out. Unit +0x4e is the frame's event (FUN_00623e.., the sequence's
    // event column): on an event-1 row (18) the hit lands when the target
    // is in reach (FUN_00622c40): the skill's damage (FUN_0056e680, flags 0x20),
    // a sure hit (ResultFlags 5 has bit 1: FUN_0057ec10 isn't run).
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
            auto stats = monster.swing_stats();
            stats.a1_min = d2d::rules::skill_phys(game_data.skills, *run_skill, {}, lvl, false) >> 8;
            stats.a1_max = std::max(d2d::rules::skill_phys(game_data.skills, *run_skill, {}, lvl, true) >> 8, stats.a1_min);
            for (auto& element : stats.elements) element.mode = "";
            const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), stats, false, rng, false, (run_skill->result_flags & 1) != 0);
            foe.take(taken);
            if (taken.hit) foe.struck_by(type_info.hit_class);
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
            if (taken.hit) struck.struck_by(type_info.hit_class);
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
    if (monster.mode == "SC" && monster.skill == d2d::rules::kAndrialSpray) {
        const int frame = int((now_ms - unit.mode_ms) / game_data.npc_timing(monster.npc, "SC").ms_per_frame());
        const int x = subtile(unit.x), y = subtile(unit.y);
        while (monster.skill_frame < std::min(frame, 12))
            if (++monster.skill_frame >= 4 && !frozen) {
                const auto [aim_x, aim_y] = d2d::rules::andariel_spray_aim(d2d::rules::direction64(x, y, monster.skill_x, monster.skill_y), monster.skill_frame);
                andariel_missile(game_data, monster, "andarielspray", float(aim_x) / 5, float(aim_y) / 5, now_ms, missiles, skill_level(game_data, monster, d2d::rules::kAndrialSpray));
            }
        if (mode_over()) { mode_ends(); monster.skill = -1; }
        else if (!think_due()) return false;
    }
    // An attack, a taunt (the Fallen's S2), a skill's sequence (MonSeq: its
    // event rows), a young one coming out (spawnmode S1), a skill cast in S3
    // (Diablo's Wall, Prison and Prime Firewall: Sk*mode S3).
    if (casting && !(monster.mode == "SC" && monster.skill == d2d::rules::kAndrialSpray)) {
        const auto& timing = game_data.npc_timing(monster.npc, monster.mode);
        const SeqRows* seq = skill_seq(game_data, type_info, monster.skill);
        if (seq && seq->front().mode == "WL") seq = nullptr;               // a charge's swing: one hit (its flag 0x40 after the first)
        const auto skill = [&](const char* name) { return seq && monster.skill == skill_id(game_data, name); };
        // A sequence's rows, one a frame (FUN_005a8670, SQ's event function):
        // event 4, or 1 / 2 till a shot has marked it done (unit flag 0x40),
        // runs its skill; a plain mode strikes on its action frame.
        int due = 0;
        if (seq) {
            const int reached = std::min(int((now_ms - unit.mode_ms) / std::max<std::uint32_t>(timing.ms_per_frame() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, "SQ")), 1)), int(seq->size()) - 1);
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
        } else if (!monster.struck && now_ms >= unit.mode_ms + timing.action_ms() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, monster.mode))) {
            monster.struck = true;
            due = 1;
        }
        if (frozen) due = 0;
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
                x.src = monster_shot_stats(missile_info, monster.swing_stats(), first, cast, s1_shot, lvl);
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
                        const auto [aim_x, aim_y] = d2d::rules::kNovaRing[std::size_t(k)];
                        const std::size_t had = missiles.size();
                        andariel_missile(game_data, monster, tower->srvmissilea, float(aim_x), float(aim_y), now_ms, missiles, lvl, tower);
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
                monster.nesting = false;
                const int room = room_of(level, (float(monster.skill_x) + 0.5f) / 5, (float(monster.skill_y) + 0.5f) / 5);
                const auto young = game_data.monsters.by_id.find(type_info.spawn);
                if (int at_x = 0, at_y = 0; born && young != game_data.monsters.by_id.end()
                    && spawn_spot(game_data, level, crowd, rooms_in_play, young->second, monster.skill_x, monster.skill_y, 1, room, at_x, at_y)) {
                    auto laid = make_monster(game_data, young->second, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes), level.id);
                    laid.aware = true; laid.no_exp = true; laid.no_drop = true;
                    laid.seed = d2d::rules::Rng{ rng.next() };                 // its own seed, drawn off the game's
                    if (type_info.spawn_mode != "NU" && !type_info.spawn_mode.empty()) set_mode(game_data, laid, type_info.spawn_mode, now_ms);
                    born->push_back(std::move(laid));
                }
            } else if (const auto* fire = seq ? game_data.skills.get(monster.skill) : nullptr; fire && fire->srvdofunc == d2d::rules::ServerDoFunction::kImpInferno) {
                // imp6..8's Inferno (seq_impinferno, its event-4 row 12): do 126 as the
                // plain SC cast's, its end set as the sequence started (srvst 59, the use above).
                monster.breath = monster.skill;
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
                        if (taken.hit) { foe.knocked_from = std::pair{ unit.x, unit.y }; foe.struck_by(type_info.hit_class); monprop_hit(monster, foe, false); }
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
                // d is FUN_006417f0's (ai_distance on the subtiles).
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
                    erupt.anim_rate = std::max(d2d::rules::eval_calc(game_data.skills, maul->calc[0], {}, monster.skill, lvl), 0);   // calc1 (FUN_00621780)
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
                const int room = room_of(level, (float(egg_x) + 0.5f) / 5, (float(egg_y) + 0.5f) / 5);
                const auto young = game_data.monsters.by_id.find(type_info.spawn);
                if (int put_x = 0, put_y = 0; born && young != game_data.monsters.by_id.end()
                    && spawn_spot(game_data, level, crowd, rooms_in_play, young->second, egg_x, egg_y, -1, room, put_x, put_y)) {
                    auto laid = make_monster(game_data, young->second, (float(put_x) + 0.5f) / 5, (float(put_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes), level.id);
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
                    const int egg_x = subtile(unit.x), egg_y = subtile(unit.y), egg_room = room_of(level, unit.x, unit.y);
                    std::vector<Monster> larvae;
                    larvae.reserve(std::size_t(count));
                    Crowd with_larvae = crowd;
                    auto hatch = [&](int radius, std::string_view larva_mode) {
                        int put_x = 0, put_y = 0;
                        if (!spawn_spot(game_data, level, with_larvae, rooms_in_play, young->second, egg_x, egg_y, radius, egg_room, put_x, put_y)) return false;
                        auto& larva = larvae.emplace_back(make_monster(game_data, young->second, (float(put_x) + 0.5f) / 5, (float(put_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes), level.id));
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
                if (back > 0) heal(monster, monster.stats.hit_points * back / 100);
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
                // FUN_005cc960 puts no state on it: a raised monster gets unit
                // flags 0x4020000 (no experience, no drop). FUN_005ccb10's
                // overlay is the skill's srvoverlay (+0x4e), none for Resurrect
                // and Resurrect2 (their healing is a castoverlay, the client's).
                if (monster.skill_unit >= 0 && std::size_t(monster.skill_unit) < pack.size()) {
                    auto& raised = pack[std::size_t(monster.skill_unit)];
                    if (raised.usable_corpse()) {
                        raised.no_exp = raised.no_drop = true;
                        raised.hit_points = raised.last_hp = raised.stats.hit_points;
                        raised.life_frac = raised.max_life_frac;
                        raised.ai_state = raised.ai_command = 0; raised.skill = -1; raised.left_mode = "NU";
                        set_mode(game_data, raised, "NU", now_ms);
                    }
                }
            } else if (monster.skill == d2d::rules::kAndyPoisonBolt) {                     // FUN_0056ecb0: one andypoisonbolt at the target
                andariel_missile(game_data, monster, "andypoisonbolt", dx, dy, now_ms, missiles, skill_level(game_data, monster, d2d::rules::kAndyPoisonBolt));
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
                    // calc1 (at least 1) frames on; srvoverlay curseattract on it. The
                    // unit is its path's (+0x58, FUN_00553540): the think's second
                    // target, which `use` made the foe; its overlay (row > 0,
                    // FUN_00621e40) a monster's flash_overlay, else Foe::flashed.
                    const auto& rows = game_data.overlay_rows;
                    const auto named = game_data.overlays.find(cast->srvoverlay);
                    if (const auto row = named == game_data.overlays.end() ? rows.end() : std::ranges::find(rows, &named->second); row > rows.begin() && row != rows.end()) {
                        const int overlay = int(row - rows.begin());
                        if (foe.of && foe.of >= pack.data() && foe.of < pack.data() + pack.size()) { auto& hit = pack[std::size_t(foe.of - pack.data())]; hit.flash_overlay = overlay; hit.flash_at = now_ms; }
                        else foe.flashed = overlay;
                    }
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
                    // attack rate, armor and damage %). The roll is the caster's seed
                    // (+0x20), drawn for any live minion before the Bloodlust test.
                    // The minion turns itself (morph_monster: FUN_00574370).
                    // Its state 0x8e (changeclass, stat 0x163 the old class) isn't kept: only the
                    // client's set function FUN_004d89b0 reads it, and acts only for 0x21f
                    // (baalthrone); no server code tests the state.
                    if (monster.skill_unit >= 0 && std::size_t(monster.skill_unit) < pack.size()) {
                        auto& whipped = pack[std::size_t(monster.skill_unit)];
                        const bool minion = whipped.type >= 0 && game_data.monsters.types[std::size_t(whipped.type)].base == 0x1c5;
                        const bool lusted = whipped.cry.skill == monster.skill && now_ms < whipped.cry.until;
                        const int chance = d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl);
                        const auto summon = game_data.monsters.by_id.find(cast->summon);
                        if (whipped.alive() && minion && chance <= monster.seed(100) && !lusted && summon != game_data.monsters.by_id.end()) {
                            morph_monster(game_data, whipped, summon->second, rng, players_in(foes));
                            whipped.aware = true;
                            set_mode(game_data, whipped, cast->summode.empty() ? "NU" : cast->summode, now_ms);
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
                        const auto [aim_x, aim_y] = d2d::rules::kNovaRing[std::size_t(k)];
                        const std::size_t had = missiles.size();
                        andariel_missile(game_data, monster, cast->srvmissilea, float(aim_x), float(aim_y), now_ms, missiles, lvl, cast);
                        if (missiles.size() > had) { missiles.back().dies = now_ms + std::uint32_t(std::max(reach, 1)) * 40; missiles.back().to.reset(); }
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kMissileWall) {
                    countess_firewall(game_data, monster, now_ms, missiles, cast->srvmissilea);
                } else if (cast->srvdofunc == ServerDoFunction::kScatteredBolts) {
                    // FUN_005c9300: calc1 of its srvmissilea (FUN_00646ca0) at the target, each
                    // wandering by its index (FUN_005c9290), as the player's Charged Bolt.
                    const int bolts = std::max(d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl), 1);
                    for (int bolt = 0; bolt < bolts; ++bolt) {
                        const std::size_t had = missiles.size();
                        andariel_missile(game_data, monster, cast->srvmissilea, dx, dy, now_ms, missiles, lvl, cast);
                        if (missiles.size() > had) missiles.back().bolt = bolt;
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kMonCurseCast) {
                    // FUN_005ce2b0: seed % 5 picks 0x6e3228's curse (Amplify Damage,
                    // Weaken, Life Tap, Decrepify, Lower Resist), cast at its level round the
                    // target as do 30, with the pick's own stats (rules::monster_curse_stats).
                    const int curse_index = monster.seed(5);
                    const int curse = d2d::rules::kMonsterCurses[std::size_t(curse_index)];
                    const auto* cursed = game_data.skills.get(curse);
                    const int radius = cursed ? d2d::rules::eval_calc(game_data.skills, cursed->aurarange, {}, curse, lvl) : 0;
                    for (auto& near_foe : foes)
                        if (near_foe.alive && !near_foe.of && !foe.of && std::hypot(near_foe.x - foe.x, near_foe.y - foe.y) * 5 <= float(radius))
                            near_foe.cursed = { curse, lvl, curse_index };
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
                        for (auto& element : stats.elements) element = {};
                        stats.elements[0] = { 1, 100, dmg - dmg / 2, dmg - dmg / 2, 0, "A1" };   // fire
                        for (auto& near_foe : foes)
                            if (near_foe.alive && std::hypot(near_foe.x - corpse.unit.x, near_foe.y - corpse.unit.y) * 5 <= float(radius)) {
                                const auto taken = d2d::rules::monster_blow(near_foe.fighter, near_foe.level, near_foe.motion(), stats, false, rng, false, true, true);   // FUN_005c4d10: the record straight to FUN_0057c6c0 (not FUN_0057b420's roll)
                                near_foe.take(taken);
                            }
                        d2d::log::info("{} explodes a corpse ({} damage)", monster.npc.name, dmg);
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kBaalCorpseExplode) {
                    for (auto& corpse : pack)
                        if (baal_corpse_spent(corpse, subtile(corpse.unit.x) - subtile(unit.x), subtile(corpse.unit.y) - subtile(unit.y))) corpse.corpse_used = true;
                } else if (cast->srvdofunc == ServerDoFunction::kMinionSpawner) {
                    // Start 62 (FUN_005d2420 -> FUN_0056e620 -> FUN_0063ea70): the
                    // caster's MonStats spawn at its spot + spawnx / spawny, in
                    // spawnmode (out of 0..15: NU); no spawn row (Nihlathak's), the
                    // class his think kept (AI control +0x3c, read by FUN_0058f710: FUN_0063ec70, the
                    // level's own minion). Do 135 (FUN_005d2490) makes it through
                    // FUN_005b2f20: exactly there, else within 4.
                    // Flags 0x4020000 (no experience, no drop); MinionSpawner's Skills
                    // +0xe6 overlay is none. FUN_00621ce0 records the caster as it's
                    // made; nothing reads that for these.
                    const auto spawn_row = game_data.monsters.by_id.find(type_info.spawn);
                    const int young = spawn_row != game_data.monsters.by_id.end() ? spawn_row->second
                                    : type_info.ai_name == "Nihlathak" ? level_monster(game_data, level, int(game_data.mon_bin[0x1c5])) : -1;
                    const int at_x0 = subtile(unit.x) + (spawn_row != game_data.monsters.by_id.end() ? type_info.spawn_x : 0), at_y0 = subtile(unit.y) + (spawn_row != game_data.monsters.by_id.end() ? type_info.spawn_y : 0);
                    const int room = room_of(level, (float(at_x0) + 0.5f) / 5, (float(at_y0) + 0.5f) / 5);
                    if (int at_x = 0, at_y = 0; born && young >= 0
                        && (spawn_spot(game_data, level, crowd, rooms_in_play, young, at_x0, at_y0, -1, room, at_x, at_y) || spawn_spot(game_data, level, crowd, rooms_in_play, young, at_x0, at_y0, 4, room, at_x, at_y))) {
                        auto made = make_monster(game_data, young, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes), level.id);
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
                    for (auto& element : stats.elements) element = {};
                    const auto added = d2d::rules::row_damage(cast->etype, cast->emin, cast->emax, cast->emin_lev, cast->emax_lev, cast->hitshift, cast->elen, cast->elen_lev, lvl);
                    if (added.etype >= 0) stats.elements[0] = { added.etype, 100, added.elo >> 8, added.ehi >> 8, added.elen, "A1" };
                    const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), stats, false, rng, false, true, true);   // the record is min == max, not FUN_0057b420's roll
                    foe.take(taken);
                    if (taken.hit) foe.struck_by(type_info.hit_class);
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
                    const int foe_room = room_of(level, foe.x, foe.y);
                    if (born && !d2d::rules::level_ids::is_town(level.id))
                        for (std::size_t k = 0; k < kPrison.size(); ++k) {
                            int at_x = 0, at_y = 0;
                            if (!spawn_spot(game_data, level, crowd, rooms_in_play, 0x154 + int(k), subtile(foe.x) + kPrison[k].first, subtile(foe.y) + kPrison[k].second, -1, foe_room, at_x, at_y)) {
                                if (k == 0) break;
                                continue;
                            }
                            auto bone = make_monster(game_data, 0x154 + int(k), (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes), level.id);
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
                            monster.armor_pool = d2d::rules::eval_calc(game_data.skills, cast->aura_calc[k], {}, monster.skill, lvl);
                            monster.armored = monster.armor_pool > 0;
                        }
                    // Another's (UberIzual's Chilling Armor): its state for auralen frames.
                    if (!cast->aurastate.empty() && !std::ranges::contains(cast->aurastat, d2d::d2s::kBoneArmor))
                        monster.self_state = { monster.skill, lvl, now_ms + std::uint32_t(std::max(0, d2d::rules::eval_calc(game_data.skills, cast->auralen, {}, monster.skill, lvl))) * 40 };
                } else if (cast->srvdofunc == ServerDoFunction::kRegurgitatorEat) {
                    // FUN_005cdc10: a corpse (DD) it's at eaten (gone), its life (256ths)
                    // up by MulDiv(the corpse's max life, calc1, 100) (FUN_00483360: ECX,
                    // EDX, then 100 on the stack), at most its max, at least 1.
                    if (monster.skill_unit >= 0 && std::size_t(monster.skill_unit) < pack.size())
                        if (auto& eaten = pack[std::size_t(monster.skill_unit)]; eaten.usable_corpse()) {
                            eaten.corpse_used = true;
                            const int pct = d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl);
                            const int max_life = monster.max_life_fine();
                            const auto life = std::int64_t(monster.life_fine()) + d2d::rules::mul_div(eaten.max_life_fine(), pct, 100);
                            monster.set_life_fine(int(std::min<std::int64_t>(std::max<std::int64_t>(life, 1), max_life)));
                        }
                } else if (cast->srvdofunc == ServerDoFunction::kMonsterBreath || cast->srvdofunc == ServerDoFunction::kDiabLight || cast->srvdofunc == ServerDoFunction::kImpInferno) {
                    // DiabLight: the same start (srvst 53) and its bolts while state 0xc's on (FUN_005cc690).
                    // FUN_005cc240 / FUN_005cc4e0: state 0xc, its end calc2 frames on; a
                    // flame now and every calc3 frames after (the breath below).
                    // Imp Inferno (FUN_005d1280 / FUN_005d1350): its end set at the
                    // start (srvst 59, the think's use), a flame every 3 frames.
                    monster.breath = monster.skill;
                    monster.breath_state = true;
                    if (cast->srvdofunc != ServerDoFunction::kImpInferno) {
                        d2d::rules::CalcEnv env;
                        env.rng = &monster.seed;
                        monster.breath_until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data.skills, cast->calc[1], env, monster.skill, lvl), 1)) * 40;
                    }
                    monster.breath_next = now_ms;
                    monster.next_act = std::max(monster.next_act, monster.breath_until);
                } else if (cast->srvdofunc == ServerDoFunction::kMissileAtTarget) {
                    // FUN_005ca3e0: its srvmissilea at the target (MonBlizzard's centre),
                    // or at its point (the hell meteor's), when the missile's Size
                    // square there is clear of mask 5 (FUN_0056e450 -> FUN_0064d800).
                    // FUN_0056ede0 -> FUN_0059fa30 with flag 1 starts it there (record
                    // +0x14 / +0x18), heading a subtile on at (+1, +1); every shipped
                    // row (Vel 0) stands.
                    const float aim_x = monster.at_point ? (float(monster.skill_x) + 0.5f) / 5 - unit.x : dx, aim_y = monster.at_point ? (float(monster.skill_y) + 0.5f) / 5 - unit.y : dy;
                    const auto meteor = game_data.missiles.find(cast->srvmissilea);
                    const int size = meteor == game_data.missiles.end() ? 1 : std::max(meteor->second.size, 1);
                    const int point_x = subtile(unit.x + aim_x), point_y = subtile(unit.y + aim_y);
                    bool clear = true;
                    for (int at_y = point_y - (size >> 1); at_y < point_y - (size >> 1) + size; ++at_y)
                        for (int at_x = point_x - (size >> 1); at_x < point_x - (size >> 1) + size; ++at_x)
                            clear = clear && !level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 5);
                    if (clear) missile_at_target(game_data, monster, cast->srvmissilea, aim_x, aim_y, now_ms, missiles, lvl, cast);
                } else if (cast->srvdofunc == ServerDoFunction::kMonPoisonNova) {
                    // FUN_005ccd10: srvmissilea toward the even 8 of rules::kPoisonNova at
                    // Param1 << 6, then every calc2th from the second at Param2 << 6
                    // (calc2 2: the 8 between; under 2: none), as the chest trap's.
                    const auto ring = game_data.missiles.find(cast->srvmissilea);
                    const int step = d2d::rules::eval_calc(game_data.skills, cast->calc[1], {}, monster.skill, lvl);
                    auto shoot = [&](std::size_t way, int param) {
                        const auto [off_x, off_y] = d2d::rules::kPoisonNova[way];
                        const std::size_t had = missiles.size();
                        andariel_missile(game_data, monster, cast->srvmissilea, float(off_x), float(off_y), now_ms, missiles, lvl, cast);
                        if (missiles.size() == had) return;
                        auto& ring_shot = missiles.back();
                        const float speed = missile_speed((param << 6) * 75 / 100), length = std::hypot(float(off_x), float(off_y));   // FUN_0059fa30's x 75 / 100
                        ring_shot.velocity_x = float(off_x) / length * speed; ring_shot.velocity_y = float(off_y) / length * speed;
                    };
                    if (ring != game_data.missiles.end()) {
                        for (std::size_t way = 0; way < 16; way += 2) shoot(way, ring->second.param1);
                        if (step > 1)
                            for (std::size_t way = 1; way < 16; way += std::size_t(step)) shoot(way, ring->second.param2);
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
                        imp_shot.src = monster.swing_stats();
                        imp_shot.src.no_hit_roll = !imp_shot.info->to_hit;
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
                    // target +- 9 (two draws of his seed), made in A1, bound to him. All
                    // one class (FUN_0063efa0, his base 0x220, once): rand(2) + the
                    // difficulty steps down the chain from baaltentacle1 (0x232,
                    // FUN_0054da60), then two rand(24) for a spot this caller drops.
                    const int count = int(monster.seed.next() % 3) + 2 + monster.difficulty;
                    const auto kind = game_data.monsters.by_id.find("baaltentacle" + std::to_string(1 + monster.seed(2) + monster.difficulty));
                    monster.seed(24); monster.seed(24);
                    for (int made = 0; born && kind != game_data.monsters.by_id.end() && made < count; ++made) {
                        const int at_x = subtile(foe.x) + int(monster.seed.next() % 18) - 9, at_y = subtile(foe.y) + int(monster.seed.next() % 18) - 9;
                        auto tentacle = make_monster(game_data, kind->second, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes), level.id);
                        std::tie(tentacle.unit.x, tentacle.unit.y) = level.nearest_free(tentacle.unit.x, tentacle.unit.y);
                        tentacle.aware = true;
                        tentacle.seed = d2d::rules::Rng{ rng.next() };
                        set_mode(game_data, tentacle, "A1", now_ms);
                        born->push_back(std::move(tentacle));
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kHydra) {
                    // FUN_005ca910: three of its summon (hydra1) at the target, each gone Param1 frames on (AI control +0x14).
                    // Each through FUN_0056d940 -> FUN_005b2f20 at radius -1 (flags 0x42): the
                    // target's subtile alone (three draws of the room's seed), else none.
                    // hydra1 is SizeX 0, so the heads share it.
                    if (const auto summoned = game_data.monsters.by_id.find(cast->summon); born && summoned != game_data.monsters.by_id.end())
                        for (int head = 0; head < 3; ++head) {
                            int at_x = 0, at_y = 0;
                            if (!spawn_spot(game_data, level, crowd, rooms_in_play, summoned->second, subtile(foe.x), subtile(foe.y), -1, room_of(level, foe.x, foe.y), at_x, at_y)) continue;
                            auto hydra = make_monster(game_data, summoned->second, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes), level.id);
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
                // auralen 300 frames, aurastat velocitypercent -100. Its
                // auratargetstate (slowed) reaches no one: the row names no
                // auraevent (+0x84 < 0, so the event loop's FUN_0056e740 never
                // runs) and no aurarangecalc.
                monster.laid_until = now_ms + 300 * 40;
            } else if (attack && foe.alive && dist <= kMeleeReach + 0.3f) {
                // Melee: block, reductions, resistances; a hit that lands
                // pays the foe's thorns (lightning ones less its resistance).
                auto stats = monster.swing_stats();                                // Weaken, Decrepify, Battle Cry, Taunt, a boss's aura: its damage %
                // Fire Hit (srvstfunc 42, FUN_005cae40, the Sand Raiders'): the hit
                // is its S1 damage and S1 elements (FUN_005a4f50 mode 8) at the
                // skill's to-hit (s1_strike). It ORs in the skill's ResultFlags,
                // HitFlags and HitClass (+0x12e / +0x130 / +0x134): all empty in
                // Fire Hit's row, so none are read.
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
                    s1_strike(stats, game_data.skills, *struck_with, skill_level(game_data, monster, monster.skill));
                } else if (smite || monster.mode == "A2") {
                    stats.a1_min = stats.a2_min; stats.a1_max = stats.a2_max; stats.to_hit = stats.to_hit2;
                    for (auto& element : stats.elements) element.mode = element.mode == "A2" ? "A1" : "";
                }
                // Charge's hit (FUN_005cf900): sure to hit (flags 9, no FUN_0057ec10), calc1 % more
                // damage, the skill's element at its level (FUN_0056e0c0). Jab's
                // (do 7, FUN_005db2d0): the same on a hit it rolls.
                // Power Strike's (start 6, FUN_005da940; MonPowerStrike / MonIceSpear):
                // a hit it rolls with no to-hit bonus, calc1 % more, the element.
                const bool power = struck_with && struck_with->srvstfunc == d2d::rules::ServerStartFunction::kElementalStrike;
                if (struck_with && (jab || power || struck_with->srvdofunc == d2d::rules::ServerDoFunction::kCharge)) {
                    const int lvl = skill_level(game_data, monster, monster.skill);
                    const int more = d2d::rules::eval_calc(game_data.skills, struck_with->calc[0], {}, monster.skill, lvl);
                    for (int* damage : { &stats.a1_min, &stats.a1_max }) *damage = std::max(*damage * (100 + more) / 100, 0);
                    const auto added = d2d::rules::row_damage(struck_with->etype, struck_with->emin, struck_with->emax, struck_with->emin_lev, struck_with->emax_lev,
                                                              struck_with->hitshift, struck_with->elen, struck_with->elen_lev, lvl);
                    if (added.etype >= 0)
                        for (auto& element : stats.elements)
                            if (element.type < 0) { element = { added.etype, 100, added.elo >> 8, added.ehi >> 8, added.elen, "A1" }; break; }
                }
                for (int* damage : { &stats.a1_min, &stats.a1_max }) *damage = std::max(*damage * (100 + monster.dmg_pct + monster.aura_dmg) / 100, 0);
                stats.to_hit += stats.to_hit * monster.aura_th / 100;
                const bool charge = struck_with && struck_with->srvdofunc == d2d::rules::ServerDoFunction::kCharge;
                const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), stats, false, rng, false, charge);
                // MonFrenzy's strike (do 109, FUN_005cdf10): a hit puts on its state
                // (FUN_005d8c70) for ln12 frames, one more charge up to its level.
                if (struck_with && struck_with->srvdofunc == d2d::rules::ServerDoFunction::kMonFrenzy && taken.hit) {
                    const int lvl = skill_level(game_data, monster, monster.skill);
                    monster.frenzy.skill = monster.skill;
                    monster.frenzy.level = std::min(monster.frenzy.level + 1, lvl);
                    monster.frenzy.until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data.skills, struck_with->auralen, {}, monster.skill, lvl), 1)) * 40;
                }
                foe.take(taken);
                if (taken.hit) foe.struck_by(type_info.hit_class);
                if (taken.hit) monprop_hit(monster, foe, false);
                if (taken.hit && smite && (struck_with->result_flags & 8)) foe.knocked_from = std::pair{ unit.x, unit.y };
                if (taken.hit && smite) foe.stun_ticks = std::max(foe.stun_ticks, d2d::rules::eval_calc(game_data.skills, struck_with->calc[1], {}, monster.skill, skill_level(game_data, monster, monster.skill)));
                if (taken.hit && monster.mana_hi > 0) foe.mana_burn += rng.range(monster.mana_lo, monster.mana_hi);   // Mana Burn
                if (taken.hit) cursed_hit(game_data, monster, foes, foe);
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
        if (mode_over()) {
            if (monster.leaping) { monster.leaping = false; monster.leap_until = 0; unit.x = monster.leap_to_x; unit.y = monster.leap_to_y; }
            mode_ends();
            monster.skill = -1;
        } else if (!think_due()) return false;
    }
    // A monster's velocitypercent (stat 0x43) starts at 75, not 100: its
    // stat initialiser FUN_00573cb0 ends in SetStat(unit, 0x43, 0x4b)
    // (0x573f16). Chilled, coldeffect goes on it (FUN_0057af80) with its
    // think's pace, Spider Lay's -100 % and the rest; the walk % is their
    // sum, at least 25 (FUN_00623f50, 0x62414e): laid (-100 %), it
    // creeps at that floor in game.exe too.
    const int chill = now_ms < monster.chill_until ? type_info.diff[std::size_t(monster.difficulty)].cold_effect : 0;
    const int pace = monster.move_pct - (now_ms < monster.laid_until ? 100 : 0);
    const float walk = cells_per_sec(float(type_info.velocity)) * elapsed * float(std::max(kMonsterVelocityPct + monster.speed_pct + monster.boss_speed + pace + chill, 25)) / 100;
    // A moving mode's path velocity is MonStats Velocity (FUN_00621360,
    // +0x32) x the walk %, RN too (FUN_00623f50, 0x6241a9); Run only comes
    // in through an AI's pace (Run x 100 / Velocity - 100) or a charge.
    const float run = walk;
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
        // The teleport mod (teleport_mod) for an AI think() doesn't run.
        if (now_ms >= monster.next_act && teleport_mod(game_data, level, monster, int(dist * 5), crowd, rooms_in_play, now_ms)) return false;
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

Monster make_monster(const GameData& game_data, int type, float x, float y, d2d::rules::Rng& rng, int difficulty, bool stats, int players, int level_id) {
    const auto& type_info = game_data.monsters.types[std::size_t(type)];
    Monster monster;
    monster.type = type;
    monster.level_id = level_id;
    monster.players = players;
    monster.killable = type_info.killable;
    // FUN_005b2a00: Align 1 good (stat 0xac 2), 2 neutral (1), else evil.
    monster.align = type_info.align == 1 ? 2 : type_info.align == 2 ? 1 : 0;
    monster.row_align = type_info.align;
    monster.npc = game_data.mon_npc[std::size_t(type)];
    for (std::size_t layer = 0; layer < 16; ++layer)
        if (!type_info.parts[layer].empty()) monster.npc.comp[layer] = type_info.parts[layer][std::size_t(rng(int(type_info.parts[layer].size())))];
    monster.unit.x = monster.home_x = x;
    monster.unit.y = monster.home_y = y;
    monster.unit.dir = rng(16);
    monster.unit.shape = monster.npc.shape;
    monster.unit.wait_until = std::uint32_t(rng(4000));
    if (stats) monster.stats = d2d::rules::monster_stats(game_data.monsters, type, difficulty, rng, 0, players, area_monster_level(game_data, level_id, difficulty));
    // FUN_00573cb0 keeps a zero roll (the town folk, critters, traps, Baal on
    // the stairs). game.exe's dead test is the mode (FUN_005541b0: DT / DD),
    // so it lives and thinks; d2d's is life, so it holds one point, `lifeless`:
    // any landed hit kills it, as life <= 0 after one does (0x57c8e2).
    monster.lifeless = monster.stats.hit_points <= 0;
    monster.stats.hit_points = std::max(monster.stats.hit_points, 1);
    monster.hit_points = monster.last_hp = monster.stats.hit_points;
    monster.difficulty = std::clamp(difficulty, 0, 2);
    // Its MonProp row; spawn_monsters takes it again on the unit seed. Here
    // the row draws on the seed it has yet, which no shipped row feels: 1.14d
    // MonProp has no chance column set, and its only min..max slots are
    // quillrat6..8's thorns (N, H), made only by spawn_monsters (no MonStats
    // spawn / minion, Skills summon or quest makes them).
    monster.props = d2d::rules::monster_props(game_data.rules, type_info, monster.difficulty, monster.seed);
    return monster;
}

// Poison hit (FUN_005a1e00): poisonmindam / maxdam (stats 57 / 58) =
// MonLvl damage x MonUMod constants rows 0x10 / 0x13 + difficulty, or
// 0x1c / 0x1f with the hook's unique flag; poisonlength (59) = (mlvl x 5 +
// 150) x 2.
void cursed_hit(const GameData& game_data, Monster& monster, std::span<Foe> foes, const Foe& struck) {
    if ((monster.boss != d2d::rules::Boss::unique && monster.boss != d2d::rules::Boss::superunique)
        || !std::ranges::contains(monster.mods, d2d::rules::umod::curse) || (monster.seed.next() & 3) == 0) return;
    const int curse_level = std::max(monster.stats.level / 5 + 1, 1);
    const auto* amplify = game_data.skills.get(66);
    const int radius = std::clamp(amplify ? d2d::rules::eval_calc(game_data.skills, amplify->aurarange, {}, 66, curse_level) : 1, 1, 40);
    for (auto& near_foe : foes)
        if (near_foe.alive && !near_foe.of && std::hypot(near_foe.x - struck.x, near_foe.y - struck.y) * 5 <= float(radius))
            near_foe.cursed = { 66, curse_level };
}

bool swaps_ai(const GameData& game_data, const Monster& monster) {
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    using d2d::rules::Boss;
    return !monster.uninterruptable() && type_info.can_walk && !type_info.boss_column && type_info.switch_ai && !(type_info.base == 0x1ec && monster.riding)
        && (monster.targetable() || !monster.alive()) && monster.boss != Boss::unique && monster.boss != Boss::superunique;
}

void ai_toggle(const GameData& game_data, Monster& monster, int special) {
    if (d2d::rules::ai_toggle(monster.special, monster.other, { &monster.ai_state, &monster.ai_state2, &monster.ai_state3 }, special,
                              d2d::rules::has_other_think(game_data.monsters.types[std::size_t(monster.type)].ai_name)))
        monster.ai_command = 0;                                    // FUN_0058ede0: its command list freed
}

d2d::rules::GearSum spawn_gear(const GameData& game_data, std::span<const d2d::rules::SpawnGear> gear, int difficulty) {
    std::vector<d2d::d2s::Item> items;
    for (const auto& worn : gear) {
        if (!game_data.rules.item_info.contains(worn.code)) continue;
        d2d::rules::Rng unit_seed{ worn.unit_seed }, item_seed{ worn.item_seed };
        auto item = d2d::rules::generate_item(game_data.rules, worn.code, worn.ilvl, worn.quality, item_seed, &unit_seed, nullptr, false, difficulty);
        item.location = d2d::d2s::item_location::kEquipped;
        item.slot = worn.loc;
        items.push_back(std::move(item));
    }
    return d2d::rules::gear_sum(game_data.rules, items);
}

void wear_gear(d2d::rules::MonStats& stats, const d2d::rules::GearSum& gear) {
    // FUN_0057b420 reads the unit's stats whatever the mode, so a two-handed
    // weapon's damage stands in for every mode's (A1 / A2 / S1).
    // ponytail: the items' other stats (resistances, life, to-hit, their
    // elements) don't reach a made monster yet.
    for (auto [low, high] : { std::pair{ &stats.a1_min, &stats.a1_max }, std::pair{ &stats.a2_min, &stats.a2_max }, std::pair{ &stats.s1_min, &stats.s1_max } })
        std::tie(*low, *high) = d2d::rules::gear_damage(gear, *low, *high);
    stats.armor_class += int(gear.defense + gear.sum[d2d::d2s::kArmorClass]);
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
               int difficulty, d2d::rules::Rng& rng, int players, const d2d::rules::GearSum* gear) {
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    const auto boss = d2d::rules::boss_stats(game_data.umods, type_info, kind, mods, difficulty);
    monster.stats = d2d::rules::monster_stats(game_data.monsters, monster.type, difficulty, rng, boss.level_add, players, area_monster_level(game_data, monster.level_id, difficulty));
    if (gear) wear_gear(monster.stats, *gear);
    monster.boss = kind;
    monster.mods = mods;
    monster.super = super;
    monster.name_seed = name_seed;
    // FUN_005a0d20 scales max life in 256ths, first for hpmultiply and then for the champion kind.
    int life = monster.stats.hit_points * 256;
    life = int(std::uint32_t(life) + std::uint32_t(d2d::rules::mul_div(life, boss.hp_pct, 100)));
    life = int(std::uint32_t(life) + std::uint32_t(d2d::rules::mul_div(life, boss.life_after_pct, 100)));
    monster.lifeless = life <= 0;                                       // as make_monster's
    monster.set_life_fine(std::max(life, 256));
    monster.stats.hit_points = monster.hit_points;
    monster.max_life_frac = monster.life_frac;
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
    if (super == 6) { monster.half_freeze = true; monster.special = 0xd; }   // FUN_005a49b0 case 6, the Countess: FUN_00639db0(0x76) (state 118), FUN_005b0e00(0xd)
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

bool hurt(const GameData& game_data, Monster& monster, int damage, std::uint32_t now_ms, int fraction, int hit_class, bool marked) {
    if (!monster.targetable() || (damage <= 0 && fraction <= 0 && !monster.lifeless)) return false;
    // This blow's own size in 256ths, for its get-hit, before the carried
    // part of a point joins it.
    const std::int64_t blow_fine = (std::int64_t(std::max(damage, 0)) << 8) + std::max(fraction, 0);
    // Life is in 256ths in game.exe (0057c852): carry the part of a point,
    // and a life under one point after a hit is dead (0057c865).
    monster.life_frac += std::max(fraction, 0);
    damage += monster.life_frac >> 8;
    monster.life_frac &= 255;
    monster.hit_points -= damage;
    if (monster.hit_points < 1 || (monster.hit_points == 1 && monster.life_frac > 0) || monster.lifeless) { monster.hit_points = 0; monster.life_frac = 0; }   // dead is 0 (0057c86c)
    monster.aware = true;
    if (!monster.alive()) set_mode(game_data, monster, "DT", now_ms);
    // Its get-hit (FUN_0057cb00 from FUN_0057cee0's monster branch): the share of
    // max life by the blow's hit class, rolled on its own seed (unit +0x20), then
    // MonStats2's mGH (0x57cbda, after the rolls at 0x57cb95 / 0x57cbc0: one without
    // it still rolls); a stunned one (state 0x15) takes it without the roll (0057d0b9).
    else if (now_ms < monster.stun_until
             || (d2d::rules::get_hit(monster.max_life_fine(), blow_fine, hit_class, marked, monster.seed)
                 && game_data.monsters.types[std::size_t(monster.type)].can_get_hit))
        set_mode(game_data, monster, "GH", now_ms);
    return !monster.alive();
}

void heal(Monster& monster, int points) {
    if (points <= 0 || !monster.alive()) return;
    const int healed = monster.hit_points + points;
    monster.hit_points = std::min(healed, monster.stats.hit_points);
    if (healed > monster.stats.hit_points) monster.life_frac = monster.max_life_frac;
    else if (healed == monster.stats.hit_points) monster.life_frac = std::max(monster.life_frac, monster.max_life_frac);
}

bool follow_path(const Level& level, UnitState& unit, float step, const Crowd& crowd, bool* blocked) {
    auto& path = unit.path;
    auto& x = unit.x;
    auto& y = unit.y;
    auto& dir = unit.dir;
    // FUN_00650660: one step a frame; landing on a point (FUN_00650090: within the step) ends the frame there.
    if (!path.empty() && step > 0) {
        const auto [target_x, target_y] = path.front();
        const float dx = target_x - x, dy = target_y - y, dist = std::hypot(dx, dy);
        if (dist > 0.05f) dir = direction16(dx, dy);
        if (dist <= step) { x = target_x; y = target_y; path.erase(path.begin()); return !path.empty(); }
        const float next_x = x + dx / dist * step, next_y = y + dy / dist * step;
        if (level.unit_blocked(next_x, next_y, unit.shape) || crowd.at(next_x, next_y, &unit)) {
            // FUN_00650150: a blocked subtile ends the path on a subtile's centre, the one it's on here.
            x = (std::floor(x * 5) + 0.5f) / 5; y = (std::floor(y * 5) + 0.5f) / 5;
            path.clear();
            if (blocked) *blocked = true;
            return false;
        }
        x = next_x; y = next_y;
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
    monster.element_on[2] = true;   // set_mode rolled the slot's last element; this one is the swing's, 100 %
}

}  // namespace d2d::game
