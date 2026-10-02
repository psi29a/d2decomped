// SPDX-License-Identifier: GPL-3.0-or-later
// D2Decomp composite component table — what a .d2s appearance byte means.
//
// A save's appearance bytes (header 0x88, one per layer HD TR LG RA LA RH
// LH SH S1..S8) are indices into a table game.exe builds at startup
// (FUN_00506000, table at 0x87d838, 12-byte entries). Rebuilt here from
// the same excel data:
//   [0] none, [1..3] "lit" "med" "hvy" (body-part armour tiers), then one
//   entry per distinct graphic code (alternategfx, else code) of every
//   weapon, torso armour, shield and helm (not circlets) in weapons.txt,
//   armor.txt, misc.txt order.
// Placement is not plain append: a cursor walks up from slot 4 and an item
// skips any slot whose *reserved type* (a static per-slot list, 0x72e1e8)
// is in the same category as the item — weapon (ItemTypes 45) or armour
// (50) — or that is already taken. The cursor only advances when an item
// lands on it; a code already present below the cursor is skipped. That
// quirk is why claws sit at 43 ahead of the battle bows at 47.
// Verified 29/29 against equipped items in real 1.14d saves (test_compcode).
// See docs/research/re/compcode.md.
#pragma once

#include <txt.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::compcode {

// ItemTypes.txt hierarchy: a type "is a" T if it is T or inherits it
// through Equiv1/Equiv2.
class Types {
public:
    explicit Types(const txt::Table& itemtypes) {
        const auto code = itemtypes.col("Code"), equiv1 = itemtypes.col("Equiv1"),
                   equiv2 = itemtypes.col("Equiv2");
        for (std::size_t i = 0; i < itemtypes.size(); ++i)
            if (const auto type_code = itemtypes.get(i, code); !type_code.empty())
                index_.emplace(std::string(type_code), int(i));
        parents_.resize(itemtypes.size());
        for (std::size_t i = 0; i < itemtypes.size(); ++i)
            for (auto equiv : { equiv1, equiv2 })
                if (auto parent = index_.find(std::string(itemtypes.get(i, equiv))); parent != index_.end())
                    parents_[i].push_back(parent->second);
    }
    [[nodiscard]] int index(std::string_view code) const {
        auto found = index_.find(std::string(code));
        return found == index_.end() ? -1 : found->second;
    }
    // The hierarchy is acyclic (D2's own walk, FUN_00504a20, errors out
    // past 64 pending entries); rows with an empty Code aren't indexed, so
    // an empty Equiv can't alias them.
    [[nodiscard]] bool isa(int type, int ancestor) const {
        if (type == ancestor) return true;
        if (type < 0 || type >= int(parents_.size())) return false;
        for (int parent : parents_[std::size_t(type)]) if (isa(parent, ancestor)) return true;
        return false;
    }
private:
    std::unordered_map<std::string, int> index_;
    std::vector<std::vector<int>>        parents_;
};

inline constexpr int kWeapon = 45, kArmor = 50, kTorso = 3, kShield = 51,
                     kHelm = 37, kCirclet = 75;

// Composite weapon-class tokens, indexed by D2Comp's weapon-class id
// (table after the layer names in game.exe; count 15 at 0x72e198).
inline constexpr std::array<std::string_view, 15> kWClass = {
    "", "hth", "1ht", "2ht", "1hs", "2hs", "bow", "xbw", "stf",
    "1js", "1jt", "1ss", "1st", "ht1", "ht2",
};
// Item wclass token -> id; empty/unknown is hand-to-hand, as D2 maps
// unmatched codes through {code, n} at 0x72ef68 to 0x72ef30[0] = 1.
inline int wclass_id(std::string_view token) {
    for (std::size_t i = 1; i < kWClass.size(); ++i)
        if (kWClass[i] == token) return int(i);
    return 1;
}

struct Entry {
    std::string code;          // DCC component token, e.g. "lit", "hax", "buc"
    int         wclass  = 1;   // weapon-class id held in one hand (0x87d83c)
    int         wclass2 = 1;   // ... held with both hands (0x87e430)
    int         type    = -1;  // ItemTypes index; -1 for lit/med/hvy
    bool        armor   = false;  // is-a Any Armor (shields, helms held)
};

