// SPDX-License-Identifier: GPL-3.0-or-later
// A classic (version 0) item's mods: FUN_0065fe10's own table, not the
// Properties.txt funcs (docs/research/re/drops.md "A classic item's mods").
#include "item_gen.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::rules {

namespace {

using d2d::d2s::ItemProp;

// The table's handlers (FUN_0065fe10 -> [row * 8 + 0x745b58]).
enum class Handler {
    None,           // 0x65e440: mov eax, 1 / ret 0x1c
    Add,            // FUN_0065d1c0 (and FUN_0065db90, FUN_0065e450): FUN_0065cf40, flag 0
    AddAc,          // FUN_0065d2b0: FUN_0065cf40, flag 1 (the ac rule on any kind)
    PerLevel,       // FUN_0065d110: the param, no draw
    Sock,           // FUN_0065d220
    Indestruct,     // FUN_0065d270
    ResAll,         // FUN_0065d310
    ResAllMax,      // FUN_0065d4b0
    DamageMin,      // FUN_0065d650
    DamageMax,      // FUN_0065d7d0
    DamagePercent,  // FUN_0065d950
    Charged,        // FUN_0065dbc0
    ByTime,         // FUN_0065dd80
    MinMax,         // FUN_0065de40
    MinMaxLength,   // FUN_0065e070
    Skill,          // FUN_0065e170
    ElementSkill,   // FUN_0065e230
    ClassSkills,    // FUN_0065e2d0
};

struct Row { std::string_view code; Handler handler; int stat; };

// Rows 0..243, keyed by the 1.14d Properties.txt code of each row (read
// from game.exe at 0x745b58, 8 bytes a row). A code past row 243 isn't
// here and does nothing: row 244's handler is null, and rows 245..267
// would read the func table at 0x7462f8 (bugs.md #137), which no
// version-0 data names.
constexpr Row kRows[] = {
    { "ac", Handler::Add, 31 }, { "ac-miss", Handler::Add, 32 }, { "ac-hth", Handler::Add, 33 },
    { "red-dmg", Handler::Add, 34 }, { "red-dmg%", Handler::Add, 36 }, { "ac%", Handler::AddAc, 16 },
    { "red-mag", Handler::Add, 35 }, { "str", Handler::Add, 0 }, { "dex", Handler::Add, 2 },
    { "vit", Handler::Add, 3 }, { "enr", Handler::Add, 1 }, { "mana", Handler::Add, 9 },
    { "mana%", Handler::AddAc, 77 }, { "hp", Handler::Add, 7 }, { "hp%", Handler::AddAc, 76 },
    { "att", Handler::Add, 19 }, { "block", Handler::Add, 20 }, { "cold-min", Handler::Add, 54 },
    { "cold-max", Handler::Add, 55 }, { "cold-len", Handler::Add, 56 }, { "fire-min", Handler::Add, 48 },
    { "fire-max", Handler::Add, 49 }, { "ltng-min", Handler::Add, 50 }, { "ltng-max", Handler::Add, 51 },
    { "pois-min", Handler::Add, 57 }, { "pois-max", Handler::Add, 58 }, { "pois-len", Handler::Add, 59 },
    { "dmg-min", Handler::DamageMin, 21 }, { "dmg-max", Handler::DamageMax, 22 },
    { "dmg%", Handler::DamagePercent, -1 }, { "dmg-to-mana", Handler::AddAc, 114 }, { "res-fire", Handler::Add, 39 },
    { "res-fire-max", Handler::Add, 40 }, { "res-ltng", Handler::Add, 41 }, { "res-ltng-max", Handler::Add, 42 },
    { "res-cold", Handler::Add, 43 }, { "res-cold-max", Handler::Add, 44 }, { "res-mag", Handler::Add, 37 },
    { "res-mag-max", Handler::Add, 38 }, { "res-pois", Handler::Add, 45 }, { "res-pois-max", Handler::Add, 46 },
    { "res-all", Handler::ResAll, -1 }, { "res-all-max", Handler::ResAllMax, -1 },
    { "abs-fire%", Handler::Add, 142 }, { "abs-fire", Handler::Add, 143 }, { "abs-ltng%", Handler::Add, 144 },
    { "abs-ltng", Handler::Add, 145 }, { "abs-mag%", Handler::Add, 146 }, { "abs-mag", Handler::Add, 147 },
    { "abs-cold%", Handler::Add, 148 }, { "abs-cold", Handler::Add, 149 }, { "dur", Handler::Add, 73 },
    { "dur%", Handler::AddAc, 75 }, { "regen", Handler::Add, 74 }, { "thorns", Handler::Add, 78 },
    { "swing1", Handler::Add, 93 }, { "swing2", Handler::Add, 93 }, { "swing3", Handler::Add, 93 },
    { "gold%", Handler::Add, 79 }, { "mag%", Handler::Add, 80 }, { "knock", Handler::Add, 81 },
    { "regen-stam", Handler::AddAc, 28 }, { "regen-mana", Handler::AddAc, 27 }, { "stam", Handler::Add, 11 },
    { "time", Handler::Add, 82 }, { "manasteal", Handler::Add, 62 }, { "lifesteal", Handler::Add, 60 },
    { "ama", Handler::ClassSkills, 83 }, { "pal", Handler::ClassSkills, 84 }, { "nec", Handler::ClassSkills, 85 },
    { "sor", Handler::ClassSkills, 86 }, { "bar", Handler::ClassSkills, 87 }, { "herb", Handler::Add, 88 },
    { "light", Handler::Add, 89 }, { "color", Handler::Add, 90 }, { "ease", Handler::Add, 91 },
    { "move1", Handler::Add, 96 }, { "move2", Handler::Add, 96 }, { "move3", Handler::Add, 96 },
    { "balance1", Handler::Add, 99 }, { "balance2", Handler::Add, 99 }, { "balance3", Handler::Add, 99 },
    { "block1", Handler::Add, 102 }, { "block2", Handler::Add, 102 }, { "block3", Handler::Add, 102 },
    { "cast1", Handler::Add, 105 }, { "cast2", Handler::Add, 105 }, { "cast3", Handler::Add, 105 },
    { "res-pois-len", Handler::Add, 110 }, { "dmg", Handler::Add, 111 }, { "howl", Handler::Add, 112 },
    { "stupidity", Handler::Add, 113 }, { "ignore-ac", Handler::Add, 115 }, { "reduce-ac", Handler::Add, 116 },
    { "noheal", Handler::Add, 117 }, { "half-freeze", Handler::Add, 118 }, { "att%", Handler::AddAc, 119 },
    { "dmg-ac", Handler::Add, 120 }, { "dmg-demon", Handler::AddAc, 121 }, { "dmg-undead", Handler::AddAc, 122 },
    { "att-demon", Handler::Add, 123 }, { "att-undead", Handler::Add, 124 }, { "throw", Handler::Add, 125 },
    { "fireskill", Handler::ElementSkill, 126 }, { "allskills", Handler::Add, 127 },
    { "light-thorns", Handler::Add, 128 }, { "freeze", Handler::Add, 134 }, { "openwounds", Handler::Add, 135 },
    { "crush", Handler::Add, 136 }, { "kick", Handler::Add, 137 }, { "mana-kill", Handler::Add, 138 },
    { "demon-heal", Handler::Add, 139 }, { "bloody", Handler::Add, 140 }, { "deadly", Handler::Add, 141 },
    { "slow", Handler::Add, 150 }, { "nofreeze", Handler::Add, 153 }, { "stamdrain", Handler::Add, 154 },
    { "reanimate", Handler::Add, 155 }, { "pierce", Handler::Add, 156 }, { "magicarrow", Handler::Add, 157 },
    { "explosivearrow", Handler::Add, 158 }, { "dru", Handler::ClassSkills, 179 },
    { "ass", Handler::ClassSkills, 180 }, { "skill", Handler::Skill, 107 }, { "skilltab", Handler::None, -1 },
    { "aura", Handler::None, -1 }, { "att-skill", Handler::None, -1 }, { "hit-skill", Handler::None, -1 },
    { "gethit-skill", Handler::None, -1 }, { "gembonus", Handler::None, -1 }, { "regen-dur", Handler::None, -1 },
    { "fire-fx", Handler::None, -1 }, { "ltng-fx", Handler::None, -1 }, { "sock", Handler::Sock, 194 },
    { "dmg-fire", Handler::MinMax, 48 }, { "dmg-ltng", Handler::MinMax, 50 }, { "dmg-mag", Handler::MinMax, 52 },
    { "dmg-cold", Handler::MinMaxLength, 54 }, { "dmg-pois", Handler::MinMaxLength, 57 },
    { "dmg-throw", Handler::MinMax, 159 }, { "dmg-norm", Handler::MinMax, 21 }, { "ac/lvl", Handler::PerLevel, 214 },
    { "ac%/lvl", Handler::PerLevel, 215 }, { "hp/lvl", Handler::PerLevel, 216 },
    { "mana/lvl", Handler::PerLevel, 217 }, { "dmg/lvl", Handler::PerLevel, 218 },
    { "dmg%/lvl", Handler::PerLevel, 219 }, { "str/lvl", Handler::PerLevel, 220 },
    { "dex/lvl", Handler::PerLevel, 221 }, { "enr/lvl", Handler::PerLevel, 222 },
    { "vit/lvl", Handler::PerLevel, 223 }, { "att/lvl", Handler::PerLevel, 224 },
    { "att%/lvl", Handler::PerLevel, 225 }, { "dmg-cold/lvl", Handler::PerLevel, 226 },
    { "dmg-fire/lvl", Handler::PerLevel, 227 }, { "dmg-ltng/lvl", Handler::PerLevel, 228 },
    { "dmg-pois/lvl", Handler::PerLevel, 229 }, { "res-cold/lvl", Handler::PerLevel, 230 },
    { "res-fire/lvl", Handler::PerLevel, 231 }, { "res-ltng/lvl", Handler::PerLevel, 232 },
    { "res-pois/lvl", Handler::PerLevel, 233 }, { "abs-cold/lvl", Handler::PerLevel, 234 },
    { "abs-fire/lvl", Handler::PerLevel, 235 }, { "abs-ltng/lvl", Handler::PerLevel, 236 },
    { "abs-pois/lvl", Handler::PerLevel, 237 }, { "thorns/lvl", Handler::PerLevel, 238 },
    { "gold%/lvl", Handler::PerLevel, 239 }, { "mag%/lvl", Handler::PerLevel, 240 },
    { "regen-stam/lvl", Handler::PerLevel, 241 }, { "stam/lvl", Handler::PerLevel, 242 },
    { "dmg-dem/lvl", Handler::PerLevel, 243 }, { "dmg-und/lvl", Handler::PerLevel, 244 },
    { "att-dem/lvl", Handler::PerLevel, 245 }, { "att-und/lvl", Handler::PerLevel, 246 },
    { "crush/lvl", Handler::PerLevel, 247 }, { "wounds/lvl", Handler::PerLevel, 248 },
    { "kick/lvl", Handler::PerLevel, 249 }, { "deadly/lvl", Handler::PerLevel, 250 },
    { "gems%/lvl", Handler::None, -1 }, { "rep-dur", Handler::PerLevel, 252 },
    { "rep-quant", Handler::PerLevel, 253 }, { "stack", Handler::Add, 254 }, { "item%", Handler::None, -1 },
    { "dmg-slash", Handler::None, -1 }, { "dmg-slash%", Handler::None, -1 }, { "dmg-crush", Handler::None, -1 },
    { "dmg-crush%", Handler::None, -1 }, { "dmg-thrust", Handler::None, -1 }, { "dmg-thrust%", Handler::None, -1 },
    { "abs-slash", Handler::None, -1 }, { "abs-crush", Handler::None, -1 }, { "abs-thrust", Handler::None, -1 },
    { "abs-slash%", Handler::None, -1 }, { "abs-crush%", Handler::None, -1 }, { "abs-thrust%", Handler::None, -1 },
    { "ac/time", Handler::ByTime, 268 }, { "ac%/time", Handler::ByTime, 269 }, { "hp/time", Handler::ByTime, 270 },
    { "mana/time", Handler::ByTime, 271 }, { "dmg/time", Handler::ByTime, 272 },
    { "dmg%/time", Handler::ByTime, 273 }, { "str/time", Handler::ByTime, 274 },
    { "dex/time", Handler::ByTime, 275 }, { "enr/time", Handler::ByTime, 276 }, { "vit/time", Handler::ByTime, 277 },
    { "att/time", Handler::ByTime, 278 }, { "att%/time", Handler::ByTime, 279 },
    { "dmg-cold/time", Handler::ByTime, 280 }, { "dmg-fire/time", Handler::ByTime, 281 },
    { "dmg-ltng/time", Handler::ByTime, 282 }, { "dmg-pois/time", Handler::ByTime, 283 },
    { "res-cold/time", Handler::ByTime, 284 }, { "res-fire/time", Handler::ByTime, 285 },
    { "res-ltng/time", Handler::ByTime, 286 }, { "res-pois/time", Handler::ByTime, 287 },
    { "abs-cold/time", Handler::ByTime, 288 }, { "abs-fire/time", Handler::ByTime, 289 },
    { "abs-ltng/time", Handler::ByTime, 290 }, { "abs-pois/time", Handler::ByTime, 291 },
    { "gold%/time", Handler::ByTime, 292 }, { "mag%/time", Handler::ByTime, 293 },
    { "regen-stam/time", Handler::ByTime, 294 }, { "stam/time", Handler::ByTime, 295 },
    { "dmg-dem/time", Handler::ByTime, 296 }, { "dmg-und/time", Handler::ByTime, 297 },
    { "att-dem/time", Handler::ByTime, 298 }, { "att-und/time", Handler::ByTime, 299 },
    { "crush/time", Handler::ByTime, 300 }, { "wounds/time", Handler::ByTime, 301 },
    { "kick/time", Handler::ByTime, 302 }, { "deadly/time", Handler::ByTime, 303 },
    { "gems%/time", Handler::None, -1 }, { "pierce-fire", Handler::None, -1 }, { "pierce-ltng", Handler::None, -1 },
    { "pierce-cold", Handler::None, -1 }, { "pierce-pois", Handler::None, -1 }, { "dmg-mon", Handler::None, -1 },
    { "dmg%-mon", Handler::None, -1 }, { "att-mon", Handler::None, -1 }, { "att%-mon", Handler::None, -1 },
    { "ac-mon", Handler::None, -1 }, { "ac%-mon", Handler::None, -1 }, { "indestruct", Handler::Indestruct, -1 },
    { "charged", Handler::Charged, 204 },
};

const Row* find_row(std::string_view code) {
    for (const auto& row : kRows)
        if (row.code == code) return &row;
    return nullptr;
}

}  // namespace

