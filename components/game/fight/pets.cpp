// SPDX-License-Identifier: GPL-3.0-or-later
// Fight (fight.hpp): pets: their foes, walks, thinks and casts. The rest is in this directory.
#include "fight.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "gamedata.hpp"
#include "internal.hpp"
#include "montypes.hpp"

#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <merc.hpp>
#include <monsters.hpp>
#include <pets.hpp>
#include <rules.hpp>
#include <shadows.hpp>
#include <skills.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

// A monster mode's number (MonMode: DT 0 .. RN 15); a MonSeq sequence
// (seq_swtigerfist) or Skills.txt monanim xx plays SQ (14).
int monster_mode(std::string_view token) {
    static constexpr std::array<std::string_view, 16> kMonModes{ "DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN" };
    const auto found = std::ranges::find(kMonModes, token);
    return found == kMonModes.end() ? 14 : int(found - kMonModes.begin());
}

namespace {

// FUN_00645460: a skill's kind, Skills.txt range — none 0, h2h 1, rng 2,
// both 1 (2 with a missile weapon).
// ponytail: a Shadow with its owner's bow would count both as 2.
int skill_kind(const d2d::rules::Skill& skill) { return skill.range == "h2h" || skill.range == "both" ? 1 : skill.range == "rng" ? 2 : 0; }

// Skills.txt charclass as FUN_00645040's class (7: none).
int skill_class(const d2d::rules::Skill& skill) {
    static constexpr std::array<std::string_view, 7> kClasses{ "ama", "sor", "nec", "pal", "bar", "dru", "ass" };
    const auto found = std::ranges::find(kClasses, std::string_view(skill.cls));
    return found == kClasses.end() ? 7 : int(found - kClasses.begin());
}

}  // namespace

// FUN_005eab20 by name: may a Shadow (MonStats row `type_info`, raised by
// `summoning`) have the skill? One that summons nothing, or neither this
// monster nor the Shadow's pettype.
bool shadow_may_have(const d2d::rules::Skill* skill, const d2d::rules::MonType& type_info, const d2d::rules::Skill& summoning) {
    if (!skill) return false;
    if (skill->summon.empty()) return true;
    if (skill->summon == type_info.id) return false;
    return skill->pettype.empty() || skill->pettype != summoning.pettype;
}

auto Fight::pet_foe(const Pet& pet) const -> Foe {
        auto fighter = d2d::rules::simple_fighter(pet.monster.stats.a1_min, pet.monster.stats.a1_max, pet.monster.stats.to_hit,
                                                  pet.monster.stats.armor_class * (100 + pet_buff_stat(pet, d2d::d2s::kSkillArmorPercent)) / 100);   // Shout's
        static constexpr std::array<int, 4> kResists{ d2d::d2s::kFireResist, d2d::d2s::kLightningResist, d2d::d2s::kColdResist, d2d::d2s::kPoisonResist };
        for (std::size_t k = 0; k < 4; ++k) fighter.res[k] = std::min(pet.res[k] + pet_buff_stat(pet, kResists[k]), 95);   // Fade's on a Shadow
        fighter.thorns_pct = pet.thorns;                             // Iron Golem's thorns
        fighter.dr_pct = std::min(pet_buff_stat(pet, d2d::d2s::kDamageResist), 50);   // a curse's (Amplify Damage)
        const auto& type_info = game_data->monsters.types[std::size_t(pet.monster.type)];
        const bool still = type_info.velocity == 0 && type_info.run == 0 && pet.ranged >= 0;   // a Hydra, like a trap, isn't there to hit
        Foe foe{ pet.monster.unit.x, pet.monster.unit.y, pet.monster.stats.level, pet.monster.alive() && pet.monster.mode != "DT" && pet.shot_skill < 0 && !still && pet.where == level,
                 pet.monster.unit.walking, fighter };
        foe.pet = true; foe.size = type_info.size; foe.threat = type_info.threat; foe.id = pet.monster.id;
        monster_foe_props(*game_data, pet.monster, foe);
        foe.life_pct = int(std::int64_t(pet.monster.hit_points) * 100 / std::max(pet.monster.stats.hit_points, 1));
        const auto& own = type_info.diff[std::size_t(pet.monster.difficulty)].res;
        std::array<int, 5> res{ own[0] + pet_buff_stat(pet, d2d::d2s::kDamageResist), own[1] };
        for (std::size_t k = 0; k < 3; ++k) res[k + 2] = pet.res[k] + pet_buff_stat(pet, kResists[k]);
        foe.pick = unit_pick(*game_data, res, pet.aura, pet.aura_level, d2d::rules::swing_max_damage(pet.monster.stats, pet.monster.swing_mode));
        return foe;
    }

auto Fight::pet_hurt(Pet& pet, int damage, std::uint32_t now_ms, int hit_class, bool marked) -> void {
        if (damage > 0 && pet.monster.mode != "DT") (void)hurt(*game_data, pet.monster, damage, now_ms, 0, hit_class, marked);
    }

auto Fight::pets_cross(const Level* from, const Level* destination, float dx, float dy) -> void {
        for (auto& pet : actor->pets)
            if (pet.where == from && pet.shot_skill < 0 && pet.monster.alive()) {
                pet.where = destination; pet.target = -1;
                pet.monster.unit.x -= dx; pet.monster.unit.y -= dy; pet.monster.unit.goal_x -= dx; pet.monster.unit.goal_y -= dy;
                for (auto& [step_x, step_y] : pet.monster.unit.path) { step_x -= dx; step_y -= dy; }
                pet.monster.home_x -= dx; pet.monster.home_y -= dy;
            }
    }

auto Fight::pet_aura(Pet& pet, std::uint32_t now_ms) -> void {
        const auto* skill = game_data->skills.get(pet.aura);
        if (!skill || skill->srvdofunc != ServerDoFunction::kEnemyAura || now_ms < pet.aura_next || skill->etype < 0 || skill->etype > 4) return;
        pet.aura_next = now_ms + std::uint32_t(std::max(skill->perdelay, 25)) * 40;
        const int radius = calc(*skill, skill->aurarange, pet.aura_level);
        const auto damage = d2d::rules::MissileDamage{ .etype = skill->etype,
                                                   .elo = d2d::rules::elem_damage(game_data->skills, *skill, calc_env(), pet.aura_level, false),
                                                   .ehi = d2d::rules::elem_damage(game_data->skills, *skill, calc_env(), pet.aura_level, true) };
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].targetable() && std::hypot(monsters[i].unit.x - pet.monster.unit.x, monsters[i].unit.y - pet.monster.unit.y) * 5 <= float(radius))
                land(i, d2d::rules::missile_blow(damage, target_of(i), {}, rng), false, now_ms, pet.monster.id);
    }

