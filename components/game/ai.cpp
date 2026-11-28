// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for ai.hpp: monsters, NPCs and the merc: spawning, paths, the AI step.
#include "ai.hpp"
#include "ai/units.hpp"

#include "ai/internal.hpp"
#include "gamedata.hpp"
#include "level.hpp"
#include "log.hpp"
#include "npc.hpp"
#include "spawning.hpp"

#include <chat_commands.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <item_gen.hpp>
#include <leap_wait.hpp>
#include <level_ids.hpp>
#include <merc.hpp>
#include <missiles.hpp>
#include <mon_equip.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>
#include <overlays.hpp>
#include <pets.hpp>
#include <points.hpp>
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
#include <functional>
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
                                                 const Crowd& crowd, const UnitState* self, const UnitState* target) {
    auto sub = [](float value) { return int(std::floor(value * 5)); };
    auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
    const std::pair from{ sub(x), sub(y) };
    Crowd lifted = crowd;                     // flag 0x800: the target's stamp lifted too (FUN_00649970 -> FUN_00649560)
    std::erase(lifted.units, target);
    const auto blocked = [&](int subtile_x, int subtile_y) {
        return level.unit_blocked(centre(subtile_x), centre(subtile_y), self ? self->shape : UnitShape{}) || lifted.at(centre(subtile_x), centre(subtile_y), self);
    };
    // Type 7 has path flag 0x1000 (PATHTYPEFLAGS 0x6eb690): a blocked end is walked back toward
    // the start and pushed on (FUN_00648120), and with no free spot there's no path.
    int to_x = sub(goal_x), to_y = sub(goal_y);
    if (blocked(to_x, to_y) && !d2d::rules::end_walk_back(from.first, from.second, to_x, to_y, true, blocked)) return {};
    const auto steps = d2d::rules::player_path(from.first, from.second, to_x, to_y, to_unit ? 1 : 0, to_unit, blocked);
    // A point given twice (the toward pather's cut-short subtile) stays: the walk passes it (FUN_0064fe40).
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

// FUN_00648eb0: a door's 0x800 (BlocksVis). With the path's velocity
// (+0x7c) 0 the word under the unit's test shape now (FUN_0064d9b0); else
// +0x54 as its steps left it (UnitState::path_word). A monster's velocity is
// its MonStats Velocity (+0x32 << 8, FUN_005735a0) from the start, and only
// moving modes change it (FUN_00623f50), so one that walks reads +0x54.
bool on_door(const Level& level, const UnitState& unit, bool moving) {
    if (moving) return (unit.path_word & 0x0800) != 0;
    UnitShape door_test = unit.shape;
    door_test.mask = 0x0800;
    return level.unit_blocked(unit.x, unit.y, door_test);
}

// A frame of a walk along `points` (subtiles) from points[point], at
// `velocity` (path +0x7c): rules::walk_frame (FUN_00650840) on the unit's 16.16
// position, a new subtile tested as the pathers test it (FUN_0064ff90 ->
// FUN_0064eda0: the walls and the others' stamps under its mask, its own
// off; a mask of 0x3401, an opendoors monster's, tests as 0x3c01, so a door
// stops it). Each new subtile's door bit 0x800 under that shape is or'ed into
// path +0x54 (UnitState::path_word), which a path's making zeroes
// (FUN_00649970). False when the walk is over, the unit on a subtile's centre and
// `point` past the last; `hit` when blocked. Its facing aims at the next
// point not under it (FUN_0064fe40 -> FUN_006485f0: rules::aim_direction),
// as the path is made and after each step, and turns a step a frame toward
// that (FUN_00648640, the client's); `dir` is it drawn in 16 (FUN_00600c90:
// missile_direction). A `dir` set outside a walk is where it starts from.
// A monster's turn still going when the walk ends goes on in its later
// frames (turn_frame: the client's FUN_00463390 -> FUN_00648640 each frame).
// An NPC, a pet and the merc are monster units too: theirs goes on the same way.
// An action's facing outside a walk turns the same way (turn_to: FUN_006485f0).
// ponytail: the client's turn of a town NPC toward the local player within 7
// (FUN_004b13a0's FUN_00649ef0 call) isn't ported; a player's facing is still
// set at once (FUN_006485f0 turns a type-0 path too; its callers not traced).
bool path_frame(const Level& level, UnitState& unit, std::span<const std::pair<int, int>> points, std::size_t& point, int velocity,
                const Crowd& crowd, bool* hit = nullptr) {
    auto centre = [](int coord) { return (float(coord) + 0.5f) / 5; };
    auto& facing = unit.facing;
    if (missile_direction(facing.now, 16) != unit.dir) {
        const auto sector = std::ranges::find(kDir16FromSector, unit.dir) - std::begin(kDir16FromSector);
        facing.now = facing.goal = std::uint8_t(sector * 4 & 0x3f);
    }
    auto aim = [&] {
        const auto fine = [](int coord) { return (std::uint32_t(coord) << 16) + 0x8000; };
        for (auto next = point; next < points.size(); ++next)
            if (const auto to_x = fine(points[next].first), to_y = fine(points[next].second); to_x != unit.x.raw || to_y != unit.y.raw) {
                facing.aim(d2d::rules::aim_direction(unit.x.raw, unit.y.raw, to_x, to_y));
                return;
            }
    };
    aim();
    UnitShape step_test = unit.shape;                                  // FUN_0064ff90: 0x3401 -> 0x3c01
    if (step_test.mask == 0x3401) step_test.mask = 0x3c01;
    UnitShape door_test = unit.shape;
    door_test.mask = 0x0800;
    const bool going = d2d::rules::walk_frame(unit.x.raw, unit.y.raw, points, point, velocity, [&](int at_x, int at_y) {
        if ((step_test.mask & 0x0800) && level.unit_blocked(centre(at_x), centre(at_y), door_test)) unit.path_word |= 0x0800;
        return level.unit_blocked(centre(at_x), centre(at_y), step_test) || crowd.at(centre(at_x), centre(at_y), &unit);
    }, hit);
    if (going) aim();
    facing.turn();
    if (!going && !unit.turns_on) facing.now = facing.goal;
    unit.dir = missile_direction(facing.now, 16);
    return going;
}


}  // namespace

// A frame's turn off a walk (FUN_00648640 each client frame, FUN_004b13a0):
// on toward the goal a walk or turn_to left; a `dir` set since (face) is taken at once.
void turn_frame(UnitState& unit) {
    auto& facing = unit.facing;
    if (missile_direction(facing.now, 16) != unit.dir) {
        face(unit, unit.dir);
        return;
    }
    facing.turn();
    unit.dir = missile_direction(facing.now, 16);
}

// A facing taken at once (a unit as it's made), now and goal both as
// FUN_006488a0 sets them: the sector in 64, so no pending turn carries on.
void face(UnitState& unit, int dir) {
    unit.dir = dir;
    const auto sector = std::ranges::find(kDir16FromSector, dir) - std::begin(kDir16FromSector);
    unit.facing.now = unit.facing.goal = std::uint8_t(sector * 4 & 0x3f);
}

// FUN_00649ef0(path, x, y, 0): the 64-step aim from where it is at the
// subtile's centre (FUN_0064fe10), handed to FUN_006485f0, which for a
// player's or monster's path only sets the goal (+0x65) and the step (+0x66,
// DIFFDIRECTIONS); turn_frame (FUN_00648640) turns it a step a frame.
// The client's MonsterMessage (FUN_004aff60) does it for a mode at a unit
// (FaceTarget FUN_00465bf0 with EDX 0: commands 5, 0xa, 0xd, 0xf, 0x10, 0x12,
// 0x1b, 0x1d; not for FUN_00463860's 0xe7) or at a point (FUN_00621c00: 4,
// 0xb, 0xc, 0xe, 0x11, 0x1a, 0x1c).
void turn_to(UnitState& unit, int to_x, int to_y) {
    unit.facing.aim(d2d::rules::aim_direction(unit.x.raw, unit.y.raw, (std::uint32_t(to_x) << 16) + 0x8000, (std::uint32_t(to_y) << 16) + 0x8000));
}

bool set_off(const Level& level, UnitState& unit, float goal_x, float goal_y, bool to_unit, const Crowd& crowd) {
    unit.goal_x = goal_x; unit.goal_y = goal_y; unit.to_unit = to_unit;
    unit.budget = 0x14;
    unit.path = monster_walk(level, unit, crowd);
    unit.path_points = int(unit.path.size());
    unit.path_word = 0;
    return !unit.path.empty();
}

bool walk_on(const Level& level, UnitState& unit, int velocity, const Crowd& crowd, const UnitState* target) {
    if (unit.path.empty()) return false;
    if (target) {
        const int x = subtile(unit.x), y = subtile(unit.y), at_x = subtile(target->x), at_y = subtile(target->y);
        const int distance = d2d::rules::unit_distance(at_x - x, at_y - y, unit.shape.size, target->shape.size);
        const int check = d2d::rules::chase_check(true, distance, 0, true, subtile(unit.goal_x) - at_x, subtile(unit.goal_y) - at_y,
                                                  unit.path_points - int(unit.path.size()), unit.path_points, false, unit.budget);
        if (check == 0) unit.path.clear();                     // at it, or the budget spent: over (FUN_006507b0)
        if (check == 2) {                                      // FUN_00650350: type 0xd, then 0xf, to where it is now
            unit.goal_x = target->x; unit.goal_y = target->y;
            unit.path = monster_walk(level, unit, crowd);
            unit.path_points = int(unit.path.size());
            unit.path_word = 0;
        }
    }
    return follow_path(level, unit, velocity, crowd);
}

// A random unique's name (the client's FUN_004ac870): on {name seed, 666},
// a suffix then a prefix into string 0x6b9 ("%0 %1"); then rand(100) < 50
// builds it again, appellation, suffix, prefix, into 0x6ba ("%0 %1 %2").
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
    auto joined = region;
    for (const auto& spawn : spawns) {
        if (spawn.type < 0 || std::size_t(spawn.type) >= game_data.mon_npc.size()) continue;
        const bool boss = spawn.boss != d2d::rules::Boss::none;
        const auto& type_info = game_data.monsters.types[std::size_t(spawn.type)];
        // Its unit seed is the room's draw (Spawn::seed); the stat init's draws on it are make_monster's,
        // a boss's life make_boss's on the same seed, then the rest (finish_unit_seed). The region's
        // joins were made as the spawns were (objgroups.cpp `made`).
        auto monster = make_monster(game_data, spawn.type, (float(spawn.x) + 0.5f) / 5, (float(spawn.y) + 0.5f) / 5, rng, difficulty, !boss, players, level_id,
                                    d2d::rules::Rng{ spawn.seed }, &joined);
        monster.cage = spawn.cage;
        monster.preset = spawn.preset;
        monster.listed = spawn.cage < 0;                              // a prisoner is in no target list (Monster::listed)
        const auto gear = spawn_gear(game_data, spawn.gear, difficulty);
        if (!boss && !spawn.gear.empty()) { wear_gear(monster.stats, gear); wear_gear_stats(monster, gear); }
        if (boss) {
            make_boss(game_data, monster, spawn.boss, spawn.mods, spawn.super, spawn.name_seed, difficulty, monster.seed, players, spawn.gear.empty() ? nullptr : &gear);
            finish_unit_seed(game_data, monster);
        }
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
        monster.path = spawn.path;
        out.push_back(std::move(monster));
    }
    return out;
}

// FUN_005a68e0's mode change; into death of a Reanimated Horde, MonUMod 34's hook 1 (FUN_005a3840) queues its
// event 7 (`rise_at`) here, so a death between two Fight::boss_events passes still queues it.
void set_mode(const GameData& game_data, Monster& monster, std::string_view mode, std::uint32_t now_ms) {
    const bool into_death = mode == "DT" && monster.mode != "DT";
    // FUN_005a68e0: a last command 0x13 or over loses 0x10 and the new one is dropped: a hit's 0x13
    // is 3 (GH), so it reads as got hit through one more mode.
    if (monster.left_mode == "hit") monster.left_mode = "GH";
    else if (monster.mode != "NU") monster.left_mode = monster.mode;
    monster.mode = mode;
    if (into_death && monster.type >= 0 && std::ranges::contains(d2d::rules::family_mods(game_data.monsters.types[std::size_t(monster.type)].base), d2d::rules::umod::ai_after_death)) {
        const auto& per_difficulty = game_data.monsters.types[std::size_t(monster.type)].diff[std::size_t(std::clamp(monster.difficulty, 0, 2))];
        const auto queued = d2d::rules::ai_after_death_queue(0, monster.align, per_difficulty.aip[7], per_difficulty.aip[0], monster.seed);
        if (queued.cleared) monster.rise_at = 0;
        if (queued.frames) monster.rise_at = now_ms + std::uint32_t(*queued.frames) * 40;
    }
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
    monster.leap_hold_row = -1; monster.seq_shift_ms = 0;     // no wait at a landing in the new mode
    monster.unit.mode_ms = now_ms;
    monster.unit.walking = mode == "WL" || mode == "RN";
    monster.scheduled_action_ms.reset();
    monster.scheduled_anim_position.reset();
    monster.scheduled_older_think_ms.reset();
    // FUN_005539b0 queues the action and mode end from the fixed-point
    // rate at mode start. Chill (FUN_0057af80) refreshes the rate without
    // replacing those events. Verified for Council S1/A1 and standalone Mauler A2.
    constexpr std::size_t first_council_id = 345, council_count = 3;
    const bool valid_type = monster.type >= 0 && std::size_t(monster.type) < game_data.monsters.types.size();
    const bool council_attack = valid_type && (mode == "S1" || mode == "A1")
        && game_data.mon_bin.size() >= first_council_id + council_count
        && std::ranges::contains(std::span(game_data.mon_bin).subspan(first_council_id, council_count), std::size_t(monster.type));
    const bool mauler_a2 = mode == "A2" && valid_type
        && game_data.monsters.types[std::size_t(monster.type)].ai_name == "DeathMauler";
    if (monster.skill < 0 && (council_attack || mauler_a2)) {
        const auto& timing = game_data.npc_timing(monster.npc, mode);
        const int rate = mode_rate(game_data, monster, mode);
        if (rate > 0 && timing.action >= 0 && timing.frames > 0) {
            const auto deadline = [now_ms, rate = std::uint32_t(rate)](std::uint32_t frame) {
                const auto ticks = (std::uint64_t(frame) * 256 + rate - 1) / rate;
                return now_ms + std::uint32_t(ticks * 40);
            };
            monster.scheduled_action_ms = now_ms + action_due_ms(game_data, monster, mode);
            if (monster.think_pending()) monster.scheduled_older_think_ms = monster.next_act;
            monster.scheduled_anim_position = 0;
            monster.scheduled_anim_ms = now_ms;
            monster.mode_until = deadline(timing.frames);
            return;
        }
    }
    // A monster's FHR is its MonProp's (props.fhr): FUN_00625500 reads the
    // unit's full stat 99, but no Skills.txt aurastat / passivestat gives it.
    monster.mode_until = mode == "NU" || mode == "WL" || mode == "DD" ? 0
        : mode == "GH"                                                ? now_ms + get_hit_ms(game_data.npc_timing(monster.npc, mode), monster.props.fhr)
                                                                      : now_ms + game_data.npc_timing(monster.npc, mode).length_ms() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, mode));
}

int mode_end_event(const GameData& game_data, const Monster& monster, std::string_view mode, int frame) {
    return frame + int(game_data.npc_timing(monster.npc, mode).length_ms() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, mode)) / 40) + 1;
}

namespace {

constexpr std::array<std::string_view, 16> kMonModes{ "DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN" };

int mon_mode(std::string_view mode) {
    const auto found = std::ranges::find(kMonModes, mode);
    return found == kMonModes.end() ? 1 : int(found - kMonModes.begin());
}

int anim_speed(const GameData& game_data, const Monster& monster, std::string_view mode) {
    const auto& anim = game_data.npc_timing(monster.npc, mode);
    return int(anim.speed ? anim.speed : anim.cof_speed ? anim.cof_speed : 256);
}

bool npc_row(const GameData& game_data, const Monster& monster) {
    return monster.type >= 0 && std::size_t(monster.type) < game_data.mon_is_npc.size() && game_data.mon_is_npc[std::size_t(monster.type)];
}

}  // namespace

int mode_rate(const GameData& game_data, const Monster& monster, std::string_view mode) {
    const auto* skill = game_data.skills.get(monster.skill);
    const bool known = monster.type >= 0 && std::size_t(monster.type) < game_data.monsters.types.size();
    d2d::rules::ModeRateIn rate_in{ .mode = mon_mode(mode), .low_class = monster.type >= 0 && monster.type < 0x19a, .npc = npc_row(game_data, monster),
                               .use_attack_rate = skill && skill->use_attack_rate, .speed = anim_speed(game_data, monster, mode),
                               .velocity_pct = monster.walk_pct, .fhr = monster.props.fhr, .fcr = monster.props.fcr, .ias = monster.props.ias,
                               .attack_rate = 100 + monster.attack_pct + monster.aura_rate, .other_rate = 100 + monster.anim_pct,
                               .skill_flags = monster.diab_run >= 2 ? 1 : 0 };   // DiabRun's do (FUN_005cd380, 0x5cd498) sets 1
    if (known) rate_in.base = { game_data.monsters.types[std::size_t(monster.type)].walk_base, game_data.monsters.types[std::size_t(monster.type)].run_base };
    return d2d::rules::monster_mode_rate(rate_in);
}

// Whether a mode's rate in game.exe is other than a % of its AnimData speed: a moving one (FUN_006214a0:
// WL, RN; S1..S4 for a class under 0x19a that isn't a MonStats npc), KB, BL, DT, DD.

// FUN_006214a0 also makes SQ (and S1..S4 for a class at or above 0x19a) a moving mode when the unit's
// skill instance has flags & 1 and not 0x1000 (FUN_006446a0). Of the server do-funcs that set 1 (Whirlwind
// FUN_005d8f50, Charge's end FUN_005cf900, DiabRun FUN_005cd380, FUN_005cb940, FUN_005da020), only
// DiabRun's plays one of those modes (SQ); d2d keeps it as diab_run past its wind-up.
bool own_rate_mode(const GameData& game_data, const Monster& monster, std::string_view mode) {
    const int index = mon_mode(mode);
    const bool skill_mode = index >= 8 && index <= 11, flagged = monster.diab_run >= 2;
    return index == 0 || index == 2 || index == 6 || index == 12 || index == 13 || index == 15
        || (skill_mode && monster.type >= 0 && monster.type < 0x19a && !npc_row(game_data, monster))
        || ((skill_mode || index == 14) && flagged && !npc_row(game_data, monster));
}

int chill_pct(const GameData& game_data, const Monster& monster) {
    if (monster.type < 0 || std::size_t(monster.type) >= game_data.monsters.types.size()) return 0;
    return game_data.monsters.types[std::size_t(monster.type)].diff[std::size_t(std::clamp(monster.difficulty, 0, 2))].cold_effect;
}

int anim_rate_pct(const GameData& game_data, const Monster& monster, std::string_view mode) {
    if (mode != "GH" && own_rate_mode(game_data, monster, mode)) {
        const int speed = anim_speed(game_data, monster, mode), rate = std::max(mode_rate(game_data, monster, mode), 1);
        return int((std::int64_t(rate) * 100 + speed - 1) / speed) - 100;
    }
    const auto* skill = game_data.skills.get(monster.skill);
    return anim_rate_pct(monster, mode, skill && skill->use_attack_rate);
}

