// Drops: treasure classes (TreasureClassEx and the auto weapN / armoN
// classes) and item quality rolls (ItemRatio), as game.exe rolls them
// (docs/research/re/drops.md).
#pragma once

#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace d2d::rules {

// What a kill drops: an item (code, quality 1 low .. 7 unique) or gold
// ("gld": `gold` coins, or while 0 its amount is rolled as it's made:
// gold_amount, times `mul` / 256 when the entry was "gld,mul=N").
struct Drop { std::string code; int quality = 2; int gold = 0; int mul = 0; };

// The item's quality at item level ilvl (FUN_00558640, all draws off the
// dropper's seed): a type flagged Normal stays normal; a base that only
// drops unique (Items +0x129), or a quest item of a Magic type, is unique.
// Then unique, set, rare (Rare types) in turn: odds (base - (ilvl - qlvl) /
// divisor) * 128, with magic find times 100 / (mf + 100, diminished past
// 10 with k 250 / 500 / 600: FUN_00558610), at least `min`, less the TC's
// modifier in 1024ths; odds <= 0 or rand(odds) < 128 wins. A Magic type is
// magic past those; else magic (mf undiminished), superior, normal, low.
// The ItemRatio row is by the base's class (type Class, FUN_00629f70) and
// uber (an exceptional or elite weapon or armor, FUN_0062b4d0).
// mod: Unique, Set, Rare, Magic.
inline int roll_quality(const Tables& tables, const std::string& code, int ilvl, const std::array<int, 4>& mod, Rng& rng, int magic_find = 0) {
    const auto info = tables.item_info.find(code);
    const auto base = tables.item_base.find(code);
    if (info == tables.item_info.end() || base == tables.item_base.end()) return 2;
    const auto type_found = tables.types.find(info->second.type);
    const ItemType type = type_found != tables.types.end() ? type_found->second : ItemType{};
    if (type.always_normal) return 2;
    if (base->second.only_unique || (type.always_magic && base->second.quest)) return 7;
    const bool uber = (type_is(tables, info->second.type, "weap") || type_is(tables, info->second.type, "armo"))
                      && (base->second.ubercode == code || base->second.ultracode == code) && info->second.type != "tpot" && !base->second.quest;
    const auto& ratio = tables.quality_ratio[std::size_t((type.cls.empty() ? 0 : 2) + (uber ? 1 : 0))];
    const int past = ilvl - base->second.level;
    auto wins = [&](const QualityRatio& row, int knee, int modifier) {
        int odds = (row.base - past / std::max(row.divisor, 1)) * 128;
        if (magic_find != 0) {
            const int x = magic_find + 100, eff = knee == 0 || x < 111 ? x : (x - 100) * knee / (x - 100 + knee) + 100;
            if (eff != 0) odds = odds * 100 / eff;
        }
        odds = std::max(odds, row.min);
        odds -= modifier * odds / 1024;
        return odds <= 0 || rng(odds) < 128;
    };
    if (magic_find >= -99) {
        if (wins(ratio[0], 250, mod[0])) return 7;
        if (wins(ratio[1], 500, mod[1])) return 5;
        if (type.can_rare && wins(ratio[2], 600, mod[2])) return 6;
        if (type.always_magic || wins(ratio[3], 0, mod[3])) return 4;
    }
    const int superior = (ratio[4].base - past / std::max(ratio[4].divisor, 1)) * 128;
    if (superior <= 0 || rng(superior) < 128) return 3;
    const int normal = (ratio[5].base - past / std::max(ratio[5].divisor, 1)) * 128;
    return normal < 1 || rng(normal) < 128 ? 2 : 1;
}

// A gold pile's coins (FUN_00557ab0, the first draw on the new item's
// unit seed): ilvl (at least 1, FUN_00558d90) + rand(5 ilvl), at least 1;
// a ",mul=N" entry scales it by N / 256 (FUN_0055a6d0).
// ponytail: gold find (FUN_005589a0: + killer stat 0x4f %) isn't applied.
inline int gold_amount(int ilvl, int mul, Rng& item_seed) {
    ilvl = std::max(ilvl, 1);
    const int gold = std::max(ilvl + item_seed(5 * ilvl), 1);
    return mul ? gold * mul >> 8 : gold;
}

// A monster's class moved on by level (FUN_00654e00): while the next row
// of its group has a level <= lvl. lvl 0 (normal difficulty, a MonStats
// noRatio or boss row: FUN_0055afa0) keeps it.
inline std::string tc_upgrade(const Tables& tables, std::string name, int lvl) {
    for (std::size_t guard = 0; lvl > 0 && guard < tables.treasure.size(); ++guard) {   // the whole group (Super: 90 rows)
        const auto here = tables.treasure.find(name);
        if (here == tables.treasure.end() || here->second.group == 0 || here->second.next.empty()) break;
        const auto next = tables.treasure.find(here->second.next);
        if (next == tables.treasure.end() || lvl < next->second.level) break;
        name = here->second.next;
    }
    return name;
}

