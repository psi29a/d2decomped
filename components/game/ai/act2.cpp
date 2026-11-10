// SPDX-License-Identifier: GPL-3.0-or-later
// Act 2's think inputs (rules/thinks/act2.hpp reads them).
#include "ai.hpp"

#include "gamedata.hpp"
#include "internal.hpp"

#include <monster_ids.hpp>
#include <monsters.hpp>
#include <uniques.hpp>

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>

namespace d2d::game {

namespace {

struct Act2Ai : ThinkCtx {
    // The searches (FUN_005dd0b0 mode 1: near_order): the nearest living
    // kin of its class (FUN_005b0d00, the Panthers'), any other living one
    // (FUN_005f06b0, the Sand Raider's), by squared distance (FUN_005b0bd0).
    void kin_near(bool any_type) {
        int kin_dist = std::numeric_limits<int>::max();
        for (const int pack_index : near_order([&](const Monster& other) { return &other != &monster; })) {
            const auto& other = pack[std::size_t(pack_index)];
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y, squared = off_x * off_x + off_y * off_y;
            if (other.alive() && (any_type || other.type == monster.type) && squared < kin_dist) { kin = pack_index; kin_dist = squared; }
        }
        input.kin = kin >= 0; input.kin_dist = kin_dist;
    }
    void panther() { kin_near(false); }
    void sand_raider() { kin_near(true); }
    // The Greater Mummy's (FUN_005f2a00) within aip5: the count, a corpse
    // (DD, not raised) and a hurt one, the last found; an lUndead (MonStats
    // +0xd & 8) not a unique (FUN_005a0180, 8). Radament's (FUN_005f2b10,
    // class 0xe5): within aip5 + 10, uniques too, on Normal any undead
    // (FUN_0063e990).
    void greater_mummy() {
        for (const int pack_index : near_order([&](const Monster& other) { return &other != &monster; })) {
            const auto& other = pack[std::size_t(pack_index)];
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y, squared = off_x * off_x + off_y * off_y;
            const bool radament = monster.type == d2d::rules::monster_ids::kRadament;
            const auto& other_type = game_data.monsters.types[std::size_t(other.type)];
            if (!(radament && monster.difficulty == 0 ? other_type.undead : other_type.low_undead)) continue;
            if (!radament && (other.boss == d2d::rules::Boss::unique || other.boss == d2d::rules::Boss::superunique)) continue;
            const int reach = input.aip[4] + (radament ? 10 : 0);
            if (squared > reach * reach) continue;
            ++input.in_reach;
            if (other.usable_corpse()) corpse = pack_index;
            else if (other.alive() && other.hit_points < other.stats.hit_points) aid = pack_index;
        }
        input.corpse = corpse >= 0; input.aid = aid >= 0;
        corpse_place();
    }
    // The Vulture's: the rooms, a weak unit within 11 (squared
    // 0x79) at most aip3 % life (FUN_005f30f0: a player or a monster, not
    // itself, alive, life <= max / 100 x aip3), its group, the free spot
    // nearest its circling point (FUN_005f3170: FUN_0064e7b0 at its size,
    // mask 0xffff, from the target's room then its own; none keeps the
    // point). d2d's grid is the level's, so the two searches are one
    // (deviations.md approximation 14).
    void vulture() {
        input.same_room = room_of(level, unit.x, unit.y) == room_of(level, target->x, target->y);
        input.can_land = monster.aloft;                            // FUN_005f2fc0 fails on a landed one (pattern 1)
        // In a group: an owner (FUN_0058f0d0: AI control +0x28 / +0x2c, set by
        // FUN_0058f030 on a boss's minions, FUN_005a0c00; never on the boss),
        // not a plain pack's. A MonStats SetBoss party (FUN_005b2830, +0xc &
        // 0x10) would group too, but no Vulture row has SetBoss or minion1.
        input.grouped = monster.boss == d2d::rules::Boss::minion;
        for (const auto& foe : foes) {
            const int off_x = subtile(foe.x) - x, off_y = subtile(foe.y) - y;
            if (foe.alive && off_x * off_x + off_y * off_y <= 0x79 && foe.life_pct <= input.aip[2]) input.victim = true;
        }
        for (const auto& other : pack) {
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y;
            if (&other != &monster && other.alive() && off_x * off_x + off_y * off_y <= 0x79 && other.hit_points <= other.stats.hit_points / 100 * input.aip[2])
                input.victim = true;
        }
        if (monster.ai_state2 != 0 && monster.ai_state3 != 0) {
            input.free_spot[0] = free_spot_near(level, crowd, &unit, monster.ai_state2, monster.ai_state3, unit.shape.size, 0xffff);
            if (!input.free_spot[0] && room_of(level, (float(monster.ai_state2) + 0.5f) / 5, (float(monster.ai_state3) + 0.5f) / 5) >= 0)
                input.free_spot[0] = std::pair{ monster.ai_state2, monster.ai_state3 };   // keep 1: the point, in a room
        }
    }
    // Leap (do 77): a free spot past the target (twice it less itself,
    // FUN_0064e7b0 at its size, 0x3c01) in a clear line (0xc01).
    void sand_leaper() {
        const auto landing = free_spot_near(level, crowd, &unit, 2 * target_x - x, 2 * target_y - y, unit.shape.size, 0x3c01);
        input.usable = landing && !d2d::rules::sight_blocked(x, y, 0, landing->first, landing->second, 0, [this](int at_x, int at_y) { return wallc01(at_x, at_y); });
    }
    // Search type 0 with a think (Coldworm's, MonAI 66): no target looked for,
    // the think runs anyway; it reads none, so any live foe stands in.
    void coldworm_search() {
        if (!target)
            for (auto& foe : foes) if (foe.alive) { target = &foe; break; }
    }
};

constexpr auto kRows = std::to_array<AiRow>({
    { .ai_name = "SandLeaper", .prep = &part_as<Act2Ai, &Act2Ai::sand_leaper> },
    { .ai_name = "ClawViper", .prep = &part_as<ThinkCtx, &ThinkCtx::charge_line> },
    { .ai_name = "PantherWoman", .prep = &part_as<Act2Ai, &Act2Ai::panther> },
    { .ai_name = "PantherJavelin", .prep = &part_as<Act2Ai, &Act2Ai::panther> },
    { .ai_name = "SandRaider", .prep = &part_as<Act2Ai, &Act2Ai::sand_raider> },
    { .ai_name = "GreaterMummy", .prep = &part_as<Act2Ai, &Act2Ai::greater_mummy> },
    { .ai_name = "Vulture", .prep = &part_as<Act2Ai, &Act2Ai::vulture> },
    { .ai_name = "SandMaggotQueen", .search = &part_as<Act2Ai, &Act2Ai::coldworm_search> },
});

}  // namespace

const std::span<const AiRow> kAct2Ais{ kRows };

}  // namespace d2d::game
