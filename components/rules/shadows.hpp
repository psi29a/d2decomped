// SPDX-License-Identifier: GPL-3.0-or-later
// The Shadows (MonAI 105 ShadowWarrior / 106 ShadowMaster): the skills a
// Shadow Warrior is given (FUN_005eb490), the Shadow Master's init
// (FUN_005ecb70) and think (FUN_005eb970) with its unit scan (FUN_005eb6d0).
// tools/emu/shadow_init.py and shadow_master.py check them against game.exe;
// docs/research/re/pet-ai.md "The Shadows". The Warrior's think is
// shadow_warrior_think in pets.hpp.
#pragma once

#include "rules.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace d2d::rules {

// FUN_005eab20: may a Shadow have this skill? One that summons nothing, or
// neither this monster (MonStats row `pet_class`) nor this pet's pettype
// (`pet_types` rows in PetType.txt; Skills.txt summon / pettype).
inline bool shadow_may_have(int summon, int pet_type, int pet_class, int own_pet_type, int pet_types) {
    if (summon == 0) return true;
    if (summon == (pet_class & 0xffff)) return false;
    if (pet_type >= 0 && pet_type < pet_types) return own_pet_type != pet_type;
    return true;
}

// FUN_005eb420: a skill's level on the Shadow, the owner's hard points (none
// counts 1) / 2 + its level in the summoning skill / 2, 1..24.
inline int shadow_skill_level(int hard_points, int summoned_level) { return std::clamp(hard_points / 2 + summoned_level / 2, 1, 0x18); }

// FUN_005eaf00: the level the Warrior gives itself the owner's left / right
// skill at each think: +0x1c / 3 + the owner's level in it (bonuses on) / 2.
inline int shadow_hand_level(int summoned_level, int level) { return std::max(summoned_level / 3 + level / 2, 1); }

// FUN_005eabf0: may the Warrior's AI use a skill it may have? Aitype 4 / 13
// only in melee (the driver's [6]), the rest only out of it; not an aitype 1
// buff whose aurastate it's in; with no target (the driver's [2]) not
// aitype 2, 4, 5, 11, 12 or 13; aitype 2 not in its aurastate nor at a
// target in its auratargetstate; with Skills.txt flags bit 2 and its
// aurastate on, not once that state's aurastat1 (aura_value) is 3.
inline bool shadow_ai_may_use(bool may_have, int aitype, bool melee, bool target, bool in_state, bool target_in_state2, bool aura,
                              std::optional<int> aura_value) {
    if (!may_have || melee != (aitype == 4 || aitype == 13)) return false;
    if (aitype == 1 && in_state) return false;
    if (!target && (aitype == 2 || aitype == 4 || aitype == 5 || aitype == 11 || aitype == 12 || aitype == 13)) return false;
    if (aitype == 2 && (in_state || target_in_state2)) return false;
    return !(aura && in_state && aura_value && *aura_value >= 3);
}

struct ShadowGift {
    int skill = 0, level = 0;
};

// FUN_005eb490 (ShadowWarrior's init): AI control 0, 0, 1; with a player
// owner +0x1c its level in the summoning skill (-1: it hasn't the skill,
// +0x1c stays 1), Attack (given at 1 if missing) on both hands, then each of
// the owner's class's skills it may have (may_have(id)) at
// shadow_skill_level (hard_points(id), -1 none). What it's given, in order.
template <class MayHave, class HardPoints>
std::vector<ShadowGift> shadow_warrior_init(std::array<int, 3>& ctrl, bool player_owner, int summoned_level, bool has_attack,
                                            std::span<const int> class_skills, MayHave&& may_have, HardPoints&& hard_points) {
    ctrl = { 0, 0, 1 };
    std::vector<ShadowGift> gifts;
    if (!player_owner) return gifts;
    if (summoned_level >= 0) ctrl[2] = summoned_level;
    if (!has_attack) gifts.push_back({ 0, 1 });
    for (const int id : class_skills) {
        if (!may_have(id)) continue;
        const int hard = hard_points(id);
        gifts.push_back({ id, shadow_skill_level(hard < 0 ? 1 : hard, ctrl[2]) });
    }
    return gifts;
}

