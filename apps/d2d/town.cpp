// SPDX-License-Identifier: GPL-3.0-or-later
// The in-game client (town.hpp): Town, the view to units, the frame's
// light, the HUD overlays.
#include "town.hpp"

#include "audio.hpp"
#include "common.hpp"
#include "controls.hpp"
#include "cursor.hpp"
#include "gamepad.hpp"
#include "gamepad_map.hpp"
#include "ingame.hpp"
#include "items.hpp"
#include "keyconfig.hpp"
#include "panels.hpp"
#include "party.hpp"
#include "load.hpp"
#include "platform.hpp"
#include "roster.hpp"
#include "scene.hpp"
#include "skilltree.hpp"
#include "speech_sound.hpp"
#include "store.hpp"
#include "tile_blit.hpp"
#include "ui.hpp"
#include "world_view.hpp"

#include <combat.hpp>
#include <cues.hpp>
#include <d2gs/c2s.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <light.hpp>
#include <log.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>
#include <shake.hpp>
#include <screenshot.hpp>
#include <skills.hpp>
#include <object_sounds.hpp>
#include <outdoor.hpp>
#include <overlays.hpp>
#include <sound_ids.hpp>
#include <uniques.hpp>
#include <userdir.hpp>
#include <weather.hpp>

#include <algorithm>
#include <fstream>
#include <ios>
#include <iterator>
#include <cctype>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <exception>
#include <format>
#include <memory>
#include <numbers>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
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

// Town
auto Town::publish() -> void {
        auto& owner = focused();                        // local co-op: the screen's player
        world.act_as(owner);
        auto out = world.view();
        world.act_as(world.lead());
        out.sounds = std::move(world.cues.due);
        world.cues.due.clear();
        out.events = std::move(owner.events);
        owner.events.clear();
        net.to_client = encode_view(*scene, out, view_enc);
        // D2D_VIEW_STATS=1: each View's size in the log (the replication budget).
        if (std::getenv("D2D_VIEW_STATS")) d2d::log::info("VIEWSTAT total={} monsters={} ground={} items={}", net.to_client.size(), out.monsters.size(), out.ground.size(), out.items.size());
        if (!apply_view(*scene, net.to_client, view)) {         // lost track: the next one is whole
            d2d::log::warn("a View didn't apply ({} bytes)", net.to_client.size());
            view_enc.reset();
            return;
        }
        if (!net_game && out.merc && view.merc) view.merc->fhr = out.merc->fhr;   // the wire has none: a local game keeps what World::view() computed
        level = view.level;
        if (net_game) net_overlay();
        cues.due.insert(cues.due.end(), view.sounds.begin(), view.sounds.end());
        if (!view.has_character) return;
        character.header = view.header; character.stats = view.stats; character.items = view.items;
        // A weapon switch brought the other set's skills: the buttons take them.
        if (int(view.header.weapon_set) != skillbar_set) {
            skillbar.left = int(view.header.left_skill); skillbar.right = int(view.header.right_skill); skillbar.left_item = skillbar.right_item = -1;
            skillbar_set = int(view.header.weapon_set);
        }
        character.expansion = view.header.expansion();
        character.panel = panel_stats(*scene, character.header, character.items, character.stats);
        character.panel.attack = view.attack_lines;
        // A quest's log state sent (S->C 0x5d, FUN_004a2cb0): the Quest Log
        // button, or the open log picks it.
        // ponytail: a change in the View's log state stands in for each send
        // (FUN_00544190); none at the game's first View (the join sends none).
        for (std::size_t quest = 1; !net_game && quest < view.quest_log.size(); ++quest)   // a joined game's come as 0x5d (net_quests)
            if (quest_log.sent_known && view.quest_log[quest] && view.quest_log[quest] != quest_log.sent[quest]) quest_log_notify(quest_log, int(quest));
        quest_log.sent = view.quest_log;
        quest_log.sent_known = true;
        held = view.held;
        if (view.store) {
            const auto keep = std::move(store);
            store = *view.store;
            if (keep.npc == store.npc) { store.tab = keep.tab; store.mode = keep.mode; store.pressed = keep.pressed; }
            else store.first_tab();                 // the tab isn't sent: the World's pick again
        } else {
            store = {};
        }
        hire_offers = view.hire_offers;
        if (const auto found = scene->mercs.find(character.header.merc_type); view.merc && found != scene->mercs.end())
            merc_label = merc_name(*scene, found->second, character.header.merc_name);
    }

// Where the automap's files go: beside the save (none with saving off).
static std::filesystem::path automap_dir(const World& world) { return world.characters ? world.characters->dir : std::filesystem::path{}; }

auto Town::enter() -> void {
        shake = {}; quest_quakes.clear(); shake_rng = d2d::rules::Rng{ 666 };
        automap.cells.clear();                    // a new game: what the files beside the save kept
        automap.placed.clear();
        other_automaps.clear();
        quest_log = {};                           // done animations play again in a new game
        alert_clocks.clear();                      // Town is reused when a second game starts on the same level
        alert_level = nullptr;
        mini = {};                                // open (FUN_004567f0; ponytail: not the "Mini Panel" registry value)
        game_menu.open = false;
        controls.open = false;
        world.enter(character);
        if (scene) load_keys();
        coop.clear();                             // a new game: its first player alone
        lead_colour = 0;
        focus = 0;
        skillbar.new_game();
        hand_items.clear();
        publish();
        if (scene && level) rain_join();
        if (scene && level) load_automap(automap, automap_dir(world), character.name, scene->map_seed, level->layer, int(scene->automap_cels.frames_per_direction()));
    }

// The automap's new cells go out with the save, as on leaving a game
// (FUN_0045a5c0).
auto Town::save() -> std::string {
        if (scene && level) save_automap(automap, automap_dir(world), character.name, scene->map_seed, level->layer);
        world.each_player([&](World::Player& other) {    // the other co-op players' characters too
            if (&other == &hero) return;
            if (const auto why = world.save(); !why.empty()) d2d::log::warn("{}: {}", other.character.name, why);
        });
        return world.save();
    }

auto Town::operate(int npc_index, std::uint32_t frame_ms, int force ) -> void { world.operate(npc_index, frame_ms, force); }

// A command to the World, and in a joined game to the host as game.exe's
// client sends it: walks and runs (0x01 / 0x03) to the same act subtile
// d2d's player heads for, the run toggle (0x53 / 0x54). The World still
// walks the player here; the host walks it there on the same map.
// Skills go by the host's unit ids (d2d's monster ids are those in a
// joined game); a melee swing walks up here and plays for show, the host
// walks up there and hits. Objects and waypoints by the host unit nearest
// d2d's.
// NPC talk and trade stay local (the host's gold and items don't follow);
// belt, item moves, stat and skill points aren't sent: net-join-plan.md M9.
auto Town::send(const Command& command) -> void {
        if (!net_game || !level) { net.send(command); return; }
        auto subtile_x = [&](float cell_x) { return (cell_x + float(level->world_x)) * 5.f; };
        auto subtile_y = [&](float cell_y) { return (cell_y + float(level->world_y)) * 5.f; };
        if (const auto* move = std::get_if<cmd::Move>(&command)) {
            net_game->move_to(subtile_x(move->x), subtile_y(move->y), view.running);
            net_clicked_walk = true;
            net_clicked_ms = net_game->steady_now();
        } else if (const auto* run = std::get_if<cmd::Run>(&command)) {
            net_game->set_running(run->running);
        } else if (std::holds_alternative<cmd::SwapWeapons>(command)) {
            net_game->swap_weapons();
        } else if (const auto* merc_item = std::get_if<cmd::MercItem>(&command)) {
            net_game->merc_item(std::uint16_t(merc_item->slot));
        } else if (const auto* use = std::get_if<cmd::UseSkill>(&command)) {
            if (d2d::game::skill_refused(*scene, character, use->skill)) {   // game.exe's client checks first (FUN_004610c0): nothing sent
                world.fight.impossible(world_ms);
                return;
            }
            const auto* skill = scene->skills.get(use->skill);
            const bool melee = skill && !self_cast(*skill) && !world.fight.missile_skill(*skill) && !Fight::spot_skill(*skill) && !world.fight.summon_skill(*skill);
            const auto target = use->unit >= 0 ? net_monsters.find(std::uint32_t(use->unit)) : net_monsters.end();
            std::uint32_t item = 0xffffffff;                // a charged entry's item, as the host knows it
            if (use->item >= 0) {
                // No host id for it: nothing sent, or the host would cast the character's own
                // skill of that id, at its own level and for mana.
                const auto local = std::ranges::find(character.items, use->item, &d2d::d2s::Item::id);
                if (local == character.items.end()) return;
                item = net_game->host_item(*local);
                if (item == 0) return;
            }
            if (target != net_monsters.end()) {
                // The host drops a skill that comes mid-swing (FUN_0057edd0:
                // modes A1 / A2 / SC / TH take one only once FUN_0057ed70 says
                // the swing can end), so repeats are harmless; the same
                // target again once the swing here is over, as game.exe's
                // client, which runs its own copy of the mode.
                const bool again = use->unit == net_attack && use->skill == net_attack_skill;
                if (!again || hero.pmode < 0) net_game->skill_on(use->skill, use->left, 1, std::uint32_t(use->unit), item);
                net_attack = use->unit;
                net_attack_skill = use->skill;
                const auto& monster = target->second;
                if (melee) {
                    if (std::hypot(monster.unit.x - hero.player.x, monster.unit.y - hero.player.y) > 1.6f) net.send(cmd::Move{ monster.unit.x, monster.unit.y, use->left });
                    else world.display_swing(use->skill, monster.unit.x, monster.unit.y, world_ms);
                    return;
                }
                net.send(cmd::UseSkill{ use->skill, monster.unit.x, monster.unit.y, -1, use->left, use->item });   // the missile flies at it here, for show
                return;
            }
            net_attack = -1;
            net_game->skill_at(use->skill, use->left, subtile_x(use->x), subtile_y(use->y), item);
            // At the ground nothing is cast here, so the charge is spent here (the monster path's
            // local UseSkill spends it): the shown count drops and the picker drops the entry at 0.
            if (use->item >= 0)
                for (const auto& entry : d2d::game::charged_skills(hero.character.items))
                    if (entry.item == use->item && entry.skill == use->skill) { (void)d2d::game::use_charge(hero.character.items, entry); break; }
        } else if (const auto* pickup = std::get_if<cmd::Pickup>(&command)) {
            // A host item: the host takes it only within 5 subtiles and doesn't
            // walk us up for it (FUN_00548b00 case 4), so we walk there, it
            // with us (0x01), and ask when close (walk()); it lands in the bags
            // when the host says so.
            if (const auto found = net_game->ground.find(std::uint32_t(pickup->item)); found != net_game->ground.end()) {
                const float to_x = (float(found->second.x) + 0.5f) / 5.f - float(level->world_x), to_y = (float(found->second.y) + 0.5f) / 5.f - float(level->world_y);
                net.send(cmd::Move{ to_x, to_y, true });
                net_game->move_to(float(found->second.x), float(found->second.y), view.running);
                net_pick = int(found->first);
            }
            return;
        } else if (const auto* trade = std::get_if<cmd::OpenTrade>(&command); trade && trade->npc >= 0 && std::size_t(trade->npc) < level->npcs.size()) {
            // The host's NPC nearest d2d's (a vendor walks): talk, then trade (0x38).
            const auto& npc = level->npcs[std::size_t(trade->npc)];
            if (const auto* vendor = net_npc(trade->npc)) {
                // The host talks within 6 subtiles only: walk its player 3 from
                // the NPC on our side, then ask (walk()).
                const float from_x = net_game->host_x - vendor->x, from_y = net_game->host_y - vendor->y, apart = std::max(std::hypot(from_x, from_y), 1.f);
                net_game->move_to(vendor->x + from_x / apart * 3.f, vendor->y + from_y / apart * 3.f, view.running);
                net_game->store_items.clear();
                net_store_shown = 0;
                net_interact_pending = -1;                    // a newer NPC action wins
                net_trade_pending = int(vendor->id);
                net_trade_kind = trade->gamble ? 2 : 1; net_trade_extra = 0;
                net_trade_ms = frame_now;
            } else {
                net_game->log.note("no host NPC near " + npc.name + " to trade with");
            }
        } else if (const auto* caravan = std::get_if<cmd::Caravan>(&command); caravan && caravan->npc >= 0 && std::size_t(caravan->npc) < level->npcs.size()) {
            // Travel (Warriv, Meshif): the host's NPC as a trade's, then 0x38
            // kind 0 with the handler's level (0x4b5140 / 0x4b51a0 / 0x4b5260 /
            // 0x4b52c0); the host moves us (FUN_00579d60).
            namespace monster_ids = d2d::rules::monster_ids;
            const int hc_idx = level->npcs[std::size_t(caravan->npc)].hc_idx;
            if (const auto* host_npc = net_npc(caravan->npc)) {
                const float from_x = net_game->host_x - host_npc->x, from_y = net_game->host_y - host_npc->y, apart = std::max(std::hypot(from_x, from_y), 1.f);
                net_game->move_to(host_npc->x + from_x / apart * 3.f, host_npc->y + from_y / apart * 3.f, view.running);
                net_interact_pending = -1;                    // a newer NPC action wins
                net_trade_pending = int(host_npc->id);
                net_trade_kind = 0;
                net_trade_extra = hc_idx == monster_ids::kMeshif ? 0x4b : hc_idx == monster_ids::kWarrivAct2 ? 1 : 0x28;
                net_trade_ms = frame_now;
            } else {
                net_game->log.note("no host NPC near " + level->npcs[std::size_t(caravan->npc)].name + " to travel with");
            }
            return;                                         // the host moves us, not the local World
        } else if ((std::holds_alternative<cmd::Buy>(command) || std::holds_alternative<cmd::Sell>(command) || std::holds_alternative<cmd::Repair>(command))
                   && !net_game->trade_npc) {
            net_game->log.note("a trade with no host NPC: not made (d2d's own store would make it here only)");
            return;
        } else if (const auto* buy = std::get_if<cmd::Buy>(&command); buy && net_game->trade_npc) {
            const auto& tab = hero.store.tabs[std::size_t(std::clamp(buy->tab >= 0 ? buy->tab : store.tab, 0, 3))];
            if (buy->stock >= 0 && std::size_t(buy->stock) < tab.size()) {
                const auto& item = tab[std::size_t(buy->stock)];
                const int cost = d2d::rules::item_price(scene->rules, item, hero.store.npc_id, false, hero.store.header, store.reduced);
                net_game->buying = true;
                net_game->send_items({ d2d::net::d2gs::c2s::buy(net_game->trade_npc, std::uint32_t(item.id), 0, std::uint32_t(cost)) });
            }
            return;                                         // the host's item comes to us; its gold change too
        } else if (const auto* sell = std::get_if<cmd::Sell>(&command); sell && net_game->trade_npc) {
            auto found = std::ranges::find(hero.character.items, sell->item, &d2d::d2s::Item::id);
            const d2d::d2s::Item* item = found != hero.character.items.end() ? &*found : hero.held && hero.held->id == sell->item ? &*hero.held : nullptr;
            if (const std::uint32_t id = item ? net_game->host_item(*item) : 0) {
                const int cost = d2d::rules::item_price(scene->rules, *item, hero.store.npc_id, true, hero.store.header);
                net_game->send_items({ d2d::net::d2gs::c2s::sell(net_game->trade_npc, id, 0, std::uint32_t(cost)) });
                if (found != hero.character.items.end()) hero.character.items.erase(found);
                else hero.held.reset();
                net_cursor = 0;
            }
            return;                                         // gold from the host
        } else if (std::holds_alternative<cmd::CloseTrade>(command) && net_game->trade_npc) {
            net_game->send_items({ d2d::net::d2gs::c2s::npc_chat(false, 1, net_game->trade_npc) });
            if (net_chat_npc == net_game->trade_npc) net_chat_npc = 0;
            net_game->trade_npc = 0;
            net_game->store_items.clear();
        } else if (std::holds_alternative<cmd::UseItem>(command) || std::holds_alternative<cmd::UseBelt>(command) || std::holds_alternative<cmd::ToCursor>(command)
                   || std::holds_alternative<cmd::ToGrid>(command) || std::holds_alternative<cmd::ToBody>(command) || std::holds_alternative<cmd::ToBelt>(command)
                   || std::holds_alternative<cmd::Drop>(command) || std::holds_alternative<cmd::Socket>(command) || std::holds_alternative<cmd::ScrollToBook>(command)) {
            if (!net_items(command)) return;                  // the host can't make it: nor does the local World
        } else if (const auto* interact = std::get_if<cmd::Interact>(&command); interact && interact->npc >= 0 && std::size_t(interact->npc) < level->npcs.size()) {
            const auto& npc = level->npcs[std::size_t(interact->npc)];
            // The host walks us there only for a skill: d2d walks us up and
            // the host's player follows (walk()); it talks only within 6
            // subtiles (FUN_00548b00 case 1), so the trade's 0x13 comes again
            // once the menu's up (OpenTrade).
            if (npc.root == "objects") {
                if (const auto* object = net_game->nearest(2, -1, subtile_x(npc.x), subtile_y(npc.y), 4.f)) { net_operate_type = 2; net_operate = object->id; net_operate_ms = frame_now; net_operate_moves = false; net_operate_waypoint = -1; }
                else if (const auto* closest = net_game->nearest(2, -1, subtile_x(npc.x), subtile_y(npc.y), 40.f))
                    d2d::log::info("net: no host object within 4 of {} at ({:.0f}, {:.0f}); nearest is {} (row {}) at ({:.0f}, {:.0f})", npc.name, subtile_x(npc.x), subtile_y(npc.y), closest->id, closest->cls, closest->x, closest->y);
                else d2d::log::info("net: no host object near {} at ({:.0f}, {:.0f})", npc.name, subtile_x(npc.x), subtile_y(npc.y));
            } else if (const auto* host_npc = net_npc(interact->npc)) {
                // A talk as a trade's (kind -1): the host takes 0x13 only
                // within 6 and standing, then 0x2f on its NPC info heals
                // (live, 2026-10-18: an 0x13 while walking was dropped).
                net_interact_pending = -1;                    // a newer NPC action wins
                net_trade_pending = int(host_npc->id);
                net_trade_kind = -1; net_trade_extra = 0;
                net_trade_ms = frame_now;
                net_trade_asked = false;
            } else {                                         // the host hasn't sent it (0x0a'd, out of its sight): walk there, 0x13 once it shows
                net_game->move_to(subtile_x(npc.x), subtile_y(npc.y), view.running);
                net_interact_pending = interact->npc;
                net_game->log.note("no host unit for " + npc.name + " yet: walking there");
            }
        } else if (interact && interact->npc <= -3000 && std::size_t(-3000 - interact->npc) < hero.corpses.size()) {
            // Our corpse: the host's corpse unit (0x8e) nearest d2d's, its
            // items back as the host hands them (FUN_00548b00 case 0).
            const auto& corpse = hero.corpses[std::size_t(-3000 - interact->npc)];
            float best = 10.f;
            for (const std::uint32_t id : net_game->corpses)
                if (const auto unit = net_game->units.find(id); unit != net_game->units.end())
                    if (const float apart = std::hypot(unit->second.x - subtile_x(corpse.x), unit->second.y - subtile_y(corpse.y)); apart < best) {
                        best = apart; net_operate_type = 0; net_operate = id; net_operate_ms = frame_now; net_operate_moves = false; net_operate_waypoint = -1;
                    }
        } else if (interact && interact->npc <= -2000 && interact->npc > -2000 - int(d2d::game::World::kGamePortals)) {
            // A town portal: ours by the host's id for this end (0x82), else the
            // host's object nearest d2d's (they open apart: deviations.md) of
            // the slot's class (World::portal: 59 a town portal, 60 the quest
            // portals, 100 the true tomb's way down); operated once the host
            // has us there, as it warps us (0x15); d2d takes its own here.
            const int which = -2000 - interact->npc;
            static constexpr std::array<int, d2d::game::World::kGamePortals> kHostClass{ 59, 59, 60, 60, 60, 60, 100, 59, 60, 60, 60, 60 };
            if (which < 2 && net_game->portal_here != 0) {
                net_operate_type = 2; net_operate = net_game->portal_here; net_operate_ms = frame_now; net_operate_moves = true; net_operate_waypoint = -1;
            } else if (const auto& entry = world.portal[std::size_t(which)]; entry.level) {
                if (const auto* host_portal = net_game->nearest(2, kHostClass[std::size_t(which)], (entry.x + float(entry.level->world_x)) * 5.f, (entry.y + float(entry.level->world_y)) * 5.f, 10.f)) {
                    net_operate_type = 2; net_operate = host_portal->id; net_operate_ms = frame_now; net_operate_moves = true; net_operate_waypoint = -1;
                } else {
                    net_game->log.note(std::format("no host portal near ({:.0f}, {:.0f})", (entry.x + float(entry.level->world_x)) * 5.f, (entry.y + float(entry.level->world_y)) * 5.f));
                }
            }
        } else if (interact && interact->npc <= -4000 && std::size_t(-4000 - interact->npc) < view.others.size()) {
            if (net_game->trade.state != 0) return;            // one trade at a time
            // Another player: walk up, then 0x13 on them asks for a trade
            // (FUN_00548b00 case 0); their game.exe shows the request.
            const auto& other = view.others[std::size_t(-4000 - interact->npc)];
            net.send(cmd::Move{ other.unit.x, other.unit.y, true });
            net_operate_type = 0; net_operate = other.id; net_operate_ms = frame_now; net_operate_moves = false; net_operate_waypoint = -1;
            return;
        } else if (const auto* heard = std::get_if<cmd::QuestMessage>(&command); heard && heard->npc >= 0 && std::size_t(heard->npc) < level->npcs.size()) {
            // The host's quests hear it (0x31): its NPC's id, or for an object
            // (the Moldy Tome) the host's object there.
            const auto& npc = level->npcs[std::size_t(heard->npc)];
            const NetGame::Unit* unit = npc.root == "objects" ? net_game->nearest(2, -1, subtile_x(npc.x), subtile_y(npc.y), 4.f) : net_npc(heard->npc);
            if (unit) net_game->send_items({ d2d::net::d2gs::c2s::quest_message(unit->id, std::uint32_t(heard->string)) });
            else net_game->log.note(std::format("no host unit for {}'s quest message {}", npc.name, heard->string));
        } else if (std::holds_alternative<cmd::Resurrect>(command)) {
            net_game->send_items({ d2d::net::d2gs::c2s::resurrect() });   // the host respawns us in town (0x15)
        } else if (const auto* travel = std::get_if<cmd::Waypoint>(&command); travel && travel->npc >= 0 && std::size_t(travel->npc) < level->npcs.size()) {
            const auto& npc = level->npcs[std::size_t(travel->npc)];
            if (const auto* object = net_game->nearest(2, -1, subtile_x(npc.x), subtile_y(npc.y), 4.f)) {
                net_operate_type = 2; net_operate = object->id; net_operate_ms = frame_now; net_operate_moves = true;   // once the host has us there, stopped
                net_operate_waypoint = travel->level;
                net_warp_to = travel->level;                        // the host's reassign takes us there
                const auto [free_x, free_y] = level->nearest_free(npc.x + 0.5f, npc.y + 1.5f);   // beside it, as d2d's own walk-up
                net_warp_x = subtile_x(free_x); net_warp_y = subtile_y(free_y);
            } else {
                net_game->log.note(std::format("no host waypoint near ({:.0f}, {:.0f})", subtile_x(npc.x), subtile_y(npc.y)));
            }
        }
        net.send(command);
    }

// The host's NPC for d2d's NPC `index`: a unit of the same MonStats Id
// (Akara for Akara), the nearest if several (rogues); nullptr while the
// host hasn't sent it (out of range).
auto Town::net_npc(int index) const -> const NetGame::Unit* {
        if (!net_game || !level || index < 0 || std::size_t(index) >= level->npcs.size()) return nullptr;
        const auto& npc = level->npcs[std::size_t(index)];
        const float at_x = (npc.x + float(level->world_x)) * 5.f, at_y = (npc.y + float(level->world_y)) * 5.f;
        const NetGame::Unit* found = nullptr;
        float best = 1e9f;
        for (const auto& [unit_key, unit] : net_game->units) {
            if (unit.type != 1 || unit.cls < 0 || std::size_t(unit.cls) >= scene->monsters.types.size() || scene->monsters.types[std::size_t(unit.cls)].id != npc.id) continue;
            if (const float distance = std::hypot(unit.x - at_x, unit.y - at_y); distance < best) { best = distance; found = &unit; }
        }
        return found;
    }

// Our item moves and uses to the host, by its ids for d2d's items (the
// same code in the same place: NetGame::host_item); d2d still makes the
// move here. An item the host doesn't know is skipped and logged.
auto Town::net_items(const Command& command) -> bool {
        using namespace d2d::d2s;
        namespace c2s = d2d::net::d2gs::c2s;
        auto local = [&](int id) -> const Item* {
            const auto found = std::ranges::find(hero.character.items, id, &Item::id);
            return found == hero.character.items.end() ? nullptr : &*found;
        };
        auto host = [&](const Item* item) -> std::uint32_t {
            const std::uint32_t id = item ? net_game->host_item(*item) : 0;
            if (item && !id) net_game->log.note("no host id for our " + item->code + " (not sent)");
            return id;
        };
        const std::uint32_t held_id = hero.held ? (net_cursor ? net_cursor : net_game->host_item(*hero.held)) : 0;
        if (const auto* use = std::get_if<cmd::UseItem>(&command)) {
            const auto* item = local(use->item);
            if (const auto id = host(item); id && item->location == item_location::kBelt) net_game->send_items({ c2s::use_belt(id) });
            else if (id) net_game->send_items({ c2s::use_item(id, std::uint16_t((hero.player.x + float(level->world_x)) * 5.f), std::uint16_t((hero.player.y + float(level->world_y)) * 5.f)) });
        } else if (const auto* belt_use = std::get_if<cmd::UseBelt>(&command)) {
            const auto found = std::ranges::find_if(hero.character.items, [&](const Item& item) { return item.location == item_location::kBelt && item.column == belt_use->slot; });
            if (found != hero.character.items.end()) if (const auto id = host(&*found)) net_game->send_items({ c2s::use_belt(id) });
        } else if (const auto* to_cursor = std::get_if<cmd::ToCursor>(&command)) {
            const auto* item = local(to_cursor->item);
            if (const auto id = host(item)) {
                if (item->location == item_location::kEquipped) net_game->send_items({ c2s::body_to_cursor(std::uint16_t(item->slot)) });
                else net_game->send_items({ c2s::item_id_packet(item->location == item_location::kBelt ? 0x24 : 0x19, id) });
                net_cursor = id;
            }
        } else if (const auto* to_grid = std::get_if<cmd::ToGrid>(&command); to_grid && held_id) {
            // What's under the held item's footprint there: a swap (0x1f).
            const auto info = scene->rules.item_info.find(hero.held->code);
            const int width = info != scene->rules.item_info.end() ? info->second.width : 1, height = info != scene->rules.item_info.end() ? info->second.height : 1;
            const Item* under = nullptr;
            for (const auto& item : hero.character.items) {
                if (item.location != item_location::kStored || item.panel != to_grid->panel) continue;
                const auto item_size = scene->rules.item_info.find(item.code);
                const int item_w = item_size != scene->rules.item_info.end() ? item_size->second.width : 1, item_h = item_size != scene->rules.item_info.end() ? item_size->second.height : 1;
                if (item.column < to_grid->col + width && to_grid->col < item.column + item_w && item.row < to_grid->row + height && to_grid->row < item.row + item_h) { under = &item; break; }
            }
            const std::uint32_t buffer = to_grid->panel == item_panel::kStash ? 4 : to_grid->panel == item_panel::kCube ? 3 : 0;
            if (under) { if (const auto target = host(under)) net_game->send_items({ c2s::swap_grid(held_id, target, std::uint32_t(to_grid->col), std::uint32_t(to_grid->row)) }); net_cursor = net_game->host_item(*under); }
            else { net_game->send_items({ c2s::cursor_to_grid(held_id, std::uint32_t(to_grid->col), std::uint32_t(to_grid->row), buffer) }); net_cursor = 0; }
        } else if (const auto* to_body = std::get_if<cmd::ToBody>(&command); to_body && held_id) {
            const auto worn = std::ranges::find_if(hero.character.items, [&](const Item& item) { return item.location == item_location::kEquipped && item.slot == to_body->slot; });
            const bool swap = worn != hero.character.items.end();
            net_game->send_items({ c2s::equip(held_id, std::uint32_t(to_body->slot), swap) });
            net_cursor = swap ? net_game->host_item(*worn) : 0;
        } else if (const auto* to_belt = std::get_if<cmd::ToBelt>(&command); to_belt && held_id) {
            net_game->send_items({ c2s::cursor_to_belt(held_id, std::uint32_t(to_belt->box)) });
            net_cursor = 0;
        } else if (const auto* to_book = std::get_if<cmd::ScrollToBook>(&command); to_book && held_id) {
            const auto target = host(local(to_book->book));
            if (!target) return false;
            net_game->send_items({ c2s::scroll_to_book(held_id, target) });
            net_cursor = 0;
        } else if (const auto* socket = std::get_if<cmd::Socket>(&command); socket && held_id) {
            const auto target = host(local(socket->item));
            if (!target) return false;
            net_game->send_items({ c2s::insert_socket(held_id, target) });
            net_cursor = 0;
        } else if (std::holds_alternative<cmd::Drop>(command) && held_id) {
            net_game->send_items({ c2s::item_id_packet(0x17, held_id) });
            net_cursor = 0;
        }
        return true;
    }

