// SPDX-License-Identifier: GPL-3.0-or-later
// The char panel's hover percentages (FUN_004a7ae0 / FUN_004a7180): panels.hpp draw_char_tooltips.
#include "common.hpp"
#include "items/items.hpp"
#include "panels/panels.hpp"
#include "scene.hpp"

#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <monsters.hpp>
#include <montypes.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace d2d::client {

// The char panel's hovers (FUN_004a7d00 -> FUN_004a7ae0 / FUN_004a7180), at 800x600: px 0x7a2858 = 80,
// py 0x7a285c = -60, H 0x711470 = 600 (so py + H - 0x1e0 is the panel's top, 60).
// - Chance to hit (FUN_004a7ae0): attack line 1 under x 80+0xa2..80+0x140, y 540-0x14e..540-0x13b, line 2
//   y 540-0x134..540-0x121, each with its rating's percentage (FUN_004a7340) not 0: a 0x9b x 0x1e box
//   darkened (FUN_0046efd0 mode 2) at the row's table spot (0x6da430: {154, 115}, {159, 128}, {159, 140};
//   {154, 130}, {159, 143}, {159, 155}, from the panel's corner), "Average chance to hit" (0x103f) and
//   "%s: %d%%" (0x2777: the monster's name, the percentage held to 5..95).
// - Chance to be hit (FUN_004a7180): x 80+0xad..80+0x137, y 540-0x11e..540-0x10b: "Chance to Block: %d%%\n
//   Average chance %s will hit you: %d%%" (0x2779) with a block chance, else 0x2778 alone, as a hover box
//   from (80+0x8b, 540-0x13b) (FUN_005023b0: left there, its bottom 2 below).
// The percentages (rules::panel_hit_chance): the monster's defence or attack rating at its MonStats level
// for the difficulty (FUN_006538a0: monster_stats; the rating its A1's, else A2's, else S1's), scaled by
// 10/12 (defence) or 10/15 (rating) in a classic game above Normal unless its Align is 1.
void draw_char_tooltips(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Stats& stats,
                        const PanelStats& panel, const Hud& hud, int mouse_x, int mouse_y) {
    if (!hud.header || mouse_x < 0) return;
    constexpr int kPx = kCharPanelX, kBottom = 600 - 60;           // px, py + H
    const int difficulty = std::clamp(hud.header->active_difficulty(), 0, 2);
    const bool classic = !hud.header->expansion();
    const int clvl = int(stats.get(d2d::d2s::kLevel));
    const auto monster = [&](int cls) -> const d2d::rules::MonType* {
        return cls >= 0 && std::size_t(cls) < scene.monsters.types.size() ? &scene.monsters.types[std::size_t(cls)] : nullptr;
    };
    const auto name_of = [&](int cls) {
        return std::size_t(cls) < scene.mon_npc.size() && !scene.mon_npc[std::size_t(cls)].title.empty() ? scene.mon_npc[std::size_t(cls)].title : std::string{};
    };
    const auto stats_of = [&](int cls) { d2d::rules::Rng unused{ 1 }; return d2d::rules::monster_stats(scene.monsters, cls, difficulty, unused); };
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const auto format = [](std::string pattern, const std::string& name, int pct) {   // %s then %d, "%%" kept as one
        if (const auto found_at = pattern.find("%s"); found_at != std::string::npos) pattern.replace(found_at, 2, name);
        if (const auto found_at = pattern.find("%d"); found_at != std::string::npos) pattern.replace(found_at, 2, std::to_string(pct));
        for (std::size_t pos = pattern.find("%%"); pos != std::string::npos; pos = pattern.find("%%", pos + 1)) pattern.erase(pos, 1);
        return pattern;
    };
    // To hit.
    if (const auto* type_info = monster(hud.tohit_class)) {
        const auto held = stats_of(hud.tohit_class);
        int defense = held.armor_class;
        if (type_info->align != 1 && classic && difficulty != 0) defense = defense * 10 / 12;
        const auto pct_of = [&](int rating) { return rating > 0 ? d2d::rules::panel_hit_chance(rating, defense, clvl, held.level) : 0; };
        const int pcts[2] = { pct_of(panel.attack[0].attack_rating), pct_of(panel.attack[1].attack_rating) };
        static constexpr std::array<std::array<int, 6>, 2> kSpots{ { { 154, 115, 159, 128, 159, 140 }, { 154, 130, 159, 143, 159, 155 } } };
        const bool in_x = mouse_x >= kPx + 0xa2 && mouse_x <= kPx + 0x140;
        int row = -1;
        if (in_x && mouse_y >= kBottom - 0x14e && mouse_y <= kBottom - 0x13b && pcts[0] != 0) row = 0;
        else if (in_x && mouse_y >= kBottom - 0x134 && mouse_y <= kBottom - 0x121 && pcts[1] != 0) row = 1;
        if (row >= 0) {
            const auto& spot = kSpots[std::size_t(row)];
            const int box_x = kPx + spot[0], box_y = kCharPanelY + spot[1];
            for (int y = std::max(box_y, 0); y < std::min(box_y + 0x1e, int(kScreenHeight)); ++y)
                for (int x = std::max(box_x, 0); x < std::min(box_x + 0x9b, int(kScreenWidth)); ++x) {
                    auto* pixel = &framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4];
                    pixel[0] = std::uint8_t(pixel[0] / 4); pixel[1] = std::uint8_t(pixel[1] / 4); pixel[2] = std::uint8_t(pixel[2] / 4);
                }
            const int cell = int(scene.font.sheet().frame(0, 0).height);
            scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, kPx + spot[2], kCharPanelY + spot[3] - cell + 1, string_id(scene, 0x103f));
            scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, kPx + spot[4], kCharPanelY + spot[5] - cell + 1,
                            format(string_id(scene, 0x2777), name_of(hud.tohit_class), pcts[std::size_t(row)]));
        }
    }
    // To be hit.
    if (const auto* type_info = monster(hud.behit_class); type_info && mouse_x >= kPx + 0xad && mouse_x <= kPx + 0x137
        && mouse_y >= kBottom - 0x11e && mouse_y <= kBottom - 0x10b) {
        const auto held = stats_of(hud.behit_class);
        int rating = held.to_hit != 0 ? held.to_hit : held.to_hit2 != 0 ? held.to_hit2 : held.to_hit_s1;
        if (type_info->align != 1 && classic && difficulty != 0) rating = rating * 10 / 15;
        const int pct = d2d::rules::panel_hit_chance(rating, int(panel.defense), held.level, clvl);
        std::vector<TextLine> lines;
        const std::string chance = format(string_id(scene, 0x2778), name_of(hud.behit_class), pct);
        if (panel.block > 0) {
            std::string block = string_id(scene, 0x2779);
            block = block.substr(0, block.find('\n'));
            if (const auto found_at = block.find("%d"); found_at != std::string::npos) block.replace(found_at, 2, std::to_string(panel.block));
            if (const auto found_at = block.find("%%"); found_at != std::string::npos) block.erase(found_at, 1);
            lines.push_back({ block, kTxtWhite });
        }
        lines.push_back({ chance, kTxtWhite });
        int width = 0;
        for (const auto& line : lines) width = std::max(width, scene.font.measure(line.text));
        const int left = kPx + 0x8b, bottom = kBottom - 0x13b + 2;
        draw_hover_text(framebuffer, scene, lines, left, left + width + 4, bottom, bottom);
    }
}


}  // namespace d2d::client
