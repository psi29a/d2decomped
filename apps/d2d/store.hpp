// Vendor store: stock, buy/sell, panel and gold readouts.
#pragma once

#include "panels.hpp"

namespace {

// The vendor store (panel 0xc). Layout from FUN_00488400: buysell.dc6
// as the 2x2 left panel; tabs (buyselltabs, frame i active / i+4 not) at
// x 80 + 80i, bottom 90, labels from the 18-byte records at 0x722110
// (x 42/121/201/281, baseline 79, font16, gold when active: Armor,
// Weapons, Weapons, Misc); buttons (buysellbtn, frame base + pressed) at
// x 80 - 1 + {116, 169, 221, 273}, bottom 476: buy (2), sell (4), then
// repair (6) and repair all (18) at repair vendors, else an empty slot
// (0) and close (10) (FUN_00487ed0). Stock grid: inventory.txt "Monster2"
// (10x10 at 96,123).
// ponytail: stock = each listed item <Vendor>Min..Max times plus the
// PermStoreItems once, packed first-fit by kind (armour tab 0, weapons
// 1 then 2, misc 3) — the server's roll isn't located yet, no magic
// stock.
struct Store {
    int npc = -1, vendor = -1, tab = 0;
    int mode = 0;                               // 1 buy, 2 sell: the button toggled on, the next click trades
    d2d::d2s::Header header;                    // the player's (quest flags, difficulty) for prices
    std::array<std::vector<d2d::d2s::Item>, 4> tabs;
    std::array<bool, 4> pressed{};
    std::vector<std::string> perm;              // PermStoreItems: buying doesn't use them up
};

// First free w x h spot in a cols x rows grid of placed items, scanning
// column by column (where D2 autoplaces pickups); {-1, -1} if full.
std::pair<int, int> free_spot(const Scene& s, const std::vector<const d2d::d2s::Item*>& placed,
                              int cols, int rows, int w, int h) {
    std::vector<bool> used(std::size_t(cols * rows));
    for (const auto* it : placed) {
        const auto info = s.item_info.find(it->code);
        const int iw = info != s.item_info.end() ? info->second.w : 1, ih = info != s.item_info.end() ? info->second.h : 1;
        for (int y = it->row; y < std::min(it->row + ih, rows); ++y)
            for (int x = it->column; x < std::min(it->column + iw, cols); ++x) used[std::size_t(y * cols + x)] = true;
    }
    for (int x = 0; x + w <= cols; ++x)
        for (int y = 0; y + h <= rows; ++y) {
            bool free = true;
            for (int yy = y; yy < y + h && free; ++yy)
                for (int xx = x; xx < x + w && free; ++xx) free = !used[std::size_t(yy * cols + xx)];
            if (free) return { x, y };
        }
    return { -1, -1 };
}

std::pair<int, int> item_size(const Scene& s, const std::string& code) {
    const auto info = s.item_info.find(code);
    return info != s.item_info.end() ? std::pair{ info->second.w, info->second.h } : std::pair{ 1, 1 };
}

// Puts an item into the store grid from tab on (weapons spill 1 -> 2).
bool store_place(const Scene& s, Store& st, int tab, d2d::d2s::Item it) {
    const auto [w, h] = item_size(s, it.code);
    for (int t = tab; t < 4; ++t) {
        std::vector<const d2d::d2s::Item*> placed;
        for (const auto& i : st.tabs[std::size_t(t)]) placed.push_back(&i);
        if (const auto [x, y] = free_spot(s, placed, 10, 10, w, h); x >= 0) {
            it.column = x; it.row = y; it.location = 0; it.panel = 1;
            st.tabs[std::size_t(t)].push_back(std::move(it));
            return true;
        }
        if (t != 1) break;
    }
    return false;
}

int store_tab_for(const Scene& s, const std::string& code) {
    const auto info = s.item_info.find(code);
    const int kind = info != s.item_info.end() ? info->second.kind : 0;
    return kind == 1 ? 0 : kind == 2 ? 1 : 3;
}

int vendor_index(int hc_idx) {
    switch (hc_idx) {
        case 0x94: return 0;  case 0x93: return 1;  case 0x9a: return 2;  case 0xb2: return 3;
        case 0xca: return 4;  case 0xb1: return 5;  case 0xfd: return 6;  case 0xfe: return 7;
        case 0xff: return 8;  case 199:  return 9;  case 0xfc: return 10; case 0x101: return 12;
        case 0x195: return 13; case 0x201: return 14; case 0x1ff: return 15; case 0x200: case 0x202: return 16;
        default: return -1;
    }
}

bool is_repair_vendor(int hc_idx) {
    return hc_idx == 0x9a || hc_idx == 0xb2 || hc_idx == 0xfd || hc_idx == 0x101 || hc_idx == 0x1ff;
}

Store open_store(const Scene& s, int npc, std::uint32_t& rng) {
    Store st;
    st.npc = npc;
    st.vendor = vendor_index(s.world_npcs[std::size_t(npc)].hc_idx);
    if (st.vendor < 0) return st;
    for (const auto& vi : s.vendor_items[std::size_t(st.vendor)]) {
        if (vi.perm) st.perm.push_back(vi.code);
        int n = vi.perm ? 1 : vi.min + (vi.max > vi.min ? int((rng = rng * 0x6ac690c5u + 1u) % std::uint32_t(vi.max - vi.min + 1)) : 0);
        while (n-- > 0) {
            d2d::d2s::Item it;
            it.code = vi.code;
            if (const auto b = s.item_base.find(vi.code); b != s.item_base.end() && store_tab_for(s, vi.code) == 0)
                it.defense = b->second.minac;
            store_place(s, st, store_tab_for(s, vi.code), std::move(it));
        }
    }
    for (int t = 0; t < 4; ++t) if (!st.tabs[std::size_t(t)].empty()) { st.tab = t; break; }
    return st;
}

// The store item under the cursor (index into the open tab), or -1.
int store_item_at(const Scene& s, const Store& st, int mx, int my) {
    const auto& tab = st.tabs[std::size_t(st.tab)];
    for (std::size_t i = 0; i < tab.size(); ++i) {
        const auto [w, h] = item_size(s, tab[i].code);
        const int x = 96 + tab[i].column * 29, y = 123 + tab[i].row * 29;
        if (mx >= x && mx < x + w * 29 && my >= y && my < y + h * 29) return int(i);
    }
    return -1;
}

// Buys stock item i of the open tab into the inventory (10x4): gold
// down by the price, the item leaves the stock unless it's a perm one.
// False if it doesn't fit or you can't afford it.
// ponytail: no "not enough gold"/"no room" message, no stacks or quantity.
bool store_buy(const Scene& s, Store& st, int i, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats) {
    auto& tab = st.tabs[std::size_t(st.tab)];
    const auto& it = tab[std::size_t(i)];
    const int price = item_price(s, it, s.world_npcs[std::size_t(st.npc)], false, st.header);
    if (stats.get(d2d::d2s::kGold) + stats.get(d2d::d2s::kGoldBank) < price) return false;
    std::vector<const d2d::d2s::Item*> inv;
    for (const auto& x : items) if (x.location == 0 && x.panel == 1) inv.push_back(&x);
    const auto [w, h] = item_size(s, it.code);
    const auto [x, y] = free_spot(s, inv, 10, 4, w, h);
    if (x < 0) return false;
    auto bought = it;
    bought.column = x; bought.row = y; bought.location = 0; bought.panel = 1;
    items.push_back(std::move(bought));
    // Carried gold first, then the stash (the store shows it for that).
    // ponytail: that order is a guess; the server's buy isn't RE'd.
    const auto from_inv = std::min<std::int64_t>(stats.get(d2d::d2s::kGold), price);
    stats.v[d2d::d2s::kGold] -= from_inv;
    stats.v[d2d::d2s::kGoldBank] -= price - from_inv;
    if (std::ranges::find(st.perm, it.code) == st.perm.end()) tab.erase(tab.begin() + i);
    return true;
}

// Sells inventory item i: gold up by the sell value (carried gold caps
// at clvl x 10000), the item joins the stock.
// ponytail: quest items aren't refused, no belt/equipped selling.
void store_sell(const Scene& s, Store& st, std::size_t i, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats) {
    const int price = item_price(s, items[i], s.world_npcs[std::size_t(st.npc)], true, st.header);
    stats.v[d2d::d2s::kGold] = std::min<std::int64_t>(stats.get(d2d::d2s::kGold) + price,
                                                       stats.get(d2d::d2s::kLevel) * 10000);
    store_place(s, st, store_tab_for(s, items[i].code), items[i]);
    items.erase(items.begin() + std::ptrdiff_t(i));
}

std::array<int, 4> store_button_frames(const Scene& s, const Store& st) {
    const bool repair = st.npc >= 0 && is_repair_vendor(s.world_npcs[std::size_t(st.npc)].hc_idx);
    return { 2, 4, repair ? 6 : 0, repair ? 18 : 10 };
}

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
    const auto frames = store_button_frames(s, st);
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
        lines.push_back({ string_id(s, 0xd01) + std::to_string(item_price(s, *hover, s.world_npcs[std::size_t(st.npc)], false, st.header)),
                          kTxtWhite });
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