// A joined game: the host's monsters stand in for the World's (it makes
// none: Fight::remote_monsters) and its other players are drawn; act
// subtiles to the level's cells (a subtile's centre: d2d's x.5 is a cell's). The host's NPCs are left out: d2d's own
// walk the camp (ponytail: matched by class and position later, M6).
auto Town::net_overlay() -> void {
        if (!level || !scene) return;
        auto cell_x = [&](float subtile_x) { return (subtile_x + 0.5f) / 5.f - float(level->world_x); };
        auto cell_y = [&](float subtile_y) { return (subtile_y + 0.5f) / 5.f - float(level->world_y); };
        view.monsters.clear();
        view.others.clear();
        auto host_states = [&](const std::vector<int>& rows) {
            std::vector<std::string_view> names;
            for (const int row : rows) if (std::size_t(row) < scene->state_names.size()) names.emplace_back(scene->state_names[std::size_t(row)]);
            return names;
        };
        // A host monster's (or our merc's) unit command as a mode
        // (DAT_006da4d8, net-packets.md "The unit command"): dying plays DT
        // once, then lies DD; an act plays its mode, then stands or walks again.
        // ponytail: an act lasts 600 ms, not its animation's length.
        auto host_mode = [&](const NetGame::Unit& unit) -> std::string_view {
            static constexpr std::array<std::string_view, 0x1e> kCommandMode = {
                "WL", "WL", "", "", "SC", "SC", "GH", "NU", "DT", "DD", "A1", "A1", "S1", "S1", "S2", "S2",
                "A2", "A2", "BL", "", "KB", "SQ", "SQ", "RN", "RN", "S1", "S3", "S3", "S4", "S4" };
            const auto since = net_game->steady_now() - unit.mode_ms;
            const std::string_view commanded = unit.mode >= 0 && std::size_t(unit.mode) < kCommandMode.size() ? kCommandMode[std::size_t(unit.mode)] : "";
            if (unit.mode == 8 || unit.mode == 9) return unit.mode == 8 && since < 1500 ? "DT" : "DD";
            if (unit.life == 0) return "DD";
            if (!commanded.empty() && commanded != "WL" && commanded != "NU" && since < 600) return commanded;
            return unit.moving ? "WL" : "NU";
        };
        for (const auto& [unit_key, unit] : net_game->units) {
            if (unit.type == 0) {
                if (unit.id == net_game->self_id) continue;
                View::OtherPlayer other{ .cls = unit.cls, .name = unit.name, .id = unit.id };
                std::vector<d2d::d2s::Item> worn;
                for (const auto& entry : unit.worn) worn.push_back(entry.second);
                other.gfx = scene->look_of(worn);
                other.unit.x = cell_x(unit.x);
                other.unit.y = cell_y(unit.y);
                other.unit.walking = unit.moving;
                other.running = unit.moving && unit.running;
                if (unit.moving) other.unit.dir = direction16(unit.goal_x - unit.x, unit.goal_y - unit.y);
                // A skill (0x4c / 0x4d) plays its Skills.txt anim once, facing
                // where it went (A1 / TH an attack, else a cast, as ours).
                auto& shown = net_other_modes[unit.id];
                if (const auto* skill_row = unit.skill >= 0 ? scene->skills.get(unit.skill) : nullptr) {
                    const int mode = skill_row->anim == "A1" || skill_row->anim == "TH" ? kModeA1 : kModeSC;
                    if (net_game->steady_now() - unit.skill_ms < scene->composite(unit.cls, mode, other.gfx).length_ms()) {
                        other.mode = mode;
                        other.unit.dir = direction16(unit.skill_x - unit.x, unit.skill_y - unit.y);
                    }
                }
                if (other.mode != shown.mode || (other.mode >= 0 && unit.skill_ms != shown.skill_ms)) shown.since = world_ms;   // the animation starts over
                shown.mode = other.mode;
                shown.skill_ms = unit.skill_ms;
                other.unit.mode_ms = shown.since;
                other.states = host_states(unit.states);
                // Its walk's % from its class's run / walk and the host's chill (state 11);
                // ponytail: its FRW isn't sent, so a faster run / walk's legs don't show.
                other.walk_percent = World::walk_percent(*world.game_data, unit.cls, other.running, 0, std::ranges::find(other.states, std::string_view("cold")) != other.states.end());
                view.others.push_back(std::move(other));
                continue;
            }
            if (unit.type == 1 && unit.id == net_game->merc_id && unit.id != 0) {   // ours: the World's merc, drawn where the host has it
                if (view.merc) {
                    if (unit.moving) view.merc->unit.dir = direction16(unit.goal_x - unit.x, unit.goal_y - unit.y);
                    view.merc->unit.x = cell_x(unit.x);
                    view.merc->unit.y = cell_y(unit.y);
                    view.merc->unit.walking = unit.moving;
                    if (const auto mode = host_mode(unit); mode != view.merc->mode) { view.merc->mode = mode; view.merc->unit.mode_ms = world_ms; }
                }
                continue;
            }
            if (unit.type != 1 || unit.cls < 0 || std::size_t(unit.cls) >= scene->monsters.types.size()) continue;   // objects and warps: d2d's own
            if (std::size_t(unit.cls) < scene->mon_is_npc.size() && scene->mon_is_npc[std::size_t(unit.cls)]) continue;
            auto found = net_monsters.find(unit.id);
            if (found == net_monsters.end()) {
                found = net_monsters.emplace(unit.id, make_monster(*scene, unit.cls, cell_x(unit.x), cell_y(unit.y), rng, net_game->difficulty)).first;
                // The host's boss as its 0xac says: name, colour, mods. An act
                // boss (Andariel, Blood Raven) comes as unique with name seed 0:
                // a boss still, under its MonStats name.
                // ponytail: a rolled unique with seed 0 keeps its plain name too (1 in 65536).
                using d2d::rules::Boss;
                const Boss kind = unit.boss_flags & 4 ? Boss::superunique : unit.boss_flags & 2 ? Boss::unique : unit.boss_flags & 1 ? Boss::champion : unit.boss_flags & 8 ? Boss::minion : Boss::none;
                if (kind != Boss::none) {
                    const auto plain_name = found->second.npc.name;
                    make_boss(*scene, found->second, kind, unit.mods, unit.super, unit.name_seed, net_game->difficulty, rng);
                    if (kind == Boss::unique && unit.name_seed == 0) found->second.npc.name = plain_name;
                }
            }
            auto& monster = found->second;
            monster.id = int(unit.id);
            if (unit.moving) monster.unit.dir = direction16(unit.goal_x - unit.x, unit.goal_y - unit.y);
            monster.unit.x = cell_x(unit.x);
            monster.unit.y = cell_y(unit.y);
            monster.unit.walking = unit.moving;
            monster.mode = host_mode(unit);
            if (monster.mode != monster.last_mode) monster.unit.mode_ms = world_ms;   // the animation starts over
            monster.hit_points = unit.life == 0 ? 0 : std::max(1, monster.stats.hit_points * unit.life / 128);
            monster.host_states = host_states(unit.states);
            monster.align = std::ranges::contains(monster.host_states, std::string_view("conversion")) ? 2 : 0;   // the host converted it: no target, as Fight::monster_states
            // The host sends no MonSounds: game.exe's client plays them itself, from the unit's
            // mode (FUN_004cc410, FUN_004cb890), its first showing (FUN_004cc380), Neutral
            // (FUN_004cb460) and footsteps (FUN_004caf60). The same path as single player's,
            // which moves last_mode on.
            world.fight.monster_sounds(monster, world_ms);
            view.monsters.push_back(monster);
        }
        // A unit the host has removed is gone here too: one that comes back is
        // a new unit, its Init again, as game.exe frees and makes it anew.
        std::erase_if(net_monsters, [&](const auto& entry) { return !net_game->units.contains(std::uint64_t(1) << 32 | entry.first); });
        std::erase_if(net_other_modes, [&](const auto& entry) { return !net_game->units.contains(entry.first); });
        std::ranges::move(world.cues.due, std::back_inserter(cues.due));
        world.cues.due.clear();
        // The host's ground items, named as d2d names its own (labels kept by id).
        view.ground.clear();
        for (const auto& [id, lying] : net_game->ground) {
            auto& shown = net_ground[id];
            if (shown.id < 0) {
                shown.id = int(id);
                shown.item = lying.item;
                shown.gold = lying.gold;
                shown.now_ms = world_ms;
                if (lying.item.code == "gld") shown.label = std::to_string(lying.gold) + " Gold";
                else if (const auto lines = item_lines(*scene, lying.item, int(character.stats.get(d2d::d2s::kLevel))); !lines.empty()) {
                    shown.label = lines[0].text;
                    shown.rgb = lines[0].rgb;
                }
            }
            shown.x = cell_x(float(lying.x));
            shown.y = cell_y(float(lying.y));
            view.ground.push_back(shown);
        }
        std::erase_if(net_ground, [&](const auto& entry) { return !net_game->ground.contains(entry.first); });
        // The host monster being attacked: held, the attack goes on (input); not once it's converted.
        const int attacked = net_attack >= 0 ? view.monster(net_attack) : -1;
        if (attacked >= 0 && view.monsters[std::size_t(attacked)].alive() && !view.monsters[std::size_t(attacked)].ally_not_interact(*scene)) {
            view.attack = net_attack;
            view.attack_skill = net_attack_skill;
        } else {
            net_attack = -1;
            view.attack = -1;
        }
    }

// The character's key config (FUN_0046aae0): its <name>.key when that's
// whole, else the save folder's default.key, else the built-in table;
// then kept as <name>.key.
auto Town::load_keys() -> void {
        const auto read = [](const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary);
            return std::vector<std::uint8_t>{ std::istreambuf_iterator<char>(file), {} };
        };
        key_table = keys::defaults();
        if (!key_dir.empty()) {
            if (const auto own = keys::read_character(read(key_dir / (character.name + ".key")))) { key_table = *own; }
            else {
                if (const auto shared = keys::read_default(read(key_dir / "default.key")); shared && keys::valid(*shared)) key_table = *shared;
                save_keys(false);
            }
        }
        update_hotkey_names();
    }

// <name>.key, and with `shared` the save folder's default.key (Accept,
// FUN_00469780).
auto Town::save_keys(bool shared) const -> void {
        if (key_dir.empty()) return;
        const auto write = [](const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!file) d2d::log::warn("{}: not written", path.string());
        };
        write(key_dir / (character.name + ".key"), keys::write_character(key_table));
        if (shared) write(key_dir / "default.key", keys::write_default(key_table));
    }

auto Town::update_hotkey_names() -> void {
        for (int slot = 0; slot < 16; ++slot) {
            const int action = slot < 8 ? keys::kSkill1 + slot : keys::kSkill9 + slot - 8;
            auto key = keys::key_of(key_table, action, 1);
            if (key == keys::kNone) key = keys::key_of(key_table, action, 0);
            const auto id = keys::name_id(key);
            skillbar.hotkey_names[std::size_t(slot)] = key == keys::kNone ? std::string{} : id ? string_id(*scene, id) : std::string(1, char(key & 0xff));
        }
    }

// Accept (0x4a5020): the keys kept, the pad's settings to d2d.cfg, back to the game.
auto Town::accept_controls() -> void {
        key_table = controls.table;
        save_keys(true);
        update_hotkey_names();
        if (!(controls.pad == pad_config)) {
            set_pad_config(controls.pad);
            pad_config_changed = true;
            if (!cfg_file.empty() && !d2d::userdir::save_cfg(cfg_file, pad::save_config(pad_config))) d2d::log::warn("d2d.cfg: the pad's settings not saved");
        }
    }

auto Town::function_held(int action) const -> bool {
        return key_held(keys::key_of(key_table, action, 0)) || key_held(keys::key_of(key_table, action, 1));
    }

// Say (0x468f80..0x468ff0): phrase 0x19 + n to the server (C→S 0x3f),
// which sets it on the player; every client then plays the class's
// voice (FUN_004cb9c0: its sound table's +0x20, - 0x32 + phrase * 2: one
// of the phrase's two takes). English shows no text with it (FUN_004a0200
// only does for languages 6, 7 and 9).
auto Town::say_sound(int cls, int phrase) -> int {
        static constexpr std::array<int, 7> kChatSound = { 0xb79, 0xcb1, 0xc15, 0xc63, 0xbc7, 0xd4d, 0xcff };   // amazon_chat_a_1 ...
        return kChatSound[std::size_t(std::clamp(cls, 0, 6))] + 2 * std::clamp(phrase, 0, 7);
    }

auto Town::say(int phrase) -> void {
        audio_say = say_sound(int(character.header.cls), phrase);
        if (net_game) net_game->phrase(std::uint16_t(0x19 + phrase));
    }

// Clear Screen (FUN_0044c6b0): every closable panel shuts, and the automap.
auto Town::clear_screen() -> void {
        inv_open = char_open = stash_open = cube_open = tree_open = quest_log.open = belt_open = hireling.open = party.open = false;
        if (help.open) toggle_help();
        automap.open = false;
        skillbar.picking = 0;
    }

// The Help screen (0x4689d0: UI 0x21): it brings the mini-panel up while
// it's open (FUN_004966c0, 0x7bef00) and puts it away after (FUN_00494fc0).
// The Party screen's roster (FUN_0049a640): the other players of the
// joined game, by unit id.
// ponytail: game.exe's roster keeps the order they joined in.
auto Town::party_rows() const -> std::vector<PartyPanel::Row> {
        std::vector<PartyPanel::Row> rows;
        const auto area_name = [&](int area) -> std::string {
            if (!scene || area <= 0) return {};
            const auto found = scene->level_names.find(area);
            if (found == scene->level_names.end()) return {};
            const auto text = lookup_string(*scene, std::string_view(found->second));
            return text ? u16_to_latin1(*text) : found->second;
        };
        if (coop_hud_hidden()) {                      // local co-op: everyone else on the screen, all in its one party
            for (const auto& member : world.players)
                if (member->id != focus_id())
                    rows.push_back({ std::uint32_t(member->id), member->character.name, int(member->character.header.cls), int(member->character.stats.get(d2d::d2s::kLevel)),
                                     1, 1, 0, area_name(level ? level->id : 0), false });
            return rows;
        }
        if (!net_game) return party_preview;
        for (const auto& [unit_key, unit] : net_game->units) {
            if (unit.type != 0 || unit.id == net_game->self_id || unit.name.empty()) continue;
            PartyPanel::Row row{ unit.id, unit.name, unit.cls, unit.level, unit.party, unit.relation, unit.flags, {}, (unit.flags & 8) != 0 };
            row.area = area_name(unit.area);
            rows.push_back(std::move(row));
        }
        std::ranges::sort(rows, {}, &PartyPanel::Row::id);
        return rows;
    }

auto Town::party_own() const -> PartyPanel::Own {
        PartyPanel::Own own{ character.name, int(character.stats.get(d2d::d2s::kLevel)), party_preview.empty() ? 0xffff : 1, level && d2d::rules::level_ids::is_town(level->id) };
        if (coop_hud_hidden()) { own.party = 1; own.fixed = true; }   // the screen's player (`character`), in local co-op's one party
        if (net_game)
            if (const auto self = net_game->units.find(std::uint64_t(net_game->self_id)); self != net_game->units.end()) own.party = self->second.party;
        return own;
    }

auto Town::toggle_help() -> void {
        help.open = !help.open;
        if (help.open && !mini.open) { mini.open = true; help_opened_mini = true; }
        if (!help.open && help_opened_mini) { mini.open = false; help_opened_mini = false; }
    }

