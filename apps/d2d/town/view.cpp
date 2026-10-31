// SPDX-License-Identifier: GPL-3.0-or-later
// Town (town.hpp): the view to units: life bars, overlays, the frame's light, the automap's
// frame. The rest is in this directory.
#include "town.hpp"

#include "common.hpp"
#include "ingame.hpp"
#include "internal.hpp"
#include "panels.hpp"
#include "party.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include "tile_blit.hpp"
#include "ui.hpp"
#include "world_view.hpp"

#include <combat.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <light.hpp>
#include <log.hpp>
#include <monsters.hpp>
#include <object_ids.hpp>
#include <overlays.hpp>
#include <rules.hpp>
#include <uniques.hpp>
#include <userdir.hpp>
#include <weather.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::client {

void draw_monster_bar(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Monster& monster) {
    const auto& name = monster.npc.name;
    if (name.empty() || !monster.alive()) return;
    const int width = std::max(scene.font.measure(name) + 20, 120), height = scene.font.line_height() + 4;
    const int left = int(kScreenWidth) / 2 - width / 2, top = 10;
    const int filled = width * std::clamp(monster.hit_points, 0, monster.stats.hit_points) / std::max(monster.stats.hit_points, 1);
    for (int y = top; y < top + height; ++y)
        for (int x = left; x < left + width; ++x) {
            auto* pixel = framebuffer.data() + (std::size_t(y) * kScreenWidth + std::size_t(x)) * 4;
            const bool filled_here = x - left < filled;
            pixel[0] = filled_here ? 0x88 : 0x20; pixel[1] = filled_here ? 0x08 : 0x10; pixel[2] = filled_here ? 0x08 : 0x10;
        }
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    // Its name's colour (FUN_00454ad0): gold for uniques, blue for champions.
    const int colour = d2d::rules::bar_name_colour(monster.boss, scene.monsters.types[std::size_t(monster.type)].id);
    const std::array<std::uint8_t, 3> tint = colour == d2d::rules::kNameGold ? std::array<std::uint8_t, 3>{ 199, 179, 119 }
                                             : colour == d2d::rules::kNameBlue ? std::array<std::uint8_t, 3>{ 105, 105, 255 } : std::array<std::uint8_t, 3>{ 255, 255, 255 };
    scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, int(kScreenWidth) / 2 - scene.font.measure(name) / 2, top + 2, name, tint[0], tint[1], tint[2]);
    // The label under it (uniques and minions, d2d::rules::kUModLabel):
    // Demon / Undead, then its mods; a minion's "Minion". Champions have
    // none: their name says it.
    std::string label;
    using d2d::rules::Boss;
    const auto& type_info = scene.monsters.types[std::size_t(monster.type)];
    const std::uint16_t lead = type_info.demon ? d2d::rules::kDemonLabel : type_info.undead ? d2d::rules::kUndeadLabel : 0;
    if (lead) label = string_id(scene, lead);
    if (monster.boss == Boss::minion)
        label = (lead ? label + string_id(scene, d2d::rules::kMinionSpace) : "") + string_id(scene, d2d::rules::kMinionLabel);
    else if (monster.boss == Boss::unique || monster.boss == Boss::superunique)
        for (const int id : monster.mods) {
            if (id < 0 || std::size_t(id) >= d2d::rules::kUModLabel.size() || !d2d::rules::kUModLabel[std::size_t(id)]) continue;
            const auto next = label + (label.empty() ? "" : " ") + string_id(scene, d2d::rules::kUModLabel[std::size_t(id)]);
            if (scene.font.measure(next) > 480) break;
            label = next;
        }
    else label.clear();
    if (!label.empty())
        scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, int(kScreenWidth) / 2 - scene.font.measure(label) / 2, top + height + 2, label);
}