auto Fight::pets_turn(std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> void {
        std::erase_if(actor->pets, [&](const Pet& pet) { return !pet.monster.alive() && pet.monster.mode == "DT" && now_ms >= pet.monster.mode_until; });
        const auto save_class = std::size_t(std::max(actor->character.character_class, 0));
        for (const auto& made : std::exchange(actor->pet_summons, {}))            // a Shadow's traps and Blade Sentinel from last turn
            if (const auto* skill = game_data->skills.get(made.skill)) summon(*skill, made.level, made.x, made.y, now_ms);
        for (auto& pet : actor->pets) {
            auto& monster = pet.monster;
            auto& unit = monster.unit;
            const bool curse_spent = pet.curse.skill >= 0 && now_ms >= pet.curse.until;
            if (curse_spent) pet.curse = {};
            if (std::erase_if(pet.buffs, [&](const SelfState& buff) { return now_ms >= buff.until; }) > 0 || curse_spent) pet_buffs(pet);
            if (!monster.alive() || pet.where != level) continue;
            for (const auto& buff : pet.buffs)                              // a Shadow's Blade Shield, as the player's
                if (const auto* skill = game_data->skills.get(buff.skill); skill && skill->srvdofunc == ServerDoFunction::kBladeShield && now_ms >= pet.blade_next) {
                    pet.blade_next = now_ms + std::uint32_t(std::max(calc(*skill, skill->perdelay_calc, buff.level), 1)) * 40;
                    blade_shield(*skill, buff.level, unit.x, unit.y, actor->player_combat, false, monster.id, now_ms);
                }
            if (pet.until && now_ms >= pet.until) { monster.hit_points = 0; set_mode(*game_data, monster, "DT", now_ms); continue; }
            if (monster.mode == "KB") { if (d2d::game::knock_slide(monster, now_ms)) continue; set_mode(*game_data, monster, "GH", now_ms); }   // knocked back, then its get-hit
            const auto& ai_name = game_data->monsters.types[std::size_t(monster.type)].ai_name;
            const bool traced = d2d::rules::traced_pet_ai(ai_name);
            if (pet.shot_skill >= 0 && !traced) { trap_turn(pet, now_ms); continue; }
            if (pet.aura >= 0) pet_aura(pet, now_ms);
            if (pet.idle) continue;                            // Decoy stands where it was cast
            if (const auto* skill = game_data->skills.get(pet.skill); !traced && skill && skill->srvdofunc == ServerDoFunction::kVine && now_ms >= pet.aura_next) {
                // The vines: Poison Creeper poisons what's within 1.5 cells
                // with the skill's poison; Carrion Vine / Solar Creeper eat a
                // corpse within 5 cells for Param6 seconds, giving Param5 %
                // of max life / mana a second.
                // ponytail: their AI (Vines, CycleOfLife) and Vine Attack /
                // the cyclers (do 130, st 63) aren't traced: the published
                // behaviour.
                pet.aura_next = now_ms + 1000;
                using namespace d2d::d2s;
                if (skill->etype == 3) {
                    const auto damage = d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), pet.monster.stats.level);
                    for (std::size_t j = 0; j < monsters.size(); ++j)
                        if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - unit.x, monsters[j].unit.y - unit.y) < 1.5f)
                            land(j, d2d::rules::missile_blow(damage, target_of(j), pierce(), rng), false, now_ms, pet.monster.id);
                } else if (pet.hits > 0 || [&] {
                               const int corpse = corpse_near(unit.x, unit.y);
                               if (corpse < 0 || std::hypot(monsters[std::size_t(corpse)].unit.x - unit.x, monsters[std::size_t(corpse)].unit.y - unit.y) > 5) return false;
                               monsters[std::size_t(corpse)].corpse_used = true;
                               pet.hits = std::max(skill->par[5], 1);
                               return true;
                           }()) {
                    --pet.hits;
                    const bool life = skill->name == "Cycle of Life";
                    auto& value = actor->character.stats.values[life ? kLife : kMana];
                    value = std::min(actor->character.stats.values[life ? kMaxLife : kMaxMana], value + actor->character.stats.values[life ? kMaxLife : kMaxMana] * skill->par[4] / 100);
                }
            }
            if (monster.mode != "NU" && monster.mode != "WL" && monster.mode != "RN" && monster.mode != "GH" && monster.mode != "DT") {
                if (!monster.struck && (pet.target >= 0 || pet.cast >= 0) && now_ms >= unit.mode_ms + game_data->npc_timing(monster.npc, monster.mode).action_ms()) {
                    monster.struck = true;
                    const auto monster_index = std::size_t(std::max(pet.target, 0));
                    if (pet.cast >= 0) {
                        pet_cast(pet, now_ms, crowd);
                    } else if (const auto* missile_info = pet.ranged >= 0 ? pet_missile(*game_data->skills.get(pet.ranged), pet.variant) : nullptr; missile_info && monsters[monster_index].alive()) {
                        Missile x{ missile_info, unit.x, unit.y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(missile_info->range, 1)) * 40, {} };
                        const float dx = monsters[monster_index].unit.x - unit.x, dy = monsters[monster_index].unit.y - unit.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                        x.velocity_x = dx / distance * missile_speed(*missile_info, pet.ranged_level); x.velocity_y = dy / distance * missile_speed(*missile_info, pet.ranged_level);
                        x.dir = missile_facing(unit.x, unit.y, dx, dy); x.to = { monsters[monster_index].unit.x, monsters[monster_index].unit.y };
                        x.friendly = true; x.caster = actor->id; x.level = pet.ranged_level; x.skill = pet.ranged;
                        if (const auto* missile_skill_row = skill_named(missile_info->skill)) x.skill = missile_skill_row->id;   // 'hydra' carries the Hydra skill's damage
                        pending.push_back(std::move(x));
                    } else if (monsters[monster_index].targetable() && std::hypot(monsters[monster_index].unit.x - unit.x, monsters[monster_index].unit.y - unit.y) <= kMeleeReach + 0.3f) {
                        auto target = target_of(monster_index);
                        // A Shadow swings the owner's blow (its copied gear); its skills are shadow_cast's.
                        auto blow = d2d::rules::player_blow(pet.mirror ? actor->player_combat : d2d::rules::simple_fighter(monster.stats.a1_min, monster.stats.a1_max, monster.stats.to_hit),
                                                         target, monster.stats.level, rng);
                        if (!traced && pet.hits > 0 && --pet.hits == 0) { monster.hit_points = 0; set_mode(*game_data, monster, "DT", now_ms); }   // a Raven's last
                        if (blow.hit && pet.fire_hi > 0) blow.damage += d2d::rules::resisted(rng.range(pet.fire_lo, pet.fire_hi), target.res[2]);   // Fire Golem
                        pet_strike(pet, monster_index, blow, now_ms);
                    }
                }
                if (now_ms < monster.mode_until) continue;
                pet.cast = -1;
                set_mode(*game_data, monster, "NU", now_ms);
            }
            if (monster.mode == "GH" && now_ms < monster.mode_until) continue;
            if (ai_name == "NecroPet") { necropet_turn(pet, now_ms, elapsed, crowd); continue; }
            if (traced) { pet_think_turn(pet, now_ms, elapsed, crowd); continue; }
            if (pet.target >= 0 && !monsters[std::size_t(pet.target)].alive()) pet.target = -1;
            if (pet.target < 0) {
                float best = 1e9f;
                for (std::size_t i = 0; i < monsters.size(); ++i) {
                    const auto& other = monsters[i];
                    if (!other.alive()) continue;
                    const float to_player = std::hypot(other.unit.x - actor->player.x, other.unit.y - actor->player.y), to_pet = std::hypot(other.unit.x - unit.x, other.unit.y - unit.y);
                    if (((other.aware && to_player < 8) || to_pet < 4) && to_pet < best) { best = to_pet; pet.target = int(i); }
                }
            }
            const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
            const float speed = cells_per_sec(float(std::max(type_info.run, type_info.velocity))) * float(100 + pet.speed_pct) / 100;
            if (speed <= 0) {                                // a Hydra: it stands and shoots what comes in range
                if (pet.target >= 0 && std::hypot(monsters[std::size_t(pet.target)].unit.x - unit.x, monsters[std::size_t(pet.target)].unit.y - unit.y) > 6.f) pet.target = -1;
                for (std::size_t i = 0; pet.target < 0 && i < monsters.size(); ++i)
                    if (monsters[i].targetable() && std::hypot(monsters[i].unit.x - unit.x, monsters[i].unit.y - unit.y) <= 6.f) pet.target = int(i);
                if (pet.target >= 0 && now_ms >= monster.next_act) {
                    const auto& target = monsters[std::size_t(pet.target)];
                    unit.dir = direction16(target.unit.x - unit.x, target.unit.y - unit.y);
                    set_mode(*game_data, monster, game_data->npc_timing(monster.npc, "A1").directions ? "A1" : "NU", now_ms);
                    monster.struck = false;
                    monster.next_act = now_ms + std::uint32_t(std::max(game_data->monsters.types[std::size_t(monster.type)].diff[std::size_t(monster.difficulty)].aidel, 15)) * 40;
                }
                continue;
            }
            if (pet.target >= 0) {
                const auto& target = monsters[std::size_t(pet.target)];
                const float dx = target.unit.x - unit.x, dy = target.unit.y - unit.y;
                if (std::hypot(dx, dy) <= (pet.ranged >= 0 ? 6.f : kMeleeReach)) {
                    unit.dir = direction16(dx, dy);
                    unit.path.clear(); unit.walking = false;
                    set_mode(*game_data, monster, "A1", now_ms);
                    monster.struck = false;
                    continue;
                }
                if (unit.path.empty() || std::hypot(unit.goal_x - target.unit.x, unit.goal_y - target.unit.y) > 1.f)
                    set_off(*level, unit, target.unit.x, target.unit.y, true, crowd);
                if (!walk_on(*level, unit, speed * elapsed, crowd)) pet.target = -1;
            } else {
                pet_follow(*level, unit, actor->player.x, actor->player.y, cells_per_sec(float(game_data->run_velocity[save_class])) * 1.1f, now_ms, elapsed, crowd);
            }
            const std::string_view want = unit.walking ? "WL" : "NU";
            if (monster.mode != want) set_mode(*game_data, monster, want, now_ms);
        }
    }