auto Town::pad_field_or_free() const -> bool {
        return have_world && !game_menu.open && !controls.open && npc_menu.npc < 0 && store.npc < 0 && !inv_open && !stash_open && !cube_open;
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

auto Town::new_game() -> void {
        world.new_game();
        skillbar.new_game();
        hand_items.clear();
    }

auto Town::open_game_menu() -> void {
        if (help.open) toggle_help();                 // first: it may have brought the mini-panel up
        game_menu.restore_automap = automap.open;
        game_menu.restore_mini = mini.open;
        automap.open = mini.open = belt_open = false;
        inv_open = char_open = stash_open = cube_open = tree_open = quest_log.open = hireling.open = party.open = false;
        game_menu.expansion = character.expansion;
        game_menu.open = true;
        game_menu.show(0);
    }

auto Town::close_game_menu() -> void {
        game_menu.open = false;
        automap.open = game_menu.restore_automap;
        mini.open = game_menu.restore_mini;
    }

auto Town::update(std::vector<std::uint8_t>& framebuffer, const Mouse& frame_mouse, const std::vector<SDL_Keycode>& frame_keys,
                Screen& screen, Audio& audio, std::uint32_t frame_ms, std::uint32_t last_ms) -> void {
        now_ms = frame_ms;
        skillbar.coop = world.players.size() > 1;           // Resurrect on the right-hand picker
        if (skillbar.right == kResurrectSkill && !skillbar.usable(kResurrectSkill, false)) skillbar.right = 0;   // co-op over: Attack
        skillbar.drop_gone();                                       // a charged entry used up or taken off
        // A partner's screen with nothing of theirs open goes back to the first player.
        if (focus_id() != hero.id && focus_keys.empty() && pad_field() && !game_menu.open && !automap.open && !waypoint.open) take_focus(hero.id);
        auto keys_this_frame = frame_keys;               // and a partner's that took the screen
        keys_this_frame.insert(keys_this_frame.end(), focus_keys.begin(), focus_keys.end());
        focus_keys.clear();
        Mouse mouse = frame_mouse;             // the menu / mini-panel take their clicks from the rest
        // The gold box takes the keys and clicks while it's up: digits and
        // Backspace edit the amount (at most the gold it's from), the spinner
        // steps it, OK or Enter does the box's mode (0x454080), Cancel closes.
        if (gold_box && !game_menu.open) {
            using namespace d2d::d2s;
            const std::int64_t most = std::max<std::int64_t>(character.stats.get(gold_box->mode == 4 ? kGoldBank : kGold), 0);
            const auto amount = [&] { return std::min<std::int64_t>(gold_box->amount.empty() ? 0 : std::stoll(gold_box->amount), most); };
            for (const char digit : typed) if (digit >= '0' && digit <= '9' && gold_box->amount.size() < 10) gold_box->amount.push_back(digit);
            bool ok = false, cancel = false;
            for (const auto key : keys_this_frame) {
                if (key == SDLK_BACKSPACE && !gold_box->amount.empty()) gold_box->amount.pop_back();
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER) ok = true;
            }
            if (!gold_box->amount.empty()) gold_box->amount = std::to_string(amount());
            const auto part = gold_box_at(mouse.x, mouse.y);
            if (mouse.press_this_frame) {
                gold_box->spin = part == GoldBoxPart::kUp ? 1 : part == GoldBoxPart::kDown ? -1 : 0;
                gold_box->spin_ms = frame_ms;
                if (gold_box->spin != 0) gold_box->amount = std::to_string(std::clamp<std::int64_t>(amount() + gold_box->spin, 0, most));   // a click: one step
                gold_box->next_ms = frame_ms;
                gold_box->ok_down = part == GoldBoxPart::kOk;
                gold_box->cancel_down = part == GoldBoxPart::kCancel;
            }
            if (!mouse.down) gold_box->spin = 0;
            // Held (FUN_004bbfb0 through the box's 0x453fe0): a step at most every
            // 70 ms, 1 for the first second, then the draws held >> 5, >> 4, >> 2
            // past 1, 2, 3 s, and (ms held >> 11) x the draws past 4096 ms.
            // ponytail: game.exe counts its own draws (+0x40); here 25 a second, its in-game rate.
            if (gold_box->spin != 0 && frame_ms - gold_box->next_ms >= 70) {
                const std::uint32_t held_ms = frame_ms - gold_box->spin_ms;
                const std::int64_t draws = held_ms / 40;
                const std::int64_t step = held_ms > 0x1000 ? std::int64_t(held_ms >> 11) * draws : held_ms > 3000 ? draws >> 2 : held_ms > 2000 ? draws >> 4 : held_ms > 1000 ? draws >> 5 : 1;
                gold_box->amount = std::to_string(std::clamp<std::int64_t>(amount() + gold_box->spin * step, 0, most));
                gold_box->next_ms = frame_ms;
            }
            if (mouse.release_this_frame) {
                ok = ok || (gold_box->ok_down && part == GoldBoxPart::kOk);
                cancel = gold_box->cancel_down && part == GoldBoxPart::kCancel;
                gold_box->ok_down = gold_box->cancel_down = false;
            }
            if (ok) {
                const std::int64_t gold = amount();
                const int mode = gold_box->mode;
                if (mode == 2) { if (net_game) net_game->trade_gold(std::uint32_t(gold)); }
                else if (gold > 0 && mode >= 3) {
                    if (net_game) net_game->send_items({ d2d::net::d2gs::c2s::button(mode == 3 ? 0x14 : 0x13, std::uint16_t(gold >> 16), std::uint16_t(gold)) });
                    else send(cmd::StashGold{ int(gold), mode == 3 });
                } else if (gold > 0) {
                    if (net_game) net_game->send_items({ d2d::net::d2gs::c2s::drop_gold(net_game->self_id, std::uint32_t(gold)) });
                    else send(cmd::DropGold{ int(gold) });
                }
            }
            if (ok || cancel) gold_box.reset();
            mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
        }
        // The Party screen's clicks (FUN_0049bb60 / FUN_0049bdd0).
        if (party.open && (net_game || coop_hud_hidden()) && !help.open && !controls.open && !game_menu.open) {
            const auto click = party.input(mouse, party_rows(), party_own());
            if (net_game && click.kind == PartyPanel::Click::kButton) net_game->party_button(click.player);
            if (net_game && click.kind == PartyPanel::Click::kFlag) net_game->relation(click.player, click.flag);
            if (net_game && click.kind == PartyPanel::Click::kLeave) net_game->leave_party();
            if (mouse.x >= 80 && mouse.x < 400 && mouse.y >= 60 && mouse.y < 492) mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
        }
        if (party.open && !net_game && !coop_hud_hidden() && party_preview.empty()) party.open = false;
        // The merc's portrait (FUN_00493760 / FUN_004936e0 / FUN_004938e0): 700 ms
        // on it (x .. x + 0x2e, y 0x18 .. 0x3c) with nothing in hand lights it
        // and shows its hint; a right-click opens the Hireling screen; a
        // potion let go on it goes to the merc (0x61 slot 0).
        {
            const bool over = merc_portrait_x >= 0 && mouse.x >= merc_portrait_x && mouse.x <= merc_portrait_x + 0x2e && mouse.y >= 0x18 && mouse.y <= 0x3c;
            if (!over) merc_hover_at = 0;
            else if (merc_hover_at == 0) merc_hover_at = std::max<std::uint32_t>(frame_ms, 1);
            merc_hovered = over && !held && frame_ms - merc_hover_at >= 700;
            if (over && mouse.rpress_this_frame && character.expansion && view.merc_sheet) {
                hireling.open = true;
                char_open = stash_open = cube_open = quest_log.open = false;
                mouse.rpress_this_frame = false;
            }
            if (over && mouse.press_this_frame && held) { send(cmd::MercItem{ 0 }); mouse.press_this_frame = mouse.release_this_frame = false; }
        }
        // The Hireling screen's clicks: its close button, an item on or off the merc (0x61).
        if (hireling.open && !help.open && !controls.open && !game_menu.open) {
            if (!view.merc_sheet) hireling.open = false;
            else if (const int asked = hireling.input(*scene, mouse, held.has_value()); asked >= 0) {
                send(cmd::MercItem{ asked });
                mouse.press_this_frame = mouse.release_this_frame = false;
            }
            if (mouse.x >= 80 && mouse.x < 400 && mouse.y >= 60 && mouse.y < 492) mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
        }
        // The Help screen's close button; its other clicks go nowhere (FUN_00494f30).
        if (help.open && !controls.open && !game_menu.open) {
            if (help.input(mouse)) { help.open = true; toggle_help(); }
            mouse.down = mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
        }
        // Configure Controls (UI 0xb) takes every key and click while it's up.
        if (controls.open) {
            std::vector<std::uint16_t> virtual_keys;
            for (const auto key : keys_this_frame) if (const auto code = virtual_key(key)) virtual_keys.push_back(code);
            const auto result = controls.input(*scene, virtual_keys, mouse, std::exchange(controls_pad_press, pad::Button::Count), frame_ms);
            if (result == ControlsUI::kAccept) accept_controls();
            if (result == ControlsUI::kCancel) open_game_menu();          // FUN_0047e090(0, 1): back to the menu
            keys_this_frame.clear();
            mouse.down = mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
            mouse.wheel = 0;
        }
        // ESC handled globally in handle_sdl_events (returns to Title).
        // D2 movement: press or hold the left button on the ground
        // and the character walks toward that point (the target
        // tracks the cursor while held); the camera follows.
        // Chat: Enter opens a line, typing and Backspace edit it, Enter sends
        // it (0x15) and Esc drops it. The host's 0x26 comes back to everyone,
        // us too, as "name: message"; without a host (single player, local
        // co-op) game.exe's own server echoes it so, d2d's chat shows it.
        // "@name message" whispers (FUN_0047bad0: a name under 0x32, then
        // the message; else the whole line goes to all).
        if (!gold_box && !game_menu.open) {
            if (chat_typing) chat_typing->append(typed.substr(0, 255 - std::min<std::size_t>(chat_typing->size(), 255)));
            for (const auto key : keys_this_frame) {
                if (chat_typing && key == SDLK_BACKSPACE && !chat_typing->empty()) chat_typing->pop_back();
                if (!chat_typing) { if (keys::bound(key_table, keys::kChat, virtual_key(key))) chat_typing = std::string{}; continue; }   // the key config's Chat (Enter)
                if (key != SDLK_RETURN && key != SDLK_KP_ENTER) continue;
                if (const std::string_view line = *chat_typing; !line.empty()) {
                    std::string_view target, message = line;
                    if (line.starts_with('@') && net_game)              // '@' needs a multiplayer game (its entry's flag)
                        if (const auto space = line.find(' '); space != std::string_view::npos && space > 1 && space - 1 < 0x32 && space + 1 < line.size()) {
                            target = line.substr(1, space - 1);
                            message = line.substr(space + 1);
                        }
                    if (net_game) net_game->say(message, target);
                    else add_chat(character.name + string_id(*scene, 0xfd0) + std::string(message), 0, character.name.size());
                }
                chat_typing.reset();
            }
        }
        if (net_game) {
            // FUN_0049f490: to all, the name gold (colour code 4) then
            // SysmsgPlayer1 ": " and the message in the packet's colour;
            // a whisper, "name whispers: message" all green (2).
            for (auto& line : std::exchange(net_game->chat, {})) {
                if (line.type == 2) add_chat(line.name + string_id(*scene, 0xe46) + line.message, 2);
                else add_chat(line.name + string_id(*scene, 0xfd0) + line.message, line.colour, line.name.size());
            }
            for (const auto& event : std::exchange(net_game->events, {}))
                if (auto text = event_text(event); !text.empty()) add_chat(text, event.colour);
        }
        std::erase_if(chat_lines, [&](const auto& line) { return frame_ms >= line.until; });
        for (const auto key : keys_this_frame) {
            if ((gold_box || chat_typing || coop_typing()) && key != SDLK_ESCAPE) continue;   // typing (a co-op roster's name too): no hotkeys
            if (game_menu.open && key != SDLK_ESCAPE) continue;   // the menu's input table (FUN_00467a70) has the keys
            // The key config's functions (docs/research/re/controls.md); Esc stays Esc.
            const auto fired = keys::actions_of(key_table, virtual_key(key));
            const auto fires = [&](int action) { return std::ranges::contains(fired, action); };
            if (fires(keys::kInventory)) { inv_open = !inv_open; if (inv_open) tree_open = false; }
            if (fires(keys::kSkillTree)) { tree_open = !tree_open; if (tree_open) inv_open = false; }   // both right-hand panels
            if (fires(keys::kToggleRun)) send(cmd::Run{ !view.running });
            if (fires(keys::kSwapWeapons) && character.expansion) send(cmd::SwapWeapons{});
            for (int slot = 0; slot < 16; ++slot)
                if (fires(slot < 8 ? keys::kSkill1 + slot : keys::kSkill9 + slot - 8)) skillbar.hotkey(slot, mouse.x, mouse.y);
            if (fires(keys::kShowBelt)) belt_open = !belt_open;
            if (fires(keys::kAutomap)) automap.open = !automap.open;
            if (fires(keys::kCharacter)) { char_open = !char_open; if (char_open) stash_open = cube_open = quest_log.open = hireling.open = false; }
            if (fires(keys::kQuestLog)) toggle_quest_log();
            if (key == SDLK_ESCAPE && view.dead && (!view.ghost || view.all_dead)) { send(cmd::Resurrect{}); continue; }
            for (int belt = 0; belt < 4; ++belt)
                if (fires(keys::kBelt1 + belt)) net.send(cmd::UseBelt{ belt, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 });
            for (int phrase = 0; phrase < 8; ++phrase)
                if (fires(phrase < 7 ? keys::kSay1 + phrase : int(keys::kSayRetreat))) say(phrase);
            if (fires(keys::kRun) && !view.running) { send(cmd::Run{ true }); run_held = true; }   // 0x469000: run while held
            if (fires(keys::kClearScreen)) clear_screen();
            if (fires(keys::kClearMessages)) chat_lines.clear();   // FUN_004a01e0: every line's time out
            if (fires(keys::kPreviousSkill)) skillbar.cycle(-1);
            if (fires(keys::kNextSkill)) skillbar.cycle(1);
            if (fires(keys::kScreenShot)) screenshot_wanted = true;
            if (fires(keys::kShowPortraits)) portraits_hidden = !portraits_hidden;   // 0x493840
            if (fires(keys::kSkillSpeedBar)) skillbar.picking = skillbar.picking == 2 ? 0 : 2;   // 0x468b00: UI 3, the right hand's picker (0x7c07fc = 0)
            if (fires(keys::kMessageLog)) message_log = !message_log;
            if (fires(keys::kHelp)) toggle_help();
            // 0x468960: UI 0x16, in a multiplayer game only (FUN_0044db30), local co-op's too; a left-hand panel.
            if (fires(keys::kParty) && (net_game || coop_hud_hidden())) {
                party.open = !party.open;
                if (party.open) char_open = stash_open = cube_open = quest_log.open = hireling.open = false;
            }
            // 0x469170: UI 0x24, an expansion game with a merc; a left-hand panel.
            if (fires(keys::kHireling) && character.expansion && view.merc_sheet) {
                hireling.open = !hireling.open;
                if (hireling.open) char_open = stash_open = cube_open = quest_log.open = false;
            }   // 0x468980: UI 0x18, which 1.14d never draws
            // The automap's (0x468a60..0x468ad0), the same values as Options -> Automap.
            auto& automap_values = game_menu.value[4];
            if (fires(keys::kFadeAutomap)) automap_values[2] = (automap_values[2] + 1) % 4;   // 0x468a70: the next fade
            if (fires(keys::kPartyOnAutomap)) automap_values[4] = !automap_values[4];
            if (fires(keys::kNamesOnAutomap)) automap_values[5] = !automap_values[5];
            if (fires(keys::kToggleMiniMap)) automap.left = !automap.left;          // "AutoMap Left": the mini map's corner
            // Center Automap (0x468a60) puts the mini map's box back (FUN_00457520); d2d's never moves.
            if (key == SDLK_ESCAPE && skillbar.picking) { skillbar.picking = 0; continue; }   // the picker first
            // Esc (0x4690b0): the NPC's windows first, then the game menu
            // closes, else every Esc-closable panel at once (FUN_00456300;
            // not the automap or the mini-panel), else the menu opens.
            if (key == SDLK_ESCAPE) {
                if (chat_typing) chat_typing.reset();                         // a chat line being typed first
                else if (gold_box) gold_box.reset();                          // then the gold box
                else if (net_game && net_game->trade.state != 0) { if (net_game->trade.state == 2) net_game->trade_answer(false); else net_game->trade_cancel(); }
                else if (waypoint.open) waypoint = {};
                else if (store.npc >= 0) { send(cmd::CloseTrade{}); inv_open = false; } // the store first
                else if (speech.npc >= 0) { speech = {}; menu_after_speech = -1; }   // then speech
                else if (npc_menu.npc >= 0) npc_menu = {};          // then the menu
                else if (game_menu.open) close_game_menu();
                else if (help.open) toggle_help();                    // then the Help screen
                else if (inv_open || char_open || stash_open || cube_open || tree_open || quest_log.open || hireling.open || party.open)   // then panels
                    inv_open = char_open = stash_open = cube_open = tree_open = quest_log.open = hireling.open = party.open = false;
                else open_game_menu();
            }
        }
        if (audio_say >= 0) {                              // on the hero: one line at a time
            audio.play_sfx(*scene, audio_say, 1.f, int(rng.next() & 1), 0.f, 0, d2d::game::unit_key(d2d::game::kSoundOnPlayer, hero.id));
            audio_say = -1;
        }
        if (net_game)                                      // the other players' Says
            for (const auto& said : std::exchange(net_game->phrases, {})) audio.play_sfx(*scene, say_sound(said.cls, said.phrase), 1.f, int(rng.next() & 1));
        // Run's key let go (0x4691c0): back to walking, unless the toggle runs.
        if (run_held && !function_held(keys::kRun)) { send(cmd::Run{ false }); run_held = false; }
        // The wheel's functions (Previous / Next Skill by default) when nothing else scrolls.
        if (mouse.wheel != 0 && pad_field_or_free()) {
            const auto fired = keys::actions_of(key_table, mouse.wheel > 0 ? keys::kWheelUp : keys::kWheelDown);
            if (std::ranges::contains(fired, int(keys::kPreviousSkill))) skillbar.cycle(-1);
            if (std::ranges::contains(fired, int(keys::kNextSkill))) skillbar.cycle(1);
        }
        // The game's save (FUN_0052e2a0: every player): Baal dead, the last portal.
        if (std::exchange(world.save_due, false)) save();
        // Classic Terror's End: the game closed under the player (FUN_00530590).
        if (world.game_closed) { world.game_closed = false; save(); screen = Screen::CharSelect; return; }
        // The game menu takes every click while it's up; Exit saves and goes
        // to the roster. Its volumes go to d2d.cfg.
        bool hud_click = false;
        if (game_menu.open) {
            const auto action = game_menu.input(*scene, audio, mouse, keys_this_frame, frame_ms);
            if (game_menu.volume_changed && !cfg_file.empty()
                && !d2d::userdir::save_cfg(cfg_file, { { "master_volume", std::to_string(audio.master_volume) },
                                                      { "music_volume", std::to_string(audio.music_volume) } }))
                d2d::log::warn("d2d.cfg: the volume not saved");
            game_menu.volume_changed = false;
            if (action != GameMenu::kNone) close_game_menu();
            if (action == GameMenu::kControls) {          // CfgOptions (0x47f400): UI 0xb
                controls.family = pad_family;
                controls.pad_extra = pad_controls_page;
                controls.show(key_table, pad_config, character.expansion);
            }
            // Save and Exit logs out whoever's pad has the menu. A partner leaves alone; the first
            // player alone ends the game, in local co-op the next player leads and it goes on.
            if (action == GameMenu::kExit) {
                if (const int seat = focus_seat(); seat > 0) { coop_leave(seat); partner_left = seat; }
                else if ((handed_over = lead_leaves()) < 0) { save(); screen = Screen::CharSelect; }
            }
            hud_click = true;
        } else {
            // The mini-panel's buttons (FUN_0047ec50) and the HUD's button for it.
            const bool left_open = char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open;
            int action = -1;
            hud_click = !coop_hud_hidden() && mini.input(*scene, audio, mouse, left_open, inv_open || tree_open, action);
            if (action == MiniPanel::kCharacter) { char_open = !char_open; if (char_open) stash_open = cube_open = quest_log.open = false; }
            if (action == MiniPanel::kInventory) { inv_open = !inv_open; if (inv_open) tree_open = false; }
            if (action == MiniPanel::kSkills) { tree_open = !tree_open; if (tree_open) inv_open = false; }
            if (action == MiniPanel::kAutomap) automap.open = !automap.open;
            if (action == MiniPanel::kQuests) toggle_quest_log();
            if (action == MiniPanel::kMenu) open_game_menu();
        }
        if (hud_click) {
            if (mouse.press_this_frame) press_on_ui = true;
            mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
        }
        // A trade with another player: its box or window takes its clicks;
        // the bags stay open beside it, the cursor's item goes into our
        // offer (0x18, buffer 2).
        if (net_game && net_game->trade.state != 0 && !game_menu.open) {
            auto& trade = net_game->trade;
            if (trade.state >= 3) { inv_open = true; char_open = stash_open = cube_open = quest_log.open = false; }
            if (trade.state < 3 && gold_box && gold_box->mode == 2) gold_box.reset();
            const auto click = trade_click(*scene, trade.state, mouse.x, mouse.y);
            const bool over_window = trade.state >= 3 && mouse.x >= kCharPanelX && mouse.x < kCharPanelX + 320 && mouse.y >= kCharPanelY && mouse.y < kCharPanelY + 432;
            if (mouse.press_this_frame && (click != TradeClick::kNone || over_window)) {
                if (click == TradeClick::kAccept) {
                    if (trade.state == 2) net_game->trade_answer(true);
                    else if (trade.state >= 3 && trade.state != 7) net_game->trade_accept();
                } else if (click == TradeClick::kGold) {
                    gold_box = GoldBox{ .mode = 2 };
                } else if (click == TradeClick::kDecline) {
                    if (trade.state == 2) net_game->trade_answer(false); else net_game->trade_cancel();
                } else if (click == TradeClick::kOurGrid && hero.held) {
                    const auto& grid = scene->trade_layout[1];
                    const int column = (mouse.x - grid.grid_x) / grid.box_w, row = (mouse.y - grid.grid_y) / grid.box_h;
                    if (const std::uint32_t held_id = net_cursor ? net_cursor : net_game->host_item(*hero.held)) {
                        net_game->send_items({ d2d::net::d2gs::c2s::cursor_to_grid(held_id, std::uint32_t(column), std::uint32_t(row), 2) });
                        auto offered = *hero.held;
                        offered.location = d2d::d2s::item_location::kStored;
                        offered.panel = 101;
                        offered.column = column;
                        offered.row = row;
                        offered.id = int(held_id);
                        trade.ours.push_back(std::move(offered));
                        hero.held.reset();
                        net_cursor = 0;
                    }
                }
                press_on_ui = true;
                mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
            }
        }
        if (inv_open) tree_open = false;       // the stash / a store opened the inventory
        const auto& lay = scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
        // The inventory's and the character panel's close buttons (buysellbtn
        // 10, 11 held): pressed, then let go over it, shuts the panel
        // (FUN_0048edf0 at W - 80 - 0x12e, H - 0x7c; FUN_004a7d00 at 80 + 0x80, H - 0x78).
        auto close_button = [&](bool& down, bool& open, int left, int bottom) {
            const bool over = mouse.x >= left && mouse.x < left + 0x20 && mouse.y > bottom - 0x20 && mouse.y <= bottom;
            if (mouse.press_this_frame && over) { down = true; mouse.press_this_frame = false; }
            if (mouse.release_this_frame && down) { down = false; if (over) open = false; mouse.release_this_frame = false; }
        };
        if (inv_open && !held) close_button(inv_close_down, inv_open, lay.panel_x + 0x12, lay.panel_y + 416);
        if (char_open && !held) close_button(char_close_down, char_open, kCharPanelX + 0x80, kCharPanelY + 420);
        const bool over_panel =
            (tree_open && mouse.x >= 400 && mouse.x < 720 && mouse.y >= 60 && mouse.y < 540) ||
            (inv_open && mouse.x >= lay.panel_x && mouse.x < lay.panel_x + 320
                      && mouse.y >= lay.panel_y && mouse.y < lay.panel_y + 432) ||
            ((char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open) && mouse.x >= kCharPanelX && mouse.x < kCharPanelX + 320
                       && mouse.y >= kCharPanelY && mouse.y < kCharPanelY + 432);
        // The belt: its HUD strip (row 1's boxes) toggles the popup;
        // strip and open popup take the click instead of the world.
        const auto& belt = scene->belts[std::size_t(belt_index(*scene, character.items))];
        const auto& first_box = belt.box[0];
        const auto& fourth_box = belt.box[3];
        const bool over_belt = !coop_hud_hidden() &&
            mouse.x >= first_box[0] && mouse.x <= fourth_box[1]
            && ((mouse.y >= first_box[2] && mouse.y <= first_box[3])
                || (belt_open && mouse.y >= belt.box[std::size_t(std::max(belt.boxes - 1, 0))][2]
                              && mouse.y <= first_box[3]));
        // The item cursor (not with a trade button toggled on, or a
        // menu or speech up). Held over the store's stock, a click
        // sells the item.
        bool item_click = false;
        // With an identify scroll picked, a click names the item for it and ends the pick.
        if (identify_with >= 0 && mouse.press_this_frame) {
            const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                                { inv_open, stash_open, cube_open, belt_open, character.expansion }, mouse.x, mouse.y);
            if (const auto* to_cursor = click.cmd ? std::get_if<cmd::ToCursor>(&*click.cmd) : nullptr) net.send(cmd::IdentifyWith{ identify_with, to_cursor->item });
            identify_with = -1;
            item_click = true;
        }
        // With Charsi's Imbue chosen, a click on an item takes it up and imbues it.
        if (imbue_with >= 0 && mouse.press_this_frame && !item_click) {
            const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                                { inv_open, stash_open, cube_open, belt_open, character.expansion }, mouse.x, mouse.y);
            if (click.cmd && std::holds_alternative<cmd::ToCursor>(*click.cmd)) {
                net.send(*click.cmd);
                net.send(cmd::Imbue{ imbue_with });
            }
            imbue_with = -1;
            item_click = true;
        }
        if (mouse.press_this_frame && !item_click && store.mode == 0 && npc_menu.npc < 0 && speech.npc < 0) {
            if (held && store.npc >= 0 && mouse.x >= 96 && mouse.x < 96 + 10 * 29
                && mouse.y >= 123 && mouse.y < 123 + 10 * 29) {
                net.send(cmd::Sell{ held->id });
                item_click = true;
            } else {
                const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                                    { inv_open, stash_open, cube_open, belt_open, character.expansion }, mouse.x, mouse.y);
                if (click.cmd) net.send(*click.cmd);
                item_click = click.consumed;
            }
        }
        if (mouse.press_this_frame && over_belt && mouse.y >= first_box[2] && !item_click) belt_open = !belt_open;
        // A right-click on a carried item uses it: a potion in the inventory,
        // stash or belt is drunk.
        if (mouse.rpress_this_frame && !held && store.npc < 0 && npc_menu.npc < 0 && speech.npc < 0) {
            const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                                { inv_open, stash_open, cube_open, belt_open, character.expansion }, mouse.x, mouse.y);
            const auto* to_cursor = click.cmd ? std::get_if<cmd::ToCursor>(&*click.cmd) : nullptr;
            const auto used = to_cursor ? std::ranges::find(character.items, to_cursor->item, &d2d::d2s::Item::id) : character.items.end();
            if (imbue_with >= 0) imbue_with = -1;                             // a right-click drops the imbue pick too
            else if (identify_with >= 0) identify_with = -1;                  // a right-click drops the pick
            else if (used != character.items.end() && (used->code == "isc" || used->code == "ibk")) {
                identify_with = used->id;
                audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorIdentifyReady, 1.f, 0);
            }
            else if (to_cursor) net.send(cmd::UseItem{ to_cursor->item });
        }
        // Quest log (FUN_004a3e40 press, FUN_004a42a0 release): a tab switches
        // on the press; an icon with something to say, close and questlast
        // hold with a click (Sounds.txt 4); let go on the same icon picks it,
        // over close shuts the log, over questlast plays the picked quest's
        // message again (FUN_004a27d0).
        // ponytail: the description isn't hidden while that message plays (0x7bf2b3).
        if (quest_log.open) {
            const int button = quest_button_at(mouse.x, mouse.y);
            const int slot = quest_slot_at(mouse.x, mouse.y);
            if (mouse.press_this_frame) {
                if (const int act = quest_tab_at(mouse.x, mouse.y, character.expansion); act >= 0) quest_log_tab(quest_log, quest_bits(), quest_state(), act, character.expansion);
                else if (slot >= 0) {
                    for (const auto& entry : kQuestLog)
                        if (entry.act == quest_log.act && entry.slot == slot && quest_text(quest_bits(), entry.quest, quest_state()).shown != 2) {
                            quest_log.pressed = slot;
                            audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0);
                        }
                } else if (button) {
                    (button == 1 ? quest_log.close_down : quest_log.last_down) = true;
                    audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0);
                }
            }
            if (mouse.release_this_frame) {
                if (quest_log.close_down) { if (button == 1) quest_log.open = false; }
                else if (quest_log.pressed >= 0) {
                    if (slot == quest_log.pressed) quest_log.remembered[std::size_t(quest_log.act)] = quest_log.slot = slot;
                } else if (quest_log.last_down && button == 2 && quest_log.slot >= 0) {
                    for (const auto& entry : kQuestLog)
                        if (entry.act == quest_log.act && entry.slot == quest_log.slot)
                            if (const auto text = quest_text(quest_bits(), entry.quest, quest_state()); text.speech) replay_speech = text.speech;
                }
                quest_log.pressed = -1;
                quest_log.close_down = quest_log.last_down = false;
            }
        }
        // The Quest Log button (UI 0x11): pressed with a click (FUN_004a2a20),
        // let go over it it goes and the log opens on its quest (FUN_004a4110).
        if (const auto quest_button = quest_log_button_now(); quest_button.x0 >= 0) {
            const bool over = over_quest_log_button(quest_button, mouse.x, mouse.y);
            if (mouse.press_this_frame && over) { quest_log.button_down = true; audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0); }
            if (mouse.release_this_frame) {
                if (quest_log.button_down && over) { quest_log.button = false; toggle_quest_log(); }
                quest_log.button_down = false;
            }
        }
        // Skill tree: tabs switch on press; a skill icon pressed and
        // released spends a point (FUN_004ab7e0 / FUN_004abc30).
        if (tree_open) {
            const int cls = std::max(character.character_class, 0);
            const int skill = skill_at(*scene, cls, tree_tab, mouse.x, mouse.y);
            if (mouse.press_this_frame) {
                if (const int tab = skill_tab_at(mouse.x, mouse.y); tab > 0) tree_tab = tab;
                skill_pressed = character.stats.get(d2d::d2s::kSkillPts) > 0
                    && d2d::rules::can_learn(scene->rules, cls, skill, character.stats.skills, int(character.stats.get(d2d::d2s::kLevel))) ? skill : -1;
            }
            if (mouse.release_this_frame) {
                if (skill_pressed >= 0 && skill == skill_pressed) net.send(cmd::SkillPoint{ skill });
                skill_pressed = -1;
            }
        }
        // Char panel stat buttons: press, then release on the same
        // button spends a point (Shift: all of them), FUN_004a78c0.
        if (char_open && character.stats.get(d2d::d2s::kStatPts) > 0) {
            const int stat_button = stat_button_at(mouse.x, mouse.y);
            if (mouse.press_this_frame && stat_button >= 0) stat_pressed = stat_button;
            if (mouse.release_this_frame) {
                if (stat_pressed >= 0 && stat_button == stat_pressed) {
                    const int count = (SDL_GetModState() & SDL_KMOD_SHIFT) ? int(character.stats.get(d2d::d2s::kStatPts)) : 1;
                    net.send(cmd::StatPoint{ kStatButtons[std::size_t(stat_button)].stat, count });
                }
                stat_pressed = -1;
            }
        } else {
            stat_pressed = -1;
        }
        // Right-clicking the Horadric Cube ("box") in the inventory
        // or the stash opens it in the left panel, as D2 does.
        if (mouse.rpress_this_frame)
            for (const auto& item : character.items) {
                if (item.code != "box" || item.location != d2d::d2s::item_location::kStored) continue;
                const bool in_inv = inv_open && item.panel == d2d::d2s::item_panel::kInventory, in_stash = stash_open && item.panel == d2d::d2s::item_panel::kStash;
                if (!in_inv && !in_stash) continue;
                const auto rect = grid_rect(*scene, in_inv ? lay : scene->stash_layout[character.expansion ? 1 : 0], item);
                if (mouse.x >= rect[0] && mouse.x < rect[0] + rect[2] && mouse.y >= rect[1] && mouse.y < rect[1] + rect[3]) {
                    if (net_game) send(cmd::UseItem{ item.id });  // the host opens it (0x20 -> 0x77 0x15)
                    else { cube_open = true; stash_open = char_open = false; }
                }
            }
        // The Cube's Transmute (FUN_0048a190): held on a press over it, sent
        // on letting go over it — the World's (cmd::Transmute), a joined
        // game's host's (C->S 0x4f 0x18).
        if (cube_open && mouse.press_this_frame && transmute_at(*scene, mouse.x, mouse.y)) {
            transmute_down = true;
            audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0);
        }
        if (transmute_down && !mouse.down) {
            if (cube_open && transmute_at(*scene, mouse.x, mouse.y)) {
                if (net_game) net_game->send_items({ d2d::net::d2gs::c2s::button(0x18) });
                else send(cmd::Transmute{});
            }
            transmute_down = false;
        }
        // The gold coins open the gold box (0x454150): the stash's to withdraw
        // (4), the inventory's to deposit (3) with the stash open, else to drop (1).
        const bool trading = net_game && net_game->trade.state != 0;
        if (!gold_box && !trading && store.npc < 0 && mouse.press_this_frame
            && ((stash_open && stash_coin_at(mouse.x, mouse.y)) || (inv_open && inventory_coin_at(mouse.x, mouse.y)))) {
            gold_box = GoldBox{ .mode = stash_open ? (stash_coin_at(mouse.x, mouse.y) ? 4 : 3) : 1 };
            audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0);
            mouse.press_this_frame = false;
        }
        // An open NPC menu takes every click: an entry runs its action,
        // anything else closes it.
        bool menu_click = false;
        if (g_drawn.level == level) {                                 // drawn last frame, in this level
            automap_reveal(*scene, *level, automap, g_drawn.cells);
            if (view.level == level) automap_icons(*world.game_data, *level, automap, view.player.x, view.player.y, view.npc_states);
            for (const auto& neighbour : level->nearby)               // what's in view across the edge
                if (neighbour.level->layer == level->layer) automap_reveal(*scene, *neighbour.level, automap, g_drawn.cells, neighbour.dx, neighbour.dy);
        }
        // The NPC's voice (FUN_004a10e0 plays FUN_004e0650's sound for
        // the speech string) follows the speech box.
        if (speech.npc < 0 && audio.voice.src) audio.stop_voice();
        // The level's SoundEnviron (Levels.txt SoundEnv -> Song, Day / Night
        // Ambience, Day / Night Event). Day is the day's phases 1..3
        // (FUN_004e42e0 via FUN_0061c220), 0° to 180°; else night.
        const bool day = view.day.phase >= 1 && view.day.phase <= 3;
        const int amb = day ? level->ambience : level->night_ambience;
        if (audio.music.sound == 0) {
            audio.play_music(*scene, level->song);
            audio.play(audio.ambience, *scene, amb);
            if (audio.music.sound == 0) audio.music.sound = -1;   // don't retry every frame
        }
        // Another level's song: game.exe (FUN_004dcaa0) switches 75 sound
        // ticks (3 s) into the new level, so skirting an edge doesn't flip
        // it, and cross-fades (Audio::crossfade_music). Its ambience then,
        // and at dusk and dawn.
        if (audio.music.sound > 0 && level->song > 0 && audio.music.sound != level->song && frame_ms - level_ms >= 75 * 40)
            audio.crossfade_music(*scene, level->song);
        if (audio.music.sound > 0 && audio.music.sound == level->song && audio.ambience.sound != amb) {
            const bool turn = (audio.ambience.sound == level->ambience && amb == level->night_ambience)
                           || (audio.ambience.sound == level->night_ambience && amb == level->ambience);
            if (turn) audio.crossfade_ambience(*scene, amb);   // dusk / dawn (FUN_004e42e0)
            else audio.play(audio.ambience, *scene, amb);
            audio.ambience.sound = amb;           // tried: not again every frame
        }
        // The weather, a client frame at a time where it rains (FUN_00473f50;
        // leaving, the drops go and the cycle waits), and its sound: Sounds.txt
        // 64 scene_rain at the density's volume, easing 6 a tick
        // (FUN_004e42e0).
        if (frame_ms - rain_ms > 1000) rain_ms = frame_ms;
        for (; frame_ms - rain_ms >= 40; rain_ms += 40) {
            den_tick(frame_ms);
            critter_tick(frame_ms);
            const float dx = cam_x - rain_cam_x, dy = cam_y - rain_cam_y;
            rain_cam_x = cam_x; rain_cam_y = cam_y;
            ++rain.clock;                                 // the client frame (0x7a0498)
            if (level->mud) rain.tick_bubbles(int(std::lround((dx - dy) * (kIsoW / 2))), int(std::lround((dx + dy) * (kIsoH / 2))));
            else rain.bubbles.clear();
            if (!level->rain) { rain.drops.clear(); rain.splashes.clear(); }
            else rain.tick(rain.rng, g_view_width, int(kScreenHeight), int(std::lround((dx - dy) * (kIsoW / 2))), int(std::lround((dx + dy) * (kIsoH / 2))),
                           d2d::rules::kDay[std::size_t(view.day.phase)].type, level->id, d2d::rules::level_ids::act_of(level->id));
            const int want = level->rain && !rain.snow ? int(rain.volume() * 255.f) : 0;   // FUN_00473c40: no sound in the snow
            rain_vol = rain_vol < want ? std::min(want, rain_vol + 6) : std::max(want, rain_vol - 6);
        }
        // The lightning's drawn frame (a frame drawn each update) (FUN_00473910): its flash, and thunder
        // queued that many sound ticks on.
        if (const auto strike = rain.frame(rain.rng, audio.dev != nullptr); strike.thunder >= 0) {
            thunder_at = frame_ms + std::uint32_t(strike.thunder) * 40; thunder_x = strike.x; thunder_y = strike.y; thunder_due = true;
        }
        if (thunder_due && frame_ms >= thunder_at) { thunder_due = false; audio.play_placed(*scene, d2d::rules::sound_ids::kEventThunder, 0, float(thunder_x), float(thunder_y)); }
        if (rain_vol > 0 && audio.rain.sound != d2d::rules::sound_ids::kSceneRain) audio.play(audio.rain, *scene, d2d::rules::sound_ids::kSceneRain);
        if (rain_vol == 0 && audio.rain.sound) audio.stop(audio.rain);
        if (audio.rain.src && scene->sounds.size() > d2d::rules::sound_ids::kSceneRain)
            audio.set_gain(audio.rain, float(scene->sounds[d2d::rules::sound_ids::kSceneRain].volume) / 255.f * float(rain_vol) / 255.f);
        // Every Event Delay ticks, give or take a third, one of the event
        // sounds from the left or right (FUN_004e42e0: x +-450..750 and
        // 2 dy -100..100 client pixels, FUN_004b99a0; the first within one
        // gap of arriving), mixed by that distance like any placed sound.
        {
            const std::uint32_t tick = frame_ms / 40;
            const int event_sound = day ? level->day_event : level->night_event, delay = level->event_delay;
            auto spread = [&](int spread_range) { return sound_rng(2 * spread_range + 1) - spread_range; };
            if (event_sound != amb_event) {
                amb_event = event_sound;
                amb_next = std::uint32_t(std::max(1, delay + spread(delay / 3)));
                amb_last = tick - std::uint32_t(sound_rng(int(amb_next)));
            }
            if (event_sound > 0 && tick - amb_last >= amb_next && !audio.world_paused) {
                const int x = (sound_rng(2) ? 1 : -1) * (450 + sound_rng(301));
                const int y = sound_rng(201) - 100;
                audio.play_placed(*scene, event_sound, sound_rng(16), float(x), float(y));
                amb_last = tick;
                amb_next = std::uint32_t(std::max(1, delay + spread(delay / 3)));
            }
        }
        if (questdone_sound) { questdone_sound = false; audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorQuestDone, 1.f, 0); }
        if (replay_speech) {
            const auto found = std::ranges::find_if(kSpeechSound, [&](const auto& entry) { return entry.first == replay_speech; });
            if (found != kSpeechSound.end()) audio.play_voice(*scene, found->second);
            replay_speech = 0;
        }
        if (speech.npc >= 0 && speech.voice == 0) {
            speech.voice = -1;
            const auto found = std::ranges::find_if(kSpeechSound, [&](const auto& entry) { return entry.first == speech.string; });
            if (found != kSpeechSound.end()) { speech.voice = found->second; audio.play_voice(*scene, found->second); }
        }
        if (speech.npc >= 0 && (speech.done(frame_ms) || mouse.press_this_frame)) {
            menu_click = mouse.press_this_frame;          // a click skips the speech
            speech = {};
            if (menu_after_speech >= 0 && std::size_t(menu_after_speech) < level->npcs.size()) open_menu(menu_after_speech);
            menu_after_speech = -1;
        } else if (npc_menu.npc >= 0 && npc_menu.scroll_max > 0 && mouse.wheel != 0) {   // the hire list scrolls
            npc_menu = open_hire_menu(*scene, npc_menu.npc, hire_offers, character.stats.get(d2d::d2s::kGold) + character.stats.get(d2d::d2s::kGoldBank),
                                      npc_menu.scroll - mouse.wheel);
        } else if (npc_menu.npc >= 0 && mouse.press_this_frame) {
            menu_click = true;                            // the click is the menu's, not a walk
            const int line_index = npc_menu.line_at(mouse.x, mouse.y);
            const auto action = line_index >= 0 ? npc_menu.lines[std::size_t(line_index)].action : NpcMenuState::kClose;
            const int npc_menu_arg = line_index >= 0 ? npc_menu.lines[std::size_t(line_index)].arg : -1;
            const int who = npc_menu.npc;
            const auto& npc = level->npcs[std::size_t(who)];
            const auto& state = view.npc_states[std::size_t(who)];
            const float dx = (npc.path.empty() ? npc.x : state.x) - view.player.x, dy = (npc.path.empty() ? npc.y : state.y) - view.player.y;
            const int screen_x = int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
            const int screen_y = kViewY + int(std::lround((dx + dy) * (kIsoH / 2)));
            npc_menu = {};
            if (action == NpcMenuState::kHire) {
                net.send(cmd::OpenHire{ who });             // Kashya's list: the World rolls it
            } else if (action == NpcMenuState::kHireOffer) {
                net.send(cmd::Hire{ npc_menu_arg });
            } else if (action == NpcMenuState::kIdentify) {
                net.send(cmd::Identify{});
            } else if (action == NpcMenuState::kGamble || action == NpcMenuState::kTrade) {
                net.send(cmd::OpenTrade{ who, action == NpcMenuState::kGamble });
            } else if (action == NpcMenuState::kTalk) {
                npc_menu = open_talk_menu(*scene, *level, who, screen_x, screen_y, npc_quest);
            } else if (action == NpcMenuState::kRespec) {
                npc_menu = open_respec_menu(*scene, who, screen_x, screen_y);
            } else if (action == NpcMenuState::kRespecOk) {
                net.send(cmd::Respec{ who });
            } else if (action == NpcMenuState::kCaravan) {
                send(cmd::Caravan{ who });                      // a joined game: the host's NPC (Town::send)
            } else if (action == NpcMenuState::kResurrectMerc) {
                net.send(cmd::ResurrectMerc{ who });
            } else if (action == NpcMenuState::kImbue) {
                // 0x4b35b0: the menu closes and the inventory opens; the
                // server imbues the item in hand (C->S 0x38 kind 0).
                // ponytail: game.exe opens a one-slot item panel (0x4c0620):
                // the item in hand goes in its slot, identified and
                // imbuable only (FUN_004bfc50), and the panel's button sends
                // (FUN_004bfaf0). Here an item in hand goes at once, else
                // the next click on an item takes it up and sends
                // (act1-end.md "Imbue").
                if (held) net.send(cmd::Imbue{ who });
                else { imbue_with = who; inv_open = true; char_open = stash_open = cube_open = quest_log.open = false; }
            } else if (action == NpcMenuState::kQuest) {
                speech = start_speech(*scene, who, std::uint16_t(npc_menu_arg), frame_ms);
                send(cmd::QuestMessage{ who, npc_menu_arg });
            } else if (action == NpcMenuState::kIntro || action == NpcMenuState::kGossip) {
                const auto found = std::ranges::find_if(kNpcTalk, [&](const NpcTalk& entry) { return entry.hc_idx == npc.hc_idx; });
                if (found != kNpcTalk.end() && !found->topics.empty()) {
                    const int cls = std::max(character.character_class, 0);
                    if (gossip_pick.size() != level->npcs.size()) gossip_pick.assign(level->npcs.size(), -1);
                    int topic;
                    auto done = [&](int quest) { return character.header.quest_flag(character.header.active_difficulty(), quest, 0); };
                    if (action == NpcMenuState::kIntro) {
                        topic = talk_topic(*found, true, cls, rng, done);
                    } else {
                        if (gossip_pick[std::size_t(who)] < 0)
                            gossip_pick[std::size_t(who)] = talk_topic(*found, false, cls, rng, done);
                        topic = gossip_pick[std::size_t(who)];
                    }
                    speech = start_speech(*scene, who, found->topics[std::size_t(topic)].string, frame_ms);
                }
            }
            menu_click = true;
        }
        // Store: tabs (x 80+80i, y 60..90) switch; the buttons press;
        // close (button 4 at non-repair vendors) closes.
        store.pressed.fill(false);
        if (store.npc >= 0) {
            static constexpr int kBtnX[4] = { 116, 169, 221, 273 };
            for (int i = 0; i < 4; ++i) {
                const int button_x = kCharPanelX - 1 + kBtnX[i], button_y = 476 - 32 + 1;
                const bool hovered = mouse.x >= button_x && mouse.x < button_x + 32 && mouse.y >= button_y && mouse.y < button_y + 32;
                if (hovered && mouse.down) store.pressed[std::size_t(i)] = true;
                if (hovered && mouse.release_this_frame && i < 2) store.mode = store.mode == i + 1 ? 0 : i + 1;
                // Repair vendors: repair (6) toggles like buy/sell,
                // repair all (18) fixes everything worn and carried.
                const bool repairer = store_button_frames(store)[2] == 6;
                if (hovered && mouse.release_this_frame && i == 2 && repairer) store.mode = store.mode == 3 ? 0 : 3;
                if (hovered && mouse.release_this_frame && i == 3 && repairer) net.send(cmd::Repair{ -1 });
                if (hovered && mouse.release_this_frame && i == 3 && store_button_frames(store)[3] == 10) {
                    send(cmd::CloseTrade{});
                    inv_open = false;
                    break;
                }
            }
            if (mouse.press_this_frame && mouse.y >= 60 && mouse.y <= 90
                && mouse.x >= kCharPanelX && mouse.x < kCharPanelX + 320)
                store.tab = (mouse.x - kCharPanelX) / 80;
            // Right-click on stock buys; with Buy or Sell toggled on,
            // a left click buys the stock item / sells your item.
            const int store_index = store.npc >= 0 ? store_item_at(*scene, store, mouse.x, mouse.y) : -1;
            if (store_index >= 0 && (mouse.rpress_this_frame || (mouse.press_this_frame && store.mode == 1)))
            {
                send(cmd::Buy{ store_index, store.tab });
            }
            // Sell: an inventory item; repair: that or a worn one.
            if (store.npc >= 0 && mouse.press_this_frame && (store.mode == 2 || store.mode == 3))
                for (std::size_t i = 0; i < character.items.size(); ++i) {
                    const auto& item = character.items[i];
                    const bool worn = item.location == d2d::d2s::item_location::kEquipped && item.slot >= d2d::d2s::body_location::kFirst && item.slot <= d2d::d2s::body_location::kLast;
                    if (!(item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory) && !(worn && store.mode == 3)) continue;
                    const auto rect = worn ? lay.slots[std::size_t(item.slot)] : grid_rect(*scene, lay, item);
                    if (mouse.x >= rect[0] && mouse.x < rect[0] + rect[2] && mouse.y >= rect[1] && mouse.y < rect[1] + rect[3]) {
                        if (store.mode == 2) send(cmd::Sell{ item.id });
                        else send(cmd::Repair{ item.id });
                        break;
                    }
                }
        }
        // Waypoint panel: tabs switch acts, cancel (or the row of the
        // level you're in) closes it.
        if (waypoint.open) {
            const bool on_cancel = mouse.x >= kCharPanelX + 0x111 && mouse.x < kCharPanelX + 0x111 + 0x24
                                && mouse.y >= 60 + 0x183 && mouse.y < 60 + 0x183 + 0x22;
            waypoint.cancel_down = on_cancel && mouse.down;
            if (mouse.press_this_frame) {
                if (const int tab = waypoint_tab_at(character.header, character.expansion, mouse.x, mouse.y); tab >= 0) waypoint.tab = tab;
                else if (const int row = waypoint_row_at(*scene, waypoint, character.header, mouse.x, mouse.y); row >= 0) {
                    net.send(cmd::Waypoint{ waypoint.npc, scene->waypoint_levels[std::size_t(waypoint.tab)][std::size_t(row)].level });
                    waypoint = {};
                }
            }
            if (on_cancel && mouse.release_this_frame) waypoint = {};
        }
        // New Stats / New Skills: a press holds the button with a click
        // (Sounds.txt 4, FUN_004a66e0 / FUN_004a6790); let go over it and it
        // opens the character panel / tree (FUN_004a6840 / FUN_004a6920).
        const auto shown = level_buttons_now();
        const bool on_stats = over_stats_button(shown.stats_x, mouse.x, mouse.y);
        const bool on_skills = over_skills_button(shown.skills_x, mouse.x, mouse.y);
        if (shown.stats_x < 0) stats_down = false;          // hidden lets go (FUN_004a6b30)
        if (shown.skills_x < 0) skills_down = false;
        const bool level_click = mouse.press_this_frame && (on_stats || on_skills);
        if (level_click) { (on_stats ? stats_down : skills_down) = true; audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0); }
        if (mouse.release_this_frame) {
            if (stats_down && on_stats) { char_open = true; stash_open = cube_open = quest_log.open = false; }
            if (skills_down && on_skills) { tree_open = true; inv_open = false; }
            stats_down = skills_down = false;
        }
        // The run button: the same press and click (FUN_00499500); let go
        // over it and run toggles, as R does (FUN_004996a0).
        const bool on_run = !coop_hud_hidden() && over_run_button(mouse.x, mouse.y);
        const bool run_click = mouse.press_this_frame && on_run;
        if (run_click) { run_down = true; audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0); }
        if (mouse.release_this_frame) {
            if (run_down && on_run) send(cmd::Run{ !view.running });
            run_down = false;
        }
        // Holding an item, a click on the world drops it (C→S 0x17).
        const bool bar_click = (!coop_hud_hidden() || skillbar.picking) && skillbar.click(mouse);
        if (held && mouse.press_this_frame && !item_click && !over_panel && !over_belt && !menu_click && !bar_click && !level_click && !run_click
            && npc_menu.npc < 0 && speech.npc < 0 && store.npc < 0 && have_world)
            net.send(cmd::Drop{ held->id });
        const bool over_ui = over_panel || over_belt || menu_click || npc_menu.npc >= 0 || item_click || held || bar_click || level_click || run_click
                          || hud_click || game_menu.open;
        // A press on the UI stays the UI's while the button is held: no walk
        // starts under a menu that just closed.
        if (mouse.press_this_frame) press_on_ui = over_ui;
        else if (!mouse.down) press_on_ui = false;
        if (have_world) walk(mouse, over_ui || press_on_ui, audio, frame_ms, last_ms);
        // Walked away from the NPC whose windows are open: they shut, as Esc.
        // game.exe's panels leave no world to walk in; widescreen does
        // (deviations.md). 2 cells is ours: game.exe talks within 6 subtiles.
        if (const int npc_open = store.npc >= 0 ? store.npc : npc_menu.npc >= 0 ? npc_menu.npc : speech.npc;
            have_world && level && npc_open >= 0 && std::size_t(npc_open) < level->npcs.size() && std::size_t(npc_open) < view.npc_states.size()) {
            const auto& npc = level->npcs[std::size_t(npc_open)];
            const auto& state = view.npc_states[std::size_t(npc_open)];
            const float dx = (npc.path.empty() ? npc.x : state.x) - view.player.x, dy = (npc.path.empty() ? npc.y : state.y) - view.player.y;
            if (dx * dx + dy * dy > 2.f * 2.f) {
                if (store.npc >= 0) { send(cmd::CloseTrade{}); inv_open = false; }
                npc_menu = {}; speech = {}; menu_after_speech = -1;
            }
        }
        std::erase_if(quest_quakes, [&](d2d::rules::QuestQuake& control) {
            return control.step(frame_ms, shake, [&] {
                if (const auto found = scene->sound_index.find("monster_diablo_taunt_1"); found != scene->sound_index.end())
                    audio.play_sfx(*scene, found->second, 1.f, 0);
            });
        });
        audio.quake(*scene, shake.step(frame_ms, shake_rng));
        play_cues(cues, audio, view.player.x, view.player.y, rng, frame_ms);
        object_sounds(audio, frame_ms, view.player.x, view.player.y);
        audio.stop_owners([&](int owner) { return owner > 0 ? std::ranges::contains(view.missiles, owner, &d2d::game::View::Shot::sound) : std::ranges::contains(object_loops, owner); });
        audio.track(view.player.x, view.player.y, [&](int owner) -> std::optional<std::pair<float, float>> {
            if (const auto shot = std::ranges::find(view.missiles, owner, &d2d::game::View::Shot::sound); owner > 0 && shot != view.missiles.end()) return std::pair{ shot->x, shot->y };
            if (const auto object = std::size_t(-1 - owner); owner < 0 && object < view.npc_states.size()) return std::pair{ view.npc_states[object].x, view.npc_states[object].y };
            return std::nullopt;
        });
        audio.pause_world(false);                  // a cinematic's hold ends
        audio.menu_paused = !net_game && (game_menu.open || controls.open);   // the world's sounds fade out (Audio::step_ducks)
        draw(framebuffer, mouse, frame_ms);
        // Screen Shot (FUN_004fa7a0): at most one a second, Screenshot%03d,
        // the first number free. d2d writes PNG in its screenshots folder
        // (deviations.md), game.exe JPEG in the game's.
        if (std::exchange(screenshot_wanted, false) && !shot_dir.empty() && frame_ms - last_shot_ms >= 1000) {
            last_shot_ms = frame_ms;
            for (int number = 0; number < 1000; ++number) {
                const auto path = shot_dir / std::format("Screenshot{:03}.png", number);
                if (std::filesystem::exists(path)) continue;
                try {
                    d2d::screenshot::save_png(path, framebuffer, std::uint32_t(fb_width(framebuffer)), kScreenHeight);
                    d2d::log::info("screenshot: {}", path.string());
                } catch (const std::exception& error) { d2d::log::warn("screenshot: {}", error.what()); }
                break;
            }
        }
    }

