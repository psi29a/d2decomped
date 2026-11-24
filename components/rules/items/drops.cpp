// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of drops.hpp's functions, with the research behind each.
#include "drops.hpp"

#include "item_gen.hpp"
#include "item_price.hpp"
#include "replenish.hpp"
#include "rules.hpp"
#include "vendor.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace d2d::rules {

// The item's quality at item level ilvl (FUN_00558640, all draws off the
// dropper's seed): a type flagged Normal stays normal; a base that only
// drops unique (Items +0x129), or a quest item of a Magic type, is unique.
// A Magic type is magic past those; else magic (mf undiminished),
// superior, normal, low. mod: Unique, Set, Rare, Magic.

// Then unique, set, rare (Rare types) in turn: odds (base - (ilvl - qlvl)
// / divisor) * 128, with magic find times 100 / (mf + 100, diminished past
// 10 with k 250 / 500 / 600: FUN_00558610), at least `min`, less the TC's
// modifier in 1024ths; odds <= 0 or rand(odds) < 128 wins. The ItemRatio
// row is by the base's class (type Class, FUN_00629f70) and uber (an
// exceptional or elite weapon or armor, FUN_0062b4d0).
int roll_quality(const Tables& tables, const std::string& code, int ilvl, const std::array<int, 4>& mod, Rng& rng, int magic_find) {
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

// The quality an item asked for at `quality` is made at (FUN_00557450):
// an always-magic type's quest base unique, else magic at least; no rare
// where the type can't be; an only-unique base unique; an always-normal
// type (or none) normal.

// A unique / set answers as asked even with no row; game.exe makes it rare
// / magic (FUN_005573f0 / FUN_00557380), and the only reader
// (open_container: FUN_00585f60's FUN_0062a0f0, quality 4..9) counts both
// alike. It falls under 4 only when no affix fits the base, not checked
// base by base.
int made_quality(const Tables& tables, const std::string& code, int quality) {
    const auto info = tables.item_info.find(code);
    const auto base = tables.item_base.find(code);
    if (info == tables.item_info.end() || base == tables.item_base.end()) return quality;
    const auto type_found = tables.types.find(info->second.type);
    if (type_found == tables.types.end()) return 2;
    const auto& type = type_found->second;
    if (type.always_magic) quality = base->second.quest ? 7 : quality >= 4 && quality <= 9 ? quality : 4;
    if (!type.can_rare && quality == 6) quality = 4;
    if (base->second.only_unique) quality = 7;
    return type.always_normal ? 2 : quality;
}

// A stand's item's quality (FUN_00556f60, nothing asked for), the first
// draw on its own seed: unique, rare, set, magic, superior, normal in
// turn, each won at rand(odds) == 0. A quest base is normal. The ItemRatio
// row (FUN_00637910) is the highest Version not over the item's (0 or 100).
// An expansion item (version 100+): odds base - past / divisor, under 1 a
// win with no draw; past ilvl - qlvl, 1 under 1 or for an ItemTypes row
// 0x34 (misc) item (FUN_00629bb0). A classic one (version 0): past the raw
// ilvl, unique / rare / set base - ilvl with no divisor, odds under 1 made 1
// and still drawn.
// None: superior with the args' +0x80 & 0x40 (set by FUN_005594c0 /
// FUN_00559630), else low. Then made.
int stand_quality(const Tables& tables, const std::string& code, int ilvl, Rng& item_seed, bool classic, bool superior) {
    const auto info = tables.item_info.find(code);
    const auto base = tables.item_base.find(code);
    if (info == tables.item_info.end() || base == tables.item_base.end()) return 2;
    if (base->second.quest) return 2;
    const auto type_found = tables.types.find(info->second.type);
    const bool cls = type_found != tables.types.end() && !type_found->second.cls.empty();
    const bool uber = (type_is(tables, info->second.type, "weap") || type_is(tables, info->second.type, "armo"))
                      && (base->second.ubercode == code || base->second.ultracode == code) && info->second.type != "tpot";
    const auto pair = std::size_t((cls ? 2 : 0) + (uber ? 1 : 0));
    const bool old_rows = classic && tables.has_classic_ratio[pair];   // a classic class item has no row (fatal 0x486): the expansion's
    const auto& ratio = old_rows ? tables.quality_ratio_classic[pair] : tables.quality_ratio[pair];
    const int past = classic ? ilvl : type_is(tables, info->second.type, "misc") ? 1 : std::max(ilvl - base->second.level, 1);
    static constexpr std::array<std::pair<int, std::size_t>, 6> kOrder{ { { 7, 0 }, { 6, 2 }, { 5, 1 }, { 4, 3 }, { 3, 4 }, { 2, 5 } } };
    for (const auto& [quality, row] : kOrder) {
        if (classic) {
            const int odds = std::max(ratio[row].base - (quality >= 5 ? past : past / std::max(ratio[row].divisor, 1)), 1);
            if (item_seed(odds) == 0) return made_quality(tables, code, quality);
            continue;
        }
        const int odds = ratio[row].base - past / std::max(ratio[row].divisor, 1);
        if (odds < 1 || item_seed(odds) == 0) return made_quality(tables, code, quality);
    }
    return made_quality(tables, code, superior ? 3 : 1);
}

TreasureEntry treasure_entry(std::string cell) {
    std::erase(cell, '"');
    TreasureEntry entry;
    const auto comma = cell.find(',');
    entry.code = comma == std::string::npos || cell.starts_with("gld,") ? std::move(cell) : cell.substr(0, comma);
    return entry;
}

// A gold pile's coins (FUN_00557ab0, the first draw on the new item's
// unit seed): ilvl (at least 1, FUN_00558d90) + rand(5 ilvl), at least 1;
// a ",mul=N" entry scales it by N / 256 (FUN_0055a6d0).

// Gold find (FUN_005589a0: times 100 + the killer's stat 0x4f, / 100) is
// the caller's (Loot::put).
int gold_amount(int ilvl, int mul, Rng& item_seed) {
    ilvl = std::max(ilvl, 1);
    const int gold = std::max(ilvl + item_seed(5 * ilvl), 1);
    return mul ? gold * mul >> 8 : gold;
}

// A monster's class moved on by level (FUN_00654e00): while the next row
// of its group has a level <= lvl.

// lvl 0 (normal difficulty, a MonStats noRatio or boss row:
// FUN_0055afa0) keeps it.
std::string tc_upgrade(const Tables& tables, std::string name, int lvl) {
    for (std::size_t guard = 0; lvl > 0 && guard < tables.treasure.size(); ++guard) {   // the whole group (Super: 90 rows)
        const auto here = tables.treasure.find(name);
        if (here == tables.treasure.end() || here->second.group == 0 || here->second.next.empty()) break;
        const auto next = tables.treasure.find(here->second.next);
        if (next == tables.treasure.end() || lvl < next->second.level) break;
        name = here->second.next;
    }
    return name;
}

}

