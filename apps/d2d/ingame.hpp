// render_ingame: world + every in-game panel composed per frame.
#pragma once

#include "store.hpp"

namespace {

void render_ingame(std::vector<std::uint8_t>& fb,
                   const Scene& s,
                   int class_idx,
                   const Scene::Appearance& gfx,
                   std::string_view name,
                   bool hardcore,
                   float cam_x,
                   float cam_y,
                   int player_mode,
                   int player_dir,
                   std::uint32_t elapsed_ms,
                   int mouse_x = -1, int mouse_y = -1,
                   std::span<const NpcState> npcs = {},
                   const std::vector<d2d::d2s::Item>* inventory = nullptr,
                   const d2d::d2s::Stats* char_stats = nullptr,
                   const d2d::d2s::Stats* hud_stats = nullptr,
                   const PanelStats* panel = nullptr,
                   std::uint32_t player_mode_ms = 0,
                   const std::vector<d2d::d2s::Item>* belt = nullptr,
                   int* hovered_npc = nullptr,
                   const std::vector<d2d::d2s::Item>* stash = nullptr, bool stash_expansion = true,
                   bool belt_popup = false, bool cube_open = false,
                   const NpcMenuState* npc_menu = nullptr, const Speech* speech = nullptr,
                   const Automap* automap = nullptr, const Store* store = nullptr) {
    // Prefer the real tile-composited world when townE1.ds1 loaded; fall
    // back to the credits DC6 placeholder when it didn't (headless CI, a
    // stripped MPQ dir, etc.). Palette follows the render path: ACT1 for
    // the tiles, Sky for the credits DC6 which was authored against it.
    if (!s.world_dt1s.empty()) {
        set_phase(MainPhase::IngameClear);
        // Clear to black — tiles don't cover every subtile so an
        // uninitialized fb would leak the previous frame's contents.
        std::fill(fb.begin(), fb.end(), std::uint8_t{0});
        for (std::size_t i = 3; i < fb.size(); i += 4) fb[i] = 0xFF;
        set_phase(MainPhase::IngameFloor);   // render_world does floor+shadow+walls internally
        // Player: the camera follows them, so their feet sit on the camera
        // point (kW/2, kH/2 + kIsoH/2); render_world slots them into the
        // wall pass by depth. Wears the loaded save's gear, or the
        // class's starting gear.
        std::vector<Unit> units;
        units.reserve(s.world_npcs.size() + 1);
        if (class_idx >= 0 && class_idx < 7)
            units.push_back({ cam_x, cam_y, &s.composite(kUiToSaveClass[class_idx], player_mode, gfx),
                              player_dir, nullptr, player_mode_ms });
        // NPCs and objects, at their live position when they patrol.
        for (std::size_t i = 0; i < s.world_npcs.size(); ++i) {
            const auto& n = s.world_npcs[i];
            const NpcState* st = i < npcs.size() ? &npcs[i] : nullptr;
            const float x = st ? st->x : n.x, y = st ? st->y : n.y;
            if (std::abs(x - cam_x) >= 14 || std::abs(y - cam_y) >= 14) continue;
            const auto& anim = s.npc_anim(n, st && st->walking ? std::string_view("WL") : std::string_view(n.mode));
            units.push_back({ x, y, &anim, st ? st->dir : 0, &n.name, st ? st->mode_ms : 0, int(i) });
        }
        std::pair<const Unit*, std::array<int, 4>> hovered{ nullptr, {} };
        render_world(fb, s, cam_x, cam_y, elapsed_ms, units, mouse_x, mouse_y, &hovered);
        if (hovered_npc) *hovered_npc = hovered.first ? hovered.first->npc : -1;
        // Name over whatever the cursor points at, centred above it.
        // ponytail: no highlight tint yet (D2 brightens the unit too).
        if (hovered.first) {
            const auto& nm = *hovered.first->name;
            const auto& b  = hovered.second;
            const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
            s.font.draw(fb, kW, kH, pal, (b[0] + b[2]) / 2 - s.font.measure(nm) / 2,
                        b[1] - s.font.line_height() - 2, nm);
        }
        if (inventory && class_idx >= 0 && class_idx < 7)
        {
            // With a store open, your items show what the vendor pays ("Sell value: ", 0xd03).
            std::function<std::string(const d2d::d2s::Item&)> sell_price;
            if (store && store->npc >= 0)
                sell_price = [&](const d2d::d2s::Item& it) {
                    return string_id(s, 0xd03) + std::to_string(item_price(s, it, s.world_npcs[std::size_t(store->npc)], true, store->header));
                };
            draw_inventory(fb, s, s.inv_layout[std::size_t(kUiToSaveClass[class_idx])], *inventory,
                           mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1, &sell_price);
            if (hud_stats) draw_gold(fb, s, *hud_stats, false);
        }
        if (char_stats) draw_char_panel(fb, s, *char_stats, panel ? *panel : PanelStats{}, name, class_idx);
        if (store && store->npc >= 0)
        {
            draw_store(fb, s, *store, mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1);
            if (hud_stats) draw_gold(fb, s, *hud_stats, true);
        }
        if (stash) {
            const int e = stash_expansion ? 1 : 0;
            if (cube_open)
                draw_storage(fb, s, *stash, s.cube_panel, s.cube_layout, 4, mouse_x, mouse_y,
                             hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1);
            else
                draw_storage(fb, s, *stash, s.stash_panel[std::size_t(e)], s.stash_layout[std::size_t(e)], 5,
                             mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1);
        }
        if (automap) draw_automap(fb, s, *automap, cam_x, cam_y);
        if (npc_menu) draw_npc_menu(fb, s, *npc_menu, mouse_x, mouse_y, elapsed_ms);
        if (speech) draw_speech(fb, s, *speech, elapsed_ms);
        if (hud_stats) draw_hud(fb, s, *hud_stats);
        if (belt) draw_belt(fb, s, *belt, mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1,
                            belt_popup);
        // Dev overlay: a red dot on every blocked subtile around the camera.
        if (g_debug_collision) {
            const int cx = int(cam_x * 5), cy = int(cam_y * 5);
            for (int sy = cy - 60; sy <= cy + 60; ++sy)
                for (int sx = cx - 60; sx <= cx + 60; ++sx) {
                    const float wx = (float(sx) + 0.5f) / 5, wy = (float(sy) + 0.5f) / 5;
                    if (!s.blocked(wx, wy)) continue;
                    const int px = int(kW) / 2 + int(std::lround(((wx - cam_x) - (wy - cam_y)) * (kIsoW / 2)));
                    const int py = int(kH) / 2 + kIsoH / 2 + int(std::lround(((wx - cam_x) + (wy - cam_y)) * (kIsoH / 2)));
                    for (int oy = -1; oy <= 1; ++oy)
                        for (int ox = -1; ox <= 1; ++ox) {
                            const int x = px + ox, y = py + oy;
                            if (x < 0 || y < 0 || x >= int(kW) || y >= int(kH)) continue;
                            auto* d = fb.data() + (std::size_t(y) * kW + std::size_t(x)) * 4;
                            d[0] = 255; d[1] = 0; d[2] = 0;
                        }
                }
        }
        set_phase(MainPhase::IngameHudText);
    } else {
        std::fill(fb.begin(), fb.end(), std::uint8_t{0});
        for (std::size_t i = 3; i < fb.size(); i += 4) fb[i] = 0xFF;
        blit_dc6_grid(fb, s.credits_bg, s.pal, 0, 0, s.bg_tiles_across);
    }
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    if (speech && speech->npc >= 0) return;               // the dev overlay would cover the speech box

    std::string cls = kClassKey[class_idx];
    if (auto v = lookup_string(s, kClassKey[class_idx])) cls = u16_to_latin1(*v);

    constexpr const char* welcome = "WELCOME TO SANCTUARY";
    const int ww = s.font.measure(welcome);
    // Dev HUD at the top edge, clear of the player at screen centre.
    s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - ww/2, 8,
                       welcome, 255, 208, 80);

    // On hardcore, D2 marks the caption with a red " (HC)" suffix — we
    // fudge that with a red tint on the trailing tag.
    const std::string line = name.empty() ? cls : std::string(name) + " the " + cls;
    const int lw = s.font.measure(line);
    s.font.draw(fb, kW, kH, pal, int(kW)/2 - lw/2, 28, line);
    if (hardcore) {
        constexpr const char* tag = " (HARDCORE)";
        const int tw = s.font.measure(tag);
        s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - lw/2 + lw, 28,
                           tag, 220, 60, 60);
        (void)tw;
    }

    if (inventory || char_stats || stash || belt_popup) return;   // the hint would run under a panel
    constexpr const char* hint =
        "d2d dev build — click to walk around the Rogue camp";
    const int hw = s.font.measure(hint);
    s.font.draw(fb, kW, kH, pal, int(kW)/2 - hw/2, int(kH) - 140, hint);   // above the HUD bar
    constexpr const char* esc = "press Esc to return to title";
    const int ew = s.font.measure(esc);
    s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - ew/2, int(kH) - 120,
                       esc, 200, 200, 200);
}

}  // namespace