// A pet's walk between thinks (a walk at its target ends in reach); true
// while it walks or isn't due a think.
auto Fight::pet_walking(Pet& pet, std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> bool {
        auto& monster = pet.monster;
        auto& unit = monster.unit;
        const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
        if (pet.target >= 0 && !monsters[std::size_t(pet.target)].alive()) { pet.target = -1; pet.chase = false; }
        // A walk at its foe ends in melee as its think tests it (FUN_00622c40:
        // the units' distance within MonStats2 MeleeRng + 1).
        const auto in_melee = [&](const Monster& foe) {
            const auto sub = [](float cells) { return int(std::floor(cells * 5)); };
            return d2d::rules::unit_distance(sub(foe.unit.x) - sub(unit.x), sub(foe.unit.y) - sub(unit.y), std::max(type_info.size, 1),
                                             game_data->monsters.types[std::size_t(foe.type)].size) <= type_info.melee_rng + 1;
        };
        if (unit.walking) {
            if (pet.chase && (pet.target < 0 || in_melee(monsters[std::size_t(pet.target)]))) {
                unit.walking = false; unit.path.clear();
            } else {
                int velocity = monster.mode == "RN" ? type_info.run : type_info.velocity;
                // In a state that looks like a player class (States gfxtype 2:
                // the Shadows' "shadowwarrior", class 6) it moves as that class
                // (FUN_00645270 -> FUN_00621360: CharStats WalkVelocity /
                // RunVelocity); a Shadow's MonStats Velocity is 0.
                if (const auto* summoning = game_data->skills.get(pet.skill); summoning && !summoning->aurastate.empty())
                    if (const auto state = game_data->states.find(summoning->aurastate); state != game_data->states.end() && state->second.gfx_type == 2) {
                        const auto cls = std::size_t(std::clamp(state->second.gfx_class, 0, 6));
                        velocity = monster.mode == "RN" ? game_data->run_velocity[cls] : game_data->walk_velocity[cls];
                    }
                const float pace = float(std::max(100 + monster.move_pct, 10)) * float(100 + pet.speed_pct + pet_buff_stat(pet, d2d::d2s::kVelocityPercent)) / 10000;   // Burst of Speed's on a Shadow
                if (walk_on(*level, unit, cells_per_sec(float(velocity)) * pace * elapsed, crowd)) return true;
            }
            pet.chase = false;
            set_mode(*game_data, monster, "NU", now_ms);
            monster.next_act = now_ms;                                    // a walk's end thinks at once (FUN_005a8030)
        }
        return monster.mode != "NU" || now_ms < monster.next_act;
    }

// A NecroPet's frame outside an attack (the golems, the Valkyrie, the
// skeletons and mages): a walk goes on (a walk at its foe ends in reach),
// then at its think rules::necropet_think. The foe is FUN_005dd7f0's for a
// good monster: mode 5 over the near rooms' monsters (rules::search_near:
// a threat of 2 or more first, else the rest, within 0x23), sight needed
// in a preset room that isn't Outdoors, or once after a walk at a foe found
// no path (flag 0x40); its melee is MeleeRng + 1, its line FUN_005dc640.
// A teleport is room_teleport (mode 3).
// ponytail: the owner's path spot (+0x10) is the player's next path point;
// the crowd counts by merc_gap from this pet's size
// (FUN_005e3900's order of units isn't read); a good monster's own room
// order isn't kept (the search runs over the monsters in fight order).
auto Fight::necropet_turn(Pet& pet, std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> void {
        auto& monster = pet.monster;
        auto& unit = monster.unit;
        const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
        const int difficulty = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        auto sub = [](float cells) { return int(std::floor(cells * 5)); };
        auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
        if (pet_walking(pet, now_ms, elapsed, crowd)) return;
        const int pet_x = sub(unit.x), pet_y = sub(unit.y), size = std::max(type_info.size, 1);
        const bool town = d2d::rules::level_ids::is_town(level->id);
        // The foe (FUN_005dd7f0 -> FUN_005dd0b0 mode 5).
        const int room = room_of(*level, unit.x, unit.y);
        const auto* in_room = room >= 0 ? &level->rooms[std::size_t(room)] : nullptr;
        const bool indoor = in_room && in_room->kind == 2
            && !(std::size_t(in_room->def) < game_data->prest_outdoors.size() && game_data->prest_outdoors[std::size_t(in_room->def)]);
        const bool need_sight = std::exchange(monster.force_sight, false) || indoor;
        const auto wall = [&](int at_x, int at_y) { return level->blocked(centre(at_x), centre(at_y), 0x04); };
        std::vector<d2d::rules::NearFoe> nears;
        for (const auto& other : monsters) {
            const auto& other_type = game_data->monsters.types[std::size_t(other.type)];
            const int foe_x = sub(other.unit.x), foe_y = sub(other.unit.y);
            auto& entry = nears.emplace_back(d2d::rules::NearFoe{ d2d::rules::near_distance(foe_x - pet_x, foe_y - pet_y, other_type.size), other.align, other_type.threat,
                                                                  false, true, !other.targetable(), town });   // FUN_005dca70: no unit flag 0x4, no target
            entry.blocked = need_sight && entry.distance <= 0x23 && d2d::rules::sight_blocked(foe_x, foe_y, other_type.size, pet_x, pet_y, size, wall);
        }
        const auto pick = d2d::rules::search_near(nears, 2, need_sight);
        const int found = pick.target >= 0 ? pick.target : pick.second;
        d2d::rules::PetView view{ .x = pet_x, .y = pet_y, .size = size, .owner_x = sub(actor->player.x), .owner_y = sub(actor->player.y), .owner_mode = actor->owner_mode,
                                  .cur_x = sub(actor->player.path.empty() ? actor->player.x : actor->player.path.front().first),
                                  .cur_y = sub(actor->player.path.empty() ? actor->player.y : actor->player.path.front().second),
                                  .end_x = sub(actor->player.path.empty() ? actor->player.x : actor->player.path.back().first),
                                  .end_y = sub(actor->player.path.empty() ? actor->player.y : actor->player.path.back().second),
                                  .ring = actor->footsteps, .cursor = actor->footstep_cursor, .arrive_x = actor->warp_spot.first, .arrive_y = actor->warp_spot.second,
                                  .town = town, .velocity = type_info.velocity, .run = type_info.run };
        view.pets = int(std::ranges::count_if(actor->pets, [](const Pet& other) { return other.monster.alive(); }));
        view.crowd = int(std::ranges::count_if(actor->pets, [&](const Pet& other) {
            return &other != &pet && other.monster.alive() && other.where == level
                && d2d::rules::merc_gap(pet_x, pet_y, size, sub(other.monster.unit.x), sub(other.monster.unit.y)) < 2;
        }));
        if (found >= 0) {
            const auto& foe = monsters[std::size_t(found)];
            const auto& foe_type = game_data->monsters.types[std::size_t(foe.type)];
            view.foe = true;
            view.foe_x = sub(foe.unit.x); view.foe_y = sub(foe.unit.y);
            view.foe_distance = pick.target >= 0 ? pick.best : pick.second_best;
            view.foe_melee = d2d::rules::unit_distance(view.foe_x - pet_x, view.foe_y - pet_y, size, foe_type.size) <= type_info.melee_rng + 1;
            view.clear = d2d::rules::pet_line_clear(pet_x, pet_y, size, view.foe_x, view.foe_y,
                                                    [&](int at_x, int at_y) { return level->blocked(centre(at_x), centre(at_y), 0x805); });
        }
        auto area_at = [&](int from_x, int from_y, int x, int y) { return int(area_id_at(*game_data, *level, centre(from_x), centre(from_y), centre(x), centre(y))); };   // FUN_0061b130
        std::vector<std::pair<float, float>> route;
        auto try_move = [&](const d2d::rules::MercMove& move) {
            if (move.unit != 0) return found >= 0 && set_off(*level, unit, monsters[std::size_t(found)].unit.x, monsters[std::size_t(found)].unit.y, true, crowd);
            route = move_route(*level, unit, pet_x, pet_y, move, crowd);
            return !route.empty();
        };
        using Kind = d2d::rules::PetAct::Kind;
        const auto act = d2d::rules::necropet_think(view, monster.seed, area_at, try_move);
        monster.force_sight = act.unreachable;
        const int aidel = type_info.diff[std::size_t(difficulty)].aidel > 0 ? type_info.diff[std::size_t(difficulty)].aidel : 15;
        if (act.kind == Kind::moved || act.kind == Kind::chase) {
            set_mode(*game_data, monster, act.move.mode == d2d::rules::kMonsterRun ? "RN" : "WL", now_ms);
            if (act.kind != Kind::chase) unit.path = route;
            unit.walking = true;
            if (act.kind == Kind::moved) { unit.goal_x = centre(act.move.x); unit.goal_y = centre(act.move.y); unit.budget = 0x14; unit.path_points = int(route.size()); unit.to_unit = false; }
            monster.move_pct = act.move.pct;
            pet.chase = act.kind == Kind::chase;
            pet.target = pet.chase ? found : -1;
        } else if (act.kind == Kind::swing && found >= 0) {
            pet.target = found;
            unit.dir = direction16(monsters[std::size_t(found)].unit.x - unit.x, monsters[std::size_t(found)].unit.y - unit.y);
            unit.path.clear(); unit.walking = false;
            set_mode(*game_data, monster, "A1", now_ms);
            monster.struck = false;
        } else if (act.kind == Kind::teleport) {
            room_teleport(*game_data, *level, crowd, spawning, unit);
            unit.path.clear(); unit.walking = false;
            set_mode(*game_data, monster, "NU", now_ms);
            monster.next_act = now_ms + 5 * 40;
        } else {
            monster.next_act = now_ms + std::uint32_t(act.kind == Kind::stand ? act.frames : aidel) * 40;
        }
    }

// A traced pet's frame outside an attack (rules::traced_pet_ai): a walk
// goes on, then at its think the AI's rules::*_think on a PetScene, its
// follows and follow-or-fight tests by rules::PetFollow as NecroPet's. The
// units: the foe of the mode 5 search (necropet_turn's), the corpse near the
// owner (Fenris: 10 subtiles; the cycle vines: their skill's calc, 5..50)
// or near the Death Sentry. The AI driver's target and the merc-style
// search's are that foe too.
// ponytail: `foe2` is the mode 5 foe, not FUN_005ddc30's; no unit is ever
// poisoned (state 2), slowed (0x3c) or in Fenris's rage (0x8a); the
// owner's life / mana are whole points; Blade Sentinel isn't summoned, so
// its think doesn't run. The Shadows: the Warrior's scene and the Master's
// think below.
auto Fight::pet_think_turn(Pet& pet, std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> void {
        using d2d::rules::PetUnit;
        auto& monster = pet.monster;
        auto& unit = monster.unit;
        if (pet_walking(pet, now_ms, elapsed, crowd)) return;
        const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
        const int difficulty = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        auto sub = [](float cells) { return int(std::floor(cells * 5)); };
        auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
        const int size = std::max(type_info.size, 1);
        const bool town = d2d::rules::level_ids::is_town(level->id);
        // The foe (as necropet_turn's).
        const int room = room_of(*level, unit.x, unit.y);
        const auto* in_room = room >= 0 ? &level->rooms[std::size_t(room)] : nullptr;
        const bool indoor = in_room && in_room->kind == 2
            && !(std::size_t(in_room->def) < game_data->prest_outdoors.size() && game_data->prest_outdoors[std::size_t(in_room->def)]);
        const bool need_sight = std::exchange(monster.force_sight, false) || indoor;
        const int pet_x = sub(unit.x), pet_y = sub(unit.y);
        const auto wall = [&](int at_x, int at_y) { return level->blocked(centre(at_x), centre(at_y), 0x04); };
        std::vector<d2d::rules::NearFoe> nears;
        for (const auto& other : monsters) {
            const auto& other_type = game_data->monsters.types[std::size_t(other.type)];
            const int foe_x = sub(other.unit.x), foe_y = sub(other.unit.y);
            auto& entry = nears.emplace_back(d2d::rules::NearFoe{ d2d::rules::near_distance(foe_x - pet_x, foe_y - pet_y, other_type.size), other.align, other_type.threat,
                                                                  false, true, !other.targetable(), town });   // FUN_005dca70: no unit flag 0x4, no target
            entry.blocked = need_sight && entry.distance <= 0x23 && d2d::rules::sight_blocked(foe_x, foe_y, other_type.size, pet_x, pet_y, size, wall);
        }
        const auto pick = d2d::rules::search_near(nears, 2, need_sight);
        const int found = pick.target >= 0 ? pick.target : pick.second;
        // The corpse: near the owner for Fenris (10) and the cycle vines
        // (their skill's calc), near itself for the Death Sentry.
        const auto* summoning = game_data->skills.get(pet.skill);
        const int corpse_reach = summoning && type_info.ai_name != "DruidWolf" ? std::clamp(d2d::rules::eval_calc(game_data->skills, summoning->calc[0], calc_env(), summoning->id, monster.stats.level), 5, 0x32) : 10;
        const bool by_self = type_info.ai_name == "DeathSentry";
        int corpse = corpse_near(by_self ? unit.x : actor->player.x, by_self ? unit.y : actor->player.y);
        if (corpse >= 0 && !by_self && std::hypot(monsters[std::size_t(corpse)].unit.x - actor->player.x, monsters[std::size_t(corpse)].unit.y - actor->player.y) * 5 > float(corpse_reach)) corpse = -1;
        std::array<int, 6> index{ -1, -1, -1, found, found, corpse };   // by PetUnit: a monster
        d2d::rules::PetScene scene;
        scene.cls = monster.npc.hc_idx; scene.owner_id = 1; scene.owner_mode = actor->owner_mode; scene.town = town; scene.frame = int(now_ms / 40);
        std::array<int, 6> sizes{ 0, size, 2, 0, 0, 0 };
        scene.spot[1] = { pet_x, pet_y };
        scene.spot[2] = { sub(actor->player.x), sub(actor->player.y) };
        for (std::size_t k = 3; k < 6; ++k)
            if (index[k] >= 0) {
                const auto& other = monsters[std::size_t(index[k])];
                scene.spot[k] = { sub(other.unit.x), sub(other.unit.y) };
                sizes[k] = game_data->monsters.types[std::size_t(other.type)].size;
            }
        for (std::size_t from = 1; from < 6; ++from)
            for (std::size_t to = 1; to < 6; ++to) {
                const auto [from_x, from_y] = scene.spot[from];
                const auto [to_x, to_y] = scene.spot[to];
                scene.gap[from][to] = d2d::rules::merc_gap(from_x, from_y, sizes[from], to_x, to_y);
                const int shrink = sizes[from] / 2 + sizes[to] / 2;        // FUN_006416d0
                const int across = std::max(std::abs(to_x - from_x) - shrink, 0), down = std::max(std::abs(to_y - from_y) - shrink, 0);
                scene.distance[from][to] = (across <= down ? across + down * 2 : down + across * 2) / 2;
            }
        if (found >= 0) {
            const auto& foe = monsters[std::size_t(found)];
            const auto [foe_x, foe_y] = scene.spot[3];
            scene.foe = scene.foe2 = scene.driver = true;
            scene.foe_distance = scene.foe2_distance = scene.driver_distance = pick.target >= 0 ? pick.best : pick.second_best;
            scene.foe_melee = scene.foe2_melee = scene.driver_melee
                = d2d::rules::unit_distance(foe_x - pet_x, foe_y - pet_y, size, game_data->monsters.types[std::size_t(foe.type)].size) <= type_info.melee_rng + 1;
            scene.clear[3] = scene.clear[4] = d2d::rules::pet_line_clear(pet_x, pet_y, size, foe_x, foe_y,
                                                                         [&](int at_x, int at_y) { return level->blocked(centre(at_x), centre(at_y), 0x805); });
            scene.poison_resist = target_of(std::size_t(found)).res[5];
        }
        for (std::size_t k = 3; k < 6; ++k) {
            if (index[k] < 0) continue;
            const auto& other = monsters[std::size_t(index[k])];
            scene.melee_of[k] = d2d::rules::unit_distance(scene.spot[k].first - pet_x, scene.spot[k].second - pet_y, size, sizes[k]) <= type_info.melee_rng + 1;
            scene.dying[k] = !other.alive();
        }
        scene.nearby = corpse >= 0;
        scene.corpse_id = corpse >= 0 ? monsters[std::size_t(corpse)].id : 0;
        scene.radius = corpse_reach * 2;
        scene.skill_level = std::max(monster.stats.level, 1);
        {
            using namespace d2d::d2s;
            scene.life = int(actor->character.stats.values[kLife] >> 8); scene.max_life = int(actor->character.stats.values[kMaxLife] >> 8);
            scene.mana = int(actor->character.stats.values[kMana] >> 8); scene.max_mana = int(actor->character.stats.values[kMaxMana] >> 8);
        }
        static constexpr std::array<std::string_view, 16> kModes{ "DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN" };
        auto mode_of = [&](std::string_view token) { const auto found_mode = std::ranges::find(kModes, token); return found_mode == kModes.end() ? 4 : int(found_mode - kModes.begin()); };
        const auto& per_difficulty = type_info.diff[std::size_t(difficulty)];
        for (std::size_t k = 0; k < 5; ++k) scene.aip[k] = per_difficulty.aip[k];
        const auto* skill1 = skill_named(type_info.skill[0]);
        const auto* skill2 = skill_named(type_info.skill[1]);
        scene.skill1 = skill1 ? skill1->id : -1; scene.skill2 = skill2 ? skill2->id : -1;
        scene.mode1 = mode_of(type_info.sk_mode[0]); scene.mode2 = mode_of(type_info.sk_mode[1]);
        scene.skill_mode = scene.mode1;
        scene.velocity = type_info.velocity; scene.run = type_info.run;
        // How it follows: PetFollow on its PetView (as NecroPet's).
        d2d::rules::PetView view{ .x = pet_x, .y = pet_y, .size = size, .owner_x = sub(actor->player.x), .owner_y = sub(actor->player.y), .owner_mode = actor->owner_mode,
                                  .cur_x = sub(actor->player.path.empty() ? actor->player.x : actor->player.path.front().first),
                                  .cur_y = sub(actor->player.path.empty() ? actor->player.y : actor->player.path.front().second),
                                  .end_x = sub(actor->player.path.empty() ? actor->player.x : actor->player.path.back().first),
                                  .end_y = sub(actor->player.path.empty() ? actor->player.y : actor->player.path.back().second),
                                  .ring = actor->footsteps, .cursor = actor->footstep_cursor, .arrive_x = actor->warp_spot.first, .arrive_y = actor->warp_spot.second,
                                  .town = town, .velocity = type_info.velocity, .run = type_info.run };
        view.pets = int(std::ranges::count_if(actor->pets, [](const Pet& other) { return other.monster.alive(); }));
        view.crowd = int(std::ranges::count_if(actor->pets, [&](const Pet& other) {
            return &other != &pet && other.monster.alive() && other.where == level
                && d2d::rules::merc_gap(pet_x, pet_y, size, sub(other.monster.unit.x), sub(other.monster.unit.y)) < 2;
        }));
        auto area_under = [&](int from_x, int from_y, int x, int y) { return int(area_id_at(*game_data, *level, centre(from_x), centre(from_y), centre(x), centre(y))); };   // FUN_0061b130
        std::vector<std::pair<float, float>> route;
        int chased = -1;
        auto move_pet = [&](const d2d::rules::MercMove& move) {
            if (move.unit != 0) {
                if (move.unit == int(PetUnit::owner)) return set_off(*level, unit, actor->player.x, actor->player.y, true, crowd);
                const int monster_at = index[std::size_t(move.unit)];
                if (monster_at < 0 || !set_off(*level, unit, monsters[std::size_t(monster_at)].unit.x, monsters[std::size_t(monster_at)].unit.y, true, crowd)) return false;
                chased = monster_at;
                return true;
            }
            route = move_route(*level, unit, pet_x, pet_y, move, crowd);
            return !route.empty();
        };
        struct World {
            decltype(move_pet)& move_to;
            d2d::rules::PetView& view;
            d2d::rules::Rng& seed;
            decltype(area_under)& area_of;
            std::function<void()> teleport_to;
            d2d::rules::PetAct last;
            bool try_move(const d2d::rules::MercMove& move) { return move_to(move); }
            bool follow(int mode, bool run, int pct, int reach) {
                d2d::rules::PetFollow follower(view, seed, area_of, move_to);
                const bool took = follower.follow(mode, run, pct, reach);
                last = follower.act;
                return took;
            }
            bool decide(PetUnit foe, bool melee, bool stay, int reach) {
                d2d::rules::PetFollow follower(view, seed, area_of, move_to);
                const bool took = follower.decide(foe != PetUnit::none, melee, stay, reach);
                last = follower.act;
                return took;
            }
            bool teleport() { teleport_to(); return true; }
            std::function<void()> give;
            void give_hands() { give(); }
            [[nodiscard]] d2d::rules::PetAct followed() const { return last; }
        };
        World world{ move_pet, view, monster.seed, area_under, [&] { room_teleport(*game_data, *level, crowd, spawning, unit); }, {} };
        const auto& ai_name = type_info.ai_name;
        // The Shadow Warrior: the owner's left / right skills (given when its
        // think gets that far, at FUN_005eaf00's level: give_hands), its
        // class, and what Attack and each hand are to its AI (FUN_005ead50 /
        // FUN_005eabf0) once given.
        std::function<void()> give_hands = [] {};
        if (ai_name == "ShadowWarrior") {
            const auto hand = [&](std::uint32_t id) { return game_data->skills.get(int(id)) ? int(id) : -1; };
            scene.left = hand(actor->character.header.left_skill); scene.right = hand(actor->character.header.right_skill);
            scene.owner_class = std::clamp(actor->character.character_class, 0, 6);
            const auto hand_level = [&](int id) { return d2d::rules::shadow_hand_level(pet.ctrl[2], skill_level ? skill_level(id) : 1); };
            give_hands = [this, &pet, hand_level, left = scene.left, right = scene.right] { for (const int id : { left, right }) shadow_give(pet, id, hand_level(id)); };
            scene.pet_attack = std::ranges::contains(pet.skill_list, 0, &d2d::rules::ShadowListed::id);
            scene.aip8_nightmare = type_info.diff[1].aip[7]; scene.aip8_hell = type_info.diff[2].aip[7];
            const std::array<int, 3> hands{ 0, scene.left, scene.right };
            for (std::size_t k = 0; k < hands.size(); ++k) {
                auto& entry = scene.skills[k];
                entry = { .id = hands[k] };
                const auto* row = game_data->skills.get(hands[k]);
                const auto listed = std::ranges::find(pet.skill_list, hands[k], &d2d::rules::ShadowListed::id);
                const bool given = k > 0;                                    // a hand: as it will be once given
                if (!row || (!given && listed == pet.skill_list.end())) continue;
                const int lvl = std::max(given ? hand_level(hands[k]) : listed->level, 1);
                entry.cls = skill_class(*row);
                const auto held = std::ranges::find(pet.charges, hands[k], &Charge::skill);
                const bool buffed = std::ranges::contains(pet.buffs, hands[k], &SelfState::skill);
                const bool charged = !row->aurastate.empty() && (held != pet.charges.end() || buffed);   // in its aurastate: a buff on, or a charge-up's charges
                bool target_in = false;                                      // the driver's target in its auratargetstate (its curse / cry's)
                if (found >= 0 && !row->auratarget.empty())
                    for (const auto* effect : { &monsters[std::size_t(found)].curse, &monsters[std::size_t(found)].cry })
                        if (const auto* effect_skill = game_data->skills.get(effect->skill); effect_skill && effect_skill->auratarget == row->auratarget && now_ms < effect->until) target_in = true;
                entry.ai_ok = d2d::rules::shadow_ai_may_use(shadow_may_have(row, type_info, *game_data->skills.get(pet.skill)), row->aitype, scene.driver_melee, scene.driver,
                                                            charged, target_in, row->progressive, held != pet.charges.end() ? std::optional<int>(held->count) : std::nullopt);
                entry.mana = std::max(((row->mana + row->lvlmana * (lvl - 1)) << (row->manashift & 31)) >> 8, 0);   // FUN_006459f0
                entry.kind = listed != pet.skill_list.end() ? listed->kind : skill_kind(*row);
                entry.mode = listed != pet.skill_list.end() ? listed->mode : monster_mode(row->monanim);
                entry.delay = d2d::rules::eval_calc(game_data->skills, row->delay, calc_env(), row->id, lvl);
            }
            scene.skills[3] = { .id = -2 };
        }
        world.give = std::move(give_hands);
        d2d::rules::PetBrain brain(scene, monster.seed, pet.ctrl, world);
        // The Shadow Master (shadows.hpp) on the units round it: the pet, its
        // owner, the monsters within 64 subtiles of either, its fellow pets
        // (the traps it counts). Its world turns what it does into a PetAct.
        // A monster's states are its curse / cry (their auratargetstates).
        // ponytail: units by that radius, not FUN_005dd0b0's rooms.
        auto master_think = [&] {
            using d2d::rules::ShadowUnit;
            d2d::rules::ShadowMasterScene master;
            auto& units = master.units;
            std::vector<int> monster_of{ -1, -1 };                          // by unit: its monster (-1 the pet and owner, -2 a fellow pet)
            units.push_back({ .type = 1, .x = pet_x, .y = pet_y, .foe = false, .owner = 1 });
            units.push_back({ .type = 0, .x = sub(actor->player.x), .y = sub(actor->player.y), .targetable = false, .foe = false });
            const int owner_x = sub(actor->player.x), owner_y = sub(actor->player.y);
            for (std::size_t i = 0; i < monsters.size(); ++i) {
                const auto& other = monsters[i];
                const int other_x = sub(other.unit.x), other_y = sub(other.unit.y);
                if (std::max(std::abs(other_x - pet_x), std::abs(other_y - pet_y)) > 0x40 && std::max(std::abs(other_x - owner_x), std::abs(other_y - owner_y)) > 0x40) continue;
                if (!other.alive() && other.mode != "DT") continue;
                const auto& other_type = game_data->monsters.types[std::size_t(other.type)];
                const bool special = other.boss == d2d::rules::Boss::champion || other.boss == d2d::rules::Boss::unique || other.boss == d2d::rules::Boss::superunique;
                const auto foe = target_of(i);
                ShadowUnit entry{ .type = 1, .x = other_x, .y = other_y, .targetable = other.alive(), .dying = !other.alive(), .foe = other.align == 0,
                                  .melee = d2d::rules::unit_distance(other_x - pet_x, other_y - pet_y, size, other_type.size) <= type_info.melee_rng + 1,
                                  .worth = d2d::rules::shadow_worth(true, !other.alive(), game_data->mon_is_npc[std::size_t(other.type)], other_type.killable,
                                                                    other_type.boss_column, other_type.prime_evil, special),
                                  .blocked = d2d::rules::sight_blocked(pet_x, pet_y, size, other_x, other_y, other_type.size, wall),
                                  .monster_id = other_type.base >= 0 ? other_type.base : other.type };
                entry.resist = { foe.res[0], foe.res[2], foe.res[3], foe.res[1], foe.res[4], foe.res[5] };   // by EType: none, fire, light, magic, cold, poison
                entry.drain = foe.drain;
                units.push_back(std::move(entry));
                monster_of.push_back(int(i));
            }
            for (const auto& fellow : actor->pets)
                if (&fellow != &pet && fellow.monster.alive() && fellow.where == level) {
                    const auto& fellow_type = game_data->monsters.types[std::size_t(fellow.monster.type)];
                    units.push_back({ .type = 1, .x = sub(fellow.monster.unit.x), .y = sub(fellow.monster.unit.y), .targetable = false, .foe = false, .side = true,
                                      .monster_id = fellow_type.base >= 0 ? fellow_type.base : fellow.monster.type });
                    monster_of.push_back(-2);
                }
            const auto unit_of = [&](int monster_index) {
                const auto slot = monster_index >= 0 ? std::ranges::find(monster_of, monster_index) : monster_of.end();
                return slot == monster_of.end() ? -1 : int(slot - monster_of.begin());
            };
            units[0].target = pet.target >= 0 && monsters[std::size_t(pet.target)].alive() ? unit_of(pet.target) : -1;
            units[1].target = actor->attack_mon >= 0 ? unit_of(actor->attack_mon) : -1;
            master.owner = 1;
            master.driver = found >= 0 ? unit_of(found) : -1;
            master.driver_melee = scene.driver_melee; master.driver_distance = scene.driver_distance;
            master.skills = pet.skill_list;
            for (const auto& listed : pet.skill_list)
                if (const auto* row = game_data->skills.get(listed.id)) {
                    const auto missile = game_data->missiles.find(row->srvmissilea);
                    static constexpr std::array<int, 7> kEType{ 0, 1, 2, 4, 5, 3, 6 };   // d2d's etype (-1 none, fire, light, cold, poison, magic, stun) as EType
                    master.rows[listed.id] = { .aitype = row->aitype, .bonus = row->aibonus, .reqlevel = row->reqlevel,
                                               .etype = kEType[std::size_t(std::clamp(row->etype + 1, 0, 6))],
                                               .state = row->aurastate.empty() ? 0 : 1000 + row->id, .state2 = row->auratarget.empty() ? 0 : 2000 + row->id,
                                               .progressive = row->progressive,
                                               .srvmissile = row->srvmissile.empty() ? -1 : 0, .missile = row->srvmissilea.empty() ? -1 : 0,
                                               .missile_range = missile == game_data->missiles.end() ? -1 : missile->second.range,
                                               .repeat = row->srvdofunc == ServerDoFunction::kBreath };
                }
            for (std::size_t k = 2; k < units.size(); ++k)                  // a monster in a skill's auratargetstate: its curse or cry's
                if (monster_of[k] >= 0)
                    for (const auto* effect : { &monsters[std::size_t(monster_of[k])].curse, &monsters[std::size_t(monster_of[k])].cry })
                        if (const auto* effect_skill = game_data->skills.get(effect->skill); effect_skill && !effect_skill->auratarget.empty() && now_ms < effect->until)
                            for (const auto& [id, row] : master.rows)
                                if (const auto* listed_row = game_data->skills.get(id); row.state2 > 0 && listed_row && listed_row->auratarget == effect_skill->auratarget) units[k].states.push_back(row.state2);
            for (const auto& buff : pet.buffs)                              // its self states' aurastates on it
                if (const auto row = master.rows.find(buff.skill); row != master.rows.end() && row->second.state > 0) units[0].states.push_back(row->second.state);
            for (const auto& held : pet.charges)                            // a charge-up's aurastate on it, its count the aurastat
                if (const auto row = master.rows.find(held.skill); row != master.rows.end() && row->second.state > 0) {
                    units[0].states.push_back(row->second.state);
                    row->second.charges = held.count;
                }
            for (std::size_t k = 0; k < 3; ++k) { master.fixed[k] = type_info.diff[k].aip[0]; master.fixed[k + 3] = type_info.diff[k].aip[1]; }
            master.aip3 = per_difficulty.aip[2];
            master.scan = d2d::rules::shadow_scan(units, 0, 1);
            master.life = monster.hit_points * 100 / std::max(monster.stats.hit_points, 1);
            master.left = pet.left_skill >= 0;
            master.town = town;
            master.charged = !pet.charges.empty();                         // FUN_0063a2b0: a progressive_* state on it
            d2d::rules::PetAct out;
            const auto aim = [&](int target) {
                index[3] = target >= 0 ? monster_of[std::size_t(target)] : -1;
                return index[3] >= 0 ? PetUnit::foe : PetUnit::none;
            };
            struct MasterWorld {
                std::function<bool(int, bool)> decide;
                std::function<bool(int, int, int)> skill;
                std::function<bool(int)> approach;
                std::function<bool(int, int)> away;
                std::function<void(int)> run, set_left, stand;
            } master_world;   // assigned one by one: MSVC can't take these lambdas in a braced init (C2011)
            master_world.decide = [&](int foe, bool melee) {
                const bool took = world.decide(foe >= 0 ? PetUnit::foe : PetUnit::none, melee, false, 6);
                if (took) out = world.followed();
                return took;
            };
            master_world.skill = [&](int mode, int id, int target) {
                out = { .kind = d2d::rules::PetAct::Kind::skill, .unit = aim(target), .skill = id, .mode = mode };
                return true;
            };
            master_world.approach = [&](int target) {                                               // FUN_005ded00(target, 4): a walk at it
                if (aim(target) == PetUnit::none) return false;
                const d2d::rules::MercMove move{ .unit = int(PetUnit::foe) };
                if (!move_pet(move)) return false;
                out = { .kind = d2d::rules::PetAct::Kind::chase, .move = move, .unit = PetUnit::foe };
                return true;
            };
            master_world.away = [&](int x, int y) {                                             // FUN_005df140: 8 away, path type 0xf
                const d2d::rules::MercMove move{ .x = x, .y = y, .type = 0xf, .steps = 8 };
                if (!move_pet(move)) return false;
                out = { .kind = d2d::rules::PetAct::Kind::moved, .move = move };
                return true;
            };
            master_world.run = [&](int) {                                                      // FUN_005ded20: a run at the owner
                const d2d::rules::MercMove move{ .mode = d2d::rules::kMonsterRun, .unit = int(PetUnit::owner) };
                if (move_pet(move)) out = { .kind = d2d::rules::PetAct::Kind::chase, .move = move, .unit = PetUnit::owner };
            };
            master_world.set_left = [&](int id) { pet.left_skill = id; };
            master_world.stand = [&](int frames) { out = { .kind = d2d::rules::PetAct::Kind::stand, .frames = frames }; };
            d2d::rules::shadow_master_think(master, monster.seed, pet.ctrl, master_world);
            return out;
        };
        if (ai_name == "ShadowMaster") brain.act = master_think();
        else if (ai_name == "ShadowWarrior") d2d::rules::shadow_warrior_think(brain);
        else if (ai_name == "Hydra") d2d::rules::hydra_think(brain);
        else if (ai_name == "AssassinSentry") d2d::rules::sentry_think(brain);
        else if (ai_name == "DeathSentry") d2d::rules::death_sentry_think(brain);
        else if (ai_name == "Raven") d2d::rules::raven_think(brain);
        else if (ai_name == "DruidWolf" && monster.npc.hc_idx == 0x1a4) d2d::rules::spirit_wolf_think(brain);
        else if (ai_name == "DruidWolf") d2d::rules::fenris_think(brain);
        else if (ai_name == "Totem") d2d::rules::totem_think(brain);
        else if (ai_name == "Vines") d2d::rules::poison_creeper_think(brain);
        else if (ai_name == "CycleOfLife") d2d::rules::cycle_vine_think(brain);
        else d2d::rules::druid_bear_think(brain);
        const auto& act = brain.act;
        monster.force_sight = act.unreachable;
        using Kind = d2d::rules::PetAct::Kind;
        const int aidel = per_difficulty.aidel > 0 ? per_difficulty.aidel : 15;
        const auto target_of_unit = [&](PetUnit which) { return which == PetUnit::none ? -1 : index[std::size_t(which)]; };
        switch (act.kind) {
        case Kind::moved:
        case Kind::chase: {
            set_mode(*game_data, monster, act.move.mode == d2d::rules::kMonsterRun ? "RN" : "WL", now_ms);
            if (act.kind != Kind::chase) unit.path = route;
            unit.walking = true;
            if (act.kind == Kind::moved) { unit.goal_x = centre(act.move.x); unit.goal_y = centre(act.move.y); unit.budget = 0x14; unit.path_points = int(route.size()); unit.to_unit = false; }
            monster.move_pct = act.move.pct;
            pet.chase = act.kind == Kind::chase && chased >= 0;
            pet.target = pet.chase ? chased : -1;
            break;
        }
        case Kind::swing:
        case Kind::skill:
        case Kind::seq: {
            const int target = target_of_unit(act.unit);
            pet.target = target;
            pet.cast = act.kind == Kind::swing ? -1 : act.skill;
            pet.cast_x = centre(act.x); pet.cast_y = centre(act.y);
            if (target >= 0) unit.dir = direction16(monsters[std::size_t(target)].unit.x - unit.x, monsters[std::size_t(target)].unit.y - unit.y);
            unit.path.clear(); unit.walking = false;
            const std::string_view mode = act.kind == Kind::skill && act.mode < 16 && game_data->npc_timing(monster.npc, kModes[std::size_t(act.mode)]).directions
                                              ? kModes[std::size_t(act.mode)] : "A1";
            monster.next_act = now_ms + std::uint32_t(std::max(act.frames, 0)) * 40;
            if (!game_data->npc_timing(monster.npc, mode).directions) {   // no frames for it (a trap): at once
                if (pet.cast >= 0) pet_cast(pet, now_ms, crowd);
                pet.cast = -1;
                monster.next_act = std::max(monster.next_act, now_ms + 40);
                break;
            }
            set_mode(*game_data, monster, mode, now_ms);
            monster.struck = false;
            break;
        }
        case Kind::die:
            monster.hit_points = 0;
            set_mode(*game_data, monster, "DT", now_ms);
            break;
        case Kind::teleport:                                        // mode 3: room_teleport, stand 5
            room_teleport(*game_data, *level, crowd, spawning, unit);
            unit.path.clear(); unit.walking = false;
            monster.next_act = now_ms + 5 * 40;
            break;
        case Kind::stand:
            monster.next_act = now_ms + std::uint32_t(std::max(act.frames, 1)) * 40;
            break;
        case Kind::failed:
            monster.next_act = now_ms + std::uint32_t(aidel) * 40;
            break;
        }
    }

// A traced pet's skill on its attack mode's action frame (pet.cast at
// pet.target or its spot): a trap's or the Hydra's shot, the Death
// Sentry's corpse blast, Poison Creeper's poison, the cycle vines eating a
// corpse (their skill's Param5 % of the owner's max life / mana), a wolf's
// Teleport, Fenris eating a corpse, the Grizzly's Maul (a blow).
// ponytail: Vine Attack and the cyclers (do 130, st 63) and Fenris's rage
// as their published effects, not traced.
// Blade Shield's strike (do 54, a periodic skill every perdelay frames:
// FUN_005d7e10 -> FUN_005d7ce0, each unit by FUN_005d7c40): every monster
// within aurarange of (x, y), a hit at the skill's to-hit (FUN_006449f0),
// its own damage (FUN_0056e170, MinDam..MaxDam by level) and the weapon's
// at SrcDam (FUN_0057b7d0, 32 / 128).
auto Fight::blade_shield(const d2d::rules::Skill& skill, int lvl, float x, float y, const d2d::rules::Fighter& fighter, bool by_player, int striker, std::uint32_t now_ms) -> void {
        const auto env = calc_env();
        d2d::rules::Swing swing;
        swing.ar_pct = d2d::rules::skill_tohit(game_data->skills, skill, env, lvl);
        swing.srcdam = skill.srcdam;
        swing.skill_lo = d2d::rules::skill_phys(game_data->skills, skill, env, lvl, false);
        swing.skill_hi = d2d::rules::skill_phys(game_data->skills, skill, env, lvl, true);
        const int radius = std::max(calc(skill, skill.aurarange, lvl), 1);
        for (std::size_t j = 0; j < monsters.size(); ++j)
            if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - x, monsters[j].unit.y - y) * 5 <= float(radius))
                land(j, d2d::rules::player_blow(fighter, target_of(j), int(actor->character.stats.get(d2d::d2s::kLevel)), rng, swing), by_player, now_ms, striker);
    }

// What a Shadow's self states give it: their aurastats at their levels,
// summed again as one starts or ends.
auto Fight::pet_buffs(Pet& pet) -> void {
        pet.buff_stats.clear();
        const auto env = calc_env();
        auto add = [&](int skill_id, int lvl) {
            if (const auto* skill = game_data->skills.get(skill_id))
                for (std::size_t k = 0; k < skill->aurastat.size(); ++k)
                    if (skill->aurastat[k] >= 0) pet.buff_stats[skill->aurastat[k]] += d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], env, skill->id, lvl);
        };
        for (const auto& buff : pet.buffs) add(buff.skill, buff.level);
        if (pet.curse.skill >= 0) add(pet.curse.skill, pet.curse.level);
        // Battle Orders' maxhp percent on its life, as the player's (bo_life).
        auto& stats = pet.monster.stats;
        const int want = int(std::int64_t(stats.hit_points - pet.bo_life) * pet_buff_stat(pet, d2d::d2s::kMaxLifePercent) / 100);
        stats.hit_points += want - pet.bo_life;
        pet.monster.hit_points = std::min(pet.monster.hit_points + std::max(want - pet.bo_life, 0), stats.hit_points);
        pet.bo_life = want;
    }

