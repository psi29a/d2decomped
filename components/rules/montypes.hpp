// MonStats / MonStats2 / MonLvl / Levels.txt monster rows as the rules
// read them (split from monsters.hpp so uniques.hpp can use them too).
#pragma once

#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2d::rules {

// The MonStats / MonStats2 columns spawning and fighting read, per row.
struct MonType {
    std::string id, code, name_key, ai_name;         // Id, Code, NameStr, AI
    std::string montype;                        // MonType (MonUMod exclusions)
    std::array<std::string, 3> tc_champion, tc_unique;   // TreasureClass2 / 3 by difficulty
    int base = -1;                              // BaseId row (19: fallen1, 91: scarab1)
    int min_grp = 0, max_grp = 0, party_min = 0, party_max = 0, sparse = 0, rarity = 0;
    std::array<int, 2> minion{ -1, -1 };        // minion1/2 rows
    std::array<int, 3> level{};                 // Level, Level(N), Level(H)
    int velocity = 0, run = 0;
    bool enabled = false, killable = false, melee = false;
    bool undead = false, demon = false;         // hUndead / lUndead, demon (Holy Bolt, FoH, Blessed Hammer)
    std::string miss_a2;                        // MissA2: what an A2 attack fires (quillrat1: spike1)
    std::string sound;                          // MonSound: its MonSounds.txt row
    // El1..3 Mode ("A1", "A2", ...) and Type (0 fire, 1 light, 2 cold, 3 poison, 4 magic, -1 none).
    std::array<std::string, 3> el_mode;
    std::array<int, 3> el_type{ -1, -1, -1 };
    bool can_block = false;                     // MonStats2 mBL
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
    int size = 2;                               // SizeX
    std::string base_w;                         // BaseW
    std::array<std::vector<std::string>, 16> parts;   // HDv..S8v components, per layer present
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
    std::array<int, 3> umin{}, umax{};          // MonUMin/Max (normal has none in 1.14d act 1)
    bool wander = false;                        // MonWndr
    int num_mon = 0;                            // NumMon
    std::vector<int> mon, nmon;                 // mon1.., nmon1.. rows (normal / NM+hell)
};

}  // namespace d2d::rules
