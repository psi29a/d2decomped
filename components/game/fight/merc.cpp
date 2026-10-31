// SPDX-License-Identifier: GPL-3.0-or-later
// Fight (fight.hpp): the hireling: its fighter, gear and turn. The rest is in this directory.
#include "fight.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "gamedata.hpp"
#include "internal.hpp"
#include "log.hpp"

#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <merc.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

auto Fight::merc_fighter() const -> d2d::rules::Fighter {
        using namespace d2d::d2s;
        // Its stats as a unit's (FUN_00572840's base, its worn items'
        // props, sockets and set bonuses summed, as a player's are), read as
        // game.exe reads any unit's:
        // - damage (FUN_0057b420): a two-handed weapon reads stats 0x17 /
        //   0x18, where the level-up put its base damage; a one-handed one or
        //   none 0x15 / 0x16, which it doesn't (1 / 2 at least); the weapon's
        //   own damage and enhanced damage as make_fighter has them;
        // - attack rating (combat.md, a monster's): stat 19 + dex x 5,
        //   x (100 + stat 119) / 100;
        // - defence (FUN_006223f0): stat 31 (its base, its items' own
        //   defence after their enhanced defence) + dex / 4, + skill_armor_percent
        //   (Frozen Armor);
        // - resistances: its base + its items', less the difficulty's penalty
        //   (FUN_0057be00 takes it off a player or a hireling: ctx +0x14 0),
        //   DifficultyLevels ResistPenalty in LoD, -100 .. 75 + max-resist
        //   (95 at most).
        // - block (FUN_00622720 for a monster): stat 20 (MonStats ToBlock,
        //   none for a hireling, its shield's block and its items' to-block),
        //   75 at most, only with a shield (FUN_006225f0: never for act3hire,
        //   0x167; no other merc can hold one).
        const auto gear = merc_gear();
        auto sum = gear.sum;
        const auto& weapon_sum = gear.weapon_sum;
        const Item *weapon = gear.weapon, *shield = gear.shield;
        const std::int64_t item_def = gear.item_def, per_level = gear.per_level;
        const auto weapon_info = weapon ? game_data->rules.item_info.find(weapon->code) : game_data->rules.item_info.end();
        if (weapon_info != game_data->rules.item_info.end() && weapon_info->second.two_handed) {
            sum[kMinDamage] += actor->merc_st.dmg_min;                           // stats 0x17 / 0x18
            sum[kMaxDamage] += actor->merc_st.dmg_max;
        }
        sum[kToHit] += actor->merc_st.attack_rating;
        // A monster's curse on it (Amplify Damage, Weaken): its aurastats
        // join its stats (merc_foe drops a spent one).
        if (const auto* skill = actor->curses[1].skill >= 0 ? game_data->skills.get(actor->curses[1].skill) : nullptr) {
            const d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, actor->merc_st.level, nullptr };
            for (std::size_t k = 0; k < skill->aurastat.size(); ++k)
                if (skill->aurastat[k] >= 0 && std::size_t(skill->aurastat[k]) < sum.size())
                    sum[std::size_t(skill->aurastat[k])] += d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], env, skill->id, actor->curses[1].level);
        }
        Stats stats;
        stats.values[kLevel] = actor->merc_st.level;
        stats.values[kStr] = actor->merc_st.str + sum[kStr];
        stats.values[kDex] = actor->merc_st.dex + sum[kDex];
        d2d::rules::ClassGains gains;
        gains.to_hit = 35;                                                // make_fighter's (dex - 7) x 5: a monster's dex x 5
        std::int64_t defense = actor->merc_st.def + item_def + sum[kArmorClass] + per_level * actor->merc_st.level / 8 + stats.values[kDex] / 4;
        if (const auto* skill = actor->merc_buff.skill >= 0 ? game_data->skills.get(actor->merc_buff.skill) : nullptr) {
            const d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, actor->merc_st.level, nullptr };
            for (std::size_t k = 0; k < skill->aurastat.size(); ++k)
                if (skill->aurastat[k] == kSkillArmorPercent)
                    defense += defense * d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], env, skill->id, actor->merc_buff.level) / 100;
        }
        defense += defense * merc_cry_stat(kSkillArmorPercent) / 100;   // Shout's
        const int difficulty = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        const auto penalty = actor->character.header.expansion() ? game_data->resist_penalty[std::size_t(difficulty)] : std::array<std::int64_t, 3>{ 0, -20, -50 }[std::size_t(difficulty)];
        std::array<int, 4> res{};
        constexpr std::array<int, 4> kResStat{ 39, 41, 43, 45 };          // fire, lightning, cold, poison (+1: its max)
        for (std::size_t k = 0; k < 4; ++k)
            res[k] = int(std::clamp<std::int64_t>(actor->merc_st.resist + sum[std::size_t(kResStat[k])] + penalty, -100, std::min<std::int64_t>(75 + sum[std::size_t(kResStat[k] + 1)], 95)));
        auto fighter = d2d::rules::make_fighter(game_data->rules, weapon, shield, sum, weapon_sum, stats, gains, int(defense), res);
        fighter.block = 0;
        if (const auto found = shield && actor->merc_npc && actor->merc_npc->hc_idx != 0x167 ? game_data->rules.item_base.find(shield->code) : game_data->rules.item_base.end();
            found != game_data->rules.item_base.end())
            fighter.block = int(std::clamp<std::int64_t>(found->second.block + sum[kToBlock], 0, 75));
        return fighter;
    }