// FUN_005539b0 walks the animation from FUN_00623b10's start frame (0 for a
// monster that isn't transformed) at the unit's rate (+0x4c, FUN_00623f50:
// mode_rate) and files the event frame's marker on the first tick that
// reaches it, rules::frame_tick (council-s1.md).
// ponytail: a mode with no event frame files no marker there (flags 1, 2, 4
// only): FUN_005539b0 has no fallback (only the end, type 1), FUN_005a8030
// (the end) runs no do, and FUN_005a75c0's skill path (FUN_0056faf0 ->
// FUN_0056f640) runs only the srvstfunc (table 0x732140). So with no MonSeq
// sequence (FUN_006634c0's rows instead) such a mode's action never runs:
// stock 1.14d's ZMS1HTH plays MonTeleport (srvdofunc 98, no srvstfunc) without
// the teleport. 25 monster modes have no marker in AnimData (ZMS1 / ZMS2, FAS1
// / FAS2, MMS1, SKS1*, SUS1, SMS1 and more); which of them play through a
// sequence isn't checked yet, so d2d still strikes such a mode halfway; with no
// action there, test_game_missiles' teleporting unique zombie would not land,
// so an oracle run of game.exe's own update on it settles which before a port.
std::uint32_t action_due_ms(const GameData& game_data, const Monster& monster, std::string_view mode) {
    const auto& timing = game_data.npc_timing(monster.npc, mode);
    if (timing.action < 0) return timing.action_ms() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, mode));
    return std::uint32_t(d2d::rules::frame_tick(timing.action, 0, std::max(mode_rate(game_data, monster, mode), 1))) * 40;
}

// A monster's war cry nova (do 68: Madawc's Shout, the overseers'): hit 18 (FUN_005ab0b0) on each unit
// it reaches, an ally of its owner by FUN_00554de0 (same alignment, FUN_00650d70), runs FUN_005d8290
// on it: the skill's aurastate for auralen frames at the missile's level. Its 64 strike nothing else
// (visual_only), so they fly on.
void war_cry_reach(const GameData& game_data, const Level& level, std::span<const Missile> missiles, std::span<Monster> monsters, std::uint32_t now_ms) {
    for (const auto& missile : missiles) {
        if (missile.friendly || !missile.info || missile.info->hit_func != 18 || missile.skill < 0) continue;
        const auto* cry = game_data.skills.get(missile.skill);
        const auto owner = std::ranges::find(monsters, missile.owner, &Monster::id);
        if (!cry || cry->srvdofunc != d2d::rules::ServerDoFunction::kWarCry || cry->aurastate.empty() || owner == monsters.end()) continue;
        const int frames = std::max(d2d::rules::eval_calc(game_data.skills, cry->auralen, {}, missile.skill, missile.level), 1);
        for (auto& ally : monsters)
            if (&ally != &*owner && ally.alive() && ally.align == owner->align && ally.type >= 0 && std::size_t(ally.type) < game_data.monsters.types.size()
                && missile_struck(level, missile, ally.unit.x, ally.unit.y, game_data.monsters.types[std::size_t(ally.type)].size))
                put_state(game_data, ally, { missile.skill, missile.level, now_ms + std::uint32_t(frames) * 40 }, now_ms);
    }
}

std::vector<UnitState> npc_start(const Level& level) {
    std::vector<UnitState> out;
    for (const auto& npc : level.npcs) out.push_back({ .x = Fine(npc.x), .y = Fine(npc.y), .shape = npc.shape });
    return out;
}

// A town NPC's first think, 2 frames after `now_ms`: game.exe files it as
// the NPC's room comes into play (its first client, FUN_0053a8e0, or made
// in one already in play, FUN_00554850) through FUN_00573780: mode NU,
// event 2 at frame + 2 (0x5737c0).

// World files the states its rooms grow in a frame; npc_patrol files the
// rest the first time it meets them.
void file_first_think(UnitState& state, std::uint32_t now_ms) {
    constexpr std::uint32_t kFrameMs = 40;
    state.filed = true;
    state.wait_until = now_ms + 2 * kFrameMs;                 // NU: event 2 at frame + 2 (FUN_00573780, 0x5737c0)
}

