// SPDX-License-Identifier: GPL-3.0-or-later
// Fight (fight.hpp): summons and traps. The rest is in this directory.
#include "fight.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "gamedata.hpp"
#include "internal.hpp"

#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <mon_equip.hpp>
#include <monsters.hpp>
#include <pets.hpp>
#include <rules.hpp>
#include <shadows.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

auto Fight::summon_skill(const d2d::rules::Skill& skill) const -> bool {
        if (skill.srvdofunc == ServerDoFunction::kRevive) return true;                  // Revive: the corpse's own monster
        return !skill.summon.empty() && game_data->monsters.row(skill.summon) >= 0
            && (skill.srvdofunc == ServerDoFunction::kGolem || skill.srvdofunc == ServerDoFunction::kIronGolem || skill.srvdofunc == ServerDoFunction::kRaiseSkeleton || skill.srvdofunc == ServerDoFunction::kValkyrie || skill.srvdofunc == ServerDoFunction::kRaven
                || skill.srvdofunc == ServerDoFunction::kHydra || skill.srvdofunc == ServerDoFunction::kDecoy || skill.srvdofunc == ServerDoFunction::kMirrorImage || skill.srvdofunc == ServerDoFunction::kBoneWall || skill.srvdofunc == ServerDoFunction::kBonePrison
                || skill.srvdofunc == ServerDoFunction::kVine
                || (skill.srvdofunc == ServerDoFunction::kDruidSummon && (skill.pettype == "spiritwolf" || skill.pettype == "fenris" || skill.pettype == "grizzly" || skill.pettype == "totem"))
                || (skill.srvdofunc == ServerDoFunction::kTrap && trap_shot(skill) >= 0));
    }

auto Fight::trap_shot(const d2d::rules::Skill& skill) const -> int {
        for (const auto& name : skill.sumskill)
            if (const auto found = game_data->skills.by_name.find(name); !name.empty() && found != game_data->skills.by_name.end())
                if (const auto* trap_skill = game_data->skills.get(found->second); trap_skill && skill_missile(*trap_skill, true)) return trap_skill->id;
        return -1;
    }

auto Fight::cast_summon(int skill, float target_x, float target_y, std::uint32_t now_ms) -> bool {
        const auto* skill_row = game_data->skills.get(skill);
        if (!skill_row || dead() || actor->pmode >= 0 || !summon_skill(*skill_row)) return false;
        if (skill_row->target_corpse && corpse_near(target_x, target_y) < 0) return false;
        if (ask(skill, now_ms) != skill) return false;
        const int lvl = cast_level(skill);
        pay(*skill_row, lvl);
        actor->swing_skill = skill;
        actor->cast_x = target_x; actor->cast_y = target_y;
        actor->attack_mon = -1;
        actor->player.dir = direction16(target_x - actor->player.x, target_y - actor->player.y);
        set_pmode(kModeSC, now_ms);
        actor->pstruck = false;
        return true;
    }

auto Fight::corpse_near(float x, float y) const -> int {
        int best = -1; float best_distance = 2.f;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (const auto& corpse = monsters[i]; !corpse.alive() && !corpse.corpse_used && corpse.mode == "DD")
                if (const float distance = std::hypot(corpse.unit.x - x, corpse.unit.y - y); distance < best_distance) { best_distance = distance; best = int(i); }
        return best;
    }

auto Fight::skill_named(std::string_view name) const -> const d2d::rules::Skill* {
        if (name.empty()) return nullptr;
        if (const auto found = game_data->skills.by_name.find(std::string(name)); found != game_data->skills.by_name.end()) return game_data->skills.get(found->second);
        for (const auto& [skill_name, id] : game_data->skills.by_name)
            if (std::ranges::equal(skill_name, name, [](char left, char right) { return std::tolower(std::uint8_t(left)) == std::tolower(std::uint8_t(right)); }))
                return game_data->skills.get(id);
        return nullptr;
    }

