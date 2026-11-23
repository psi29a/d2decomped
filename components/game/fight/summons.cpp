// SPDX-License-Identifier: GPL-3.0-or-later
// Fight (fight.hpp): summons and traps. The rest is in this directory.
#include "fight.hpp"

#include "ai.hpp"
#include "ai/units.hpp"
#include "character.hpp"
#include "gamedata.hpp"
#include "internal.hpp"

#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

// A summoning skill d2d builds (the do spawns the `summon` row through
// FUN_0056d940): Clay / Blood / Iron / Fire Golem (56, 57), Raise
// Skeleton and Skeletal Mage (31, from a corpse), Valkyrie (16), the
// Druid's Raven (114), Spirit Wolf, Fenris and Grizzly (119).
auto Fight::summon_skill(const d2d::rules::Skill& skill) const -> bool {
        if (skill.srvdofunc == ServerDoFunction::kRevive) return true;                  // Revive: the corpse's own monster
        return !skill.summon.empty() && game_data->monsters.row(skill.summon) >= 0
            && (skill.srvdofunc == ServerDoFunction::kGolem || skill.srvdofunc == ServerDoFunction::kIronGolem || skill.srvdofunc == ServerDoFunction::kRaiseSkeleton || skill.srvdofunc == ServerDoFunction::kValkyrie || skill.srvdofunc == ServerDoFunction::kRaven
                || skill.srvdofunc == ServerDoFunction::kHydra || skill.srvdofunc == ServerDoFunction::kDecoy || skill.srvdofunc == ServerDoFunction::kMirrorImage || skill.srvdofunc == ServerDoFunction::kBoneWall || skill.srvdofunc == ServerDoFunction::kBonePrison
                || skill.srvdofunc == ServerDoFunction::kVine
                || (skill.srvdofunc == ServerDoFunction::kDruidSummon && (skill.pettype == "spiritwolf" || skill.pettype == "fenris" || skill.pettype == "grizzly" || skill.pettype == "totem"))
                || (skill.srvdofunc == ServerDoFunction::kTrap && trap_shot(skill) >= 0));
    }

// A trap's shooting skill: the first of its sumskills that fires a
// missile d2d builds (the monster skills 'sentry lightning',
// 'BoltSentry', 'death sentry ltng'); -1 for the others (Wake of Fire's
// and Inferno's do 125 / 95, Death Sentry's corpse blast do 55).
auto Fight::trap_shot(const d2d::rules::Skill& skill) const -> int {
        for (const auto& name : skill.sumskill)
            if (const auto found = game_data->skills.by_name.find(name); !name.empty() && found != game_data->skills.by_name.end())
                if (const auto* trap_skill = game_data->skills.get(found->second); trap_skill && skill_missile(*trap_skill, true)) return trap_skill->id;
        return -1;
    }