auto Fight::merc_gear() const -> MercGear {
        using namespace d2d::d2s;
        MercGear gear;
        auto add = [](d2d::rules::StatSum& into, const std::vector<ItemProp>& props) {
            for (const auto& prop : props) if (prop.stat >= 0 && std::size_t(prop.stat) < into.size()) into[std::size_t(prop.stat)] += prop.value;
        };
        for (const auto& item : actor->character.merc_items) {
            if (item.location != item_location::kEquipped) continue;
            add(gear.sum, item.props);
            for (const auto& socketed : item.socketed_items) add(gear.sum, socket_props(*game_data, item, socketed));
            add(gear.sum, set_bonus_props(*game_data, actor->character.merc_items, item));
            std::int64_t enhanced_defense = 0;
            for (const auto& prop : item.props) {
                if (prop.stat == kArmorPercent) enhanced_defense += prop.value;
                if (prop.stat == kArmorPerLevel) gear.per_level += prop.value;
            }
            if (item.defense > 0) gear.item_def += item.defense * (100 + enhanced_defense) / 100;
            if (item.slot != body_location::kRightArm && item.slot != body_location::kLeftArm) continue;
            const auto info = game_data->rules.item_info.find(item.code);
            const auto found = game_data->rules.item_base.find(item.code);
            if (info == game_data->rules.item_info.end() || found == game_data->rules.item_base.end()) continue;
            if (info->second.kind == 2 && (!gear.weapon || item.slot == body_location::kRightArm)) gear.weapon = &item;
            if (info->second.kind == 1 && found->second.block > 0) gear.shield = &item;
        }
        if (gear.weapon) {
            add(gear.weapon_sum, gear.weapon->props);
            for (const auto& socketed : gear.weapon->socketed_items) add(gear.weapon_sum, socket_props(*game_data, *gear.weapon, socketed));
        }
        return gear;
    }

auto Fight::merc_cry_stat(int stat) const -> int {
        const auto* skill = actor->merc_cry.skill >= 0 ? game_data->skills.get(actor->merc_cry.skill) : nullptr;
        if (!skill) return 0;
        const d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, actor->merc_st.level, nullptr };
        int sum = 0;
        for (std::size_t k = 0; k < skill->aurastat.size(); ++k)
            if (skill->aurastat[k] == stat) sum += d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], env, skill->id, actor->merc_cry.level);
        return sum;
    }

auto Fight::merc_max_life() const -> int {
        // Its level-up life (stat 7) + its items' maxhp and hp per level (216,
        // an eighth a level), the items' maxhp percent (76) on the level-up's.
        // ponytail: vitality's share isn't given (a hireling has no CharStats
        // row); its items' mana goes nowhere (d2d keeps no merc mana).
        const auto gear = merc_gear();
        return int(std::max<std::int64_t>(actor->merc_st.life + gear.sum[d2d::d2s::kMaxLife] + gear.sum[216] * actor->merc_st.level / 8
                                              + std::int64_t(actor->merc_st.life) * (gear.sum[d2d::d2s::kMaxLifePercent] + merc_cry_stat(d2d::d2s::kMaxLifePercent)) / 100, 1));
    }