void view_units(const Scene& scene, const View& view, float camera_x, float camera_y, const std::string* merc_label, std::vector<Unit>& out,
                std::span<const View::Shot> effects , std::uint32_t now_ms , const std::string* corpse_name , int cls ,
                StateClock* clk) {
    auto in_view = [&](float x, float y) { return std::abs(x - camera_x) < 14 && std::abs(y - camera_y) < 14; };
    for (std::size_t i = 0; i < view.ground.size(); ++i) {
        const auto& ground_item = view.ground[i];
        if (!in_view(ground_item.x, ground_item.y)) continue;
        Unit unit{ ground_item.x, ground_item.y, nullptr, 0, &ground_item.label, ground_item.now_ms, -1000 - int(i) };
        unit.sprite = scene.flippy(ground_item.item.code);
        if (unit.sprite) unit.sprite_file = scene.rules.item_info.at(ground_item.item.code).flippy;
        unit.cmap = scene.item_map(ground_item.item, false);   // FUN_00471ec0: the character's colours
        unit.rgb = ground_item.rgb;
        out.push_back(std::move(unit));
    }
    for (const auto& fire : view.fires) out.push_back({ fire.x, fire.y, &scene.npc_anim(*fire.npc, fire.npc->mode), 0, nullptr, fire.born, -2 });
    // Local co-op's fallen: their bodies where they fell, lying as a corpse lies.
    for (const auto& body : view.bodies) {
        const auto& anim = scene.composite(body.cls, kModeDT, body.gfx);
        out.push_back({ body.x, body.y, &anim, body.dir, nullptr, now_ms - (anim.length_ms() - 1), -5000 - body.id });
    }
    // The player's corpses: lying (DT's last frame, as the dead player is
    // drawn), in what they wore; named by the player (-3000 - which).
    for (const auto& corpse : view.corpses) {
        const auto& anim = scene.composite(cls, kModeDT, corpse.gfx);
        out.push_back({ corpse.x, corpse.y, &anim, corpse.dir, corpse_name, now_ms - (anim.length_ms() - 1), -3000 - corpse.which });
    }
    // Town portals: opening (OP, FrameCnt1 15 at FrameDelta 200/256 a tick:
    // 768 ms), then ON; named by where they lead (-2000 - which). Tristram
    // Cain's (which kGamePortals, object 189: its token tP is TP's files, MPQ names
    // being case-blind) has no name: Selectable0..2 all 0.
    for (const auto& portal : view.portals) {
        const Level* destination = portal.which < int(d2d::game::World::kGamePortals) ? scene.level(portal.destination) : nullptr;
        const bool opening = now_ms - portal.born < kPortalOpenMs;
        out.push_back({ portal.x, portal.y, &scene.npc_anim(scene.town_portal, opening ? "OP" : "ON"), 0, destination ? &destination->name : nullptr,
                        opening ? portal.born : portal.born + kPortalOpenMs, -2000 - portal.which });
        out.back().shadow = false;
    }
    if (view.merc) {
        out.push_back({ view.merc->unit.x, view.merc->unit.y, &scene.npc_anim(*view.merc->npc, view.merc->mode), view.merc->unit.dir,
                        view.merc->mode == "DT" ? nullptr : merc_label, view.merc->unit.mode_ms, -2 });
        out.back().hold = view.merc->mode == "DT";
        if (view.merc->mode == "GH") out.back().get_hit_rate = std::uint16_t(d2d::rules::get_hit_rate(int(out.back().anim->speed ? out.back().anim->speed : out.back().anim->cof_speed), view.merc->fhr));
    }
    // ponytail: a party member's merc (party_pets, "hireable") in GH has no FHR here: its get-hit plays at the plain rate and loops.
    for (const auto* pets : { &view.pets, &view.party_pets })
        for (const auto& pet : *pets) {
            out.push_back({ pet.unit.x, pet.unit.y, &scene.npc_anim(pet.npc, pet.mode), pet.unit.dir, nullptr, pet.unit.mode_ms, -3 });
            out.back().hold = pet.mode == "DT";
        }
    // A joined game's other players, in what they wear (0x9d), walking,
    // standing or in a skill's mode.
    const bool in_town = view.level && d2d::rules::level_ids::is_town(view.level->id);
    for (std::size_t i = 0; i < view.others.size(); ++i) {                   // -4000 - i: a click asks them to trade
        const auto& other = view.others[i];
        if (!in_view(other.unit.x, other.unit.y)) continue;
        const int mode = other.mode >= 0 ? other.mode : other.running ? kModeRN : other.unit.walking ? (in_town ? kModeTW : kModeWL) : (in_town ? kModeTN : kModeNU);
        out.push_back({ other.unit.x, other.unit.y, &scene.composite(other.cls, mode, other.gfx), other.unit.dir, &other.name, other.unit.mode_ms, -4000 - int(i) });
        out.back().overlay_class = 1;                 // FUN_006223a0: a player's Height2
        // A walk, run or town walk plays at their walk's rate, as our own (FUN_00623f50).
        if (other.mode < 0 && !other.ghost && (mode == kModeWL || mode == kModeRN || mode == kModeTW))
            if (const auto& walk_anim = *out.back().anim; walk_anim.speed || walk_anim.cof_speed)
                out.back().rate = float(d2d::rules::moving_anim_rate(mode == kModeRN, other.walk_percent)) / float(walk_anim.speed ? walk_anim.speed : walk_anim.cof_speed);
        if (other.ghost) { out.back().spirit = true; out.back().shadow = false; }   // local co-op's dead, walking along
        dress(scene, out.back(), -2 - int(other.id & 0xffffff), other.states, clk);
    }
    auto shot = [&](const View::Shot& shot_state) {
        Unit unit{ shot_state.x, shot_state.y, nullptr, shot_state.dir, nullptr, shot_state.born, -1 };
        unit.missile = shot_state.info;
        out.push_back(std::move(unit));
    };
    for (const auto& shot_state : view.missiles) shot(shot_state);
    for (const auto& shot_state : effects) shot(shot_state);                 // the client's own (the Den's light beams)
    for (std::size_t i = 0; i < view.monsters.size(); ++i) {
        const auto& monster = view.monsters[i];
        if (monster.corpse_used || !in_view(monster.unit.x, monster.unit.y)) continue;
        // A buried maggot: S1 backwards (seq_maggotdown's rows), then held on its first frame; not hovered.
        // unverified (source: MonSeq seq_maggotdown): how game.exe's client draws one under the sand.
        const bool down = monster.buried, row = !down && monster.seq_frame >= 0;   // a sequence: its row's mode and frame
        const auto& anim = scene.npc_anim(monster.npc, down ? std::string_view("S1") : row ? monster.seq_mode : std::string_view(monster.mode));
        const auto mpf = anim.ms_per_frame();
        // No name, so no hover, for a converted one, and a click there walks.
        // game.exe's hit test (FUN_00466870) drops a unit FUN_00465c60 refuses: outside town a
        // monster of the player's alignment (cursor-units.md).
        const bool selectable = monster.targetable() && !monster.ally_not_interact(scene);
        out.push_back({ monster.unit.x, monster.unit.y, &anim, monster.unit.dir, selectable ? &monster.npc.name : nullptr,
                        row ? now_ms - mpf * std::uint32_t(monster.seq_frame) - mpf / 2 : down && monster.mode != "S1" ? 0u : monster.unit.mode_ms, -10 - int(i) });
        out.back().reverse = down;
        out.back().hold = !row && monster.mode == "DT";             // a death outlasting its frames (the host's 1500 ms) stays on the last, not back to the first
        if (!row && !down && monster.mode == "GH") out.back().get_hit_rate = std::uint16_t(d2d::rules::get_hit_rate(int(anim.speed ? anim.speed : anim.cof_speed), monster.props.fhr));   // struck: its frame steps at the get-hit rate (rules::get_hit_frame)
        out.back().overlay_class = monster.npc.overlay_class;
        out.back().cmap = scene.monster_map(monster.npc);
        out.back().ghostly = monster.alive() && std::ranges::find(monster.mods, d2d::rules::umod::ghostly) != monster.mods.end();
        dress(scene, out.back(), monster.id, monster_states(scene, monster, now_ms, view.aura), clk);
    }
}

