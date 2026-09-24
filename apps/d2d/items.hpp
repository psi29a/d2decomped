// Item hover text, property lines and vendor prices.
#pragma once

#include "ai.hpp"

namespace {

// An item's hover text, top line first, in D2's quality colours. Name IDs
// resolve as verified against 19 real saves (test data): unique/set ID =
// UniqueItems/SetItems row without "Expansion" separators; magic
// prefix/suffix ID = MagicPrefix/MagicSuffix raw data row (row 0 is the
// blank "none"); rare names = RarePrefix row (id - 156) + RareSuffix row
// (id - 1); runeword ID = rank of its RunewordN + 27.
// ponytail: no requirements, durability, set bonus lines or weapon damage.
struct TextLine { std::string text; std::array<std::uint8_t, 3> rgb; };

// D2's printf subset in its strings: %d, %+d, %s, %%. Args in order.
std::string d2_format(std::string_view f, std::initializer_list<std::variant<std::int64_t, std::string>> args) {
    std::string out;
    auto a = args.begin();
    for (std::size_t i = 0; i < f.size(); ++i) {
        if (f[i] != '%' || i + 1 == f.size()) { out += f[i]; continue; }
        if (f[i + 1] == '%') { out += '%'; ++i; continue; }
        const bool plus = f[i + 1] == '+';
        const std::size_t k = i + (plus ? 2 : 1);
        if (k >= f.size() || (f[k] != 'd' && f[k] != 's' && f[k] != 'i')) { out += f[i]; continue; }
        if (a != args.end()) {
            if (const auto* n = std::get_if<std::int64_t>(&*a)) out += (plus && *n >= 0 ? "+" : "") + std::to_string(*n);
            else out += std::get<std::string>(*a);
            ++a;
        }
        i = k;
    }
    return out;
}

// An item's property list as the tooltip's text lines, by ItemStatCost
// descfunc (the column semantics the game's item-description code uses),
// highest descpriority first. dgrp groups (all resistances, all
// attributes) collapse into one line when every member is present with
// the same value; min/max damage pairs become "Adds X-Y ..." and 17/18
// "+X% Enhanced Damage", as the game hard-codes them.
// ponytail: no descfunc 17/18 (time-of-day), 22/23 (monster types);
// charges/skill lines use the skill's string key.
std::vector<std::string> prop_lines(const Scene& s, std::vector<d2d::d2s::ItemProp> props, int clvl) {
    auto str = [&](std::string_view key) {
        if (key.empty()) return std::string{};
        const auto v = lookup_string(s, key);
        std::string t = v ? u16_to_latin1(*v) : std::string(key);
        while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
        return t;
    };
    auto desc = [&](int stat) -> const Scene::StatDesc* {
        return stat >= 0 && std::size_t(stat) < s.stat_desc.size() ? &s.stat_desc[std::size_t(stat)] : nullptr;
    };
    auto skill = [&](int id) {
        return id >= 0 && std::size_t(id) < s.skill_name.size() ? str(s.skill_name[std::size_t(id)]) : std::string{};
    };
    // One stat list, like the unit's: repeats of a (stat, param) add up.
    {
        std::vector<d2d::d2s::ItemProp> merged;
        for (const auto& p : props) {
            auto m = std::ranges::find_if(merged, [&](const auto& q) { return q.stat == p.stat && q.param == p.param; });
            if (m != merged.end() && p.stat != 204) m->value += p.value;      // 204: charges don't add
            else merged.push_back(p);
        }
        props = std::move(merged);
    }
    struct Out { int prio; std::string text; };
    std::vector<Out> out;
    auto value_of = [&](int stat) -> std::optional<std::int64_t> {
        for (const auto& p : props) if (p.stat == stat) return p.value;
        return std::nullopt;
    };
    std::unordered_set<int> done;
    // Hard-coded pairs.
    auto range = [&](int lo, int hi, const char* range_key, const char* single_key, int prio, int len_stat = -1) {
        const auto a = value_of(lo), b = value_of(hi);
        if (!a || !b) return;
        std::int64_t x = *a, y = *b;
        std::optional<std::int64_t> secs;
        if (len_stat >= 0) {
            const auto len = value_of(len_stat).value_or(0);
            x = x * len / 256; y = y * len / 256; secs = len / 25;
            done.insert(len_stat);
        }
        std::string t = x == y && single_key ? (secs ? d2_format(str(single_key), { x, *secs })
                                                     : d2_format(str(single_key), { x }))
                      : secs ? d2_format(str(range_key), { x, y, *secs }) : d2_format(str(range_key), { x, y });
        out.push_back({ prio, std::move(t) });
        done.insert(lo); done.insert(hi);
    };
    range(21, 22, "strModMinDamageRange", nullptr, 127);
    // Two-handed/thrown copies of min/max damage only show without the base.
    if (value_of(21)) { done.insert(23); done.insert(159); }
    if (value_of(22)) { done.insert(24); done.insert(160); }
    range(48, 49, "strModFireDamageRange", "strModFireDamage", 102);
    range(50, 51, "strModLightningDamageRange", "strModLightningDamage", 99);
    range(52, 53, "strModMagicDamageRange", "strModMagicDamage", 104);
    range(54, 55, "strModColdDamageRange", "strModColdDamage", 96);
    if (done.contains(54)) done.insert(56);
    range(57, 58, "strModPoisonDamageRange", "strModPoisonDamage", 92, 59);
    if (const auto a = value_of(17), b = value_of(18); a && b && *a == *b) {
        out.push_back({ 130, "+" + std::to_string(*a) + "% " + str("strModEnhancedDamage") });
        done.insert(17); done.insert(18);
    }
    // dgrp groups.
    std::unordered_map<int, std::vector<int>> groups;
    for (std::size_t i = 0; i < s.stat_desc.size(); ++i)
        if (s.stat_desc[i].dgrp) groups[s.stat_desc[i].dgrp].push_back(int(i));
    for (const auto& [g, members] : groups) {
        std::optional<std::int64_t> v;
        bool same = true;
        for (int m : members) {
            const auto x = value_of(m);
            if (!x || (v && *v != *x)) { same = false; break; }
            v = x;
        }
        if (!same || !v) continue;
        const auto& d = *desc(members.front());
        const std::string s1 = str(*v < 0 ? d.dgrp_neg : d.dgrp_pos);
        std::string t;
        switch (d.dgrp_func) {
            case 1: case 6: case 12: t = d.dgrp_val == 2 ? s1 + " " + d2_format("%+d", { *v }) : d2_format("%+d", { *v }) + " " + s1; break;
            case 3: case 9: t = d.dgrp_val == 2 ? s1 + " " + std::to_string(*v) : std::to_string(*v) + " " + s1; break;
            case 4: case 8: t = d.dgrp_val == 2 ? s1 + " " + d2_format("%+d%%", { *v }) : d2_format("%+d%%", { *v }) + " " + s1; break;
            case 19: t = d2_format(s1, { *v }); break;
            default: t = d2_format(s1, { *v }); break;
        }
        out.push_back({ d.prio, std::move(t) });
        for (int m : members) done.insert(m);
    }
    for (const auto& p : props) {
        if (done.contains(p.stat)) continue;
        const auto* d = desc(p.stat);
        if (!d || d->func == 0) continue;
        std::int64_t v = p.value;
        // Per-level stats (op 2..5 with op param): value * clvl >> param.
        if (d->op >= 2 && d->op <= 5 && d->op_param > 0) v = v * clvl >> d->op_param;
        const std::string s1 = str(v < 0 ? d->neg : d->pos), s2 = str(d->str2);
        auto place = [&](const std::string& num) {
            if (d->val == 0) return s1;
            return d->val == 2 ? s1 + " " + num : num + " " + s1;
        };
        auto with2 = [&](std::string t) { return s2.empty() ? t : t + " " + s2; };
        std::string t;
        switch (d->func) {
            case 1: case 12: t = place(d2_format("%+d", { v })); break;
            case 2: t = place(std::to_string(v) + "%"); break;
            case 3: t = place(std::to_string(v)); break;
            case 4: t = place(d2_format("%+d%%", { v })); break;
            case 5: t = place(std::to_string(v * 100 / 128) + "%"); break;
            case 6: t = with2(place(d2_format("%+d", { v }))); break;
            case 7: t = with2(place(std::to_string(v) + "%")); break;
            case 8: t = with2(place(d2_format("%+d%%", { v }))); break;
            case 9: t = with2(place(std::to_string(v))); break;
            case 10: t = with2(place(std::to_string(v * 100 / 128) + "%")); break;
            case 11: t = d2_format(s1, { std::int64_t(1), v ? 100 / v : 0 }); break;
            case 13: t = d2_format("%+d", { v }) + " " + (p.param >= 0 && p.param < 7
                         ? str(s.class_strs[std::size_t(p.param)].all_skills) : std::string{}); break;
            case 14: {
                const int cls = p.param >> 3, tab = p.param & 7;
                if (cls < 0 || cls >= 7 || tab > 2) break;
                const auto& cs = s.class_strs[std::size_t(cls)];
                t = d2_format(str(cs.tab[tab]), { v }) + " " + str(cs.only);
                break;
            }
            case 15: t = d2_format(s1, { v, std::int64_t(p.param & 63), skill(p.param >> 6) }); break;
            case 16: t = d2_format(s1, { v, skill(p.param) }); break;
            case 19: t = d2_format(s1, { v }); break;
            case 20: t = place(std::to_string(-v) + "%"); break;
            case 21: t = place(std::to_string(-v)); break;
            case 24:                                       // descstr is just "(%d/%d Charges)"
                t = "Level " + std::to_string(p.param & 63) + " " + skill(p.param >> 6) + " "
                  + d2_format(s1, { v & 255, v >> 8 });
                break;
            case 27: {
                const int c = std::size_t(p.param) < s.skill_class.size() ? s.skill_class[std::size_t(p.param)] : -1;
                t = d2_format("%+d", { v }) + " to " + skill(p.param)
                  + (c >= 0 ? " " + str(s.class_strs[std::size_t(c)].only) : "");
                break;
            }
            case 28: t = d2_format("%+d", { v }) + " to " + skill(p.param); break;
            default: break;
        }
        if (!t.empty()) out.push_back({ d->prio, std::move(t) });
    }
    std::ranges::stable_sort(out, [](const Out& a, const Out& b) { return a.prio > b.prio; });
    std::vector<std::string> lines;
    for (auto& o : out) lines.push_back(std::move(o.text));
    return lines;
}
constexpr std::array<std::uint8_t, 3> kTxtWhite{ 255, 255, 255 }, kTxtBlue{ 105, 105, 255 },
    kTxtGreen{ 0, 255, 0 }, kTxtGold{ 199, 179, 119 }, kTxtYellow{ 255, 255, 100 },
    kTxtOrange{ 255, 168, 0 }, kTxtGrey{ 105, 105, 105 };

// FUN_0062efb0 (D2Common's transaction cost), for buying (sell == false,
// the vendor's price) and selling (the vendor pays). Base = the item's
// cost (items record +0xe0), plus per quality (mult, add) extras:
// low quality -base/2, magic prefix + suffix, rare/crafted its six
// affixes, set/unique their cost mult/add (extra = base*mult/1024 + add);
// + half the cost of each socketed item (FUN_006292f0); ethereal sells
// at a quarter. Then npc.txt: * buy/sell mult / 1024 and each questflag's
// mult when that quest is done; * quantity; selling caps at "max buy"
// for the difficulty.
// ponytail: no charges/books/ammo branches, automagic affix, durability
// or the reduced-prices stat.

std::vector<TextLine> item_lines(const Scene& s, const d2d::d2s::Item& it, int clvl) {
    auto str = [&](std::string_view key) {
        if (key.empty()) return std::string{};
        const auto v = lookup_string(s, key);
        return v ? u16_to_latin1(*v) : std::string(key);
    };
    auto at = [](const std::vector<std::string>& v, int i) -> std::string_view {
        return i >= 0 && std::size_t(i) < v.size() ? std::string_view(v[std::size_t(i)]) : std::string_view{};
    };
    const auto& nm = s.item_names;
    const auto info = s.rules.item_info.find(it.code);
    const std::string base = str(info != s.rules.item_info.end() && !info->second.namestr.empty()
                                     ? std::string_view(info->second.namestr) : std::string_view(it.code));
    std::vector<TextLine> out;
    auto two = [&](std::string name, std::array<std::uint8_t, 3> c) {
        if (name.empty()) { out.push_back({ base, c }); return; }
        out.push_back({ std::move(name), c });
        out.push_back({ base, c });
    };
    auto join = [](std::string a, const std::string& b) {
        if (a.empty()) return b;
        if (b.empty()) return a;
        return a + " " + b;
    };
    if (it.runeword) {
        const int rank = it.runeword_id == 2718 ? 48 - 27 : it.runeword_id - 27;   // 2718: Delirium's odd ID
        out.push_back({ str(at(nm.runeword, rank)), kTxtGold });
        out.push_back({ base, kTxtGrey });
    } else switch (it.quality) {
        case 1: {
            static constexpr const char* kLow[] = { "Crude", "Cracked", "Damaged", "Low Quality" };
            out.push_back({ join(str(it.qsub >= 0 && it.qsub < 4 ? kLow[it.qsub] : ""), base), kTxtWhite });
            break;
        }
        case 3: out.push_back({ join(str("Hiquality"), base), kTxtWhite }); break;
        case 4: out.push_back({ join(join(str(at(nm.prefix, it.prefix)), base), str(at(nm.suffix, it.suffix))),
                                kTxtBlue }); break;
        case 5: two(str(at(nm.set, it.set_id)), kTxtGreen); break;
        case 7: two(str(at(nm.unique, it.unique_id)), kTxtGold); break;
        case 6: case 8:
            two(join(str(at(nm.rare_pre, it.rare1 - 156)), str(at(nm.rare_suf, it.rare2 - 1))),
                it.quality == 6 ? kTxtYellow : kTxtOrange);
            break;
        default: out.push_back({ base, it.ethereal || it.socketed ? kTxtGrey : kTxtWhite }); break;
    }
    if (it.personalized && !it.owner.empty()) out.front().text = it.owner + "'s " + out.front().text;
    if (it.defense >= 0) {
        // Shown with the item's own +% and flat defence applied (16, 31).
        std::int64_t ed = 0, flat = 0;
        for (const auto& p : it.props) {
            if (p.stat == 16) ed += p.value;
            if (p.stat == 31) flat += p.value;
            if (p.stat == 214) flat += p.value * clvl / 8;         // armor per level
        }
        const auto def = std::int64_t(it.defense) * (100 + ed) / 100 + flat;
        out.push_back({ str("ItemStats1h") + " " + std::to_string(def), def != it.defense ? kTxtBlue : kTxtWhite });
    }
    if (it.quantity >= 0) out.push_back({ "Quantity: " + std::to_string(it.quantity), kTxtWhite });
    auto props = it.props;
    for (const auto& j : it.socketed_items) {
        const auto sp = socket_props(s, it, j);
        props.insert(props.end(), sp.begin(), sp.end());
    }
    for (auto& l : prop_lines(s, std::move(props), clvl)) out.push_back({ std::move(l), kTxtBlue });
    if (it.ethereal) out.push_back({ "Ethereal", kTxtBlue });
    if (it.socketed) out.push_back({ "Socketed (" + std::to_string(it.sockets) + ")", kTxtBlue });
    return out;
}

// Hover text box: lines centred over [x0, x1], bottom on `bottom` (below
// `top` instead when it would leave the screen), on a darkened backdrop.
void draw_hover_text(std::vector<std::uint8_t>& fb, const Scene& s, const std::vector<TextLine>& lines,
                     int x0, int x1, int top, int bottom) {
    if (lines.empty()) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int lh = 16;                                   // font16 cell height
    int w = 0;
    for (const auto& l : lines) w = std::max(w, s.font.measure(l.text));
    const int h = lh * int(lines.size());
    int bx = std::clamp((x0 + x1) / 2 - w / 2 - 2, 0, std::max(0, int(kW) - w - 4));
    int by = bottom - h - 2;
    if (by < 0) by = std::min(top, int(kH) - h - 4);
    for (int y = std::max(0, by); y < std::min(int(kH), by + h + 4); ++y)
        for (int x = bx; x < std::min(int(kW), bx + w + 4); ++x) {
            auto* p = &fb[(std::size_t(y) * kW + std::size_t(x)) * 4];
            p[0] = std::uint8_t(p[0] / 4); p[1] = std::uint8_t(p[1] / 4); p[2] = std::uint8_t(p[2] / 4);
        }
    int y = by + 2;
    for (const auto& l : lines) {
        const int lw = s.font.measure(l.text);
        s.font.draw_tinted(fb, kW, kH, pal, bx + 2 + (w - lw) / 2, y, l.text, l.rgb[0], l.rgb[1], l.rgb[2]);
        y += lh;
    }
}

}  // namespace
