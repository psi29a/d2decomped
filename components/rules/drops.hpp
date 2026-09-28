// Drops: treasure classes (TreasureClassEx and the auto weapN / armoN
// classes) and item quality rolls (ItemRatio).
#pragma once

#include "rules.hpp"

#include <array>
#include <string>
#include <vector>

namespace d2d::rules {

// What a kill drops: an item (code, quality 1 low .. 7 unique) or gold.
struct Drop { std::string code; int quality = 2; int gold = 0; };

// The item's quality at item level ilvl (ITEMS_RollQuality in 1.10's
// terms; ItemRatio.txt): from unique down to superior, the odds are
// (base - (ilvl - qlvl) / divisor) * 128, at least `min`, less the treasure
// class's modifier (1024ths); a roll below 128 of them wins. Rings,
// amulets, charms, jewels (ItemTypes Magic) are at least magic; potions,
// gems and the like (not armor or weapons, not Magic) stay normal.
// ponytail: no magic find, no class-specific rows, no low quality.
inline int roll_quality(const Tables& tables, const std::string& code, int ilvl, const std::array<int, 4>& mod, Rng& rng) {
    const auto* info = [&]() -> const ItemInfo* { const auto found = tables.item_info.find(code); return found == tables.item_info.end() ? nullptr : &found->second; }();
    const auto type = info ? tables.types.find(info->type) : tables.types.end();
    const bool magic_only = type != tables.types.end() && type->second.always_magic;
    if (!magic_only && (!info || info->kind == 0 || (type != tables.types.end() && type->second.always_normal))) return 2;
    const auto found = tables.item_base.find(code);
    const int qlvl = found != tables.item_base.end() ? found->second.level : 1;
    const bool uber = found != tables.item_base.end() && (found->second.ubercode == code || found->second.ultracode == code)
                      && found->second.normcode != code;
    const auto& ratio = tables.quality_ratio[uber ? 1 : 0];
    static constexpr int kQuality[5] = { 7, 5, 6, 4, 3 };
    for (int quality = 0; quality < (magic_only ? 4 : 5); ++quality) {
        if (quality == 2 && type != tables.types.end() && !type->second.can_rare) continue;
        std::int64_t chance = std::int64_t(ratio[std::size_t(quality)].base - (ilvl - qlvl) / std::max(ratio[std::size_t(quality)].divisor, 1)) * 128;
        chance = std::max<std::int64_t>(chance, ratio[std::size_t(quality)].min);
        if (quality < 4) chance -= chance * mod[std::size_t(quality)] / 1024;
        if (chance <= 0 || rng(int(std::min<std::int64_t>(chance, 1 << 30))) < 128) return kQuality[quality];
    }
    return magic_only ? 4 : 2;
}

// Resolve treasure class `tc` for a monster of level mlvl: each pick
// rolls NoDrop against the entries' weights; a class entry resolves in
// turn (the highest quality modifiers on the way win), "gld" is gold
// (",mul=N": times N/256).
// ponytail: single player NoDrop as written (no player-count scaling),
// no negative picks, the gold amount isn't traced (mlvl + rand(8 mlvl)).
inline void roll_drops(const Tables& tables, const std::string& treasure_class, int mlvl, Rng& rng, std::vector<Drop>& out,
                       std::array<int, 4> mod = {}, int depth = 0) {
    const auto found = tables.treasure.find(treasure_class);
    if (found == tables.treasure.end() || depth > 10) return;
    const auto& treasure = found->second;
    for (int i = 0; i < 4; ++i) mod[std::size_t(i)] = std::max(mod[std::size_t(i)], treasure.mod[std::size_t(i)]);
    int total = treasure.nodrop;
    for (const auto& [item_name, chance] : treasure.items) total += chance;
    for (int pick = 0; pick < std::max(treasure.picks, 1); ++pick) {
        int roll = rng(total);
        if (roll < treasure.nodrop) continue;
        roll -= treasure.nodrop;
        for (const auto& [name, chance] : treasure.items) {
            if (roll >= chance) { roll -= chance; continue; }
            if (name.starts_with("gld")) {
                int gold = mlvl + rng(8 * std::max(mlvl, 1));
                if (const auto multiplier_at = name.find("mul="); multiplier_at != std::string::npos) gold = gold * std::atoi(name.c_str() + multiplier_at + 4) / 256;
                out.push_back({ "gld", 2, std::max(gold, 1) });
            } else if (tables.treasure.contains(name)) {
                roll_drops(tables, name, mlvl, rng, out, mod, depth + 1);
            } else {
                out.push_back({ name, roll_quality(tables, name, mlvl, mod, rng), 0 });
            }
            break;
        }
    }
}

// The auto classes game.exe builds from the item tables: weapN / armoN
// hold the spawnable weapons / armor of level N-2..N (N = 3, 6, ... 87),
// weighted by their rarity.
inline void add_auto_treasure(Tables& tables, const std::vector<std::pair<std::string, int>>& weapons_by_level,
                              const std::vector<std::pair<std::string, int>>& armor_by_level) {
    for (const auto& [prefix, list] : { std::pair{ "weap", &weapons_by_level }, std::pair{ "armo", &armor_by_level } })
        for (int level = 3; level <= 87; level += 3) {
            TreasureClass treasure_class;
            treasure_class.nodrop = 0;
            for (const auto& [code, lvl] : *list)
                if (lvl > level - 3 && lvl <= level) {
                    const auto found = tables.item_rarity.find(code);
                    treasure_class.items.emplace_back(code, found == tables.item_rarity.end() ? 1 : std::max(found->second, 1));
                }
            if (!treasure_class.items.empty()) tables.treasure.try_emplace(prefix + std::to_string(level), std::move(treasure_class));
        }
}

}  // namespace d2d::rules
