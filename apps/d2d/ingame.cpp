// Definitions for ingame.hpp: the in-game frame and the rain.
#include "ingame.hpp"

#include "common.hpp"
#include "panels.hpp"
#include "scene.hpp"
#include "store.hpp"
#include "ui.hpp"
#include "watchdog.hpp"
#include "world_view.hpp"

#include <d2s_items.hpp>
#include <rules.hpp>
#include <weather.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::client {

void draw_rain(std::vector<std::uint8_t>& framebuffer, const d2d::rules::Rain& rain) {
    const int bottom = int(kScreenHeight) - 47;
    for (const auto& drop : rain.drops) {
        int dx = 0, dy = 0;
        if (!drop.landed) {
            dx = int(d2d::rules::cos512(rain.wind) * float(drop.len));
            dy = int(d2d::rules::sin512(rain.wind) * float(drop.len));
            if (const int room = drop.bottom - drop.y; room < dy) { dx = dy ? room * dx / dy : 0; dy = room; }
        }
        const auto rgb = d2d::rules::rain_rgb(drop.kind, drop.tone);
        const bool half = drop.kind == 0;
        const int steps = std::max({ std::abs(dx), std::abs(dy), 1 });
        for (int k = 0; k <= steps; ++k) {
            const int x = drop.x + dx * k / steps, y = drop.y + dy * k / steps;
            if (x < 0 || y < 0 || x >= int(kScreenWidth) || y >= bottom) continue;
            auto* pixel = framebuffer.data() + (std::size_t(y) * kScreenWidth + std::size_t(x)) * 4;
            for (int channel = 0; channel < 3; ++channel) pixel[channel] = half ? std::uint8_t((pixel[channel] + rgb[std::size_t(channel)]) / 2) : rgb[std::size_t(channel)];
        }
    }
}