auto Town::quest_bits() const -> const d2d::rules::QuestBits& {
        return character.header.quests[std::size_t(std::clamp(character.header.active_difficulty(), 0, 2))];
    }

// The player's act (0-based): the act of the level the View has them on
// (acts 1 and 2 so far; a joined game's too).
auto Town::view_act() const -> int { return view.level ? d2d::rules::level_ids::act_of(view.level->id) : 0; }

auto Town::toggle_quest_log() -> void {
        if (quest_log.open) { quest_log.open = false; return; }
        quest_log_open(quest_log, quest_bits(), quest_state(), view_act());
        char_open = stash_open = cube_open = false;
    }

auto Town::quest_log_button_now() const -> QuestLogButton {
        const bool left_open = char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open;
        return quest_log_button(quest_log, left_open, inv_open || tree_open, char_open);
    }

auto Town::level_buttons_now() const -> LevelButtons {
        const bool left_open = char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open;
        auto out = level_buttons(character.stats, left_open, inv_open || tree_open, char_open, tree_open, store.npc >= 0);
        out.stats_down = stats_down;
        out.skills_down = skills_down;
        return out;
    }

auto Town::hovered_monster() const -> int {
        return hovered_npc <= -10 && hovered_npc > -1000 && std::size_t(-10 - hovered_npc) < view.monsters.size() ? -10 - hovered_npc : -1;
    }

auto Town::hovered_ground() const -> int {
        return hovered_npc <= -1000 && std::size_t(-1000 - hovered_npc) < view.ground.size() ? -1000 - hovered_npc : -1;
    }

auto Town::input(const Mouse& mouse, bool over_ui) const -> std::vector<Command> {
        std::vector<Command> out;
        if (view.dead && (!view.ghost || view.all_dead)) {   // a click, once the death has played, respawns
            if (mouse.press_this_frame) out.push_back(cmd::Resurrect{});
            return out;
        }
        if (over_ui) return out;
        // Screen -> world: invert the iso projection around the player,
        // who sits at (kW/2, kViewY).
        const float iso_u = float(mouse.x - int(kScreenWidth) / 2) / (kIsoW / 2);
        const float iso_v = float(mouse.y - kViewY) / (kIsoH / 2);
        const float world_x = cam_x + (iso_u + iso_v) / 2, world_y = cam_y + (iso_v - iso_u) / 2;
        if (view.ghost) {                                    // a ghost only walks
            if (mouse.press_this_frame || mouse.down) out.push_back(cmd::Move{ world_x, world_y, mouse.press_this_frame });
            return out;
        }
        const int hovered_monster_index = hovered_monster();
        const bool live = hovered_monster_index >= 0 && view.monsters[std::size_t(hovered_monster_index)].alive();
        if (mouse.press_this_frame) {
            if (live) out.push_back(cmd::UseSkill{ skillbar.left, world_x, world_y, view.monsters[std::size_t(hovered_monster_index)].id, true, skillbar.left_item });
            else if (hovered_ground() >= 0) out.push_back(cmd::Pickup{ view.ground[std::size_t(hovered_ground())].id });
            else if (hovered_npc >= 0 || (hovered_npc <= -2000 && hovered_npc > -2000 - int(d2d::game::World::kGamePortals)) || (hovered_npc <= -3000 && hovered_npc > -3016)
                     || (hovered_npc <= -4000 && hovered_npc > -4000 - int(view.others.size())))
                out.push_back(cmd::Interact{ hovered_npc });
            else if (function_held(keys::kStandStill)) out.push_back(cmd::UseSkill{ skillbar.left, world_x, world_y, -1, true, skillbar.left_item });   // 0x7a0664: no step, the skill there
            else out.push_back(cmd::Move{ world_x, world_y, true });
        } else if (mouse.down) {                             // held: the attack goes on, else the walk re-aims
            if (const int attacked = view.monster(view.attack); attacked >= 0 && view.monsters[std::size_t(attacked)].alive())
                out.push_back(cmd::UseSkill{ view.attack_skill, world_x, world_y, view.attack, true, view.attack_skill == skillbar.left ? skillbar.left_item : -1 });
            else if (function_held(keys::kStandStill))
                out.push_back(cmd::UseSkill{ skillbar.left, world_x, world_y, -1, true, skillbar.left_item });
            else
                out.push_back(cmd::Move{ world_x, world_y, false });
        }
        if (mouse.rpress_this_frame && skillbar.right == kResurrectSkill) {   // co-op: the body nearest the click
            if (const auto* body = body_near(world_x, world_y, 1.5f)) out.push_back(cmd::UseSkill{ kResurrectSkill, body->x, body->y, body->id });
        } else if (mouse.rpress_this_frame) {
            out.push_back(cmd::UseSkill{ skillbar.right, world_x, world_y, live ? view.monsters[std::size_t(hovered_monster_index)].id : -1, false, skillbar.right_item });
        }
        return out;
    }

auto Town::pad_field() const -> bool {
        return have_world && !inv_open && !char_open && !tree_open && !stash_open && !cube_open && !belt_open && !quest_log.open && !hireling.open && !help.open && !party.open
            && !game_menu.open && !controls.open && !waypoint.open && npc_menu.npc < 0 && speech.npc < 0 && store.npc < 0 && !held && identify_with < 0
            && !skillbar.picking && !chat_typing && !gold_box && !(net_game && net_game->trade.state != 0);
    }

// What the pad's A would act on now: a monster in reach in the facing (the
// held target first), else, with `uses` (a fresh press), the ground item,
// then the NPC, object, town portal or corpse in reach there.
auto Town::pad_aim(int skill, bool steering) const -> pad::Aim {
        const auto* info = scene ? scene->skills.get(skill) : nullptr;
        // Skills.txt's range is none / h2h / range / both; spells (Fire Ball: none) fire a missile.
        const bool ranged = info && (!info->srvmissile.empty() || info->range == "range");
        if (!ranged) return {};
        return { 12.f, 14.f, steering ? pad::Aim{}.cone_cos : -2.f };   // the stick left alone: any way round
    }

auto Town::pad_pick(const PadPlayer& player_pad, const UnitState& unit, bool uses, const pad::Aim& aim) const -> PadPick {
        const float hero_x = unit.x, hero_y = unit.y, face_x = player_pad.face_x, face_y = player_pad.face_y;
        std::vector<pad::Candidate> monsters;
        for (const auto& monster : view.monsters)
            if (monster.alive() && !monster.ally_not_interact(*scene)) monsters.push_back({ monster.id, monster.unit.x, monster.unit.y });   // not a converted one, as the cursor
        if (const int hit = pad::pick_target(hero_x, hero_y, face_x, face_y, monsters, player_pad.target, aim); hit >= 0)
            return { PadPick::kMonster, monsters[std::size_t(hit)].id, monsters[std::size_t(hit)].x, monsters[std::size_t(hit)].y };
        if (!uses) return {};
        std::vector<pad::Candidate> drops;
        for (const auto& item : view.ground) drops.push_back({ item.id, item.x, item.y });
        if (const int pick = pad::pick_target(hero_x, hero_y, face_x, face_y, drops, -1, pad::Aim{ 3.5f, 3.5f, -0.2f }); pick >= 0)
            return { PadPick::kItem, drops[std::size_t(pick)].id, drops[std::size_t(pick)].x, drops[std::size_t(pick)].y };
        std::vector<pad::Candidate> things;      // NPCs and objects you can use
        if (level)
            for (std::size_t i = 0; i < level->npcs.size(); ++i) {
                const auto& npc = level->npcs[i];
                const UnitState* state = i < view.npc_states.size() ? &view.npc_states[i] : nullptr;
                if ((state && state->hidden) || npc.name.empty()) continue;
                if (state && !state->mode.empty() && npc.root == "objects" && !(npc.selectable >> game::mode_index(state->mode) & 1)) continue;
                things.push_back({ int(i), state ? state->x : npc.x, state ? state->y : npc.y });
            }
        for (const auto& portal : view.portals)  // Interact's ids, as the mouse's hover names them (Cain's, 8, has no name)
            if (portal.which < int(d2d::game::World::kGamePortals)) things.push_back({ -2000 - portal.which, portal.x, portal.y });
        for (const auto& corpse : view.corpses) things.push_back({ -3000 - corpse.which, corpse.x, corpse.y });
        if (const int pick = pad::pick_target(hero_x, hero_y, face_x, face_y, things, -1, pad::Aim{ 4.5f, 4.5f, -0.2f }); pick >= 0)
            return { PadPick::kThing, things[std::size_t(pick)].id, things[std::size_t(pick)].x, things[std::size_t(pick)].y };
        return {};
    }

// The pad's buttons in the field: hit what is in reach in the facing, else
// pick up / talk to what is, else swing that way. Nothing is latched: a
// released button forgets its target. The aiming stick turns the facing
// (and so the target) without walking; else the walk does.
auto Town::pad_act(PadPlayer& player_pad, const UnitState& unit, bool dead, bool field, int left_skill, int right_skill, const Send& out, int left_item, int right_item) -> void {
        const auto& move = player_pad.move;
        if (move.aim || move.active) {
            const auto face = move.aim ? pad::world_step(move.aim_x, move.aim_y, kIsoW, kIsoH, 1.f) : pad::world_step(move.x, move.y, kIsoW, kIsoH, 1.f);
            player_pad.face_x = face.x; player_pad.face_y = face.y;
        }
        const bool want = move.attack || move.attack_down || move.alt;
        if (!want || dead || !field) { if (!want) player_pad.target = -1; return; }
        const float hero_x = unit.x, hero_y = unit.y, face_x = player_pad.face_x, face_y = player_pad.face_y;
        if (move.alt && right_skill == kResurrectSkill) {    // co-op: the body nearest, in reach
            if (const auto* body = body_near(hero_x, hero_y, 4.f)) out(cmd::UseSkill{ kResurrectSkill, body->x, body->y, body->id });
            return;
        }
        const bool left = !move.alt;
        const int skill = left ? left_skill : right_skill, item = left ? left_item : right_item;
        const PadPick pick = pad_pick(player_pad, unit, left && move.attack, pad_aim(skill, move.aim || move.active));
        if (pick.kind == PadPick::kMonster) {
            player_pad.target = pick.id;
            out(cmd::UseSkill{ skill, pick.x, pick.y, pick.id, left, item });
            return;
        }
        player_pad.target = -1;
        if (move.attack_down && !move.attack && !move.alt) return;   // held with nothing left to hit
        if (pick.kind == PadPick::kItem) { out(cmd::Pickup{ pick.id }); return; }
        if (pick.kind == PadPick::kThing) { out(cmd::Interact{ pick.id }); return; }
        // Nothing in reach: the left skill swings only with a monster about (in camp A never just walks); the right one always casts (buffs, teleport).
        const bool foe_near = std::ranges::any_of(view.monsters, [&](const auto& foe) { return foe.alive() && !foe.ally_not_interact(*scene) && std::hypot(foe.unit.x - hero_x, foe.unit.y - hero_y) < 10.f; });
        if (move.alt || (move.attack && foe_near))
            out(cmd::UseSkill{ skill, hero_x + face_x * 3.f, hero_y + face_y * 3.f, -1, left, item });
    }

auto Town::pad_walk(PadPlayer& player_pad, const UnitState& unit, bool dead, bool field, const Send& out) -> void {
        const bool walk = field && player_pad.move.active && !dead;
        if (walk) {
            const auto step = pad::world_step(player_pad.move.x, player_pad.move.y, kIsoW, kIsoH, 6.f);
            // Walk or run is the run toggle's (R, the left stick's click), as with the mouse.
            out(cmd::Move{ unit.x + step.x, unit.y + step.y, !player_pad.walking });
        } else if (player_pad.walking) {
            out(cmd::Move{ unit.x, unit.y, true });   // stick centred: stop here, not at the old destination
        }
        player_pad.walking = walk;
    }

auto Town::coop_seat(int seat, int colour, const pad::Sample& pad_now) -> void {
        if (!scene || std::ranges::contains(coop, seat, &CoopPlayer::seat)) return;
        std::vector<std::string> playing;
        for (const auto& in_game : world.players) playing.push_back(in_game->character.name);
        auto& waiting = coop.emplace_back();
        waiting.mapper = pad::Mapper{ pad_config };
        (void)waiting.mapper.step(pad_now, pad::Context::Menu, 0);
        waiting.seat = seat;
        waiting.colour = colour;
        waiting.roster = open_roster(*scene, playing, world.lead().character.header);
    }

