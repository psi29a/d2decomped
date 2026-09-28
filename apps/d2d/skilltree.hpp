// The skill tree panel ('T'): docs/research/re/skill-tree.md.
#pragma once

#include "scene.hpp"

namespace d2d::client {

// The right panel's right edge and bottom at 800x600 (game.exe works from
// W - panel x and panel y + H - 480).
constexpr int kTreeR = 720, kTreeB = 540;

// A skill's icon box {left, bottom} (FUN_004aaa50 columns, FUN_004aa9c0
// rows); icons are 48x48.
std::pair<int, int> skill_icon_at(const d2d::rules::ClassSkill& class_skill);

// The class skill (0..29) whose icon on tab `tab` is under (mx, my), or -1.
int skill_at(const Scene& scene, int cls, int tab, int mouse_x, int mouse_y);

// The tab under (mx, my): the strip x R-88..R, split into bands bottom
// (1) to top (3) (FUN_004ab7e0), or 0.
int skill_tab_at(int mouse_x, int mouse_y);

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
void draw_skill_tree(std::vector<std::uint8_t>& framebuffer, const Scene& scene, int cls, int tab,
                     const std::array<std::uint8_t, 30>& levels, const d2d::d2s::Stats& stats, int pressed, int mouse_x, int mouse_y);

}  // namespace d2d::client
