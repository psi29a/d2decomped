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

struct Act4Ai : ThinkCtx {
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
    // Diablo's (FUN_005e8f20, once his search found someone): the unit
    // lists' best by his score (FUN_005e8530), above 0, the first on a tie.
    // Lists 0..7: a player of his act under 0x3fc (FUN_005e8eb0: FUN_005dc530;
    // Baal's FUN_005fc000: the same act byte, under 0x3fc)
    // and the pets after it (a dead player scores 0); list 8, the good
    // monsters of his act. List 9's best (neutral: Confuse, Attract) is the
    // fallback: within 5 of him, with the best out of reach (FUN_00622c40)
    // and no path to it (type 2, FUN_00649970), it's taken. None: the
    // search's stays.
    // Each foe's pick: Fight::world's (a pet's or monster's, unit_pick).
    // Baal's and his clone's (FUN_005fbdd0, score FUN_005fbb00) are the same
    // with the point bonus at 0x4b.
    void chooser_pick(int point_far) {
        if (!target) return;
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
    void diablo_search() { chooser_pick(0x55); }
    void baal_search() { chooser_pick(0x4b); }
    // Its Bone Armor up (Skill2) as it casts it.
    bool abyss_knight_skill() {
        if (act.act == d2d::rules::MonAct::skill && act.n == 1) monster.armored = true;
        return false;
    }
};

constexpr auto kRows = std::to_array<AiRow>({
    { .ai_name = "VileMother", .prep = &part_as<Act4Ai, &Act4Ai::vile_mother> },
    { .ai_name = "FingerMage", .prep = &part_as<Act4Ai, &Act4Ai::finger_mage> },
    { .ai_name = "Regurgitator", .prep = &part_as<Act4Ai, &Act4Ai::regurgitator> },
    { .ai_name = "AbyssKnight", .prep = &part_as<Act4Ai, &Act4Ai::abyss_knight>, .skill = &part_as<Act4Ai, &Act4Ai::abyss_knight_skill> },
    { .ai_name = "OblivionKnight", .prep = &part_as<Act4Ai, &Act4Ai::oblivion_knight> },
    { .ai_name = "Diablo", .search = &part_as<Act4Ai, &Act4Ai::diablo_search>, .prep = &part_as<Act4Ai, &Act4Ai::diablo> },
    { .ai_name = "BaalCrab", .search = &part_as<Act4Ai, &Act4Ai::baal_search>, .prep = &part_as<Act4Ai, &Act4Ai::baal_crab>, .aim = &part_as<ThinkCtx, &ThinkCtx::aim_map_point> },
    { .ai_name = "BaalCrabClone", .search = &part_as<Act4Ai, &Act4Ai::baal_search>, .prep = &part_as<Act4Ai, &Act4Ai::baal_crab_clone>, .aim = &part_as<ThinkCtx, &ThinkCtx::aim_map_point> },
    { .ai_name = "UberIzual", .prep = &part_as<Act4Ai, &Act4Ai::uber_izual> },
});

}  // namespace

const std::span<const AiRow> kAct4Ais{ kRows };

}  // namespace d2d::game