void npc_patrol(const GameData& game_data, const Level& level, std::vector<UnitState>& npcs, std::array<int, 3> busy,
                std::uint32_t now_ms, float /* elapsed: a call is a frame */, const Crowd& crowd, std::span<const UnitState* const> visitors, bool guard_aside,
                std::span<const Monster> monsters) {
    constexpr std::uint32_t kFrameMs = 40;
    for (std::size_t i = 0; i < npcs.size() && i < level.npcs.size(); ++i) {
        const auto& npc = level.npcs[i];
        if (npc.town_ai == TownAi::none || ((npc.town_ai == TownAi::npc || npc.town_ai == TownAi::towner) && npc.path.empty())) continue;
        auto& state = npcs[i];
        state.turns_on = true;                                      // a monster unit's turn, a step a frame (turn_frame)
        if (!state.walking) turn_frame(state);
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
            if (!walk_on(level, state, path_velocity(int(npc.velocity)), crowd)) {
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
        // Flavie (Navi): her search's flag word as set (under 4; town-npcs.md "Flavie"), the visitor busy as its talking above.
        // FUN_005e7e20 -> FUN_005ddc30: A1 when its hostile monster pick is under 25 unit-distance.
        const bool navi_enemy = npc.town_ai == TownAi::navi && std::ranges::any_of(monsters, [&](const Monster& monster) {
            return monster.align != 2 && monster.targetable()
                && d2d::rules::unit_distance(subtile(monster.unit.x) - x, subtile(monster.unit.y) - y, npc.shape.size, monster.unit.shape.size) < 25;
        });
        // ponytail: this existence test does not rank FUN_005ddc30's primary / secondary picks, and the town-NPC path does not file Flavie's action-frame arrow.
        const auto act = npc.town_ai == TownAi::jarjar ? d2d::rules::jarjar_think(state.brain, x, y, int(now_ms / kFrameMs), guard_aside, guard_aside, visitor)
                       : npc.town_ai == TownAi::vendor ? d2d::rules::vendor_think(npc.seed)
                       : npc.town_ai == TownAi::navi   ? d2d::rules::navi_think(state.brain, npc.seed, listed, visitor, visitor.present && busy[0] >= 0,
                                                                                visitor.present && visitor.distance < 4, navi_enemy)
                                                       : d2d::rules::npc_think(state.brain, npc.seed, points, x, y, npc.hc_idx, npc.modes, 1, visitor, npc.town_ai == TownAi::towner);
        if (act.face >= 0) state.facing.aim(act.face);         // FUN_00648820 (FUN_006485f0's, with a range assert): a goal, turned a frame at a time
        state.greets = state.greets || act.greet;          // unit sound 0x12: the client's greeting (FUN_004e0590 kind 1)
        switch (act.kind) {
        case d2d::rules::NpcAct::Kind::stand:
            state.wait_until = act.frames < 0 ? std::numeric_limits<std::uint32_t>::max() : now_ms + std::uint32_t(act.frames) * kFrameMs;
            break;
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
        case d2d::rules::NpcAct::Kind::attack:                  // A1; its action-frame arrow is not ported
            state.mode = "A1";
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
    if (!rect || !rooms_in_play || index >= rooms_in_play->rooms_up.size() || !rooms_in_play->rooms_up[index] || index >= rooms_in_play->room_seeds.size()) return false;
    d2d::rules::SpawnRoom spawn_room{ rect->x * 5, rect->y * 5, rect->width * 5, rect->height * 5, rooms_in_play->room_seeds[index] };
    const auto& shape = game_data.mon_npc[std::size_t(type)].shape;
    const bool placed = d2d::rules::monster_detail::place(spawn_room, x, y, radius, [&](int at_x, int at_y) {
        const UnitState probe{ .x = Fine::centre(at_x), .y = Fine::centre(at_y), .shape = shape };
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

// A monster move's path to subtile (to_x, to_y) (FUN_00649970) by its
// path type and Monster::path_steps, into Monster::steps (ai.cpp).

// A move's path to subtile (to_x, to_y) (FUN_00649970) by its path type:
// rules::monster_path, the toward pather then the wall pather (FUN_00650350,
// FUN_005a6290); type 7 rules::player_path, then the wall pather (type 0xf
// from then on); each at the path's steps (+0x91, Monster::path_steps).
// Leading points on its own subtile are dropped (FUN_0064fe40).
// FUN_006483a0 refuses a monster that isn't npc / inTown / a pet a path
// whose end room is in town; a thinking monster here is never in a town
// level, and its path stays in its level, so none is refused.
// Type 7 has path flag 0x1000 (PATHTYPEFLAGS 0x6eb690; 0xd and 0xf don't):
// a blocked end is walked back toward the start (FUN_00648120), and with no
// free spot there's no path. A monster's type 7 is only the Dark Wanderer's
// walk to a spot (act3.cpp), never at a foe, so flag 0x800 (the target's
// stamp lifted) has nothing to lift here.
bool path_to(const Level& level, Monster& monster, int to_x, int to_y, bool at_foe, const Crowd& crowd) {
    auto& unit = monster.unit;
    auto centre = [](int coord) { return (float(coord) + 0.5f) / 5; };
    monster.step = 0;
    monster.end_x = to_x; monster.end_y = to_y;
    unit.path_word = 0;                                         // FUN_00649970 zeroes +0x54
    const int x = subtile(unit.x), y = subtile(unit.y);
    const auto blocked = [&](int at_x, int at_y) { return level.unit_blocked(centre(at_x), centre(at_y), unit.shape) || crowd.at(centre(at_x), centre(at_y), &unit); };
    if (monster.path_type == 0) {
        monster.steps = d2d::rules::monster_path(x, y, to_x, to_y, at_foe, blocked, monster.path_steps);
        return !monster.steps.empty();
    }
    if (monster.path_type == 7) {
        const bool end_free = at_foe || !blocked(to_x, to_y) || d2d::rules::end_walk_back(x, y, to_x, to_y, false, blocked);
        if (end_free) monster.steps = d2d::rules::player_path(x, y, to_x, to_y, at_foe ? 1 : 0, at_foe, blocked, monster.path_steps);
        else monster.steps.clear();
    }
    if (monster.path_type == 7 && monster.steps.empty()) monster.path_type = 0xf;
    if (monster.path_type == 0xf) monster.steps = d2d::rules::wall_path(x, y, to_x, to_y, monster.path_steps, at_foe, blocked);
    monster.steps.erase(monster.steps.begin(), std::ranges::find_if(monster.steps, [&](const std::pair<int, int>& point) { return point != std::pair(x, y); }));
    return !monster.steps.empty();
}

namespace {

// A move's frame (FUN_00650840): a chase checks its foe first
// (rules::chase_check: stop at it, re-path when it's moved), then the unit
// steps `step` cells toward its point's centre, taking the next when it
// gets there (one a frame, FUN_00650090). False when the move ends: at the
// foe, the budget spent, no re-path, blocked (FUN_00650150 sets idx =
// count) or past its last point; it stands at its subtile's centre then
// (FUN_006507b0). `spot`: a move to a spot; a chase whose foe is gone ends.
// The step is path_frame's. The foe's spot is the unit's own: FUN_00679250's
// offset by the path's +0x68 never applies, nothing sets that byte (stubs.md).
bool move_frame(const Level& level, Monster& monster, const Foe* chased, bool spot, int size, int velocity, const Crowd& crowd) {
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
        auto point = std::size_t(monster.step);
        going = path_frame(level, unit, monster.steps, point, velocity, crowd);
        monster.step = int(point);
    }
    if (going) return true;
    const float centre_x = (float(subtile(unit.x)) + 0.5f) / 5, centre_y = (float(subtile(unit.y)) + 0.5f) / 5;
    if (!level.unit_blocked(centre_x, centre_y, unit.shape)) { unit.x = Fine::centre(subtile(unit.x)); unit.y = Fine::centre(subtile(unit.y)); }
    monster.steps.clear();
    monster.step = 0;
    return false;
}

}  // namespace

// A monster's skill missile toward (dx, dy) cells (FUN_0059fa30, ai.cpp).

// One of Andariel's skill missiles (FUN_0059fa30, flags 0x20): from her
// toward (dx, dy) cells, its Missiles.txt row's damage at skill level `lvl`
// (rules::monster_skill_level): physical in 256ths (SrcDamage 0: none of hers), its
// poison per frame over ELen as the total, another element in 256ths.
// ToHit 0: it always hits. A Fallen Shaman's fire bolts too. A row naming a
// Skill (`carried`: the Summoner's Fire Ball, Glacial Spike, Frost Nova)
// carries that skill's element at the level instead (FUN_0064b860), and its
// physical in place of the row's (FUN_00647bc0 / FUN_00647d00: skill_phys).
// A CanSlow row's owner under Slow Missiles (state 0x57, slowmissiles; FUN_0059fa30 0x59fb3f..0x59fb5d):
// that state's stat 161 (skill_handofathena, its aurastat at its level), else -1.
static int slow_missiles_pct(const GameData& game_data, const Monster& monster, const GameData::MissileInfo& info, std::uint32_t now_ms) {
    if (!info.can_slow) return -1;
    for (const auto& state : monster.curse_and_states())
        if (const auto* skill = game_data.skills.get(state.skill); skill && now_ms < state.until && skill->auratarget == "slowmissiles")
            for (std::size_t k = 0; k < skill->aurastat.size(); ++k)
                if (skill->aurastat[k] == d2d::d2s::kHandOfAthena) return d2d::rules::eval_calc(game_data.skills, skill->aura_calc[k], {}, skill->id, state.level);
    return -1;
}

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
        const int phys_lo = d2d::rules::skill_phys(game_data.skills, *carried, {}, lvl, false);
        const int phys_hi = std::max(d2d::rules::skill_phys(game_data.skills, *carried, {}, lvl, true), phys_lo);
        stats.a2_min = phys_lo >> 8;
        stats.a2_max = std::max(phys_hi >> 8, stats.a2_min);
        stats.unblockable = !(phys_lo > 0 && phys_hi > 0);              // the record carries the skill's physical (FUN_0064b860), not the row's: a Death Mauler's maul stays blockable
    }
    stats.to_hit = monster.stats.to_hit;                              // its stat 0x13, rolled when the row has ToHit
    const float speed = missile_speed(d2d::rules::missile_velocity(missile_info.vel, missile_info.vel_lev, lvl, slow_missiles_pct(game_data, monster, missile_info, now_ms)));
    const float distance = std::max(std::hypot(dx, dy), 0.01f);
    missiles.push_back({ &missile_info, monster.unit.x, monster.unit.y, dx / distance * speed, dy / distance * speed, missile_facing(monster.unit.x, monster.unit.y, dx, dy), now_ms,
                         now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, stats });
    missiles.back().level = lvl; missiles.back().aim_at = { monster.unit.x + dx, monster.unit.y + dy }; missiles.back().owner = monster.id;
    missiles.back().owner_align = monster.align;
    if (carried) missiles.back().skill = carried->id;
}

// A monster turned into MonStats row `type` in place (FUN_00574370: the same unit id,
// its group and spot, a new init for the class, FUN_00574250).
void morph_monster(const GameData& game_data, Monster& unit, int type, d2d::rules::Rng& rng, int players, d2d::rules::Region* region) {
    auto turned = make_monster(game_data, type, unit.unit.x, unit.unit.y, rng, unit.difficulty, true, players, unit.level_id, unit.seed, region);
    turned.id = unit.id; turned.leader = unit.leader; turned.classic = unit.classic;
    turned.home_x = unit.home_x; turned.home_y = unit.home_y;
    unit = std::move(turned);
}

// Baal's Vile Effigy (FUN_005fc860): baalclone +- 12 subtiles (two draws of his seed) about
// (at_x, at_y), a third of his life, no regen (stat 0x4a 0); none without the row.
std::optional<Monster> vile_effigy(const GameData& game_data, const Level& level, Monster& baal, int at_x, int at_y, d2d::rules::Rng& rng, int players,
                                   d2d::rules::Region* region) {
    const auto made = game_data.monsters.by_id.find("baalclone");
    if (made == game_data.monsters.by_id.end()) return std::nullopt;
    const int clone_x = at_x + int(baal.seed.next() % 24) - 12, clone_y = at_y + int(baal.seed.next() % 24) - 12;
    auto clone = make_monster(game_data, made->second, (float(clone_x) + 0.5f) / 5, (float(clone_y) + 0.5f) / 5, rng, baal.difficulty, true, players, level.id, std::nullopt, region);
    std::tie(clone.unit.x, clone.unit.y) = level.nearest_free(clone.unit.x, clone.unit.y);
    clone.stats.hit_points = std::max(baal.stats.hit_points / 3, 1);
    clone.hit_points = std::max(baal.hit_points / 3, 1);
    clone.stats.regen = 0;
    clone.aware = true;
    clone.no_exp = clone.no_drop = true;                           // unit flags |= 0x4020000 (005fca71)
    return clone;
}

// A monster's missile made at its target, (dx, dy) cells away (FUN_0056ede0 -> FUN_0059fa30
// flag 1: the record's +0x14 / +0x18), heading a subtile on at (+1, +1).
void missile_at_target(const GameData& game_data, const Monster& monster, const std::string& name, float dx, float dy, std::uint32_t now_ms,
                       std::vector<Missile>& missiles, int lvl, const d2d::rules::Skill* carried) {
    const std::size_t before = missiles.size();
    andariel_missile(game_data, monster, name, 0.2f, 0.2f, now_ms, missiles, lvl, carried);
    if (missiles.size() == before) return;
    auto& centre = missiles.back();
    centre.x += dx; centre.y += dy;
    centre.aim_at = std::pair{ centre.x + 0.2f, centre.y + 0.2f };
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
// FUN_005dd0b0 mode 6 / 7's candidates (FUN_005dcbd0, FUN_005dcc60): the near
// rooms' foes, each room's newest first (the list from its end), its own
// skipped; their FUN_005dc380 distance, threat and whether each is an enemy
// (alive, out of town, not a friend: FUN_005dc970), and the line from them
// (mask 4) tested under 0x31. With the foe each entry is.
struct SightList { std::vector<d2d::rules::SightFoe> foes; std::vector<Foe*> units; };
SightList sight_list(const GameData& game_data, const Level& level, const Monster& monster, std::span<Foe> foes) {
    const auto& unit = monster.unit;
    const int size = game_data.monsters.types[std::size_t(monster.type)].size;
    const int x = subtile(unit.x), y = subtile(unit.y);
    const auto wall = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x04); };
    const bool town = d2d::rules::level_ids::is_town(level.id);
    SightList list;
    for (const auto& [room_level, room] : near_rooms(game_data, level, unit.x, unit.y)) {
        if (room_level != &level) continue;
        for (std::size_t i = foes.size(); i-- > 0;) {
            auto& foe = foes[i];
            if (foe.monster == &monster || room_of(level, foe.x, foe.y) != room) continue;
            const int foe_x = subtile(foe.x), foe_y = subtile(foe.y);
            auto& entry = list.foes.emplace_back(d2d::rules::SightFoe{ d2d::rules::near_distance(foe_x - x, foe_y - y, foe.size), foe.threat,
                                                                       foe.alive && !town && !d2d::rules::friends(monster.align, foe.monster ? foe.monster->align : 2) });
            entry.blocked = entry.enemy && entry.distance < 0x31 && d2d::rules::sight_blocked(foe_x, foe_y, foe.size, x, y, size, wall);
            list.units.push_back(&foe);
        }
    }
    return list;
}
// FUN_005dd510's path test for a monster: a type 2 path from it to the foe
// (rules::path_found) at its path's steps, through its own collision and the
// crowd's (the foe's stamp too: type 2's flags lift none).
bool path_found(const Level& level, const Monster& monster, const Foe& foe, const Crowd& crowd) {
    const auto& unit = monster.unit;
    const auto centre = [](int coord) { return (float(coord) + 0.5f) / 5; };
    return d2d::rules::path_found(subtile(unit.x), subtile(unit.y), subtile(foe.x), subtile(foe.y), monster.path_steps, [&](int at_x, int at_y) {
        return level.unit_blocked(centre(at_x), centre(at_y), unit.shape) || crowd.at(centre(at_x), centre(at_y), &unit);
    });
}
struct Search { Foe* target = nullptr; int best = 0, nearest = 0x7fffffff; };
Search search_target(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, AreaSeen* seen, const Crowd& crowd) {
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
        for (const auto& foe : foes) units.push_back({ room_of(level, foe.x, foe.y), true, foe.room_stamp });
        const auto near_of = d2d::rules::room_order(units, rooms);
        std::vector<d2d::rules::NearFoe> nears;
        nears.reserve(near_of.size());
        for (const int index : near_of) {
            const auto& foe = foes[std::size_t(index)];
            auto& entry = nears.emplace_back(d2d::rules::NearFoe{ nearby(foe), foe.monster ? foe.monster->align : 2, foe.threat, foe.monster == &monster, foe.monster || foe.pet, !foe.alive, town });
            entry.blocked = need_sight && entry.distance <= 0x23 && d2d::rules::sight_blocked(subtile(foe.x), subtile(foe.y), foe.size, x, y, type_info.size, wall);
            entry.waking = foe.monster && foe.alive && foe.monster->sighted && area_id_at(game_data, level, foe.x, foe.y, foe.x, foe.y) == here;
        }
        auto pick = d2d::rules::search_near(nears, align, need_sight);
        if (pick.target >= 0) pick.target = near_of[std::size_t(pick.target)];
        if (pick.second >= 0) pick.second = near_of[std::size_t(pick.second)];
        return pick;
    };
    // Terror's init (special 11, 0x5e80e0: an AI table entry Ghidra misses): the unit its path was at
    // (FUN_00553540, not itself; here the foe it last chased or swung at)
    // as its skill-set target by id (FUN_00573090, SwitchAI only: a player
    // kind 1, a monster 2, a missile 4), which its first think's search
    // takes (FUN_005dd610: by id, alive, no sight test) and the think then
    // clears (FUN_005e8140 -> FUN_00573120). d2d's kind 1 matches Foe::id,
    // so it also stands for the merc or a pet (game.exe's kind 2).
    if (monster.special == 11 && monster.ai_state3 == 0 && monster.set_kind == 0 && type_info.switch_ai && monster.chase >= 0
        && std::size_t(monster.chase) < foes.size())
        if (const auto& chased = foes[std::size_t(monster.chase)]; chased.monster != &monster) {
            monster.set_kind = chased.monster ? 2 : 1;
            monster.set_id = chased.monster ? chased.monster->id : chased.id;
        }
    // FUN_005dd510 (from FUN_005dd7f0): with `other` (list 9's nearest, or mode 5's secondary)
    // under 6 off and no type 2 path to `found`, mode 7's pick (another enemy of threat 2 or
    // more, rules::search_threat) within 0x13 takes found's place; else `other`. True: other.
    const auto other_instead = [&](Search& primary, int other_best) {
        if (other_best >= 6 || path_found(level, monster, *primary.target, crowd)) return false;
        const auto list = sight_list(game_data, level, monster, foes);
        const auto found = std::ranges::find(list.units, primary.target);
        int threat_best = 0;
        const int threat = d2d::rules::search_threat(list.foes, found == list.units.end() ? -1 : int(found - list.units.begin()), threat_best);
        if (threat < 0 || threat_best > 0x13) return true;
        primary.target = list.units[std::size_t(threat)]; primary.best = threat_best;
        return false;
    };
    Search found;
    if (monster.set_kind == 2) {                                    // FUN_005dd610: Attract's monster
        for (auto& foe : foes)
            if (foe.monster && foe.monster->id == monster.set_id) { if (foe.alive) found = { &foe, nearby(foe) }; break; }
    } else if (monster.set_kind == 1) {                             // Terror's: a player, the merc or a pet, by Foe::id
        for (auto& foe : foes)
            if (foe.id == monster.set_id) { if (foe.alive) found = { &foe, nearby(foe) }; break; }
    } else if (monster.set_kind == 3) {                             // Confuse's: as a random alignment, the primary only
        const auto pick = mode5(d2d::rules::confuse_align(monster.align, monster.seed.next() & 1));
        if (pick.target >= 0) found = { &foes[std::size_t(pick.target)], pick.best };
    }
    if (!found.target) monster.set_kind = 0;                        // FUN_00573120
    if (!found.target && monster.align == 0) {
        std::vector<d2d::rules::SearchFoe> picks;
        std::vector<Foe*> pick_foes;
        // The player's pets in game.exe's list order: the one that entered the list last first (FUN_005b1900).
        std::vector<d2d::rules::ListEntry> entries;
        // A converted monster (FUN_005d0350 at 0x5d0501: FUN_005b1900(game, monster, 0, caster's list)) is the caster's newest.
        for (const auto& foe : foes) {
            const bool turned = foe.monster && foe.monster->align == 2 && foe.monster->listed && foe.monster->pet_serial > 0;
            entries.push_back({ turned ? d2d::rules::ListKind::pet : foe.monster ? d2d::rules::ListKind::other : foe.pet ? d2d::rules::ListKind::pet : d2d::rules::ListKind::player,
                                turned ? foe.monster->pet_serial : foe.listed });
        }
        for (const std::size_t slot : d2d::rules::search_order(entries)) {
            auto& foe = foes[slot];
            // List 9: neutral monsters; a converted one is in its caster's list,
            // with the player's pets (FUN_005b1900).
            const bool converted = foe.monster && foe.monster->align == 2 && foe.monster->listed && foe.alive;
            if (foe.monster && !(foe.monster->align == 1 && foe.alive) && !converted) continue;
            const int foe_x = subtile(foe.x), foe_y = subtile(foe.y);
            picks.emplace_back(d2d::rules::SearchFoe{ d2d::rules::ai_distance(foe_x - x, foe_y - y), foe.pet || converted, town && !foe.monster, !foe.alive && !foe.monster, false,
                                                      foe.monster && !converted ? 9 : 0 });
            auto& pick = picks.back();
            if (pick.pet && pick.dead) pick.distance = 0x7fffffff;
            pick.blocked = need_sight && (pick.list || pick.distance < best) && d2d::rules::sight_blocked(x, y, type_info.size, foe_x, foe_y, foe.size, wall);
            pick_foes.push_back(&foe);
        }
        const auto pick = d2d::rules::search_pick(picks, best, need_sight);
        found = { pick.target >= 0 ? pick_foes[std::size_t(pick.target)] : nullptr, pick.best, pick.nearest };
        if (pick.nine >= 0 && pick.target != pick.nine && other_instead(found, pick.nine_best)) { found.target = pick_foes[std::size_t(pick.nine)]; found.best = pick.nine_best; }
    } else if (!found.target) {
        const auto pick = mode5(monster.align);
        if (pick.target >= 0) found = { &foes[std::size_t(pick.target)], pick.best };
        if (pick.second >= 0 && (pick.target < 0 || other_instead(found, pick.second_best))) found = { &foes[std::size_t(pick.second)], pick.second_best };
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
void charge_swing(const GameData& game_data, Monster& monster, const Foe& foe, int skill, std::uint32_t now_ms) {
    const bool second = monster.type == int(game_data.mon_bin[std::size_t(d2d::rules::monster_ids::kDuriel)]) || monster.type == int(game_data.mon_bin[0x49]);
    const std::string_view mode = second ? "A2" : "A1";
    turn_to(monster.unit, subtile(foe.x), subtile(foe.y));
    monster.skill = skill;                        // before set_mode: its length reads the skill's UseAttackRate
    set_mode(game_data, monster, mode, now_ms);
    monster.struck = false; monster.wandering = false; monster.seq_done = 0; monster.seq_once = false;
    monster.skill_frame = 0; monster.skill_x = subtile(foe.x); monster.skill_y = subtile(foe.y);
    attack_starts(game_data, monster, mode);
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
// FUN_0054dc40 with no unit: 20 tries, each rand(width) then rand(height) on
// the room's seed (+0x6c) inside the room's subtiles less its first row and
// column, the first where FUN_005b2f20 places the type at radius -1.
std::optional<std::pair<int, int>> room_free_spot(const GameData& game_data, const Level& level, const Crowd& crowd, Spawning::LevelState* rooms_in_play,
                                                  int type, int room) {
    const auto rect = room < 0 ? std::nullopt : level_room(level, std::size_t(room));
    if (!rect || !rooms_in_play || std::size_t(room) >= rooms_in_play->room_seeds.size()) return std::nullopt;
    const int left = rect->x * 5 + 1, top = rect->y * 5 + 1, width = (rect->x + rect->width) * 5 - left, height = (rect->y + rect->height) * 5 - top;
    for (int tries = 0; tries < 20; ++tries) {
        auto& room_seed = rooms_in_play->room_seeds[std::size_t(room)];
        const int try_x = room_seed(width) + left, try_y = room_seed(height) + top;
        int at_x = 0, at_y = 0;
        if (spawn_spot(game_data, level, crowd, rooms_in_play, type, try_x, try_y, -1, room, at_x, at_y)) return std::pair{ at_x, at_y };
    }
    return std::nullopt;
}

bool teleport_mod(const GameData& game_data, const Level& level, Monster& monster, int distance, const Crowd& crowd, Spawning::LevelState* rooms_in_play, std::uint32_t now_ms) {
    if ((monster.boss != d2d::rules::Boss::unique && monster.boss != d2d::rules::Boss::superunique) || !std::ranges::contains(monster.mods, d2d::rules::umod::teleport)
        || monster.seed.next() % 100 >= 40) return false;
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    const bool low = std::int64_t(monster.hit_points) * 100 < std::int64_t(monster.stats.hit_points) * 30;
    const bool melee = type_info.melee || type_info.base == 10 || type_info.base == 0x159 || type_info.base == 0x22d;
    if (!low && (melee || distance >= 10)) return false;
    if (monster.seed(100) >= 15) return false;
    const int teleport = skill_id(game_data, "MonTeleport");
    if (teleport < 0) return false;
    const auto spot = room_free_spot(game_data, level, crowd, rooms_in_play, monster.type, room_of(level, monster.unit.x, monster.unit.y));
    if (!spot) return false;
    if (low && monster.seed(100) < 25 && now_ms >= monster.no_heal_until) heal(monster, monster.stats.level);
    monster.skill = teleport;
    monster.skill_sound = teleport;
    monster.skill_x = spot->first; monster.skill_y = spot->second; monster.at_point = true;
    monster.unit.walking = false;
    set_mode(game_data, monster, "S1", now_ms);                              // MonTeleport's monanim
    monster.struck = false;
    return true;
}

}  // namespace

// The AI search's pick for `monster` among `foes` (FUN_005dd7f0, ai.cpp): its index, or -1.

// The AI search's pick (search_target, FUN_005dd7f0) as a foe index: a pet that runs its own monster's search (a
// revived monster: alignment 2, mode 5).
int search_foe(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, const Crowd& crowd) {
    const auto found = search_target(game_data, level, monster, foes, nullptr, crowd);
    return found.target ? int(found.target - foes.data()) : -1;
}

auto ThinkCtx::idle(int frames) -> void {                           // FUN_005de080
    if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
    monster.wandering = false;
    monster.next_act = now_ms + std::uint32_t(std::max(frames, 1)) * 40;
}

// A move sets off (FUN_005a7c20) with a re-path budget of 0x14
// (FUN_006490e0) and its path (path_to), at a foe or to unit.goal (at a
// unit there: `to_unit`); one with no path can't start, and a spot move
// then thinks aidel on (FUN_005a73e0). Its path's steps are `path_steps`
// (FUN_005a6290 -> FUN_00648e70: a walk's 5).
auto ThinkCtx::set_off(bool running, const Foe* foe, bool to_unit, int path_steps) -> bool {
    monster.budget = 0x14;
    monster.path_steps = path_steps;
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

auto ThinkCtx::walk_to(int off_x, int off_y, bool running) -> bool {   // to a spot, subtiles off
    unit.goal_x = (float(x + off_x) + 0.5f) / 5; unit.goal_y = (float(y + off_y) + 0.5f) / 5;
    return set_off(running, nullptr);
}

auto ThinkCtx::use(std::string_view mode, int skill) -> void {     // FUN_005dead0 / FUN_005ddf90: against the target (mode ctx +8)
    monster.chase = targeted ? int(target - foes.data()) : -1;
    turn_to(unit, target_x, target_y);
    monster.skill = skill;                        // before set_mode: its length reads the skill's UseAttackRate
    monster.skill_sound = skill;
    set_mode(game_data, monster, mode, now_ms);
    monster.struck = false; monster.wandering = false; monster.seq_done = 0; monster.seq_once = false;
    monster.skill_frame = 0; monster.skill_x = target_x; monster.skill_y = target_y; monster.at_point = false;
    attack_starts(game_data, monster, mode);
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
// place of its MonAI's (FUN_005b15d0). Thinks draw the unit's own seed, as
// the stat init left it (make_monster; tools/emu/unit_seed_init.py).
// ponytail: in melee skips the path test (FUN_00622aa0, mask 0x804); the path types a think
// gives its next move (FUN_005de190's EDX: 48 of its 103 calls, 1 / 2 /
// 7 / 0xd / 0xf) taken only for the Dark Wanderer's (Monster::path_type),
// the rest as path_to's; a circle as a walk to the point n
// subtiles to the side of the target; a back-off sets off when its end
// and first step are open.
bool think(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, d2d::rules::Rng& rng, std::uint32_t now_ms,
           int walk, int run, const Crowd& crowd, std::span<Monster> pack, AreaSeen* seen, const OpenDoor* open_door, std::vector<Monster>* born,
           Spawning::LevelState* rooms_in_play, int players_counted, Spawning* spawning) {
    // A made unit's seed is a step of the game seed (FUN_00555230 -> FUN_00650e40), d2d's rng without one.
    d2d::rules::Rng& game_seed = spawning ? spawning->game : rng;
    d2d::rules::Region* const level_region = spawning ? spawning->region(level.id) : nullptr;   // a made monster's look (FUN_00547bc0)
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
                  x, y, target, best, targeted, target_x, target_y, dx, dy, act, second, corpse, kin, aid, imp_found, lay_x, lay_y, wisp_finds, spawning };
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
            charge_swing(game_data, monster, *chased, with, now_ms);
            return true;
        }
    }
    if (now_ms < monster.next_act && !move_ended) return true;
    monster.thought_ms = now_ms;
    // The driver's first step (FUN_005b10e0 → FUN_005b0f50): with opendoors
    // (MonStats flags +0xc & 8) and the monster bit 0x800 under it
    // (FUN_00648eb0: what its last path's steps met, on_door), its door
    // (OpenDoor) operated, it stands 5.
    if (type_info.open_doors && on_door(level, unit, type_info.velocity != 0) && open_door && *open_door && (*open_door)(monster, now_ms)) { ctx.idle(5); return true; }
    // Then FUN_005b0ff0: a unique's minion (its AI control's +0xc the leader, FUN_005a0c00 -> FUN_005dd330)
    // over 0x14 from its leader (FUN_005dc380, its own size off) walks to a spot 0x13 about it (FUN_005df530 on
    // its seed) on path type 7 with 0x28 steps (FUN_005de190(7, 0, 0x28)), before any search or think; the
    // leader gone or dead, or itself without WL, and it never does again.
    // d2d's leader is its group's first's place in the level's monster list (Fight::rooms_up translates a
    // spawn's through Spawning::LevelState::spawn_slots).
    // A revived monster's owner (FUN_005c55c0) is Fight::pets_turn's. Taunt (FUN_005d8570) sets none (0x5d8718):
    // put_state's leader_lost.
    // item_reanimate's monster (FUN_005c07a0: its player, 0x5c08ca) rises from a corpse as its death ends: FUN_005c09e0
    // hooks unit event 0xd on the victim, which mode 0's end (0x5a72b0, table 0x6e2260 row 0 +8) fires after DD.
    // ponytail: that monster isn't made: the kill hook and the raise belong to the deaths and pets (fight/).
    if (monster.boss == d2d::rules::Boss::minion && !monster.leader_lost && monster.leader >= 0 && std::size_t(monster.leader) < pack.size()
        && &pack[std::size_t(monster.leader)] != &monster && pack[std::size_t(monster.leader)].boss != d2d::rules::Boss::none
        && pack[std::size_t(monster.leader)].boss != d2d::rules::Boss::minion) {
        const auto& lead = pack[std::size_t(monster.leader)];
        const int lead_x = subtile(lead.unit.x), lead_y = subtile(lead.unit.y), own_x = subtile(unit.x), own_y = subtile(unit.y);
        const int gap = d2d::rules::near_distance(lead_x - own_x, lead_y - own_y, type_info.size);
        const auto step = d2d::rules::leader_follow(0, 1, type_info.can_walk, true, !lead.alive(), gap, false);
        if (step.cleared) monster.leader_lost = true;
        if (step.walk) {
            const auto [spot_x, spot_y] = d2d::rules::spot_about(monster.seed, lead_x, lead_y, 0x13);
            unit.goal_x = (float(spot_x) + 0.5f) / 5; unit.goal_y = (float(spot_y) + 0.5f) / 5;
            monster.next_path_type = 7;
            ctx.set_off(false, nullptr, false, 0x28);
        }
        if (step.acted) return true;
    }
    // ReanimatedHorde's think (FUN_005e1540) first puts the unit flags 0xe back
    // (|= 0xe at 0x5e156c), whatever it then does: its rise (Monster::rising) is over.
    if (type_info.ai_name == "ReanimatedHorde") monster.rising = false;
    x = subtile(unit.x); y = subtile(unit.y);
    const auto searched = search_target(game_data, level, monster, foes, seen, crowd);
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
            // Bit 0x40 under its own collision pattern (path +0x48, FUN_00649180), not the one subtile.
            auto on_pattern = unit.shape;
            on_pattern.mask = 0x40;
            const auto miss = d2d::rules::search_missed(type_info.ai_name, has_walk, monster.got_hit(), level.unit_blocked(unit.x, unit.y, on_pattern), nearest);
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
    if (special && targeted && best < 0x14 && !target->pet && !target->monster && !monster.first_sight) {
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
            if (&foe == target || !foe.alive || foe.monster == &monster || foe.threat <= 1 || d2d::rules::friends(monster.align, foe.monster ? foe.monster->align : 2)) continue;
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
    input.room_spot = [&] { return room_free_spot(game_data, level, crowd, rooms_in_play, monster.type, room_of(level, monster.unit.x, monster.unit.y)); };   // Baal's teleport
    input.home_dist = d2d::rules::ai_distance(subtile(monster.home_x) - x, subtile(monster.home_y) - y);
    input.off_x = target_x - x; input.off_y = target_y - y;
    int pace = 0;
    input.pace = &pace; input.state3 = &monster.ai_state3;
    input.target_life_pct = target->life_pct;
    input.laying = now_ms < monster.laid_until;
    input.velocity = type_info.velocity; input.run = type_info.run;
    input.aidel = per_difficulty.aidel ? per_difficulty.aidel : 15;
    input.neutral = monster.mode == "NU"; input.in_town = d2d::rules::level_ids::is_town(level.id);
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
    // always tested (mask 4); FUN_005dd510 asks for a path to the primary
    // (path_found: the secondary under 6 off and none, mode 7's pick).
    // Its state 0x92 (invis) skip needs no test here: only FUN_005ef620 (Baal into
    // the stairs: d2d's gone, so not alive) and FUN_0063efa0 case 0x219 (the
    // Ancients' statues: objects in d2d) set it.
    if (monster.set_kind != 0) { second = target; input.second_dist = best; }
    else {
        const auto list = sight_list(game_data, level, monster, foes);
        int gap = 0x7fffffff;
        const int chosen = d2d::rules::sight_choice(list.foes, d2d::rules::search_sight(list.foes),
                                                    [&](int pick) { return path_found(level, monster, *list.units[std::size_t(pick)], crowd); }, gap);
        if (chosen >= 0) { second = list.units[std::size_t(chosen)]; input.second_dist = gap; }
    }
    input.second = second != nullptr;
    input.aura_on = monster.aura != 0;
    std::vector<d2d::rules::ThinkEffect> effects;
    input.effects = &effects;
    std::vector<std::function<d2d::rules::Think()>> otherwise;     // its next acts when a walk can't set off (ThinkIn::otherwise)
    input.otherwise = &otherwise;
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
    int strike_delay = 0;
    // Its effects: the leader's command done; Duriel's aura (Skill4) on;
    // states 0x5a / 0x5b (looks: blue / red) on or off; an overlay played
    // once on it; stat 0x4a (regen). The Summoner's quest reads his first
    // think (ai_state).
    for (const auto& effect : effects) {
        if (effect.kind == d2d::rules::ThinkEffect::state && (effect.id == 0x5a || effect.id == 0x5b)) {
            const auto bit = std::uint8_t(effect.id == 0x5a ? 1 : 2);
            monster.looks = effect.value ? std::uint8_t(monster.looks | bit) : std::uint8_t(monster.looks & ~bit);
        }
        if (effect.kind == d2d::rules::ThinkEffect::overlay) d2d::rules::add_one_shot(monster.one_shots, effect.id, now_ms);
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
            if (auto clone = vile_effigy(game_data, level, monster, target ? target_x : x, target ? target_y : y, game_seed, d2d::rules::game_players(players_in(foes), players_counted), level_region)) {
                clone->next_act = now_ms + 15 * 40;
                born->push_back(std::move(*clone));
                ++monster.ai_state2;
            }   // a4q1's FUN_005b4390 (World::act4_update)
        if (effect.kind == d2d::rules::ThinkEffect::aura) { monster.aura = skill_id(game_data, type_info.skill[std::size_t(effect.id)]); monster.aura_lvl = std::max(input.aip[0], 1); }
        if (effect.kind == d2d::rules::ThinkEffect::strike) strike_delay = effect.id;
    }
    // The unit it's at: its kin (a walk to it), the hurt one or corpse it
    // casts on; the second target, in the target's place.
    using d2d::rules::ThinkUnit;
    const auto at_second = [&] {                                   // the act now current: a next act (set_off_or_next) too
        if (act.unit != ThinkUnit::second || !second || second == target) return;
        target = second;
        targeted = true;                                           // a unit now, a search-type-0 think's too (GoodNpcRanged): its chase and shot go at it
        target_x = subtile(target->x); target_y = subtile(target->y);
        dx = target->x - unit.x; dy = target->y - unit.y;
    };
    at_second();
    const Monster* at_unit = act.unit == ThinkUnit::kin ? (kin >= 0 ? &pack[std::size_t(kin)] : corpse >= 0 ? &pack[std::size_t(corpse)] : nullptr)
                           : act.unit == ThinkUnit::aid && aid >= 0 ? &pack[std::size_t(aid)] : nullptr;
    // After the act, its next think (unit event 2) n frames on: `wait`
    // (FUN_005de0f0) replaces the pending one, `event` (FUN_005de130) only
    // when none is pending or it'd come sooner. It comes mid-mode or not
    // (FUN_005b1740 reads no mode; monster_update).
    const auto file_think = [&monster, now_ms, wait = act.wait, event = act.event] {
        if (wait > 0) {
            monster.next_act = now_ms + std::uint32_t(wait) * 40;
            monster.scheduled_older_think_ms.reset();
        } else if (event > 0 && (!monster.think_pending() || now_ms + std::uint32_t(event) * 40 <= monster.next_act)) {
            monster.next_act = now_ms + std::uint32_t(event) * 40;
            monster.scheduled_older_think_ms.reset();
        }
    };
    // FUN_0058f730 / FUN_0058ef40: its leader's group, itself too. One made
    // outside a group (leader -1) has none to look up, so nothing is sent.
    if (rally && monster.leader >= 0)
        for (auto& other : pack)
            if (other.alive() && other.leader == monster.leader) other.ai_command = 1;
    // A walk that can't set off with another act to try: the think it filed
    // (FUN_005a73e0) is dropped (FUN_00540e60(2, 0)) and the next one runs.
    std::size_t tried = 0;
    const auto set_off_or_next = [&](auto&& sets_off) {
        const auto pending = monster.next_act;
        if (sets_off() || tried >= otherwise.size()) return true;
        monster.next_act = pending;
        const auto next = std::move(otherwise[tried++]);          // moved out: the act it makes may add its own
        act = next();
        at_second();
        return false;
    };
    const auto walk_or_next = [&](int off_x, int off_y) { return set_off_or_next([&] { return ctx.walk_to(off_x, off_y, false); }); };
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
            if (act.x != x || act.y != y) turn_to(unit, act.x, act.y);   // FUN_00621c00 only off its own subtile
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
                    auto laid = make_monster(game_data, young->second, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, d2d::rules::game_players(players_in(foes), players_counted), level.id, d2d::rules::Rng{ game_seed.next() }, level_region);
                    laid.aware = true; laid.no_exp = true;
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
        case MonAct::around: if (walk_or_next(target_x - x + act.x, target_y - y + act.y)) return true; break;
        case MonAct::home: if (walk_or_next(subtile(monster.home_x) - x, subtile(monster.home_y) - y)) return true; break;
        case MonAct::point:                                        // FUN_005dead0(Sk1mode, Skill1, no unit, x, y)
            ctx.use(type_info.sk_mode[0], skill_id(game_data, type_info.skill[0]));
            monster.skill_x = act.x; monster.skill_y = act.y;
            if (act.x != x || act.y != y) turn_to(unit, act.x, act.y);   // FUN_00621c00 only off its own subtile
            return true;
        case MonAct::fly: ctx.walk_to(act.x - x, act.y - y, false); return true;   // FUN_005de440: to the point (mode n: the Vulture's S1)
        case MonAct::keep: {                                       // FUN_005de4e0: toward, or from, the target to keep x off
            if (act.unit == ThinkUnit::self) { ctx.idle(0); return true; }   // the Vulture's landing: a move onto itself
            const int stride = std::min(std::abs(best - act.x), act.n), sign = best < act.x ? -1 : 1;
            const int across = std::abs(target_x - x), down = std::abs(target_y - y), sum = std::max(across + down, stride);
            int step_x = sum ? across * stride / sum : 0, step_y = sum ? down * stride / sum : 0;
            while (sum && step_x + step_y < stride) { ++step_x; ++step_y; }
            if (walk_or_next(((target_x > x) - (target_x < x)) * step_x * sign, ((target_y > y) - (target_y < y)) * step_y * sign)) return true;
            break;
        }
        case MonAct::circle: {                                     // FUN_005df7d0
            const float side = act.x ? 1.f : -1.f, length = std::max(std::hypot(dx, dy), 0.01f), off = float(act.n) / 5;
            unit.goal_x = target->x - dy / length * off * side; unit.goal_y = target->y + dx / length * off * side;
            if (set_off_or_next([&] { return ctx.set_off(false, nullptr); })) return true;
            break;
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
            // Its run keeps the unit's path type: 0x14 is the path's step cap
            // (FUN_00648e70: +0x90 / +0x91, at most 77; Monster::path_steps, its
            // chase's re-paths too), after +0x14 / +0x16 are cleared
            // (FUN_00649050). State 0x12 (skill_move, FUN_00639db0) only the
            // client tests (FUN_004c7890); FUN_00571b70 clears it.
            // DiabRun (srvst 54 / do 103, FUN_005cd2c0 / FUN_005cd380): his
            // sequence at the target's spot as it is now (monster_update plays it).
            if (const auto* run_skill = game_data.skills.get(id); run_skill && run_skill->srvdofunc == d2d::rules::ServerDoFunction::kDiabRun) {
                monster.chase = chase_of();
                turn_to(unit, target_x, target_y);
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
                    if (input.in_melee) { charge_swing(game_data, monster, *target, id, now_ms); monster.skill_sound = id; return true; }
                    if (ctx.set_off(true, target, false, 0x14)) { monster.chase = chase_of(); monster.charging = id; monster.skill_sound = id; return true; }
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
                unit.x = Fine::centre(spot->first); unit.y = Fine::centre(spot->second);
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
            if (seq) {
                // FUN_00621260 starts DeathMaul at 256 sequence units per tick.
                // Its rate refresh leaves the sequence rate alone even when A2 slows.
                const auto steps = std::uint32_t(seq->size());
                monster.mode_until = now_ms + (start_skill && start_skill->srvdofunc == d2d::rules::ServerDoFunction::kDeathMaul
                    ? steps * 40
                    : steps * game_data.npc_timing(monster.npc, played).ms_per_frame() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, "SQ")));
            }
            // The leap's flight: from its takeoff row to its landing row (do 77
            // / 78 on the sequence's event-1 rows), Leap Attack's strike on the
            // next (FUN_005da660).
            // The flight is a line: the takeoff (FUN_005d9f70) lays path type 9
            // (FUN_00679f70, table 0x6eb6d8: one point, the landing) with collision
            // type and mask 0, so nothing blocks it; FUN_005d9d50's FUN_0064ea90 only
            // stamps the landing spot (pattern 0x80) till it lands (FUN_005da120).
            // The takeoff's pace (FUN_005d9f70: FUN_00648690 gets MonStats Run (row +0x34)
            // << 8, 16.16 subtiles a frame): Run / 80 cells a frame, `Monster::leap_pace`.
            // FUN_005d9d50 stamps the landing (FUN_0064ea90, the unit's pattern, bits 0x80: the centre's
            // unit bit 0x1000) till it lands (FUN_005da120 clears it): `landing_mark` in the crowd's
            // held marks (Fight::crowd), so no other unit steps or lands there meanwhile, those that
            // think later this frame included.
            // The wait at the landing for a unit not there yet (FUN_005da120 -> FUN_006212c0): `leap_hold`.
            if (landing) {
                monster.leaping = true;
                monster.leap_takeoff = 0; monster.leap_until = 0;
                monster.leap_from_x = unit.x; monster.leap_from_y = unit.y;
                monster.leap_to_x = (float(landing->first) + 0.5f) / 5; monster.leap_to_y = (float(landing->second) + 0.5f) / 5;
                monster.landing_mark = {};
                monster.landing_mark.x = monster.leap_to_x; monster.landing_mark.y = monster.leap_to_y;
                monster.landing_mark.shape = UnitShape{ 1, 1, unit.shape.mask };   // its centre subtile alone (pattern 1's point)
                monster.leap_pace = leap_cells_per_frame(type_info.run);
            }
            // The do (77 Leap, 78 Leap Attack) runs on the sequence's event-1 rows: the first the takeoff (FUN_005d9f70),
            // the second the landing (FUN_005da120): seq_leaperleap's 1 and 9, seq_ancientleap's 5 and 11.
            if (landing && seq) {
                const auto row_ms = game_data.npc_timing(monster.npc, played).ms_per_frame() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, "SQ"));
                int takeoff = -1, lands = -1;
                for (std::size_t seq_row = 0; seq_row < seq->size(); ++seq_row)
                    if ((*seq)[seq_row].event == 1) { if (takeoff < 0) takeoff = int(seq_row); else if (lands < 0) lands = int(seq_row); }
                if (lands >= 0) {
                    monster.leap_takeoff = unit.mode_ms + std::uint32_t(takeoff) * row_ms;
                    monster.leap_until = unit.mode_ms + std::uint32_t(lands) * row_ms;
                }
                monster.leap_target = target ? target->id : -1;
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
            if (monster.skill_unit >= 0) turn_to(unit, pack[std::size_t(monster.skill_unit)].unit.x.subtile(), pack[std::size_t(monster.skill_unit)].unit.y.subtile());
            if (row && row->aim) row->aim(ctx);                    // its own aim (a point on the map, its find: ai/)
            return true;
        }
        case MonAct::die:                                          // no loot, no experience
            monster.hit_points = 0;
            set_mode(game_data, monster, "DT", now_ms);
            return true;
        case MonAct::none: case MonAct::untraced: return true;
    } }();
    // ElementalBeast's in-melee branch (FUN_005f6b70) starts S1, then files
    // its type-0 action event through FUN_005417d0 for the next game frame.
    // That explicit event lands before S1's animation action frame. The pose
    // then plays from this frame (as set_mode's Council start), not a stale clock.
    if (acted && strike_delay > 0 && monster.mode == "S1") {
        monster.scheduled_action_ms = now_ms + std::uint32_t(strike_delay) * 40;
        if (!monster.scheduled_anim_position) {
            monster.scheduled_anim_position = 0;
            monster.scheduled_anim_ms = now_ms;
        }
    }
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
    const float speed = missile_speed(d2d::rules::missile_velocity(maker->second.vel, maker->second.vel_lev, lvl, slow_missiles_pct(game_data, monster, maker->second, now_ms)));
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
                    std::vector<Monster>* born, AreaSeen* seen, const OpenDoor* open_door, Spawning::LevelState* rooms_in_play, int players_counted,
                    Spawning* spawning) {
    // A made unit's seed is a step of the game seed (FUN_00555230 -> FUN_00650e40), d2d's rng without one.
    d2d::rules::Rng& game_seed = spawning ? spawning->game : rng;
    d2d::rules::Region* const level_region = spawning ? spawning->region(level.id) : nullptr;   // a made monster's look (FUN_00547bc0)
    auto& unit = monster.unit;
    unit.turns_on = true;
    // FUN_004b13a0 turns every monster path a step each client frame (FUN_00648640). A walk's own
    // step does it while walking; DiabRun's wind-up is RN with no step, so it turns here too.
    if (!unit.walking || monster.diab_run == 1) turn_frame(unit);
    // Its foe: the one its last think's mode or move was set against
    // (FUN_005ddf90 / FUN_005dead0 / FUN_005deb60 put the target in the mode
    // ctx, FUN_005a7c20 on the path, +0x58): an attack's hit, a shot's aim
    // and a flight go at it. With none alive, the nearest of the player's side.
    Foe* pick = &foes[0];
    for (auto& foe : foes)
        if (!foe.monster && foe.alive && (!pick->alive || std::hypot(foe.x - unit.x, foe.y - unit.y) < std::hypot(pick->x - unit.x, pick->y - unit.y))) pick = &foe;
    if (monster.chase >= 0 && std::size_t(monster.chase) < foes.size() && foes[std::size_t(monster.chase)].alive) pick = &foes[std::size_t(monster.chase)];
    Foe& foe = *pick;
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    // Its walk % this frame (the chill, its think's pace, Spider Lay's -100 % and the rest; FUN_00623f50 0x62414e:
    // at least 25), before any mode's early return: a moving mode's rate reads it (mode_rate), the walk below too.
    const int walk_chill = now_ms < monster.chill_until ? chill_pct(game_data, monster) : 0;
    const int walk_pace = monster.move_pct - (now_ms < monster.laid_until ? 100 : 0);
    const int walk_pct = std::max(kMonsterVelocityPct + monster.speed_pct + monster.boss_speed + walk_pace + walk_chill, 25);
    monster.walk_pct = walk_pct;
    const bool frozen = now_ms < monster.frozen_until;
    if (monster.scheduled_action_ms && (monster.mode == "A1" || monster.mode == "A2" || monster.mode == "S1")) {
        const auto ticks = (now_ms - monster.scheduled_anim_ms) / 40;
        if (ticks > 0) {
            // FUN_00623e00 adds the current rate to the existing position.
            // The visible pose can slow without moving the queued action.
            const auto& timing = game_data.npc_timing(monster.npc, monster.mode);
            const auto length = std::uint64_t(timing.frames) * 256;
            if (length && !frozen) {
                const auto rate = std::uint32_t(std::max(mode_rate(game_data, monster, monster.mode), 0));
                monster.scheduled_anim_position = std::uint32_t((monster.scheduled_anim_position.value_or(0) + std::uint64_t(ticks) * rate) % length);
            }
            monster.scheduled_anim_ms += ticks * 40;
        }
    }
    // Its think event comes due mid-mode too (FUN_005b1740 reads no mode) and
    // its act replaces the mode (FUN_005a7c20 stops none), but not under
    // state 0x36 (FUN_005a7c20's error 0x753) or frozen (FUN_005a7f80 drops
    // it). A mode's end (FUN_005a8030: not WL / RN) goes to NU, whose start
    // FUN_005a73e0 files the think aidel on (0: 15; stunned 0x2d) unless one
    // is pending later.
    const bool own_think = monster.special == 10 || monster.special == 11 || d2d::rules::traced_ai(type_info.ai_name);
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
        // A rise's think (MonUMod 34, 0x33 frames on) stands even when S1 ends on its own frame.
        if (monster.think_pending() && (monster.next_act > now_ms || monster.rising)) return;
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
        monster.scheduled_older_think_ms.reset();
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
    // The original monster event bucket runs same-tick entries in insertion
    // order (FUN_005416b0 / FUN_00541060). A think queued before this attack
    // may replace its mode and clear the action or end before it can fire.
    const auto dispatch_think = [&] {
        const int pace = path_velocity(type_info.velocity, walk_pct);
        think(game_data, level, monster, foes, rng, now_ms, pace, pace, crowd, pack, seen, open_door, born,
              rooms_in_play, players_counted, spawning);
    };
    const auto run_older_think = [&](std::uint32_t due) {
        if (monster.scheduled_older_think_ms != due || monster.next_act != due
            || now_ms < monster.stun_until || !think_due()) return false;
        monster.scheduled_older_think_ms.reset();
        const auto old_mode = monster.mode;
        const auto old_start = unit.mode_ms;
        dispatch_think();
        return monster.mode != old_mode || unit.mode_ms != old_start;
    };
    if (!monster.struck && monster.scheduled_action_ms && run_older_think(*monster.scheduled_action_ms)) return false;
    const bool elemental_beast_strike = monster.mode == "S1" && type_info.ai_name == "ElementalBeast";
    const bool attack = monster.mode == "A1" || monster.mode == "A2" || elemental_beast_strike;
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
        // A sequence skill (seq_diablorun): game.exe plays it in SQ (mode 14) at the SQ rate, not the run's.
        // Its do-func (FUN_005cd380, past the wind-up) sets the skill instance's flags to 1 (0x5cd498), so
        // FUN_00623f50 rates SQ as a moving mode from then on: walk base x walk % (mode_rate), 256ths of a
        // frame a tick, so the rows passed are the ticks x that rate / 256 (no whole-ms frame to round).
        const int frames = monster.diab_run >= 2
            ? int(std::int64_t((now_ms - monster.diab_run_ms) / 40) * std::max(mode_rate(game_data, monster, "SQ"), 1) / 256)
            : int((now_ms - monster.diab_run_ms)
                  / std::max<std::uint32_t>(game_data.npc_timing(monster.npc, "RN").ms_per_frame() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, "SQ")), 1));
        const auto contact = [&] {
            if (!foe.alive || d2d::rules::unit_distance(subtile(foe.x) - subtile(unit.x), subtile(foe.y) - subtile(unit.y), type_info.size, foe.size) > type_info.melee_rng + 1) return;
            auto stats = monster.swing_stats();
            stats.a1_min = d2d::rules::skill_phys(game_data.skills, *run_skill, {}, lvl, false) >> 8;
            stats.a1_max = std::max(d2d::rules::skill_phys(game_data.skills, *run_skill, {}, lvl, true) >> 8, stats.a1_min);
            for (auto& element : stats.elements) element.mode = "";
            const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), stats, false, monster.seed, false, (run_skill->result_flags & 1) != 0, false, foe.seed);
            foe.take(taken, monster.id);
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
            const int step = int(float(path_velocity(std::max(d2d::rules::eval_calc(game_data.skills, run_skill->calc[0], {}, monster.skill, lvl), 1))) * pace * chill);
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
            const auto taken = d2d::rules::monster_blow(struck.fighter, struck.level, struck.motion(), skill_strike(game_data, monster, *spin, lvl), false, monster.seed, false, false, false, struck.seed);
            struck.take(taken, monster.id);
            if (taken.hit) struck.struck_by(type_info.hit_class);
            if (taken.hit) monprop_hit(monster, struck, false);         // a melee blow: MonProp knock / crush, as the strike's
            struck.melee_by.push_back(monster.id);
            if (taken.hit) struck.landed_by.push_back(monster.id);
            if (taken.hit && taken.physical_fine > 0) struck.damaged_by.push_back(monster.id);
        }
        monster.seq_frame = (*seq)[std::size_t(rows) % seq->size()].frame;
        monster.seq_mode = (*seq)[std::size_t(rows) % seq->size()].mode;
        const float chill = now_ms < monster.chill_until ? float(100 + type_info.diff[std::size_t(monster.difficulty)].cold_effect) / 100.f : 1.f;
        const float pace = float(std::max(100 + monster.speed_pct + monster.boss_speed, 10)) / 100;
        const int step = int(float(path_velocity(type_info.velocity)) * pace * chill);
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
        const auto* active_skill = seq ? game_data.skills.get(monster.skill) : nullptr;
        if (seq) {
            const auto step_ms = active_skill && active_skill->srvdofunc == d2d::rules::ServerDoFunction::kDeathMaul
                ? 40u : std::max<std::uint32_t>(timing.ms_per_frame() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, "SQ")), 1);
            // Waiting at a leap's landing (FUN_005da120) it shows the row it rewinds to and steps back on; landed, the rows run on from there.
            const int played = monster.leap_hold_row >= 0 ? monster.leap_hold_row * int(step_ms) : std::max(int(now_ms - unit.mode_ms) - monster.seq_shift_ms, 0);
            const int reached = std::min(played / int(step_ms), int(seq->size()) - 1);
            monster.seq_mode = (*seq)[std::size_t(reached)].mode;
            monster.seq_frame = (*seq)[std::size_t(reached)].frame;
            // The row it waits on still shows, but its event stays pending till it lands: run in the air it strikes nothing and would be spent.
            const int events_to = monster.leaping && monster.leap_hold_row >= 0 ? reached - 1 : reached;
            for (; monster.seq_done <= events_to; ++monster.seq_done) {
                const int event = (*seq)[std::size_t(monster.seq_done)].event;
                if (event == 4 || ((event == 1 || event == 2) && !monster.seq_once)) ++due;
                // Jab's client do (cltdofunc 16, FUN_004f4590) on an event-3 row: Duriel's.
                if (const auto* jab = event == 3 ? game_data.skills.get(monster.skill) : nullptr; jab && jab->clt_do_func == 16) {
                    monster.seq_sound = jab->do_sound_b;
                    ++monster.seq_sounds;
                }
            }
        } else if (!monster.struck && now_ms >= (monster.scheduled_action_ms
            ? *monster.scheduled_action_ms : unit.mode_ms + action_due_ms(game_data, monster, monster.mode))) {
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
                const float speed = missile_speed(d2d::rules::missile_velocity(missile_info.vel, missile_info.vel_lev, lvl, slow_missiles_pct(game_data, monster, missile_info, now_ms)));
                const float distance = std::max(std::hypot(aim_x, aim_y), 0.01f);
                Missile x{ &missile_info, unit.x, unit.y, aim_x / distance * speed, aim_y / distance * speed, missile_facing(unit.x, unit.y, aim_x, aim_y), now_ms,
                           now_ms + std::uint32_t(missile_info.range) * 40, monster.stats };
                x.level = lvl; x.aim_at = { unit.x + aim_x, unit.y + aim_y }; x.owner = monster.id; x.owner_align = monster.align;
                const bool first = monster.mode == "A1", cast = monster.mode == "SC";   // an A1 shot carries A1's damage; S1's and SC's S1's (FUN_005a4f50)
                x.src = monster_shot_stats(missile_info, monster.swing_stats(), first, cast, s1_shot, lvl);
                // A do 2 row (the viper's poison javelin, FUN_005ae400): its trail from the subtile after its own; its
                // SrvCalc1 (FUN_0064b7c0) over 0 makes each cloud loop its SubLoop (level x 2 - 2) times (FUN_005a9720).
                if (const auto* trail = missile_info.subs[0]; missile_info.srv_do == 2 && trail) {
                    x.subtile_x = int(std::floor(float(unit.x) * 5)); x.subtile_y = int(std::floor(float(unit.y) * 5));
                    const int over = missile_info.srv_calc.empty() ? 0 : d2d::rules::eval_calc(game_data.skills, missile_info.srv_calc, {}, -1, lvl);
                    x.trail_loops = over > 0 && trail->sub_loop ? (trail->sub_stop - trail->sub_start) * (lvl * 2 - 2) : 0;
                }
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
                        y.aim_at = { unit.x + side_x, unit.y + side_y };
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
                        launched.aim_at = { launched.x + float(off_x) / 5, launched.y + float(off_y) / 5 };
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
                        if (missiles.size() > had) missiles.back().aim_at = offset_target(unit.x, unit.y, aim_x, aim_y);
                    }
                monster.seq_once = true;
            } else if (skill("GargoyleTrap")) {                               // srvdofunc 93 (FUN_005cc050): srvmissilea shafire3 square on at its target
                const auto [from_x, from_y, off_x, off_y] = d2d::rules::gargoyle_shot(subtile(unit.x), subtile(unit.y), subtile(foe.x), subtile(foe.y));
                const std::size_t had = missiles.size();
                andariel_missile(game_data, monster, "shafire3", float(off_x) / 5, float(off_y) / 5, now_ms, missiles, skill_level(game_data, monster, monster.skill));
                if (missiles.size() > had) {
                    auto& launched = missiles.back();
                    launched.x = (float(from_x) + 0.5f) / 5; launched.y = (float(from_y) + 0.5f) / 5;
                    launched.aim_at = { launched.x + float(off_x) / 5, launched.y + float(off_y) / 5 };
                }
            } else if (skill("Nest")) {                                       // srvdofunc 91 (FUN_005cbe00): its spawn at the skill's spot, in spawnmode
                // Through FUN_005b2f20 within 1 of the spot, flags 0x4020000 (no
                // experience, no drop); Skills +0xe6's overlay is none for Nest.
                monster.nesting = false;
                const int room = room_of(level, (float(monster.skill_x) + 0.5f) / 5, (float(monster.skill_y) + 0.5f) / 5);
                const auto young = game_data.monsters.by_id.find(type_info.spawn);
                if (int at_x = 0, at_y = 0; born && young != game_data.monsters.by_id.end()
                    && spawn_spot(game_data, level, crowd, rooms_in_play, young->second, monster.skill_x, monster.skill_y, 1, room, at_x, at_y)) {
                    auto laid = make_monster(game_data, young->second, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, d2d::rules::game_players(players_in(foes), players_counted), level.id, d2d::rules::Rng{ game_seed.next() }, level_region);
                    laid.aware = true; laid.no_exp = true; laid.no_drop = true;
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
                // target in reach, knockback (flags | 8). FUN_005a8670 tests flag 0x40
                // a row at a time: two rows due in one update strike once.
                if (!monster.leaping && !monster.seq_once) {
                    if (foe.alive && dist <= kMeleeReach + 0.3f) {
                        const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), skill_strike(game_data, monster, *leap, skill_level(game_data, monster, monster.skill)), false, monster.seed,
                                                                    false, false, false, foe.seed);
                        foe.take(taken, monster.id);
                        if (taken.hit) { foe.knocked_from = std::pair{ unit.x, unit.y }; foe.struck_by(type_info.hit_class); monprop_hit(monster, foe, false); }
                        d2d::log::info("{} lands its leap attack ({})", monster.npc.name, taken.hit ? "hit" : "missed");
                        foe.melee_by.push_back(monster.id);
                        if (taken.hit) foe.landed_by.push_back(monster.id);
                        if (taken.hit && taken.physical_fine > 0) foe.damaged_by.push_back(monster.id);
                    }
                    monster.seq_once = true;
                }
            } else if (const auto* maul = active_skill; maul && maul->srvdofunc == d2d::rules::ServerDoFunction::kDeathMaul) {
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
                    auto laid = make_monster(game_data, young->second, (float(put_x) + 0.5f) / 5, (float(put_y) + 0.5f) / 5, rng, monster.difficulty, true, d2d::rules::game_players(players_in(foes), players_counted), level.id, d2d::rules::Rng{ game_seed.next() }, level_region);
                    laid.aware = true; laid.no_exp = true;
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
                        auto& larva = larvae.emplace_back(make_monster(game_data, young->second, (float(put_x) + 0.5f) / 5, (float(put_y) + 0.5f) / 5, rng, monster.difficulty, true, d2d::rules::game_players(players_in(foes), players_counted), level.id, d2d::rules::Rng{ game_seed.next() }, level_region));
                        larva.aware = true; larva.no_exp = true;
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
                        raised.hit_points = raised.stats.hit_points;
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
                    // FUN_00621e40) a monster's one_shots, else Foe::flashed.
                    const auto& rows = game_data.overlay_rows;
                    const auto named = game_data.overlays.find(cast->srvoverlay);
                    if (const auto row = named == game_data.overlays.end() ? rows.end() : std::ranges::find(rows, &named->second); row > rows.begin() && row != rows.end()) {
                        const int overlay = int(row - rows.begin());
                        if (foe.monster && foe.monster >= pack.data() && foe.monster < pack.data() + pack.size()) { d2d::rules::add_one_shot(pack[std::size_t(foe.monster - pack.data())].one_shots, overlay, now_ms); }
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
                        const bool lusted = whipped.curse.skill == monster.skill && now_ms < whipped.curse.until;   // Bloodlust, a curse state: put_state's curse slot
                        const int chance = d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl);
                        const auto summon = game_data.monsters.by_id.find(cast->summon);
                        if (whipped.alive() && minion && chance <= monster.seed(100) && !lusted && summon != game_data.monsters.by_id.end()) {
                            morph_monster(game_data, whipped, summon->second, rng, d2d::rules::game_players(players_in(foes), players_counted), level_region);
                            whipped.aware = true;
                            set_mode(game_data, whipped, cast->summode.empty() ? "NU" : cast->summode, now_ms);
                            d2d::log::info("{} whips a minion into a suicide minion", monster.npc.name);
                        } else if (whipped.alive()) {
                            const int frames = std::max(d2d::rules::eval_calc(game_data.skills, cast->auralen, {}, monster.skill, lvl), 0);
                            put_state(game_data, whipped, { monster.skill, lvl, now_ms + std::uint32_t(frames) * 40 }, now_ms);
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
                    // ally (hit 18, FUN_005ab0b0) puts it there too. The nova's 64 go out
                    // on rules::kNovaRing (as do 27's) for their Range.
                    // The nova hits nothing: each ally it reaches gets the state there
                    // (Fight::world_step's pass over the monsters' hit-18 missiles).
                    const auto shout = game_data.missiles.find(cast->srvmissilea);
                    const int frames = std::max(d2d::rules::eval_calc(game_data.skills, cast->auralen, {}, monster.skill, lvl), 1);
                    const Monster::SkillState state{ monster.skill, lvl, now_ms + std::uint32_t(frames) * 40 };
                    if (!cast->aurastate.empty()) put_state(game_data, monster, state, now_ms);
                    for (int k = 0; shout != game_data.missiles.end() && k < 64; ++k) {
                        const auto [aim_x, aim_y] = d2d::rules::kNovaRing[std::size_t(k)];
                        const std::size_t had = missiles.size();
                        andariel_missile(game_data, monster, cast->srvmissilea, float(aim_x), float(aim_y), now_ms, missiles, lvl, cast);
                        if (missiles.size() > had) {
                            auto& ring_missile = missiles.back();
                            ring_missile.visual_only = true;
                            ring_missile.dies = now_ms + std::uint32_t(std::max(shout->second.range, 1)) * 40;
                            ring_missile.aim_at = offset_target(unit.x, unit.y, aim_x, aim_y);
                            ring_missile.skill = monster.skill; ring_missile.level = lvl; ring_missile.owner = monster.id; ring_missile.owner_align = monster.align;   // its state, for hit 18
                        }
                    }
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
                        if (near_foe.alive && !near_foe.monster && !foe.monster && std::hypot(near_foe.x - foe.x, near_foe.y - foe.y) * 5 <= float(radius))
                            near_foe.cursed = { monster.skill, lvl };
                } else if (cast->srvdofunc == ServerDoFunction::kNova) {
                    const auto ring = game_data.missiles.find(cast->srvmissilea);
                    const int reach = ring == game_data.missiles.end() ? 0
                        : ring->second.range + ring->second.lev_range * lvl + d2d::rules::eval_calc(game_data.skills, cast->calc[0], {}, monster.skill, lvl);
                    for (int k = 0; k < 64; ++k) {
                        const auto [aim_x, aim_y] = d2d::rules::kNovaRing[std::size_t(k)];
                        const std::size_t had = missiles.size();
                        andariel_missile(game_data, monster, cast->srvmissilea, float(aim_x), float(aim_y), now_ms, missiles, lvl, cast);
                        if (missiles.size() > had) { missiles.back().dies = now_ms + std::uint32_t(std::max(reach, 1)) * 40; missiles.back().aim_at = offset_target(unit.x, unit.y, aim_x, aim_y); }
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
                        if (near_foe.alive && !near_foe.monster && !foe.monster && std::hypot(near_foe.x - foe.x, near_foe.y - foe.y) * 5 <= float(radius))
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
                        // FUN_005c4df0, as the player's (Fight::corpse_blast): the corpse's life x calc1..calc2 % on the
                        // corpse's own seed (0x5c4f65, ESI the corpse), MulDiv'd down when it's above the caster's level;
                        // calc3 % of it to the EType, the rest physical, which reaches half as far.
                        const auto calc_of = [&](const d2d::rules::Calc& expr) { return d2d::rules::eval_calc(game_data.skills, expr, {}, monster.skill, lvl); };
                        const int life_fine = d2d::rules::corpse_life_fine(game_data.monsters, corpse.type, corpse.difficulty, corpse.stats.level);
                        const int whole = d2d::rules::corpse_blast_fine(life_fine, calc_of(cast->calc[0]), calc_of(cast->calc[1]), corpse.seed, corpse.stats.level, monster.stats.level);
                        const auto [phys_fine, fire_fine] = d2d::rules::corpse_blast_split(whole, calc_of(cast->calc[2]), cast->etype == 0);
                        const auto [walk, phys_reach] = d2d::rules::corpse_blast_reach(calc_of(cast->aurarange));
                        const int dmg = (phys_fine + fire_fine) >> 8;
                        const int corpse_x = subtile(corpse.unit.x), corpse_y = subtile(corpse.unit.y);
                        for (auto& near_foe : foes) {
                            const int apart = (subtile(near_foe.x) - corpse_x) * (subtile(near_foe.x) - corpse_x) + (subtile(near_foe.y) - corpse_y) * (subtile(near_foe.y) - corpse_y);
                            if (!near_foe.alive || apart > walk * walk) continue;
                            const auto stats = d2d::rules::corpse_blast_record(monster.stats, phys_fine, fire_fine, apart <= phys_reach * phys_reach);
                            const auto taken = d2d::rules::monster_blow(near_foe.fighter, near_foe.level, near_foe.motion(), stats, false, monster.seed, false, true, true);   // FUN_005c4d10: the record straight to FUN_0057c6c0
                            near_foe.take(taken, monster.id);
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
                        auto made = make_monster(game_data, young, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, d2d::rules::game_players(players_in(foes), players_counted), level.id, d2d::rules::Rng{ game_seed.next() }, level_region);
                        made.aware = true; made.no_exp = true; made.no_drop = true;
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
                    const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), stats, false, monster.seed, false, true, true);   // the record is min == max, not FUN_0057b420's roll
                    foe.take(taken, monster.id);
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
                    std::size_t prison_head = 0;
                    if (born && !d2d::rules::level_ids::is_town(level.id))
                        for (std::size_t k = 0; k < kPrison.size(); ++k) {
                            int at_x = 0, at_y = 0;
                            if (!spawn_spot(game_data, level, crowd, rooms_in_play, 0x154 + int(k), subtile(foe.x) + kPrison[k].first, subtile(foe.y) + kPrison[k].second, -1, foe_room, at_x, at_y)) {
                                if (k == 0) break;
                                continue;
                            }
                            auto bone = make_monster(game_data, 0x154 + int(k), (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, d2d::rules::game_players(players_in(foes), players_counted), level.id, d2d::rules::Rng{ game_seed.next() }, level_region);
                            if (k > 0) bone.owner_born = int(prison_head);   // FUN_005b31b0: a minion of the first
                            else prison_head = born->size();
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
                        int velocity = param << 6;                     // FUN_0059fa30: its Slow Missiles % first, then x 75 / 100
                        if (const int slow = ring != game_data.missiles.end() ? slow_missiles_pct(game_data, monster, ring->second, now_ms) : -1; slow >= 0) velocity = velocity * slow / 100;
                        const float speed = missile_speed(velocity * 75 / 100), length = std::hypot(float(off_x), float(off_y));
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
                        auto tentacle = make_monster(game_data, kind->second, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, d2d::rules::game_players(players_in(foes), players_counted), level.id, d2d::rules::Rng{ game_seed.next() }, level_region);
                        std::tie(tentacle.unit.x, tentacle.unit.y) = level.nearest_free(tentacle.unit.x, tentacle.unit.y);
                        tentacle.aware = true;
                        set_mode(game_data, tentacle, "A1", now_ms);
                        born->push_back(std::move(tentacle));
                    }
                } else if (cast->srvdofunc == ServerDoFunction::kHydra) {
                    // FUN_005ca910: three heads, head k of its summon's row plus k (hydra1..3) a subtile apart round the
                    // target (0x6e312c / 0x6e3120: rules::hydra), each through FUN_0056d940 at radius -1, then 4; none
                    // when the target's room is a town's; each gone Param1 frames on (AI control +0x14).
                    // ponytail: a monster's are three of hydra1 on the target, radius -1 alone (a player's: summons.cpp).
                    if (const auto summoned = game_data.monsters.by_id.find(cast->summon); born && summoned != game_data.monsters.by_id.end())
                        for (int head = 0; head < 3; ++head) {
                            int at_x = 0, at_y = 0;
                            if (!spawn_spot(game_data, level, crowd, rooms_in_play, summoned->second, subtile(foe.x), subtile(foe.y), -1, room_of(level, foe.x, foe.y), at_x, at_y)) continue;
                            auto hydra = make_monster(game_data, summoned->second, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, d2d::rules::game_players(players_in(foes), players_counted), level.id, d2d::rules::Rng{ game_seed.next() }, level_region);
                            hydra.aware = true;
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
                } else if (smite || monster.mode == "A2" || elemental_beast_strike) {
                    if (elemental_beast_strike) {
                        stats.a1_min = stats.s1_min; stats.a1_max = stats.s1_max; stats.to_hit = stats.to_hit_s1;
                    } else {
                        stats.a1_min = stats.a2_min; stats.a1_max = stats.a2_max; stats.to_hit = stats.to_hit2;
                    }
                    const std::string_view source_mode = elemental_beast_strike ? "S1" : "A2";
                    for (auto& element : stats.elements) element.mode = element.mode == source_mode ? "A1" : "";
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
                const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), stats, false, monster.seed, false, charge, false, foe.seed);
                // MonFrenzy's strike (do 109, FUN_005cdf10): a hit puts on its state
                // (FUN_005d8c70) for ln12 frames, one more charge up to its level.
                if (struck_with && struck_with->srvdofunc == d2d::rules::ServerDoFunction::kMonFrenzy && taken.hit) {
                    const int lvl = skill_level(game_data, monster, monster.skill);
                    monster.frenzy.skill = monster.skill;
                    monster.frenzy.level = std::min(monster.frenzy.level + 1, lvl);
                    monster.frenzy.until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data.skills, struck_with->auralen, {}, monster.skill, lvl), 1)) * 40;
                }
                foe.take(taken, monster.id);
                if (taken.hit) foe.struck_by(type_info.hit_class);
                if (taken.hit) monprop_hit(monster, foe, false);
                if (taken.hit && smite && (struck_with->result_flags & 8)) foe.knocked_from = std::pair{ unit.x, unit.y };
                if (taken.hit && smite) foe.stun_ticks = std::max(foe.stun_ticks, d2d::rules::eval_calc(game_data.skills, struck_with->calc[1], {}, monster.skill, skill_level(game_data, monster, monster.skill)));
                if (taken.hit && monster.mana_hi > 0) foe.mana_burn += d2d::rules::mana_burn_fine(monster.mana_lo, monster.mana_hi, rng);   // Mana Burn: 256ths, 256 times the points
                if (taken.hit) cursed_hit(game_data, monster, foes, foe);
                foe.melee_by.push_back(monster.id);
                if (taken.hit) foe.landed_by.push_back(monster.id);
                if (taken.hit && taken.physical_fine > 0) foe.damaged_by.push_back(monster.id);
                const auto& res = type_info.diff[std::size_t(monster.difficulty)].res;
                // Iron Maiden (FUN_005c5f50, rules::iron_maiden_fine) as the hit lands: its own record back, the
                // physical resisted on the monster (FUN_0057c1e0, res[0]), with Thorns' 0x4021 (below). It is the
                // striker's domeleedamage event, which FUN_0057c6c0 raises after FUN_0057c1e0 has run the struck
                // unit's own events (its absorbs: Bone Armor, Energy Shield, 0x57c2fc) and its reduction, so +8
                // is what they left. A player's absorbs come off as its blows resolve: its blow carries `reflect`
                // and Fight::hurt_side sends it back then; a merc or pet has none, so it goes now.
                const bool absorbs_later = !foe.pet && !foe.monster;
                if (const auto* maiden = monster.curse.skill >= 0 ? game_data.skills.get(monster.curse.skill) : nullptr; taken.hit && maiden && maiden->auratarget == "ironmaiden" && absorbs_later) {
                    if (!foe.blows.empty()) foe.blows.back().reflect = true;
                } else if (taken.hit && maiden && maiden->auratarget == "ironmaiden") {
                    const int physical = taken.raw >= 0 ? d2d::rules::reduce_fine_signed(taken.raw, foe.fighter.dr_pct, foe.fighter.dr_flat) : taken.physical << 8;
                    if (const auto back = d2d::rules::iron_maiden_fine(true, false, false, physical, monster.reflect_pct, 0, 0)) {
                        const int fine = d2d::rules::resisted_fine(*back, std::max(monster.target(game_data).res[0], -100));
                        const bool out_of_gh = monster.mode != "GH";
                        // Its flags are the skill's ResultFlags | 0x20 (FUN_005c5f50).
                        if (hurt(game_data, monster, fine >> 8, now_ms, fine & 255, maiden->hit_class, false, 0, record_gets_hit(maiden->result_flags | 0x20))) return true;
                        // The record is ResultFlags 0x4001 | 0x20 (0x4021, as Thorns'): FUN_0057cee0's 0x4000
                        // branch raises event 4 (FUN_005a43a0) whatever the damage, 0 included.
                        if (out_of_gh) monster.struck_event = true;
                    }
                }
                const int thorns = foe.fighter.thorns + d2d::rules::resisted(foe.fighter.thorns_light, res[3]) + taken.damage * foe.fighter.thorns_pct / 100;
                if (taken.hit && thorns > 0) {
                    // Thorns' record carries 0x4021 (FUN_005bf670): FUN_0057cee0 raises event 4
                    // (FUN_005a43a0) for 0x4000, no get-hit roll, and hook 4 tests the mode it was struck in.
                    const bool out_of_gh = monster.mode != "GH";
                    if (hurt(game_data, monster, thorns, now_ms, 0, 0, false, 0, record_gets_hit(0x4021))) return true;
                    if (out_of_gh) monster.struck_event = true;
                }
            }
        }
        if (monster.leaping) {                                          // Leap's flight (do 77, FUN_005d9f70): from its takeoff to its landing
            const std::uint32_t lands = monster.leap_until ? monster.leap_until : monster.mode_until;
            const std::uint32_t off = std::max(unit.mode_ms, monster.leap_takeoff);
            const float done = now_ms < off ? 0.f : leap_done(monster, now_ms - off, std::max<std::uint32_t>(lands > off ? lands - off : 1, 1));
            unit.x = monster.leap_from_x + (monster.leap_to_x - monster.leap_from_x) * done;
            unit.y = monster.leap_from_y + (monster.leap_to_y - monster.leap_from_y) * done;
        }
        // FUN_005da120: the landing event with the unit not on the landing yet waits there a tick at a time.
        const auto leap_step = std::max<std::uint32_t>(game_data.npc_timing(monster.npc, monster.mode).ms_per_frame() * 100 / std::uint32_t(100 + anim_rate_pct(game_data, monster, "SQ")), 1);
        while (monster.leaping && monster.leap_until && now_ms >= monster.leap_until && leap_hold(monster, type_info.base, leap_step)) {}
        if (monster.leaping && monster.leap_until && now_ms >= monster.leap_until) {   // Leap Attack down at row 11
            if (monster.leap_hold_row >= 0) {                          // the rows go on from the row it landed on
                monster.seq_shift_ms = int(monster.leap_until - unit.mode_ms) - monster.leap_hold_row * int(leap_step);
                monster.leap_hold_row = -1;
            }
            monster.leaping = false; monster.leap_until = 0; unit.x = monster.leap_to_x; unit.y = monster.leap_to_y;
            // On it (FUN_005da120's landing branch): any monster but sandleaper1 (0x4e, FUN_005da020's own landing)
            // cuts a row off what's left of its sequence (+0x48 & ~0xff, less 0x100), so it ends a row sooner.
            if (type_info.base != 0x4e && monster.mode_until > now_ms) monster.mode_until -= std::min(leap_step, monster.mode_until - now_ms);
            // sandleaper1's (FUN_005da020): with the unit it leapt at still there (FUN_00552f60 on its id), the
            // sequence goes to row 12 (FUN_006212c0, so rows 10 and 11, 11's event, are passed by) and the length
            // it has left keeps its whole rows and takes one more (+0x48 = (+0x48 & ~0xff) + 0x100, FUN_005da020); its
            // type 8 path from there is to the landing it stands on (FUN_005da120 passes the landing, +0x18 / +0x1c).
            if (type_info.base == 0x4e && std::ranges::any_of(foes, [&](const Foe& other) { return other.id == monster.leap_target && other.alive; })) {
                const std::uint32_t left_rows = monster.mode_until > now_ms ? (monster.mode_until - now_ms) / leap_step : 0;
                monster.seq_shift_ms = int(now_ms - unit.mode_ms) - 12 * int(leap_step);
                monster.mode_until = now_ms + (left_rows + 1) * leap_step;
            }
        }
        if (mode_over()) {
            if (monster.leaping) { monster.leaping = false; monster.leap_until = 0; unit.x = monster.leap_to_x; unit.y = monster.leap_to_y; }
            if (run_older_think(monster.mode_until)) return false;
            const auto end_tick = monster.mode_until;
            const bool think_after_end = monster.scheduled_action_ms && monster.next_act == end_tick
                && now_ms >= monster.stun_until && think_due();
            mode_ends();
            monster.skill = -1;
            if (think_after_end) {
                // FUN_005a73e0 files the idle think without removing the
                // same-tick entry already in the dispatch bucket.
                const auto idle_think = monster.next_act;
                monster.next_act = end_tick;
                dispatch_think();
                if (monster.next_act == end_tick) monster.next_act = idle_think;
                return false;
            }
        } else if (!think_due()) return false;
    }
    // A monster's velocitypercent (stat 0x43) starts at 75, not 100: its
    // stat initialiser FUN_00573cb0 ends in SetStat(unit, 0x43, 0x4b)
    // (0x573f16). Chilled, coldeffect goes on it (FUN_0057af80) with its
    // think's pace, Spider Lay's -100 % and the rest; the walk % is their
    // sum, at least 25 (FUN_00623f50, 0x62414e): laid (-100 %), it
    // creeps at that floor in game.exe too.
    const int walk = path_velocity(type_info.velocity, walk_pct);
    // A moving mode's path velocity is MonStats Velocity (FUN_00621360,
    // +0x32) x the walk %, RN too (FUN_00623f50, 0x6241a9); Run only comes
    // in through an AI's pace (Run x 100 / Velocity - 100) or a charge.
    const int run = walk;
    if (think(game_data, level, monster, foes, rng, now_ms, walk, run, crowd, pack, seen, open_door, born, rooms_in_play, players_counted, spawning)) return false;
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
        if (type_info.open_doors && on_door(level, unit, type_info.velocity != 0) && open_door && *open_door && (*open_door)(monster, now_ms)) {   // its door first, as think's
            if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
            monster.next_act = now_ms + 5 * 40;
            return false;
        }
        const Foe* found = search_target(game_data, level, monster, foes, seen, crowd).target;
        monster.aware = foe.alive && found;
        if (found) monster.chase = int(found - foes.data());
        if (!monster.aware) monster.next_act = now_ms + std::uint32_t(type_info.diff[std::size_t(monster.difficulty)].aidel) * 40;
    }
    if (foe.alive && monster.aware) {
        // The teleport mod (teleport_mod) for an AI think() doesn't run.
        if (now_ms >= monster.next_act && teleport_mod(game_data, level, monster, int(dist * 5), crowd, rooms_in_play, now_ms)) return false;
        if (dist <= kMeleeReach) {
            turn_to(unit, subtile(foe.x), subtile(foe.y));
            if (now_ms >= monster.next_act) { set_mode(game_data, monster, "A1", now_ms); monster.struck = false; attack_starts(game_data, monster, "A1"); }
            else if (monster.mode != "NU") set_mode(game_data, monster, "NU", now_ms);
            return false;
        }
        // Shooters (MissA2) shoot from up to 7 cells: each think (aidel)
        // the aip2 chance to fire, else close in.
        if (miss != game_data.missiles.end() && dist < 7 && now_ms >= monster.next_act) {
            monster.next_act = now_ms + std::uint32_t(type_info.diff[std::size_t(monster.difficulty)].aidel) * 40;
            if (rng(100) < std::max(type_info.diff[std::size_t(monster.difficulty)].aip[1], 1)) {
                turn_to(unit, subtile(foe.x), subtile(foe.y));
                set_mode(game_data, monster, "A2", now_ms);
                monster.struck = false;
                attack_starts(game_data, monster, "A2");
                return false;
            }
        }
        if (monster.mode != "WL") {                                // sets off at a think (FUN_005a7c20), else aidel on
            if (now_ms < monster.next_act) return false;
            monster.budget = 0x14;
            monster.path_steps = 5;                                 // FUN_005a6290's walk
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
    const float wander = cells_per_sec(float(type_info.velocity)) * elapsed * float(walk_pct) / 100;
    if (std::hypot(unit.goal_x - unit.x, unit.goal_y - unit.y) <= wander || !monster_step(level, monster, unit.goal_x, unit.goal_y, wander, crowd)) {
        if (std::hypot(unit.goal_x - unit.x, unit.goal_y - unit.y) <= wander) { unit.x = unit.goal_x; unit.y = unit.goal_y; }
        set_mode(game_data, monster, "NU", now_ms);
        unit.wait_until = now_ms + 2000 + std::uint32_t(rng(3000));
    }
    return false;
}

bool monster_step(const Level& level, Monster& monster, float target_x, float target_y, float step, const Crowd& crowd) {
    auto& unit = monster.unit;
    const float dx = target_x - unit.x, dy = target_y - unit.y, dist = std::hypot(dx, dy);
    if (dist < 0.01f) return true;
    face(unit, direction16(dx, dy));
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
    (void)walk_on(level, unit, int(std::lround(speed * elapsed * 5 * 4096)), crowd);   // still walking or not: the follow decides by distance, above
    if (std::hypot(unit.x - old_x, unit.y - old_y) > speed * elapsed * 0.3f) unit.stuck_since = 0;
    else if (!unit.stuck_since) unit.stuck_since = now_ms;
    else if (now_ms - unit.stuck_since > 1500) { unit.x = player_x + 1; unit.y = player_y + 1; unit.walking = false; unit.stuck_since = 0; path.clear(); }
}

int players_in(std::span<const Foe> foes) {
    return int(std::ranges::count_if(foes, [](const Foe& foe) { return !foe.pet && !foe.monster; }));
}

// A plain monster as FUN_005b2a00 makes it; a trap-melee's level is MonUMod 14's (FUN_005a3b50).
// Its unit seed (+0x20) is {a game seed step, 666}, taken as the unit is made (FUN_00555230 ->
// FUN_00552df0 -> FUN_00650e40), before its stat init FUN_00573cb0 draws on it: the look
// (FUN_005739d0, rules::monster_look), rows 0x137 / 0x138's rand(4) (0x573d68..0x573d9b),
// the life (FUN_0045c3e0(max - min + 1) at 0x573f8f, in monster_stats), then finish_unit_seed.
// A think's first draw sees the seed as all that left it.
Monster make_monster(const GameData& game_data, int type, float x, float y, d2d::rules::Rng& rng, int difficulty, bool stats, int players, int level_id,
                     std::optional<d2d::rules::Rng> unit_seed, d2d::rules::Region* region) {
    const auto& type_info = game_data.monsters.types[std::size_t(type)];
    Monster monster;
    monster.type = type;
    monster.level_id = level_id;
    monster.players = players;
    monster.killable = type_info.killable;
    // FUN_005b2a00: Align 1 good (stat 0xac 2), 2 neutral (1), else evil.
    monster.align = d2d::rules::unit_alignment(type_info.align);
    monster.row_align = type_info.align;
    monster.npc = game_data.mon_npc[std::size_t(type)];
    monster.seed = unit_seed ? *unit_seed : d2d::rules::Rng{ rng.next() };
    const auto* sets = region ? d2d::rules::region_sets(*region, game_data.monsters, type, monster.seed) : nullptr;   // FUN_00574250 -> FUN_00547bc0
    auto look = d2d::rules::monster_look(sets, type_info.choices, monster.seed);
    // The knights' rand(4) goes over the look's S3 byte (unit data +0xe), 0x138's S4's too (+0xf): their
    // missile's element shows as that part (pos, fir, col, unh).
    monster.variant = d2d::rules::monster_variant(type, monster.seed);
    if (monster.variant >= 0) {
        look[10] = std::uint8_t(monster.variant);
        if (type == 0x138) look[11] = std::uint8_t(monster.variant);
    }
    monster.look = look;
    for (std::size_t layer = 0; layer < 16; ++layer)
        if (look[layer] < type_info.parts[layer].size()) monster.npc.comp[layer] = type_info.parts[layer][look[layer]];
    monster.unit.x = monster.home_x = x;
    monster.unit.y = monster.home_y = y;
    face(monster.unit, rng(16));
    monster.unit.shape = monster.npc.shape;
    monster.unit.wait_until = std::uint32_t(rng(4000));
    if (stats) {
        const int area = area_monster_level(game_data, level_id, difficulty);
        // MonUMod 14 (spcdamage, every BaseId 0x162 by FUN_005b21b0; hook 0 FUN_005a3b50): its level is the
        // area's monster level on every difficulty, 1 at least; the life it rolled stays at the MonStats level.
        // Its to-hit (stat 0x13) = level + 50, at most 90, set after each swing's own stats (swing_stats).
        int level_add = 0;
        if (level_id >= 0 && std::ranges::contains(d2d::rules::family_mods(type_info.base), d2d::rules::umod::spcdamage)) {   // a known area, whatever its level
            const auto spc = d2d::rules::spcdamage(type_info.base, area);
            level_add = spc.level - d2d::rules::start_level(type_info, std::clamp(difficulty, 0, 2), area);
            monster.spc_to_hit = spc.to_hit;
        }
        monster.stats = d2d::rules::monster_stats(game_data.monsters, type, difficulty, monster.seed, level_add, players, area);
    }
    // FUN_00573cb0 keeps a zero roll (the town folk, critters, traps, Baal on
    // the stairs). game.exe's dead test is the mode (FUN_005541b0: DT / DD),
    // so it lives and thinks; d2d's is life, so it holds one point, `lifeless`:
    // any landed hit kills it, as life <= 0 after one does (0x57c8e2).
    monster.lifeless = monster.stats.hit_points <= 0;
    monster.stats.hit_points = std::max(monster.stats.hit_points, 1);
    monster.hit_points = monster.stats.hit_points;
    monster.difficulty = std::clamp(difficulty, 0, 2);
    if (stats) finish_unit_seed(game_data, monster);
    return monster;
}

// FUN_00573cb0 after the life: the MonProp row (FUN_00573470; its chance
// and min..max draws on the unit seed, 0x5741e0), then FUN_005d6b60 with
// oninit set at the unit's start level (0x574235), whose pick of each
// MonEquip row's item draws on it too. The items themselves (two game seed
// steps each) are the spawn's (objgroups.cpp `made`) or a pet's (summons.cpp).
void finish_unit_seed(const GameData& game_data, Monster& monster) {
    if (monster.type < 0 || std::size_t(monster.type) >= game_data.monsters.types.size()) return;
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    monster.props = d2d::rules::monster_props(game_data.rules, type_info, monster.difficulty, monster.seed);
    if (std::size_t(monster.type) >= game_data.mon_equip_first.size()) return;
    const int level_now = d2d::rules::start_level(type_info, monster.difficulty, area_monster_level(game_data, monster.level_id, monster.difficulty));
    (void)d2d::rules::monster_gear(game_data.mon_equip, game_data.mon_equip_first[std::size_t(monster.type)], monster.type, level_now, true, 0,
                                   std::nullopt, monster.seed);
}

// The Cursed umod's hit hook (FUN_005a2530), on a melee or missile hit that
// lands: a unique's or superunique's 3 in 4 on its own seed (a draw's low 2
// bits), Amplify Damage (skill 66) at level mlvl / 5 + 1 on every foe within
// its aurarangecalc (1..40 subtiles) of the one struck (FUN_0056dbc0 round
// FUN_0056d2c0's target; FUN_005a23d0, enemies only).

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
        if (near_foe.alive && !near_foe.monster && std::hypot(near_foe.x - struck.x, near_foe.y - struck.y) * 5 <= float(radius))
            near_foe.cursed = { 66, curse_level };
}

