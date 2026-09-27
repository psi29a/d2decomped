// Vendor store: stock, buy/sell, panel and gold readouts.
#pragma once

#include "skilltree.hpp"

namespace {

using d2d::rules::Store;

// The store for world NPC npc (stock rolled from rng).
Store open_store(const GameData& s, const Level& L, int npc, d2d::rules::Rng& rng) {
    const auto& n = L.npcs[std::size_t(npc)];
    Store st = d2d::rules::open_store(s.rules, n.hc_idx, n.id, rng);
    st.npc = npc;
    return st;
}

// The hire list (FUN_004b5c60): a 490x350 NPC text window, header
// "Your Gold: %d     Hire which Mercenary?" (0xd24, carried + stash gold)
// in gold, one line per offer — its name, " - ", then "Lvl" (0xd28),
// "Life" (0xd26), "Def" (0xd27), "Cost" (0xd29), each ": %u" — and
// "cancel" (0xd48).
// ponytail: one centred text line per offer in the NPC-menu style; the
// game's scrolling list widget (FUN_004bf8f0, 490x280 at (W-490)/2,
// H/2-160, rows 35 high) and the second line of hire description aren't
// drawn.
NpcMenuState open_hire_menu(const Scene& s, int npc, const std::vector<d2d::rules::MercOffer>& offers,
                            std::int64_t gold) {
    NpcMenuState m;
    m.npc = npc;
    std::string head = string_id(s, 0xd24);
    if (const auto p = head.find("%d"); p != head.npos) head.replace(p, 2, std::to_string(gold));
    m.lines.push_back({ head, 21, 0, 0, true });
    auto label = [&](std::uint16_t id) { return string_id(s, id) + ": "; };
    for (std::size_t i = 0; i < offers.size(); ++i) {
        const auto& o = offers[i];
        const auto merc = s.mercs.find(o.id);
        const auto name = merc != s.mercs.end() ? merc_name(s, merc->second, o.name) : std::string("?");
        m.lines.push_back({ name + " - " + label(0xd28) + std::to_string(o.level) + "  " + label(0xd26) + std::to_string(o.life)
                                + "  " + label(0xd27) + std::to_string(o.def) + "  " + label(0xd29) + std::to_string(o.cost),
                            0x23, 0, 0, false, NpcMenuState::kHireOffer, int(i) });
    }
    m.lines.push_back({ string_id(s, 0xd48), 0x23 });
    m.w = 0x1ea; m.h = 0x15e;
    m.x = (int(kW) - m.w) / 2; m.y = (int(kH) - m.h) / 2;
    for (auto& l : m.lines) {
        l.width = s.font.measure(l.text);
        l.x = std::max(4, (m.w - l.width) / 2);
    }
    return m;
}

// The store item under the cursor (index into the open tab), or -1.
int store_item_at(const Scene& s, const Store& st, int mx, int my) {
    const auto& tab = st.tabs[std::size_t(st.tab)];
    for (std::size_t i = 0; i < tab.size(); ++i) {
        const auto [w, h] = d2d::rules::item_size(s.rules, tab[i].code);
        const int x = 96 + tab[i].column * 29, y = 123 + tab[i].row * 29;
        if (mx >= x && mx < x + w * 29 && my >= y && my < y + h * 29) return int(i);
    }
    return -1;
}

std::array<int, 4> store_button_frames(const Store& st) {
    const bool repair = st.npc >= 0 && d2d::rules::is_repair_vendor(st.hc_idx);
    return { 2, 4, repair ? 6 : 0, repair ? 18 : 10 };
}

// The vendor store (panel 0xc). Layout from FUN_00488400: buysell.dc6
// as the 2x2 left panel; tabs (buyselltabs, frame i active / i+4 not) at
// x 80 + 80i, bottom 90, labels from the 18-byte records at 0x722110
// (x 42/121/201/281, baseline 79, font16, gold when active: Armor,
// Weapons, Weapons, Misc); buttons (buysellbtn, frame base + pressed) at
// x 80 - 1 + {116, 169, 221, 273}, bottom 476: buy (2), sell (4), then
// repair (6) and repair all (18) at repair vendors, else an empty slot
// (0) and close (10) (FUN_00487ed0). Stock grid: inventory.txt "Monster2"
// (10x10 at 96,123).
void draw_store(std::vector<std::uint8_t>& fb, const Scene& s, const Store& st, int mx, int my, int clvl) {
    if (st.npc < 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    if (s.store_panel.frames_per_direction() >= 4) {
        const auto& f0 = s.store_panel.frame(0, 0);
        blit_sprite(fb, f0, pal, kCharPanelX, kCharPanelY);
        blit_sprite(fb, s.store_panel.frame(0, 1), pal, kCharPanelX + int(f0.width), kCharPanelY);
        blit_sprite(fb, s.store_panel.frame(0, 2), pal, kCharPanelX, kCharPanelY + int(f0.height));
        blit_sprite(fb, s.store_panel.frame(0, 3), pal, kCharPanelX + int(f0.width), kCharPanelY + int(f0.height));
    }
    static constexpr int kTabLabelX[4] = { 42, 121, 201, 281 };
    static constexpr std::uint16_t kTabString[4] = { 0xfc4, 0xfc5, 0xfc5, 0xfc7 };
    for (int i = 0; i < 4; ++i) {
        const bool active = i == st.tab;
        if (s.store_tabs.frames_per_direction() >= 8) {
            const auto& f = s.store_tabs.frame(0, std::uint32_t(active ? i : i + 4));
            blit_sprite(fb, f, pal, kCharPanelX + 80 * i, 90 - int(f.height) + 1);
        }
        const std::string label = string_id(s, kTabString[i]);
        const int w = s.font.measure(label);
        const int cell = s.font.sheet().frames_per_direction() > 0 ? int(s.font.sheet().frame(0, 0).height) : 16;
        const int x = kCharPanelX + kTabLabelX[i] - w / 2, y = 79 - cell + 1;
        if (active) s.font.draw_tinted(fb, kW, kH, pal, x, y, label, 199, 179, 119);
        else        s.font.draw(fb, kW, kH, pal, x, y, label);
    }
    const auto frames = store_button_frames(st);
    static constexpr int kBtnX[4] = { 116, 169, 221, 273 };
    for (int i = 0; i < 4; ++i)
        if (std::uint32_t(frames[std::size_t(i)] + 1) < s.store_buttons.frames_per_direction()) {
            const bool down = st.pressed[std::size_t(i)] || (i < 2 && st.mode == i + 1);
            const auto& f = s.store_buttons.frame(0, std::uint32_t(frames[std::size_t(i)] + (down ? 1 : 0)));
            blit_sprite(fb, f, pal, kCharPanelX - 1 + kBtnX[i], 476 - int(f.height) + 1);
        }
    // Stock, Monster2 grid.
    Scene::InvLayout L;
    L.grid_x = 96; L.grid_y = 123; L.box_w = L.box_h = 29;
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hb{};
    for (const auto& it : st.tabs[std::size_t(st.tab)]) {
        const auto [x, y, w, h] = grid_rect(s, L, it);
        if (const auto* spr = s.item_sprite(it); spr && spr->frames_per_direction() > 0) {
            const auto& f = spr->frame(0, 0);
            blit_sprite(fb, f, pal, x + (w - int(f.width)) / 2, y + (h - int(f.height)) / 2);
        }
        if (mx >= x && mx < x + w && my >= y && my < y + h) { hover = &it; hb = { x, y, w, h }; }
    }
    if (hover) {
        auto lines = item_lines(s, *hover, clvl);
        // "Cost: " (0xd01) + the vendor's price, as the store hover shows it (FUN_004b2ad0).
        // At the gamble screen, the gamble price (FUN_00629370).
        const int price = st.gamble ? d2d::rules::gamble_price(s.rules, hover->code, clvl)
                                    : d2d::rules::item_price(s.rules, *hover, st.npc_id, false, st.header);
        lines.push_back({ string_id(s, 0xd01) + std::to_string(price), kTxtWhite });
        draw_hover_text(fb, s, lines, hb[0], hb[0] + hb[2], hb[1] + hb[3], hb[1]);
    }
}

// Gold readouts (FUN_00488100, docs/research/re/store.md), font16 white,
// baselines at 800x600: the inventory's carried gold (stat 14) at x 508,
// y 468 after the goldcoinbtn (frame 0, bottom-left 484,469); with a
// store open, "Stash" (0xcf3) at x 101, y 434 and the stash gold (15)
// right-aligned to x 278.
// ponytail: the coin button doesn't click (no gold drop/withdraw yet).
void draw_gold(std::vector<std::uint8_t>& fb, const Scene& s, const d2d::d2s::Stats& st, bool store) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int cell = s.font.sheet().frames_per_direction() > 0 ? int(s.font.sheet().frame(0, 0).height) : 16;
    auto text = [&](int x, int baseline, const std::string& t) { s.font.draw(fb, kW, kH, pal, x, baseline - cell + 1, t); };
    if (!store) {
        if (s.gold_coin.frames_per_direction() > 0) {
            const auto& f = s.gold_coin.frame(0, 0);
            blit_sprite(fb, f, pal, 484, 469 - int(f.height) + 1);
        }
        text(508, 468, std::to_string(st.get(d2d::d2s::kGold)));
        return;
    }
    text(101, 434, string_id(s, 0xcf3));
    const auto n = std::to_string(st.get(d2d::d2s::kGoldBank));
    text(278 - s.font.measure(n), 434, n);
}

}  // namespace
