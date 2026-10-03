// SPDX-License-Identifier: GPL-3.0-or-later
// MonStats / MonStats2 / MonLvl / Levels.txt monster rows as the rules
// read them (split from monsters.hpp so uniques.hpp can use them too).
#pragma once


#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2d::rules {

// The MonStats / MonStats2 columns spawning and fighting read, per row.
struct MonType {
    std::string id, code, name_key, ai_name;         // Id, Code, NameStr, AI
    std::string montype;                        // MonType (MonUMod exclusions)
    std::array<std::string, 3> tc_champion, tc_unique;   // TreasureClass2 / 3 by difficulty
    std::array<std::string, 3> tc_quest;        // TreasureClass4: while quest TCQuestId isn't done (flags 15, 1, TCQuestCP)
    int tc_quest_id = 0, tc_quest_cp = 0;
    bool tc_fixed = false;                      // noRatio or boss (MonStats +0xc & 0x44): its TC never moves on by level
    bool no_ratio = false;                      // noRatio (+0xc & 4): stats as written, not MonLvl percentages (FUN_006538a0)
    int base = -1;                              // BaseId row (19: fallen1, 91: scarab1)
    int min_grp = 0, max_grp = 0, party_min = 0, party_max = 0, sparse = 0, rarity = 0;
    std::array<int, 2> minion{ -1, -1 };        // minion1/2 rows
    std::array<int, 3> level{};                 // Level, Level(N), Level(H)
    int velocity = 0, run = 0;
    bool spawnable = false, killable = false, melee = false, open_doors = false;   // isSpawn (MonStats flag bit 0), killable, isMelee, opendoors
    bool ranged = false;                        // rangedtype
    bool undead = false, demon = false;         // hUndead / lUndead, demon (Holy Bolt, FoH, Blessed Hammer)
    std::string miss_a1, miss_a2;               // MissA1 / MissA2: what an A1 / A2 attack fires (sk_archer1: skbowarrow1, quillrat1: spike1)
    std::array<std::string, 4> skill;           // Skill1..4 (Skills.txt names; "" none): the MonAI thinks' skills
    std::array<std::string, 4> sk_mode;         // Sk1mode..4: a mode token, or a MonSeq sequence (seq_nestlay)
    std::array<int, 4> sk_lvl{};                // Sk1lvl..4 (record +0x198)
    int trans_lvl = 0;                          // TransLvl (record byte +0x4b, FUN_006510c0): which of a skill missile's variants (shafire1..5)
    std::string spawn, spawn_mode;              // spawn / spawnmode: what Nest lays (crownest1: foulcrow1, NU)
    int spawn_x = 0, spawn_y = 0;               // spawnx / spawny: where, off the layer (subtiles)
    int place_spawn = -1;                       // with placespawn, the spawn row a population pick becomes 80 % of the time (FUN_005bde80)
    std::string sound;                          // MonSound: its MonSounds.txt row
    std::string usound;                         // UMonSound: a boss's or minion's (rules::boss_sound)
    // El1..3 Mode ("A1", "A2", ...) and Type (0 fire, 1 light, 2 cold, 3 poison, 4 magic, -1 none).
    std::array<std::string, 3> el_mode;
    std::array<int, 3> el_type{ -1, -1, -1 };
    bool can_block = false;                     // MonStats2 mBL
    int threat = 0;                             // threat (record +0x4e, FUN_005dc920): a mode 5 search's primary at 2 or more
    bool switch_ai = false;                     // SwitchAI (flags +0xe & 1, FUN_00573040): Confuse / Attract can set its target
    // Percentages of the MonLvl row (1.10+ style), per difficulty.
    struct Diff {
        int min_hp = 0, max_hp = 0, armor_class = 0, exp = 0;
        int a1_min = 0, a1_max = 0, a1_th = 0, a2_min = 0, a2_max = 0, a2_th = 0;
        int aidel = 0, aidist = 0;
        std::array<int, 8> aip{};
        std::string treasure_class;                          // TreasureClass1
        std::array<int, 6> res{};                // ResDm, ResMa, ResFi, ResLi, ResCo, ResPo (%)
        int to_block = 0, drain = 100, cold_effect = 0;   // ToBlock, Drain (leech %), coldeffect (speed % while chilled)
        struct El { int pct = 0, min = 0, max = 0, dur = 0; };
        std::array<El, 3> elements{};                  // El1..3 Pct / MinD / MaxD (MonLvl %) / Dur (ticks)
    };
    std::array<Diff, 3> diff{};
    // MonStats2.
    int size = 2, melee_rng = 0;                // SizeX, MeleeRng (in melee: unit_distance within it + 1)
    std::string base_w;                         // BaseW
    std::array<std::vector<std::string>, 16> parts;   // HDv..S8v components, per layer present
    std::array<std::uint8_t, 16> choices{};     // HDv..S8v list lengths, every layer (MonStats2 +0x15)
    int pieces = 0;                             // TotalPieces (MonStats2 +0xec)
    int spawn_col = 0;                          // spawnCol (MonStats2 +10): FUN_005b2a00's collision mask
};

// MonLvl.txt, by level: the base values MonStats' percentages apply to.
struct MonLvl { std::array<int, 3> armor_class{}, to_hit{}, hit_points{}, damage{}, experience{}; };

struct Monsters {
    std::vector<MonType> types;                 // MonStats rows
    std::unordered_map<std::string, int> by_id;
    std::vector<MonLvl> lvl;                    // by level
    // By Id, any case (Skills.txt's summon says ClayGolem for claygolem).
    [[nodiscard]] int row(std::string id) const {
        if (const auto found = by_id.find(id); found != by_id.end()) return found->second;
        for (auto& letter : id) letter = char(std::tolower(static_cast<unsigned char>(letter)));
        for (const auto& [key, row_index] : by_id)
            if (key.size() == id.size() && std::ranges::equal(key, id, [](char left, char right) { return std::tolower(static_cast<unsigned char>(left)) == right; })) return row_index;
        return -1;
    }
};

// Levels.txt monster columns for one level.
struct LevelMon {
    std::array<int, 3> density{};               // MonDen, (N), (H): chance in 100000 per 3x3 subtiles
    std::array<int, 3> umin{}, umax{};          // MonUMin/Max (FUN_005479c0: Levels +0x28 / +0x2b + difficulty)
    bool wander = false;                        // MonWndr
    int num_mon = 0;                            // NumMon
    bool ranged_first = false;                  // rangedspawn (level def +0x31)
    std::vector<int> mon, nmon;                 // mon1.., nmon1.. rows (normal / NM+hell)
    std::vector<int> umon;                      // umon1..: normal's unique picks (FUN_005bde80)
    // Random object groups per room (FUN_00552610, objects.md): each ObjGrp
    // is an objgroup.txt row (Offset), rolled with the matching ObjPrb
    // (0..100) on the room1 seed.
    std::array<std::uint8_t, 8> obj_group{}, obj_prob{};
};

// One objgroup.txt row (Offset key, 0..132 in 1.14d): 8 slots of {object
// id, density (PopulateFn param), weight}. A row picks one slot on the
// room seed, weights taken as cumulative probabilities that sum to 100.
struct ObjGroup {
    std::array<int, 8> id{};
    std::array<std::uint8_t, 8> density{};
    std::array<std::uint8_t, 8> weight{};
};

}  // namespace d2d::rules