auto Fight::cast_summon(int skill, float target_x, float target_y, std::uint32_t now_ms) -> bool {
        const auto* skill_row = game_data->skills.get(skill);
        if (!skill_row || dead() || actor->pmode >= 0 || !summon_skill(*skill_row)) return false;
        if (skill_row->target_corpse && corpse_near(target_x, target_y, skill_row->srvstfunc) < 0) return false;
        if (skill_row->srvdofunc == ServerDoFunction::kIronGolem && golem_item_at(target_x, target_y) < 0) return false;   // start 20 (FUN_005c32a0)
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

// Raise's, the explosions' and Revive's corpse tests (rules::raise_takes, blast_takes, revive_takes) by the
// skill's srvstfunc; any other start takes any corpse.
auto Fight::corpse_takes(d2d::rules::ServerStartFunction start, const Monster& corpse) const -> bool {
        using Start = d2d::rules::ServerStartFunction;
        if (start != Start::kRaiseSkeleton && start != Start::kCorpseBlast && start != Start::kRevive) return true;
        const auto& type_info = game_data->monsters.types[std::size_t(corpse.type)];
        const int flags = d2d::rules::monster_data_flags(corpse.boss);
        const d2d::rules::CorpseState state{ .mode = 0xc, .flags = flags, .targetable = false, .dying = false, .hidden = false,
                                             .uninterruptable = corpse.uninterruptable(), .attached = corpse.riding,
                                             .town = level && d2d::rules::level_ids::is_town(level->id) };
        return start == Start::kRaiseSkeleton ? d2d::rules::raise_takes(type_info, state)
             : start == Start::kCorpseBlast   ? d2d::rules::blast_takes(type_info, state)
                                              : d2d::rules::revive_takes(type_info, state);
    }

auto Fight::corpse_near(float x, float y, d2d::rules::ServerStartFunction start) const -> int {
        int best = -1; float best_distance = 2.f;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (const auto& corpse = monsters[i]; !corpse.alive() && !corpse.corpse_used && !corpse.shatter && corpse.mode == "DD" && corpse_takes(start, corpse))
                if (const float distance = std::hypot(corpse.unit.x - x, corpse.unit.y - y); distance < best_distance) { best_distance = distance; best = int(i); }
        return best;
    }

// Iron Golem's item (FUN_005c32a0, start 20 and do 57's check): a ground
// item (type 4, mode 3) of a metal base (FUN_00629cc0: Items bitfield1 & 2),
// identified (flag 0x10), at the cast's subtile, else the nearest within a
// cell: its index in loot.ground, or -1.
auto Fight::golem_item_at(float x, float y) const -> int {
        if (loot.ground_level != level) return -1;
        const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
        int best = -1;
        float best_distance = 1.f;
        for (std::size_t i = 0; i < loot.ground.size(); ++i) {
            const auto& lying = loot.ground[i];
            const auto base = game_data->rules.item_base.find(lying.item.code);
            // A ground item, held by none (rules::iron_golem_takes, FUN_005c32a0).
            if (base == game_data->rules.item_base.end() || !d2d::rules::iron_golem_takes(4, 3, base->second.bitfield1, lying.item.identified ? 0x10 : 0, false)) continue;
            if (subtile(lying.x) == subtile(x) && subtile(lying.y) == subtile(y)) return int(i);
            if (const float distance = std::hypot(lying.x - x, lying.y - y); distance < best_distance) { best_distance = distance; best = int(i); }
        }
        return best;
    }

// The pet arrives (FUN_0056d940, FUN_005c49e0, FUN_005c4470): spawned
// as its MonStats row at the row's level, then set to its level
// (rules::summon_level: Raven's and the Druid's forced to max(calc2,
// 1), else min(max(clvl x 3 / 4 + the skill level, 1), clvl) for Raise,
// the golems, the Decoy, the Valkyrie and the traps), with MonLvl's
// defense and attack rating at it added (stats 31 / 19), life + calc1
// %. Past petmax of its pettype the oldest goes. Iron Golem wears the
// metal item it's cast at (golem_item_at). A skill by its name as the
// tables write it (Fire Golem's sumskill says 'holy fire').

// The skill's passive stats, aurastats and sumskills land on it as
// summons.cpp has them (FUN_005c4470; the masteries come in that way),
// and its sumumod (0x5c4934: a trap's 32, a Shadow's 42). Blood Golem's
// life steal is pet_strike's (rules::blood_golem_steal); its damaged
// events (func 26, FUN_005c6690) pass Param5 % of a hit on it to the
// owner, the skill being its MonStats +0x170 one (BloodGolem): 0 in
// 1.14d's Skills.txt, so nothing.
auto Fight::skill_named(std::string_view name) const -> const d2d::rules::Skill* {
        if (name.empty()) return nullptr;
        if (const auto found = game_data->skills.by_name.find(std::string(name)); found != game_data->skills.by_name.end()) return game_data->skills.get(found->second);
        for (const auto& [skill_name, id] : game_data->skills.by_name)
            if (std::ranges::equal(skill_name, name, [](char left, char right) { return std::tolower(std::uint8_t(left)) == std::tolower(std::uint8_t(right)); }))
                return game_data->skills.get(id);
        return nullptr;
    }

// The missile row a pet's skill shoots: its srvmissile, else
// srvmissilea — NecromageMissile's do 149 (FUN_005ce0b0) adds the mage's
// variant (+0xf of its monster data) to it: necromage1..4, rolled at
// the raise (FUN_005c4180: the caster's seed & 3).
// d2d's rng(4) is that step on Fight::rng (summon_one): deviations.md
// approximation 18.
auto Fight::pet_missile(const d2d::rules::Skill& skill, int variant ) const -> const GameData::MissileInfo* {
        std::string name = skill.srvmissile.empty() ? skill.srvmissilea : skill.srvmissile;
        if (skill.srvdofunc == ServerDoFunction::kVariantMissile && !name.empty() && name.back() == '1') name.back() = char('1' + std::clamp(variant, 0, 3));
        const auto found = game_data->missiles.find(name);
        return name.empty() || found == game_data->missiles.end() ? nullptr : &found->second;
    }

auto Fight::summon(const d2d::rules::Skill& skill, int lvl, float x, float y, std::uint32_t now_ms) -> void {
        int type = game_data->monsters.row(skill.summon);
        const auto env = calc_env();
        int corpse = -1;
        if (skill.target_corpse) {
            corpse = corpse_near(x, y, skill.srvstfunc);
            if (corpse < 0) return;
            monsters[std::size_t(corpse)].corpse_used = true;
            x = monsters[std::size_t(corpse)].unit.x; y = monsters[std::size_t(corpse)].unit.y;
            if (skill.srvdofunc == ServerDoFunction::kRevive) type = monsters[std::size_t(corpse)].type;
        }
        if (type < 0) return;
        // Iron Golem (do 57, FUN_005c5250): its check again (FUN_005c32a0), then the
        // golem, the item off the floor (FUN_0061a270) and on it (FUN_005606b0 at its
        // type's body location), then its stats (FUN_005c50c0): summon_one.
        if (skill.srvdofunc == ServerDoFunction::kIronGolem) {
            const int taken = golem_item_at(x, y);
            if (taken < 0) return;
            golem_item = loot.ground[std::size_t(taken)].item;
            x = loot.ground[std::size_t(taken)].x; y = loot.ground[std::size_t(taken)].y;   // on the item: FUN_0045adf0 / FUN_0045ae20 of it (0x5c535e..0x5c536f)
            loot.ground.erase(loot.ground.begin() + taken);
        }
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
                made.aim_at = std::pair{ aim_x, aim_y };
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
        summon_one(skill, type, lvl, env, x, y, now_ms, corpse);
    }

// Makes one pet of monster row `type` for `skill` at a spot: its AI control
// starts as the AI's init does (the traced inits in the body), returns its id.
auto Fight::summon_one(const d2d::rules::Skill& skill, int type, int lvl, const d2d::rules::CalcEnv& env, float x, float y, std::uint32_t now_ms, int corpse) -> int {
        const int max = std::max(d2d::rules::eval_calc(game_data->skills, skill.petmax, env, skill.id, lvl), 1);
        auto same = [&](const Pet& pet) { return game_data->skills.get(pet.skill) && game_data->skills.get(pet.skill)->pettype == skill.pettype; };
        while (skill.pettype != "none" && std::ranges::count_if(actor->pets, same) >= max) actor->pets.erase(std::ranges::find_if(actor->pets, same));
        const int clvl = int(actor->character.stats.get(d2d::d2s::kLevel));
        // FUN_005c49e0 (the level, then MonLvl's defense and attack rating added): Raise (0x5c4c5e), the golems
        // (FUN_005c50c0), the Decoy (0x5dc149), the Valkyrie (0x5dc2c9) and the traps (FUN_005d5e10 0x5d5fc1) pass
        // the skill level as the bonus; the Raven (0x5c6a29) and the Druid's summons (0x5c74c9) force max(calc2, 1).
        // The summons whose do doesn't run it (Hydra's FUN_005ca910, Vines' FUN_005c6a80) keep the spawn's: their
        // MonStats level, defense and to-hit (noRatio rows: as written, FUN_00573cb0).
        const bool forced_calc2 = skill.srvdofunc == ServerDoFunction::kRaven || skill.srvdofunc == ServerDoFunction::kDruidSummon;
        const bool levelled = forced_calc2 || skill.srvdofunc == ServerDoFunction::kRaiseSkeleton || skill.srvdofunc == ServerDoFunction::kGolem
                              || skill.srvdofunc == ServerDoFunction::kIronGolem || skill.srvdofunc == ServerDoFunction::kDecoy || skill.srvdofunc == ServerDoFunction::kValkyrie
                              || skill.srvdofunc == ServerDoFunction::kTrap;
        const int forced = forced_calc2 ? std::max(d2d::rules::eval_calc(game_data->skills, skill.calc[1], env, skill.id, lvl), 1) : 0;
        const int plvl = d2d::rules::summon_level(clvl, forced, levelled ? lvl : 0);
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
        // Revive (do 58, FUN_005c56c0) raises the corpse itself (FUN_00553540: the unit cast at), so it keeps
        // what it was made with (its level, damage, defence, to-hit, MonProp stats) and its own unit seed;
        // the rest are spawned at their MonStats Level (FUN_005b2f20 -> FUN_005b2a00; the stat init FUN_00573cb0).
        const bool raised = skill.srvdofunc == ServerDoFunction::kRevive && corpse >= 0;
        if (raised) {
            const auto& body = monsters[std::size_t(corpse)];
            monster.stats = body.stats;
            monster.props = body.props;
            monster.seed = body.seed;
            monster.sighted = body.sighted;                       // the corpse's own AI control: its flag 8 stays (the unit is raised, not made)
        } else {
            // A made one's unit seed (+0x20): {a game seed step, 666} (FUN_00555230 -> FUN_00552df0 ->
            // FUN_00650e40), as every unit but a player's gets as it's made (drops.md "the unit seed").
            monster.seed = d2d::rules::Rng{ spawning.game.next() };
            monster.stats = d2d::rules::monster_stats(game_data->monsters, type, monster.difficulty, rng);
        }
        // Its life and damage straight from MonStats' columns (a Raise
        // Skeleton skeleton: 21 life, 1-2 damage in Normal; 30 / 42 in
        // Nightmare / Hell), not scaled by MonLvl: every player summon's row
        // is noRatio (+0xc & 4), which the stat init (FUN_00573cb0 ->
        // FUN_006538a0) and the per-mode damage (FUN_005a4f50) take as
        // written, and Align 1 (row +0x4c) skips the players' life bonus
        // (FUN_00573930).
        // The life roll is min + rand(max - min + 1) on the pet's unit seed
        // (+0x20, 0x573f83..0x573f8f), here rng.range on Fight::rng:
        // deviations.md approximation 18.
        // Revive (do 58, FUN_005c56c0) raises the corpse itself: a life rolled
        // at the corpse's level and, above the owner's, MulDiv'd down with
        // the level to the owner's (rules::revive_life).
        // The roll is on the corpse's own unit seed (FUN_0045c3e0 on +0x20), which the pet keeps.
        const bool revive = skill.srvdofunc == ServerDoFunction::kRevive;
        if (revive) monster.align = 2;                    // FUN_005c55c0: FUN_005543b0(pet, 2), Conversion's alignment
        const int corpse_level = monster.stats.level;
        const auto revived = revive ? d2d::rules::revive_life(game_data->monsters, type, monster.difficulty, corpse_level, clvl, raised ? monster.seed : rng) : d2d::rules::RevivedLife{};
        if (!revive) {
            const auto& per_difficulty = game_data->monsters.types[std::size_t(type)].diff[std::size_t(monster.difficulty)];
            monster.stats.hit_points = std::max(rng.range(per_difficulty.min_hp, std::max(per_difficulty.max_hp, per_difficulty.min_hp)), 1);
            monster.stats.a1_min = per_difficulty.a1_min; monster.stats.a1_max = std::max(per_difficulty.a1_max, per_difficulty.a1_min);
            monster.stats.a2_min = per_difficulty.a2_min; monster.stats.a2_max = std::max(per_difficulty.a2_max, per_difficulty.a2_min);
        }
        // The Druid's wolves, Grizzly and Raven: MonStats SkillDamage (+0xa8)
        // names their summon skill, and each swing's per-mode damage
        // (FUN_005a4f50 -> FUN_0056e050, a monster with an owner ->
        // FUN_0056e000) adds that skill's physical (FUN_00647bc0 /
        // FUN_00647d00 >> 8) to A1's, A2's and S1's and its to-hit
        // (FUN_006449f0) to theirs, at the owner's level in it
        // (FUN_00644aa0). Their rows have no damage of their own.
        // SkillDamage (MonStats col 149) isn't loaded: in 1.14d only druidhawk,
        // spiritwolf, fenris and druidbear have one, each its summoning skill
        // (Raven, Summon Spirit Wolf / Fenris / Grizzly), and they are the
        // summoned rows with no A1 damage (all three difficulties) whose skill
        // has MaxDam (bladecreeper, Blade Sentinel's, isn't summoned here):
        // that test picks the same rows. Here at the cast's level; each frame
        // after, pet_skill_damage moves it to the owner's level then.
        int summon_skill_to_hit = 0;
        if (monster.stats.a1_max == 0 && skill.maxdam > 0) {
            const int low = d2d::rules::skill_phys(game_data->skills, skill, env, lvl, false) >> 8;
            const int high = std::max(d2d::rules::skill_phys(game_data->skills, skill, env, lvl, true) >> 8, low);
            const std::array<std::pair<int*, int*>, 3> modes{ { { &monster.stats.a1_min, &monster.stats.a1_max }, { &monster.stats.a2_min, &monster.stats.a2_max },
                                                                 { &monster.stats.s1_min, &monster.stats.s1_max } } };
            for (const auto& [mode_min, mode_max] : modes) {
                *mode_min += low;
                *mode_max += high;
            }
            summon_skill_to_hit = d2d::rules::skill_tohit(game_data->skills, skill, env, lvl);
            pet.skill_damage = true;
            pet.skill_low = low; pet.skill_high = high; pet.skill_to_hit = summon_skill_to_hit;
        }
        if (revive) monster.stats.level = revived.level;
        else if (skill.srvdofunc == ServerDoFunction::kMirrorImage) monster.stats.level = clvl;
        else if (levelled) monster.stats.level = plvl;
        if (levelled && !game_data->monsters.lvl.empty()) {
            const auto& level_row = game_data->monsters.lvl[std::min<std::size_t>(std::size_t(plvl), game_data->monsters.lvl.size() - 1)];
            // It adds the row's defense to the pet's own (MonStats AC, 0x5c4ac6) and the row's attack rating to
            // stat 0x13 (0x5c4adf). A swing (FUN_005a4f50) puts its mode's A1TH / A2TH / S1TH, as written (noRatio,
            // FUN_006538a0), on the unit's swing list (FUN_00625760 list 1, 0x5a5190), so a hit's 0x13 is both.
            monster.stats.armor_class += level_row.armor_class[std::size_t(monster.difficulty)];
            for (int* mode_to_hit : { &monster.stats.to_hit, &monster.stats.to_hit2, &monster.stats.to_hit_s1 }) *mode_to_hit += level_row.to_hit[std::size_t(monster.difficulty)];
        }
        for (int* mode_to_hit : { &monster.stats.to_hit, &monster.stats.to_hit2, &monster.stats.to_hit_s1 }) *mode_to_hit += summon_skill_to_hit;
        // FUN_005c4470: the skill's passive stats (base) and aurastats (its
        // aurastate) onto the pet, at the skill's level through the owner's
        // calcs — Skeleton Mastery, Golem Mastery and the golems' synergies
        // come in that way — then life (with maxhp) + calc1 %, then its
        // sumskills at their sumsk calcs.
        std::unordered_map<int, int> pet_stats;
        const auto summoned = d2d::rules::summon_stats(game_data->skills, skill, env, lvl);
        for (const auto* stats : { &summoned.passive, &summoned.aura })
            for (const auto& [stat_id, value] : *stats) pet_stats[stat_id] += value;
        // Last in FUN_005c4470 (0x5c49c7), FUN_005d6b60 at the skill's level:
        // the Valkyrie's MonEquip gear, the rows from her level down, made at
        // calc2 or min(max(3 x level, 1), the owner's level). Its stats join
        // hers; a two-handed weapon's damage replaces her A1 (FUN_0057b420).
        // The walk is on her unit seed, each item made as a drop (two game seed
        // steps), as a spawn's gear (objgroups.cpp, FUN_00573cb0's end).
        // ponytail: her seed's earlier draws (FUN_00573cb0's stat rolls, on the
        // fight's rng here: approximation 18) aren't taken before the walk; a
        // Shadow's gear (the owner's items) isn't made, it fights with the
        // owner's (pet.mirror). Its stats go in before the life below, so a
        // gear maxhp would get calc1 % (game.exe makes it after).
        if (const int first = std::size_t(type) < game_data->mon_equip_first.size() ? game_data->mon_equip_first[std::size_t(type)] : -1;
            first >= 0 && skill.srvdofunc != ServerDoFunction::kMirrorImage) {
            std::uint32_t worn = 0;
            for (const auto& item : actor->character.items)
                if (item.location == d2d::d2s::item_location::kEquipped && item.slot > 0 && item.slot < 11) worn |= 1u << item.slot;
            const int ilvl = d2d::rules::summon_gear_level(calc(skill, skill.calc[1], lvl), lvl, clvl);
            std::vector<d2d::d2s::Item> items;
            for (const auto& gear : d2d::rules::monster_gear(game_data->mon_equip, first, type, lvl, false, 0, worn, monster.seed)) {
                const auto& code = game_data->mon_equip[gear.row].item[gear.slot];
                const std::string base = code.substr(0, code.find_last_not_of(' ') + 1);
                if (gear.owners || !game_data->rules.item_info.contains(base)) continue;
                d2d::rules::Rng unit_seed{ spawning.game.next() }, item_seed{ spawning.game.next() };
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
        // Iron Golem's item (FUN_005606b0, before FUN_005c50c0 makes its stats):
        // worn in its type's first body location, its stats the golem's, so
        // its absorbs count in Summon Resist below and its maxhp gets calc1 %.
        if (skill.srvdofunc == ServerDoFunction::kIronGolem && golem_item) {
            auto item = *std::exchange(golem_item, std::nullopt);
            const auto info = game_data->rules.item_info.find(item.code);
            const auto item_type = info == game_data->rules.item_info.end() ? game_data->rules.types.end() : game_data->rules.types.find(info->second.type);
            item.location = d2d::d2s::item_location::kEquipped;
            item.slot = item_type == game_data->rules.types.end() ? 0 : item_type->second.body[0];
            // Its sockets' stats go with it, as a worn item's do (socket_props, panel_stats).
            for (const auto& socketed : item.socketed_items)
                for (const auto& prop : socket_props(*game_data, item, socketed)) item.props.push_back(prop);
            const auto worn_gear = d2d::rules::gear_sum(game_data->rules, std::span(&item, 1));
            for (std::size_t stat_id = 0; stat_id < worn_gear.sum.size(); ++stat_id)
                if (worn_gear.sum[stat_id] != 0) pet_stats[int(stat_id)] += int(stat_id == d2d::d2s::kMaxLife ? worn_gear.sum[stat_id] << 8 : worn_gear.sum[stat_id]);
            std::tie(monster.stats.a1_min, monster.stats.a1_max) = d2d::rules::gear_damage(worn_gear, monster.stats.a1_min, monster.stats.a1_max);
            monster.stats.armor_class += int(worn_gear.defense);
        }
        auto stat = [&](int id) { const auto found = pet_stats.find(id); return found == pet_stats.end() ? 0 : found->second; };
        using namespace d2d::d2s;
        // Summon Resist (FUN_005c40d0): a raised skeleton or mage and a golem, after its stats.
        if (skill.srvdofunc == ServerDoFunction::kRaiseSkeleton || skill.srvdofunc == ServerDoFunction::kGolem || skill.srvdofunc == ServerDoFunction::kIronGolem) {
            int owner_value = 0;
            for (const auto& passive : d2d::rules::passive_stats(game_data->skills, env))
                if (passive.stat == d2d::rules::kPassiveSummonResist) owner_value += passive.value;
            for (const auto& [stat_id, value] : d2d::rules::summon_resist(owner_value, stat(kAbsorbFirePercent), stat(kAbsorbLightningPercent), stat(kAbsorbColdPercent)))
                pet_stats[stat_id] += value;
        }
        // The Decoy's do (FUN_005dc000 0x5dc0ec..0x5dc12f) sets its life first: the owner's max life x calc3 / 100
        // (FUN_00483360), 256ths; calc1 % then goes on that, as on Revive's (above).
        const int base_fine = skill.srvdofunc == ServerDoFunction::kDecoy
                                  ? d2d::rules::mul_div(int(actor->character.stats.values[kMaxLife]), calc(skill, skill.calc[2], lvl), 100)
                              : revive ? revived.life_fine
                                       : monster.stats.hit_points << 8;
        // A life that wraps to 0 or below (bugs.md 123) is set as it is (stats 7 and 6, 0x5c4816 / 0x5c4821);
        // the dead test is the mode (FUN_005541b0), so the pet lives and thinks till a landed hit, which
        // kills it (life <= 0 after it, 0x57c8e2): a zero-life monster's `lifeless` (make_monster).
        // The max life it reads is the unit's whole (FUN_00625d10): the items' maxhp and their
        // item_maxhp_percent (76, op 11) of the base, as a merc's and a made monster's (gear_life_fine).
        int life_fine = d2d::rules::summon_life(base_fine + stat(kMaxLife) + d2d::rules::mul_div(base_fine, stat(kMaxLifePercent), 100), calc(skill, skill.calc[0], lvl));
        monster.lifeless = life_fine <= 0;
        if (life_fine <= 0) life_fine = 256;
        monster.stats.hit_points = int((std::int64_t(life_fine) + 255) >> 8);
        monster.max_life_frac = monster.stats.hit_points * 256 - life_fine;
        monster.hit_points = monster.stats.hit_points;
        monster.life_frac = monster.max_life_frac;
        for (int* damage : { &monster.stats.a1_min, &monster.stats.a1_max }) *damage += stat(kNormalDamage);                          // item_normaldamage
        pet.a1_unscaled = { monster.stats.a1_min, monster.stats.a1_max };
        pet.damage_pct = stat(kDamagePercent);
        for (int* damage : { &monster.stats.a1_min, &monster.stats.a1_max }) *damage += *damage * pet.damage_pct / 100;              // damagepercent
        monster.stats.to_hit += stat(kToHit);                                                                                       // tohit
        pet.to_hit_unscaled = monster.stats.to_hit;
        pet.to_hit_pct = stat(kToHitPercent);
        monster.stats.to_hit += monster.stats.to_hit * pet.to_hit_pct / 100;                                                        // item_tohit_percent
        monster.stats.armor_class += stat(kArmorClass); monster.stats.armor_class += monster.stats.armor_class * (stat(kArmorPercent) + stat(kSkillArmorPercent)) / 100;   // armorclass, item / skill_armor_percent
        const auto& base_res = type_info.diff[std::size_t(monster.difficulty)].res;
        pet.res = { base_res[2] + stat(kFireResist), base_res[3] + stat(kLightningResist), base_res[4] + stat(kColdResist), base_res[5] + stat(kPoisonResist) };
        pet.thorns = stat(kThornsPercent); pet.fire_lo = stat(kFireMinDamage); pet.fire_hi = std::max(stat(kFireMaxDamage), pet.fire_lo); pet.speed_pct = stat(kVelocityPercent);
        pet.slow = stat(kItemSlow);
        for (const auto& [given_id, given_level] : d2d::rules::summon_skill_levels(game_data->skills, skill, env, lvl)) {
            const auto& given = *game_data->skills.get(given_id);
            if (given.aura) { pet.aura = given.id; pet.aura_level = given_level; }
            else if (pet_missile(given) && skill.srvdofunc != ServerDoFunction::kVine && skill.srvdofunc != ServerDoFunction::kTrap) { pet.ranged = given.id; pet.ranged_level = given_level; }
        }
        monster.unit.x = monster.home_x = x; monster.unit.y = monster.home_y = y;
        face(monster.unit, actor->player.dir);
        pet.skill = skill.id;
        pet.level = lvl;
        pet.where = level;
        pet.variant = int(rng(4));
        switch (skill.srvdofunc) {
            case ServerDoFunction::kRevive: pet.until = now_ms + std::uint32_t(std::max(calc(skill, skill.calc[1], lvl), 1)) * 40; break;   // Revive: calc2 frames
            case ServerDoFunction::kHydra:                                                                                   // Hydra: Param1 frames, it stands
                pet.until = now_ms + std::uint32_t(std::max(skill.par[0], 1)) * 40;
                if (const auto* hydra = skill_named("HydraMissile")) { pet.ranged = hydra->id; pet.ranged_level = lvl; }
                break;
            case ServerDoFunction::kDecoy:                                  // Decoy (FUN_005dc000): its life above, calc2 frames, it stands
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
        // The Blade Sentinel's init (FUN_005ea510, MonAI 102): -1, 1, 0.
        else if (ai_name == "BladeCreeper") pet.ctrl = d2d::rules::blade_sentinel_init();
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
