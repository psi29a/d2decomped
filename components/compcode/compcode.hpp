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
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace d2d::compcode {

// ItemTypes.txt hierarchy: a type "is a" T if it is T or inherits it
// through Equiv1/Equiv2.
class Types {
public:
    explicit Types(const txt::Table& itemtypes) {
        const auto code = itemtypes.col("Code"), e1 = itemtypes.col("Equiv1"),
                   e2 = itemtypes.col("Equiv2");
        for (std::size_t i = 0; i < itemtypes.size(); ++i)
            if (const auto c = itemtypes.get(i, code); !c.empty())
                index_.emplace(std::string(c), int(i));
        parents_.resize(itemtypes.size());
        for (std::size_t i = 0; i < itemtypes.size(); ++i)
            for (auto c : { e1, e2 })
                if (auto p = index_.find(std::string(itemtypes.get(i, c))); p != index_.end())
                    parents_[i].push_back(p->second);
    }
    [[nodiscard]] int index(std::string_view code) const {
        auto it = index_.find(std::string(code));
        return it == index_.end() ? -1 : it->second;
    }
    // The hierarchy is acyclic (D2's own walk, FUN_00504a20, errors out
    // past 64 pending entries); rows with an empty Code aren't indexed, so
    // an empty Equiv can't alias them.
    [[nodiscard]] bool isa(int t, int of) const {
        if (t == of) return true;
        if (t < 0 || t >= int(parents_.size())) return false;
        for (int p : parents_[std::size_t(t)]) if (isa(p, of)) return true;
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
    auto reserved = [](int i) { return i < int(kReservedType.size()) ? int(kReservedType[std::size_t(i)]) : 0; };
    for (const txt::Table* t : { &weapons, &armor, &misc }) {
        const auto c_code = t->col("code"), c_alt = t->col("alternategfx"),
                   c_type = t->col("type"), c_wc = t->col("wclass"),
                   c_wc2 = t->col("2handedwclass");
        for (std::size_t r = 0; r < t->size(); ++r) {
            std::string code(t->get(r, c_alt));
            if (code.empty()) code = t->get(r, c_code);
            if (code.empty()) continue;
            const int ty = types.index(t->get(r, c_type));
            bool known = false;
            for (int i = 0; i < cursor && !known; ++i) known = table[std::size_t(i)].code == code;
            if (known) continue;
            const bool wanted = (types.isa(ty, kWeapon) || types.isa(ty, kTorso) ||
                                 types.isa(ty, kShield) || types.isa(ty, kHelm)) &&
                                !types.isa(ty, kCirclet);
            if (!wanted) continue;
            int idx = cursor;
            while ((types.isa(reserved(idx), kWeapon) && types.isa(ty, kWeapon)) ||
                   (types.isa(reserved(idx), kArmor) && types.isa(ty, kArmor)) ||
                   !table[std::size_t(idx)].code.empty())
                ++idx;
            if (idx > 0xfe) idx = cursor;   // D2 falls back to the cursor slot
            table[std::size_t(idx)] = { code, wclass_id(t->get(r, c_wc)),
                                        wclass_id(t->get(r, c_wc2)), ty,
                                        types.isa(ty, kArmor) };
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
                                     std::uint8_t rh, std::uint8_t lh, std::uint8_t sh) {
    const bool both = rh != 0xff && lh != 0xff;
    auto claw = [&](int w) { return (w == 13 || w == 14) && d2s_class != 6; };
    int a = 0, b = 0;
    if (rh != 0xff) {
        const auto& e = table[rh];
        a = (both || (lh == 0xff && sh == 0xff && e.wclass2 != e.wclass)) ? e.wclass2 : e.wclass;
        if (claw(a) || e.armor) a = 0;   // reserved-list wclass, 0 for these slots
    }
    if (lh != 0xff) {
        const auto& e = table[lh];
        b = both ? e.wclass2 : e.wclass;
        if (claw(b) || e.armor) b = 0;
    }
    auto id = [&]() -> int {
        if (a == 0) return b == 0 ? 1 : b;
        if (a == b && (a == 6 || a == 7)) return a;   // bow/xbow pair
        if (a == 8) return 8;                         // staff
        if (b == 0) return a;
        // Dual wield: 1hs(4) 1ht(2) 2hs(5) combos -> 1js/1jt/1ss/1st.
        if (a == 4 && b == 4) return 11;
        if (a == 4 && b == 2) return 9;
        if (a == 5 && b == 2) return 11;
        if (a == 2 && b == 4) return 12;
        if (b == 4 || b == 5) {
            if (a == 2) return 11;
        } else if (a == 2) {
            return b == 2 ? 10 : 0;
        }
        if (a == 5) return (b == 4 || b == 5) ? 11 : 0;
        if (a == 4) return b == 5 ? 11 : 0;
        if (a == 13) return b == 13 ? 13 : 0;
        if (a == 14) return b == 14 ? 13 : 0;
        return (a == 1 && b == 1) ? 1 : 0;
    }();
    return kWClass[std::size_t(id)];
}

// What each wearable draws as: armor / weapons / misc.txt `component` (the
// layer: 0 HD, 1 TR, 5 RH, 6 LH, 7 SH, 10 S3, 16 none), its graphic code
// (alternategfx, else code), and body armour's lit / med / hvy tiers
// (Torso, Legs, rArm, lArm, rSPad, lSPad: 0..2).
// Transform: the colormap set its tint uses (compcode.md "Tints").
struct Piece { std::string gfx; int component = 16, transform = 0; std::array<int, 6> tiers{ -1, -1, -1, -1, -1, -1 }; };
inline std::unordered_map<std::string, Piece> pieces(const txt::Table& weapons, const txt::Table& armor, const txt::Table& misc) {
    std::unordered_map<std::string, Piece> out;
    for (const txt::Table* t : { &weapons, &armor, &misc })
        for (std::size_t r = 0; r < t->size(); ++r) {
            const std::string code(t->get(r, "code"));
            if (code.empty()) continue;
            Piece p;
            p.gfx = std::string(t->get(r, "alternategfx"));
            if (p.gfx.empty()) p.gfx = code;
            const auto c = t->get(r, "component");
            p.component = c.empty() ? 16 : std::atoi(std::string(c).c_str());
            p.transform = std::atoi(std::string(t->get(r, "Transform")).c_str());
            if (t->get(r, "type") == "circ") p.component = 16;   // circlets aren't drawn (compcode.md)
            int k = 0;
            for (const char* col : { "Torso", "Legs", "rArm", "lArm", "rSPad", "lSPad" }) {
                const auto v = t->get(r, col);
                p.tiers[std::size_t(k++)] = v.empty() ? -1 : std::atoi(std::string(v).c_str());
            }
            out.emplace(code, std::move(p));
        }
    return out;
}

// A worn item: its body location, code and colour (Colours::of).
struct Worn { int slot; std::string code; int colour = -1; };

// The layers each worn item draws on, with its piece (look's placement).
template <class F> void each_layer(const std::unordered_map<std::string, Piece>& pcs, const std::vector<Worn>& worn, F&& f) {
    for (const auto& w : worn) {
        if (w.slot != 1 && w.slot != 3 && w.slot != 4 && w.slot != 5) continue;
        const auto p = pcs.find(w.code);
        if (p == pcs.end() || p->second.component >= 16) continue;
        if (p->second.component == 1) {
            static constexpr int kLayer[6] = { 1, 2, 3, 4, 8, 9 };   // TR LG RA LA S1 S2
            for (int k = 0; k < 6; ++k)
                if (p->second.tiers[std::size_t(k)] >= 0) f(kLayer[k], w, p->second);
            continue;
        }
        f(p->second.component == 5 && w.slot == 5 ? 6 : p->second.component, w, p->second);
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
    std::array<std::uint8_t, 16> a;
    a.fill(0xff);
    for (int l : { 1, 2, 3, 4, 8, 9 }) a[std::size_t(l)] = 1;
    auto index = [&](const std::string& gfx) -> std::uint8_t {
        for (std::size_t i = 1; i < table.size() && i < 0xff; ++i) if (table[i].code == gfx) return std::uint8_t(i);
        return 0xff;
    };
    each_layer(pcs, worn, [&](int layer, const Worn&, const Piece& p) {
        if (p.component == 1) {
            static constexpr int kTier[10] = { 0, 0, 1, 2, 3, 0, 0, 0, 4, 5 };   // TR LG RA LA .. S1 S2
            a[std::size_t(layer)] = std::uint8_t(1 + p.tiers[std::size_t(kTier[layer])]);
        } else a[std::size_t(layer)] = index(p.gfx);
    });
    return a;
}

// An item's colour, a Colors.txt index or -1 (FUN_0062c100): a unique's
// UniqueItems chrtransform, a set item's SetItems chrtransform; magic, rare
// and crafted items the first suffix with a transformcolor, else the first
// prefix, else the class automod (AutoMagic). Uniques and sets by row
// without separators, affixes by raw row (the save's ids).
struct Colours {
    std::vector<std::string> codes;                            // Colors.txt Code, by index
    std::vector<std::string> unique, set, prefix, suffix, automod;
    [[nodiscard]] int at(const std::vector<std::string>& v, int row) const {
        if (row < 0 || std::size_t(row) >= v.size() || v[std::size_t(row)].empty()) return -1;
        for (std::size_t i = 0; i < codes.size(); ++i) if (codes[i] == v[std::size_t(row)]) return int(i);
        return -1;
    }
    [[nodiscard]] int of(int quality, int unique_id, int set_id, int pre, int suf, const std::array<int, 6>& rare, int class_affix) const {
        if (quality == 7) return at(unique, unique_id);
        if (quality == 5) return at(set, set_id);
        if (quality != 4 && quality != 6 && quality != 8) return -1;
        const std::array<int, 3> sufs = quality == 4 ? std::array<int, 3>{ suf, 0, 0 } : std::array<int, 3>{ rare[1], rare[3], rare[5] };
        const std::array<int, 3> pres = quality == 4 ? std::array<int, 3>{ pre, 0, 0 } : std::array<int, 3>{ rare[0], rare[2], rare[4] };
        for (int s : sufs) if (s > 0) if (const int c = at(suffix, s); c >= 0) return c;
        for (int p : pres) if (p > 0) if (const int c = at(prefix, p); c >= 0) return c;
        return at(automod, class_affix);   // ponytail: row = the save's class affix id, untested (no worn example)
    }
};

// Each layer's tint (the d2s header's 16 bytes at 0x98): (Transform x 32
// + colour + 1) & 0xff, 0xff with no colour or Transform 0, 3 or 4.
// Transform 8 wraps below 0x20 (bugs.md #12); tint_of reads it back.
inline std::array<std::uint8_t, 16> tints(const std::unordered_map<std::string, Piece>& pcs, const std::vector<Worn>& worn) {
    std::array<std::uint8_t, 16> a;
    a.fill(0xff);
    each_layer(pcs, worn, [&](int layer, const Worn& w, const Piece& p) {
        const int t = p.transform;
        a[std::size_t(layer)] = w.colour < 0 || t <= 0 || t == 3 || t == 4 || t > 8 ? 0xff : std::uint8_t((t * 32 + w.colour + 1) & 0xff);
    });
    return a;
}
// A tint byte's colormap set and colour; false for none.
struct Tint { int transform = 0, colour = 0; };
inline bool tint_of(std::uint8_t b, Tint& t) {
    if (b == 0xff || b == 0) return false;
    t.transform = (b - 1) >> 5; t.colour = (b - 1) & 31;
    if (t.transform == 0) t.transform = 8;   // the wrap (bugs.md #12)
    return t.colour < 21;
}

}  // namespace d2d::compcode