// Reserved type per slot (first dword of each 12-byte record at 0x72e1e8,
// game.exe 1.14d) — the only static data the builder needs.
inline constexpr std::array<std::uint8_t, 283> kReservedType = {
     1,  1,  1,  1, 37, 37, 37, 37, 37, 37, 37,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  2,  2,  2,  2, 16, 16, 16, 15, 15, 15, 19, 19, 19,
    37,  2,  2, 25, 25, 25, 29, 43, 30, 30, 36, 28, 30, 30, 36, 31, 31, 30, 30, 38,
    38, 38, 38, 32, 32, 32, 32, 33, 33, 33, 30, 30, 33, 33, 33, 33, 27, 27, 27, 27,
    26, 26, 26, 26, 28, 34, 28, 28, 34, 31, 34, 34, 35, 35, 43, 29, 30, 36, 24, 38,
    38, 42, 32, 30, 30, 30, 33, 27, 27, 27, 27, 26, 28, 34, 28, 35, 35,  1,  3,  3,
     2,  3, 40, 40, 19, 19, 16, 16, 15, 15, 30, 28, 43, 29,  3, 28, 28, 36, 36, 30,
    30, 32, 32, 33, 33, 33, 34, 34, 32, 33, 24, 24, 26, 26, 36, 28, 28, 28, 28, 28,
    28, 28, 28, 28, 28, 25, 25, 25, 25, 36, 24, 24, 24, 36, 36, 36, 36, 36, 36, 36,
    30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 32, 32, 32, 32, 42, 43,
    42, 43, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 34, 34, 34, 34, 34, 34, 26, 26,
    26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 27, 35, 35, 35, 35, 37, 37, 37, 37, 37,
    37, 37,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  2,  2,
     2,  2,  2, 16, 16, 16, 16, 16, 15, 15, 15, 15, 15, 19, 19, 19, 19, 19, 37,  2,
     2, 36, 36,
};

inline std::vector<Entry> build(const txt::Table& itemtypes, const txt::Table& weapons,
                                const txt::Table& armor, const txt::Table& misc) {
    const Types types(itemtypes);
    std::vector<Entry> table(0x400);
    table[1].code = "lit"; table[2].code = "med"; table[3].code = "hvy";
    int cursor = 4;
    auto reserved = [](int index) { return index < int(kReservedType.size()) ? int(kReservedType[std::size_t(index)]) : 0; };
    for (const txt::Table* source_table : { &weapons, &armor, &misc }) {
        const auto c_code = source_table->col("code"), c_alt = source_table->col("alternategfx"),
                   c_type = source_table->col("type"), c_wc = source_table->col("wclass"),
                   c_wc2 = source_table->col("2handedwclass");
        for (std::size_t row = 0; row < source_table->size(); ++row) {
            std::string code(source_table->get(row, c_alt));
            if (code.empty()) code = source_table->get(row, c_code);
            if (code.empty()) continue;
            const int type = types.index(source_table->get(row, c_type));
            bool known = false;
            for (int i = 0; i < cursor && !known; ++i) known = table[std::size_t(i)].code == code;
            if (known) continue;
            const bool wanted = (types.isa(type, kWeapon) || types.isa(type, kTorso) ||
                                 types.isa(type, kShield) || types.isa(type, kHelm)) &&
                                !types.isa(type, kCirclet);
            if (!wanted) continue;
            int idx = cursor;
            while ((types.isa(reserved(idx), kWeapon) && types.isa(type, kWeapon)) ||
                   (types.isa(reserved(idx), kArmor) && types.isa(type, kArmor)) ||
                   !table[std::size_t(idx)].code.empty())
                ++idx;
            if (idx > 0xfe) idx = cursor;   // D2 falls back to the cursor slot
            table[std::size_t(idx)] = { code, wclass_id(source_table->get(row, c_wc)),
                                        wclass_id(source_table->get(row, c_wc2)), type,
                                        types.isa(type, kArmor) };
            if (idx == cursor) ++cursor;
        }
    }
    table.resize(0x100);   // appearance bytes are u8; 0xff = empty
    return table;
}