auto Town::lead_leaves() -> int {
        // World::leave makes slot [1] the lead: the seat of whoever plays it, so their own pad keeps them.
        if (world.players.size() < 2) return -1;
        const auto next = std::ranges::find(coop, world.players[1]->id, &CoopPlayer::id);
        if (next == coop.end()) return -1;
        take_focus(hero.id);
        hand_items.erase(hero.id);
        if (scene && level) save_automap(automap, automap_dir(world), character.name, scene->map_seed, level->layer);
        if (const auto why = world.leave(hero.id); !why.empty()) d2d::log::warn("{}", why);
        // The next player's character is the screen's now (`hero` is slot [0], theirs).
        static_cast<Character&>(character) = hero.character;
        character.hardcore = hero.character.header.hardcore();
        character.selected = kSaveClassToUi[std::clamp(int(hero.character.header.cls), 0, 6)];
        skillbar.left = int(hero.character.header.left_skill); skillbar.right = int(hero.character.header.right_skill);
        load_hands(hero.id);
        skillbar_set = int(hero.character.header.weapon_set);
        const int seat = next->seat;
        lead_colour = next->colour;
        pad = next->pad;
        coop.erase(next);
        view_enc.reset();                             // the next View whole: another player's
        publish();
        return seat;
    }

// The level's objects' sounds as the client plays them (FUN_004cb460's object
// branch): the row a mode plays as an object enters it (none as it first
// shows), and its looping row while it's in that mode in sight (the 14 cells
// view_units draws: the unit is there), started again when it ends.
auto Town::object_sounds(const Audio& audio, std::uint32_t frame_ms, float sight_x, float sight_y) -> void {
        object_loops.clear();
        if (!level || !scene) return;
        if (object_level != level) { object_modes.assign(level->npcs.size(), {}); object_called.assign(level->npcs.size(), 0); object_level = level; }   // a new level: nothing has changed yet
        if (object_modes.size() != level->npcs.size()) { object_modes.resize(level->npcs.size()); object_called.resize(level->npcs.size()); }
        for (std::size_t i = 0; i < level->npcs.size() && i < view.npc_states.size(); ++i) {
            const auto& npc = level->npcs[i];
            const auto& state = view.npc_states[i];
            auto& was = object_modes[i];
            if (npc.root != "objects" || state.hidden || std::abs(state.x - sight_x) >= 14 || std::abs(state.y - sight_y) >= 14) { was = {}; continue; }
            const std::string_view mode = state.mode.empty() ? std::string_view("NU") : std::string_view(state.mode);
            const auto object_unit = d2d::game::unit_key(d2d::game::kSoundOnObject, int(i));
            if (!was.empty() && was != mode)
                cues.cue_on(object_unit, d2d::rules::object_sound(npc.object_id, d2d::game::mode_index(mode), d2d::game::mode_index(was)), frame_ms, state.x, state.y);
            was = mode;
            const auto subtile = [](float value) { return int(std::floor(value * 5)); };
            const int range = d2d::rules::unit_range(subtile(state.x) - subtile(sight_x), subtile(state.y) - subtile(sight_y), npc.size_x, 2);
            if (const int call = d2d::rules::object_call(npc.object_id, d2d::game::mode_index(mode), range, object_called[i] != 0); call > 0) {
                cues.cue_on(object_unit, call, frame_ms, state.x, state.y);
                object_called[i] = 1;
            }
            if (const int loop = d2d::rules::object_loop(npc.object_id, d2d::game::mode_index(mode)); loop > 0) {
                const int owner = -1 - int(i);
                object_loops.push_back(owner);
                if (!audio.owner_playing(owner) && !std::ranges::contains(cues.due, owner, &d2d::game::Cues::Cue::owner)) cues.cue_on(object_unit, loop, frame_ms, state.x, state.y, owner);
            }
        }
    }

auto Town::set_pad_config(const pad::Config& config) -> void {
        pad_config = config;
        lead_mapper = pad::Mapper{ config };
        for (auto& coop_player : coop) coop_player.mapper = pad::Mapper{ config };
    }

auto Town::coop_join(int seat, const Character& joining) -> void {
        auto found = std::ranges::find(coop, seat, &CoopPlayer::seat);
        if (found == coop.end()) { coop.emplace_back().seat = seat; found = coop.end() - 1; found->mapper = pad::Mapper{ pad_config }; }
        found->id = world.join(joining).id;
        found->roster.reset();
    }

auto Town::coop_text(std::string_view text, bool backspace) -> void {
        for (auto& coop_player : coop)
            if (coop_player.roster && roster_typing(*coop_player.roster)) { roster_text(*coop_player.roster, text, backspace); return; }
    }

auto Town::coop_typing() const -> bool {
        return std::ranges::any_of(coop, [](const CoopPlayer& coop_player) { return coop_player.roster && roster_typing(*coop_player.roster); });
    }

auto Town::coop_enter() -> void {
        if (!scene) return;
        for (auto& coop_player : coop)
            if (coop_player.roster && roster_typing(*coop_player.roster)) {
                if (const auto choice = roster_input(*coop_player.roster, pad::Actions{ .keys = { pad::Key::Return } }, *scene)) (void)coop_choose(coop_player.seat, *choice);
                return;
            }
    }

auto Town::coop_leave(int seat) -> void {
        const auto found = std::ranges::find(coop, seat, &CoopPlayer::seat);
        if (found == coop.end()) return;
        if (found->id == focus_id()) take_focus(hero.id);   // their screen goes back first
        if (found->id > 0)                              // still choosing: no one in the game to leave
            if (const auto why = world.leave(found->id); !why.empty()) d2d::log::warn("co-op seat {}: {}", seat, why);
        coop.erase(found);
    }

auto Town::coop_pad(int seat, const pad::Sample& sample, float seconds) -> void {
        const auto found = std::ranges::find(coop, seat, &CoopPlayer::seat);
        if (found == coop.end()) return;
        auto& coop_player = *found;
        if (coop_player.roster) {                       // still choosing: the pad plays the roster
            const auto actions = coop_player.mapper.step(sample, pad::Context::Menu, seconds);
            if (const auto choice = scene ? roster_input(*coop_player.roster, actions, *scene) : std::nullopt) (void)coop_choose(seat, *choice);
            return;
        }
        const auto in_world = std::ranges::find(world.players, coop_player.id, [](const auto& each) { return each->id; });
        if (in_world == world.players.end() || coop_player.id == focus_id()) return;   // the screen's: main's pad pipeline plays it
        const auto opens = field_play(coop_player.pad, coop_player.mapper, **in_world, sample, seconds,
                                      [&](const Command& command) { coop_player.orders.push_back(command); });
        // A panel, the game menu or the skill picker: the screen is theirs, when no one else has it.
        if (opens.empty() || !focus_free()) return;
        take_focus(coop_player.id);
        for (const pad::Key key : opens)                // the keys that opened it, for this frame's UI
            if (key == pad::Key::Up) pad_open_picker(); // field_play's stand-in for the picker
            else focus_keys.push_back(pad_keycode(key));
    }

// A seat's roster choice made (in the game or the lobby): a save joins, a
// new character is made and saved at once (as char-create's OK). False
// for a name a player in the game or a save on disk has (char-create's).
auto Town::coop_choose(int seat, const RosterChoice& choice) -> bool {
        if (!scene) return false;
        const auto& lead = world.lead().character.header;
        if (choice.save >= 0) {
            coop_join(seat, character_of_save(*scene, std::size_t(choice.save)));
            return true;
        }
        const auto lower = [](char letter) { return std::tolower(static_cast<unsigned char>(letter)); };
        const auto same = [&](const std::string& name) { return std::ranges::equal(name, choice.name, {}, lower, lower); };
        if (std::ranges::any_of(world.players, [&](const auto& each) { return same(each->character.name); })
            || (world.characters && std::filesystem::exists(world.characters->path(choice.name)))) return false;
        coop_join(seat, made_character(*scene, choice.cls, choice.name, lead.hardcore(), lead.expansion(), rng));
        auto& made = *world.players.back();
        world.each_player([&](World::Player& each) {
            if (&each != &made) return;
            if (const auto why = world.save(); !why.empty()) d2d::log::warn("{}: {}", made.character.name, why);
        });
        return true;
    }

auto Town::lead_pad(const pad::Sample& sample, float seconds) -> void {
        (void)field_play(lead_field, lead_mapper, hero, sample, seconds, [&](const Command& command) { lead_orders.push_back(command); });
    }

auto Town::field_play(PadPlayer& player_pad, pad::Mapper& mapper, const World::Player& hero_there, const pad::Sample& sample, float seconds,
                      const Send& out) -> std::vector<pad::Key> {
        const auto actions = mapper.step(sample, pad::Context::Game, seconds);
        player_pad.move = { actions.move_x, actions.move_y, actions.move_active, actions.click_press, actions.click_down, actions.rclick_press,
                            actions.aim_active, actions.aim_x, actions.aim_y };
        std::vector<pad::Key> opens;
        for (const pad::Key key : actions.keys)
            switch (key) {
                case pad::Key::ToggleRun:   out(cmd::Run{ !hero_there.running }); break;
                case pad::Key::Belt1:       out(cmd::UseBelt{ 0 }); break;
                case pad::Key::Belt2:       out(cmd::UseBelt{ 1 }); break;
                case pad::Key::Belt3:       out(cmd::UseBelt{ 2 }); break;
                case pad::Key::Belt4:       out(cmd::UseBelt{ 3 }); break;
                case pad::Key::SwapWeapons: out(cmd::SwapWeapons{}); break;
                case pad::Key::Inventory: case pad::Key::Character: case pad::Key::SkillTree: case pad::Key::QuestLog:
                case pad::Key::Automap: case pad::Key::Escape:
                    opens.push_back(key); break;
                default: break;
            }
        if (actions.skill_picker) opens.push_back(pad::Key::Up);          // the right stick's click: the picker
        const bool dead = hero_there.pmode == game::kModeDT || hero_there.pmode == game::kModeDD;
        const bool field = !game_menu.open;
        pad_walk(player_pad, hero_there.player, dead && !hero_there.ghost, field, out);   // a ghost walks
        // Its hands' charged entries (kept while another player has the
        // screen); one gone since is Attack / 0, as the skillbar's drop_gone.
        auto& hands = hand_items[hero_there.id];
        int left_skill = int(hero_there.character.header.left_skill), right_skill = int(hero_there.character.header.right_skill);
        const auto entries = d2d::game::charged_skills(hero_there.character.items);
        const auto gone = [&](int skill, int item) { return item >= 0 && std::ranges::none_of(entries, [&](const d2d::game::ChargedSkill& had) { return had.item == item && had.skill == skill; }); };
        if (gone(left_skill, hands.left)) { left_skill = 0; hands.left = -1; }
        if (gone(right_skill, hands.right)) { right_skill = 0; hands.right = -1; }
        pad_act(player_pad, hero_there.player, dead, field, left_skill, right_skill, out, hands.left, hands.right);
        return opens;
    }

auto Town::focus_id() const -> int { return focus > 0 ? focus : hero.id; }

auto Town::body_near(float x, float y, float reach) const -> const View::Body* {
        const View::Body* best = nullptr;
        for (const auto& body : view.bodies)
            if (const float apart = std::hypot(body.x - x, body.y - y); apart <= reach && (!best || apart < std::hypot(best->x - x, best->y - y))) best = &body;
        return best;
    }

auto Town::focused() -> World::Player& {
        const auto found = std::ranges::find(world.players, focus_id(), [](const auto& each) { return each->id; });
        return found == world.players.end() ? hero : **found;
    }

auto Town::focus_free() const -> bool { return focus_id() == hero.id && pad_field() && !game_menu.open && !automap.open; }

auto Town::focus_seat() const -> int {
        const auto found = std::ranges::find(coop, focus_id(), &CoopPlayer::id);
        return found == coop.end() ? 0 : found->seat;
    }

auto Town::focus_colour() const -> int {
        const auto found = std::ranges::find(coop, focus_id(), &CoopPlayer::id);
        return found == coop.end() ? lead_colour : found->colour;
    }

auto Town::load_hands(int id) -> void {
        const auto kept = hand_items.contains(id) ? hand_items.at(id) : HandItems{};
        skillbar.left_item = kept.left; skillbar.right_item = kept.right; skillbar.hotkey_items = kept.hotkeys;
    }

auto Town::take_focus(int id) -> void {
        if (id == focus_id()) return;
        hand_items[focus_id()] = { skillbar.left_item, skillbar.right_item, skillbar.hotkey_items };   // the last owner's charged entries, kept
        // Whatever the last owner had open shuts (it's theirs, not the next one's).
        inv_open = char_open = stash_open = cube_open = tree_open = belt_open = false;
        quest_log.open = false; automap.open = false; skillbar.picking = 0;
        npc_menu = {}; speech = {}; waypoint = {}; store = {}; menu_after_speech = -1;
        focus = id == hero.id ? 0 : id;
        auto& owner = focused();
        static_cast<Character&>(character) = owner.character;
        character.hardcore = owner.character.header.hardcore();
        character.selected = kSaveClassToUi[std::clamp(int(owner.character.header.cls), 0, 6)];
        skillbar.left = int(owner.character.header.left_skill); skillbar.right = int(owner.character.header.right_skill);
        load_hands(id);
        skillbar_set = int(owner.character.header.weapon_set);
        quest_log.sent_known = false;
        pad = {};
        view_enc.reset();                             // the next View whole: another player's
        publish();
        d2d::log::info("co-op: the screen is {}'s", owner.character.name);
    }

auto Town::pad_focus() const -> std::optional<PadFocus> {
        if (controls.open) {                                            // Configure Controls: the selected row's column
            const auto [x, y] = controls.focus();
            return PadFocus{ 9000 + controls.sel * 2 + controls.column + (controls.pad_page ? 1000 : 0), x, y };
        }
        if (skillbar.picking) {                                         // the hand's skill now, else its button
            const bool on_left = skillbar.picking == 1;
            const auto rows = skillbar.rows(on_left);
            for (std::size_t row = 0; row < rows.size(); ++row)
                for (std::size_t col = 0; col < rows[row].size(); ++col)
                    if (rows[row][col] == skillbar.hand(on_left)) {
                        const auto [x, y] = skillbar.picker_at(rows, on_left, int(row), int(col));
                        return PadFocus{ 3000 + skillbar.picking, x + SkillBar::kIcon / 2, y - SkillBar::kIcon / 2 };
                    }
            return PadFocus{ 3000 + skillbar.picking, (on_left ? SkillBar::kButtonLeftX : SkillBar::kButtonRightX) + SkillBar::kIcon / 2, int(kScreenHeight) - SkillBar::kIcon / 2 };
        }
        if (game_menu.open) {                                           // its highlight: the d-pad steps it by key, the cursor follows
            const int count = int(game_menu.rows().size());
            return PadFocus{ 1000 + game_menu.menu * 64 + game_menu.sel, int(kScreenWidth) / 2, game_menu.top() + game_menu.step() * game_menu.sel + count + game_menu.step() / 2 };
        }
        if (waypoint.open) return PadFocus{ 4000 + waypoint.tab, kCharPanelX + 17 + 140, 60 + kWpHitTop[0] + 15 };   // the tab's first row
        if (npc_menu.npc >= 0) {
            int base = npc_menu.y;
            for (const auto& line : npc_menu.lines) {                   // the topmost line you can choose
                base += line.height;
                if (!line.header) return PadFocus{ 2000 + npc_menu.npc * 64 + int(npc_menu.lines.size()), npc_menu.x + npc_menu.box_width / 2, base - line.height / 2 };
            }
        }
        return std::nullopt;
    }

// Where a panel's d-pad hops the cursor: each item and free cell of the open
// grids, the body slots, and a store's stock (with an item held, the stock
// grid's middle: dropping it there sells it).
auto Town::pad_snap_points() const -> std::vector<pad::Point> {
        std::vector<pad::Point> points;
        if (!scene) return points;
        const auto centre = [&](int x, int y, int width, int height) { points.push_back({ x + width / 2, y + height / 2 }); };
        const auto& inv = scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
        std::vector<std::pair<const Scene::InvLayout*, int>> grids;
        if (inv_open) grids.emplace_back(&inv, d2d::d2s::item_panel::kInventory);
        if (cube_open) grids.emplace_back(&scene->cube_layout, d2d::d2s::item_panel::kCube);
        else if (stash_open) grids.emplace_back(&scene->stash_layout[character.expansion ? 1 : 0], d2d::d2s::item_panel::kStash);
        for (const auto& [layout, panel] : grids) {
            std::vector<bool> taken(std::size_t(layout->cols * layout->rows));
            for (const auto& item : character.items) {
                if (item.location != d2d::d2s::item_location::kStored || item.panel != panel) continue;
                const auto rect = grid_rect(*scene, *layout, item);
                centre(rect[0], rect[1], rect[2], rect[3]);
                const auto [width, height] = d2d::rules::item_size(scene->rules, item.code);
                for (int row = item.row; row < item.row + height && row < layout->rows; ++row)
                    for (int col = item.column; col < item.column + width && col < layout->cols; ++col) taken[std::size_t(row * layout->cols + col)] = true;
            }
            for (int row = 0; row < layout->rows; ++row)
                for (int col = 0; col < layout->cols; ++col)
                    if (!taken[std::size_t(row * layout->cols + col)])
                        centre(layout->grid_x + col * layout->box_w, layout->grid_y + row * layout->box_h, layout->box_w, layout->box_h);
        }
        if (inv_open)
            for (int slot = d2d::d2s::body_location::kFirst; slot <= d2d::d2s::body_location::kLast; ++slot)
                if (const auto& rect = inv.slots[std::size_t(slot)]; rect[2] > 0) centre(rect[0], rect[1], rect[2], rect[3]);
        if (skillbar.picking) {                                         // the picker's icons and both hands' buttons
            const bool on_left = skillbar.picking == 1;
            const auto rows = skillbar.rows(on_left);
            for (std::size_t row = 0; row < rows.size(); ++row)
                for (std::size_t col = 0; col < rows[row].size(); ++col) {
                    const auto [x, y] = skillbar.picker_at(rows, on_left, int(row), int(col));
                    centre(x, y - SkillBar::kIcon, SkillBar::kIcon, SkillBar::kIcon);
                }
            for (const int x : { SkillBar::kButtonLeftX, SkillBar::kButtonRightX }) centre(x, int(kScreenHeight) - SkillBar::kIcon, SkillBar::kIcon, SkillBar::kIcon);
        }
        if (tree_open) {                                                // the tab's skills and the three tabs
            const auto& list = scene->rules.class_skills[std::size_t(std::max(character.character_class, 0))];
            for (const auto& class_skill : list)
                if (class_skill.page == tree_tab)
                    if (const auto [x, y] = skill_icon_at(class_skill); x > -1000) centre(x, y - 0x30, 0x30, 0x30);
            for (const int top : { kTreeB - 0x174, kTreeB - 0x108, kTreeB - 0x9c }) centre(kTreeR - 0x58, top, 0x58, 0x6b);
        }
        if (char_open && character.stats.get(d2d::d2s::kStatPts) > 0)  // the stat buttons
            for (const auto& button : kStatButtons) centre(kCharPanelX + button.x, kCharPanelY + button.y - 22, 40, 22);
        if (quest_log.open) {                                           // the acts' tabs, the quests, Close and the last quest
            for (int act = 0; act < (character.expansion ? 5 : 4); ++act) centre(kCharPanelX + act * (character.expansion ? 0x40 : 0x50), kCharPanelY, character.expansion ? 0x40 : 0x50, 0x1c);
            for (const auto& slot : kQuestSlot) centre(kCharPanelX + slot.hit_x, kCharPanelY + slot.hit_y, 0x38, 0x34);
            centre(kCharPanelX + 0x116, kCharPanelY + 0x188, 0x24, 0x22);
            centre(kCharPanelX + 0xe6, kCharPanelY + 0x187, 0x1e, 0x21);
        }
        if (waypoint.open) {                                            // the open acts' tabs, the activated rows, Cancel
            for (int act = 0; act < (character.expansion ? 5 : 4); ++act)
                if (waypoint_act_open(character.header, act)) centre(kCharPanelX + act * (character.expansion ? 64 : 80), 60, character.expansion ? 64 : 80, 30);
            const auto& rows = scene->waypoint_levels[std::size_t(waypoint.tab)];
            for (std::size_t i = 0; i < rows.size() && i < 9; ++i)
                if (character.header.waypoint(character.header.active_difficulty(), rows[i].waypoint)) centre(kCharPanelX + 17, 60 + kWpHitTop[i], 280, 30);
            centre(kCharPanelX + 0x111, 60 + 0x183, 0x24, 0x22);
        }
        if (net_game && net_game->trade.state != 0)                     // a trade's buttons, gold and our offer's cells
            for (const auto& box : trade_boxes(*scene, net_game->trade.state)) centre(box[0], box[1], box[2] - box[0], box[3] - box[1]);
        if (belt_open) {                                                // the belt's boxes
            const auto& belt = scene->belts[std::size_t(belt_index(*scene, character.items))];
            for (int box = 0; box < belt.boxes; ++box) {
                const auto& edges = belt.box[std::size_t(box)];
                centre(edges[0], edges[2], edges[1] - edges[0], edges[3] - edges[2]);
            }
        }
        if (npc_menu.npc >= 0 && store.npc < 0) {                      // the lines you can choose
            int base = npc_menu.y;
            for (const auto& line : npc_menu.lines) {
                base += line.height;
                if (!line.header) points.push_back({ npc_menu.x + npc_menu.box_width / 2, base - line.height / 2 });
            }
        }
        if (store.npc >= 0) {
            if (held) centre(96, 123, 10 * 29, 10 * 29);
            else
                for (const auto& stock : store.tabs[std::size_t(store.tab)]) {
                    const auto [width, height] = d2d::rules::item_size(scene->rules, stock.code);
                    centre(96 + stock.column * 29, 123 + stock.row * 29, width * 29, height * 29);
                }
        }
        return points;
    }

// A store's X: the stock under the cursor bought, the inventory item under
// it sold, or the one held sold (as a drop on the stock grid).
auto Town::pad_trade(int mouse_x, int mouse_y) -> void {
        if (store.npc < 0 || !scene) return;
        if (held) { net.send(cmd::Sell{ held->id }); return; }
        if (const int stock = store_item_at(*scene, store, mouse_x, mouse_y); stock >= 0) { send(cmd::Buy{ stock, store.tab }); return; }
        const auto& inv = scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
        for (const auto& item : character.items) {
            if (item.location != d2d::d2s::item_location::kStored || item.panel != d2d::d2s::item_panel::kInventory) continue;
            const auto rect = grid_rect(*scene, inv, item);
            if (mouse_x >= rect[0] && mouse_x < rect[0] + rect[2] && mouse_y >= rect[1] && mouse_y < rect[1] + rect[3]) { send(cmd::Sell{ item.id }); return; }
        }
    }

auto Town::tip_env() const -> d2d::rules::CalcEnv {
        d2d::rules::CalcEnv env{ [this](int id) { return d2d::game::skill_base_level(*scene, character, id); },
                                 [this](int id) { return skillbar.level(id); },   // with the skill shrine's +all skills
                                 {}, int(character.stats.get(d2d::d2s::kLevel)), nullptr };
        // The stats the calcs and masteries read: the gear's and the passives' (no weapon-type ones).
        auto sum = std::make_shared<d2d::rules::StatSum>();
        for (const auto& prop : d2d::game::gear_props(*scene, character.items))
            if (prop.stat >= 0 && std::size_t(prop.stat) < sum->size()) (*sum)[std::size_t(prop.stat)] += prop.value;
        for (const auto& passive : d2d::rules::passive_stats(scene->skills, env))
            if (passive.itype.empty() && passive.stat >= 0 && std::size_t(passive.stat) < sum->size()) (*sum)[std::size_t(passive.stat)] += passive.value;
        env.stat = [stat_sum = std::move(sum)](int id) { return id >= 0 && std::size_t(id) < stat_sum->size() ? int((*stat_sum)[std::size_t(id)]) : 0; };
        return env;
    }

auto Town::pad_back(int mouse_x, int mouse_y) const -> bool {
        if (gold_box) return true;                                   // the gold box: B is its Cancel
        if (!scene || held || !(stash_open || cube_open)) return false;
        const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                            { inv_open, stash_open, cube_open, belt_open, character.expansion }, mouse_x, mouse_y);
        return !(click.cmd && std::holds_alternative<cmd::ToCursor>(*click.cmd));
    }

// The field's right stick click: the right hand's skill picker (A on the
// left skill button switches to the left's), the cursor on its skill.
auto Town::pad_open_picker() -> void {
        if (pad_field()) skillbar.picking = 2;
    }

// A store's shoulders: the next tab with stock on it, that way round.
auto Town::pad_tab(int step) -> void {
        for (int i = 1; i < 4; ++i)
            if (const int tab = ((store.tab + step * i) % 4 + 4) % 4; !store.tabs[std::size_t(tab)].empty()) { store.tab = tab; return; }
    }