// FUN_005ecb70 (ShadowMaster's init): +0x14 -1, +0x18 aip3 + 1, +0x1c a seed
// step's low bit.
inline std::array<int, 3> shadow_master_init(int aip3, Rng& seed) { return { -1, aip3 + 1, int(seed.next() & 1) }; }

// A unit as the Shadow Master sees it (subtiles). `target`: its own target
// (FUN_00553540), `owner` (FUN_0058f0d0), both unit indexes or -1. `foe`:
// FUN_00554200 from the pet; `melee` FUN_00622c40 from the pet; `worth`
// FUN_005eb650 (shadow_worth); `side` FUN_00650d70 (the pet's side),
// `monster_id` its MonStats id (FUN_00463860). `resist` by EType (none =
// damage, fire, light, magic, cold, poison); `monster_level` MonStats
// +0xa0 by difficulty.
struct ShadowUnit {
    int type = 1, x = 0, y = 0;
    bool targetable = true, dying = false, foe = true, melee = false, worth = false, side = false;
    int target = -1, owner = -1, monster_level = 0, monster_id = -1;
    std::vector<int> states;
    std::array<int, 6> resist{};
    [[nodiscard]] bool in(int state) const { return std::ranges::find(states, state) != states.end(); }
};

// FUN_005eb650: worth chasing — a monster, not dying, MonStats not npc but
// killable, and boss or primeevil or a champion / unique / minion (monster
// data +0x16 & 0xe).
inline bool shadow_worth(bool monster, bool dying, bool npc, bool killable, bool boss, bool prime_evil, bool special) {
    return monster && !dying && !npc && killable && (boss || prime_evil || special);
}

// What FUN_005eb6d0 gathers over the units round the pet (squared subtile
// distances): the closest foe within 32 (and its distance), how many within
// 10, how many within 32; the closest to the owner and how many within 10
// of it; the pet's side's traps; the last foe within 32 worth chasing.
struct ShadowScan {
    int closest = -1, closest_distance = INT_MAX, close_count = 0;
    int owner_closest = -1, owner_closest_distance = INT_MAX, owner_close_count = 0;
    int all = 0, traps = 0, worth = -1;
};

inline int shadow_distance(const ShadowUnit& from, const ShadowUnit& unit) {   // FUN_005b0bd0
    return (from.x - unit.x) * (from.x - unit.x) + (from.y - unit.y) * (from.y - unit.y);
}

// FUN_005eb6d0 on each unit but the pet (and the dying): the traps first
// (MonStats 0x19a..0x1a0 but 0x19e on its side), then targetable foes.
inline ShadowScan shadow_scan(std::span<const ShadowUnit> units, int pet, int owner) {
    ShadowScan scan;
    for (int index = 0; index < int(units.size()); ++index) {
        const auto& unit = units[std::size_t(index)];
        if (index == pet || unit.dying) continue;
        if (unit.type == 1 && unit.side && unit.monster_id >= 0x19a && unit.monster_id <= 0x1a0 && unit.monster_id != 0x19e) { ++scan.traps; continue; }
        if (!unit.targetable || !unit.foe) continue;
        if (owner >= 0) {
            const int apart = shadow_distance(units[std::size_t(owner)], unit);
            if (apart <= 0x64) ++scan.owner_close_count;
            if (apart < scan.owner_closest_distance) { scan.owner_closest = index; scan.owner_closest_distance = apart; }
        }
        const int apart = shadow_distance(units[std::size_t(pet)], unit);
        if (apart > 0x400) continue;
        ++scan.all;
        if (apart <= 0x64) ++scan.close_count;
        if (apart < scan.closest_distance) { scan.closest = index; scan.closest_distance = apart; }
        if (unit.worth) scan.worth = index;
    }
    return scan;
}

