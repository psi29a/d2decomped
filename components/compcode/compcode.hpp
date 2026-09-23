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

}  // namespace d2d::compcode