// Weapon class a composite animates with, from the right-hand, left-hand
// and shield appearance bytes (0xff = empty). Port of FUN_00504af0,
// including its quirks: a lone weapon uses its two-handed class, claws
// only count for the Assassin (d2s class 6), and two claws resolve to
// "ht1". Returns "" for combinations D2 rejects (it then falls back to a
// default composite).
inline std::string_view weapon_class(int d2s_class, const std::vector<Entry>& table,
                                     std::uint8_t right_hand, std::uint8_t left_hand, std::uint8_t shield) {
    const bool both = right_hand != 0xff && left_hand != 0xff;
    auto claw = [&](int weapon) { return (weapon == 13 || weapon == 14) && d2s_class != 6; };
    int right_class = 0, left_class = 0;
    if (right_hand != 0xff) {
        const auto& entry = table[right_hand];
        right_class = (both || (left_hand == 0xff && shield == 0xff && entry.wclass2 != entry.wclass)) ? entry.wclass2 : entry.wclass;
        if (claw(right_class) || entry.armor) right_class = 0;   // reserved-list wclass, 0 for these slots
    }
    if (left_hand != 0xff) {
        const auto& entry = table[left_hand];
        left_class = both ? entry.wclass2 : entry.wclass;
        if (claw(left_class) || entry.armor) left_class = 0;
    }
    auto id = [&]() -> int {
        if (right_class == 0) return left_class == 0 ? 1 : left_class;
        if (right_class == left_class && (right_class == 6 || right_class == 7)) return right_class;   // bow/xbow pair
        if (right_class == 8) return 8;                         // staff
        if (left_class == 0) return right_class;
        // Dual wield: 1hs(4) 1ht(2) 2hs(5) combos -> 1js/1jt/1ss/1st.
        if (right_class == 4 && left_class == 4) return 11;
        if (right_class == 4 && left_class == 2) return 9;
        if (right_class == 5 && left_class == 2) return 11;
        if (right_class == 2 && left_class == 4) return 12;
        if (left_class == 4 || left_class == 5) {
            if (right_class == 2) return 11;
        } else if (right_class == 2) {
            return left_class == 2 ? 10 : 0;
        }
        if (right_class == 5) return (left_class == 4 || left_class == 5) ? 11 : 0;
        if (right_class == 4) return left_class == 5 ? 11 : 0;
        if (right_class == 13) return left_class == 13 ? 13 : 0;
        if (right_class == 14) return left_class == 14 ? 13 : 0;
        return (right_class == 1 && left_class == 1) ? 1 : 0;
    }();
    return kWClass[std::size_t(id)];
}

// What each wearable draws as: armor / weapons / misc.txt `component` (the
// layer: 0 HD, 1 TR, 5 RH, 6 LH, 7 SH, 10 S3, 16 none), its graphic code
// (alternategfx, else code), and body armour's lit / med / hvy tiers
// (Torso, Legs, rArm, lArm, rSPad, lSPad: 0..2).
// Transform / InvTrans: the colormap sets its tint uses on the character /
// in the inventory (compcode.md "Tints").
struct Piece { std::string gfx, type, type2; int component = 16, transform = 0, inv_transform = 0; std::array<int, 6> tiers{ -1, -1, -1, -1, -1, -1 }; };
inline std::unordered_map<std::string, Piece> pieces(const txt::Table& weapons, const txt::Table& armor, const txt::Table& misc) {
    std::unordered_map<std::string, Piece> out;
    for (const txt::Table* table : { &weapons, &armor, &misc })
        for (std::size_t row = 0; row < table->size(); ++row) {
            const std::string code(table->get(row, "code"));
            if (code.empty()) continue;
            Piece piece;
            piece.gfx = std::string(table->get(row, "alternategfx"));
            if (piece.gfx.empty()) piece.gfx = code;
            piece.type = std::string(table->get(row, "type")); piece.type2 = std::string(table->get(row, "type2"));
            const auto component = table->get(row, "component");
            piece.component = component.empty() ? 16 : std::atoi(std::string(component).c_str());
            piece.transform = std::atoi(std::string(table->get(row, "Transform")).c_str());
            piece.inv_transform = std::atoi(std::string(table->get(row, "InvTrans")).c_str());
            if (table->get(row, "type") == "circ") piece.component = 16;   // circlets aren't drawn (compcode.md)
            int layer_count = 0;
            for (const char* col : { "Torso", "Legs", "rArm", "lArm", "rSPad", "lSPad" }) {
                const auto value = table->get(row, col);
                piece.tiers[std::size_t(layer_count++)] = value.empty() ? -1 : std::atoi(std::string(value).c_str());
            }
            out.emplace(code, std::move(piece));
        }
    return out;
}

