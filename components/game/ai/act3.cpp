// SPDX-License-Identifier: GPL-3.0-or-later
// Act 3's think inputs (rules/thinks/act3.hpp reads them).
#include "ai.hpp"

#include "gamedata.hpp"
#include "internal.hpp"

#include <monsters.hpp>
#include <shrines.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace d2d::game {

namespace {

using d2d::rules::Boss;

struct Act3Ai : ThinkCtx {
    // The Fetish Shaman's (FUN_005f9a80: FUN_005dd0b0 mode 1, filter
    // FUN_005f99a0): the closest corpse (DD) by squared distance within
    // aip5 squared, selectable, of BaseId 0x8d (or 0x18c unless aip2 is 1),
    // not neutral, not special (data +0x16 & 8) unless aip2 is over 2, of
    // its own group unless aip2 is set.
    void fetish_shaman() {
        const int any_group = input.aip[1], reach = input.aip[4] * input.aip[4];
        int closest = std::numeric_limits<int>::max();
        for (const int pack_index : near_order([&](const Monster& other) { return &other != &monster && other.usable_corpse(); })) {
            const auto& other = pack[std::size_t(pack_index)];
            const int base = game_data.monsters.types[std::size_t(other.type)].base;
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y, squared = off_x * off_x + off_y * off_y;
            if ((base != 0x8d && (base != 0x18c || any_group == 1)) || other.align == 1 || (any_group <= 2 && other.boss != Boss::none && other.boss != Boss::minion)
                || (any_group == 0 && other.leader != int(self)) || squared > reach || squared >= closest) continue;
            corpse = pack_index;
            closest = squared;
        }
        input.kin_dist = closest;
        input.corpse = corpse >= 0;
        corpse_place();
    }
    // The ThornHulk's FUN_005415a0(1) after its frenzy (Skill1) starts
    // (FUN_005f4850): that cast mode's end event.
    void thorn_hulk() { input.next_event = mode_end_event(game_data, monster, type_info.sk_mode[0], frame); }
    // The tentacles' head (their leader) and its mode.
    void tentacle() {
        const bool has_leader = monster.leader >= 0 && std::size_t(monster.leader) < pack.size() && &pack[std::size_t(monster.leader)] != &monster;
        const auto& head = has_leader ? pack[std::size_t(monster.leader)] : monster;
        input.grouped = has_leader && (head.alive() || head.mode == "DD");
        input.leader_mode = monster_mode(head.mode, 16);
    }
    // Its own spot's collision: the frogs' 0xc01, the zealots' 0x40; the
    // zealots' spare: the player's quest 0x15 bit 0 (a3q5 done) in act 3.
    void frog_demon() { input.spot_clear = !level.blocked(unit.x, unit.y, 0xc01); }
    void zakarum_zealot() {
        input.spot_clear = !level.blocked(unit.x, unit.y, 0x40);
        input.spared = target->a3q5_done && d2d::rules::level_act(level.id) == 2;
    }
    // The heals' searches: the priests' (FUN_005f7240) a zakarum (BaseId
    // zealot1 / cantor1) within aip6 (squared) under 61 % life, the last
    // found (its best is never kept, bugs.md #22); the Council's
    // (FUN_005e0430) the weakest under 75 % (its start, 0x4b) within 50
    // (squared 2500), itself too, not under Confuse / Attract / Conversion
    // (FUN_006259b0 0). Both friends (FUN_00650d70), live, in near order.
    void heal_search(bool priest) {
        int weakest = 75;
        for (const int found : near_order([&](const Monster& other) {
                 return other.alive() && d2d::rules::friends(monster.align, other.align) && (priest ? &other != &monster && other.align != 2 : other.align == 0);
             })) {
            const auto& other = pack[std::size_t(found)];
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y, squared = off_x * off_x + off_y * off_y;
            const int life = int(std::int64_t(other.hit_points) * 100 / std::max(other.stats.hit_points, 1));
            const int base = game_data.monsters.types[std::size_t(other.type)].base;
            if (priest && (base == 235 || base == 238) && squared <= input.aip[5] * input.aip[5] && life < 61) { aid = found; input.kin_dist = squared; }
            if (!priest && squared <= 2500 && life < weakest) { aid = found; weakest = life; input.kin_dist = squared; }
        }
        input.aid = aid >= 0;
    }
    void zakarum_priest() { heal_search(true); }
    void high_priest() { heal_search(false); }
    // The wisps (FUN_0065a950 / FUN_0065ac70, radius 0x20): its room alone
    // when the square 32 round it lies inside it (FUN_0065a6b0), else its
    // near list; FUN_0065a710 lets every room of it through (bugs.md #23);
    // each live (FUN_0063ea40: not DT / DD) unit of BaseId 0x76 in them
    // (FUN_005f3960, FUN_00463860), itself too, each room's newest first
    // (its unit list, +0x74 / +0xe8). The blessing's target a player not
    // dying or dead (FUN_005541b0).
    void will_o_wisp() {
        auto rects = missile_rooms(game_data, level, unit.x, unit.y);
        if (!rects.empty()) {
            const auto own = rects.front();
            const bool inside = own[0] < x - 0x20 && own[1] < y - 0x20 && x + 0x20 < own[0] + own[2] && y + 0x20 < own[1] + own[3];
            rects.erase(rects.begin());
            if (inside) rects = { own };
            for (const auto& rect : rects)
                for (std::size_t i = pack.size(); i-- > 0;) {
                    const auto& other = pack[i];
                    const int other_x = subtile(other.unit.x), other_y = subtile(other.unit.y);
                    if (other_x < rect[0] || other_y < rect[1] || other_x >= rect[0] + rect[2] || other_y >= rect[1] + rect[3]) continue;
                    if (other.alive() && other.mode != "DT" && other.mode != "DD" && game_data.monsters.types[std::size_t(other.type)].base == 118
                        && !std::ranges::contains(wisp_finds, int(i)))
                        wisp_finds.push_back(int(i));
                }
        }
        input.wisps = int(wisp_finds.size());
        input.bless_ok = !target->pet && !target->of && target->alive;
    }
    // MonAI 91, DarkWanderer (FUN_005ea130; a3q7's record 0x1c): his spot
    // (FUN_005bd0d0, from the marker's x + 7 he's made on) is 20 subtiles
    // north, else 11 north, 2 east or west of that, else 8 north, else 3
    // north, the first he has a clear line to (FUN_006229f0: sight_blocked
    // from his subtile and size to a size-2 end, mask 0x3c01; the last taken
    // as it is), found once and kept in the record (its +2 flag). AI control
    // +0x14 0 -> 1; a foe 20 or more off: stand 40. Under 20, state 1: walk
    // there, state 2. State 2 at its end: within 2 of it, or after 3 more
    // tries (+0x18), he's gone (unit flag 0x20000, FUN_005ddfc0) and a3q7
    // marks every player in act 3 (FUN_005bd4a0 -> FUN_005bd390, word 0x20
    // bit 0); else walk again, a player's path (FUN_005de190(1, 0, 0): type
    // 7). The afterglow he leaves: World::tick.
    bool dark_wanderer() {
        if (!monster.wanderer_spot) {
            const int home_x = subtile(monster.home_x), home_y = subtile(monster.home_y);
            const auto wall = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x3c01); };
            auto clear = [&](int at_x, int at_y) { return !d2d::rules::sight_blocked(x, y, type_info.size, at_x, at_y, 2, wall); };
            int spot_x = home_x, spot_y = home_y - 20;
            if (!clear(spot_x, spot_y)) {
                spot_y = home_y - 11;
                if (!clear(spot_x, spot_y) && !clear(spot_x = home_x + 2, spot_y) && !clear(spot_x = home_x - 2, spot_y)) {
                    spot_x = home_x; spot_y = home_y - 8;
                    if (!clear(spot_x, spot_y)) spot_y = home_y - 3;
                }
            }
            monster.wanderer_spot = std::pair{ spot_x, spot_y };
        }
        const auto [spot_x, spot_y] = *monster.wanderer_spot;
        if (monster.ai_state == 0) { monster.ai_state = 1; monster.ai_state2 = 0; }
        if (best >= 0x14) { idle(0x28); return true; }
        auto head_off = [&] { unit.goal_x = (float(spot_x) + 0.5f) / 5; unit.goal_y = (float(spot_y) + 0.5f) / 5; set_off(false, nullptr); };
        if (monster.ai_state == 1) { head_off(); monster.ai_state = 2; return true; }
        if (d2d::rules::ai_distance(spot_x - x, spot_y - y) < 2 || monster.ai_state2 >= 3) { monster.gone = true; return true; }
        ++monster.ai_state2;
        monster.next_path_type = 7;
        head_off();
        return true;
    }
};