// rules::ai_swappable (FUN_005dd480): a champion is refused too (+0x16 & 0xa, its 0xd).
bool swaps_ai(const GameData& game_data, const Monster& monster) {
    const d2d::rules::CorpseState unit{ .mode = monster.alive() ? 1 : 0xc, .flags = d2d::rules::monster_data_flags(monster.boss), .targetable = monster.targetable(),
                                        .uninterruptable = monster.uninterruptable(), .attached = monster.riding };
    return d2d::rules::ai_swappable(game_data.monsters.types[std::size_t(monster.type)], unit);
}

void ai_toggle(const GameData& game_data, Monster& monster, int special) {
    if (d2d::rules::ai_toggle(monster.special, monster.other, { &monster.ai_state, &monster.ai_state2, &monster.ai_state3 }, special,
                              d2d::rules::has_other_think(game_data.monsters.types[std::size_t(monster.type)].ai_name)))
        monster.ai_command = 0;                                    // FUN_0058ede0: its command list freed
}

// A made monster's MonEquip items (rules::SpawnGear), each made as a drop on
// its seeds, summed (rules::gear_sum); and that gear on its stats: every
// mode's blow as FUN_0057b420 reads it, its defence (stat 31).
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
    // The items' other stats are wear_gear_stats'.
    for (auto [low, high] : { std::pair{ &stats.a1_min, &stats.a1_max }, std::pair{ &stats.a2_min, &stats.a2_max }, std::pair{ &stats.s1_min, &stats.s1_max } })
        std::tie(*low, *high) = d2d::rules::gear_damage(gear, *low, *high);
    stats.armor_class += int(gear.defense + gear.sum[d2d::d2s::kArmorClass]);
}

