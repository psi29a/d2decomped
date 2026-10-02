// Definitions for store.hpp: the store panel.
#include "store.hpp"

#include "common.hpp"
#include "items.hpp"
#include "panels.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <d2s_items.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace d2d::client {

NpcMenuState open_hire_menu(const Scene& scene, int npc, const std::vector<d2d::rules::MercOffer>& offers,
                            std::int64_t gold) {
    NpcMenuState menu;
    menu.npc = npc;
    std::string head = string_id(scene, 0xd24);
    if (const auto found = head.find("%d"); found != head.npos) head.replace(found, 2, std::to_string(gold));
    menu.lines.push_back({ head, 21, 0, 0, true });
    auto label = [&](std::uint16_t id) { return string_id(scene, id) + ": "; };
    for (std::size_t i = 0; i < offers.size(); ++i) {
        const auto& offer = offers[i];
        const auto merc = scene.mercs.find(offer.id);
        const auto name = merc != scene.mercs.end() ? merc_name(scene, merc->second, offer.name) : std::string("?");
        menu.lines.push_back({ name + " - " + label(0xd28) + std::to_string(offer.level) + "  " + label(0xd26) + std::to_string(offer.life)
                                + "  " + label(0xd27) + std::to_string(offer.def) + "  " + label(0xd29) + std::to_string(offer.cost),
                            0x23, 0, 0, false, NpcMenuState::kHireOffer, int(i) });
    }
    menu.lines.push_back({ string_id(scene, 0xd48), 0x23 });
    menu.box_width = 0x1ea; menu.box_height = 0x15e;
    menu.x = (int(kScreenWidth) - menu.box_width) / 2; menu.y = (int(kScreenHeight) - menu.box_height) / 2;
    for (auto& line : menu.lines) {
        line.width = scene.font.measure(line.text);
        line.x = std::max(4, (menu.box_width - line.width) / 2);
    }
    return menu;
}

int store_item_at(const Scene& scene, const Store& store, int mouse_x, int mouse_y) {
    const auto& tab = store.tabs[std::size_t(store.tab)];
    for (std::size_t i = 0; i < tab.size(); ++i) {
        const auto [width, height] = d2d::rules::item_size(scene.rules, tab[i].code);
        const int x = 96 + tab[i].column * 29, y = 123 + tab[i].row * 29;
        if (mouse_x >= x && mouse_x < x + width * 29 && mouse_y >= y && mouse_y < y + height * 29) return int(i);
    }
    return -1;
}

std::array<int, 4> store_button_frames(const Store& store) {
    const bool repair = store.npc >= 0 && d2d::rules::is_repair_vendor(store.hc_idx);
    return { 2, 4, repair ? 6 : 0, repair ? 18 : 10 };
}