// FUN_00647280 on a Shadow: a skill it has takes the new level; else it
// joins the list's tail with its monanim mode and kind (FUN_00647110).
auto Fight::shadow_give(Pet& pet, int skill, int lvl) const -> void {
        const auto* row = game_data->skills.get(skill);
        if (!row) return;
        if (const auto found = std::ranges::find(pet.skill_list, skill, &d2d::rules::ShadowListed::id); found != pet.skill_list.end()) { found->level = lvl; return; }
        pet.skill_list.push_back({ .id = skill, .level = lvl, .kind = skill_kind(*row), .mode = monster_mode(row->monanim) });
    }

// A Shadow's skill on its action frame, at its level on the Shadow: Attack
// and the claws and kicks as its owner's blow (it has the owner's gear) with
// the skill's swing; a missile skill fired from it (Blade Fury, Fire Blast,
// Shock Web, ...); Psychic Hammer and Mind Blast at its target.
// Dragon Flight puts it beside the target and kicks. Charge-ups charge it,
// finishers release them, as the player's.
// Its self states, traps and Blade Sentinel as the player's.
// ponytail: Dragon Flight's two events (the flight, then the kick) land
// on one frame.
auto Fight::shadow_cast(Pet& pet, std::uint32_t now_ms, const Crowd& crowd) -> void {
        const auto& monster = pet.monster;
        const auto* skill = game_data->skills.get(pet.cast);
        if (!skill) return;
        const auto listed = std::ranges::find(pet.skill_list, skill->id, &d2d::rules::ShadowListed::id);
        const int lvl = std::max(listed == pet.skill_list.end() ? 1 : listed->level, 1);
        const int target = pet.target >= 0 && monsters[std::size_t(pet.target)].alive() ? pet.target : -1;
        const auto& foe = target >= 0 ? monsters[std::size_t(target)].unit : monster.unit;
        const Caster caster{ monster.unit.x, monster.unit.y, lvl, target, foe.x, foe.y, false, true, monster.id };
        // Its self states (do 18 Burst of Speed / Fade / Venom, 47 Cloak of
        // Shadows, 54 Blade Shield): auralen frames on it; Cloak blinds the
        // monsters within its aurarange (their cry, as the player's).
        if (skill->srvdofunc == ServerDoFunction::kSelfState || skill->srvdofunc == ServerDoFunction::kCloakOfShadows || skill->srvdofunc == ServerDoFunction::kBladeShield) {
            const int len = calc(*skill, skill->auralen, lvl);
            const std::uint32_t until = len > 0 ? now_ms + std::uint32_t(len) * 40 : ~0u;
            std::erase_if(pet.buffs, [&](const SelfState& buff) { return buff.skill == skill->id; });
            pet.buffs.push_back({ skill->id, lvl, until });
            pet_buffs(pet);
            if (skill->srvdofunc == ServerDoFunction::kCloakOfShadows)
                for (auto& other : monsters)
                    if (other.alive() && std::hypot(other.unit.x - monster.unit.x, other.unit.y - monster.unit.y) * 5 <= float(calc(*skill, skill->aurarange, lvl)))
                        cloak(*game_data, other, skill->id, lvl, until, now_ms);
            return;
        }
        // Its traps and Blade Sentinel (do 45 / 44): laid at its target (else
        // where it stands) once the pets have had their turns.
        if (skill->srvdofunc == ServerDoFunction::kTrap || skill->srvdofunc == ServerDoFunction::kBladeSentinel) {
            actor->pet_summons.push_back({ skill->id, lvl, foe.x, foe.y });
            return;
        }
        if (skill->id != 0 && missile_skill(*skill)) { fire(*skill, now_ms, caster); return; }
        if (skill->srvdofunc == ServerDoFunction::kPsychicHammer || skill->srvdofunc == ServerDoFunction::kMindBlast) { spot(*skill, now_ms, caster); return; }
        std::erase_if(pet.charges, [&](const Charge& charge) { return now_ms >= charge.until; });
        if (skill->srvdofunc == ServerDoFunction::kDragonFlight && target >= 0) {   // do 52: beside the target, then the kick (as the owner's)
            if (in_town(level)) return;
            auto& unit = pet.monster.unit;
            put_near(*level, crowd, unit, foe.x, foe.y);
            unit.path.clear(); unit.walking = false;
            auto fighter = actor->pf_kick;
            auto swing = skill_swing(*skill, lvl, calc_env(), false);
            add_charges(pet.charges, fighter, swing, false);
            pet_strike(pet, std::size_t(target), d2d::rules::player_blow(fighter, target_of(std::size_t(target)), monster.stats.level, rng, swing), now_ms);
            release(pet.charges, std::size_t(target), now_ms, false);
            return;
        }
        if (target < 0 || (skill->id != 0 && skill_kind(*skill) != 1)) return;
        if (std::hypot(foe.x - monster.unit.x, foe.y - monster.unit.y) > kMeleeReach + 0.3f) return;
        // As the player's hit: a charge-up's hit adds a charge, a finisher
        // takes the charges' bonus and releases them on a hit.
        auto swing = skill->id == 0 ? d2d::rules::Swing{} : skill_swing(*skill, lvl, calc_env(), false);
        auto fighter = swing.kick ? actor->pf_kick : actor->player_combat;
        if (const int length = pet_buff_stat(pet, d2d::d2s::kPoisonLength); length > 0) {   // Venom: its poison on the blow
            auto& [low, high] = fighter.elem[3];
            low += pet_buff_stat(pet, d2d::d2s::kPoisonMinDamage) * length / 256; high += pet_buff_stat(pet, d2d::d2s::kPoisonMaxDamage) * length / 256;
            fighter.poison_len = std::max(fighter.poison_len, length);
        }
        const bool finishing = finisher(skill);
        if (finishing) add_charges(pet.charges, fighter, swing, false);
        const auto blow = d2d::rules::player_blow(fighter, target_of(std::size_t(target)), monster.stats.level, rng, swing);
        pet_strike(pet, std::size_t(target), blow, now_ms);
        if (blow.hit && skill->srvstfunc == ServerStartFunction::kChargeUp) charge(pet.charges, *skill, lvl, now_ms);
        if (blow.hit && finishing) release(pet.charges, std::size_t(target), now_ms, false);
    }