void render_ingame(std::vector<std::uint8_t>& framebuffer,
                   const Scene& scene,
                   const Level& level,
                   int class_idx,
                   const Scene::Appearance& gfx,
                   std::string_view name,
                   [[maybe_unused]] bool hardcore,   // game.exe draws no caption in game
                   float cam_x,
                   float cam_y,
                   int player_mode,
                   int player_dir,
                   std::uint32_t elapsed_ms,
                   int mouse_x , int mouse_y ,
                   std::span<const UnitState> npcs ,
                   const std::vector<d2d::d2s::Item>* inventory ,
                   const d2d::d2s::Stats* char_stats ,
                   const d2d::d2s::Stats* hud_stats ,
                   const PanelStats* panel ,
                   std::uint32_t player_mode_ms ,
                   const std::vector<d2d::d2s::Item>* belt ,
                   int* hovered_npc ,
                   const std::vector<d2d::d2s::Item>* stash , bool stash_expansion ,
                   bool belt_popup , bool cube_open ,
                   const NpcMenuState* npc_menu , const Speech* speech ,
                   const Automap* automap , const Store* store ,
                   int stat_pressed ,
                   const Npc* merc , const UnitState* merc_state ,
                   const std::string* merc_label ,
                   std::span<const Unit> extra_units , float player_rate ,
                   const Lighting* light , d2d::rules::Rain* rain , bool player_visible ,
                   const Unit* player_look ,     // its states' colour shift and overlays
                   bool show_items,               // Alt held: every ground item's name
                   const Hud& hud) {
    // Prefer the real tile-composited world when townE1.ds1 loaded; fall
    // back to the credits DC6 placeholder when it didn't (headless CI, a
    // stripped MPQ dir, etc.). Palette follows the render path: ACT1 for
    // the tiles, Sky for the credits DC6 which was authored against it.
    if (!level.dt1s.empty()) {
        set_phase(MainPhase::IngameClear);
        // Clear to black — tiles don't cover every subtile so an
        // uninitialized fb would leak the previous frame's contents.
        std::fill(framebuffer.begin(), framebuffer.end(), std::uint8_t{0});
        for (std::size_t i = 3; i < framebuffer.size(); i += 4) framebuffer[i] = 0xFF;
        set_phase(MainPhase::IngameFloor);   // render_world does floor+shadow+walls internally
        // Player: the camera follows them, so their feet sit on the camera
        // point (kW/2, kH/2 + kIsoH/2); render_world slots them into the
        // wall pass by depth. Wears the loaded save's gear, or the
        // class's starting gear.
        std::vector<Unit> units;
        units.reserve(level.npcs.size() + 1);
        if (player_visible && class_idx >= 0 && class_idx < 7)
            units.push_back({ cam_x, cam_y, &scene.composite(kUiToSaveClass[class_idx], player_mode, gfx),
                              player_dir, nullptr, player_mode_ms });
        if (!units.empty()) {
            units.back().rate = player_rate;
            units.back().overlay_class = 1;                 // FUN_006223a0: a player's Height2
            if (player_look) { units.back().shift = player_look->shift; units.back().overs = player_look->overs; }
        }
        // NPCs and objects, at their live position when they patrol.
        for (std::size_t i = 0; i < level.npcs.size(); ++i) {
            const auto& npc = level.npcs[i];
            const UnitState* state = i < npcs.size() ? &npcs[i] : nullptr;
            if (state && state->hidden) continue;
            const float x = state ? state->x : npc.x, y = state ? state->y : npc.y;
            if (std::abs(x - cam_x) >= 14 || std::abs(y - cam_y) >= 14) continue;
            const auto& anim = scene.npc_anim(npc, state && state->walking ? std::string_view("WL") : state && !state->mode.empty() ? state->mode : std::string_view(npc.mode));
            static const std::string none;
            // An object in a new mode keeps its name where it's Selectable in it (an open door).
            const bool unselectable = state && !state->mode.empty() && npc.root == "objects" && !(npc.selectable >> game::mode_index(state->mode) & 1);
            units.push_back({ x, y, &anim, state ? state->dir : 0, unselectable ? &none : &npc.name, state ? state->mode_ms : 0, int(i) });
            if (state && state->alert) units.back().overlay = &scene.npc_alert;
            units.back().shadow = npc.root != "objects";
        }
        // The neighbour levels' objects and NPCs (torches by the camp's
        // gate, Flavie, patrolling rogues), where they are now: D2 draws the
        // rooms round the player whichever level they're in. Their states
        // follow this level's in `npcs` (World::View::npc_states). Not
        // clickable from here (npc -3); quest-gated ones (Cain) left out.
        static const std::string no_name;
        std::size_t next_state = level.npcs.size();
        for (const auto& neighbour : level.nearby)
            for (const auto& npc : neighbour.level->npcs) {
                const UnitState* state = next_state < npcs.size() ? &npcs[next_state] : nullptr;
                ++next_state;
                if (npc.quest || (state && state->hidden)) continue;
                const float x = (state ? state->x : npc.x) + float(neighbour.dx), y = (state ? state->y : npc.y) + float(neighbour.dy);
                if (std::abs(x - cam_x) >= 14 || std::abs(y - cam_y) >= 14) continue;
                const auto mode = state && state->walking ? std::string_view("WL") : state && !state->mode.empty() ? state->mode : std::string_view(npc.mode);
                units.push_back({ x, y, &scene.npc_anim(npc, mode), state ? state->dir : 0, &no_name, state ? state->mode_ms : 0, -3 });
                units.back().shadow = npc.root != "objects";
            }
        if (merc && merc_state)                    // npc -2: not an NPC-menu unit
            units.push_back({ merc_state->x, merc_state->y,
                              &scene.npc_anim(*merc, merc_state->walking ? std::string_view("WL") : std::string_view("NU")),
                              merc_state->dir, merc_label, merc_state->mode_ms, -2 });
        units.insert(units.end(), extra_units.begin(), extra_units.end());
        if (hovered_npc && *hovered_npc != -1)            // last frame's: brighter (render_world)
            for (auto& unit : units) if (unit.npc == *hovered_npc && unit.name) unit.highlight = true;
        std::pair<const Unit*, std::array<int, 4>> hovered{ nullptr, {} };
        std::vector<std::pair<const Unit*, std::array<int, 4>>> items;
        render_world(framebuffer, scene, level, cam_x, cam_y, elapsed_ms, units, mouse_x, mouse_y, &hovered, light, rain, show_items ? &items : nullptr);
        if (rain) draw_rain(framebuffer, *rain);
        if (hovered_npc) *hovered_npc = hovered.first ? hovered.first->npc : -1;
        // Alt ("Show Items"): each ground item's name in a dark box over it,
        // nudged up clear of the ones already placed; the label under the
        // mouse is the item it points at (a click picks it up).
        // ponytail: game.exe's label layout isn't traced (box padding, the
        // stacking order, the hovered label's own colour).
        if (show_items) {
            const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
            std::ranges::sort(items, {}, [](const auto& entry) { return -entry.second[3]; });   // nearest the bottom first
            std::vector<std::array<int, 4>> placed;
            const int line_height = scene.font.line_height();
            for (const auto& [unit, box] : items) {
                if (!unit->name || unit->name->empty()) continue;
                const int width = scene.font.measure(*unit->name) + 8;
                std::array<int, 4> rect{ (box[0] + box[2]) / 2 - width / 2, box[1] - line_height - 4, 0, 0 };
                rect[2] = rect[0] + width; rect[3] = rect[1] + line_height + 2;
                for (bool moved = true; moved;) {
                    moved = false;
                    for (const auto& placed_rect : placed)
                        if (rect[0] < placed_rect[2] && placed_rect[0] < rect[2] && rect[1] < placed_rect[3] && placed_rect[1] < rect[3]) { const int dy = rect[3] - placed_rect[1]; rect[1] -= dy; rect[3] -= dy; moved = true; }
                }
                placed.push_back(rect);
                for (int y = std::max(rect[1], 0); y < std::min(rect[3], int(kScreenHeight)); ++y)
                    for (int x = std::max(rect[0], 0); x < std::min(rect[2], int(kScreenWidth)); ++x) {
                        auto* pixel = &framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4];
                        pixel[0] = std::uint8_t(pixel[0] / 4); pixel[1] = std::uint8_t(pixel[1] / 4); pixel[2] = std::uint8_t(pixel[2] / 4);
                    }
                scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, rect[0] + 4, rect[1] + 1, *unit->name, unit->rgb[0], unit->rgb[1], unit->rgb[2]);
                if (mouse_x >= rect[0] && mouse_x < rect[2] && mouse_y >= rect[1] && mouse_y < rect[3]) {
                    if (hovered_npc) *hovered_npc = unit->npc;
                    hovered = { nullptr, {} };              // the label names it: no second name
                }
            }
        }
        // Name over whatever the cursor points at, centred above it.
        if (hovered.first && (hovered.first->npc > -10 || hovered.first->npc <= -1000)) {   // monsters: their bar at the top
            const auto& hovered_name = *hovered.first->name;
            const auto& box  = hovered.second;
            const auto& colour  = hovered.first->rgb;
            const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
            scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, (box[0] + box[2]) / 2 - scene.font.measure(hovered_name) / 2,
                               box[1] - scene.font.line_height() - 2, hovered_name, colour[0], colour[1], colour[2]);
        }
        if (inventory && class_idx >= 0 && class_idx < 7)
        {
            // With a store open, your items show what the vendor pays ("Sell value: ", 0xd03).
            std::function<std::string(const d2d::d2s::Item&)> sell_price;
            if (store && store->npc >= 0)
                sell_price = [&](const d2d::d2s::Item& item) {
                    return string_id(scene, 0xd03) + std::to_string(d2d::rules::item_price(scene.rules, item, store->npc_id, true, store->header));
                };
            draw_inventory(framebuffer, scene, scene.inv_layout[std::size_t(kUiToSaveClass[class_idx])], *inventory,
                           mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1, &sell_price);
            if (hud_stats) draw_gold(framebuffer, scene, *hud_stats, false);
        }
        if (char_stats) draw_char_panel(framebuffer, scene, *char_stats, panel ? *panel : PanelStats{}, name, class_idx, stat_pressed);
        if (store && store->npc >= 0)
        {
            draw_store(framebuffer, scene, *store, mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1);
            if (hud_stats) draw_gold(framebuffer, scene, *hud_stats, true);
        }
        if (stash) {
            const int expansion_index = stash_expansion ? 1 : 0;
            if (cube_open)
                draw_storage(framebuffer, scene, *stash, scene.cube_panel, scene.cube_layout, 4, mouse_x, mouse_y,
                             hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1);
            else
                draw_storage(framebuffer, scene, *stash, scene.stash_panel[std::size_t(expansion_index)], scene.stash_layout[std::size_t(expansion_index)], 5,
                             mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1);
        }
        if (automap) draw_automap(framebuffer, scene, *automap, cam_x + float(level.world_x), cam_y + float(level.world_y));
        if (npc_menu) draw_npc_menu(framebuffer, scene, *npc_menu, mouse_x, mouse_y, elapsed_ms);
        if (speech) draw_speech(framebuffer, scene, *speech, elapsed_ms);
        if (hud_stats) draw_hud(framebuffer, scene, *hud_stats, hud, mouse_x, mouse_y);
        if (belt) draw_belt(framebuffer, scene, *belt, mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1,
                            belt_popup);
        // Dev overlay: a red dot on every blocked subtile around the camera.
        if (g_debug_collision) {
            const int center_x = int(cam_x * 5), center_y = int(cam_y * 5);
            for (int screen_y = center_y - 60; screen_y <= center_y + 60; ++screen_y)
                for (int screen_x = center_x - 60; screen_x <= center_x + 60; ++screen_x) {
                    const float world_x = (float(screen_x) + 0.5f) / 5, world_y = (float(screen_y) + 0.5f) / 5;
                    if (!level.blocked(world_x, world_y)) continue;
                    const int pixel_x = int(kScreenWidth) / 2 + int(std::lround(((world_x - cam_x) - (world_y - cam_y)) * (kIsoW / 2)));
                    const int pixel_y = int(kScreenHeight) / 2 + kIsoH / 2 + int(std::lround(((world_x - cam_x) + (world_y - cam_y)) * (kIsoH / 2)));
                    for (int offset_y = -1; offset_y <= 1; ++offset_y)
                        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
                            const int x = pixel_x + offset_x, y = pixel_y + offset_y;
                            if (x < 0 || y < 0 || x >= int(kScreenWidth) || y >= int(kScreenHeight)) continue;
                            auto* pixel = framebuffer.data() + (std::size_t(y) * kScreenWidth + std::size_t(x)) * 4;
                            pixel[0] = 255; pixel[1] = 0; pixel[2] = 0;
                        }
                }
        }
        set_phase(MainPhase::IngameHudText);
    } else {
        std::fill(framebuffer.begin(), framebuffer.end(), std::uint8_t{0});
        for (std::size_t i = 3; i < framebuffer.size(); i += 4) framebuffer[i] = 0xFF;
        blit_dc6_grid(framebuffer, scene.credits_bg, scene.pal, 0, 0, scene.bg_tiles_across);
    }
}

}  // namespace d2d::client