auto Town::walk(const Mouse& mouse, bool over_ui, Audio& audio, std::uint32_t frame_ms, std::uint32_t last_ms) -> void {
        frame_now = frame_ms;
        if (net_game) {
            net_game->pump(frame_ms, frame_ms - last_ms);
            // The host moved us (a correction, later warps and waypoints): there we are.
            // Only a spot on d2d's level: a warp's or waypoint's lands as d2d
            // takes it too (the same exit spot, FUN_005550b0).
            world.host_warps = true;
            if (net_game->take_reassign() && level) {
                net_moving_until = 0;
                rain.reassign(rain.rng, level->id, d2d::rules::level_ids::act_of(level->id));
                const float to_x = (net_game->self_x + 0.5f) / 5.f - float(level->world_x), to_y = (net_game->self_y + 0.5f) / 5.f - float(level->world_y);
                const auto inside = [](const Level& where, float cell_x, float cell_y) {
                    return cell_x >= 0 && cell_y >= 0 && cell_x < float(where.ds1.width()) && cell_y < float(where.ds1.height());
                };
                if (!inside(*level, to_x, to_y))
                    d2d::log::info("net: the host put us at {}, {}, off level {} (the warp asked for: {})", net_game->self_x, net_game->self_y, level->id, net_warp_to);
                if (inside(*level, to_x, to_y)) {
                    hero.player.x = to_x;
                    hero.player.y = to_y;
                    hero.player.walking = false;
                } else if (const Level* landed = [&]() -> const Level* {
                               // The warp asked for, else the last 0x03's area, else the
                               // act's level that holds the spot (a quest's door: Duriel's lair).
                               for (const int id : { net_warp_to, net_game->area })
                                   if (const Level* asked = id >= 0 ? world.game_data->level(id) : nullptr; asked && asked->ds1.width() > 0
                                       && inside(*asked, (net_game->self_x + 0.5f) / 5.f - float(asked->world_x), (net_game->self_y + 0.5f) / 5.f - float(asked->world_y))) return asked;
                               for (int id = 1; id < 137; ++id)
                                   if (d2d::rules::level_ids::act_of(id) == d2d::rules::level_ids::act_of(level->id))
                                       if (const Level* holder = world.game_data->level(id); holder && holder->ds1.width() > 0
                                           && inside(*holder, (net_game->self_x + 0.5f) / 5.f - float(holder->world_x), (net_game->self_y + 0.5f) / 5.f - float(holder->world_y))) return holder;
                               return nullptr;
                           }(); landed) {
                    // The host took the warp we asked for: there with it; else
                    // where its last 0x03 said (a caravan, a boat: a new act).
                    const float there_x = (net_game->self_x + 0.5f) / 5.f - float(landed->world_x), there_y = (net_game->self_y + 0.5f) / 5.f - float(landed->world_y);
                    if (inside(*landed, there_x, there_y)) world.arrive(landed, there_x, there_y, "the host's warp");
                    net_warp_to = -1;
                    level = world.level;
                }
            }
            // The host's player is the truth: d2d's walked on where the host's
            // couldn't (a door only d2d opened, a wall the host's path won't
            // take); 2 s that far apart and d2d goes back to where the host has us.
            // Counted from when d2d stops: mid-walk the host's place (0x95 / 0x96,
            // about every 2 s) lags 30+ subtiles behind (live 2026-10-25).
            if (level && net_game->host_x > 0) {
                const float drift = std::hypot((hero.player.x + float(level->world_x)) * 5.f - net_game->host_x, (hero.player.y + float(level->world_y)) * 5.f - net_game->host_y);
                const float back_x = net_game->host_x / 5.f - float(level->world_x), back_y = net_game->host_y / 5.f - float(level->world_y);
                if (hero.player.walking || drift < 15.f || back_x < 0 || back_y < 0 || back_x >= float(level->ds1.width()) || back_y >= float(level->ds1.height())) net_drift_ms = 0;
                else if (net_drift_ms == 0) net_drift_ms = frame_ms;
                else if (frame_ms - net_drift_ms > 2000) {
                    d2d::log::info("net: {:.0f} subtiles from the host's player; back to it", drift);
                    hero.player.x = back_x; hero.player.y = back_y; hero.player.walking = false;
                    net_drift_ms = 0;
                }
            }
            // The host killed us: d2d's death plays (DT, DD, the corpse); a
            // click then sends 0x41 and respawns here too.
            if (net_game->take_death() && !world.fight.dead()) world.fight.die(frame_ms);
            // The host's quest log news (0x5d): the Quest Log button, as the
            // single-player path raises it on a log change (FUN_004a2cb0).
            // Our quest words as the host has them (0x28): the quest log
            // and the NPCs' talk read these.
            if (net_game->quest_words) {
                hero.character.header.quests[std::size_t(std::clamp(hero.character.header.active_difficulty(), 0, 2))] = *net_game->quest_words;
                net_game->quest_words.reset();
            }
            for (const int quest : net_game->quest_news) quest_log_notify(quest_log, quest);
            net_game->quest_news.clear();
            for (const auto kind : std::exchange(net_game->quest_quakes, {}))
                if (kind != d2d::rules::QuestQuake::diablo || level->id == d2d::rules::level_ids::kChaosSanctuary)
                    quest_quakes.push_back({ kind, frame_ms });
            if (net_game->movie >= 0) world.movie = std::exchange(net_game->movie, -1);
            // Our stats as the host sets them: attributes, points, life / mana /
            // stamina and their maxima (8.8 fixed point, as in a save), level,
            // experience, gold. The host's word wins over d2d's own regen.
            // The maxima (7, 9, 11) are the character's own, as a save keeps
            // them (Miriam: 714 / 409 / 423): our items' and Battle Orders'
            // shares stay on top (Fight::own_stats).
            for (const auto& change : net_game->stat_changes) {
                if (change.id < 0 || change.id > 15) continue;
                auto& value = hero.character.stats.values[std::size_t(change.id)];
                value = change.add ? value + change.value : change.value;
                if (!change.add && (change.id == d2d::d2s::kMaxLife || change.id == d2d::d2s::kMaxMana || change.id == d2d::d2s::kMaxStamina)) {
                    const auto which = std::size_t(change.id - d2d::d2s::kMaxLife) / 2;   // life, mana, stamina
                    value += hero.item_max[which] + std::array<std::int64_t, 3>{ hero.bo_life, hero.bo_mana, 0 }[which];
                }
                if (std::getenv("D2D_NET_STATS")) d2d::log::info("net stat {} = {}", change.id, value);
            }
            net_game->stat_changes.clear();
            // What the host put in our bags (a pick-up, a buy): where the host put
            // it, so it keeps matching the host's item.
            for (auto& item : net_game->picked) {
                item.id = world.next_item_id++;
                hero.character.items.push_back(std::move(item));
                world.cues.cue("item_pickup", 0, hero.player.x, hero.player.y);
            }
            net_game->picked.clear();
            // A trade's end: the host sends our items again (a cancel, all
            // of them, new ids; live 2026-10-05): one item a spot, the last.
            if (net_game->trade_settling()) {
                auto& items = hero.character.items;
                for (std::size_t later = items.size(); later-- > 0;)
                    for (std::size_t earlier = 0; earlier < later; ++earlier)
                        if (items[earlier].location == items[later].location && items[earlier].panel == items[later].panel && items[earlier].column == items[later].column
                            && items[earlier].row == items[later].row && items[earlier].slot == items[later].slot && items[earlier].location != d2d::d2s::item_location::kEquipped) {
                            items.erase(items.begin() + std::ptrdiff_t(earlier));
                            --later;
                            break;
                        }
            }
            // The open store shows the host's stock (its ids, for 0x32), by tab.
            if (hero.store.npc >= 0 && net_game->trade_npc && net_game->store_items.size() != net_store_shown) {
                net_store_shown = net_game->store_items.size();
                for (auto& tab : hero.store.tabs) tab.clear();
                for (const auto& [id, stocked] : net_game->store_items) {
                    auto item = stocked;
                    item.id = int(id);
                    hero.store.tabs[std::size_t(std::clamp(item.panel - 1, 0, 3))].push_back(std::move(item));   // page 2 weapons, 4 misc (Akara's, recorded)
                }
            }
            // Once, a while in: how many of our items the host's match (the net
            // log says which don't).
            if (!net_items_checked && net_game->steady_now() > 3000 && !net_game->own_items.empty()) {
                net_items_checked = true;
                int matched = 0;
                for (const auto& item : hero.character.items) {
                    if (net_game->host_item(item)) ++matched;
                    else net_game->log.note("our " + item.code + " (location " + std::to_string(item.location) + ", panel " + std::to_string(item.panel) + " at "
                                            + std::to_string(item.column) + "," + std::to_string(item.row) + ", slot " + std::to_string(item.slot) + ") has no host item");
                }
                d2d::log::info("net: {} of our {} items match the host's ({} it has)", matched, hero.character.items.size(), net_game->own_items.size());
            }
            // d2d's walks of its own (up to an NPC, an object, an item, a melee
            // target): the host's player follows where ours is, every 300 ms
            // (a target the host can't stand on, an NPC's spot, it'd refuse).
            // Not while a warp or portal is being taken: ours may already
            // be in the next area, the host's isn't yet. Nor on a click's walk: the
            // host has its target, and our spot sent then (live 2026-10-25: 40 ms
            // after the click, before ours moved) stopped the host's where it stood.
            const bool area_change = (net_operate != 0 && net_operate_moves) || frame_ms < net_moving_until || net_game->holding();
            if (!hero.player.walking && net_game->steady_now() - net_clicked_ms > 500) net_clicked_walk = false;
            if (hero.player.walking && !net_clicked_walk && frame_ms - net_follow_ms >= 300 && !area_change) {
                net_follow_ms = frame_ms;
                net_game->move_to((hero.player.x + float(level->world_x)) * 5.f, (hero.player.y + float(level->world_y)) * 5.f, view.running);
            }
            // A trade walked to: asked for once the host has us within 6.
            if (net_trade_pending >= 0) {
                const auto vendor = net_game->units.find(std::uint64_t(1) << 32 | std::uint32_t(net_trade_pending));
                if (vendor == net_game->units.end() || frame_ms - net_trade_ms > 8000) {
                    net_game->log.note("the trade's NPC couldn't be reached on the host");
                    net_trade_pending = -1;
                    net_trade_asked = false;
                } else if (const float apart = std::hypot(net_game->host_x - vendor->second.x, net_game->host_y - vendor->second.y); apart > 6.f) {
                    if (frame_ms - net_follow_ms >= 500) {                      // she walks: after her, 2 out
                        net_follow_ms = frame_ms;
                        net_game->move_to(vendor->second.x + (net_game->host_x - vendor->second.x) / apart * 2.f,
                                          vendor->second.y + (net_game->host_y - vendor->second.y) / apart * 2.f, view.running);
                    }
                } else if (!net_trade_asked && net_game->steady_now() - net_game->walked_ms >= 500) {
                    // A walking player is busy: the host drops all but chat,
                    // skill picks, 0x43, 0x66 (FUN_0054d750, FUN_0057eec0). As a
                    // real client (recorded through d2proxy): where we see the
                    // NPC (0x59), then 0x13; the host answers with NPC info.
                    namespace c2s = d2d::net::d2gs::c2s;
                    net_game->npc_info = 0;
                    net_game->send_items({ c2s::unit_position(1, std::uint32_t(net_trade_pending), std::uint32_t(vendor->second.x), std::uint32_t(vendor->second.y)),
                                           c2s::interact(1, std::uint32_t(net_trade_pending)) });
                    net_trade_asked = true;
                } else if (net_trade_asked && net_game->npc_info == std::uint32_t(net_trade_pending)) {
                    // Talking: start the chat, then the trade (1), gamble (2: unverified) or travel (0).
                    namespace c2s = d2d::net::d2gs::c2s;
                    const auto host_npc = std::uint32_t(net_trade_pending);
                    if (net_trade_kind > 0) net_game->trade_npc = host_npc;
                    if (net_chat_npc != host_npc || net_trade_kind < 0) net_game->send_items({ c2s::npc_chat(true, 1, host_npc) });   // a talk: again, its first came before the 0x13 landed
                    net_chat_npc = host_npc;
                    if (net_trade_kind >= 0) net_game->send_items({ c2s::npc_action(std::uint32_t(net_trade_kind), host_npc, std::uint32_t(net_trade_extra)) });
                    net_trade_pending = -1;
                    net_trade_asked = false;
                }
            }
            // An object operated here: the host operates it only near
            // (FUN_00548b00 → FUN_00584540, range < 0x33) and drops a walking
            // player's 0x13 (FUN_0054d750), so it goes once the host has us
            // stopped close by; until then the host's player walks there.
            if (net_operate != 0) {
                const auto object = net_game->units.find(std::uint64_t(net_operate_type) << 32 | net_operate);
                // A warp: to its mouth (the host takes 0x13 within 0x33 and walks
                // the rest, FUN_00548b00 case 5; under 5 it warps).
                const bool warp = net_operate_type == 5 || net_operate_waypoint >= 0;   // a waypoint too: its object fills its tile
                const float spot_x = warp ? net_warp_x : object == net_game->units.end() ? 0.f : object->second.x;
                const float spot_y = warp ? net_warp_y : object == net_game->units.end() ? 0.f : object->second.y;
                if (object == net_game->units.end() || frame_ms - net_operate_ms > (warp ? 15000u : 8000u)) {
                    net_game->log.note("object " + std::to_string(net_operate) + " couldn't be reached on the host");
                    net_operate = 0;
                    net_warp_to = -1;
                } else if (std::hypot(net_game->host_x - spot_x, net_game->host_y - spot_y) > (warp ? 2.f : 4.f)) {
                    if (frame_ms - net_follow_ms >= 500) {
                        net_follow_ms = frame_ms;
                        net_game->move_to(spot_x, spot_y, view.running);
                    }
                } else if (net_game->steady_now() - net_game->walked_ms >= 500) {
                    if (net_operate_type == 0 && net_game->units.contains(net_operate) && net_game->corpses.end() == std::ranges::find(net_game->corpses, net_operate))
                        net_game->trade_request(net_operate);            // a player, not our corpse: a trade
                    else net_game->interact(net_operate_type, net_operate);
                    if (net_operate_waypoint >= 0) net_game->waypoint(net_operate, net_operate_waypoint);   // in reach and 10 s on from a level change, it goes at once
                    if (net_operate_waypoint >= 0) { net_waypoint_object = net_operate; net_waypoint_level = net_operate_waypoint; net_waypoint_ms = frame_ms; net_waypoint_tries = 0; net_game->waypoint_menu = 0; }
                    net_operate = 0;
                    net_operate_waypoint = -1;
                    if (net_operate_moves) net_moving_until = frame_ms + 5000;
                }
            }
            // Until the host moves us: its menu open (0x63) takes the level
            // (0x49); out of reach it walks us up and stops, so both again
            // (live 2026-10-18: a 0x49 before the menu, or within 10 s of a
            // level change, is dropped).
            if (net_waypoint_level >= 0) {
                if (net_warp_to < 0) {                               // there: the reassign took us
                    net_waypoint_level = -1;
                } else if (net_game->waypoint_menu == net_waypoint_object) {
                    net_game->waypoint(net_waypoint_object, net_waypoint_level);   // pending till the reassign: the retries below if it's dropped
                    net_game->waypoint_menu = 0;
                    net_waypoint_ms = frame_ms;
                } else if (frame_ms - net_waypoint_ms > 3000 && net_waypoint_tries < 3 && net_game->steady_now() - net_game->walked_ms >= 500) {
                    // Out of its reach the host walks us up (FUN_00548a50) and
                    // stops: again from there.
                    net_game->interact(2, net_waypoint_object);
                    net_game->waypoint(net_waypoint_object, net_waypoint_level);
                    net_waypoint_ms = frame_ms;
                    ++net_waypoint_tries;
                } else if (frame_ms - net_waypoint_ms > 3500) {
                    net_game->log.note("the host didn't open waypoint " + std::to_string(net_waypoint_object));
                    net_waypoint_level = -1;
                    net_warp_to = -1;
                }
            }
            // The host's doors (0x0e: opened by anyone, a monster too): d2d's
            // door nearest the host's object, to its mode and footprint.
            for (const auto& [id, mode] : net_game->object_modes) {
                const auto object = net_game->units.find(std::uint64_t(2) << 32 | id);
                if (object == net_game->units.end() || mode < 0 || mode > 2) continue;
                int door = -1;
                float best = 4.f;
                for (std::size_t i = 0; i < level->npcs.size(); ++i) {
                    const auto& npc = level->npcs[i];
                    if (npc.root != "objects" || !World::is_door(npc.operate_fn)) continue;
                    if (const float apart = std::hypot((npc.x + float(level->world_x)) * 5.f - object->second.x, (npc.y + float(level->world_y)) * 5.f - object->second.y); apart < best) { best = apart; door = int(i); }
                }
                if (door >= 0) world.set_door_mode(door, mode, frame_ms);
            }
            net_game->object_modes.clear();
            // A pick-up walked to: asked for once both of us are there.
            if (net_pick >= 0) {
                const auto found = net_game->ground.find(std::uint32_t(net_pick));
                if (found == net_game->ground.end()) {
                    net_pick = -1;
                } else if (std::hypot(net_game->host_x - float(found->second.x), net_game->host_y - float(found->second.y)) < 4.f
                           || (!hero.player.walking && std::hypot((hero.player.x + float(level->world_x)) * 5.f - float(found->second.x),
                                                                   (hero.player.y + float(level->world_y)) * 5.f - float(found->second.y)) < 4.f)) {
                    net_game->pick_up(found->first);
                    net_pick = -1;
                }
            }
            // A warp d2d's player set off for: the host's warp unit there (0x13).
            if (hero.take_warp >= 0 && hero.take_warp != net_warp_sent && std::size_t(hero.take_warp) < level->warps.size()) {
                const auto& warp = level->warps[std::size_t(hero.take_warp)];
                if (const auto* unit = net_game->nearest(5, -1, (warp.unit_x + float(level->world_x)) * 5.f, (warp.unit_y + float(level->world_y)) * 5.f, 15.f)) {
                    net_operate_type = 5; net_operate = unit->id; net_operate_ms = frame_ms; net_operate_moves = true; net_operate_waypoint = -1;   // as an object's: once the host has us there, stopped
                    // Its mouth: where one comes out (the free spot nearest the unit,
                    // then ExitWalk, FUN_005550b0). The unit sits in the rocks, and
                    // the host's walk to it (FUN_00548a50) stalls from the wrong side;
                    // game.exe's client walks round to the mouth first (live 2026-10-17).
                    const auto [free_x, free_y] = level->nearest_free(warp.unit_x, warp.unit_y);
                    net_warp_x = (free_x + warp.exit_x + float(level->world_x)) * 5.f;
                    net_warp_y = (free_y + warp.exit_y + float(level->world_y)) * 5.f;
                    net_warp_to = warp.destination;
                }
            }
            net_warp_sent = hero.take_warp;
        }
        // The skill buttons: a change goes to the World (0x3c), which runs a
        // right-button aura (a Paladin's).
        if (std::uint32_t(skillbar.left) != character.header.left_skill) net.send(cmd::SelectSkill{ skillbar.left, true });
        if (std::uint32_t(skillbar.right) != character.header.right_skill || (view.aura != 0) != (scene->skills.get(skillbar.right) && scene->skills.get(skillbar.right)->aura))
            net.send(cmd::SelectSkill{ skillbar.right, false });
        // The skill shrine's +all skills while its boost lasts.
        skillbar.extra.clear();
        for (const auto& [id, value] : view.boost) if (id == d2d::d2s::kAllSkills) skillbar.extra.push_back({ .stat = d2d::d2s::kAllSkills, .value = value });
        // The NPC the client's talking with (it stands meanwhile), when that changes.
        if (const int talk = npc_menu.npc >= 0 ? npc_menu.npc : speech.npc >= 0 ? speech.npc : store.npc; talk != talking_sent) {
            net.send(cmd::Chat{ talk });
            talking_sent = talk;
        }
        // A joined game, as game.exe's client: 0x2f opens a talk, 0x30 shuts
        // it (the host's quests' "talk closed", FUN_00543d50). Checked every
        // frame, so a talk opened before the host's NPC is known opens once it is.
        if (net_game) {
            namespace c2s = d2d::net::d2gs::c2s;
            if (const auto* waited = net_interact_pending >= 0 ? net_npc(net_interact_pending) : nullptr) {
                net_trade_pending = int(waited->id);
                net_trade_kind = -1; net_trade_extra = 0;
                net_trade_ms = frame_now;
                net_trade_asked = false;
                net_interact_pending = -1;
            }
            const int talk = talking_sent;
            const auto* host_npc = talk >= 0 && level && std::size_t(talk) < level->npcs.size() && level->npcs[std::size_t(talk)].root != "objects" ? net_npc(talk) : nullptr;
            if (net_chat_npc && (!host_npc || host_npc->id != net_chat_npc)) { net_game->send_items({ c2s::npc_chat(false, 1, net_chat_npc) }); net_chat_npc = 0; }
            if (host_npc && host_npc->id != net_chat_npc) { net_chat_npc = host_npc->id; net_game->send_items({ c2s::npc_chat(true, 1, net_chat_npc) }); }
        }
        for (const auto& command : input(mouse, over_ui)) send(command);
        // The screen's pad plays the screen's player (local co-op: a partner's while they have it).
        const Send to_world = [this](const Command& command) { send(command); };
        const bool field = pad_field() && !game_menu.open && !chat_typing && !gold_box;
        pad_walk(pad, focused().player, view.dead && !view.ghost, field, to_world);   // a ghost walks
        pad_act(pad, focused().player, view.dead, pad_field(), skillbar.left, skillbar.right, to_world, skillbar.left_item, skillbar.right_item);
        // Fixed ticks of kTickMs; after a stall, a few to catch up, then the
        // clock skips ahead (game.exe catches up one frame at most).
        if (world_ms == 0 || frame_ms - world_ms > 1000) world_ms = frame_ms - std::min<std::uint32_t>(frame_ms - last_ms, kTickMs);
        // A local game pauses under the game menu (FUN_0044efa0: game mode
        // 0 / 1, UI 9 or 0xb up, the player in a room): it draws and plays
        // sounds, the server's frame (FUN_0052fc20) doesn't run.
        if (!net_game && (game_menu.open || controls.open)) world_ms = frame_ms;
        bool ticked = false;
        for (int ticks = 0; frame_ms - world_ms >= kTickMs && ticks < 5; ++ticks) {
            prev_x = view.player.x; prev_y = view.player.y;
            std::vector<Order> orders;
            for (auto& command : net.receive()) orders.push_back({ focus_id(), std::move(command) });   // the screen's player's
            for (auto& command : lead_orders) orders.push_back({ hero.id, std::move(command) });       // the first player's field play meanwhile
            lead_orders.clear();
            for (auto& coop_player : coop) {                // the other co-op players' (their pads')
                for (auto& command : coop_player.orders) orders.push_back({ coop_player.id, std::move(command) });
                coop_player.orders.clear();
            }
            world.tick(orders, world_ms + kTickMs, world_ms);
            world_ms += kTickMs;
            ticked = true;
        }
        if (!ticked && view.level == world.level) return;
        publish();
        // What the World said, handled once the View it came in is here
        // (the hire list's offers come with it).
        for (const auto& event : view.events) handle(event, audio, frame_ms);
        // A partner's: a window to open (an NPC talked to, the stash) takes
        // the screen when it's free; the rest is the screen's business only.
        for (auto& other : world.players) {
            if (other->id == focus_id()) continue;
            const auto said = std::exchange(other->events, {});
            for (const auto& event : said)
                if (std::holds_alternative<ev::MusicCue>(event)) handle(event, audio, frame_ms);
            const auto opens = std::ranges::find_if(said, [](const Event& event) { return std::holds_alternative<ev::OpenUI>(event); });
            if (opens == said.end() || !focus_free()) continue;
            take_focus(other->id);
            handle(*opens, audio, frame_ms);
            break;
        }
        if (net_game) net_quests(frame_ms);
        if (frame_ms - world_ms >= kTickMs) world_ms = frame_ms - (frame_ms - world_ms) % kTickMs;
    }

auto Town::handle(const Event& event, Audio& audio, std::uint32_t frame_ms) -> void {
        if (const auto* level_changed = std::get_if<ev::LevelChanged>(&event)) {
            net_interact_pending = -1;                    // its index was the old level's
            if (level_changed->from->layer != level->layer) {
                // A layer change saves the old layer's map and loads the
                // new one's (FUN_00458d40).
                save_automap(automap, automap_dir(world), character.name, scene->map_seed, level_changed->from->layer);
                const bool seen = other_automaps.count(level->layer) != 0;
                other_automaps[level_changed->from->layer] = std::move(automap);
                automap = std::move(other_automaps[level->layer]);
                if (!seen) load_automap(automap, automap_dir(world), character.name, scene->map_seed, level->layer, int(scene->automap_cels.frames_per_direction()));
                if (level_changed->keep_map) automap.open = other_automaps[level_changed->from->layer].open;
            }
            hovered_npc = -1;
            // The slide carries on in the new level's cells (a warp's
            // distance still snaps).
            prev_x += float(level_changed->from->world_x - level->world_x);
            prev_y += float(level_changed->from->world_y - level->world_y);
            npc_menu = {}; store = {}; speech = {}; waypoint = {}; menu_after_speech = -1;
            shake = {}; quest_quakes.clear(); audio.stop_quake();
            level_ms = frame_ms;                                  // its song comes in 3 s later
            rain.reassign(rain.rng, level->id, d2d::rules::level_ids::act_of(level->id));   // the host's 0x15 for the warp
            return;
        }
        if (const auto* quake = std::get_if<ev::QuestQuake>(&event)) { quest_quakes.push_back({ quake->kind, frame_ms }); return; }
        if (std::holds_alternative<ev::Lightning>(event)) { rain.set_off(); return; }
        if (std::holds_alternative<ev::Reassigned>(event)) { rain.reassign(rain.rng, level->id, d2d::rules::level_ids::act_of(level->id)); return; }
        if (const auto* news = std::get_if<ev::QuestNews>(&event)) { quest_log_notify(quest_log, news->quest); return; }
        if (const auto* music = std::get_if<ev::MusicCue>(&event)) { audio.play_sfx(*scene, music->sound, float(audio.music_volume) / 100.f, 0); return; }
        if (const auto* object_speech = std::get_if<ev::Speech>(&event)) {   // FUN_004a1600 mode 2: the speech box, heard at once
            speech = start_speech(*scene, object_speech->npc, std::uint16_t(object_speech->string), frame_ms);
            send(cmd::QuestMessage{ object_speech->npc, object_speech->string });
            menu_after_speech = -1;
            return;
        }
        const auto& open_ui = std::get<ev::OpenUI>(event);
        if (open_ui.kind == ev::OpenUI::stash) { stash_open = inv_open = true; char_open = quest_log.open = false; return; }
        if (open_ui.kind == ev::OpenUI::trade) { inv_open = true; char_open = stash_open = cube_open = quest_log.open = false; return; }
        if (open_ui.kind == ev::OpenUI::hire) {
            npc_menu = open_hire_menu(*scene, open_ui.npc, hire_offers, character.stats.get(d2d::d2s::kGold) + character.stats.get(d2d::d2s::kGoldBank));
            return;
        }
        if (open_ui.kind == ev::OpenUI::waypoint) {
            waypoint = { .open = true, .tab = view_act(), .npc = open_ui.npc };   // on the act's own tab
            inv_open = char_open = stash_open = cube_open = quest_log.open = false;
            return;
        }
        // A joined game's lines are the host's (0x27, net_quests): ones that
        // came first, else none till they come.
        auto lines = open_ui.quest;
        if (net_game) {
            const auto* host_npc = net_npc(open_ui.npc);
            const bool early = host_npc && host_npc->id == net_early_quest.id && world_ms - net_early_ms < 3000;
            lines = early ? net_early_quest.lines : std::vector<d2d::rules::QuestMsg>{};
            net_early_quest = {};
        }
        open_menu(open_ui.npc);
        take_npc_quest(open_ui.npc, std::move(lines), frame_ms);
    }

// A quest message for the player plays at once (FUN_004a10e0 on the first
// kind-0 one; hearing it is what the server acts on), the menu after it.
auto Town::take_npc_quest(int npc, std::vector<d2d::rules::QuestMsg> lines, std::uint32_t frame_ms) -> void {
        npc_quest = std::move(lines);
        for (const auto& message : npc_quest)
            if (message.greet) {
                npc_menu = {};
                speech = start_speech(*scene, npc, std::uint16_t(message.string), frame_ms);
                send(cmd::QuestMessage{ npc, message.string });
                menu_after_speech = npc;
                return;
            }
    }

// A joined game's quest talk (the host runs the quests): each NPC's lines
// as it sends them (0x27 mode 1) — the one we're talking to takes them
// now, one that comes first waits for d2d's menu — and its "!" (0x8a):
// npcalert over that NPC, as game.exe's client puts the looping overlay
// on (FUN_004b3380 -> FUN_00470390, overlay 0x48).
// ponytail: 0x8a's palace guard check (class 0x14b, FUN_004b1620) isn't
// here, and its alert still expires locally after 16 * 111 ms.
auto Town::net_quests(std::uint32_t frame_ms) -> void {
        for (auto& host_quest : net_game->npc_quests) {
            const auto* host_npc = npc_menu.npc >= 0 ? net_npc(npc_menu.npc) : nullptr;
            if (host_npc && host_npc->id == host_quest.id) take_npc_quest(npc_menu.npc, std::move(host_quest.lines), frame_ms);
            else { net_early_quest = std::move(host_quest); net_early_ms = world_ms; }
        }
        net_game->npc_quests.clear();
        // The log as the host has it (0x52 / 0x5d / 0x29): the View's are d2d's own records'.
        if (net_game->quest_log_known) {
            view.quest_log = net_game->quest_log;
            view.game_quests = net_game->game_quests;
        }
        view.den_left = net_game->den_left;
        if (net_game->sky) { world.day.set(net_game->sky->phase, net_game->sky->time, net_game->sky->eclipse); net_game->sky.reset(); }
        // The Cube: the host opens it (0x77 0x15); shut here, the host's shuts too (0x4f 0x17).
        if (std::exchange(net_game->cube_opened, false)) { cube_open = net_cube_open = true; inv_open = true; stash_open = char_open = false; }
        if (net_cube_open && !cube_open) { net_game->send_items({ d2d::net::d2gs::c2s::button(0x17) }); net_cube_open = false; }
        for (const std::uint32_t id : net_game->npc_alerts) net_alert_ms[id] = world_ms;
        net_game->npc_alerts.clear();
        for (std::size_t i = 0; level && i < view.npc_states.size() && i < level->npcs.size(); ++i) {
            const auto* host_npc = level->npcs[i].root != "objects" ? net_npc(int(i)) : nullptr;
            const auto shown = host_npc ? net_alert_ms.find(host_npc->id) : net_alert_ms.end();
            view.npc_states[i].alert = shown != net_alert_ms.end() && world_ms - shown->second < 16 * 111;
        }
    }

auto Town::den_ambient() const -> int {
        if (!level || level->id != d2d::rules::DenQuest::kDen || !view.den_cleared || den_lit) return -1;
        return den_flash < 0 ? 80 : int(d2d::rules::cos512(den_flash * 128 / 30) * 80.0f);
    }