// A think's move as a path by its type (FUN_00649970) from subtile
// (from_x, from_y): the cells to walk, empty when none.
auto move_route(const Level& level, const UnitState& unit, int from_x, int from_y, const d2d::rules::MercMove& move, const Crowd& crowd)
    -> std::vector<std::pair<float, float>> {
    auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
    auto blocked = [&](int x, int y) { return level.unit_blocked(centre(x), centre(y), unit.shape) || crowd.at(centre(x), centre(y), &unit); };
    const int steps = move.steps ? move.steps : 5;
    std::vector<std::pair<int, int>> points;
    if (move.type == 7) points = d2d::rules::player_path(from_x, from_y, move.x, move.y, 0, false, blocked, steps);
    else if (move.type == 1) points = d2d::rules::search_path(from_x, from_y, move.x, move.y, false, blocked);
    else if (move.type == 0xf) points = d2d::rules::wall_path(from_x, from_y, move.x, move.y, steps, false, blocked);
    else points = d2d::rules::monster_path(from_x, from_y, move.x, move.y, false, blocked, steps);
    std::vector<std::pair<float, float>> route;
    std::pair last{ from_x, from_y };
    for (const auto& point : points)
        if (point != last) { route.emplace_back(centre(point.first), centre(point.second)); last = point; }
    return route;
}

// A unit put at the free spot nearest (x, y) cells (FUN_00554ea0 ->
// FUN_0064e7b0, its size, mask 0x1c09); none: it stays.
void put_near(const Level& level, const Crowd& crowd, UnitState& unit, float x, float y) {
    if (const auto spot = free_spot_near(level, crowd, &unit, int(std::floor(x * 5)), int(std::floor(y * 5)), unit.shape.size, 0x1c09)) {
        unit.x = (float(spot->first) + 0.5f) / 5; unit.y = (float(spot->second) + 0.5f) / 5;
    }
}

// A pet's or the merc's think mode 3 (FUN_005e3930): a spot in the owner's
// room (FUN_0054dc40: up to 20 tries inside its rect less a subtile each
// side, on the room's seed, each where MonStats class 0x16b fits:
// FUN_005b2f20 radius -1, test only, spawn_spot), then put_near it.
// False when no try fits.
bool room_teleport(const GameData& game_data, const Level& level, const Crowd& crowd, Spawning& spawning, UnitState& unit) {
    if (spawning.room_level != &level) return false;
    auto& in_play = spawning.levels[spawning.room_level];
    if (!keep_camp_seeds(game_data, level, in_play)) return false;   // the builder's busy: the next think
    const auto room = spawning.room < 0 ? std::nullopt : level_room(level, std::size_t(spawning.room));
    if (!room || std::size_t(spawning.room) >= in_play.room_seeds.size()) return false;
    for (int tries = 0; tries < 20; ++tries) {
        auto& seed = in_play.room_seeds[std::size_t(spawning.room)];
        const int x = room->x * 5 + 1 + seed(room->width * 5 - 1), y = room->y * 5 + 1 + seed(room->height * 5 - 1);
        if (int fit_x = 0, fit_y = 0; !spawn_spot(game_data, level, crowd, &in_play, 0x16b, x, y, -1, spawning.room, fit_x, fit_y)) continue;
        put_near(level, crowd, unit, (float(x) + 0.5f) / 5, (float(y) + 0.5f) / 5);
        return true;
    }
    return false;
}