// A skill the Shadow Master has (its list at +0xa8): id, level (bonuses on),
// kind (FUN_00645460: 1 walks to its target out of melee), mode.
struct ShadowListed {
    int id = 0, level = 0, kind = 0, mode = 0;
};

// Its Skills.txt row: aitype, aibonus, reqlevel, EType, aurastate,
// auratargetstate, flags bit 2 (`aura`), srvmissile, srvmissilea and that
// missile's Range (-1: no row), srvdofunc 19 (`repeat`).
struct ShadowSkillRow {
    int aitype = 0, bonus = 0, reqlevel = 0, etype = 0, state = 0, state2 = 0;
    bool aura = false;
    int srvmissile = -1, missile = -1, missile_range = -1;
    bool repeat = false;
};

// What the think reads. units[0] is the pet. `rows` by skill id (absent:
// past Skills.txt). `groups`: States.txt group by state. `fixed`: MonStats
// +0x56..+0x60 (aip1 N / NM / H, aip2 N / NM / H), `aip3` by difficulty.
// `life` %; `left` it has a left skill (FUN_00620190); `low`
// FUN_0063a2b0; `blocked` FUN_00622aa0 mask 4; `aura_value` its aurastat
// (FUN_006256b0 / FUN_00625d00; none: no list); `town` the pet's room.
struct ShadowMasterScene {
    std::vector<ShadowUnit> units;
    int owner = -1, driver = -1, driver_distance = 0;
    bool driver_melee = false, has_list = true;
    std::vector<ShadowListed> skills;
    std::map<int, ShadowSkillRow> rows;
    std::vector<int> groups;
    std::array<int, 6> fixed{};
    int aip3 = 0;
    ShadowScan scan;
    int life = 100;
    bool left = false, low = false, blocked = false, town = false;
    std::optional<int> aura_value;
};