auto Fight::pet_missile(const d2d::rules::Skill& skill, int variant ) const -> const GameData::MissileInfo* {
        std::string name = skill.srvmissile.empty() ? skill.srvmissilea : skill.srvmissile;
        if (skill.srvdofunc == ServerDoFunction::kVariantMissile && !name.empty() && name.back() == '1') name.back() = char('1' + std::clamp(variant, 0, 3));
        const auto found = game_data->missiles.find(name);
        return name.empty() || found == game_data->missiles.end() ? nullptr : &found->second;
    }

auto Fight::summon(const d2d::rules::Skill& skill, int lvl, float x, float y, std::uint32_t now_ms) -> void {
        int type = game_data->monsters.row(skill.summon);
        const auto env = calc_env();
        if (skill.target_corpse) {
            const int corpse = corpse_near(x, y);
            if (corpse < 0) return;
            monsters[std::size_t(corpse)].corpse_used = true;
            x = monsters[std::size_t(corpse)].unit.x; y = monsters[std::size_t(corpse)].unit.y;
            if (skill.srvdofunc == ServerDoFunction::kRevive) type = monsters[std::size_t(corpse)].type;
        }
        if (type < 0) return;
        // Bone Wall (do 60, FUN_005c58b0): one on the point, then its two
        // makers (srvmissilea, missile do 13: missile_tick) fly across the
        // caster's line from it, each laying up to calc2 / 2 more
        // (rules::bone_wall_makers). Bone Wall can't be cast in town (InTown).
        // Bone Prison (62, FUN_005c5d00): twelve round the point at 0x6e304c / 0x6e307c.
        if (skill.srvdofunc == ServerDoFunction::kBoneWall) {
            const int first = summon_one(skill, type, lvl, env, x, y, now_ms);
            const int piece_x = int(std::floor(x * 5)), piece_y = int(std::floor(y * 5));
            const auto makers = d2d::rules::bone_wall_makers(int(std::floor(actor->player.x * 5)), int(std::floor(actor->player.y * 5)), piece_x, piece_y,
                                                             calc(skill, skill.calc[1], lvl), false);
            const auto maker = game_data->missiles.find(skill.srvmissilea);
            if (makers.count <= 0 || maker == game_data->missiles.end()) return;
            const float from_x = (float(piece_x) + 0.5f) / 5, from_y = (float(piece_y) + 0.5f) / 5;
            for (const auto& [to_x, to_y] : makers.aims) {
                const float aim_x = (float(to_x) + 0.5f) / 5, aim_y = (float(to_y) + 0.5f) / 5;
                auto& made = launch(maker->second, skill, lvl, from_x, from_y, aim_x - from_x, aim_y - from_y, maker->second.range + maker->second.lev_range * lvl, now_ms);
                made.to = std::pair{ aim_x, aim_y };
                made.hops = makers.count; made.target_id = first;
                made.subtile_x = piece_x; made.subtile_y = piece_y;
            }
            return;
        }
        if (skill.srvdofunc == ServerDoFunction::kBonePrison) {
            static constexpr int kRingX[12] = { -1, 1, 3, 4, 4, 3, -1, 1, -3, -4, -4, -3 };
            static constexpr int kRingY[12] = { -4, -4, -3, -1, 1, 3, 4, 4, 3, -1, 1, -3 };
            for (std::size_t k = 0; k < 12; ++k) summon_one(skill, type, lvl, env, x + float(kRingX[k]) / 5, y + float(kRingY[k]) / 5, now_ms);
            return;
        }
        // Hydra (do 144, FUN_005ca910): three at once, each through FUN_0056d940 ->
        // FUN_005b2f20 at radius -1: the cast's subtile alone, else that one isn't made.
        if (skill.srvdofunc == ServerDoFunction::kHydra) {
            Crowd others;
            crowd(others);
            for (int head = 0; head < 3; ++head)
                if (int at_x = 0, at_y = 0; spawn_spot(*game_data, *level, others, &spawning.levels[level], type, int(std::floor(x * 5)), int(std::floor(y * 5)), -1, room_of(*level, x, y), at_x, at_y))
                    summon_one(skill, type, lvl, env, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, now_ms);
            return;
        }
        summon_one(skill, type, lvl, env, x, y, now_ms);
    }

