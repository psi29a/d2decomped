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
    auto clear = [&](float from_x, float from_y, float to_x, float to_y) {
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
        out.push_back(std::move(monster));
    }
    return out;
}

void set_mode(const GameData& game_data, Monster& monster, std::string_view mode, std::uint32_t now_ms) {
    monster.mode = mode;
    monster.unit.mode_ms = now_ms;
    monster.unit.walking = mode == "WL";
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

bool monster_update(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, d2d::rules::Rng& rng,
                    std::uint32_t now_ms, float elapsed, const Crowd& crowd, std::vector<Missile>& missiles) {
    auto& unit = monster.unit;
    // After the nearest one alive (the player or the merc).
    Foe* pick = &foes[0];
    for (auto& foe : foes)
        if (foe.alive && (!pick->alive || std::hypot(foe.x - unit.x, foe.y - unit.y) < std::hypot(pick->x - unit.x, pick->y - unit.y))) pick = &foe;
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
    if (monster.mode == "A1" || monster.mode == "A2") {
        if (!monster.struck && now_ms >= unit.mode_ms + game_data.npc_timing(monster.npc, monster.mode).action_ms()) {
            monster.struck = true;
            if (monster.mode == "A2" && miss != game_data.missiles.end()) {         // fire: at the foe, from here
                const auto& missile_info = miss->second;
                const float speed = cells_per_sec(float(missile_info.vel)), distance = std::max(dist, 0.01f);
                Missile x{ &missile_info, unit.x, unit.y, dx / distance * speed, dy / distance * speed, direction32(dx, dy), now_ms,
                           now_ms + std::uint32_t(missile_info.range) * 40, monster.stats };
                x.src.a2_min = monster.stats.a2_min * missile_info.src_damage / 128 + missile_info.min;
                x.src.a2_max = monster.stats.a2_max * missile_info.src_damage / 128 + missile_info.max;
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
            } else if (foe.alive && dist <= kMeleeReach + 0.3f) {
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
        monster.next_act = now_ms + std::uint32_t(type_info.diff[std::size_t(monster.difficulty)].aidel) * 40;
    }
    // Chilled, it moves at coldeffect % slower.
    const float chill = now_ms < monster.chill_until ? float(100 + type_info.diff[std::size_t(monster.difficulty)].cold_effect) / 100.f : 1.f;
    const float walk = cells_per_sec(float(type_info.velocity)) * elapsed * chill * float(std::max(100 + monster.speed_pct + monster.boss_speed, 10)) / 100;
    if (now_ms < monster.flee_until) {
        if (monster.mode != "WL") set_mode(game_data, monster, "WL", now_ms);
        if (!monster_step(level, monster, unit.x - dx, unit.y - dy, cells_per_sec(float(type_info.run)) * elapsed * chill, crowd)) monster.flee_until = 0;
        return false;
    }
    // Blind (Dim Vision, Cloak of Shadows): it doesn't see past arm's length.
    if (foe.alive && (dist < 8 || (monster.aware && dist < 16)) && (now_ms >= monster.blind_until || dist < 1.5f)) {
        monster.aware = true;
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
        // ponytail: aip2 read as the shoot chance; QuillRat's think isn't traced.
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
        // Straight at the player, else sidestep round whoever's in the way.
        bool moved = false;
        for (const float turn : { 0.f, 0.785f, -0.785f, 1.571f, -1.571f }) {
            const float cosine = std::cos(turn), sine = std::sin(turn);
            if ((moved = monster_step(level, monster, unit.x + dx * cosine - dy * sine, unit.y + dx * sine + dy * cosine, walk, crowd))) break;
        }
        if (moved != (monster.mode == "WL")) set_mode(game_data, monster, moved ? "WL" : "NU", now_ms);
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

void fallen_scatter(const GameData& game_data, std::vector<Monster>& ms_, std::size_t dead, d2d::rules::Rng& rng, std::uint32_t now_ms) {
    const auto& dead_monster = ms_[dead];
    if (game_data.monsters.types[std::size_t(dead_monster.type)].ai_name != "Fallen") return;
    for (auto& monster : ms_)
        if (&monster != &dead_monster && monster.alive() && monster.leader == dead_monster.leader && std::hypot(monster.unit.x - dead_monster.unit.x, monster.unit.y - dead_monster.unit.y) < 10
            && game_data.monsters.types[std::size_t(monster.type)].ai_name == "Fallen")
            monster.flee_until = now_ms + 2000 + std::uint32_t(rng(1000));
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
