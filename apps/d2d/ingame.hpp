// render_ingame: world + every in-game panel composed per frame.
#pragma once

#include "common.hpp"
#include "panels.hpp"
#include "scene.hpp"
#include "ui.hpp"
#include "world_view.hpp"

namespace d2d::client {

// The rain over the world (FUN_00473910 → FUN_00473470): each drop a line
// along the wind, cut where it lands, a landed one a dot; the day's drops
// half see-through. Nothing over the bottom panel (47 pixels).
void draw_rain(std::vector<std::uint8_t>& framebuffer, const d2d::rules::Rain& rain);

void render_ingame(std::vector<std::uint8_t>& framebuffer,
                   const Scene& scene,
                   const Level& level,
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
                   std::span<const UnitState> npcs = {},
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
                   const Automap* automap = nullptr, const Store* store = nullptr,
                   int stat_pressed = -1,
                   const Npc* merc = nullptr, const UnitState* merc_state = nullptr,
                   const std::string* merc_label = nullptr,
                   std::span<const Unit> extra_units = {}, float player_rate = 1.f,
                   const Lighting* light = nullptr, d2d::rules::Rain* rain = nullptr, bool player_visible = true,
                   const Unit* player_look = nullptr,     // its states' colour shift and overlays
                   bool show_items = false);

}  // namespace d2d::client