// FUN_005eb970 (ShadowMaster). `world`: decide(foe, melee) (FUN_005e45d0,
// reach 6), skill(mode, id, target) (FUN_005dead0), approach(target)
// (FUN_005ded00, 4), away(x, y) (FUN_005deb60 path type 0xf, 1 off) — true
// when they took — and run(unit), set_left(id), stand(frames).
template <class World>
void shadow_master_think(const ShadowMasterScene& scene, Rng& seed, std::array<int, 3>& ctrl, World& world) {
    const auto& units = scene.units;
    const auto& fixed = scene.fixed;
    auto apart = [&](int from, int unit) { return shadow_distance(units[std::size_t(from)], units[std::size_t(unit)]); };
    auto listed = [&](int id) -> const ShadowListed* {
        const auto found = std::ranges::find(scene.skills, id, &ShadowListed::id);
        return found == scene.skills.end() ? nullptr : &*found;
    };
    auto row_of = [&](int id) -> const ShadowSkillRow* {
        const auto found = scene.rows.find(id);
        return found == scene.rows.end() ? nullptr : &found->second;
    };
    auto group_mate = [&](int state) {                                   // FUN_005eb7f0
        if (state < 0 || state >= int(scene.groups.size()) || scene.groups[std::size_t(state)] == 0) return false;
        const int group = scene.groups[std::size_t(state)];
        for (int other = 0; other < int(scene.groups.size()); ++other)
            if (other != state && scene.groups[std::size_t(other)] == group && units[0].in(other)) return true;
        return false;
    };
    auto cast = [&](int id, bool melee, int target) -> bool {             // FUN_005eb8b0
        if (target >= 0 && (target == 0 || target == scene.owner)) return false;
        const auto* skill = listed(id);
        if (!skill) return false;
        if (target >= 0 && (!units[std::size_t(target)].targetable || scene.town)) return false;
        if (skill->kind == 1 && !melee) return world.approach(target);
        return world.skill(skill->mode, id, target);
    };
    if (!scene.has_list) { world.stand(100); return; }
    const int owner = scene.owner;
    const bool melee = scene.driver_melee;
    int target = scene.driver;
    if (owner >= 0 && apart(0, owner) > fixed[5] * fixed[5] && world.decide(-1, melee)) return;
    if (ctrl[0] > 0) {
        if (units[0].target >= 0) target = units[0].target;
        if (target < 0) ctrl[0] = ctrl[1] = 0;
        else {
            --ctrl[0];
            if (cast(ctrl[1], melee, target)) return;
        }
    }
    if (fixed[4] < scene.driver_distance) target = -1;
    if (target < 0)
        for (const auto& skill : scene.skills) {
            const auto* row = row_of(skill.id);
            if (!row) continue;
            if (row->aitype == 1) {
                const int state = row->state;
                if (state <= 0 || units[0].in(state)) continue;
                if (group_mate(state) && seed(100) >= 4) continue;
                if (seed.next() % 100 >= 0x3c) continue;
                const auto* handle = listed(skill.id);                     // FUN_006439b0: its first entry
                if (handle->kind == 1 && !melee ? world.approach(-1) : world.skill(handle->mode, skill.id, -1)) return;
            } else if (row->aitype == 6 && !scene.left && seed.next() % 100 < 0x14)
                world.set_left(skill.id);
        }
    int helper = -1;
    if (owner >= 0) {
        const int theirs = units[std::size_t(owner)].target;
        if (theirs >= 0 && !units[std::size_t(theirs)].dying && units[std::size_t(theirs)].foe) target = helper = theirs;
        if (apart(0, owner) <= 0x90 && world.decide(target, melee)) return;
    }
    if (target < 0) { world.stand(0x19); return; }
    const int chance = std::clamp(scene.aip3 - 2 * std::max(ctrl[2], 1), 5, 100);
    if (melee && seed(100) < chance && cast(0, melee, target)) return;
    const auto& scan = scene.scan;
    if (!melee) {
        const int pick = helper >= 0 ? helper : scan.owner_closest >= 0 ? scan.owner_closest : scan.worth >= 0 && apart(0, scan.worth) < 0x400 ? scan.worth : -1;
        if (pick >= 0) target = pick;
        if (!units[std::size_t(target)].worth) {
            const int theirs = units[std::size_t(target)].owner;
            if (theirs >= 0 && !units[std::size_t(theirs)].dying && apart(0, theirs) < 0x400) target = theirs;
        }
    }
    const int life = scene.life, distance = apart(0, target);
    const bool low = fixed[2] > 0 && scene.low, clear = !scene.blocked;
    if (scan.close_count > 3 && seed(0x20) < 2 * scan.close_count) {
        if (owner >= 0 && apart(0, owner) > 0x24) { world.run(owner); return; }
        const auto& pet = units[0];
        const auto& foe = units[std::size_t(target)];
        const int side_x = pet.x < foe.x ? -1 : foe.x < pet.x ? 1 : 0, side_y = pet.y < foe.y ? -1 : foe.y < pet.y ? 1 : 0;
        if (world.away(pet.x + 8 * side_x, pet.y + 8 * side_y)) return;
    }
    struct Entry {
        int unit = -1, skill = 0, score = 0;
    };
    std::vector<Entry> entries{ { target, 0, 0 } };
    int aura_sum = 0;
    for (const auto& skill : scene.skills) {
        const auto* row = row_of(skill.id);
        if (!row) continue;
        const auto& foe = units[std::size_t(target)];
        const int etype = row->etype == 12 ? 4 : row->etype;
        const int resist = etype >= 0 && etype < int(foe.resist.size()) ? foe.resist[std::size_t(etype)] : 0;   // EType 12: cold
        int score = row->bonus + row->reqlevel / 4 + skill.level + -resist / 10;
        const int state = row->state;
        int who = target, got = 0;
        auto roll = [&] { return seed(fixed[3]); };
        auto aura_full = [&] {                                           // the running aurastat sum; 3+ drops the skill
            if (!row->aura || state <= 0 || !units[0].in(state) || !scene.aura_value) return false;
            aura_sum += *scene.aura_value;
            return *scene.aura_value >= 3;
        };
        const bool close_in = scan.closest_distance <= 0x19;
        switch (row->aitype) {
        case 1:
            if (state > 0 && !units[0].in(state)) continue;
            if (close_in) score -= 6;
            score += group_mate(state) ? -10 : 10;
            got = roll() + score;
            who = 0;
            break;
        case 2:
            if (state > 0 && units[0].in(state)) continue;
            if (row->state2 > 0 && foe.in(row->state2)) continue;
            if (close_in) score -= 10;
            got = roll() + score;
            break;
        case 3:
            if (scan.traps > 5) score -= 2 * scan.traps;
            if (close_in) score -= 7;
            if (scan.all < 3) score -= 10;
            got = scan.all * 3 - 9 + roll() + score;
            break;
        case 4:
        case 12:
            if (row->aitype == 12 && foe.type == 1 && foe.monster_level < 0x19) continue;
            if (distance > fixed[0] * fixed[0]) score -= 10;
            score += fixed[1];
            if (melee || distance <= 0x19) score += 10;
            if (row->aitype == 4) {
                if (row->aura) {
                    if (aura_full()) continue;
                    score += fixed[2];
                } else if (fixed[2] > 0 && !low) score -= 10;
                else score += aura_sum * 4 + 3;
                got = roll() + score;
            } else {
                if (aura_full()) continue;
                got = roll() + score;
                if (life < 0x4b) got += 8;
                if (life < 0x32) got += 0xc;
            }
            break;
        case 5:
        case 11:
            if (!clear) continue;
            if (row->srvmissile < 0 && row->missile >= 0 && row->missile_range >= 0 && distance >= (row->missile_range - 1) * (row->missile_range - 1)) continue;
            if (close_in) score -= 5;
            if (distance <= 0x19) score -= 5;
            if (low) score -= 5;
            got = roll() + score + (row->aitype == 11 ? scan.all * 3 : 0);
            break;
        case 6:
            if (seed(100) < (scene.left ? 6 : 0x14)) world.set_left(skill.id);
            continue;
        case 7: {
            const int draw = roll();
            if (life > 0x42) continue;
            who = -1;
            if (owner < 0) { seed(0x28); seed(0x28); }                    // the spot it works out, then drops (bugs.md #17)
            got = draw + score + (life < 0x2d ? 0x14 : 10);
            break;
        }
        case 8: {
            const int draw = roll();
            if (life > 0x42) continue;
            who = 0;
            got = (draw + score) * (life < 0x2d ? 4 : 2);
            break;
        }
        case 13:
            score += fixed[1];
            score += fixed[2] > 0 && !low ? -5 : aura_sum;
            if ((life < 0x32 || scan.close_count > 3) && scan.owner_closest >= 0 && scan.owner_close_count < 4 && apart(0, scan.owner_closest) > 0x19) {
                score += 0x14;
                who = scan.owner_closest;
            } else {
                if (distance < 0x19) continue;
                if (distance > 0x144) score += 10;
            }
            got = roll() + score;
            break;
        default:
            continue;
        }
        if (got > entries.back().score) entries.push_back({ who, skill.id, got });
    }
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
        if ((seed.next() & 3) == 0) continue;
        if (cast(entry->skill, units[std::size_t(entry->unit < 0 ? 0 : entry->unit)].melee, entry->unit)) {
            if (const auto* row = row_of(entry->skill); row && row->repeat) { ctrl[1] = entry->skill; ctrl[0] = 0x19; }
            return;
        }
    }
    if (cast(0, units[std::size_t(target)].melee, target)) return;
    world.stand(0xf);
}

}  // namespace d2d::rules