// Local co-op's Resurrect (docs/design/local-coop.md): a gold aura round a
// body being raised, on the ground (an isometric ellipse) with a soft rim,
// a column of light and flecks rising; faint and slow at first, brighter and
// strobing faster (0.6 Hz to 5 Hz) as the channel's share done grows.
void draw_raise_aura(std::vector<std::uint8_t>& framebuffer, int centre_x, int centre_y, float raised, std::uint32_t now_ms) {
    const float progress = std::clamp(raised, 0.f, 1.f), seconds = float(now_ms) / 1000.f;
    const float freq = 0.6f + 4.4f * std::pow(progress, 1.5f);
    const float strobe = 0.5f + 0.5f * std::sin(2 * std::numbers::pi_v<float> * freq * seconds);
    const float level = (0.18f + 0.82f * progress) * (0.65f + 0.35f * strobe);
    const float radius_x = 34 + 18 * progress, radius_y = 15 + 8 * progress, column = 14 + 10 * progress;
    constexpr std::array<float, 3> kGold{ 255, 196, 72 };
    const auto add = [&](int x, int y, float light) {
        if (x < 0 || x >= int(kScreenWidth) || y < 0 || y >= int(kScreenHeight) - 48 || light <= 0) return;   // not over the HUD
        auto* pixel = &framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4];
        for (std::size_t k = 0; k < 3; ++k) pixel[k] = std::uint8_t(std::min(255.f, float(pixel[k]) + kGold[k] * 1.1f * light));
    };
    for (int y = centre_y - 100; y <= centre_y + int(radius_y * 1.4f); ++y)
        for (int x = centre_x - int(radius_x * 1.4f); x <= centre_x + int(radius_x * 1.4f); ++x) {
            const float dx = float(x - centre_x) / radius_x, dy = float(y - centre_y) / radius_y, apart = std::hypot(dx, dy);
            const float glow = std::pow(std::max(1 - apart, 0.f), 1.6f), halo = std::exp(-(apart - 1) * (apart - 1) / 0.05f) * 0.45f;
            const float rising = std::clamp(float(centre_y + 6 - y) / 90.f, 0.f, 1.f) * std::clamp(float(y - (centre_y - 95)) / 95.f, 0.f, 1.f);
            const float beam = std::pow(std::max(1 - std::abs(float(x - centre_x)) / column, 0.f), 2.f) * rising;
            add(x, y, (glow + halo) * level + beam * level * progress * 0.55f);
        }
    d2d::rules::Rng flecks{ now_ms / 50 + 1 };                // a few each frame, drifting up
    for (int k = 0; k < int(14 * progress); ++k) {
        const int x = centre_x + int(flecks.range(-int(radius_x * 0.6f), int(radius_x * 0.6f)));
        const int y = centre_y - int(flecks.range(0, int(70 * progress)));
        for (int fleck_y = y - 1; fleck_y <= y; ++fleck_y)
            for (int fleck_x = x - 1; fleck_x <= x; ++fleck_x) add(fleck_x, fleck_y, level * 0.9f);
    }
}

