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

namespace d2d::game {

namespace {

using d2d::rules::Boss;

struct Act3Preps : ThinkPrep {
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
        static constexpr std::array<std::string_view, 16> kModes{ "DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN" };
        const bool has_leader = monster.leader >= 0 && std::size_t(monster.leader) < pack.size() && &pack[std::size_t(monster.leader)] != &monster;
        const auto& head = has_leader ? pack[std::size_t(monster.leader)] : monster;
        input.grouped = has_leader && (head.alive() || head.mode == "DD");
        input.leader_mode = int(std::ranges::find(kModes, head.mode) - kModes.begin());
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
};

constexpr auto kRows = std::to_array<PrepRow>({
    { "ThornHulk", &prep_as<Act3Preps, &Act3Preps::thorn_hulk> },
    { "Tentacle", &prep_as<Act3Preps, &Act3Preps::tentacle> },
    { "FetishShaman", &prep_as<Act3Preps, &Act3Preps::fetish_shaman> },
    { "FrogDemon", &prep_as<Act3Preps, &Act3Preps::frog_demon> },
    { "ZakarumZealot", &prep_as<Act3Preps, &Act3Preps::zakarum_zealot> },
    { "ZakarumPriest", &prep_as<Act3Preps, &Act3Preps::zakarum_priest> },
    { "HighPriest", &prep_as<Act3Preps, &Act3Preps::high_priest> },
    { "WillOWisp", &prep_as<Act3Preps, &Act3Preps::will_o_wisp> },
});

}  // namespace

const std::span<const PrepRow> kAct3Preps{ kRows };

}  // namespace d2d::game