auto Town::den_tick(std::uint32_t frame_ms) -> void {
        if (view.den_cleared && !den_seen && level && level->id == d2d::rules::DenQuest::kDen) den_flash = 0;
        den_seen = view.den_cleared;
        if (!view.den_cleared) { den_flash = -1; den_lit = false; den_beams.clear(); return; }
        if (!den_lit && den_flash >= 0 && ++den_flash > 29) den_lit = true;
        if (!level || level->id != d2d::rules::DenQuest::kDen) { den_beams.clear(); return; }
        if (!den_lit || !den_beams.empty()) return;
        const auto found = scene->missiles.find("denofevillight");
        if (found == scene->missiles.end()) return;
        for (const auto& room : level->rooms)
            for (int tries = 0, made = 0; tries < 25 && made < 3; ++tries) {
                const int subtile_x = room.x * 5 + rain.rng(room.width * 5), subtile_y = room.y * 5 + rain.rng(room.height * 5);
                const float x = (float(subtile_x) + 0.5f) / 5, y = (float(subtile_y) + 0.5f) / 5;
                if (level->blocked_here(x, y, 0x05)) continue;
                den_beams.push_back({ &found->second, x, y, 0, frame_ms });
                ++made;
            }
        d2d::log::info("Den of Evil: {} light beams in {} rooms", den_beams.size(), level->rooms.size());
    }

auto Town::critter_tick(std::uint32_t frame_ms) -> void {
        namespace ids = d2d::rules::monster_ids;
        const auto& game_data = *world.game_data;
        if (!level) return;
        const auto& types = game_data.monsters.types;
        auto room_up = [&](const d2d::drlg::Outdoor::RoomSeed& room) {   // FUN_0046c460
            d2d::rules::Rng room_seed{ room.seed };
            for (const auto& critter : level->critters) {
                if (critter.row < 0 || std::size_t(critter.row) >= types.size()) break;
                if (int(room_seed.next() % 100) >= critter.percent) continue;
                const auto& type = types[std::size_t(critter.row)];
                int count = room_seed(type.max_grp - type.min_grp) + type.min_grp;
                if (critter.amount != 0) count *= critter.amount;
                for (; count > 0; --count)                 // FUN_0046c1a0: 10 tries at a free spot
                    for (int tries = 0; tries < 10; ++tries) {
                        const int subtile_x = room.x * 5 + room_seed(room.width * 5 - 1), subtile_y = room.y * 5 + room_seed(room.height * 5 - 1);
                        const float x = (float(subtile_x) + 0.5f) / 5, y = (float(subtile_y) + 0.5f) / 5;
                        if (level->blocked_here(x, y, 0x3f11)) continue;
                        // ponytail: its unit seed is the room's on from here; game.exe's comes with the unit (FUN_00465fd0), not traced.
                        critters.push_back({ critter.row, x, y, x, y, 4, false, false, 0, 0, frame_ms, d2d::rules::Rng{ room_seed.next() } });
                        break;
                    }
            }
        };
        if (level->id != critters_level) {
            critters.clear();                              // the last level's go at once, even while the camp waits
            std::optional<std::vector<d2d::drlg::Outdoor::RoomSeed>> camp;
            if (level == &game_data.town) {                // the camp keeps no rooms: made from the map seed
                camp = d2d::game::camp_room_seeds(game_data);
                if (!camp) return;                         // the builder's busy: next frame
            }
            critters_level = level->id;
            critter_rooms.assign(level->rooms.size(), false);
            // ponytail: the camp's come all at once (it keeps no rooms to come up); it's small.
            if (camp) for (const auto& room : *camp) room_up(room);
        }
        // FUN_0044c750: a room's critters as it comes up on the client, when
        // the host adds it; d2d's rooms come up the same way (Spawning's `up`).
        if (const auto state = world.fight.spawning.levels.find(level); state != world.fight.spawning.levels.end())
            for (std::size_t index = 0; index < state->second.up.size() && index < critter_rooms.size(); ++index)
                if (state->second.up[index] && !critter_rooms[index]) { critter_rooms[index] = true; room_up(level->rooms[index]); }
        const float player_x = view.player.x, player_y = view.player.y;
        // FUN_006416d0: subtiles apart less half of each one's size (a critter's 0, a unit's 2), (smaller + 2 x larger) / 2.
        auto apart = [](float from_x, float from_y, float to_x, float to_y) {
            const int across = std::max(std::abs(int(std::lround((to_x - from_x) * 5))) - 1, 0), down = std::max(std::abs(int(std::lround((to_y - from_y) * 5))) - 1, 0);
            return (std::min(across, down) + 2 * std::max(across, down)) / 2;
        };
        auto walk_to = [&](Critter& critter, float x, float y, int after) {   // FUN_004aff60: walk there, then wait
            critter.goal_x = x; critter.goal_y = y; critter.after = after;
            if (!critter.walking) critter.mode_ms = frame_ms;
            critter.walking = true;
        };
        auto wander = [&](Critter& critter, int radius, int after) {         // FUN_0046c960: a random corner `radius` subtiles off
            const float step = float(radius) / 5;
            const float dx = critter.seed.next() & 1 ? -step : step, dy = critter.seed.next() & 1 ? -step : step;
            walk_to(critter, critter.x + dx, critter.y + dy, after);
        };
        auto flee = [&](Critter& critter, int radius, int after) {           // FUN_0046c7d0: straight away, when the next subtile's free
            const float away_x = critter.x < player_x ? -1.f : critter.x > player_x ? 1.f : 0.f, away_y = critter.y < player_y ? -1.f : critter.y > player_y ? 1.f : 0.f;
            if (level->blocked_here(critter.x + away_x / 5, critter.y + away_y / 5, 0x3c01)) return false;
            walk_to(critter, critter.x + away_x * float(radius) / 5, critter.y + away_y * float(radius) / 5, after);
            return true;
        };
        auto stand = [&](Critter& critter, int frames) {
            critter.wait = frames;
            if (critter.walking) critter.mode_ms = frame_ms;
            critter.walking = false;
        };
        // FUN_0046c600: the nearest player, or monster other than a chicken or bunny (the town's NPCs too).
        auto nearest_other = [&](const Critter& critter) {
            int best = apart(critter.x, critter.y, player_x, player_y);
            for (const auto& other : view.others) best = std::min(best, apart(critter.x, critter.y, other.unit.x, other.unit.y));
            for (const auto& monster : view.monsters)
                if (monster.alive()) best = std::min(best, apart(critter.x, critter.y, monster.unit.x, monster.unit.y));
            for (std::size_t i = 0; i < view.npc_states.size() && i < level->npcs.size(); ++i)
                if (level->npcs[i].root != "objects" && !view.npc_states[i].hidden) best = std::min(best, apart(critter.x, critter.y, view.npc_states[i].x, view.npc_states[i].y));
            return best;
        };
        auto flies = [](int row) { return row == ids::kBird1 || row == ids::kBird2 || row == ids::kParrot; };
        for (auto& critter : critters) {
            if (critter.walking) {                          // at its Velocity (MonStats), stopped by what's in the way
                const float speed = std::size_t(critter.row) < game_data.mon_npc.size() ? d2d::game::cells_per_sec(game_data.mon_npc[std::size_t(critter.row)].velocity) * 0.04f : 0.1f;
                const float dx = critter.goal_x - critter.x, dy = critter.goal_y - critter.y, left = std::hypot(dx, dy);
                if (left > 0.01f) critter.dir = d2d::game::direction16(dx, dy);
                const float step = std::min(speed, left);
                const float next_x = critter.x + (left > 0 ? dx / left * step : 0), next_y = critter.y + (left > 0 ? dy / left * step : 0);
                if (left <= speed || (!flies(critter.row) && level->blocked_here(next_x, next_y, 0x3c01))) { stand(critter, critter.after); if (left <= speed) { critter.x = next_x; critter.y = next_y; } }
                else { critter.x = next_x; critter.y = next_y; }
                continue;
            }
            if (critter.wait > 0) { --critter.wait; continue; }
            const int player_gap = apart(critter.x, critter.y, player_x, player_y);
            if (player_gap > 30) { stand(critter, std::min(player_gap - 15, 200)); continue; }   // FUN_0046cbd0
            if (std::size_t(critter.row) < types.size() && types[std::size_t(critter.row)].zoo) {   // FUN_0046d660: MonStats zoo (no shipped critter)
                const bool walks = std::size_t(critter.row) < game_data.mon_npc.size() && game_data.mon_npc[std::size_t(critter.row)].modes >> 2 & 1;   // FUN_0046c140: has WL
                const int roll = walks ? critter.seed(100) : 0;
                if (!walks || roll < 40) stand(critter, 20);
                else if (roll < 60) walk_to(critter, player_x, player_y, 6);   // FUN_0046c770: to the player
                else if (roll < 80) flee(critter, 8, 4);
                else wander(critter, 8, 4);
                continue;
            }
            if (critter.row == ids::kRat || critter.row == ids::kScorpion || critter.row == ids::kLarva || critter.row == ids::kMiniSpider) {   // FUN_0046d110
                if (player_gap < 2 && int(critter.x * 5) == int(player_x * 5) && int(critter.y * 5) == int(player_y * 5)) { stand(critter, 0); continue; }   // FUN_0046cbf0
                if (player_gap < 4) { flee(critter, 6, 0); continue; }
                // FUN_0046c660 over the units near: a corpse 3..25 off and over 7
                // from the player, a coin flip each, the last one wins.
                // ponytail: d2d's monsters then the players' bodies; game.exe's goes by its rooms' unit lists.
                std::optional<std::pair<float, float>> corpse;
                auto consider = [&](float x, float y) {
                    const int gap = apart(critter.x, critter.y, x, y);
                    if (gap >= 3 && gap <= 25 && apart(x, y, player_x, player_y) > 7 && critter.seed(2) != 0) corpse = { x, y };
                };
                if (player_gap > 7) {
                    for (const auto& monster : view.monsters)
                        if (!monster.alive() && !monster.gone) consider(monster.unit.x, monster.unit.y);
                    for (const auto& body : view.bodies) consider(body.x, body.y);
                }
                if (corpse && !critter.marked) { walk_to(critter, corpse->first, corpse->second, 6); critter.marked = true; continue; }   // FUN_0046c770
                if (!critter.marked) { if (critter.seed(100) > 74) wander(critter, 5, 0); else stand(critter, 30); continue; }
                if (critter.seed(100) < 20) critter.marked = false;   // resting by a corpse, till one in five
                stand(critter, 32);
                continue;
            }
            if (flies(critter.row)) {                       // FUN_0046ce40: gone once it's flown and the player's past 25
                if (critter.marked && player_gap > 25) { critter.row = -1; continue; }
                critter.marked = true;
                // A spot 40 subtiles off the player on one axis, rand(40) on the
                // other, each signed by a coin; it flies 3 x that from where it is,
                // its path's distance capped at 77 (FUN_00648e70: 0x5a -> 0x4d).
                int across = 40, down = 40;
                if (critter.seed.next() & 1) down = critter.seed(40); else across = critter.seed(40);
                if (critter.seed.next() & 1) across = -across;
                if (critter.seed.next() & 1) down = -down;
                float to_x = 3 * (player_x + float(across) / 5) - 2 * critter.x, to_y = 3 * (player_y + float(down) / 5) - 2 * critter.y;
                const float length = std::hypot(to_x - critter.x, to_y - critter.y), most = 77.f / 5;
                // ponytail: cut short along the line; game.exe's path stops at its 77th subtile, same line for a flyer.
                if (length > most) { to_x = critter.x + (to_x - critter.x) * most / length; to_y = critter.y + (to_y - critter.y) * most / length; }
                walk_to(critter, to_x, to_y, 0);
                continue;
            }
            const int chance = int(critter.seed.next() % 100);
            switch (critter.row) {
            case ids::kChicken: case ids::kBug: case ids::kBunny: {   // FUN_0046ccd0 / FUN_0046d310
                const int flee_by = critter.row == ids::kBunny ? 6 : 4;
                if (nearest_other(critter) < flee_by) { if (!flee(critter, flee_by, 0x78)) wander(critter, flee_by, 0x78); }
                else if (chance < 30) wander(critter, 2, 0);
                else stand(critter, 5);
                break;
            }
            case ids::kBat:                                             // FUN_0046cdc0
                if (player_gap < 5) { if (chance < 66) flee(critter, 8, 0x1e); else wander(critter, 12, 0x1e); }
                else wander(critter, 9, 0);
                break;
            case ids::kMaggot:                                          // FUN_0046d070
                if (chance < 40) stand(critter, 20); else wander(critter, 4, 0);
                break;
            case ids::kSnake:                                           // FUN_0046d280
                if (player_gap < 2) flee(critter, 4, 0);
                else if (chance < 75) stand(critter, 32);
                else wander(critter, 4, 0);
                break;
            default: stand(critter, 25); break;                         // the fish: it stays
            }
        }
        std::erase_if(critters, [](const Critter& critter) { return critter.row < 0; });
    }

auto Town::open_menu(int npc) -> void {
        const auto& npc_info = level->npcs[std::size_t(npc)];
        const auto& state = view.npc_states[std::size_t(npc)];
        const float dx = (npc_info.path.empty() ? npc_info.x : state.x) - view.player.x, dy = (npc_info.path.empty() ? npc_info.y : state.y) - view.player.y;
        npc_menu = open_npc_menu(*scene, *level, npc,
            int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2))),
            kViewY + int(std::lround((dx + dy) * (kIsoH / 2))),
            int(character.stats.get(d2d::d2s::kLevel)), d2d::rules::unidentified(character.items), [&] {
                const int difficulty = character.header.active_difficulty();
                const auto& quest_bits = character.header.quests[std::size_t(std::clamp(difficulty, 0, 2))];
                return !d2d::rules::quest::bit(quest_bits, d2d::rules::kRespecQuest, 0) && (d2d::rules::quest::bit(quest_bits, d2d::rules::kRespecQuest, 1) || difficulty == 2);
            }(), character.header.quest_flag(character.header.active_difficulty(),   // table 0x7253e0: Warriv by quest 6, Meshif by quest 14 (bit 0)
                                             npc_info.hc_idx == d2d::rules::monster_ids::kMeshif ? d2d::rules::SevenTombsQuest::kQuest : d2d::rules::AndyQuest::kQuest, 0),
            character.header.quest_flag(character.header.active_difficulty(), npc_info.hc_idx == d2d::rules::SiegeQuest::kLarzuk ? d2d::rules::SiegeQuest::kQuest
                                        : npc_info.hc_idx == d2d::rules::BetrayalQuest::kAnya ? d2d::rules::BetrayalQuest::kQuest : d2d::rules::ToolsQuest::kQuest, 1),
            character.header.merc_seed != 0 && character.header.merc_dead
                ? resurrect_line(*scene, character.header.merc_type, character.header.merc_name,
                                 d2d::rules::merc_resurrect_cost(d2d::rules::merc_stats(scene->rules, character.header.merc_type, character.header.merc_exp).level))
                : std::string());
    }

// Widescreen: the world draws into `framebuffer` (wider than kScreenWidth)
// and everything else into its centre 800x600, as it always did.
auto Town::draw(std::vector<std::uint8_t>& framebuffer, const Mouse& mouse, std::uint32_t frame_ms) -> void {
    const int screen_w = fb_width(framebuffer);
    const bool left_open = char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open || party.open;
    if (screen_w == int(kScreenWidth)) {
        draw_view(framebuffer, nullptr, mouse, frame_ms);
        draw_coop_portraits(framebuffer, left_open);
        return;
    }
    const int off = (screen_w - int(kScreenWidth)) / 2;
    ui_buffer.resize(std::size_t(kScreenWidth) * kScreenHeight * 4);
    draw_view(ui_buffer, &framebuffer, mouse, frame_ms);
    for (std::size_t y = 0; y < kScreenHeight; ++y)
        std::copy_n(ui_buffer.begin() + std::ptrdiff_t(y * kScreenWidth * 4), kScreenWidth * 4,
                    framebuffer.begin() + std::ptrdiff_t((y * std::size_t(screen_w) + std::size_t(off)) * 4));
    draw_coop_portraits(framebuffer, left_open);       // on the whole frame: widescreen's spare width
}

auto Town::draw_view(std::vector<std::uint8_t>& framebuffer, std::vector<std::uint8_t>* wide, const Mouse& mouse, std::uint32_t frame_ms) -> void {
        auto& unit = view.player;                           // the client's copy: its animation clock is the client's
        const int pmode = view.pmode;
        const bool run_on = view.running && character.stats.values[d2d::d2s::kStamina] > 0;   // no run at stamina 0 (FUN_0057f090)
        if (const bool running = unit.walking && run_on; pmode < 0 && (unit.walking != player_walked || running != player_ran)) {
            player_walked = unit.walking; player_ran = running; walk_ms = frame_ms;
        }
        if (pmode < 0) unit.mode_ms = std::max(unit.mode_ms, walk_ms);     // a swing's end restarts it too
        const int ui_cls = kSaveClassToUi[std::max(character.character_class, 0)];
        // Monsters in view, as units the world draws by depth.
        std::vector<Unit> extra;
        // The camera (and the player's unit) between the World's last two
        // ticks; a jump (a warp, devctl) snaps.
        // ponytail: the other units move at the tick rate, as game.exe draws them.
        const float blend = std::clamp(float(frame_ms - world_ms) / float(kTickMs), 0.f, 1.f);
        const bool jump = std::hypot(unit.x - prev_x, unit.y - prev_y) > 2.f;
        const float hero_x = jump ? unit.x : prev_x + (unit.x - prev_x) * blend, hero_y = jump ? unit.y : prev_y + (unit.y - prev_y) * blend;
        cam_x = hero_x; cam_y = hero_y;
        if (!coop.empty()) {
            // Local co-op: the camera eases to the party's middle (one screen,
            // World::leash keeps it there); a jump (a level, a warp) snaps.
            float sum_x = hero_x, sum_y = hero_y, count = 1;
            for (const auto& other : view.others)
                if (std::ranges::contains(coop, int(other.id), &CoopPlayer::id)) { sum_x += other.unit.x; sum_y += other.unit.y; ++count; }
            const float want_x = sum_x / count, want_y = sum_y / count;
            const float ease = std::clamp(float(frame_ms - party_ms) / 120.f, 0.f, 1.f);
            party_ms = frame_ms;
            const bool party_jump = jump || std::hypot(want_x - party_x, want_y - party_y) > 8.f;
            party_x = party_jump ? want_x : party_x + (want_x - party_x) * ease;
            party_y = party_jump ? want_y : party_y + (want_y - party_y) * ease;
            cam_x = party_x; cam_y = party_y;
        }
        // Inverse isometric projection: shift the camera, keeping every
        // world unit (including the local player) at its actual position.
        cam_x += float(shake.offset.first) / kIsoW + float(shake.offset.second) / kIsoH;
        cam_y += float(shake.offset.second) / kIsoH - float(shake.offset.first) / kIsoW;
        const std::pair<float, float> hero_at{ hero_x, hero_y };
        state_clock.now = frame_ms;
        view_units(*scene, view, cam_x, cam_y, &merc_label, extra, den_beams, frame_ms, &character.name, std::max(character.character_class, 0), &state_clock);
        for (const auto& critter : critters)                      // the client's critters: walking or standing, never hovered
            if (std::size_t(critter.row) < world.game_data->mon_npc.size() && std::abs(critter.x - cam_x) < 14 && std::abs(critter.y - cam_y) < 14) {
                const auto& anim = scene->npc_anim(world.game_data->mon_npc[std::size_t(critter.row)], critter.walking ? std::string_view("WL") : std::string_view("NU"));
                if (anim.length_ms() > 0) extra.push_back({ critter.x, critter.y, &anim, critter.dir, nullptr, critter.mode_ms, -3 });
            }
        // The host's overhead messages (0x26 type 5): a shrine's comes as
        // its string id in decimal ("3684", ShrMsg1). Each stays (length x 8
        // + 125) frames (FUN_00661110). An object's goes on d2d's object
        // nearest the host's; a player's or monster's on theirs.
        // ponytail: text that isn't a string id isn't shown.
        std::vector<UnitState> said_npcs;                    // the NPC states drawn, with the host's objects' messages: a copy (the View's are replicated by change)
        if (net_game) {
            for (const auto& said : std::exchange(net_game->overheads, {})) {
                const auto number = std::strtoul(said.text.c_str(), nullptr, 10);
                if (said.text.empty() || said.text.find_first_not_of("0123456789") != std::string::npos || number == 0 || number > 0xffff) continue;
                net_said[std::uint64_t(said.type) << 32 | said.id] = { std::uint16_t(number), frame_ms + std::uint32_t(said.text.size() * 8 + 125) * kTickMs };
            }
            std::erase_if(net_said, [&](const auto& entry) { return frame_ms >= entry.second.until; });
            for (const auto& [unit_key, said] : net_said) {
                const auto host = net_game->units.find(unit_key);
                if (host == net_game->units.end()) continue;
                if (host->second.type == 2) {
                    int nearest = -1;
                    float best = 4.f;
                    for (std::size_t i = 0; i < level->npcs.size() && i < view.npc_states.size(); ++i)
                        if (level->npcs[i].root == "objects")
                            if (const float apart = std::hypot((level->npcs[i].x + float(level->world_x)) * 5.f - host->second.x, (level->npcs[i].y + float(level->world_y)) * 5.f - host->second.y); apart < best) { best = apart; nearest = int(i); }
                    if (nearest < 0) continue;
                    if (said_npcs.empty()) said_npcs = view.npc_states;
                    said_npcs[std::size_t(nearest)].says = said.string;
                    continue;
                }
                for (auto& drawn : extra) {
                    const bool is_player = drawn.npc <= -4000 && std::size_t(-4000 - drawn.npc) < view.others.size() && view.others[std::size_t(-4000 - drawn.npc)].id == host->second.id && host->second.type == 0;
                    const bool is_monster = drawn.npc <= -10 && drawn.npc > -4000 && std::size_t(-10 - drawn.npc) < view.monsters.size() && std::uint32_t(view.monsters[std::size_t(-10 - drawn.npc)].id) == host->second.id && host->second.type == 1;
                    if (is_player || is_monster) drawn.says = said.string;
                }
            }
        }
        std::erase_if(state_clock.seen, [&](const auto& entry) { return frame_ms - entry.second.last > 5000; });
        if (alert_level != level) { alert_clocks.clear(); alert_level = level; }
        alert_clocks.resize(view.npc_states.size());
        for (std::size_t i = 0; i < view.npc_states.size(); ++i) {
            alert_clocks[i].set(view.npc_states[i].alert, frame_ms);
            if (net_game && view.npc_states[i].alert) {
                const auto* host_npc = net_npc(int(i));
                const auto shown = host_npc ? net_alert_ms.find(host_npc->id) : net_alert_ms.end();
                if (shown != net_alert_ms.end()) alert_clocks[i].start = frame_ms - (world_ms - shown->second);
            }
        }
        Unit player_look{};
        auto look_states = player_states(*scene, view);
        if (net_game)
            for (const int row : net_game->self_states) if (std::size_t(row) < scene->state_names.size()) look_states.emplace_back(scene->state_names[std::size_t(row)]);
        player_look.x = view.player.x; player_look.y = view.player.y;
        dress(*scene, player_look, -1, look_states, &state_clock);
        // A state gone plays its offsound once, where it was; one leaving the
        // view with its unit (the in_view box, 14 cells) hasn't ended.
        for (auto& [key, seen] : state_clock.seen)
            if (seen.last != frame_ms && !seen.off) {
                seen.off = true;
                if (key.second->off_sound > 0 && std::abs(seen.x - cam_x) < 13 && std::abs(seen.y - cam_y) < 13) state_clock.sounds.push_back({ key.second->off_sound, seen.x, seen.y });
            }
        for (const auto& sound : state_clock.sounds) cues.cue(sound.sound, frame_ms, sound.x, sound.y);
        state_clock.sounds.clear();
        const bool town = d2d::rules::level_ids::is_town(level->id);                       // TN/TW in town, NU/WL outside
        // A dead player has no DD composite: DT held on its last frame.
        const auto cls = std::max(character.character_class, 0);
        if (pmode == kModeDD && !view.ghost) unit.mode_ms = frame_ms - (scene->composite(cls, kModeDT, view.gfx).length_ms() - 1);
        int mode = view.ghost ? (unit.walking ? (town ? kModeTW : kModeWL) : town ? kModeTN : kModeNU)   // a ghost walks along, its body lies apart
                 : pmode == kModeDD ? kModeDT : pmode >= 0 ? pmode : unit.walking ? (run_on ? kModeRN : town ? kModeTW : kModeWL) : town ? kModeTN : kModeNU;
        std::uint32_t mode_ms = unit.mode_ms;
        float rate = pmode >= 0 && pmode != kModeDD ? view.prate : 1.f;
        // A walk, run or town walk plays at the walk's rate, not AnimData's (FUN_00623f50).
        if (pmode < 0 && !view.ghost && (mode == kModeWL || mode == kModeRN || mode == kModeTW))
            if (const auto& walk_anim = scene->composite(cls, mode, view.gfx); walk_anim.speed || walk_anim.cof_speed)
                rate = float(d2d::rules::moving_anim_rate(mode == kModeRN, view.walk_percent)) / float(walk_anim.speed ? walk_anim.speed : walk_anim.cof_speed);
        if (!view.seq.empty() && attack_mode(pmode)) { std::tie(mode, mode_ms) = view_seq(*scene, int(cls), view, frame_ms); rate = 1.f; }   // an SQ skill's frame
        const auto light = frame_light(*scene, view, cam_x, cam_y, den_beams, den_ambient(), extra, &player_look, frame_ms, &hero_at);
        // A pad in the field shows no cursor: it hovers what A would act on
        // instead (its name, and a monster's life bar), a spot up its body.
        // ponytail: a spot, not its box: something drawn in front can take the hover.
        int hover_x = held ? -1 : mouse.x, hover_y = held ? -1 : mouse.y;
        if (pad_cue) {
            hover_x = hover_y = -1;
            if (const auto pick = pad_pick(pad, focused().player, true); pick.kind != PadPick::kNone && focus_id() == hero.id) {
                const float dx = pick.x - cam_x, dy = pick.y - cam_y;
                hover_x = int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
                hover_y = kViewY + int(std::lround((dx + dy) * (kIsoH / 2))) - (pick.kind == PadPick::kItem ? 4 : 24);
            }
        }
        automap_frame();
        render_ingame(framebuffer, *scene, *view.level, ui_cls,
                      view.gfx,
                      character.name, character.hardcore,
                      cam_x, cam_y, mode,
                      unit.dir, frame_ms, hover_x, hover_y, said_npcs.empty() ? std::span<const UnitState>(view.npc_states) : std::span<const UnitState>(said_npcs),
                      inv_open ? &character.items : nullptr,
                      char_open ? &character.stats : nullptr, &character.stats, &character.panel, mode_ms, &character.items,
                      &hovered_npc, stash_open || cube_open ? &character.items : nullptr, character.expansion, belt_open,
                      cube_open ? (transmute_down ? 2 : 1) : 0, &npc_menu, &speech, &automap, &store, stat_pressed,
                      nullptr, nullptr, nullptr, extra, rate, light.pal ? &light : nullptr, &rain,
                      !(pmode == kModeDD && !view.corpses.empty()),    // dead, the corpse lies there instead
                      &player_look, alt_held || function_held(keys::kShowItems),   // the key config's Show Items (Alt)
                      Hud{ view.poisoned, view.running, run_down, character.header.weapon_set != 0, coop_hud_hidden(),
                          char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open, inv_open || tree_open, view.stamina_blue }, wide, { hero_x - cam_x, hero_y - cam_y }, view.ghost, alert_clocks);
        for (const auto& body : view.bodies)                       // local co-op: a body being raised
            if (body.raisers > 0 || body.raised > 0) {
                const float dx = body.x - cam_x, dy = body.y - cam_y;
                draw_raise_aura(framebuffer, int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2))), kViewY + int(std::lround((dx + dy) * (kIsoH / 2))), body.raised, frame_ms);
            }
        if (scene->store_buttons.frames_per_direction() > 11) {     // the close buttons, over their panels
            const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
            const auto& lay = scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
            if (inv_open) blit_at_anchor(framebuffer, scene->store_buttons.frame(0, inv_close_down ? 11 : 10), pal, lay.panel_x + 0x12, lay.panel_y + 416);
            if (char_open) blit_at_anchor(framebuffer, scene->store_buttons.frame(0, char_close_down ? 11 : 10), pal, kCharPanelX + 0x80, kCharPanelY + 420);
        }
        // A level's "entering" art for 2 s after a change, none for the game's
        // first (FUN_00455510): its frames side by side, centred at W/2 - 1,
        // bottom at H/2 - 140, in text colour 1, red (FUN_00502680's shift); the Den's / the Tower's quest art while that
        // quest's open in this game (FUN_004b92e0: neither done nor closed).
        if (level->id != entry_level) { entry_until = entry_level != 0 ? frame_ms + 2000 : 0; entry_level = level->id; }
        if (frame_ms < entry_until) {
            static constexpr std::array<std::pair<int, int>, 2> kQuestArt{ { { 8, 1 }, { 20, 5 } } };   // level, quest
            std::string path = scene->level_entry.contains(level->id) ? scene->level_entry.at(level->id) : std::string();
            for (const auto& [quest_level, quest] : kQuestArt)
                if (level->id == quest_level && scene->level_entry_quest.contains(quest_level)
                    && !(view.game_quests[std::size_t(quest)] >> 13 & 1) && !(view.game_quests[std::size_t(quest)] >> 15 & 1))
                    path = scene->level_entry_quest.at(quest_level);
            if (const auto art = scene->entry_art.find(path); art != scene->entry_art.end()) {
                const auto frames = art->second.frames_per_direction();
                int width = 0;
                for (std::uint32_t frame = 0; frame < frames; ++frame) width += int(art->second.frame(0, frame).width);
                const auto& pal = !scene->act_text_red.entries().empty() ? scene->act_text_red : scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
                for (std::uint32_t frame = 0; frame < frames; ++frame)
                    blit_at_anchor(framebuffer, art->second.frame(0, frame), pal, fb_width(framebuffer) / 2 - 1 - width / 2 + int(frame) * 0x100, int(kScreenHeight) / 2 - 0x8c);
            }
        }
        view_overlays(framebuffer, *scene, view, hovered_monster());
        if (!coop_hud_hidden() || skillbar.picking) skillbar.draw(framebuffer, held ? -1 : mouse.x, held ? -1 : mouse.y, tip_env());
        if (pad_cue && focus_id() == hero.id) draw_pad_hint(framebuffer);
        if (net_game && net_game->trade.state != 0) {
            std::vector<d2d::d2s::Item> theirs;
            for (const auto& [id, stocked] : net_game->store_items) {
                theirs.push_back(stocked);
                theirs.back().location = d2d::d2s::item_location::kStored;
                theirs.back().panel = 100;
            }
            const auto& trade = net_game->trade;
            std::string with = trade.with_name;
            if (with.empty())
                if (const auto partner = net_game->units.find(trade.with); partner != net_game->units.end()) with = partner->second.name;
            draw_trade(framebuffer, *scene, trade.state, with.empty() ? std::string("Another player") : with, character.name, theirs, trade.ours, trade.their_gold,
                       trade.our_gold, nullptr, held ? -1 : mouse.x, held ? -1 : mouse.y, nullptr);
        }
        draw_portraits(framebuffer, char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open);
        if (gold_box) draw_gold_box(framebuffer, *scene, *gold_box, frame_ms);
        draw_chat(framebuffer, char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open, inv_open || tree_open);
        if (quest_log_was_open && !quest_log.open) {    // shut by any means (FUN_00455ae0 → FUN_004a28d0)
            quest_log.open = true;
            quest_log_close(quest_log, quest_bits(), quest_state());
        }
        quest_log_was_open = quest_log.open;
        if (quest_log.open && draw_quest_log(framebuffer, *scene, quest_log, quest_bits(), quest_state(), frame_ms, character.expansion, mouse.x, mouse.y))
            questdone_sound = true;                    // cursor_questdone
        if (tree_open)
            draw_skill_tree(framebuffer, *scene, int(kUiToSaveClass[ui_cls]), tree_tab, character.stats.skills, character.stats, tip_env(),
                            skill_pressed, held ? -1 : mouse.x, held ? -1 : mouse.y);
        if (waypoint.open)
            draw_waypoints(framebuffer, *scene, waypoint, character.header, character.expansion, level->id, mouse.x, mouse.y);
        draw_level_buttons(framebuffer, *scene, level_buttons_now(), mouse.x, mouse.y);   // after the panels, as the frame's draw
        draw_quest_log_button(framebuffer, *scene, quest_log_button_now(), quest_log.button_down, mouse.x, mouse.y);
        const bool left_open = char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open;
        if (!coop_hud_hidden()) mini.draw(framebuffer, *scene, left_open, inv_open || tree_open, mouse.x, mouse.y);
        if (game_menu.open) game_menu.draw(framebuffer, *scene);
        if (party.open && (net_game || coop_hud_hidden() || !party_preview.empty())) party.draw(framebuffer, *scene, party_rows(), party_own(), mouse.x, mouse.y);
        if (hireling.open && view.merc_sheet)
            hireling.draw(framebuffer, *scene, *view.merc_sheet, merc_label, character.merc_items, held, mouse.x, mouse.y);
        if (help.open) help.draw(framebuffer, *scene, key_table);
        if (controls.open) controls.draw(framebuffer, *scene, now_ms);
        // A joining pad's pick: down the left, clear of the action in the middle and of the portraits' column.
        const int spare = wide ? (fb_width(*wide) - int(kScreenWidth)) / 2 : 0;
        const int roster_x = std::max(15, kCoopPortraitX + kCoopPortraitWidth + 8 - spare);
        int roster_top = 0x60;
        for (const auto& coop_player : coop)
            if (coop_player.roster) roster_top += draw_roster_at(framebuffer, *scene, *coop_player.roster, roster_x, roster_top, coop_player.colour) + 4;
    }