// The draw (0x65cf54..0x65cf8d): min == max is min, no draw; else the two in
// order and min + rand(max - min) on the item's own seed, the + 1 added only
// for version 1 or more (`cmp ax, 1` / `jb` at 0x65cf71), so a classic mod
// never rolls its max. Values add to the item's list (FUN_00627030) and
// a 0 adds nothing. The ac rule (FUN_0065ccc0) raises an armor's defence
// on stat 16 / 31 as the expansion path's does; on stats 0x11 / 0x12 /
// 0x15 / 0x16 it only re-sets the base damage stats, which d2d keeps in
// the base. The remove flag (FUN_0065fec0's last argument) is always 0 in
// item making and isn't modelled.
void apply_classic_mod(const Tables& tables, const Mod& mod, std::vector<ItemProp>& out, Rng& rng, const ModItem& target) {
    using namespace d2d::d2s;
    const Row* row = find_row(lower_case(mod.code));
    if (!row) return;
    const auto skill = [&](const std::string& name) {
        const auto found = tables.skill_id.find(name);
        return found != tables.skill_id.end() ? found->second : 0;
    };
    const bool numeric = !mod.param.empty() && ((mod.param[0] >= '0' && mod.param[0] <= '9') || mod.param[0] == '-');
    const int par = mod.param.empty() ? 0 : numeric ? std::atoi(mod.param.c_str()) : skill(mod.param);
    const bool skill_row = par >= 0 && std::size_t(par) < tables.skill_levels.size();   // [0x744304] + 0xba0
    const auto draw = [&] {
        if (mod.min == mod.max) return mod.min;
        const int low = std::min(mod.min, mod.max), high = std::max(mod.min, mod.max);
        return low + rng(high - low);
    };
    const auto put = [&](int stat, int param, int amount) {
        if (amount != 0) out.push_back({ stat, param, amount });
    };
    Item& item = *target.item;
    const ItemBase* base = target.base;
    const auto ac_rule = [&](int stat) {           // FUN_0065ccc0, stat 16 / 31
        if ((stat == kArmorPercent || stat == kArmorClass) && target.kind == 1 && base && base->maxac != 0)
            item.defense = std::max(item.defense + 1, base->maxac + 1);
    };
    const auto add = [&](int stat, bool always_ac) {   // FUN_0065cf40
        const int amount = draw();
        if (always_ac || target.raise) ac_rule(stat);
        if (amount == 0) return;
        put(stat, 0, amount);
        if (stat == kPoisonMaxDamage) put(kPoisonCount, 0, 1);
    };
    const bool weapon = target.kind == 2;          // FUN_00629bb0(item, 0x2d)
    // FUN_0065d650 / FUN_0065d7d0 / FUN_0065de40's hands: the one-hand stat
    // unless a weapon whose one-hand byte is 0 and two-hand byte isn't, the
    // two-hand stat unless a weapon the other way round, the throw stat
    // unless a weapon that isn't throwable.
    const auto hands = [&](int one_byte, int two_byte, int other_two, int other_one) {
        struct { bool one, two, thrown; } use{ !(weapon && one_byte == 0 && two_byte != 0), !(weapon && other_two == 0 && other_one != 0),
                                               !weapon || target.throwable };
        return use;
    };
    const int min_1h = base ? base->min_1h : 0, max_1h = base ? base->max_1h : 0;
    const int min_2h = base ? base->min_2h : 0, max_2h = base ? base->max_2h : 0;
    switch (row->handler) {
        case Handler::None: break;
        case Handler::Add: add(row->stat, false); break;
        case Handler::AddAc: add(row->stat, true); break;
        case Handler::PerLevel: put(row->stat, 0, par); break;
        case Handler::Sock: {                      // flag 0x800 (FUN_006280d0), then FUN_0062be00(item, param)
            item.socketed = true;
            // FUN_0062be00: the grid (inventory width x height, 6 at most; 0
            // sets nothing) and FUN_0062bc20 (gemsockets, the type's MaxSock
            // by ilvl, as func 14 has it) cap the param, taken as 1 at least.
            const auto info = tables.item_info.find(item.code);
            const int grid = info != tables.item_info.end() ? info->second.width * info->second.height : 0;
            if (grid <= 0) break;
            int most = std::min(grid, 6);
            const auto type = info->second.type.empty() ? tables.types.end() : tables.types.find(info->second.type);
            if (base && type != tables.types.end()) {
                const int ilvl = item.ilvl;
                most = std::min(most, std::min(base->gem_sockets, type->second.max_sock[std::size_t(ilvl <= 25 ? 0 : ilvl <= 40 ? 1 : 2)]));
            }
            item.sockets = std::min(std::max(par, 1), most);
            break;
        }
        case Handler::Indestruct:                  // stats 0x49 / 0x48 set to 0 (FUN_00627260)
            item.max_durability = 0;
            item.durability = 0;
            break;
        case Handler::ResAll:                      // version 0 (0x65d42b): four FUN_0065cf40, a draw each
            for (const int stat : { kFireResist, kLightningResist, kColdResist, kPoisonResist }) add(stat, false);
            break;
        case Handler::ResAllMax:
            for (const int stat : { kFireResist + 1, kLightningResist + 1, kColdResist + 1, kPoisonResist + 1 }) add(stat, false);
            break;
        case Handler::DamageMin: {                 // one draw, through FUN_0065d070: no hold
            const int amount = draw();
            const auto use = hands(min_1h, min_2h, min_2h, min_1h);
            if (use.one) put(kMinDamage, 0, amount);
            if (use.two) put(kSecondaryMinDamage, 0, amount);
            if (use.thrown) put(kThrowMinDamage, 0, amount);
            break;
        }
        case Handler::DamageMax: {
            const int amount = draw();
            const auto use = hands(max_1h, max_2h, max_2h, max_1h);
            if (use.one) put(kMaxDamage, 0, amount);
            if (use.two) put(kSecondaryMaxDamage, 0, amount);
            if (use.thrown) put(kThrowMaxDamage, 0, amount);
            break;
        }
        case Handler::DamagePercent:               // version 0 (0x65db44): FUN_0065cf40 with flag 1, stat 0x11 then 0x12
            add(kMaxDamagePercent, true);
            add(kMinDamagePercent, true);
            break;
        case Handler::Charged: {                   // func 19's level, charges and packing (FUN_0065f6a0)
            if (!skill_row) break;
            const auto [req, most_level] = tables.skill_levels[std::size_t(par)];
            const int ilvl = item.ilvl;
            int level = mod.max;
            if (level == 0) level = std::min(std::max((ilvl - req) / 4 + 1, 1), most_level >= 1 ? most_level : 20);
            else if (level < 0) level = std::max((ilvl - req) / std::max(-(std::max(99 - req, 1) / level), 1), 1);
            const int charges = mod.min == 0 ? 5 : std::clamp(mod.min < 0 ? -mod.min + -mod.min * level / 8 : mod.min, 1, 255);
            const int now = (rng(charges - charges / 8) + charges / 8 + 1) & 0xff;
            out.push_back({ row->stat, par << 6 | (level & 63), now + charges * 256 });
            break;
        }
        case Handler::ByTime:                      // func 18's packing (FUN_0065f870); out of range is fatal there
            put(row->stat, 0, par + ((mod.max + 256) * 1024 + mod.min + 256) * 4);
            break;
        case Handler::MinMax:                      // no draw: min to the stat, max to stat + 1
            if (row->stat == kMinDamage) {         // dmg-norm: the hands by the max bytes, then + 0x103 / + 0xfe
                const auto use = hands(max_1h, max_2h, max_2h, min_1h);
                if (use.one) { put(kMinDamage, 0, mod.min); put(kMaxDamage, 0, mod.max); }
                if (use.two) { put(kSecondaryMinDamage, 0, mod.min); put(kSecondaryMaxDamage, 0, mod.max); }
                if (target.throwable) { put(kThrowMinDamage, 0, mod.min); put(kThrowMaxDamage, 0, mod.max); }   // 0x65df76: throwable, any kind
            } else {
                put(row->stat, 0, mod.min);
                put(row->stat + 1, 0, mod.max);
            }
            break;
        case Handler::MinMaxLength:                // min, max, length; poison counts one whatever they are
            put(row->stat, 0, mod.min);
            put(row->stat + 1, 0, mod.max);
            put(row->stat + 2, 0, par);
            if (row->stat == kPoisonMinDamage) put(kPoisonCount, 0, 1);
            break;
        case Handler::Skill:                       // stat 0x6b, layer the skill
            if (skill_row) put(row->stat, par, draw());
            break;
        case Handler::ElementSkill: put(row->stat, 1, draw()); break;   // stat 0x7e, layer 1 (fire)
        case Handler::ClassSkills: {               // stat 0x53, layer by the row's stat
            const int layer = row->stat >= 0x53 && row->stat <= 0x57 ? row->stat - 0x53 : row->stat == 0xb3 ? 5 : 6;
            put(0x53, layer, draw());
            break;
        }
    }
}

}  // namespace d2d::rules