namespace d2d::rules {
namespace detail {

bool roll_class(const Tables& tables, const TreasureClass& treasure, std::array<int, 4> mod, int ilvl, Rng& rng, std::vector<Drop>& out,
                int& count, int max, int shares, int magic_find, int depth, int forced, const Placement& place) {
    for (int i = 0; i < 4; ++i) mod[std::size_t(i)] = std::max(mod[std::size_t(i)], treasure.mod[std::size_t(i)]);
    int total = 0;
    for (const auto& [name, chance] : treasure.items) total += chance;
    if (total == 0) return true;
    int nodrop = treasure.nodrop;
    // More players (roll_drops' n) shrink NoDrop to total * p^n / (1 - p^n),
    // p = nodrop / (nodrop + total).
    if (nodrop != 0 && shares > 1) {
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
        for (std::size_t entry = 0; entry < treasure.items.size(); ++entry) {
            const auto& name = treasure.items[entry].first;
            const int chance = treasure.items[entry].second;
            if (roll >= chance) { roll -= chance; continue; }
            const int flags = entry < treasure.entry_flags.size() ? treasure.entry_flags[entry] : 0;
            if (const auto sub = tables.treasure.find(name); sub != tables.treasure.end()) {
                if (depth < 64 && !roll_class(tables, sub->second, mod, ilvl, rng, out, count, max, shares, magic_find, depth + 1, forced, place)) return false;
            } else if (name.starts_with("gld")) {
                const auto multiplier_at = name.find("mul=");
                const Drop pile{ "gld", forced ? forced : 2, 0, multiplier_at == std::string::npos ? 0 : std::atoi(name.c_str() + multiplier_at + 4) };
                if (!place || place(pile, count)) {
                    out.push_back(pile);
                    if (++count >= max) return false;
                }
            } else if (tables.item_info.contains(name)) {
                // FUN_0055a6d0 step 4: 7 / 5 for a unique / set entry (flags 1 /
                // 2), its index + 1 the row to make (struct +0x40); else the
                // forced quality, else the quality roll. A zero flag modifier
                // (flag_mod, bin +0x30 / +0x32; the txt chances are 0) does not
                // draw. Set 29 is not decided here: it drops only from a Hell
                // Bovine (flag 1 on the made item, FUN_005c25c0).
                const int index = entry < treasure.entry_index.size() ? treasure.entry_index[entry] : 0;
                int quality = 0, special = 0;
                if ((flags & 1) != 0) { quality = 7; special = index + 1; }
                else if ((flags & 2) != 0) { quality = 5; special = index + 1; }
                else quality = forced ? forced : roll_quality(tables, name, ilvl, mod, rng, magic_find);
                const Drop made{ name, quality, 0, 0, special };
                if (!place || place(made, count)) {
                    out.push_back(made);
                    if (++count >= max) return false;
                }
            }
            break;
        }
    }
    return true;
}

}
}