// Chat text broken at spaces into lines no wider than `width` (a longer
// word by characters).
// ponytail: FUN_00502970 (the edit box's wrap) taken to break as the chat
// lines' does.
auto Town::wrap_chat(const std::string& text, int width) const -> std::vector<std::string> {
    const auto& font = scene->font_chat.line_height() > 0 ? scene->font_chat : scene->font;
    std::vector<std::string> wrapped{ std::string{} };
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = std::min(text.find(' ', start), text.size());
        const auto word = text.substr(start, end - start);
        const auto joined = wrapped.back().empty() ? word : wrapped.back() + " " + word;
        if (!wrapped.back().empty() && font.measure(joined) > width) wrapped.push_back(word);
        else wrapped.back() = joined;
        while (wrapped.back().size() > 1 && font.measure(wrapped.back()) > width) {   // a word wider than the box: cut by characters
            auto& line = wrapped.back();
            std::size_t fits = 1;
            while (fits + 1 < line.size() && font.measure(line.substr(0, fits + 1)) <= width) ++fits;
            std::string rest = line.substr(fits);
            line.resize(fits);
            wrapped.push_back(std::move(rest));
        }
        start = end + 1;
    }
    return wrapped;
}

// A chat line (FUN_0049e3a0): it stays 10 s, wrapped to the screen's width
// - 70 (at most 6 lines); past 18 lines on screen the oldest message goes.
auto Town::add_chat(const std::string& text, int colour, std::size_t gold) -> void {
    auto wrapped = wrap_chat(text, int(kScreenWidth) - 70);
    wrapped.resize(std::min<std::size_t>(wrapped.size(), 6));
    for (auto& line : wrapped) {
        chat_lines.push_back({ std::move(line), now_ms + 10000, colour, gold });
        gold = 0;                                               // the name is on the first line
    }
    if (chat_lines.size() > 18) chat_lines.erase(chat_lines.begin(), chat_lines.end() - 18);   // ponytail: by line, not by message
}

// The chat lines (FUN_0049dc40): FontInGameChat (0xd), at x 15 (the
// right half's when only a left panel is open, FUN_0045ae90 2 (?)),
// baselines 20 + 15 a line, each on a dark box from x - 5, 14 up,
// 16 high, the text's width + 10 (FUN_0046efd0 colour 0, mode 1).
// The edit box (FUN_0047b720, UI\chat.cpp): a dark box at x 0x7f, y H - 0x67,
// W - 0xff wide, 0x2f high; the typed text wrapped to W - 0x109, white, at
// x 0x83 on baselines H - 0x58 + 15 a line, the first 3 lines only; the
// caret, string 0xd4c "_", after the last line drawn, on 500 ms and off
// 100 (FUN_0047b450). Colours 6 / 7 (black, tan) are drawn white.
auto Town::draw_chat(std::vector<std::uint8_t>& framebuffer, bool left_open, bool right_open) const -> void {
    if (chat_lines.empty() && !chat_typing) return;
    const auto& font = scene->font_chat.line_height() > 0 ? scene->font_chat : scene->font;
    const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    const int x = left_open && !right_open ? int(kScreenWidth) / 2 + 15 : 15;
    int baseline = portraits_shown ? 0x5f : 20;   // ponytail: taken as below the portraits (UI flag 0x13 (?))
    static constexpr std::array<std::array<std::uint8_t, 3>, 10> kRgb{ { { 255, 255, 255 }, { 255, 77, 77 }, { 0, 255, 0 }, { 105, 105, 255 }, { 199, 179, 119 },
                                                                          { 105, 105, 105 }, { 255, 255, 255 }, { 255, 255, 255 }, { 255, 168, 0 }, { 255, 255, 100 } } };
    auto darken = [&](int left, int top, int width, int height) {   // FUN_0046efd0 colour 0, mode 1
        for (int y = std::max(top, 0); y < std::min(top + height, int(kScreenHeight)); ++y)
            for (int column = std::max(left, 0); column < std::min(left + width, int(kScreenWidth)); ++column)
                for (int channel = 0; channel < 3; ++channel) {
                    auto& value = framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(column)) * 4 + std::size_t(channel)];
                    value = std::uint8_t(value / 2);
                }
    };
    auto line = [&](const std::string& text, int colour, std::size_t gold) {
        darken(x - 5, baseline - 14, font.measure(text) + 10, 16);
        const auto& rgb = kRgb[std::size_t(colour >= 0 && colour < 10 ? colour : 0)];
        const std::string name = text.substr(0, gold), rest = text.substr(name.size());
        const int top = baseline - font.line_height() + 1;
        if (!name.empty()) font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x, top, name, 199, 179, 119);
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + font.measure(name), top, rest, rgb[0], rgb[1], rgb[2]);
        baseline += 15;
    };
    for (const auto& shown : chat_lines) line(shown.text, shown.colour, shown.gold);
    if (!chat_typing) return;
    constexpr int kWidth = int(kScreenWidth), kHeight = int(kScreenHeight);
    darken(0x7f, kHeight - 0x67, kWidth - 0xff, 0x2f);
    const auto lines = chat_typing->empty() ? std::vector<std::string>{ std::string{} } : wrap_chat(*chat_typing, kWidth - 0x109);
    const auto shown = std::min<std::size_t>(lines.size(), 3);
    for (std::size_t i = 0; i < shown; ++i)
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, 0x83, kHeight - 0x58 + 15 * int(i) - font.line_height() + 1, lines[i], 255, 255, 255);
    if (now_ms % 600 < 500)   // ponytail: blinks on the clock, not from when typing began
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, 0x83 + font.measure(lines[shown - 1]), kHeight - 0x58 + 15 * int(shown - 1) - font.line_height() + 1, "_", 255, 255, 255);
}

// The field's pad prompt over the control panel: the button A is bound to,
// and what it does to the thing it hovers. A d2d extra: game.exe has no pad.
auto Town::draw_pad_hint(std::vector<std::uint8_t>& framebuffer) const -> void {
    const auto pick = pad_pick(pad, view.player, true);
    if (pick.kind == PadPick::kNone) return;
    // A town portal (-2000 - which) is used, a corpse (-3000 - which) picked up, as an object / ground item.
    const bool portal = pick.kind == PadPick::kThing && pick.id <= -2000 && pick.id > -3000;
    const bool corpse = pick.kind == PadPick::kThing && pick.id <= -3000 && pick.id > -4000;
    const bool object = pick.kind == PadPick::kThing && pick.id >= 0 && level && std::size_t(pick.id) < level->npcs.size() && level->npcs[std::size_t(pick.id)].root == "objects";
    const std::string_view verb = pick.kind == PadPick::kMonster ? "Attack" : pick.kind == PadPick::kItem || corpse ? "Pick up" : object || portal ? "Use" : "Talk";
    const auto& primary = pad_config.bindings[pad::Action::Primary];   // its first button bound
    const auto button = primary[0] != pad::Bindings::kNone ? primary[0] : primary[1];
    if (button == pad::Bindings::kNone) return;
    const std::string text = std::string(pad::label(pad_family, button)) + "  " + std::string(verb);
    const auto& font = scene->font_chat.line_height() > 0 ? scene->font_chat : scene->font;
    const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, (int(kScreenWidth) - font.measure(text)) / 2, int(kScreenHeight) - 0x70, text, 199, 179, 119);
}

// An event message's line (FUN_0049eb10, by +1; the strings by id), or
// "" for one d2d doesn't show. The party's (7, FUN_0049e8f0) name the
// player of +3; a death (6) names the killer by +7: 0 a player (the
// second name), 1 a monster (MonStats NameStr of +3; a SuperUnique's,
// +0x18 its row, when set), 2 an object (an Exploding Barrel, 0xb).
// ponytail: 0xb / 0xf / 0x11 / 0x12 (realm news, SOJ, Diablo Walks the
// Earth) aren't shown; nor an object's own name in a death.
auto Town::event_text(const NetGame::Event& event) const -> std::string {
    auto text = [&](std::uint16_t id) { return string_id(*scene, id); };
    auto format = [](std::string pattern, std::initializer_list<std::string> values) {
        for (const auto& value : values)
            if (const auto found = pattern.find("%s"); found != std::string::npos) pattern.replace(found, 2, value);
        return pattern;
    };
    switch (event.event) {
    case 0: return event.name + text(0xe37);                   // " dropped due to timeout."
    case 1: return event.name + text(0xe38);
    case 2:                                                    // joined (not us)
        if (event.name == character.name) return {};
        return event.second.empty() ? format(text(0xe39), { event.name }) : format(text(0xe3b), { event.name, event.second });
    case 3: return event.second.empty() ? format(text(0xe3a), { event.name }) : format(text(0xe3c), { event.name, event.second });
    case 4: return event.name + text(0xe3d);                   // " is not in the game."
    case 5: return event.name + text(0xe3e);
    case 6: {
        std::string killer;
        std::uint16_t slain_by = 0xe3f;                              // " was slain by "
        if (event.sub == 0 && !event.second.empty()) { killer = event.second; slain_by = 0xe40; }
        else if (event.sub == 1 && event.second.empty() && event.argument < scene->monsters.types.size()) {
            const auto& key = scene->monsters.types[event.argument].name_key;
            const auto found = lookup_string(*scene, key);
            killer = found ? u16_to_latin1(*found) : key;
        } else if (event.sub == 1 && !event.second.empty() && event.second_id < scene->superuniques.size()) {
            killer = scene->superuniques[event.second_id].name;
            slain_by = 0xe40;
        } else if (event.sub == 2 && event.argument == 0xb) killer = text(0xc98);
        return killer.empty() ? event.name + text(0xe41) : event.name + text(slain_by) + killer;
    }
    case 7: {
        std::string who;
        if (net_game)
            if (const auto found = net_game->units.find(std::uint64_t(event.argument)); found != net_game->units.end()) who = found->second.name;
        static constexpr std::array<std::uint16_t, 11> kParty{ 0x277a, 0x277b, 0xfba, 0xfbb, 0xfbc, 0xfbd, 0xfbe, 0xfbf, 0xfc0, 0x277c, 0x277d };
        if (event.sub < 1 || event.sub > 11 || who.empty()) return {};
        const auto line = text(kParty[std::size_t(event.sub - 1)]);
        if (event.sub <= 2 || event.sub >= 10) return format(line, { who });   // "%s permits you to loot his corpse."
        return event.sub == 8 ? line + who : who + line;      // "You are now allied with " / " has left your party."
    }
    case 8: return event.name + text(0xfcc);                   // " is busy"
    case 9: return text(0xfcf);
    case 0xa: return event.name.empty() ? text(0x1027) : event.name + text(0x1026);
    case 0xd: return event.name + text(0xe44);
    case 0x10: return text(0x2a38);
    default: return {};
    }
}

namespace {
// A portrait's bar: `percent` of `width` filled `rgb`, the rest black.
void fill_bar(std::vector<std::uint8_t>& framebuffer, int x, int top, int width, int height, int percent, const std::array<std::uint8_t, 3>& rgb) {
    const int frame_w = fb_width(framebuffer), filled = x + std::clamp(percent, 0, 100) * width / 100;
    for (int y = std::max(top, 0); y < std::min(top + height, int(kScreenHeight)); ++y)
        for (int column = std::max(x, 0); column < std::min(x + width, frame_w); ++column) {
            auto* pixel = &framebuffer[(std::size_t(y) * std::size_t(frame_w) + std::size_t(column)) * 4];
            for (std::size_t channel = 0; channel < 3; ++channel) pixel[channel] = column < filled ? rgb[channel] : 0;
        }
}
// FUN_00493a00's: green, from under 50 % yellow, under 25 % red.
std::array<std::uint8_t, 3> life_colour(int life) {
    return life < 25 ? std::array<std::uint8_t, 3>{ 0xff, 0, 0 } : life < 50 ? std::array<std::uint8_t, 3>{ 0xc0, 0xc0, 0 } : std::array<std::uint8_t, 3>{ 0, 0x80, 0 };
}
}  // namespace

// The portraits (FUN_00494020): each other player in our party
// (FUN_00493b50), the merc (FUN_00493ce0), then the pets by PetType.txt row
// (icon type 1 FUN_00493e10, 2 FUN_00493eb0, 3 FUN_00493f80), left to right
// from x 15, 0x38 apart: the icon with its bottom at y 0x3c (a player's
// Hireables\<Class>Icon; a merc's / pet's the PetType micon of its MonStats
// class, else baseicon, FUN_00493240), a life bar above it (FUN_00493a00:
// x .. x + 0x2e, y 0xe .. 0x13, filled life % of the way, green (0, 0x80,
// 0) / from under 50 % (0xc0, 0xc0, 0) / under 25 % red, the rest black;
// icon type 2 only for one with drawhp), icon type 2's count in Font6 at
// (x + 8, baseline 0x37), and the name (the player's, the merc's, the
// PetType name) in Font6, cut to 0x42 px, centred on x + 0x16, its baseline
// at y 0x48 and 0x52 by turns (FUN_00492fa0). None while a left panel is
// open (FUN_0045ae90 2 / 3 (?)).
// The bars are mode 5 (as is); Z (Show Portraits, DAT_007beecc) hides them.
// The first portraits of an expansion game, the hint armed (FUN_004939b0):
// the Hireling screen opens once (UI 0x24; with no merc it shuts again, the
// hint spent all the same) and d2d.cfg's popup_hireling (game.exe: the
// registry's PopupHireling) is 1.
// A joined game's summons are the host's: its pet list (0x7a), ours by owner.
// ponytail: their life is the unit's (0x0c hits), not the list's +0x1c,
// whose sender isn't traced.
auto Town::draw_portraits(std::vector<std::uint8_t>& framebuffer, bool left_open) -> void {
    portraits_shown = false;
    merc_portrait_x = -1;                                             // none to hover or drop on until drawn
    if (left_open || portraits_hidden || coop_hud_hidden()) { merc_hovered = false; merc_hover_at = 0; return; }   // local co-op's: draw_coop_portraits
    const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    const auto& font = scene->font_tiny.line_height() > 0 ? scene->font_tiny : scene->font;
    int x = 15, name_y = 0x48;
    // Each icon at draw mode 1 (FUN_004f6480: half over what's there); the
    // merc's at mode 7 while hovered (its table untraced, lighting.md: drawn as is).
    const auto portrait = [&](const d2d::dc6::Sprite* icon, int life, std::string name, int count, bool lit = false) {
        if (icon && icon->total_frames() > 0) {
            const auto& frame = icon->frame(0, 0);
            const int top = 0x3c - int(frame.height) + 1, frame_w = fb_width(framebuffer);
            for (std::uint32_t row = 0; row < frame.height; ++row)
                for (std::uint32_t column = 0; column < frame.width; ++column) {
                    const int pixel_x = x + int(column), pixel_y = top + int(row);
                    const auto index = frame.pixels[row * frame.width + column];
                    if (index == 0 || pixel_x < 0 || pixel_x >= frame_w || pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
                    const auto colour = pal[index];
                    auto* pixel = &framebuffer[(std::size_t(pixel_y) * std::size_t(frame_w) + std::size_t(pixel_x)) * 4];
                    pixel[0] = lit ? colour.r : std::uint8_t((pixel[0] + colour.r) / 2);
                    pixel[1] = lit ? colour.g : std::uint8_t((pixel[1] + colour.g) / 2);
                    pixel[2] = lit ? colour.b : std::uint8_t((pixel[2] + colour.b) / 2);
                }
        }
        if (life >= 0) fill_bar(framebuffer, x, 0xe, 0x2e, 5, std::clamp(life, 0, 100), life_colour(std::clamp(life, 0, 100)));
        if (count > 0) {
            auto text = std::to_string(count);
            while (!text.empty() && font.measure(text) > 0x2e) text.pop_back();
            font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, x + 8, 0x37 - font.line_height() + 1, text);
        }
        while (!name.empty() && font.measure(name) > 0x42) name.pop_back();
        font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, std::max(x + 0x16 - font.measure(name) / 2, 0), name_y - font.line_height() + 1, name);
        name_y = name_y == 0x48 ? 0x52 : 0x48;
        x += 0x38;
        portraits_shown = true;
    };
    if (net_game)
        if (const auto self = net_game->units.find(std::uint64_t(net_game->self_id)); self != net_game->units.end() && self->second.party != 0xffff)
            for (const auto& [unit_key, unit] : net_game->units) {
                if (unit.type != 0 || unit.id == net_game->self_id || unit.party != self->second.party || unit.cls < 0 || unit.cls > 6) continue;
                portrait(&scene->class_icons[std::size_t(unit.cls)], unit.party_life, unit.name, 0);
            }
    const auto& game_data = *world.game_data;
    const auto icon_of = [&](const GameData::PetType& type, int monster_class) {
        for (std::size_t i = 0; i < type.class_icons.size(); ++i)
            if (type.class_icons[i].first == monster_class && !type.class_icons[i].second.empty()) return scene->hireable_icon(type.class_icons[i].second);
        return scene->hireable_icon(type.base_icon);
    };
    const auto gone = [](std::string_view mode) { return mode == "DT" || mode == "DD"; };
    if (view.merc && !gone(view.merc->mode))
        if (const auto* type = game_data.pet_type("hireable")) {
            merc_portrait_x = x; merc_portrait_y = name_y;
            portrait(icon_of(*type, view.merc_sheet ? view.merc_sheet->cls : -1),
                     view.merc_sheet ? int(std::int64_t(view.merc_sheet->life) * 100 / std::max(view.merc_sheet->max_life, 1)) : -1, merc_label, 0, merc_hovered);
        }
    for (const auto& type : game_data.pet_types) {
        if (type.icon_type < 1 || type.icon_type > 3) continue;
        const View::Pet* first = nullptr;
        int count = 0;
        if (!net_game)                                                // a joined game's: the host's list only
            for (const auto& pet : view.pets)
                if (pet.pettype == type.type && !gone(pet.mode)) { if (!first) first = &pet; ++count; }
        int first_class = first ? first->npc.hc_idx : -1, first_life = first ? first->life : -1;
        if (net_game) {
            const auto row = int(&type - game_data.pet_types.data());
            for (const auto& pet : net_game->pets) {
                if (pet.owner != net_game->self_id || pet.pettype != row) continue;
                const auto unit = net_game->units.find(std::uint64_t(1) << 32 | pet.id);
                if (count++ == 0) { first_class = pet.cls; first_life = unit == net_game->units.end() ? 100 : unit->second.life * 100 / 128; }
            }
        }
        if (count == 0) continue;
        auto name = lookup_string(*scene, std::string_view(type.name));
        portrait(icon_of(type, first_class), type.icon_type != 2 || (count == 1 && type.draw_life) ? first_life : -1,
                 name ? u16_to_latin1(*name) : std::string{}, type.icon_type == 2 ? count : 0);
    }
    if (hireling_hint && character.expansion && !hireling.open) {
        hireling_hint = false;
        hireling.open = true;
        if (!cfg_file.empty() && !d2d::userdir::save_cfg(cfg_file, { { "popup_hireling", "1" } })) d2d::log::warn("d2d.cfg: popup_hireling not saved");
    }
    // The merc's hint (FUN_00494020's tail): 0x58a3 "Right-click to Open
    // Inventory (%s)" with the Hireling key (FUN_0046a530: action 0x36), 0xf9e
    // "\n", 0x58a2 "Drop Potion on Portrait to Heal"; FUN_00502280 at
    // (x, y + 0x24), last line on top.
    if (merc_hovered && merc_portrait_x >= 0) {
        auto key = keys::key_of(key_table, keys::kHireling, 1);
        if (key == keys::kNone) key = keys::key_of(key_table, keys::kHireling, 0);
        const auto id = keys::name_id(key);
        const std::string key_name = key == keys::kNone ? std::string{} : id ? string_id(*scene, id) : std::string(1, char(key & 0xff));
        auto open = string_id(*scene, 0x58a3);
        if (const auto spot = open.find("%s"); spot != std::string::npos) open.replace(spot, 2, key_name);
        draw_hover_text(framebuffer, *scene, { { string_id(*scene, 0x58a2), kTxtWhite }, { std::move(open), kTxtWhite } }, merc_portrait_x, merc_portrait_x,
                        merc_portrait_y + 0x24, merc_portrait_y + 0x24);
    }
}

// Local co-op's portraits (d2d's own): every player, the first first, one
// under the other down the frame's left edge (widescreen's spare width when
// there is some, clear of the 800x600 UI), each a life and a mana bar, the
// class icon and the name in the seat's colour. Room to their right for
// more. Under a left panel (no spare width) they don't show.
auto Town::draw_coop_portraits(std::vector<std::uint8_t>& framebuffer, bool left_open) -> void {
    if (!coop_hud_hidden() || portraits_hidden) return;
    const int frame_w = fb_width(framebuffer), spare = (frame_w - int(kScreenWidth)) / 2;
    if (left_open && spare < kCoopPortraitX + kCoopPortraitWidth) return;
    using namespace d2d::d2s;
    const auto percent = [](const Stats& stats, int now, int most) {
        return int(std::clamp<std::int64_t>(stats.fixed(now) * 100 / std::max<std::int64_t>(stats.fixed(most), 1), 0, 100));
    };
    const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    const auto& font = scene->font_tiny.line_height() > 0 ? scene->font_tiny : scene->font;
    constexpr int kBarWidth = 0x2e;
    const int x = kCoopPortraitX, bar_x = x + (kCoopPortraitWidth - kBarWidth) / 2;
    int top = 8;
    for (const auto& member : world.players) {
        const int cls = int(member->character.header.cls);
        if (cls < 0 || cls > 6) continue;
        const auto& stats = member->character.stats;
        const int life = percent(stats, kLife, kMaxLife);
        fill_bar(framebuffer, bar_x, top, kBarWidth, 5, life, life_colour(life));
        fill_bar(framebuffer, bar_x, top + 6, kBarWidth, 3, percent(stats, kMana, kMaxMana), { 0x20, 0x40, 0xe0 });
        int below = top + 12;
        if (const auto& icon = scene->class_icons[std::size_t(cls)]; icon.total_frames() > 0) {
            const auto& frame = icon.frame(0, 0);
            blit_sprite(framebuffer, frame, pal, x + (kCoopPortraitWidth - int(frame.width)) / 2, below);
            below += int(frame.height);
        }
        const auto seat = std::ranges::find(coop, member->id, &CoopPlayer::id);
        const auto& colour = kSeatColours[std::size_t(std::clamp(seat == coop.end() ? lead_colour : seat->colour, 0, 3))];
        std::string name = member->character.name;
        while (!name.empty() && font.measure(name) > kCoopPortraitWidth) name.pop_back();
        font.draw_tinted(framebuffer, std::uint32_t(frame_w), kScreenHeight, pal, x + (kCoopPortraitWidth - font.measure(name)) / 2, below + 2, name, colour[0], colour[1], colour[2]);
        top = below + 2 + font.line_height() + 10;
    }
}

}  // namespace d2d::client
