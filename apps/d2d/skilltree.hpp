// The skill tree panel ('T'): docs/research/re/skill-tree.md.
#pragma once

#include "panels.hpp"

namespace d2d::app {

// The right panel's right edge and bottom at 800x600 (game.exe works from
// W - panel x and panel y + H - 480).
constexpr int kTreeR = 720, kTreeB = 540;

// A skill's icon box {left, bottom} (FUN_004aaa50 columns, FUN_004aa9c0
// rows); icons are 48x48.
std::pair<int, int> skill_icon_at(const d2d::rules::ClassSkill& sk) {
    static constexpr int kCol[4] = { 0, 0x131, 0xec, 0xa7 };
    static constexpr int kRow[7] = { 0, 0x1a2, 0x15e, 0x11a, 0xd6, 0x91, 0x4d };
    if (sk.col < 1 || sk.col > 3 || sk.row < 1 || sk.row > 6) return { -1000, -1000 };
    return { kTreeR - kCol[sk.col], kTreeB - kRow[sk.row] };
}

// The class skill (0..29) whose icon on tab `tab` is under (mx, my), or -1.
int skill_at(const Scene& s, int cls, int tab, int mx, int my) {
    const auto& list = s.rules.class_skills[std::size_t(cls)];
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].page != tab) continue;
        const auto [x, y] = skill_icon_at(list[i]);
        if (mx > x && mx < x + 0x30 && my > y - 0x30 && my < y) return int(i);
    }
    return -1;
}

// The tab under (mx, my): the strip x R-88..R, split into bands bottom
// (1) to top (3) (FUN_004ab7e0), or 0.
int skill_tab_at(int mx, int my) {
    if (mx < kTreeR - 0x58 || mx > kTreeR) return 0;
    if (my > kTreeB - 0x174 && my < kTreeB - 0x109) return 3;
    if (my > kTreeB - 0x108 && my < kTreeB - 0x9d) return 2;
    if (my > kTreeB - 0x9c && my < kTreeB - 0x31) return 1;
    return 0;
}

// Tab labels, as FUN_004aace0 draws them: font16, centred in R-90..R, at
// baseline 60 + y; "Skill Choices Remaining" (0x1083..0x1085) for every
// class, then the class's tab names line by line.
struct TreeLabel { std::uint16_t id; int y; };
constexpr TreeLabel kTreeHeader[3] = { { 0x1083, 85 }, { 0x1084, 97 }, { 0x1085, 109 } };
const std::vector<TreeLabel> kTreeTabs[7] = {
    { { 0x1088, 210 }, { 0x1089, 222 }, { 0x1086, 234 }, { 0x108a, 318 }, { 0x108b, 330 }, { 0x1086, 342 },
      { 0x108c, 419 }, { 0x108d, 431 }, { 0x1086, 443 } },                                               // Amazon
    { { 0x1099, 216 }, { 0x1087, 228 }, { 0x109a, 324 }, { 0x1087, 336 }, { 0x109b, 430 }, { 0x1087, 442 } },   // Sorceress
    { { 0x1092, 216 }, { 0x1087, 228 }, { 0x1093, 318 }, { 0x1094, 330 }, { 0x1087, 342 }, { 0x1095, 436 } },   // Necromancer
    { { 0x108e, 216 }, { 0x108f, 228 }, { 0x1090, 324 }, { 0x108f, 336 }, { 0x1091, 430 }, { 0x1086, 442 } },   // Paladin
    { { 0x1096, 222 }, { 0x1097, 324 }, { 0x1098, 336 }, { 0x1097, 430 }, { 0x1086, 442 } },                     // Barbarian
    { { 0x57f0, 222 }, { 0x57ee, 324 }, { 0x57ef, 336 }, { 0x57ed, 430 } },                                     // Druid
    { { 0x57f4, 210 }, { 0x57f5, 222 }, { 0x57f2, 324 }, { 0x57f3, 336 }, { 0x57f1, 430 } },                     // Assassin
};