void view_overlays(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const View& view, int hovered) {
    if (hovered >= 0) draw_monster_bar(framebuffer, scene, view.monsters[std::size_t(hovered)]);
    else if (const int attacked = view.monster(view.attack); attacked >= 0) draw_monster_bar(framebuffer, scene, view.monsters[std::size_t(attacked)]);
    // The death screen (FUN_00453100): youdiedhardcore then youdiedinst,
    // centred, from H/2 - 94 down 48 px each; then in Font30, red, centred,
    // 48 px apart: string 0x13e8 "Your deeds of valor..." (hardcore) or
    // 0x13e6 "Death takes its toll of %d Gold" (when goldlost > 0), and in
    // Nightmare / Hell (softcore) 0x13e7 "You have lost experience".
    // ponytail: FUN_00502680's anchor taken as the cel's bottom, centred.
    if (view.pmode == kModeDD && (!view.ghost || view.all_dead)) {   // local co-op's ghost: no death screen while a partner lives
        const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
        int y = int(kScreenHeight) / 2 - 0x5e;
        for (const auto* spr : { &scene.you_died, &scene.you_died_inst }) {
            int width = 0;                                   // its frames side by side (FUN_00502680)
            for (std::uint32_t k = 0; k < spr->frames_per_direction(); ++k) width += int(spr->frame(0, k).width);
            for (std::uint32_t k = 0, x = std::uint32_t(int(kScreenWidth) / 2 - width / 2); k < spr->frames_per_direction(); ++k) {
                const auto& frame = spr->frame(0, k);
                blit_sprite(framebuffer, frame, pal, int(x), y - int(frame.height) + 1);
                x += frame.width;
            }
            y += 0x30;
        }
        const auto& f30 = scene.font30.line_height() > 0 ? scene.font30 : scene.font;
        auto line = [&](const std::string& text) {
            if (!text.empty()) f30.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, int(kScreenWidth) / 2 - f30.measure(text) / 2, y - f30.line_height() + 1, text, 255, 77, 77);
            y += 0x30;
        };
        const bool hardcore = view.header.hardcore();
        if (hardcore) line(string_id(scene, 0x13e8));
        else if (view.gold_lost > 0) {
            auto fmt = string_id(scene, 0x13e6);
            if (const auto found = fmt.find("%d"); found != std::string::npos) fmt.replace(found, 2, std::to_string(view.gold_lost));
            line(fmt);
        }
        if (!hardcore && view.header.active_difficulty() > 0) line(string_id(scene, 0x13e7));
    }
}