void draw_store(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Store& store, int mouse_x, int mouse_y, const d2d::rules::Wearer* wearer) {
    if (store.npc < 0) return;
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
    if (scene.store_panel.frames_per_direction() >= 4) {
        const auto& frame = scene.store_panel.frame(0, 0);
        blit_sprite(framebuffer, frame, pal, kCharPanelX, kCharPanelY);
        blit_sprite(framebuffer, scene.store_panel.frame(0, 1), pal, kCharPanelX + int(frame.width), kCharPanelY);
        blit_sprite(framebuffer, scene.store_panel.frame(0, 2), pal, kCharPanelX, kCharPanelY + int(frame.height));
        blit_sprite(framebuffer, scene.store_panel.frame(0, 3), pal, kCharPanelX + int(frame.width), kCharPanelY + int(frame.height));
    }
    static constexpr int kTabLabelX[4] = { 42, 121, 201, 281 };
    static constexpr std::uint16_t kTabString[4] = { 0xfc4, 0xfc5, 0xfc5, 0xfc7 };
    for (int i = 0; i < 4; ++i) {
        const bool active = i == store.tab;
        if (scene.store_tabs.frames_per_direction() >= 8) {
            const auto& frame = scene.store_tabs.frame(0, std::uint32_t(active ? i : i + 4));
            blit_sprite(framebuffer, frame, pal, kCharPanelX + 80 * i, 90 - int(frame.height) + 1);
        }
        const std::string label = string_id(scene, kTabString[i]);
        const int width = scene.font.measure(label);
        const int cell = scene.font.sheet().frames_per_direction() > 0 ? int(scene.font.sheet().frame(0, 0).height) : 16;
        const int x = kCharPanelX + kTabLabelX[i] - width / 2, y = 79 - cell + 1;
        if (active) scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x, y, label, 199, 179, 119);
        else        scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, x, y, label);
    }
    const auto frames = store_button_frames(store);
    static constexpr int kBtnX[4] = { 116, 169, 221, 273 };
    for (int i = 0; i < 4; ++i)
        if (std::uint32_t(frames[std::size_t(i)] + 1) < scene.store_buttons.frames_per_direction()) {
            const bool down = store.pressed[std::size_t(i)] || (i < 2 && store.mode == i + 1);
            const auto& frame = scene.store_buttons.frame(0, std::uint32_t(frames[std::size_t(i)] + (down ? 1 : 0)));
            blit_sprite(framebuffer, frame, pal, kCharPanelX - 1 + kBtnX[i], 476 - int(frame.height) + 1);
        }
    // Stock, Monster2 grid.
    Scene::InvLayout layout;
    layout.grid_x = 96; layout.grid_y = 123; layout.box_w = layout.box_h = 29;
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hover_box{};
    for (const auto& item : store.tabs[std::size_t(store.tab)]) {
        const auto [x, y, width, height] = grid_rect(scene, layout, item);
        if (const auto* spr = scene.item_sprite(item); spr && spr->frames_per_direction() > 0) {
            const auto& frame = spr->frame(0, 0);
            blit_sprite(framebuffer, frame, scene.item_pal(item, pal), x + (width - int(frame.width)) / 2, y + (height - int(frame.height)) / 2);
        }
        if (mouse_x >= x && mouse_x < x + width && mouse_y >= y && mouse_y < y + height) { hover = &item; hover_box = { x, y, width, height }; }
    }
    if (hover) {
        const int clvl = wearer ? wearer->lvl : 1;
        auto lines = item_lines(scene, *hover, clvl, wearer);
        // "Cost: " (0xd01) + the vendor's price, as the store hover shows it (FUN_004b2ad0).
        // At the gamble screen, the gamble price (FUN_00629370).
        const int price = store.gamble ? d2d::rules::gamble_price(scene.rules, hover->code, clvl)
                                    : d2d::rules::item_price(scene.rules, *hover, store.npc_id, false, store.header);
        lines.push_back({ string_id(scene, 0xd01) + std::to_string(price), kTxtWhite });
        draw_hover_text(framebuffer, scene, lines, hover_box[0], hover_box[0] + hover_box[2], hover_box[1] + hover_box[3], hover_box[1]);
    }
}

void draw_gold(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Stats& stats, bool store) {
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
    const int cell = scene.font.sheet().frames_per_direction() > 0 ? int(scene.font.sheet().frame(0, 0).height) : 16;
    auto text = [&](int x, int baseline, const std::string& label) { scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, x, baseline - cell + 1, label); };
    if (!store) {
        if (scene.gold_coin.frames_per_direction() > 0) {
            const auto& frame = scene.gold_coin.frame(0, 0);
            blit_sprite(framebuffer, frame, pal, 484, 469 - int(frame.height) + 1);
        }
        text(508, 468, std::to_string(stats.get(d2d::d2s::kGold)));
        return;
    }
    text(101, 434, string_id(scene, 0xcf3));
    const auto gold_text = std::to_string(stats.get(d2d::d2s::kGoldBank));
    text(278 - scene.font.measure(gold_text), 434, gold_text);
}

}  // namespace d2d::client