auto Fight::pet_cast(Pet& pet, std::uint32_t now_ms, const Crowd& crowd) -> void {
        if (pet.mirror) { shadow_cast(pet, now_ms, crowd); return; }
        auto& monster = pet.monster;
        const auto* skill = game_data->skills.get(pet.cast);
        const int target = pet.target;
        const bool alive = target >= 0 && monsters[std::size_t(target)].alive();
        if (!skill) return;
        if (skill->name.starts_with("Teleport")) { put_near(*level, crowd, monster.unit, pet.cast_x, pet.cast_y); return; }   // "Teleport 2": a wolf to its owner
        if (pet.shot_skill >= 0 && alive && skill->srvdofunc != ServerDoFunction::kCorpseExplosion) { trap_fire(pet, std::size_t(target), now_ms); return; }
        if (pet.ranged >= 0 && alive) {
            const auto* missile_info = pet_missile(*game_data->skills.get(pet.ranged), pet.variant);
            if (!missile_info) return;
            auto& foe = monsters[std::size_t(target)];
            Missile x{ missile_info, monster.unit.x, monster.unit.y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(missile_info->range, 1)) * 40, {} };
            const float dx = foe.unit.x - monster.unit.x, dy = foe.unit.y - monster.unit.y, distance = std::max(std::hypot(dx, dy), 0.01f);
            x.velocity_x = dx / distance * missile_speed(*missile_info, pet.ranged_level); x.velocity_y = dy / distance * missile_speed(*missile_info, pet.ranged_level);
            x.dir = missile_facing(monster.unit.x, monster.unit.y, dx, dy); x.to = { foe.unit.x, foe.unit.y };
            x.friendly = true; x.caster = actor->id; x.level = pet.ranged_level; x.skill = pet.ranged;
            if (const auto* missile_skill_row = skill_named(missile_info->skill)) x.skill = missile_skill_row->id;
            pending.push_back(std::move(x));
            return;
        }
        if (target < 0) return;
        if (const auto* pet_skill = game_data->skills.get(pet.skill); skill->srvdofunc == ServerDoFunction::kCorpseExplosion || (pet_skill && pet_skill->name == "Death Sentry" && !alive)) {
            if (pet_skill)
                for (std::size_t k = 0; k < 5; ++k)
                    if (const auto* bone_skill = skill_named(pet_skill->sumskill[k]); bone_skill && bone_skill->srvdofunc == ServerDoFunction::kCorpseExplosion && !monsters[std::size_t(target)].corpse_used) {
                        corpse_blast(*bone_skill, pet.shot_level, std::size_t(target), now_ms);
                        break;
                    }
            return;
        }
        if (const auto* pet_skill = game_data->skills.get(pet.skill); pet_skill && pet_skill->srvdofunc == ServerDoFunction::kVine) {
            using namespace d2d::d2s;
            if (pet_skill->etype == 3 && alive) {                      // Poison Creeper
                const auto damage = d2d::rules::missile_damage(game_data->skills, *pet_skill, calc_env(), monster.stats.level);
                land(std::size_t(target), d2d::rules::missile_blow(damage, target_of(std::size_t(target)), pierce(), rng), false, now_ms, pet.monster.id);
            } else if (!alive && !monsters[std::size_t(target)].corpse_used) {   // Carrion Vine / Solar Creeper
                monsters[std::size_t(target)].corpse_used = true;
                const bool life = pet_skill->name == "Cycle of Life";
                auto& value = actor->character.stats.values[life ? kLife : kMana];
                value = std::min(actor->character.stats.values[life ? kMaxLife : kMaxMana], value + actor->character.stats.values[life ? kMaxLife : kMaxMana] * pet_skill->par[4] / 100);
            }
            return;
        }
        if (!alive) { monsters[std::size_t(target)].corpse_used = true; return; }   // Fenris eats the corpse
        if (std::hypot(monsters[std::size_t(target)].unit.x - monster.unit.x, monsters[std::size_t(target)].unit.y - monster.unit.y) <= kMeleeReach + 0.3f)
            pet_strike(pet, std::size_t(target), d2d::rules::player_blow(d2d::rules::simple_fighter(monster.stats.a1_min, monster.stats.a1_max, monster.stats.to_hit),
                                                                         target_of(std::size_t(target)), monster.stats.level, rng), now_ms);
    }

}  // namespace d2d::game