namespace detail {
// One class's picks (a frame of FUN_0055a6d0's stack); false once `max`
// items dropped (the whole roll stops).
inline bool roll_class(const Tables& tables, const TreasureClass& treasure, std::array<int, 4> mod, int ilvl, Rng& rng, std::vector<Drop>& out,
                       int& count, int max, int players, int magic_find, int depth) {
    for (int i = 0; i < 4; ++i) mod[std::size_t(i)] = std::max(mod[std::size_t(i)], treasure.mod[std::size_t(i)]);
    int total = 0;
    for (const auto& [name, chance] : treasure.items) total += chance;
    if (total == 0) return true;
    int nodrop = treasure.nodrop;
    // More players (a lone killer: 1 + (players - 1) / 2) shrink NoDrop to
    // total * p^n / (1 - p^n), p = nodrop / (nodrop + total).
    if (const int shares = 1 + (players - 1) / 2; nodrop != 0 && shares > 1) {
        const double empty = double(nodrop) / double(nodrop + total);
        double all_empty = empty;
        for (int i = 1; i < shares; ++i) all_empty *= empty;
        const double rest = 1 - all_empty;
        nodrop = rest == 0 ? 0 : int((1 - rest) * total / rest);
    }
    const int picks = std::max(std::abs(treasure.picks), 1);
    for (int pick = 0; pick < picks; ++pick) {
        int roll = pick;                     // negative picks: the pick-th by weight, no NoDrop
        if (treasure.picks >= 0) {
            roll = rng(nodrop + total);
            if (roll < nodrop) continue;
            roll -= nodrop;
        } else if (roll >= total) break;
        for (const auto& [name, chance] : treasure.items) {
            if (roll >= chance) { roll -= chance; continue; }
            if (const auto sub = tables.treasure.find(name); sub != tables.treasure.end()) {
                if (depth < 64 && !roll_class(tables, sub->second, mod, ilvl, rng, out, count, max, players, magic_find, depth + 1)) return false;
            } else if (name.starts_with("gld")) {
                const auto multiplier_at = name.find("mul=");
                out.push_back({ "gld", 2, 0, multiplier_at == std::string::npos ? 0 : std::atoi(name.c_str() + multiplier_at + 4) });
                if (++count >= max) return false;
            } else if (tables.item_info.contains(name)) {
                out.push_back({ name, roll_quality(tables, name, ilvl, mod, rng, magic_find) });
                if (++count >= max) return false;
            }
            break;
        }
    }
    return true;
}
}  // namespace detail

// Resolve treasure class `tc` (FUN_0055a6d0) off the dropper's seed `rng`:
// each pick rolls rand(NoDrop + total) (NoDrop scaled by `players`, the
// /players count); a class entry picks in turn, depth first, the highest
// quality modifiers on the way winning; an item rolls its quality at
// `ilvl`. Negative picks: no NoDrop roll, the n-th of |picks| is the entry
// whose running weight passes n (the Countess: her item TC, then her rune
// TC), stopping at the weights' total. At most `max` items (6) in all.
// ponytail: TreasureClassEx's unique / set item entries (flags 1 / 2: Cow
// King's classes only) and the m4 / m5 flag draws (bin +0x30 / +0x32,
// always 0) aren't here; every item counts (game.exe doesn't count one
// FUN_00555da0 finds no floor for, but Level::nearest_free always finds one).
inline void roll_drops(const Tables& tables, const std::string& treasure_class, int ilvl, Rng& rng, std::vector<Drop>& out,
                       int players = 1, int magic_find = 0, int max = 6) {
    const auto found = tables.treasure.find(treasure_class);
    if (found == tables.treasure.end()) return;
    int count = 0;
    detail::roll_class(tables, found->second, {}, ilvl, rng, out, count, max, players, magic_find, 0);
}

// The auto classes (FUN_006541c0): for each ItemTypes row with
// TreasureClass (bow, weap, mele, armo, abow), in its order, <code>N for N
// = 3, 6 .. 96 holds the spawnable non-quest bases (items-table order:
// weapons, armor, misc) of the type (type or type2, by Equiv) with qlvl in
// N-2..N, weighted by their type's Rarity; tpot bases only in a tpot class.
struct AutoBase { std::string code, type, type2; int level = 0; };
inline void add_auto_treasure(Tables& tables, const std::vector<std::string>& type_order, const std::vector<AutoBase>& bases) {
    for (const auto& type_code : type_order) {
        const auto type = tables.types.find(type_code);
        if (type == tables.types.end() || !type->second.treasure_class) continue;
        for (int level = 3; level <= 96; level += 3) {
            TreasureClass treasure_class;
            treasure_class.level = level - 3;
            for (const auto& base : bases) {
                if (base.level <= level - 3 || base.level > level) continue;
                if (!type_is(tables, base.type, type_code) && !type_is(tables, base.type2, type_code)) continue;
                if (type_code != "tpot" && (type_is(tables, base.type, "tpot") || type_is(tables, base.type2, "tpot"))) continue;
                const auto base_type = tables.types.find(base.type);
                treasure_class.items.emplace_back(base.code, base_type == tables.types.end() ? 1 : std::max(base_type->second.rarity, 1));
            }
            tables.treasure.try_emplace(type_code + std::to_string(level), std::move(treasure_class));
        }
    }
}

}  // namespace d2d::rules