// The panel (FUN_004ac690): background frames 0..3 then 4t..4t+3 at the
// 2x2 spots, the labels, the unspent points (font16 centred in R-65..R-25
// at baseline 140), and each skill of the tab: icon frame IconCel (+1
// while pressed), greyed when it can't take a point (draw colour 5), its
// level at (x + 48, bottom + 12) when learned (4 left for two digits).
// ponytail: grey is a 50% darken; no hover brightening or skill
// description popup, just the name; the game draws 10+ in FontFormal10.
void draw_skill_tree(std::vector<std::uint8_t>& fb, const Scene& s, int cls, int tab,
                     const std::array<std::uint8_t, 30>& lv, const d2d::d2s::Stats& st, int pressed, int mx, int my) {
    if (cls < 0 || cls > 6) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    auto dc6 = [&](const d2d::dc6::Sprite& spr, int frame, int x, int y, int shade = 256) {
        if (frame < 0 || spr.frames_per_direction() <= std::uint32_t(frame)) return;
        const auto& f = spr.frame(0, std::uint32_t(frame));
        blit_sprite(fb, f, pal, x, y - int(f.height) + 1, shade);
    };
    const auto& bg = s.skill_tree_bg[std::size_t(cls)];
    for (const int base : { 0, 4 * tab }) {
        dc6(bg, base + 0, kTreeR - 0x140, kTreeB - 0xe0);
        dc6(bg, base + 1, kTreeR - 0x40, kTreeB - 0xe0);
        dc6(bg, base + 2, kTreeR - 0x140, kTreeB - 0x30);
        dc6(bg, base + 3, kTreeR - 0x40, kTreeB - 0x30);
    }
    auto centred = [&](const d2d::font::Font& f, int x0, int x1, int y, const std::string& t) {
        const int w = f.measure(t);
        const int x = w < x1 - x0 + 1 ? x0 + (x1 - x0 + 1 - w) / 2 : x0;
        f.draw(fb, kW, kH, pal, x, y - int(f.sheet().frame(0, 0).height) + 1, t);
    };
    for (const auto& list : { std::vector<TreeLabel>(std::begin(kTreeHeader), std::end(kTreeHeader)),
                              kTreeTabs[std::size_t(cls)] })
        for (const auto& l : list)
            if (auto v = lookup_string(s, l.id)) centred(s.font, kTreeR - 0x5a, kTreeR, l.y, u16_to_latin1(*v));
    const auto pts = st.get(d2d::d2s::kSkillPts);
    centred(s.font, kTreeR - 0x41, kTreeR - 0x19, 140, std::to_string(pts));
    const auto& list = s.rules.class_skills[std::size_t(cls)];
    const int clvl = int(st.get(d2d::d2s::kLevel));
    int hover = -1;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& sk = list[i];
        if (sk.page != tab) continue;
        const auto [x, y] = skill_icon_at(sk);
        const bool live = pts > 0 ? d2d::rules::can_learn(s.rules, cls, int(i), lv, clvl) : lv[i] > 0;
        dc6(s.skill_icons[std::size_t(cls)], sk.icon + (int(i) == pressed ? 1 : 0), x, y, live ? 256 : 128);
        if (lv[i] > 0) {
            const auto& f = lv[i] > 9 ? (s.font_small.line_height() > 0 ? s.font_small : s.font) : s.font;
            f.draw(fb, kW, kH, pal, x + 0x30 - (lv[i] > 9 ? 4 : 0), y + 0xc - int(f.sheet().frame(0, 0).height) + 1,
                   std::to_string(lv[i]));
        }
        if (mx > x && mx < x + 0x30 && my > y - 0x30 && my < y) hover = int(i);
    }
    if (hover >= 0 && !list[std::size_t(hover)].name.empty()) {
        const auto [x, y] = skill_icon_at(list[std::size_t(hover)]);
        if (auto v = lookup_string(s, list[std::size_t(hover)].name))
            draw_hover_text(fb, s, { { u16_to_latin1(*v), kTxtWhite } }, x, x + 0x30, y, y - 0x30);
    }
}

}  // namespace d2d::app