// A worn item: its body location, code and colour (Colours::of).
struct Worn { int slot; std::string code; int colour = -1; };

// Colours::gem from gems.txt, keeping the codes whose misc.txt type is a
// "gem" (ItemTypes 20: not runes or jewels).
inline std::unordered_map<std::string, int> gem_colours(const txt::Table& itemtypes, const txt::Table& misc, const txt::Table& gems) {
    const Types types(itemtypes);
    const int gem_type = types.index("gem");
    std::unordered_map<std::string, std::string> type;
    for (std::size_t row = 0; row < misc.size(); ++row) type.emplace(misc.get(row, "code"), misc.get(row, "type"));
    std::unordered_map<std::string, int> out;
    for (std::size_t row = 0; row < gems.size(); ++row) {
        const std::string code(gems.get(row, "code"));
        const auto found = type.find(code);
        if (found != type.end() && types.isa(types.index(found->second), gem_type))
            out.emplace(code, std::atoi(std::string(gems.get(row, "transform")).c_str()));
    }
    return out;
}

// The layers each worn item draws on, with its piece (look's placement).
template <class F> void each_layer(const std::unordered_map<std::string, Piece>& pcs, const std::vector<Worn>& worn, F&& visit) {
    for (const auto& worn_item : worn) {
        if (worn_item.slot != 1 && worn_item.slot != 3 && worn_item.slot != 4 && worn_item.slot != 5) continue;
        const auto piece = pcs.find(worn_item.code);
        if (piece == pcs.end() || piece->second.component >= 16) continue;
        if (piece->second.component == 1) {
            static constexpr int kLayer[6] = { 1, 2, 3, 4, 8, 9 };   // TR LG RA LA S1 S2
            for (int k = 0; k < 6; ++k)
                if (piece->second.tiers[std::size_t(k)] >= 0) visit(kLayer[k], worn_item, piece->second);
            continue;
        }
        visit(piece->second.component == 5 && worn_item.slot == 5 ? 6 : piece->second.component, worn_item, piece->second);
    }
}

// A character's look (the d2s header's 16 appearance bytes) from what it
// wears: body locations 1 head, 3 torso, 4 right hand, 5 left hand. Each
// item goes on its component's layer as its graphic's table index (a one-
// hand weapon in the left hand on LH); body armour sets TR LG RA LA S1 S2 to
// lit + its tiers. Unworn: TR LG RA LA S1 S2 lit, the rest empty (0xff).
// Checked against the real saves (test_compcode).
inline std::array<std::uint8_t, 16> look(const std::vector<Entry>& table, const std::unordered_map<std::string, Piece>& pcs,
                                         const std::vector<Worn>& worn) {
    std::array<std::uint8_t, 16> tints;
    tints.fill(0xff);
    for (int lit_layer : { 1, 2, 3, 4, 8, 9 }) tints[std::size_t(lit_layer)] = 1;
    auto index = [&](const std::string& gfx) -> std::uint8_t {
        for (std::size_t i = 1; i < table.size() && i < 0xff; ++i) if (table[i].code == gfx) return std::uint8_t(i);
        return 0xff;
    };
    each_layer(pcs, worn, [&](int layer, const Worn&, const Piece& piece) {
        if (piece.component == 1) {
            static constexpr int kTier[10] = { 0, 0, 1, 2, 3, 0, 0, 0, 4, 5 };   // TR LG RA LA .. S1 S2
            tints[std::size_t(layer)] = std::uint8_t(1 + piece.tiers[std::size_t(kTier[layer])]);
        } else tints[std::size_t(layer)] = index(piece.gfx);
    });
    return tints;
}