auto Fight::summon_one(const d2d::rules::Skill& skill, int type, int lvl, const d2d::rules::CalcEnv& env, float x, float y, std::uint32_t now_ms) -> int {
        const int max = std::max(d2d::rules::eval_calc(game_data->skills, skill.petmax, env, skill.id, lvl), 1);
        auto same = [&](const Pet& pet) { return game_data->skills.get(pet.skill) && game_data->skills.get(pet.skill)->pettype == skill.pettype; };
        while (skill.pettype != "none" && std::ranges::count_if(actor->pets, same) >= max) actor->pets.erase(std::ranges::find_if(actor->pets, same));
        const int clvl = int(actor->character.stats.get(d2d::d2s::kLevel));
        const int bonus = skill.srvdofunc == ServerDoFunction::kRaven || skill.srvdofunc == ServerDoFunction::kDruidSummon ? d2d::rules::eval_calc(game_data->skills, skill.calc[1], env, skill.id, lvl) : 0;
        const int plvl = std::max(std::min(clvl, clvl * 3 / 4 + bonus), 1);
        Pet pet;
        auto& monster = pet.monster;
        monster.type = type;
        monster.npc = game_data->mon_npc[std::size_t(type)];
        monster.unit.shape = monster.npc.shape.owned();   // a pet: +0xc4 bit 31 (FUN_0056d8d0)
        const auto& type_info = game_data->monsters.types[std::size_t(type)];
        for (std::size_t layer = 0; layer < 16; ++layer)
            if (!type_info.parts[layer].empty()) monster.npc.comp[layer] = type_info.parts[layer].front();
        monster.difficulty = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        monster.row_align = type_info.align; monster.classic = !game_expansion;   // a revived one swings as a classic game's (summons are Align 1)
        monster.stats = d2d::rules::monster_stats(game_data->monsters, type, monster.difficulty, rng);   // spawned at its MonStats Level (FUN_005b2f20 -> FUN_005b2a00; the stat init FUN_00573cb0)
        // Its life and damage straight from MonStats' columns (a Raise
        // Skeleton skeleton: 21 life, 1-2 damage in Normal; 30 / 42 in
        // Nightmare / Hell), not scaled by MonLvl: every player summon's row
        // is noRatio (+0xc & 4), which the stat init (FUN_00573cb0 ->
        // FUN_006538a0) and the per-mode damage (FUN_005a4f50) take as
        // written, and Align 1 (row +0x4c) skips the players' life bonus
        // (FUN_00573930).
        // ponytail: the life roll takes d2d's rng, not the pet's unit seed (+0x20).
        // Revive (do 58, FUN_005c56c0) raises the monster as it was: its own
        // stats at its level, life and level brought down to the owner's
        // when it's above it.
        if (skill.srvdofunc != ServerDoFunction::kRevive) {
            const auto& per_difficulty = game_data->monsters.types[std::size_t(type)].diff[std::size_t(monster.difficulty)];
            monster.stats.hit_points = std::max(rng.range(per_difficulty.min_hp, std::max(per_difficulty.max_hp, per_difficulty.min_hp)), 1);
            monster.stats.a1_min = per_difficulty.a1_min; monster.stats.a1_max = std::max(per_difficulty.a1_max, per_difficulty.a1_min);
            monster.stats.a2_min = per_difficulty.a2_min; monster.stats.a2_max = std::max(per_difficulty.a2_max, per_difficulty.a2_min);
        }
        // The Druid's and Raven's hit with the skill's physical damage (their
        // MonStats rows have none).
        // ponytail: where game.exe hands the skill's damage to the pet isn't
        // traced: not the summon (FUN_005c6910 / FUN_005c7390, FUN_005c49e0,
        // FUN_005c4470) nor the per-mode damage (FUN_005a4f50); the hit's next.
        if (monster.stats.a1_max == 0 && skill.maxdam > 0) {
            const auto damage = d2d::rules::missile_damage(game_data->skills, skill, env, lvl);
            monster.stats.a1_min = damage.phys_lo >> 8; monster.stats.a1_max = std::max(damage.phys_hi >> 8, monster.stats.a1_min);
        }
        monster.stats.level = skill.srvdofunc == ServerDoFunction::kRevive ? std::min(monster.stats.level, clvl) : skill.srvdofunc == ServerDoFunction::kMirrorImage || skill.srvdofunc == ServerDoFunction::kDecoy ? clvl : plvl;
        if (skill.srvdofunc != ServerDoFunction::kRevive && !game_data->monsters.lvl.empty()) {
            const auto& level_row = game_data->monsters.lvl[std::min<std::size_t>(std::size_t(plvl), game_data->monsters.lvl.size() - 1)];
            monster.stats.armor_class = level_row.armor_class[std::size_t(monster.difficulty)];
            monster.stats.to_hit = level_row.to_hit[std::size_t(monster.difficulty)];
        }
        // FUN_005c4470: the skill's passive stats (base) and aurastats (its
        // aurastate) onto the pet, at the skill's level through the owner's
        // calcs — Skeleton Mastery, Golem Mastery and the golems' synergies
        // come in that way — then life (with maxhp) + calc1 %, then its
        // sumskills at their sumsk calcs.
        std::unordered_map<int, int> pet_stats;
        for (std::size_t k = 0; k < 5; ++k)
            if (skill.passive_stat[k] >= 0) pet_stats[skill.passive_stat[k]] += calc(skill, skill.passive_calc[k], lvl);
        for (std::size_t k = 0; k < 6; ++k)
            if (skill.aurastat[k] >= 0) pet_stats[skill.aurastat[k]] += calc(skill, skill.aura_calc[k], lvl);
        // Last in FUN_005c4470 (0x5c49c7), FUN_005d6b60 at the skill's level:
        // the Valkyrie's MonEquip gear, the rows from her level down, made at
        // calc2 or min(max(3 x level, 1), the owner's level). Its stats join
        // hers; a two-handed weapon's damage replaces her A1 (FUN_0057b420).
        // ponytail: her unit seed (the walk) and the game seed (the items)
        // are the fight's rng; a Shadow's gear (the owner's items) isn't made,
        // it fights with the owner's (pet.mirror).
        if (const int first = std::size_t(type) < game_data->mon_equip_first.size() ? game_data->mon_equip_first[std::size_t(type)] : -1;
            first >= 0 && skill.srvdofunc != ServerDoFunction::kMirrorImage) {
            std::uint32_t worn = 0;
            for (const auto& item : actor->character.items)
                if (item.location == d2d::d2s::item_location::kEquipped && item.slot > 0 && item.slot < 11) worn |= 1u << item.slot;
            d2d::rules::Rng own{ rng.next() };
            const int ilvl = d2d::rules::summon_gear_level(calc(skill, skill.calc[1], lvl), lvl, clvl);
            std::vector<d2d::d2s::Item> items;
            for (const auto& gear : d2d::rules::monster_gear(game_data->mon_equip, first, type, lvl, false, 0, worn, own)) {
                const auto& code = game_data->mon_equip[gear.row].item[gear.slot];
                const std::string base = code.substr(0, code.find_last_not_of(' ') + 1);
                if (gear.owners || !game_data->rules.item_info.contains(base)) continue;
                d2d::rules::Rng unit_seed{ rng.next() }, item_seed{ rng.next() };
                auto item = d2d::rules::generate_item(game_data->rules, base, ilvl, gear.quality, item_seed, &unit_seed, nullptr, false, monster.difficulty);
                item.location = d2d::d2s::item_location::kEquipped;
                item.slot = gear.loc;
                items.push_back(std::move(item));
            }
            const auto worn_gear = d2d::rules::gear_sum(game_data->rules, items);
            for (std::size_t stat_id = 0; stat_id < worn_gear.sum.size(); ++stat_id)
                if (worn_gear.sum[stat_id] != 0) pet_stats[int(stat_id)] += int(stat_id == d2d::d2s::kMaxLife ? worn_gear.sum[stat_id] << 8 : worn_gear.sum[stat_id]);   // an item's maxhp is whole points
            std::tie(monster.stats.a1_min, monster.stats.a1_max) = d2d::rules::gear_damage(worn_gear, monster.stats.a1_min, monster.stats.a1_max);
            monster.stats.armor_class += int(worn_gear.defense);
        }
        auto stat = [&](int id) { const auto found = pet_stats.find(id); return found == pet_stats.end() ? 0 : found->second; };
        using namespace d2d::d2s;
        monster.stats.hit_points += stat(kMaxLife) >> 8;                                               // maxhp, 256ths
        monster.stats.hit_points += int(std::int64_t(monster.stats.hit_points) * calc(skill, skill.calc[0], lvl) / 100);
        monster.hit_points = monster.stats.hit_points = std::max(monster.stats.hit_points, 1);
        for (int* damage : { &monster.stats.a1_min, &monster.stats.a1_max }) { *damage += stat(kNormalDamage); *damage += *damage * stat(kDamagePercent) / 100; }   // item_normaldamage, damagepercent
        monster.stats.to_hit += stat(kToHit); monster.stats.to_hit += monster.stats.to_hit * stat(kToHitPercent) / 100;              // tohit, item_tohit_percent
        monster.stats.armor_class += stat(kArmorClass); monster.stats.armor_class += monster.stats.armor_class * (stat(kArmorPercent) + stat(kSkillArmorPercent)) / 100;   // armorclass, item / skill_armor_percent
        const auto& base_res = type_info.diff[std::size_t(monster.difficulty)].res;
        pet.res = { base_res[2] + stat(kFireResist), base_res[3] + stat(kLightningResist), base_res[4] + stat(kColdResist), base_res[5] + stat(kPoisonResist) };
        pet.thorns = stat(kThornsPercent); pet.fire_lo = stat(kFireMinDamage); pet.fire_hi = std::max(stat(kFireMaxDamage), pet.fire_lo); pet.speed_pct = stat(kVelocityPercent);
        for (std::size_t k = 0; k < 5; ++k) {
            const auto* k_s = skill_named(skill.sumskill[k]);
            const int skill_level_value = k_s ? calc(skill, skill.sumsk_calc[k], lvl) : 0;
            if (!k_s || skill_level_value < 1) continue;
            if (k_s->aura) { pet.aura = k_s->id; pet.aura_level = skill_level_value; }
            else if (pet_missile(*k_s) && skill.srvdofunc != ServerDoFunction::kVine && skill.srvdofunc != ServerDoFunction::kTrap) { pet.ranged = k_s->id; pet.ranged_level = skill_level_value; }
        }
        monster.unit.x = monster.home_x = x; monster.unit.y = monster.home_y = y;
        monster.unit.dir = actor->player.dir;
        pet.skill = skill.id;
        pet.where = level;
        pet.variant = int(rng(4));
        switch (skill.srvdofunc) {
            case ServerDoFunction::kRevive: pet.until = now_ms + std::uint32_t(std::max(calc(skill, skill.calc[1], lvl), 1)) * 40; break;   // Revive: calc2 frames
            case ServerDoFunction::kHydra:                                                                                   // Hydra: Param1 frames, it stands
                pet.until = now_ms + std::uint32_t(std::max(skill.par[0], 1)) * 40;
                if (const auto* hydra = skill_named("HydraMissile")) { pet.ranged = hydra->id; pet.ranged_level = lvl; }
                break;
            case ServerDoFunction::kDecoy:                                  // Decoy (FUN_005dc000): owner's life x calc3 %, calc2 frames, it stands
                monster.hit_points = monster.stats.hit_points = std::max(int(actor->character.stats.fixed(d2d::d2s::kMaxLife) * calc(skill, skill.calc[2], lvl) / 100), 1);
                pet.until = now_ms + std::uint32_t(std::max(calc(skill, skill.calc[1], lvl), 1)) * 40;
                pet.idle = true;
                break;
            case ServerDoFunction::kMirrorImage: pet.mirror = true; break;          // Shadow Warrior / Master (FUN_005d6e70): the owner's level and gear
            case ServerDoFunction::kRaven: pet.hits = std::max(skill.par[4], 1); break;   // Raven: its attacks
            case ServerDoFunction::kBoneWall: case ServerDoFunction::kBonePrison: {
                // It stands. BoneWall's init (FUN_005e0390) keeps the frame + its Skill1's Param2 (Bone Wall's 600; both
                // skills' Param2 is 600); its first think past that (25 frames after it's made, then every 15: FUN_005e0400)
                // puts it in DT, at frame 610.
                const int last = skill.par[1];
                pet.idle = true;
                pet.until = now_ms + std::uint32_t(last < 25 ? 25 : 25 + ((last - 25) / 15 + 1) * 15) * 40;
                break;
            }
            case ServerDoFunction::kVine: monster.stats.level = std::max(calc(skill, skill.calc[1], lvl), 1); break;   // Vines (FUN_005c6a80): calc2 is its level
            default: break;
        }
        if (skill.srvdofunc == ServerDoFunction::kTrap) {                             // a trap (FUN_005d6170 -> FUN_005d5e10)
            pet.shot_skill = trap_shot(skill);
            for (std::size_t k = 0; k < 5; ++k)
                if (game_data->skills.by_name.contains(skill.sumskill[k]) && game_data->skills.by_name.at(skill.sumskill[k]) == pet.shot_skill)
                    pet.shot_level = std::max(d2d::rules::eval_calc(game_data->skills, skill.sumsk_calc[k], env, skill.id, lvl), 1);
            const int calc4 = d2d::rules::eval_calc(game_data->skills, skill.calc[3], env, skill.id, lvl);
            pet.shots = calc4 > 0 ? calc4 : std::max(skill.par[0], 1);
        }
        // The AI control a traced think starts from (the AIs' inits, the
        // MonAI rows' second word at 0x73ca18; the Hydra's last frame, the
        // Raven's hits, a trap's shots as d2d counts them).
        if (const auto& ai_name = game_data->monsters.types[std::size_t(monster.type)].ai_name; ai_name == "Hydra") pet.ctrl[0] = int(pet.until / 40);
        else if (ai_name == "Raven") {
            pet.ctrl = d2d::rules::raven_init(type_info.diff[std::size_t(monster.difficulty)].aip[2], monster.seed);
            pet.ctrl[0] = pet.hits;                                        // its first think's hits, here at once
        }
        // The sentries' init (FUN_005ea290, MonAI 101 / 104, run as the pet
        // is made: FUN_0056d940 -> FUN_005b0e00): +0x14 the game frame (the
        // DeathSentry's last corpse, so a corpse whose id is that frame is
        // passed over once), +0x18 -1, the shots their first think takes
        // from Skill1's calc (FUN_005ea2b0), here at once; +0x1c stays 0.
        // Vines' and CycleOfLife's (FUN_005ec6a0): +0x14 0.
        else if (ai_name == "AssassinSentry" || ai_name == "DeathSentry") pet.ctrl = { int(now_ms / 40), pet.shots, 0 };
        // The Shadows' skills: their MonStats Skill1..8 at Sk*lvl +
        // MonsterSkillBonus, in Sk*mode (FUN_004ae8d0), then the Master's
        // init (FUN_005eb490: Attack, the owner's class skills it may have)
        // or the Warrior's (FUN_005eaf50: its level only; it takes the
        // owner's hands each think).
        if (skill.srvdofunc == ServerDoFunction::kMirrorImage) {
            for (std::size_t slot = 0; slot < type_info.skill.size(); ++slot)
                if (const auto* listed = skill_named(type_info.skill[slot]); listed && type_info.sk_lvl[slot] > 0) {
                    shadow_give(pet, listed->id, d2d::rules::monster_skill_level(type_info.sk_lvl[slot], monster.difficulty));
                    std::ranges::find(pet.skill_list, listed->id, &d2d::rules::ShadowListed::id)->mode = monster_mode(type_info.sk_mode[slot]);
                }
            if (type_info.ai_name == "ShadowMaster") {
                const auto& class_ids = game_data->skills.class_ids[std::size_t(std::clamp(actor->character.character_class, 0, 6))];
                const bool has_attack = std::ranges::contains(pet.skill_list, 0, &d2d::rules::ShadowListed::id);
                const auto gifts = d2d::rules::shadow_master_init(
                    pet.ctrl, true, lvl, has_attack, class_ids, [&](int id) { return shadow_may_have(game_data->skills.get(id), type_info, skill); },
                    [&](int id) { return skill_base ? skill_base(id) : 0; });
                for (const auto& gift : gifts) shadow_give(pet, gift.skill, gift.level);
                pet.left_skill = 0;
            } else pet.ctrl = d2d::rules::shadow_warrior_init(true, lvl);
        }
        // FUN_005c4470 (0x5c4934): its skill's sumumod, 1..0x2a, through
        // FUN_005a4850(sumumod, 1): a trap's 32, a Shadow's 42.
        if (skill.sumumod > 0 && skill.sumumod < 0x2b) monster.mods.push_back(skill.sumumod);
        set_mode(*game_data, monster, "NU", now_ms);
        pet.monster.id = next_id++;                              // a unit id as a monster's (a guided missile's hold on it)
        actor->pets.push_back(std::move(pet));
        return actor->pets.back().monster.id;
    }