// What a monster's gear adds to its max life on a base of `base_fine`: maxhp
// (7, whole points) and item_maxhp_percent (76) of the base, as a merc's.
int gear_life_fine(const d2d::rules::GearSum& gear, int base_fine) {
    return int(gear.sum[d2d::d2s::kMaxLife]) * 256 + d2d::rules::mul_div(base_fine, int(gear.sum[d2d::d2s::kMaxLifePercent]), 100);
}

int boss_life_fine(int life, std::initializer_list<int> pcts, const d2d::rules::GearSum* gear) {
    for (const int pct : pcts) {
        if (pct == 0) continue;                                        // FUN_005a0d20 runs only for these mods
        const int whole = gear ? life + gear_life_fine(*gear, life) : life;
        life = int(std::uint32_t(whole) + std::uint32_t(d2d::rules::mul_div(whole, pct, 100)));
    }
    return life;
}

// The unit's stats are its base list's plus its items' (FUN_00625480 over the
// lists), so each item stat reaches what reads it: the resistances (36 damage,
// 37 magic, 39 / 41 / 43 / 45 the elements) into target(); max life (7, whole
// points) and its percent (76) of the base (gear_life_fine; a boss's scaling
// counted it already, make_boss); the attack rating (19) and its percent
// (119) on each mode's; fire, lightning, cold and magic damage (48..56) on
// every blow as a boss's enchants are (FUN_0057b7d0 reads the unit's
// stats), the cold's length too; poison (57..59) as its own poison stats.
void wear_gear_stats(Monster& monster, const d2d::rules::GearSum& gear) {
    using namespace d2d::d2s;
    const auto& sum = gear.sum;
    auto& stats = monster.stats;
    const std::array<int, 6> res_stats{ kDamageResist, kMagicResist, kFireResist, kLightningResist, kColdResist, kPoisonResist };
    for (std::size_t k = 0; k < res_stats.size(); ++k) monster.gear_res[k] += int(sum[std::size_t(res_stats[k])]);
    const int base_fine = monster.max_life_fine();
    const int extra_fine = gear_life_fine(gear, base_fine);
    if (extra_fine != 0) {
        const int max_fine = std::max(base_fine + extra_fine, 256);
        const int life_fine = std::max(monster.life_fine() + extra_fine, 1);
        stats.hit_points = (max_fine + 255) >> 8;
        monster.max_life_frac = stats.hit_points * 256 - max_fine;
        monster.set_life_fine(std::min(life_fine, max_fine));
    }
    for (int* to_hit : { &stats.to_hit, &stats.to_hit2, &stats.to_hit_s1 })
        *to_hit += int(sum[kToHit]) + *to_hit * int(sum[kToHitPercent]) / 100;
    const std::array<std::pair<int, int>, 5> element_stats{ { { kFireMinDamage, kFireMaxDamage }, { kLightningMinDamage, kLightningMaxDamage },
                                                              { kColdMinDamage, kColdMaxDamage }, { 0, 0 }, { kMagicMinDamage, kMagicMaxDamage } } };
    for (std::size_t type = 0; type < element_stats.size(); ++type) {
        const auto [low_stat, high_stat] = element_stats[type];
        if (low_stat == 0) continue;
        const int low = int(sum[std::size_t(low_stat)]), high = int(sum[std::size_t(high_stat)]);
        if (low == 0 && high == 0) continue;
        auto& enchant = stats.enchants[type];
        enchant = enchant ? std::pair{ enchant->first + low, enchant->second + high } : std::pair{ low, high };
    }
    stats.enchant_len += int(sum[kColdLength]);
    stats.poison_min += int(sum[kPoisonMinDamage]);
    stats.poison_max += int(sum[kPoisonMaxDamage]);
    stats.poison_len += int(sum[kPoisonLength]);
}

