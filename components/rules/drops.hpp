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
inline int roll_quality(const Tables& t, const std::string& code, int ilvl, const std::array<int, 4>& mod, Rng& rng) {
    const auto* info = [&]() -> const ItemInfo* { const auto i = t.item_info.find(code); return i == t.item_info.end() ? nullptr : &i->second; }();
    const auto type = info ? t.types.find(info->type) : t.types.end();
    const bool magic_only = type != t.types.end() && type->second.always_magic;
    if (!magic_only && (!info || info->kind == 0 || (type != t.types.end() && type->second.always_normal))) return 2;
    const auto b = t.item_base.find(code);
    const int qlvl = b != t.item_base.end() ? b->second.level : 1;
    const bool uber = b != t.item_base.end() && (b->second.ubercode == code || b->second.ultracode == code)
                      && b->second.normcode != code;
    const auto& R = t.quality_ratio[uber ? 1 : 0];
    static constexpr int kQuality[5] = { 7, 5, 6, 4, 3 };
    for (int q = 0; q < (magic_only ? 4 : 5); ++q) {
        if (q == 2 && type != t.types.end() && !type->second.can_rare) continue;
        std::int64_t chance = std::int64_t(R[std::size_t(q)].base - (ilvl - qlvl) / std::max(R[std::size_t(q)].divisor, 1)) * 128;
        chance = std::max<std::int64_t>(chance, R[std::size_t(q)].min);
        if (q < 4) chance -= chance * mod[std::size_t(q)] / 1024;
        if (chance <= 0 || rng(int(std::min<std::int64_t>(chance, 1 << 30))) < 128) return kQuality[q];
    }
    return magic_only ? 4 : 2;
}

// Resolve treasure class `tc` for a monster of level mlvl: each pick
// rolls NoDrop against the entries' weights; a class entry resolves in
// turn (the highest quality modifiers on the way win), "gld" is gold
// (",mul=N": times N/256).
// ponytail: single player NoDrop as written (no player-count scaling),
// no negative picks, the gold amount isn't traced (mlvl + rand(8 mlvl)).
inline void roll_drops(const Tables& t, const std::string& tc, int mlvl, Rng& rng, std::vector<Drop>& out,
                       std::array<int, 4> mod = {}, int depth = 0) {
    const auto it = t.treasure.find(tc);
    if (it == t.treasure.end() || depth > 10) return;
    const auto& c = it->second;
    for (int i = 0; i < 4; ++i) mod[std::size_t(i)] = std::max(mod[std::size_t(i)], c.mod[std::size_t(i)]);
    int total = c.nodrop;
    for (const auto& [n, p] : c.items) total += p;
    for (int pick = 0; pick < std::max(c.picks, 1); ++pick) {
        int r = rng(total);
        if (r < c.nodrop) continue;
        r -= c.nodrop;
        for (const auto& [name, p] : c.items) {
            if (r >= p) { r -= p; continue; }
            if (name.starts_with("gld")) {
                int gold = mlvl + rng(8 * std::max(mlvl, 1));
                if (const auto m = name.find("mul="); m != std::string::npos) gold = gold * std::atoi(name.c_str() + m + 4) / 256;
                out.push_back({ "gld", 2, std::max(gold, 1) });
            } else if (t.treasure.contains(name)) {
                roll_drops(t, name, mlvl, rng, out, mod, depth + 1);
            } else {
                out.push_back({ name, roll_quality(t, name, mlvl, mod, rng), 0 });
            }
            break;
        }
    }
}

// The auto classes game.exe builds from the item tables: weapN / armoN
// hold the spawnable weapons / armor of level N-2..N (N = 3, 6, ... 87),
// weighted by their rarity.
inline void add_auto_treasure(Tables& t, const std::vector<std::pair<std::string, int>>& weapons_by_level,
                              const std::vector<std::pair<std::string, int>>& armor_by_level) {
    for (const auto& [prefix, list] : { std::pair{ "weap", &weapons_by_level }, std::pair{ "armo", &armor_by_level } })
        for (int n = 3; n <= 87; n += 3) {
            TreasureClass c;
            c.nodrop = 0;
            for (const auto& [code, lvl] : *list)
                if (lvl > n - 3 && lvl <= n) {
                    const auto r = t.item_rarity.find(code);
                    c.items.emplace_back(code, r == t.item_rarity.end() ? 1 : std::max(r->second, 1));
                }
            if (!c.items.empty()) t.treasure.try_emplace(prefix + std::to_string(n), std::move(c));
        }
}

}  // namespace d2d::rules