auto Fight::merc_turn(std::uint32_t now_ms, float elapsed, const Crowd& crowd, bool town) -> void {
        auto& unit = actor->merc->unit;
        const auto set = [&](std::string_view mode) {
            actor->merc->mode = mode; unit.mode_ms = now_ms; unit.walking = mode == "WL" || mode == "RN";
            if (!unit.walking) unit.path.clear();
            // ponytail: the merc's FHR is its items' (merc_fighter); a state's stat 99 isn't kept, as a monster's.
            actor->merc->mode_until = mode == "NU" || unit.walking ? 0
                : mode == "GH"                                  ? now_ms + d2d::game::get_hit_ms(game_data->npc_timing(*actor->merc_npc, mode), merc_fighter().fhr)
                                                                : now_ms + game_data->npc_timing(*actor->merc_npc, mode).length_ms();
        };
        if (actor->merc->mode == "GH" || actor->merc->mode == "DT") actor->merc_seq = nullptr;   // struck or dying: the sequence is cut
        if (actor->merc_buff.skill >= 0 && now_ms >= actor->merc_buff.until) actor->merc_buff = {};
        if (actor->merc_cry.skill >= 0 && now_ms >= actor->merc_cry.until) { actor->merc_cry = {}; actor->merc->hit_points = std::min(actor->merc->hit_points, merc_max_life()); }
        if (actor->merc->mode == "DT") {
            if (now_ms >= actor->merc->mode_until) { actor->merc.reset(); actor->character.header.merc_dead = true; d2d::log::info("the merc died"); }
            return;
        }
        if (actor->merc->mode == "KB") { if (d2d::game::knock_slide(*actor->merc, now_ms)) return; set("GH"); }   // knocked back, then its get-hit (as a monster's)
        if (actor->merc->mode == "GH") { if (now_ms < actor->merc->mode_until) return; set("NU"); actor->merc->next_act = now_ms; }
        const bool archer = actor->merc_npc && actor->merc_npc->id == "roguehire";
        const float reach = archer ? 6.f : kMeleeReach;
        if (actor->merc_seq && actor->merc->mode != "NU" && actor->merc->mode != "WL" && actor->merc->mode != "RN") {   // an SQ skill: a hit at each event-1 frame
            const auto frame_ms = std::max<std::uint32_t>(game_data->npc_timing(*actor->merc_npc, actor->merc->mode).ms_per_frame(), 1);
            const int reached = std::min(int((now_ms - unit.mode_ms) / frame_ms), int(actor->merc_seq->size()) - 1);
            for (; actor->merc_seq_done <= reached; ++actor->merc_seq_done) {
                if ((*actor->merc_seq)[std::size_t(actor->merc_seq_done)].event != 1 || actor->merc_target < 0) continue;
                auto& target = monsters[std::size_t(actor->merc_target)];
                const auto* skill = actor->merc_skill >= 0 ? game_data->skills.get(actor->merc_skill) : nullptr;
                if (target.alive() && std::hypot(target.unit.x - unit.x, target.unit.y - unit.y) <= kMeleeReach + 0.3f) {
                    const auto swing_with = skill ? skill_swing(*skill, std::max(actor->merc_skill_level, 1), merc_calc_env(), false) : d2d::rules::Swing{};
                    merc_strike(std::size_t(actor->merc_target), swing_with, now_ms);
                }
            }
            if (now_ms < actor->merc->mode_until) return;
            actor->merc_seq = nullptr;
            set("NU");
            actor->merc->next_act = now_ms;
        }
        if (actor->merc->mode != "NU" && actor->merc->mode != "WL" && actor->merc->mode != "RN") {   // an attack: its skill (or blow) on the action frame
            if (!actor->merc->struck && actor->merc_target >= 0 && now_ms >= unit.mode_ms + game_data->npc_timing(*actor->merc_npc, actor->merc->mode).action_ms()) {
                actor->merc->struck = true;
                auto& target = monsters[std::size_t(actor->merc_target)];
                const auto* skill = actor->merc_skill >= 0 ? game_data->skills.get(actor->merc_skill) : nullptr;
                const Caster caster{ unit.x, unit.y, std::max(actor->merc_skill_level, 1), actor->merc_target, target.unit.x, target.unit.y, true };
                if (skill && skill->srvdofunc != ServerDoFunction::kAttack && missile_skill(*skill)) fire(*skill, now_ms, caster);
                else if (skill && spot_skill(*skill)) spot(*skill, now_ms, caster);
                else if (skill && self_cast(*skill)) {                    // Frozen Armor: its state on the merc for auralen frames
                    const int length = d2d::rules::eval_calc(game_data->skills, skill->auralen, merc_calc_env(), skill->id, caster.level);
                    actor->merc_buff = { skill->id, caster.level, length > 0 ? now_ms + std::uint32_t(length) * 40 : ~0u };
                }
                else {
                const float dx = target.unit.x - unit.x, dy = target.unit.y - unit.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                if (!skill && archer && game_data->missiles.contains("arrow")) {
                    const auto& missile_info = game_data->missiles.at("arrow");
                    const float speed = missile_speed(missile_info, actor->merc_st.level);
                    Missile missile{ &missile_info, unit.x, unit.y, dx / distance * speed, dy / distance * speed, missile_facing(unit.x, unit.y, dx, dy), now_ms, now_ms + std::uint32_t(missile_info.range) * 40, {} };
                    missile.to = { target.unit.x, target.unit.y };
                    const auto fighter = merc_fighter();                    // its bow's damage and attack rating with its gear
                    missile.min = fighter.min; missile.max = fighter.max; missile.attack_rating = fighter.attack_rating; missile.level = actor->merc_st.level; missile.friendly = true; missile.caster = actor->id; missile.by_merc = true;
                    missiles.push_back(std::move(missile));
                } else if (target.alive() && distance <= kMeleeReach + 0.3f) {
                    // A melee skill (Jab, Bash, Stun) builds its hit as the
                    // player's do (skill_swing) at the merc's skill level.
                    // ponytail: Jab's SQ hits come as one.
                    const auto swing_with = skill ? skill_swing(*skill, std::max(actor->merc_skill_level, 1), merc_calc_env(), false) : d2d::rules::Swing{};
                    merc_strike(std::size_t(actor->merc_target), swing_with, now_ms);
                }
                }
            }
            if (now_ms < actor->merc->mode_until) return;
            set("NU");
            actor->merc->next_act = now_ms;
        }
        const auto* type_info = actor->merc->type >= 0 ? &game_data->monsters.types[std::size_t(actor->merc->type)] : nullptr;
        if ((actor->merc->mode == "WL" || actor->merc->mode == "RN") && actor->merc_chase) {       // at a foe: in reach (or it's gone), think again
            const bool gone = actor->merc_target < 0 || !monsters[std::size_t(actor->merc_target)].alive();
            if (gone || std::hypot(monsters[std::size_t(actor->merc_target)].unit.x - unit.x, monsters[std::size_t(actor->merc_target)].unit.y - unit.y) <= reach) {
                set("NU");
                actor->merc_chase = false;
                actor->merc->next_act = now_ms;
            }
        }
        if (actor->merc->mode == "WL" || actor->merc->mode == "RN") {                    // velocitypercent: the move's pace
            const int velocity = type_info ? (actor->merc->mode == "RN" ? type_info->run : type_info->velocity) : 6;
            if (walk_on(*level, unit, cells_per_sec(float(velocity)) * float(std::max(100 + actor->merc->move_pct, 10)) / 100 * elapsed, crowd)) return;
            set("NU");
            actor->merc_chase = false;
            actor->merc->next_act = now_ms;                                      // a walk's end thinks at once (FUN_005a8030)
        }
        if (actor->merc->mode != "NU" || now_ms < actor->merc->next_act) return;
        auto sub = [](float cells) { return int(std::floor(cells * 5)); };
        auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
        const int merc_x = sub(unit.x), merc_y = sub(unit.y), size = actor->merc_npc->size_x > 0 ? actor->merc_npc->size_x : 1;
        // The foe (FUN_005ddc30 -> rules::search_sight / sight_choice): the
        // near rooms' monsters (FUN_005dce60: the merc's room's near list,
        // each room's units newest first), not good, alive, out of town,
        // their MonStats threat; sight from them to it; FUN_005dd510's path
        // test a type 2 (toward) path to the primary.
        // ponytail: no monster here is ever in state 0x92 (invis).
        std::vector<d2d::rules::SightFoe> sight_foes;
        std::vector<int> sight_index;
        const auto wall = [&](int at_x, int at_y) { return level->blocked(centre(at_x), centre(at_y), 0x04); };
        for (const auto& [room_level, room] : near_rooms(*game_data, *level, unit.x, unit.y)) {
            if (room_level != level) continue;
            for (std::size_t i = monsters.size(); i-- > 0;) {
                const auto& monster = monsters[i];
                if (room_of(*level, monster.unit.x, monster.unit.y) != room) continue;
                const auto& foe_type = game_data->monsters.types[std::size_t(monster.type)];
                const int foe_x = sub(monster.unit.x), foe_y = sub(monster.unit.y);
                auto& entry = sight_foes.emplace_back(d2d::rules::SightFoe{ d2d::rules::near_distance(foe_x - merc_x, foe_y - merc_y, foe_type.size), foe_type.threat,
                                                                            monster.targetable() && !town && !d2d::rules::friends(2, monster.align) });
                entry.blocked = entry.enemy && entry.distance < 0x31 && d2d::rules::sight_blocked(foe_x, foe_y, foe_type.size, merc_x, merc_y, size, wall);
                sight_index.push_back(int(i));
            }
        }
        int target_gap = 0x7fffffff;
        const auto path_to_primary = [&](int pick) {
            const auto& foe = monsters[std::size_t(sight_index[std::size_t(pick)])];
            const int foe_x = sub(foe.unit.x), foe_y = sub(foe.unit.y);
            if ((foe_x == merc_x && foe_y == merc_y) || std::abs(foe_x - merc_x) > 100 || std::abs(foe_y - merc_y) > 100) return false;   // FUN_00649970
            const auto points = d2d::rules::toward_path(merc_x, merc_y, foe_x, foe_y, 5, 1, [&](int at_x, int at_y) {
                return level->unit_blocked(centre(at_x), centre(at_y), unit.shape) || crowd.at(centre(at_x), centre(at_y), &unit);
            });
            return std::ranges::any_of(points, [&](const std::pair<int, int>& point) { return point != std::pair(merc_x, merc_y); });
        };
        const int chosen = d2d::rules::sight_choice(sight_foes, d2d::rules::search_sight(sight_foes), path_to_primary, target_gap);
        actor->merc_target = chosen >= 0 ? sight_index[std::size_t(chosen)] : -1;
        if (actor->merc_target < 0) target_gap = -1;
        d2d::rules::MercView view{ .cls = actor->merc_npc->hc_idx, .x = merc_x, .y = merc_y, .size = size, .mode = d2d::rules::kMonsterNeutral,
                                   .owner_x = sub(actor->player.x), .owner_y = sub(actor->player.y), .owner_mode = actor->owner_mode,
                                   .end_x = sub(actor->player.path.empty() ? actor->player.x : actor->player.path.back().first),
                                   .end_y = sub(actor->player.path.empty() ? actor->player.y : actor->player.path.back().second),
                                   .ring = actor->footsteps, .cursor = actor->footstep_cursor,
                                   .on_bit40 = level->blocked(unit.x, unit.y, 0x40), .town = town, .target = target_gap };
        auto area_at = [&](int from_x, int from_y, int x, int y) { return int(area_id_at(*game_data, *level, centre(from_x), centre(from_y), centre(x), centre(y))); };   // FUN_0061b130
        std::vector<std::pair<float, float>> route;
        auto try_move = [&](const d2d::rules::MercMove& move) {
            route = move_route(*level, unit, merc_x, merc_y, move, crowd);
            return !route.empty();
        };
        using Kind = d2d::rules::MercAct::Kind;
        const auto act = d2d::rules::hireable_think(view, actor->merc->seed, area_at, try_move);
        const int aidel = type_info && type_info->diff[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))].aidel > 0
                              ? type_info->diff[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))].aidel : 15;
        actor->merc_chase = false;
        if (act.kind == Kind::moved) {
            set(act.move.mode == d2d::rules::kMonsterRun ? "RN" : "WL");
            unit.path = route; unit.goal_x = centre(act.move.x); unit.goal_y = centre(act.move.y);
            unit.budget = 0x14; unit.path_points = int(route.size()); unit.to_unit = false;
            actor->merc->move_pct = act.move.pct;
        } else if (act.kind == Kind::stand) {
            actor->merc->next_act = now_ms + std::uint32_t(act.frames) * 40;
        } else if (act.kind == Kind::failed) {
            actor->merc->next_act = now_ms + std::uint32_t(aidel) * 40;
        } else if (act.kind == Kind::teleport) {
            // FUN_005e3930 mode 3: room_teleport, then it stands 5.
            room_teleport(*game_data, *level, crowd, spawning, unit);
            actor->merc->next_act = now_ms + 5 * 40;
        } else if (act.kind == Kind::attack && actor->merc_target >= 0 && type_info) {
            // The attack think (FUN_005e5050) and its skill pick (FUN_005e4d30).
            // FUN_005dead0 takes the row's mode directly; A1 below only replaces a missing COF.
            const auto& target = monsters[std::size_t(actor->merc_target)];
            const int foe_x = sub(target.unit.x), foe_y = sub(target.unit.y), difficulty = std::clamp(actor->character.header.active_difficulty(), 0, 2);
            static constexpr std::array<std::string_view, 16> kModes{ "DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN" };
            const auto* skill1 = skill_named(type_info->skill[0]);
            const auto sk1mode = std::ranges::find(kModes, std::string_view(type_info->sk_mode[0]));
            d2d::rules::MercFightView fight_view{ .cls = actor->merc_npc->hc_idx, .level = actor->merc_st.level, .aip1 = type_info->diff[std::size_t(difficulty)].aip[0],
                .gap = d2d::rules::merc_gap(merc_x, merc_y, size, foe_x, foe_y), .distance = target_gap + 1,
                .melee = d2d::rules::unit_distance(foe_x - merc_x, foe_y - merc_y, type_info->size, game_data->monsters.types[std::size_t(target.type)].size) <= type_info->melee_rng + 1,
                .row = d2d::rules::hireling_row(game_data->rules, actor->character.header.merc_type, actor->merc_st.level), .ids = actor->merc_ids, .levels = actor->merc_levels,
                .skill1 = skill1 ? skill1->id : -1, .sk1mode = sk1mode == kModes.end() ? 4 : int(sk1mode - kModes.begin()) };
            for (std::size_t k = 0; k < actor->merc_ids.size(); ++k) {
                const auto* skill = game_data->skills.get(actor->merc_ids[k]);
                fight_view.aura[k] = skill && skill->aura;
                fight_view.running[k] = actor->merc_ids[k] > 0 && (actor->merc_ids[k] == actor->merc_aura || (actor->merc_ids[k] == actor->merc_buff.skill && now_ms < actor->merc_buff.until));
            }
            auto move = [&](int kind, int x, int y) {
                if (kind == 2) {                                      // FUN_005ded00: a run at the foe
                    if (!set_off(*level, unit, target.unit.x, target.unit.y, true, crowd)) return false;
                    set("RN");                                        // a run keeps its path
                    actor->merc->move_pct = 0; actor->merc_chase = true;
                    return true;
                }
                if (!try_move({ .x = x, .y = y })) return false;
                set("WL");
                unit.path = route; unit.goal_x = centre(x); unit.goal_y = centre(y);
                unit.budget = 0x14; unit.path_points = int(route.size()); unit.to_unit = false;
                actor->merc->move_pct = 0;
                return true;
            };
            using AttackKind = d2d::rules::MercAttack::Kind;
            const auto attack = d2d::rules::merc_attack(fight_view, actor->merc_growth, merc_x, merc_y, foe_x, foe_y, actor->merc->seed, move);
            if (attack.kind == AttackKind::failed) actor->merc->next_act = now_ms + std::uint32_t(aidel) * 40;
            else if (attack.kind == AttackKind::stand) actor->merc->next_act = now_ms + std::uint32_t(attack.frames) * 40;
            else if (attack.kind == AttackKind::aura) {
                actor->merc_aura = attack.skill;
                d2d::log::info("the merc starts its aura {}", attack.skill);
                actor->merc->next_act = now_ms + 10 * 40;
            } else if (attack.kind == AttackKind::skill || attack.kind == AttackKind::swing) {
                actor->merc_skill = attack.kind == AttackKind::skill ? attack.skill : -1;
                actor->merc_skill_level = 0;
                for (std::size_t k = 0; k < actor->merc_ids.size(); ++k) if (actor->merc_ids[k] == actor->merc_skill) actor->merc_skill_level = actor->merc_levels[k];
                if (skill1 && actor->merc_skill == skill1->id && actor->merc_skill_level == 0) actor->merc_skill_level = type_info->sk_lvl[0];
                const auto mode = attack.kind == AttackKind::swing || attack.mode < 0 || attack.mode >= 16 ? std::string_view("A1") : kModes[std::size_t(attack.mode)];
                unit.dir = direction16(target.unit.x - unit.x, target.unit.y - unit.y);
                actor->merc_seq = nullptr;
                if (attack.kind == AttackKind::skill && attack.mode == 14)   // SQ: the sequence its MonStats slot for the skill names
                    for (std::size_t slot = 0; slot < type_info->skill.size(); ++slot)
                        if (const auto* slot_skill = skill_named(type_info->skill[slot]); slot_skill && slot_skill->id == actor->merc_skill)
                            if (const auto found = game_data->mon_seqs.find(type_info->sk_mode[slot]); found != game_data->mon_seqs.end() && !found->second.empty()) {
                                actor->merc_seq = &found->second;
                                actor->merc_seq_done = 0;
                            }
                const std::string_view played = actor->merc_seq ? std::string_view(actor->merc_seq->front().mode) : mode;
                set(game_data->npc_timing(*actor->merc_npc, played).directions ? played : std::string_view("A1"));
                if (actor->merc_seq) actor->merc->mode_until = now_ms + std::uint32_t(actor->merc_seq->size()) * game_data->npc_timing(*actor->merc_npc, actor->merc->mode).ms_per_frame();
                actor->merc->struck = false;
            }
        }
    }

}  // namespace d2d::game