// An item's colour, a Colors.txt index or -1 (FUN_0062c100): a unique's
// UniqueItems chrtransform, a set item's SetItems chrtransform; magic and
// rare items the first suffix with a transformcolor, else the first prefix,
// else the class automod (AutoMagic row id - 1). Any other (crafted too):
// its first socketed item's gems.txt transform if that's a gem, else the
// automod. Uniques and sets by row without separators, affixes by raw row
// (the save's ids).
struct Colours {
    std::vector<std::string> codes;                            // Colors.txt Code, by index
    std::vector<std::string> unique, set, prefix, suffix, automod;
    std::unordered_map<std::string, int> gem;                  // gem code -> gems.txt transform (gem types only)
    std::vector<std::string> unique_inv, set_inv;              // invtransform: a unique's / set item's colour in the inventory
    [[nodiscard]] int at(const std::vector<std::string>& column, int row) const {
        if (row < 0 || std::size_t(row) >= column.size() || column[std::size_t(row)].empty()) return -1;
        for (std::size_t i = 0; i < codes.size(); ++i) if (codes[i] == column[std::size_t(row)]) return int(i);
        return -1;
    }
    // socket: the first socketed item's code, if the item is socketed;
    // inv: the inventory's colour (FUN_0062c100's last argument).
    [[nodiscard]] int of(int quality, int unique_id, int set_id, int pre, int suf, const std::array<int, 6>& rare, int class_affix,
                         const std::string& socket = {}, bool inv = false) const {
        if (quality == 7) return at(inv ? unique_inv : unique, unique_id);
        if (quality == 5) return at(inv ? set_inv : set, set_id);
        if (quality != 4 && quality != 6) {
            if (const auto found = gem.find(socket); found != gem.end() && found->second >= 0 && found->second < 21) return found->second;
            return at(automod, class_affix - 1);
        }
        const std::array<int, 3> sufs = quality == 4 ? std::array<int, 3>{ suf, 0, 0 } : std::array<int, 3>{ rare[1], rare[3], rare[5] };
        const std::array<int, 3> pres = quality == 4 ? std::array<int, 3>{ pre, 0, 0 } : std::array<int, 3>{ rare[0], rare[2], rare[4] };
        for (int suffix_id : sufs) if (suffix_id > 0) if (const int colour = at(suffix, suffix_id); colour >= 0) return colour;
        for (int prefix_id : pres) if (prefix_id > 0) if (const int colour = at(prefix, prefix_id); colour >= 0) return colour;
        return at(automod, class_affix - 1);   // 1-based: paladin shields' 26/27 are Prismatic/Chromatic (res-all)
    }
};

// A colormap set exists for Transform 1, 2 and 5..8 (FUN_00600c20).
[[nodiscard]] inline bool tints_with(int transform) { return transform > 0 && transform <= 8 && transform != 3 && transform != 4; }

// Each layer's tint (the d2s header's 16 bytes at 0x98): (Transform x 32
// + colour + 1) & 0xff, 0xff with no colour or Transform 0, 3 or 4.
// Transform 8 wraps below 0x20; tint_of reads it back as game.exe does.
inline std::array<std::uint8_t, 16> tints(const std::unordered_map<std::string, Piece>& pcs, const std::vector<Worn>& worn) {
    std::array<std::uint8_t, 16> layers;
    layers.fill(0xff);
    each_layer(pcs, worn, [&](int layer, const Worn& worn_item, const Piece& piece) {
        const int transform = piece.transform;
        layers[std::size_t(layer)] = worn_item.colour < 0 || !tints_with(transform) ? 0xff : std::uint8_t((transform * 32 + worn_item.colour + 1) & 0xff);
    });
    return layers;
}
// A tint byte's colormap set and colour; false for none (FUN_005038d0:
// byte - 1, Transform 0 is Transform 8's colormaps).
struct Tint { int transform = 0, colour = 0; };
inline bool tint_of(std::uint8_t packed, Tint& tint) {
    if (packed == 0xff || packed == 0) return false;
    tint.transform = (packed - 1) >> 5; tint.colour = (packed - 1) & 31;
    if (tint.transform == 0) tint.transform = 8;   // the wrap
    return tint.colour < 21;
}

}  // namespace d2d::compcode