constexpr auto kRows = std::to_array<AiRow>({
    { .ai_name = "ThornHulk", .prep = &part_as<Act3Ai, &Act3Ai::thorn_hulk> },
    { .ai_name = "Tentacle", .thinks_alone = true, .prep = &part_as<Act3Ai, &Act3Ai::tentacle> },
    { .ai_name = "FetishShaman", .prep = &part_as<Act3Ai, &Act3Ai::fetish_shaman> },
    { .ai_name = "FrogDemon", .thinks_after_miss = true, .prep = &part_as<Act3Ai, &Act3Ai::frog_demon> },
    { .ai_name = "ZakarumZealot", .prep = &part_as<Act3Ai, &Act3Ai::zakarum_zealot> },
    { .ai_name = "ZakarumPriest", .prep = &part_as<Act3Ai, &Act3Ai::zakarum_priest>, .aim = &part_as<ThinkCtx, &ThinkCtx::aim_map_point> },
    { .ai_name = "HighPriest", .prep = &part_as<Act3Ai, &Act3Ai::high_priest>, .aim = &part_as<ThinkCtx, &ThinkCtx::aim_map_point> },
    { .ai_name = "WillOWisp", .prep = &part_as<Act3Ai, &Act3Ai::will_o_wisp> },
    { .ai_name = "TentacleHead", .thinks_alone = true },
    { .ai_name = "DarkWanderer", .think_targeted = &part_as<Act3Ai, &Act3Ai::dark_wanderer> },
});

}  // namespace

const std::span<const AiRow> kAct3Ais{ kRows };

}  // namespace d2d::game