// A trap's shot (its shot skill's missile) at monster `target`.
auto Fight::trap_fire(Pet& pet, std::size_t target, std::uint32_t now_ms) -> void {
        auto& monster = pet.monster;
        const auto* skill = game_data->skills.get(pet.shot_skill);
        if (!skill) return;
        const auto& missile_info = *skill_missile(*skill, true);
        const auto owner = game_data->skills.by_name.find(missile_info.skill);
        const float dx = monsters[target].unit.x - monster.unit.x, dy = monsters[target].unit.y - monster.unit.y, dist = std::max(std::hypot(dx, dy), 0.01f);
        const float speed = missile_speed(missile_info, pet.shot_level);
        // Inferno Sentry's (do 95, FUN_005cc4e0) is a stream of flames:
        // here eight at once, their reach staggered.
        const int count = skill->srvdofunc == ServerDoFunction::kScatteredBolts ? std::max(d2d::rules::eval_calc(game_data->skills, skill->calc[0], calc_env(), skill->id, pet.shot_level), 1)
                    : skill->srvdofunc == ServerDoFunction::kMonsterBreath ? 8 : 1;
        for (int j = 0; j < count; ++j) {
            const float angle = count > 1 ? (float(rng(81)) - 40) * 3.14159265f / 180 : 0.f;
            const float aim_x = dx * std::cos(angle) - dy * std::sin(angle), aim_y = dx * std::sin(angle) + dy * std::cos(angle);
            Missile x{ &missile_info, monster.unit.x, monster.unit.y, aim_x / dist * speed, aim_y / dist * speed, missile_facing(monster.unit.x, monster.unit.y, aim_x, aim_y), now_ms,
                       now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, {} };
            x.friendly = true; x.caster = actor->id; x.level = pet.shot_level;
            x.skill = owner != game_data->skills.by_name.end() ? owner->second : skill->id;
            if (skill->srvdofunc == ServerDoFunction::kMonsterBreath) { x.velocity_x = dx / dist * speed; x.velocity_y = dy / dist * speed; x.dies = now_ms + std::uint32_t(4 + 3 * j) * 40; }
            missiles.push_back(std::move(x));
        }
    }

}  // namespace d2d::game