// MonUMod poison hit's init (FUN_005a1e00): its own poison stats.
void poison_hit(const GameData& game_data, Monster& monster, int difficulty, bool unique) {
    if (game_data.monsters.lvl.empty()) return;
    const auto level = std::clamp<std::size_t>(std::size_t(std::max(monster.stats.level, 1)), 1, game_data.monsters.lvl.size() - 1);
    const int difficulty_index = std::clamp(difficulty, 0, 2), damage_pct = game_data.monsters.lvl[level].damage[std::size_t(difficulty_index)];
    const int base = unique ? 28 : 16;
    monster.stats.poison_min = damage_pct * game_data.umods.constants[std::size_t(base + difficulty_index)] / 100;
    monster.stats.poison_max = damage_pct * game_data.umods.constants[std::size_t(base + 3 + difficulty_index)] / 100;
    monster.stats.poison_len = (int(level) * 5 + 150) * 2;
}

// A champion / unique / superunique / minion (rules/uniques.hpp,
// FUN_005a2120): a higher level, more life, damage and to-hit,
// resistances, speed, an element; its name. At full life. `players`: in
// the game (rules::monster_stats' bonus). `gear`: what it wears from the
// start (wear_gear), before its mods scale it.
void make_boss(const GameData& game_data, Monster& monster, d2d::rules::Boss kind, const std::vector<int>& mods, int super, int name_seed,
               int difficulty, d2d::rules::Rng& rng, int players, const d2d::rules::GearSum* gear) {
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    const auto boss = d2d::rules::boss_stats(game_data.umods, type_info, kind, mods, difficulty);
    monster.stats = d2d::rules::monster_stats(game_data.monsters, monster.type, difficulty, rng, boss.level_add, players, area_monster_level(game_data, monster.level_id, difficulty));
    if (gear) wear_gear(monster.stats, *gear);
    monster.boss = kind;
    // A minion takes its leader's mods' inits (FUN_005a2120's party callback FUN_0058f380 ->
    // FUN_005a2100, flag 0), not their bytes (+0x1c): none of the mods' events are its own.
    monster.mods = kind == d2d::rules::Boss::minion ? std::vector<int>{} : mods;
    monster.super = super;
    monster.name_seed = name_seed;
    // FUN_005a0d20 scales max life in 256ths, first for hpmultiply and then for the champion kind.
    // It reads the unit's whole max life (FUN_00625d10: its gear's too, made with it in FUN_00573cb0)
    // and writes that scaled as the base stat 7, the gear's list still adding on top
    // (wear_gear_stats): a boss's item life counts in each scaling and again after.
    int life = boss_life_fine(monster.stats.hit_points * 256, { boss.hp_pct, boss.life_after_pct }, gear);
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
    if (!game_data.monsters.lvl.empty()) {                                         // enchanted: MonLvl damage x %, each element its own
        const auto& level_row = game_data.monsters.lvl[std::min<std::size_t>(std::size_t(monster.stats.level), game_data.monsters.lvl.size() - 1)];
        const int damage_pct = level_row.damage[std::size_t(std::clamp(difficulty, 0, 2))];
        for (std::size_t element = 0; element < boss.elem_pct.size(); ++element)
            if (const auto& pct = boss.elem_pct[element]) {
                const int low = damage_pct * pct->first / 100;
                monster.stats.enchants[element] = std::pair{ low, std::max(damage_pct * pct->second / 100, low) };
            }
        monster.stats.enchant_len = d2d::rules::boss_elem_length(boss, monster.stats.level, int(game_data.monsters.lvl.size()) - 1);
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
    if (std::ranges::contains(mods, d2d::rules::umod::aura) && kind != d2d::rules::Boss::minion) {   // FUN_005a1650 skips a minion (flag 0)
        const auto aura = d2d::rules::boss_aura(monster.stats.level, name_seed, super);
        monster.aura = aura.skill; monster.aura_lvl = aura.level;
    }
    if (super == 6) { monster.half_freeze = true; monster.special = 0xd; }   // FUN_005a49b0 case 6, the Countess: FUN_00639db0(0x76) (state 118), FUN_005b0e00(0xd)
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
    if (gear) wear_gear_stats(monster, *gear);
}

// Bone Armor's absorb on a monster (event 22, FUN_005c6370): the pool takes
// the hit's physical; at 0 the state goes. All in 256ths: the damage left.
int soak(Monster& monster, int physical, int damage) {
    if (monster.armor_pool <= 0 || physical <= 0 || damage <= 0) return damage;
    const int absorbed = std::min({ monster.armor_pool, physical, damage });
    monster.armor_pool -= absorbed;
    if (monster.armor_pool <= 0) monster.armored = false;
    return damage - absorbed;
}

// Damage to a monster: it dies (DT, then its corpse, DD), or recoils (GH)
// by rules::get_hit (FUN_0057cb00); either way it notices.
// True when this killed it.
// `hit_class` and `marked` (an element or a doubled hit) are the blow's, for
// its get-hit (rules::get_hit); a source without them (thorns, a blast) is class 0.
// `crush` (256ths) is a Crushing Blow's share of the damage: the life it takes
// is not in the record's total (+0x4c), which is the get-hit's size (FUN_0057cb00).
bool hurt(const GameData& game_data, Monster& monster, int damage, std::uint32_t now_ms, int fraction, int hit_class, bool marked, int crush, bool rolls_get_hit) {
    if (!monster.targetable() || (damage <= 0 && fraction <= 0 && !monster.lifeless)) return false;
    // This blow's own size in 256ths, for its get-hit, before the carried
    // part of a point joins it: the record's total (+0x4c), which a Crushing
    // Blow's life is not in (FUN_0057c1e0 sums it before the strike events).
    const std::int64_t blow_fine = std::max<std::int64_t>((std::int64_t(std::max(damage, 0)) << 8) + std::max(fraction, 0) - std::max(crush, 0), 0);
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
    // A record without the get-hit bit (record_gets_hit) takes neither.
    else if (rolls_get_hit
             && (now_ms < monster.stun_until
                 || (d2d::rules::get_hit(monster.max_life_fine(), blow_fine, hit_class, marked, monster.seed)
                     && game_data.monsters.types[std::size_t(monster.type)].can_get_hit)))
        set_mode(game_data, monster, "GH", now_ms);
    return !monster.alive();
}

// FUN_0057cee0's monster branch (not in mode 0 or 0xc, no state 0x36) tests
// the record's flags (+4) in order: 2 (FUN_0057ccb0), 8 (a knockback, as 4
// for a monster without KB: FUN_0046c140), 0x10, then 4: the get-hit roll
// (FUN_0057cb00, skipped under a stun, state 0x15). A record with 0x4000 and
// no 4 (Static Field's 0x4001 at 0x5c97d2, Thorns' 0x4021 from
// FUN_005bf670) only refreshes the unit (FUN_0064c040, +0xc4 |= 0x8000) and
// raises event 4 (FUN_005a43a0): no get-hit, stunned or not.
bool record_gets_hit(int result_flags) {
    return (result_flags & (2 | 8 | 0x10)) == 0 && (result_flags & 4) != 0;
}

// Fight::land's blow (FUN_0057c6c0's record): `rolls_get_hit` as hurt's.
bool blow_hurt(const GameData& game_data, Monster& monster, const d2d::rules::Blow& blow, std::uint32_t now_ms, bool rolls_get_hit) {
    // Bone Armor's pool takes the record's fine physical (FUN_005c6370 on +8).
    const int taken = soak(monster, blow.phys_fine, (blow.damage << 8) + blow.frac);
    return hurt(game_data, monster, taken >> 8, now_ms, taken & 255, blow.hit_class, blow.elemental || blow.deadly, blow.crush_fine, rolls_get_hit);
}

void heal(Monster& monster, int points) {
    if (points <= 0 || !monster.alive()) return;
    const int healed = monster.hit_points + points;
    monster.hit_points = std::min(healed, monster.stats.hit_points);
    if (healed > monster.stats.hit_points) monster.life_frac = monster.max_life_frac;
    else if (healed == monster.stats.hit_points) monster.life_frac = std::max(monster.life_frac, monster.max_life_frac);
}

bool follow_path(const Level& level, UnitState& unit, int velocity, const Crowd& crowd, bool* blocked) {
    // No route: the same stop as a blocked step (LAB_006508ff), and it counts
    // as one, so a walk to a spot does not path again off the snap.
    const bool no_route = unit.path.empty();
    std::vector<std::pair<int, int>> points;
    points.reserve(unit.path.size());
    for (const auto& [point_x, point_y] : unit.path) points.emplace_back(subtile(point_x), subtile(point_y));
    std::size_t point = 0;
    const bool going = path_frame(level, unit, points, point, velocity, crowd, blocked);
    unit.path.erase(unit.path.begin(), unit.path.begin() + std::ptrdiff_t(point));
    if (no_route && blocked) *blocked = true;
    return going;
}

void block_anim(const GameData& game_data, Monster& monster, std::uint32_t now_ms) {
    if (monster.alive() && monster.mode != "A1" && monster.mode != "A2" && game_data.npc_timing(monster.npc, "BL").directions) set_mode(game_data, monster, "BL", now_ms);
}

// A monster's frame: finish an attack / get-hit / death; flee; notice the
// player within 8 cells (then keep after them within 16), walk up and
// attack (A1: the hit lands on AnimData's event frame, MonStats A1TH vs
// the foe's defense, then block, damage reduction, resistances), wait
// aidel ticks between attacks;
// otherwise wander near home (Levels.txt MonWndr): stand 2-5 s, walk to a
// random spot within 3 cells.
// An AI whose think isn't traced (rules::traced_ai) gets one melee think
// (MonStats aip1..8 unread); distances and timings by eye; chasing goes
// straight at the player, sliding to a stop at walls. No monster a level
// places has one (every level, map seeds 1..4 and 7): it's a safety net;
// town NPCs, pets and quest NPCs think elsewhere.
// A unique's attack starting (the mode-change hook, event 0): Spectral Hit
// picks this attack's element (uniques.hpp kSpectralElement), in el[2].
void attack_starts(const GameData& game_data, Monster& monster, std::string_view mode) {
    using d2d::rules::Boss;
    if ((monster.boss != Boss::unique && monster.boss != Boss::superunique) || !std::ranges::contains(monster.mods, d2d::rules::umod::spectralhit)
        || game_data.monsters.lvl.empty()) return;
    const auto& level_row = game_data.monsters.lvl[std::min<std::size_t>(std::size_t(monster.stats.level), game_data.monsters.lvl.size() - 1)];
    const int damage_pct = level_row.damage[std::size_t(monster.difficulty)];
    const int element = d2d::rules::kSpectralElement[std::size_t(monster.seed(5))];   // FUN_005a3040: the unit's seed (0x5a304c)
    const int low = damage_pct * game_data.umods.constants[28] / 100;
    monster.stats.elements[2] = { element, 100, low, std::max(damage_pct * game_data.umods.constants[31] / 100, low), element == 2 || element == 3 ? 40 : 0, mode };
    monster.element_on[2] = true;   // set_mode rolled the slot's last element; this one is the swing's, 100 %
}


// A monster's mode rate over its AnimData speed, % more (FUN_00623f50,
// 0x623f50): its attack modes (A1, A2: 0x6e8b90 / 0x6e8cd0 mark them so
// for every monster, FUN_00621580) at 100 + attack_pct (attackrate from
// auras) + its MonProp IAS, clamped 15..175; its cast mode (SC,
// FUN_006216e0) at 100 + its FCR, at most 175, no attackrate /
// other_animrate; IAS and FCR each 120 x v / (120 + v) (FUN_00621740's
// table at 0x6e8e24). S1..S4 and SQ (a skill's sequence) are attack modes
// too while the monster's current skill has UseAttackRate (FUN_00621580:
// the mode row's +8, Skills record byte +6 & 8; `skill_attack_rate`), unless
// they're moving ones (S1..S4 below class 0x19a: own_rate_mode). Any
// other at 100 + anim_pct (other_animrate), clamped 15..175. The modes whose
// rate isn't a % of their AnimData speed (moving ones, GH, BL, KB, DT, DD)
// take the GameData overload below (mode_rate).
// A get-hit's length in ms: rules::get_hit_ticks on the mode's AnimData
// record (frames and speed, the COF's for a mode animdata lacks) and the
// unit's FHR, a tick 40 ms.
std::uint32_t get_hit_ms(const GameData::AnimTiming& timing, int fhr) {
    const int frames = int(timing.frames ? timing.frames : timing.cof_frames), speed = int(timing.speed ? timing.speed : timing.cof_speed);
    return std::uint32_t(d2d::rules::get_hit_ticks(frames, speed, fhr)) * 40u;
}

// How far along its line a takeoff has flown after `elapsed_ms` (0..1): at the
// takeoff's pace (FUN_005d9f70) when it has one, else along `span_ms`, the sequence's time.
float leap_done(const Monster& monster, std::uint32_t elapsed_ms, std::uint32_t span_ms) {
    const float length = std::hypot(monster.leap_to_x - monster.leap_from_x, monster.leap_to_y - monster.leap_from_y);
    if (monster.leap_pace > 0 && length > 0) return leap_moved(monster.leap_pace, elapsed_ms, length) / length;
    return std::clamp(float(elapsed_ms) / float(std::max<std::uint32_t>(span_ms, 1)), 0.f, 1.f);
}

// FUN_005da120's wait (d2d::rules::leap_wait): the unit's subtile (its path's 16.16 x / y, the words at +2 / +6:
// FUN_006488c0 / FUN_00648900) against the landing's; on it, short of the centre, it lands. Else the rewound row's event a tick on.
// Only sandleaper1 and the ancients rewind to a row (leap_wait: any other BaseId's row is 0); the landing's one-row
// cut of the length (+0x48) is the caller's.
bool leap_hold(Monster& monster, int base_id, std::uint32_t step_ms) {
    if (monster.leap_pace <= 0) return false;
    const std::uint32_t off = std::max(monster.unit.mode_ms, monster.leap_takeoff);   // flown from its takeoff row, as the flight is
    const float flown = leap_done(monster, monster.leap_until > off ? monster.leap_until - off : 0, 1);
    if (flown >= 1.f) return false;
    const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
    const float at_x = monster.leap_from_x + (monster.leap_to_x - monster.leap_from_x) * flown, at_y = monster.leap_from_y + (monster.leap_to_y - monster.leap_from_y) * flown;
    const int off_x = subtile(at_x) - subtile(monster.leap_to_x), off_y = subtile(at_y) - subtile(monster.leap_to_y);
    if (off_x == 0 && off_y == 0) return false;
    const int apart = d2d::rules::ai_distance(off_x, off_y);
    const auto wait = d2d::rules::leap_wait(base_id, apart, 0, 0);
    if (wait.position == 0) return false;
    monster.leap_hold_row = int(wait.position >> 8) + 1;
    monster.leap_until += step_ms; monster.mode_until += step_ms;
    return true;
}

// A knocked-back unit's slide (mode KB, Fight::knock_back: a monster, a
// pet or the merc): at the knockback's pace to its leap_to, then it stands
// till the mode ends (LAB_005a8630 steps the path each frame and leaves
// only at the mode's end, FUN_006217c0). True while the mode lasts.
bool knock_slide(Monster& monster, std::uint32_t now_ms) {
    const float dx = monster.leap_to_x - monster.leap_from_x, dy = monster.leap_to_y - monster.leap_from_y, length = std::hypot(dx, dy);
    const float done = length > 0 ? knock_moved(now_ms - monster.unit.mode_ms, length) / length : 1.f;
    monster.unit.x = monster.leap_from_x + dx * done;
    monster.unit.y = monster.leap_from_y + dy * done;
    return now_ms < monster.mode_until;
}

// A monster's MonProp hit events on a foe it hit (FUN_005c0c30 over its
// event list +0x90; the melee event, or the missile one, 6, for its
// missile's): each draws on the monster's seed.
// - Knockback (stat 81, func 7, FUN_005bf960): a draw's low 7 bits under
//   the foe's `knock_under`, result flag 8 from where the monster stands;
//   a player slides in Fight::hurt_side, a pet or the merc as a monster does.
// - Crushing Blow (stat 136, func 16, FUN_005bffc0): a draw % 100 under
//   the value; it takes current life / the foe's `crush_divisor`, doubled
//   for a missile, less its damageresist (crushing_blow), after the hit.
void monprop_hit(Monster& monster, Foe& foe, bool missile) {
    if (monster.props.knock > 0 && int(monster.seed.next() & 0x7f) < foe.knock_under) foe.knocked_from = std::pair{ monster.unit.x, monster.unit.y };
    if (monster.props.crush > 0 && int(monster.seed.next() % 100) < monster.props.crush)
        foe.crushed.push_back(foe.crush_divisor * (missile ? 2 : 1));
}

// What a Crushing Blow takes off `life` (FUN_005bffc0): life / divisor,
// less its share of the raw damageresist (stat 0x24, 100 at most; none
// when 0 or less), not the 50 % a hit's reduction stops at.
std::int64_t crushing_blow(std::int64_t life, int divisor, int damage_resist) {
    const std::int64_t crush = life / std::max(divisor, 1);
    return crush - (damage_resist > 0 ? crush * std::min(damage_resist, 100) / 100 : 0);
}

// A monster unit (a pet, or a monster made a foe by Confuse or Attract) as
// MonProp knock and crush see it. Knock: under 0x20 for a MonStats2 large
// one, 0x80 (always) a small one (FUN_005bf960: MonStats2 +5 & 8 / & 4,
// [0x6ce274] / [0x6ce270]). Crush: Monster::crush_divisor.
void monster_foe_props(const GameData& game_data, const Monster& monster, Foe& foe) {
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    foe.neutral = monster.mode == "NU";
    foe.knock_under = type_info.large ? 0x20 : type_info.small ? 0x80 : 0x40;
    foe.crush_divisor = monster.crush_divisor(game_data);
}

// pSrvDmgFunc 3 (FUN_005ad220): result flag 0x4000, a soft hit, when one step
// of the missile's own seed (+0x20: low * 0x6ac690c5 + high) has its low 7
// bits under dParam1 (+0x78). The step is kept, the roll passed or not. `rng`
// only gives a missile with no seed yet its first value (see Missile::seed).
bool soft_hit(Missile& missile, d2d::rules::Rng& rng) {
    if (!missile.info || missile.info->dmg_func != 3) return false;
    if (!missile.seed) missile.seed = d2d::rules::Rng(rng.next());   // {a step, 666}: FUN_00552df0
    return int(missile.seed->next() & 0x7f) < missile.info->dmg_par1;
}

// FUN_0059fa30's flag 2: the target point [7] / [8] subtiles from the
// start's subtile (a nova's ring, FUN_0056d400; PrimePoisonNova's;
// Frozen Orb's spin and burst), in cells: that subtile's centre, for
// Missile::aim_at.
std::pair<float, float> offset_target(float x, float y, int offset_x, int offset_y) {
    return { (std::floor(x * 5) + float(offset_x) + 0.5f) / 5, (std::floor(y * 5) + float(offset_y) + 0.5f) / 5 };
}

// A foe of `size` at (x, y) cells is in a subtile a missile entered this
// frame (FUN_005ae1f0 -> FUN_00641cb0).
bool missile_struck(const Level& level, const Missile& missile, float x, float y, int size) {
    const int foe_x = level.world_x * 5 + int(std::floor(x * 5)), foe_y = level.world_y * 5 + int(std::floor(y * 5));
    const int missile_size = missile.info ? missile.info->size : 1;
    return std::ranges::any_of(missile.flight.crossed, [&](const auto& spot) { return d2d::rules::missile_touches(spot.first, spot.second, foe_x, foe_y, size, missile_size); });
}

// A standing missile (no velocity) strikes from its own subtile every frame
// (FUN_005ae1f0, nothing crossed): a Size 2 shard takes a size 1 unit on its
// own and the 4 neighbouring subtiles, a size 2 unit on 13, size 3 on 21; a
// Size 1 Fire Wall flame 1 / 5 / 9. A row with no Size 1..3 keeps the half
// cell it always had here.
bool standing_struck(const Missile& missile, float x, float y, int size) {
    const int missile_size = missile.info ? missile.info->size : 0;
    if (missile_size < 1 || missile_size > 3) return std::hypot(x - missile.x, y - missile.y) <= 0.5f;
    const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
    return d2d::rules::missile_touches(subtile(missile.x), subtile(missile.y), subtile(x), subtile(y), size, missile_size);
}

// A missile's facing for a heading with no point to aim at (a velocity, a
// ring's or a burst's spoke): the 64-step direction of its angle, scaled
// to 255 subtiles along (direction64's 32-bit sums wrap past 258).
int missile_heading(float dx, float dy) {
    const float longer = std::max(std::abs(dx), std::abs(dy));
    if (longer <= 0) return 0;
    return d2d::rules::direction64(0, 0, int(std::lround(dx * 255 / longer)), int(std::lround(dy * 255 / longer)));
}

// A missile's facing from (x, y) at the point (dx, dy) cells on: game.exe's
// 64-step direction between their subtiles (rules::direction64, as
// FUN_0059fa30 aims at the target; 0 straight down, clockwise on screen),
// floored as missile_fly flies it. The draw takes its frame from it
// (missile_direction). A step of a cell or less is a unit heading (a
// spoke's cos / sin), not a point: its angle.
int missile_facing(float x, float y, float dx, float dy) {
    if (std::hypot(dx, dy) <= 1.01f) return missile_heading(dx, dy);
    const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
    return d2d::rules::direction64(subtile(x), subtile(y), subtile(x + dx), subtile(y + dy));
}

// `element` onto a missile's stats: poison per frame / frames in MonStats::poison_*,
// any other type as an A2 El row in whole points (shared by missile_blow and the
// map's trap missiles).
void put_missile_element(d2d::rules::MonStats& stats, const d2d::rules::MissileDamage& element) {
    if (element.etype == 3) {
        stats.poison_min = element.elo; stats.poison_max = element.ehi; stats.poison_len = element.elen;
    } else if (element.etype >= 0)
        stats.elements[0] = { element.etype, 100, element.elo >> 8, element.ehi >> 8, element.elen, "A2" };
}

// A monster missile's blow (andariel_missile): its row's physical in
// 256ths, `element` (its row's or a carried skill's at the level); it always
// hits. Poison goes in MonStats::poison_*: per frame in 256ths for ELen frames,
// the record's +0x34 / +0x38 / +0x3c, which monster_blow applies as it is (an
// `elements` entry of type 3 is the MonStats El convention, roll x 10 a frame
// for 2 x dur frames, up to 62x as much).
d2d::rules::MonStats missile_blow(const GameData::MissileInfo& info, const d2d::rules::MissileDamage& element, int monster_level) {
    d2d::rules::MonStats stats;
    stats.level = monster_level;
    stats.no_hit_roll = !info.to_hit;                                // a ToHit row rolls its owner's attack rating (the caller's)
    stats.a2_min = info.min >> 8; stats.a2_max = std::max(info.max >> 8, stats.a2_min);
    stats.unblockable = !(info.min > 0 && info.max > 0);              // FUN_005ad730: can-block is the record's physical (+8) != 0 (Fallen Shaman fire has none)
    put_missile_element(stats, element);
    return stats;
}

MonsterShotPhys monster_shot_phys(const GameData::MissileInfo& info, int src_min, int src_max, int lvl) {
    const int shift = info.hitshift & 31;
    const auto part = [&](int src, int row, const std::array<int, 5>& lev) {
        const int owner = info.src_damage <= 0 ? 0 : int(std::int64_t(src) * 256 * info.src_damage / 128);
        return owner + ((row + d2d::rules::level_bonus(lev, lvl)) << shift);
    };
    return { part(src_min, info.min, info.min_lev), part(src_max, info.max, info.max_lev) };
}

// The owner's stats as the missile carries them (what monster_blow rolls on
// impact): the physical as a2_min / a2_max whole points, the owner's own
// El rows of the shot's mode as A2's, and the row's own element (FUN_0064b100 /
// 0064b1d0 / 0064b2a0 at the missile's level: Sand Maggot poison, the pots) in a
// free slot; poison goes into MonStats::poison_min / poison_max / poison_len
// instead (per frame in 256ths for ELen frames, what the record's +0x34 / +0x38 /
// +0x3c hold and what monster_blow applies on every hit); the owner's own poison is
// dropped on a SrcDamage-0 row and summed in otherwise (the rule below).
d2d::rules::MonStats monster_shot_stats(const GameData::MissileInfo& info, d2d::rules::MonStats stats, bool first, bool cast, bool s1_shot, int lvl) {
    // A cast's (SC) owner damage is the S1 columns' (FUN_005a4f50: modes 7 and 8 take FUN_006538a0's 0x20 set).
    const int src_min = first ? stats.a1_min : s1_shot || cast ? stats.s1_min : stats.a2_min;
    const int src_max = first ? stats.a1_max : s1_shot || cast ? stats.s1_max : stats.a2_max;
    const auto phys = monster_shot_phys(info, src_min, src_max, lvl);
    stats.a2_min = phys.low >> 8; stats.a2_max = phys.high >> 8;
    if (s1_shot) stats.to_hit = stats.to_hit_s1;                    // S1TH (monster_blow rolls to_hit)
    stats.no_hit_roll = !info.to_hit;
    stats.unblockable = !(phys.low > 0 && phys.high > 0);            // FUN_005ad730: can-block is the record's physical (+8) != 0
    if (first || cast || s1_shot)                                   // and its elements (a Skeleton Mage's El1 A1): the blow rolls A2's
        for (auto& element : stats.elements) element.mode = element.mode == (cast ? "SC" : s1_shot ? "S1" : "A1") ? "A2" : "";
    const auto row = d2d::rules::row_damage(info.etype, info.emin, info.emax, info.emin_lev, info.emax_lev, info.hitshift, info.elen, info.elen_lev, lvl);
    // The owner's poison (game.exe, V read: FUN_0064b860 / FUN_0064a930 / FUN_0064aa60; V run: the
    // re-verifier's vm_game2 oracle, goospit1 with owner poison 40 / 40 / 300, whole OP set): added
    // only when SrcDamage != 0 (+0x12d; all 32 poison rows of Missiles.txt have 0, so the owner's
    // is dropped); then the rate pair (owner + row) is scaled by SrcDamage / 128 (truncated toward
    // 0) and the LENGTH (owner + row) is summed, unscaled. U: whether a cast (MissC) shot takes the same path.
    // SrcDamage is read as an unsigned byte there (V read: movzx at 0x64b8ed), so a -1 row is 255; the
    // other branch (a missile with a Skill, 0x64b9da) maps 0xff to 0, which this plain-shot path does not
    // take. No monster Miss row has -1 (only player skill missiles do), so it is not reachable today.
    const int src_damage = info.src_damage & 0xff;
    const bool row_poison = row.etype == 3 && (row.ehi > 0 || row.elo > 0);
    if (src_damage == 0) stats.poison_min = stats.poison_max = stats.poison_len = 0;
    if (row_poison) { stats.poison_min += row.elo; stats.poison_max += row.ehi; stats.poison_len += row.elen; }
    if (src_damage != 0) {
        stats.poison_min = int(std::int64_t(stats.poison_min) * src_damage / 128);
        stats.poison_max = int(std::int64_t(stats.poison_max) * src_damage / 128);
    }
    // A boss's enchants are its own stats 0x30..0x38 (fire / lightning / cold min, max, the
    // cold's length): FUN_0064a930 adds the owner's to the record in 256ths (EBX 8 at
    // 0x64becf) only when SrcDamage isn't 0, then FUN_0064aa60 scales the record by
    // SrcDamage / 128, the length too; so an A1, A2 or S1 shot alike. Kept in whole points.
    // A MissC shot takes this plain branch too: FUN_0064b860's other one is for a row with a Skill
    // (+0x194), and the only MissC rows in 1.14d, the Wills o' the Wisp's willowisplightningbolt /
    // willowisplightningbolt2, have none (SrcDamage 128; MonStats / Missiles read in tools/emu). So a
    // cast carries the enchants as any shot, and the owner's stats 21 / 22 as its SC mode set them
    // (FUN_005a4f50, the S1 columns: empty for every willowisp row, so no physical).
    if (src_damage == 0) stats.enchants = {};
    for (auto& enchant : stats.enchants)
        if (enchant) *enchant = { enchant->first * src_damage / 128, enchant->second * src_damage / 128 };
    stats.enchant_len = stats.enchant_len * src_damage / 128;
    if (!row_poison && row.etype >= 0 && (row.ehi > 0 || row.elo > 0))
        if (const auto free_slot = std::ranges::find_if(stats.elements, [](const auto& element) { return element.type < 0 || element.mode.empty(); }); free_slot != stats.elements.end())
            *free_slot = { row.etype, 100, row.elo >> 8, row.ehi >> 8, row.elen, "A2" };
    return stats;
}

// `info` from (x, y) cells along (dx, dy), as `from`'s (its owner, level,
// monster level), with its own row's blow.
Missile missile_from(const Missile& from, const GameData::MissileInfo& info, float x, float y, float dx, float dy, std::uint32_t now_ms) {
    const auto element = d2d::rules::row_damage(info.etype, info.emin, info.emax, info.emin_lev, info.emax_lev, info.hitshift, info.elen, info.elen_lev, from.level);
    Missile out{ &info, x, y, dx, dy, missile_facing(x, y, dx, dy), now_ms,
                 now_ms + std::uint32_t(std::max(info.range + info.lev_range * from.level, 1)) * 40, missile_blow(info, element, from.src.level) };
    out.level = from.level; out.owner = from.owner; out.owner_align = from.owner_align;
    return out;
}

// Fire Hit's and Jab's hit (FUN_005cae40, FUN_005db2d0): the swing's S1 damage,
// to-hit and S1 elements as A1's, plus the skill's to-hit as points (FUN_006449f0
// into FUN_0057ec10, as skill_strike).
void s1_strike(d2d::rules::MonStats& stats, const d2d::rules::SkillTables& skill_tables, const d2d::rules::Skill& skill, int lvl) {
    stats.a1_min = stats.s1_min; stats.a1_max = stats.s1_max;
    stats.to_hit = stats.to_hit_s1 + d2d::rules::skill_tohit(skill_tables, skill, {}, lvl);
    for (auto& element : stats.elements) element.mode = element.mode == "S1" ? "A1" : "";
}

// A plain monster of MonStats row `type` at (x, y) cells: its components
// and stats rolled (a boss's by make_boss instead: `stats` false), for
// `players` in the game.
// Levels.txt MonLvl1Ex / MonLvl2Ex / MonLvl3Ex of the level `level_id` for the
// difficulty (what FUN_0061dca0 returns in an expansion game; 1 for an id off
// the table or under 1, as it does); -1 when the level isn't known.
[[nodiscard]] int area_monster_level(const GameData& game_data, int level_id, int difficulty) {
    if (level_id < 0) return -1;
    if (level_id == 0 || std::size_t(level_id) >= game_data.area_level.size()) return 1;
    return game_data.area_level[std::size_t(level_id)][std::size_t(std::clamp(difficulty, 0, 2))];
}

// Baal Corpse Explode's corpses (srvdo 141, FUN_005d2e80): FUN_0056dcc0's
// area (0x56dcc0, flags 0x3002, radius 40) through FUN_0065aa40
// (0x65aa40): a monster whose subtile is within the radius of the
// caster's (dx² + dy² <= r²; every near room passes, bugs.md #23), flag
// 0x1000 only in mode DD (a DT corpse isn't one); not spent before.
bool baal_corpse_spent(const Monster& corpse, int off_x, int off_y) {
    return !corpse.alive() && corpse.mode == "DD" && !corpse.corpse_used && off_x * off_x + off_y * off_y <= 40 * 40;
}

auto Crowd::at(float x, float y, const UnitState* self) const -> bool {
    static const UnitShape player;
    return at(x, y, self ? self->shape : player, self);
}

auto Crowd::at(float x, float y, const UnitShape& mover, const UnitState* self) const -> bool {
    const int sub_x = int(std::floor(x * 5)), sub_y = int(std::floor(y * 5));
    const int half = mover.size / 2;
    auto covers = [&](int cell_x, int cell_y) {
        if (mover.pattern == 0) return cell_x == sub_x && cell_y == sub_y;
        if (mover.box()) return cell_x >= sub_x - half && cell_x < sub_x - half + mover.size && cell_y >= sub_y - half && cell_y < sub_y - half + mover.size;
        return std::abs(cell_x - sub_x) + std::abs(cell_y - sub_y) <= 1;
    };
    const auto blocks = [&](const UnitState* other) {
        if (other == self || !other->shape.stamps() || !(other->shape.bit() & mover.mask)) return false;
        const int other_x = int(std::floor(other->x * 5)), other_y = int(std::floor(other->y * 5));
        if (covers(other_x, other_y)) return true;
        static constexpr std::pair<int, int> kNeighbours[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        return other->shape.box() && std::ranges::any_of(kNeighbours, [&](const auto& step) { return covers(other_x + step.first, other_y + step.second); });
    };
    return std::ranges::any_of(units, blocks) || std::ranges::any_of(held, [&](const auto& mark) { return *mark.second && blocks(mark.first); });
}

}  // namespace d2d::game