std::pair<int, std::uint32_t> view_seq(const Scene& scene, int cls, const View& view, std::uint32_t now_ms) {
    const auto frame_index = std::size_t((now_ms - view.player.mode_ms) / std::max<std::uint32_t>(view.seq_frame_ms, 1));
    const auto& frame = view.seq[view.seq_loop ? frame_index % view.seq.size() : std::min(frame_index, view.seq.size() - 1)];
    const auto mpf = scene.composite(cls, frame.mode, view.gfx).ms_per_frame();
    return { frame.mode, now_ms - mpf * frame.frame - mpf / 2 };
}

Lighting frame_light(const Scene& scene, const View& view, float cam_x, float cam_y, std::span<const View::Shot> effects , int ambient ,
                     std::span<const Unit> units , const Unit* player_look , std::uint32_t now, const std::pair<float, float>* player_at) {
    Lighting lighting;
    if (!view.level || scene.act_lit[31].entries().empty()) return lighting;
    lighting.pal = &scene.act_lit;
    // The grid covers the view's corners (half the width in cells over 80
    // plus half the height over 40, halved, in subtiles) and the widest light
    // (18) past them, not game.exe's 48 round the player (bugs.md #11;
    // --toggle light_grid=off for that).
    const int half = (g_view_width / 2 / (kIsoW / 2) + (int(kScreenHeight) / 2 + kIsoH) / (kIsoH / 2)) * 5 / 2 + 18 + 1;
    if (ambient < 0) ambient = view.level->light >= 0 ? view.level->light : view.day.intensity();
    const auto [player_x, player_y] = player_at ? *player_at : std::pair{ cam_x, cam_y };
    lighting.ambient = ambient;
    if (g_light_grid) lighting.grid.reset(int(cam_x * 5), int(cam_y * 5), ambient, half * 2);
    else lighting.grid.reset(int(player_x * 5), int(player_y * 5), ambient);
    for (int j = 0; j < lighting.grid.grid_size; ++j)                            // what walls light (FUN_004756d0)
        for (int i = 0; i < lighting.grid.grid_size; ++i)
            lighting.grid.blocked[std::size_t(j * lighting.grid.grid_size + i)] =
                view.level->blocked((float(lighting.grid.origin_x + i) + 0.5f) / 5, (float(lighting.grid.origin_y + j) + 0.5f) / 5, 0x22);
    // Type 0 lights (the player's, objects') are shadowed by walls; type 1
    // (monsters', missiles') aren't (FUN_004755a0).
    struct Ease { int radius8 = 0; std::uint32_t changed_at = 0, seen = 0; };
    static std::unordered_map<std::uint64_t, Ease> eases;         // by light: its radius now, in eighths
    auto eased = [&](std::uint64_t key, int radius, int first) {
        const int want = std::clamp(radius, 0, 18) * 8;
        auto [found, fresh] = eases.try_emplace(key, Ease{ std::clamp(first, 0, 18) * 8, now, now });
        auto& ease = found->second;
        if (const auto steps = int((now - ease.changed_at) / 40); steps > 0) {
            ease.radius8 = ease.radius8 < want ? std::min(want, ease.radius8 + 8 * steps) : std::max(want, ease.radius8 - 8 * steps);
            ease.changed_at += std::uint32_t(steps) * 40;
        }
        ease.seen = now;
        return ease.radius8;
    };
    auto stamp8 = [&](float x, float y, int radius8, bool shadowed) {
        if (radius8 <= 0) return;
        lighting.stamps.push_back({ int(x * 40), int(y * 40), radius8, shadowed });
        if (shadowed) lighting.grid.stamp_shadowed(int(x * 40), int(y * 40), radius8, 255);
        else lighting.grid.stamp(int(x * 40), int(y * 40), radius8, 255);
    };
    auto stamp = [&](float x, float y, int radius, bool shadowed) { stamp8(x, y, std::min(radius, 18) * 8, shadowed); };
    auto lamp = [&](std::uint64_t key, float x, float y, int radius, bool shadowed, int first = -1) {
        stamp8(x, y, eased(key, radius, first < 0 ? radius : first), shadowed);
    };
    // A unit's light sits at its position + 4 eighths (FUN_00474160: (x >> 13) + 4);
    // objects' cells already hold that half subtile.
    constexpr float kHalf = 0.1f;                                   // 4 eighths of a subtile, in cells
    lamp(1, player_x + kHalf, player_y + kHalf, std::max(0, 13 + view.light_bonus), true);   // FUN_00460930: 13 + the bonus, capped at 18
    for (const auto& other : view.others) lamp(0x100000000ull + other.id, other.unit.x + kHalf, other.unit.y + kHalf, 13, true);   // every player's own (their bonus isn't sent)
    static constexpr std::array<std::string_view, 8> kModes{ "NU", "OP", "ON", "S1", "S2", "S3", "S4", "S5" };
    auto lit = [&](const Npc& npc, std::string_view mode) {
        const auto found = std::ranges::find(kModes, mode.empty() ? std::string_view(npc.mode) : mode);
        return npc.root == "objects" && found != kModes.end() ? int(npc.lit[std::size_t(found - kModes.begin())]) >> 1 : 0;   // an object's light is half its Lit (FUN_004bc580)
    };
    for (std::size_t i = 0; i < view.level->npcs.size(); ++i) {
        const auto& npc = view.level->npcs[i];
        const auto* state = i < view.npc_states.size() ? &view.npc_states[i] : nullptr;
        if (state && state->hidden) continue;
        lamp(2ull << 32 | i, state ? state->x : npc.x, state ? state->y : npc.y, lit(npc, state ? state->mode : std::string_view{}), true);
    }
    for (const auto& neighbour : view.level->nearby)                  // the torches over the level's edge
        for (const auto& npc : neighbour.level->npcs) stamp(npc.x + float(neighbour.dx), npc.y + float(neighbour.dy), lit(npc, {}), true);
    for (const auto& monster : view.monsters) if (monster.alive()) lamp(4ull << 32 | std::uint32_t(monster.id), monster.unit.x + kHalf, monster.unit.y + kHalf, monster.npc.light, false);
    for (const auto& portal : view.portals)                        // Lit1 (OP) while it opens, then Lit2 (ON)
        lamp(5ull << 32 | std::uint32_t(portal.which), portal.x, portal.y, int(scene.town_portal.lit[now - portal.born < kPortalOpenMs ? 1 : 2]) >> 1, true);
    for (const auto& missile : view.missiles) if (missile.info) stamp(missile.x, missile.y, missile.info->light, false);
    for (const auto& missile : effects) if (missile.info) stamp(missile.x, missile.y, missile.info->light, false);
    // States' overlays light their unit (Overlay.txt: InitRadius growing to
    // Radius, FUN_00474160 / FUN_00474290); a plain light, its colour
    // dropped like every light's here.
    auto overs = [&](const Unit& unit, float x, float y) {
        for (std::size_t k = 0; k < unit.overs.size(); ++k) {
            const auto& overlay = unit.overs[k];
            if (overlay.overlay->radius > 0 && d2d::rules::overlay_frame(now - overlay.start, overlay.overlay->rate, overlay.overlay->frames, overlay.once) >= 0)
                lamp(6ull << 56 ^ std::uint64_t(reinterpret_cast<std::uintptr_t>(overlay.overlay)) ^ std::uint64_t(std::uint32_t(unit.npc)) << 40 ^ overlay.start,
                     x, y, overlay.overlay->radius, false, overlay.overlay->init_radius);
        }
    };
    for (const auto& each_unit : units) overs(each_unit, each_unit.x, each_unit.y);
    if (player_look) overs(*player_look, player_x, player_y);
    std::erase_if(eases, [&](const auto& entry) { return now - entry.second.seen > 2000; });
    return lighting;
}

