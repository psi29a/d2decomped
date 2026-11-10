// SPDX-License-Identifier: GPL-3.0-or-later
// Act 4's think inputs (rules/thinks/act4.hpp reads them): the vile
// mother's young within 25 (its MonStats spawn's class) and a spot to lay
// one; the Regurgitator's corpse (a dead monster, DD, not raised) within
// aip6, its id the one it's after (state2); the Oblivion Knight's knight
// (class 0x136) within 50; Diablo's kept point and his chooser's inputs.
// The states its skills name on a unit (the knights' armor and curse, the
// finger mage's 0x54): armor since its cast, the others the foe's.
#include "ai.hpp"

#include "gamedata.hpp"
#include "internal.hpp"

#include <level_ids.hpp>
#include <monsters.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <ranges>
#include <span>
#include <utility>

namespace d2d::game {

namespace {

struct Act4Preps : ThinkPrep {
    // The nearest other unit `wanted` takes within the squared limit: its
    // index (-1 none) and squared distance.
    template <class Wanted>
    std::pair<int, int> nearest_kin(Wanted&& wanted, int limit_squared) const {
        int found = -1, found_squared = std::numeric_limits<int>::max();
        for (std::size_t i = 0; i < pack.size(); ++i) {
            const auto& other = pack[i];
            if (&other == &monster || !wanted(other)) continue;
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y, squared = off_x * off_x + off_y * off_y;
            if (squared <= limit_squared && squared < found_squared) { found = int(i); found_squared = squared; }
        }
        return std::pair{ found, found_squared };
    }
    void take(int found, int found_squared) {
        if (found < 0) return;
        const auto& other = pack[std::size_t(found)];
        input.kin_x = subtile(other.unit.x); input.kin_y = subtile(other.unit.y); input.kin_id = other.id; input.kin_dist = found_squared;
    }
    // The knight's curse (Skill4) and the finger mage's spider (Skill1):
    // their auratargetstate (skill +0x82) on the target (FUN_006256b0).
    void target_state(std::size_t slot) {
        if (const auto* own = game_data.skills.get(skill_id(game_data, type_info.skill[slot])))
            input.skill_state = !own->auratarget.empty() && std::ranges::contains(target->states, own->auratarget);
    }
    void vile_mother() {
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
    void regurgitator() {
        const auto [found, found_squared] = nearest_kin([](const Monster& other) { return other.usable_corpse(); }, input.aip[5] * input.aip[5]);
        corpse = found;
        input.corpse = found >= 0;
        take(found, found_squared);
        const auto after = std::ranges::find(pack, monster.ai_state2, &Monster::id);   // FUN_00552f60(state2)
        input.command_unit = after != pack.end() && !after->corpse_used;
        input.leader_mode = after != pack.end() && after->mode == "DD" ? 12 : 0;
        if (after != pack.end()) { input.kin_x = subtile(after->unit.x); input.kin_y = subtile(after->unit.y); }
    }
    void oblivion_knight() {
        const auto [found, found_squared] = nearest_kin([&](const Monster& other) { return other.alive() && other.type == 0x136; }, 2500);
        kin = found;
        input.kin = found >= 0;
        take(found, found_squared);
        target_state(3);
    }
    void abyss_knight() { input.skill_state = monster.armored; }
    void finger_mage() { target_state(0); }
    // UberIzual's teleport test: FUN_00622aa0 mask 6.
    void uber_izual() {
        const auto wall = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x06); };
        input.line_clear = !d2d::rules::sight_blocked(x, y, type_info.size, target_x, target_y, target->size, wall);
        input.skill_state = monster.self_state.skill >= 0 && now_ms < monster.self_state.until;
    }
    // Diablo's and Baal's (and his clone's): the chooser's inputs, the point
    // bonus past `point_far` (Baal's 0x4b, Diablo's 0x55).
    void chooser(int point_far) {
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
        const int target_room = room_of(level, target->x, target->y);
        input.usable = !d2d::rules::level_ids::is_town(level.id) && std::ranges::all_of(std::views::iota(0, 4), [&](int bone) {
            int at_x = 0, at_y = 0;
            return spawn_spot(game_data, level, crowd, rooms_in_play, 0x154 + bone, target_x + kPrison[std::size_t(bone)].first, target_y + kPrison[std::size_t(bone)].second,
                              -1, target_room, at_x, at_y); });
    }
    void diablo() { chooser(0x55); }
    void baal_crab() { chooser(0x4b); }
    void baal_crab_clone() {
        chooser(0x4b);
        baal_rider();
    }
};

constexpr auto kRows = std::to_array<PrepRow>({
    { "VileMother", &prep_as<Act4Preps, &Act4Preps::vile_mother> },
    { "FingerMage", &prep_as<Act4Preps, &Act4Preps::finger_mage> },
    { "Regurgitator", &prep_as<Act4Preps, &Act4Preps::regurgitator> },
    { "AbyssKnight", &prep_as<Act4Preps, &Act4Preps::abyss_knight> },
    { "OblivionKnight", &prep_as<Act4Preps, &Act4Preps::oblivion_knight> },
    { "Diablo", &prep_as<Act4Preps, &Act4Preps::diablo> },
    { "BaalCrab", &prep_as<Act4Preps, &Act4Preps::baal_crab> },
    { "BaalCrabClone", &prep_as<Act4Preps, &Act4Preps::baal_crab_clone> },
    { "UberIzual", &prep_as<Act4Preps, &Act4Preps::uber_izual> },
});

}  // namespace

const std::span<const PrepRow> kAct4Preps{ kRows };

}  // namespace d2d::game