namespace d2d::rules {

void roll_drops(const Tables& tables, const std::string& treasure_class, int ilvl, Rng& rng, std::vector<Drop>& out,
                int players, int magic_find, int max, int forced, int nearby, int dropper_players, const Placement& place) {
    const auto found = tables.treasure.find(treasure_class);
    if (found == tables.treasure.end()) return;
    int count = 0;
    int rolls_for = nearby + (players - nearby) / 2;
    if (dropper_players > 0) rolls_for = std::min(rolls_for, dropper_players);
    detail::roll_class(tables, found->second, {}, ilvl, rng, out, count, max, rolls_for, magic_find, 0, forced, place);
}

int chest_round(const Tables& tables, const std::string& treasure_class, Rng& unit_seed, std::vector<Drop>& out, int forced, const Placement& place) {
    const auto first = out.size();
    roll_drops(tables, treasure_class, treasure_class.back() - 'A', unit_seed, out, 1, 0, 6, forced, 1, 0, place);
    return out.size() > first ? made_quality(tables, out[first].code, out[first].quality) : 0;
}

void add_auto_treasure(Tables& tables, const std::vector<std::string>& type_order, const std::vector<AutoBase>& bases) {
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

// A vendor's item (FUN_00576330) for a fill's ask: in Nightmare above clvl
// 25 rand(100000) on the proxy seed under ilvl * 64 + 4000 takes the
// ubercode, else the NightmareUpgrade; in Hell an expansion game's roll under
// ilvl * 16 + 1000 the ultracode, else under ilvl * 128 + 5000 the ubercode,
// then the HellUpgrade wins.

// Made as a drop (FUN_00559ce0 -> FUN_00558d90: two game seed steps, the
// type flags, generate_item); a "Cracked" low one (LowQualityItems row 1) is
// thrown away and made again, five tries. FUN_00576330's other remake (the
// item came out another code: made again normal, once) can't happen: only a
// unique changes the code, and an ask (1 .. 4) turns unique only for a quest
// or unique-only base (made_quality), which no 1.14d vendor stocks (Weapons
// / Armor / Misc <Vendor>Max and MagicMax rows: none has quest or unique
// set).
std::optional<d2d::d2s::Item> make_store_item(const Tables& tables, const StoreAsk& ask, int difficulty, int clvl, bool expansion,
                                              Rng& proxy, Rng& game, std::vector<bool>* found_uniques) {
    std::string code = ask.code;
    const auto found = tables.item_base.find(code);
    if (found == tables.item_base.end()) return std::nullopt;
    auto usable = [&](const std::string& other) { return !other.empty() && other != "xxx" && tables.item_base.contains(other); };
    if (difficulty > 0 && clvl > 25) {
        const auto& base = found->second;
        const auto roll = proxy.next() % 100000;
        if (difficulty == 1) {
            if (roll < std::uint32_t(ask.ilvl * 64 + 4000) && usable(base.ubercode)) code = base.ubercode;
            else if (usable(base.nightmare_upgrade)) code = base.nightmare_upgrade;
        } else {
            if (expansion && roll < std::uint32_t(ask.ilvl * 16 + 1000) && usable(base.ultracode)) code = base.ultracode;
            else if (roll < std::uint32_t(ask.ilvl * 128 + 5000) && usable(base.ubercode)) code = base.ubercode;
            if (usable(base.hell_upgrade)) code = base.hell_upgrade;
        }
    }
    for (int tries = 0; tries < 5; ++tries) {
        Rng unit_seed{ game.next() }, item_seed{ game.next() };
        if (!expansion) (void)stand_quality(tables, code, ask.ilvl, item_seed, true);   // FUN_00557450: a classic item's roll draws all the same
        auto item = generate_item(tables, code, ask.ilvl, made_quality(tables, code, ask.quality), item_seed, &unit_seed, found_uniques, false, difficulty, 0, !expansion);
        if (item.quality == 1 && item.qsub == 1) continue;
        if (ask.fill && found->second.max_stack > 0) item.quantity = found->second.max_stack;
        replenish_arm(tables, item);   // FUN_00558d90's end (0x559117, 0x559120)
        return item;
    }
    return std::nullopt;
}



}