// The automap's options from Options -> Automap (kept in d2d.cfg, as
// game.exe keeps them in the registry) and this frame's marks.
auto Town::automap_frame() -> void {
        const auto& values = game_menu.value[4];
        automap.mini = character.expansion ? values[1] : 0;
        automap.fade = values[2];
        automap.center = values[3] != 0;
        automap.party = values[4] != 0;
        automap.names = values[5] != 0;
        const bool right_open = inv_open || tree_open, left_open = char_open || stash_open || cube_open || quest_log.open || waypoint.open || store.npc >= 0
                                                                || party.open || hireling.open;   // FUN_00455f20: UI 0x16, 0x24 left too
        automap.panel_side = right_open == left_open ? 0 : right_open ? 1 : 2;
        g_drawn.panel_side = automap.panel_side;
        automap.in_town = level && d2d::rules::level_ids::is_town(level->id);
        automap.portraits = portraits_shown;
        automap.marks.clear();
        int own_party = 0xffff;
        if (net_game)
            if (const auto self = net_game->units.find(std::uint64_t(net_game->self_id)); self != net_game->units.end()) own_party = self->second.party;
        const float world_x = level ? float(level->world_x) : 0.f, world_y = level ? float(level->world_y) : 0.f;
        // The units of the player's room and its near list (FUN_0045ac90),
        // coloured as FUN_00459bc0 picks: a dead player with their things
        // magenta, our merc and pets teal, interacting NPCs white (but the
        // Ancient statues) with their names in gold (Names on), town and
        // red portals yellow; then the party anywhere (FUN_0045ab60), green,
        // the others red; with Party on, the party's pets green too (a
        // joined game's: the host's monsters whose 0xac owner is in our party).
        const auto rooms = level && view.level == level ? missile_rooms(*world.game_data, *level, view.player.x, view.player.y) : std::vector<std::array<int, 4>>{};
        const auto close_by = [&](float x, float y) {
            const int sub_x = int(std::floor((x + world_x) * 5)), sub_y = int(std::floor((y + world_y) * 5));
            return std::ranges::any_of(rooms, [&](const auto& rect) { return sub_x >= rect[0] && sub_y >= rect[1] && sub_x < rect[0] + rect[2] && sub_y < rect[1] + rect[3]; });
        };
        const auto add = [&](float x, float y, std::array<int, 3> rgb, std::string name = {}, std::array<std::uint8_t, 3> name_rgb = {}) {
            automap.marks.push_back({ x + world_x, y + world_y, rgb, std::move(name), name_rgb });
        };
        constexpr std::array<int, 3> kMagenta{ 0xff, 0, 0xff }, kTeal{ 0x44, 0x70, 0x74 }, kWhite{ 0xf4, 0xf4, 0xf4 }, kYellow{ 0xf4, 0xf4, 0 };
        constexpr std::array<std::uint8_t, 3> kGold{ 199, 179, 119 }, kGreen{ 0, 255, 0 };
        for (const auto& corpse : view.corpses) if (close_by(corpse.x, corpse.y)) add(corpse.x, corpse.y, kMagenta);
        if (view.merc && view.merc->mode != "DT" && view.merc->mode != "DD" && close_by(view.merc->unit.x, view.merc->unit.y)) add(view.merc->unit.x, view.merc->unit.y, kTeal);
        for (const auto& pet : view.pets) {
            const auto* type = world.game_data->pet_type(pet.pettype);
            if (type && type->automap && pet.mode != "DT" && pet.mode != "DD" && close_by(pet.unit.x, pet.unit.y)) add(pet.unit.x, pet.unit.y, kTeal);
        }
        constexpr std::array<int, 3> kPartyPet{ 0x48, 0xa0, 0x34 };
        if (automap.party)
            for (const auto& pet : view.party_pets) {
                const auto* type = world.game_data->pet_type(pet.pettype);
                if (type && type->automap && pet.mode != "DT" && pet.mode != "DD" && close_by(pet.unit.x, pet.unit.y)) add(pet.unit.x, pet.unit.y, kPartyPet);
            }
        if (automap.party && net_game && own_party != 0xffff)
            for (const auto& monster : view.monsters) {
                const auto unit = net_game->units.find(std::uint64_t(1) << 32 | std::uint32_t(monster.id));
                if (unit == net_game->units.end() || unit->second.owner == 0 || unit->second.owner == net_game->self_id || !monster.alive()) continue;
                const auto owner = net_game->units.find(std::uint64_t(unit->second.owner));
                if (owner != net_game->units.end() && owner->second.party == own_party && close_by(monster.unit.x, monster.unit.y)) add(monster.unit.x, monster.unit.y, kPartyPet);
            }
        const auto npc_mark = [&](const Npc& npc, float x, float y) {
            if (!npc.interact || npc.id.starts_with("ancientstatue") || !close_by(x, y)) return;
            add(x, y, kWhite, automap.names ? npc.name : std::string{}, kGold);
        };
        if (view.level == level && level)
            for (std::size_t i = 0; i < level->npcs.size(); ++i) {
                const auto& npc = level->npcs[i];
                const auto* state = i < view.npc_states.size() ? &view.npc_states[i] : nullptr;
                if (state && state->hidden) continue;
                const float x = state ? state->x : npc.x, y = state ? state->y : npc.y;
                if (npc.root == "monsters") npc_mark(npc, x, y);
                else if (npc.object_id == d2d::rules::object_ids::kPermanentPortal && close_by(x, y)
                         && !std::ranges::contains(std::array{ 0x6f, 0x70, 0x75, 0x7d, 0x7e, 0x7f }, level->id)) add(x, y, kYellow);
            }
        for (const auto& monster : view.monsters) if (monster.alive()) npc_mark(monster.npc, monster.unit.x, monster.unit.y);
        for (const auto& portal : view.portals)
            if (close_by(portal.x, portal.y) && (portal.which < int(d2d::game::World::kGamePortals) || !std::ranges::contains(std::array{ 0x6f, 0x70, 0x75, 0x7d, 0x7e, 0x7f }, level->id)))
                add(portal.x, portal.y, kYellow);
        for (const auto& other : view.others) {
            bool same_party = !net_game;                                 // local co-op: one party
            if (net_game)
                if (const auto unit = net_game->units.find(std::uint64_t(other.id)); unit != net_game->units.end())
                    same_party = own_party != 0xffff && unit->second.party == own_party;
            if (same_party ? automap.party : close_by(other.unit.x, other.unit.y))
                add(other.unit.x, other.unit.y, same_party ? std::array<int, 3>{ 0, 0xff, 0 } : std::array<int, 3>{ 0xff, 0, 0 },
                    same_party && automap.names ? other.name : std::string{}, kGreen);
        }
        // The top right (FUN_0045ad60): the level's name, the version, the
        // difficulty past Normal (0x1057 of 0x1423 / 0x1422), EXPANSION (0x58ca).
        // ponytail: a joined game's name and password lines aren't shown.
        automap.lines.clear();
        if (level) automap.lines.push_back(level->name);
        automap.lines.emplace_back("v 1.14d");
        if (const int difficulty = net_game ? net_game->difficulty : character.header.active_difficulty(); difficulty == 1 || difficulty == 2) {
            auto line = string_id(*scene, 0x1057);
            if (const auto spot = line.find("%s"); spot != std::string::npos) line.replace(spot, 2, string_id(*scene, difficulty == 1 ? 0x1423 : 0x1422));
            automap.lines.push_back(std::move(line));
        }
        if (character.expansion) automap.lines.push_back(string_id(*scene, 0x58ca));
        const std::array<std::pair<std::string, std::string>, 6> now = { {
            { "automap_mode", std::to_string(values[1]) }, { "automap_fade", std::to_string(values[2]) }, { "automap_center", std::to_string(values[3]) },
            { "automap_party", std::to_string(values[4]) }, { "automap_names", std::to_string(values[5]) }, { "automap_left", automap.left ? "1" : "0" } } };
        if (now != automap_saved) {
            if (automap_saved_once && !cfg_file.empty() && !d2d::userdir::save_cfg(cfg_file, { now.begin(), now.end() }))
                d2d::log::warn("d2d.cfg: the automap settings not saved");
            automap_saved = now;
            automap_saved_once = true;
        }
    }

// The weather at the join (FUN_00472890): its colours as the palette's
// nearest now, full's base by the act, snow or not.
auto Town::rain_join() -> void {
        const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
        for (int tone = 0; tone < 12; ++tone) {
            for (int kind = 0; kind < 3; ++kind) {
                const auto rgb = d2d::rules::rain_rgb(kind, tone);
                rain.index[std::size_t(kind)][std::size_t(tone)] = pal.nearest(rgb[0], rgb[1], rgb[2]);
            }
            const auto grey = d2d::rules::snow_rgb(tone);
            rain.index[3][std::size_t(tone)] = pal.nearest(grey[0], grey[1], grey[2]);
        }
        for (std::size_t i = 0; i < 4; ++i) {          // FUN_006019f0: the overlays' frame counts
            rain.splash_frames[i] = int(scene->rain_splash[i].frames().size());
            rain.bubble_frames[i] = int(scene->mud_bubble[i].frames().size());
        }
        rain.join(rain.rng, level->id, d2d::rules::level_ids::act_of(level->id));
    }

}  // namespace d2d::client
