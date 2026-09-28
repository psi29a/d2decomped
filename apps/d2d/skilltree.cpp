// Definitions for skilltree.hpp: the skill tree panel.
#include "skilltree.hpp"
#include "common.hpp"
#include "items.hpp"
#include "scene.hpp"

namespace d2d::client {

std::pair<int, int> skill_icon_at(const d2d::rules::ClassSkill& class_skill) {
    static constexpr int kCol[4] = { 0, 0x131, 0xec, 0xa7 };
    static constexpr int kRow[7] = { 0, 0x1a2, 0x15e, 0x11a, 0xd6, 0x91, 0x4d };
    if (class_skill.col < 1 || class_skill.col > 3 || class_skill.row < 1 || class_skill.row > 6) return { -1000, -1000 };
    return { kTreeR - kCol[class_skill.col], kTreeB - kRow[class_skill.row] };
}

int skill_at(const Scene& scene, int cls, int tab, int mouse_x, int mouse_y) {
    const auto& list = scene.rules.class_skills[std::size_t(cls)];
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].page != tab) continue;
        const auto [x, y] = skill_icon_at(list[i]);
        if (mouse_x > x && mouse_x < x + 0x30 && mouse_y > y - 0x30 && mouse_y < y) return int(i);
    }
    return -1;
}

int skill_tab_at(int mouse_x, int mouse_y) {
    if (mouse_x < kTreeR - 0x58 || mouse_x > kTreeR) return 0;
    if (mouse_y > kTreeB - 0x174 && mouse_y < kTreeB - 0x109) return 3;
    if (mouse_y > kTreeB - 0x108 && mouse_y < kTreeB - 0x9d) return 2;
    if (mouse_y > kTreeB - 0x9c && mouse_y < kTreeB - 0x31) return 1;
    return 0;
}

void draw_skill_tree(std::vector<std::uint8_t>& framebuffer, const Scene& scene, int cls, int tab,
                     const std::array<std::uint8_t, 30>& levels, const d2d::d2s::Stats& stats, int pressed, int mouse_x, int mouse_y) {
    if (cls < 0 || cls > 6) return;
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
    auto dc6 = [&](const d2d::dc6::Sprite& spr, int frame, int x, int y, int shade = 256) {
        if (frame < 0 || spr.frames_per_direction() <= std::uint32_t(frame)) return;
        const auto& frame_ref = spr.frame(0, std::uint32_t(frame));
        blit_sprite(framebuffer, frame_ref, pal, x, y - int(frame_ref.height) + 1, shade);
    };
    const auto& background = scene.skill_tree_bg[std::size_t(cls)];
    for (const int base : { 0, 4 * tab }) {
        dc6(background, base + 0, kTreeR - 0x140, kTreeB - 0xe0);
        dc6(background, base + 1, kTreeR - 0x40, kTreeB - 0xe0);
        dc6(background, base + 2, kTreeR - 0x140, kTreeB - 0x30);
        dc6(background, base + 3, kTreeR - 0x40, kTreeB - 0x30);
    }
    auto centred = [&](const d2d::font::Font& font, int left, int right, int y, const std::string& text) {
        const int width = font.measure(text);
        const int x = width < right - left + 1 ? left + (right - left + 1 - width) / 2 : left;
        font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, x, y - int(font.sheet().frame(0, 0).height) + 1, text);
    };
    for (const auto& list : { std::vector<TreeLabel>(std::begin(kTreeHeader), std::end(kTreeHeader)),
                              kTreeTabs[std::size_t(cls)] })
        for (const auto& label : list)
            if (auto found = lookup_string(scene, label.id)) centred(scene.font, kTreeR - 0x5a, kTreeR, label.y, u16_to_latin1(*found));
    const auto pts = stats.get(d2d::d2s::kSkillPts);
    centred(scene.font, kTreeR - 0x41, kTreeR - 0x19, 140, std::to_string(pts));
    const auto& list = scene.rules.class_skills[std::size_t(cls)];
    const int clvl = int(stats.get(d2d::d2s::kLevel));
    int hover = -1;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& class_skill = list[i];
        if (class_skill.page != tab) continue;
        const auto [x, y] = skill_icon_at(class_skill);
        const bool live = pts > 0 ? d2d::rules::can_learn(scene.rules, cls, int(i), levels, clvl) : levels[i] > 0;
        dc6(scene.skill_icons[std::size_t(cls)], class_skill.icon + (int(i) == pressed ? 1 : 0), x, y, live ? 256 : 128);
        if (levels[i] > 0) {
            const auto& font = levels[i] > 9 ? (scene.font_small.line_height() > 0 ? scene.font_small : scene.font) : scene.font;
            font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, x + 0x30 - (levels[i] > 9 ? 4 : 0), y + 0xc - int(font.sheet().frame(0, 0).height) + 1,
                   std::to_string(levels[i]));
        }
        if (mouse_x > x && mouse_x < x + 0x30 && mouse_y > y - 0x30 && mouse_y < y) hover = int(i);
    }
    if (hover >= 0 && !list[std::size_t(hover)].name.empty()) {
        const auto [x, y] = skill_icon_at(list[std::size_t(hover)]);
        if (auto found = lookup_string(scene, list[std::size_t(hover)].name))
            draw_hover_text(framebuffer, scene, { { u16_to_latin1(*found), kTxtWhite } }, x, x + 0x30, y, y - 0x30);
    }
}

}  // namespace d2d::client
