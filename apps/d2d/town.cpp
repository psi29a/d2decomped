// SPDX-License-Identifier: GPL-3.0-or-later
// The in-game client (town.hpp): Town, the view to units, the frame's
// light, the HUD overlays.
#include "town.hpp"

#include "audio.hpp"
#include "common.hpp"
#include "cursor.hpp"
#include "ingame.hpp"
#include "panels.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include "skilltree.hpp"
#include "speech_sound.hpp"
#include "store.hpp"
#include "ui.hpp"
#include "world_view.hpp"

#include <d2gs/c2s.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <light.hpp>
#include <log.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <sound_ids.hpp>
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
#include <span>
#include <string>
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
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
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
        unit.cmap = scene.item_map(ground_item.item, false);   // FUN_00471ec0: the character's colours
        unit.rgb = ground_item.rgb;
        out.push_back(unit);
    }
    for (const auto& fire : view.fires) out.push_back({ fire.x, fire.y, &scene.npc_anim(*fire.npc, fire.npc->mode), 0, nullptr, 0, -2 });
    // The player's corpses: lying (DT's last frame, as the dead player is
    // drawn), in what they wore; named by the player (-3000 - which).
    for (const auto& corpse : view.corpses) {
        const auto& anim = scene.composite(cls, kModeDT, corpse.gfx);
        out.push_back({ corpse.x, corpse.y, &anim, corpse.dir, corpse_name, now_ms - (anim.length_ms() - 1), -3000 - corpse.which });
    }
    // Town portals: opening (OP, FrameCnt1 15 at FrameDelta 200/256 a tick:
    // 768 ms), then ON; named by where they lead (-2000 - which). Tristram
    // Cain's (which 4, object 189: its token tP is TP's files, MPQ names
    // being case-blind) has no name: Selectable0..2 all 0.
    for (const auto& portal : view.portals) {
        const Level* destination = portal.which < 4 ? scene.level(portal.destination) : nullptr;
        const bool opening = now_ms - portal.born < kPortalOpenMs;
        out.push_back({ portal.x, portal.y, &scene.npc_anim(scene.town_portal, opening ? "OP" : "ON"), 0, destination ? &destination->name : nullptr,
                        opening ? portal.born : portal.born + kPortalOpenMs, -2000 - portal.which });
        out.back().shadow = false;
    }
    if (view.merc)
        out.push_back({ view.merc->unit.x, view.merc->unit.y, &scene.npc_anim(*view.merc->npc, view.merc->mode), view.merc->unit.dir,
                        view.merc->mode == "DT" ? nullptr : merc_label, view.merc->unit.mode_ms, -2 });
    for (const auto& pet : view.pets) out.push_back({ pet.unit.x, pet.unit.y, &scene.npc_anim(pet.npc, pet.mode), pet.unit.dir, nullptr, pet.unit.mode_ms, -3 });
    // A joined game's other players, in their class's bare look (the items
    // they wear, 0x9d, aren't read yet). ponytail: their look and modes past walk.
    const bool in_town = view.level && view.level->id == 1;
    for (const auto& other : view.others) {
        if (!in_view(other.unit.x, other.unit.y)) continue;
        const int mode = other.unit.walking ? (in_town ? kModeTW : kModeWL) : (in_town ? kModeTN : kModeNU);
        out.push_back({ other.unit.x, other.unit.y, &scene.composite(other.cls, mode, GameData::Appearance{}), other.unit.dir, &other.name, other.unit.mode_ms, -4 });
    }
    auto shot = [&](const View::Shot& shot_state) {
        Unit unit{ shot_state.x, shot_state.y, nullptr, shot_state.dir, nullptr, shot_state.born, -1 };
        unit.missile = shot_state.info;
        out.push_back(unit);
    };
    for (const auto& shot_state : view.missiles) shot(shot_state);
    for (const auto& shot_state : effects) shot(shot_state);                 // the client's own (the Den's light beams)
    for (std::size_t i = 0; i < view.monsters.size(); ++i) {
        const auto& monster = view.monsters[i];
        if (monster.corpse_used || !in_view(monster.unit.x, monster.unit.y)) continue;
        out.push_back({ monster.unit.x, monster.unit.y, &scene.npc_anim(monster.npc, monster.mode), monster.unit.dir, monster.alive() ? &monster.npc.name : nullptr, monster.unit.mode_ms, -10 - int(i) });
        out.back().overlay_class = monster.npc.overlay_class;
        out.back().cmap = scene.monster_map(monster.npc);
        out.back().ghostly = monster.alive() && std::ranges::find(monster.mods, d2d::rules::umod::ghostly) != monster.mods.end();
        dress(scene, out.back(), monster.id, monster_states(scene, monster, now_ms, view.aura), clk);
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
    if (view.pmode == kModeDD) {
        const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
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
                     std::span<const Unit> units , const Unit* player_look , std::uint32_t now) {
    Lighting lighting;
    if (!view.level || scene.act1_lit[31].entries().empty()) return lighting;
    lighting.pal = &scene.act1_lit;
    // The grid covers the view's corners (half the width in cells over 80
    // plus half the height over 40, halved, in subtiles) and the widest light
    // (18) past them, not game.exe's 48 (bugs.md #11).
    const int half = (int(kScreenWidth) / 2 / (kIsoW / 2) + (int(kScreenHeight) / 2 + kIsoH) / (kIsoH / 2)) * 5 / 2 + 18 + 1;
    if (ambient < 0) ambient = view.level->light >= 0 ? view.level->light : view.day.intensity();
    lighting.grid.reset(int(cam_x * 5), int(cam_y * 5), ambient, half * 2);
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
        if (shadowed) lighting.grid.stamp_shadowed(int(x * 40), int(y * 40), radius8, 255);
        else lighting.grid.stamp(int(x * 40), int(y * 40), radius8, 255);
    };
    auto stamp = [&](float x, float y, int radius, bool shadowed) { stamp8(x, y, std::min(radius, 18) * 8, shadowed); };
    auto lamp = [&](std::uint64_t key, float x, float y, int radius, bool shadowed, int first = -1) {
        stamp8(x, y, eased(key, radius, first < 0 ? radius : first), shadowed);
    };
    lamp(1, cam_x, cam_y, std::max(0, 13 + view.light_bonus), true);   // FUN_00460930: 13 + the bonus, capped at 18
    static constexpr std::array<std::string_view, 8> kModes{ "NU", "OP", "ON", "S1", "S2", "S3", "S4", "S5" };
    auto lit = [&](const Npc& npc, std::string_view mode) {
        const auto found = std::ranges::find(kModes, mode.empty() ? std::string_view(npc.mode) : mode);
        return npc.root == "objects" && found != kModes.end() ? int(npc.lit[std::size_t(found - kModes.begin())]) : 0;
    };
    for (std::size_t i = 0; i < view.level->npcs.size(); ++i) {
        const auto& npc = view.level->npcs[i];
        const auto* state = i < view.npc_states.size() ? &view.npc_states[i] : nullptr;
        if (state && state->hidden) continue;
        lamp(2ull << 32 | i, state ? state->x : npc.x, state ? state->y : npc.y, lit(npc, state ? state->mode : std::string_view{}), true);
    }
    for (const auto& neighbour : view.level->nearby)                  // the torches over the level's edge
        for (const auto& npc : neighbour.level->npcs) stamp(npc.x + float(neighbour.dx), npc.y + float(neighbour.dy), lit(npc, {}), true);
    for (const auto& monster : view.monsters) if (monster.alive()) lamp(4ull << 32 | std::uint32_t(monster.id), monster.unit.x, monster.unit.y, monster.npc.light, false);
    for (const auto& portal : view.portals)                        // Lit1 (OP) while it opens, then Lit2 (ON)
        lamp(5ull << 32 | std::uint32_t(portal.which), portal.x, portal.y, int(scene.town_portal.lit[now - portal.born < kPortalOpenMs ? 1 : 2]), true);
    for (const auto& missile : view.missiles) if (missile.info) stamp(missile.x, missile.y, missile.info->light, false);
    for (const auto& missile : effects) if (missile.info) stamp(missile.x, missile.y, missile.info->light, false);
    // States' overlays light their unit (Overlay.txt: InitRadius growing to
    // Radius, FUN_00474160 / FUN_00474290); a plain light, its colour
    // dropped like every light's here.
    auto overs = [&](const Unit& unit, float x, float y) {
        for (std::size_t k = 0; k < unit.overs.size(); ++k) {
            const auto& overlay = unit.overs[k];
            if (overlay.overlay->radius > 0 && (!overlay.once || (now - overlay.start) * std::uint32_t(std::max(overlay.overlay->rate, 1)) / 640 < std::uint32_t(overlay.overlay->frames)))
                lamp(6ull << 56 ^ std::uint64_t(reinterpret_cast<std::uintptr_t>(overlay.overlay)) ^ std::uint64_t(std::uint32_t(unit.npc)) << 40 ^ overlay.start,
                     x, y, overlay.overlay->radius, false, overlay.overlay->init_radius);
        }
    };
    for (const auto& each_unit : units) overs(each_unit, each_unit.x, each_unit.y);
    if (player_look) overs(*player_look, cam_x, cam_y);
    std::erase_if(eases, [&](const auto& entry) { return now - entry.second.seen > 2000; });
    return lighting;
}

// Town
auto Town::publish() -> void {
        auto out = world.view();
        out.sounds = std::move(world.cues.due);
        world.cues.due.clear();
        out.events = std::move(world.events);
        world.events.clear();
        net.to_client = encode_view(*scene, out, view_enc);
        // D2D_VIEW_STATS=1: each View's size in the log (the replication budget).
        if (std::getenv("D2D_VIEW_STATS")) d2d::log::info("VIEWSTAT total={} monsters={} ground={} items={}", net.to_client.size(), out.monsters.size(), out.ground.size(), out.items.size());
        if (!apply_view(*scene, net.to_client, view)) {         // lost track: the next one is whole
            d2d::log::warn("a View didn't apply ({} bytes)", net.to_client.size());
            view_enc.reset();
            return;
        }
        level = view.level;
        if (net_game) net_overlay();
        cues.due.insert(cues.due.end(), view.sounds.begin(), view.sounds.end());
        if (!view.has_character) return;
        character.header = view.header; character.stats = view.stats; character.items = view.items;
        character.expansion = view.header.expansion();
        character.panel = panel_stats(*scene, character.header, character.items, character.stats);
        character.panel.attack = view.attack_lines;
        // A quest's log state sent (S->C 0x5d, FUN_004a2cb0): the Quest Log
        // button, or the open log picks it.
        // ponytail: a change in the View's log state stands in for each send
        // (FUN_00544190); none at the game's first View (the join sends none).
        for (std::size_t quest = 1; quest < view.quest_log.size(); ++quest)
            if (quest_log.sent_known && view.quest_log[quest] && view.quest_log[quest] != quest_log.sent[quest]) quest_log_notify(quest_log, int(quest));
        quest_log.sent = view.quest_log;
        quest_log.sent_known = true;
        held = view.held;
        if (view.store) {
            const auto keep = store;
            store = *view.store;
            if (keep.npc == store.npc) { store.tab = keep.tab; store.mode = keep.mode; store.pressed = keep.pressed; }
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
        automap.cells.clear();                    // a new game: what the files beside the save kept
        automap.placed.clear();
        automap.revealed.clear();
        other_automaps.clear();
        quest_log = {};                           // done animations play again in a new game
        mini = {};                                // open (FUN_004567f0; ponytail: not the "Mini Panel" registry value)
        game_menu.open = false;
        world.enter(character);
        skillbar.new_game();
        publish();
        if (scene && level) load_automap(automap, automap_dir(world), character.name, scene->map_seed, level->layer);
    }

// The automap's new cells go out with the save, as on leaving a game
// (FUN_0045a5c0).
auto Town::save() -> std::string {
        if (scene && level) save_automap(automap, automap_dir(world), character.name, scene->map_seed, level->layer);
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
// ponytail: NPC talk and trade stay local (the host's gold and items don't
// follow); belt, item moves, stat and skill points aren't sent (M9).
auto Town::send(const Command& command) -> void {
        if (!net_game || !level) { net.send(command); return; }
        auto subtile_x = [&](float cell_x) { return (cell_x + float(level->world_x)) * 5.f; };
        auto subtile_y = [&](float cell_y) { return (cell_y + float(level->world_y)) * 5.f; };
        if (const auto* move = std::get_if<cmd::Move>(&command)) {
            net_game->move_to(subtile_x(move->x), subtile_y(move->y), view.running);
        } else if (const auto* run = std::get_if<cmd::Run>(&command)) {
            net_game->set_running(run->running);
        } else if (const auto* use = std::get_if<cmd::UseSkill>(&command)) {
            const auto* skill = scene->skills.get(use->skill);
            const bool melee = skill && !self_cast(*skill) && !world.fight.missile_skill(*skill) && !Fight::spot_skill(*skill) && !world.fight.summon_skill(*skill);
            const auto target = use->unit >= 0 ? net_monsters.find(std::uint32_t(use->unit)) : net_monsters.end();
            if (target != net_monsters.end()) {
                // The host drops a skill that comes mid-swing (FUN_0057edd0:
                // modes A1 / A2 / SC / TH take one only once FUN_0057ed70 says
                // the swing can end), so repeats are harmless; the same
                // target again once the swing here is over, as game.exe's
                // client, which runs its own copy of the mode.
                const bool again = use->unit == net_attack && use->skill == net_attack_skill;
                if (!again || world.fight.pmode < 0) net_game->skill_on(use->skill, use->left, 1, std::uint32_t(use->unit));
                net_attack = use->unit;
                net_attack_skill = use->skill;
                const auto& monster = target->second;
                if (melee) {
                    if (std::hypot(monster.unit.x - world.player.x, monster.unit.y - world.player.y) > 1.6f) net.send(cmd::Move{ monster.unit.x, monster.unit.y, use->left });
                    else world.display_swing(use->skill, monster.unit.x, monster.unit.y, world_ms);
                    return;
                }
                net.send(cmd::UseSkill{ use->skill, monster.unit.x, monster.unit.y, -1, use->left });   // the missile flies at it here, for show
                return;
            }
            net_attack = -1;
            net_game->skill_at(use->skill, use->left, subtile_x(use->x), subtile_y(use->y));
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
                net_trade_pending = int(vendor->id);
                net_trade_gamble = trade->gamble;
                net_trade_ms = frame_now;
            } else {
                net_game->log.note("no host NPC near " + npc.name + " to trade with");
            }
        } else if ((std::holds_alternative<cmd::Buy>(command) || std::holds_alternative<cmd::Sell>(command) || std::holds_alternative<cmd::Repair>(command))
                   && !net_game->trade_npc) {
            net_game->log.note("a trade with no host NPC: not made (d2d's own store would make it here only)");
            return;
        } else if (const auto* buy = std::get_if<cmd::Buy>(&command); buy && net_game->trade_npc) {
            const auto& tab = world.store.tabs[std::size_t(std::clamp(buy->tab >= 0 ? buy->tab : store.tab, 0, 3))];
            if (buy->stock >= 0 && std::size_t(buy->stock) < tab.size()) {
                const auto& item = tab[std::size_t(buy->stock)];
                const int cost = d2d::rules::item_price(scene->rules, item, world.store.npc_id, false, world.store.header);
                net_game->buying = true;
                net_game->send_items({ d2d::net::d2gs::c2s::buy(net_game->trade_npc, std::uint32_t(item.id), 0, std::uint32_t(cost)) });
            }
            return;                                         // the host's item comes to us; its gold change too
        } else if (const auto* sell = std::get_if<cmd::Sell>(&command); sell && net_game->trade_npc) {
            auto found = std::ranges::find(world.character.items, sell->item, &d2d::d2s::Item::id);
            const d2d::d2s::Item* item = found != world.character.items.end() ? &*found : world.held && world.held->id == sell->item ? &*world.held : nullptr;
            if (const std::uint32_t id = item ? net_game->host_item(*item) : 0) {
                const int cost = d2d::rules::item_price(scene->rules, *item, world.store.npc_id, true, world.store.header);
                net_game->send_items({ d2d::net::d2gs::c2s::sell(net_game->trade_npc, id, 0, std::uint32_t(cost)) });
                if (found != world.character.items.end()) world.character.items.erase(found);
                else world.held.reset();
                net_cursor = 0;
            }
            return;                                         // gold from the host
        } else if (std::holds_alternative<cmd::CloseTrade>(command) && net_game->trade_npc) {
            net_game->send_items({ d2d::net::d2gs::c2s::npc_chat(false, 1, net_game->trade_npc) });
            net_game->trade_npc = 0;
            net_game->store_items.clear();
        } else if (std::holds_alternative<cmd::UseItem>(command) || std::holds_alternative<cmd::UseBelt>(command) || std::holds_alternative<cmd::ToCursor>(command)
                   || std::holds_alternative<cmd::ToGrid>(command) || std::holds_alternative<cmd::ToBody>(command) || std::holds_alternative<cmd::ToBelt>(command)
                   || std::holds_alternative<cmd::Drop>(command)) {
            net_items(command);
        } else if (const auto* interact = std::get_if<cmd::Interact>(&command); interact && interact->npc >= 0 && std::size_t(interact->npc) < level->npcs.size()) {
            const auto& npc = level->npcs[std::size_t(interact->npc)];
            // The host walks us there only for a skill: d2d walks us up and
            // the host's player follows (walk()); it talks only within 6
            // subtiles (FUN_00548b00 case 1), so the trade's 0x13 comes again
            // once the menu's up (OpenTrade).
            if (npc.root == "objects") {
                if (const auto* object = net_game->nearest(2, -1, subtile_x(npc.x), subtile_y(npc.y), 4.f)) { net_operate_type = 2; net_operate = object->id; net_operate_ms = frame_now; }
            } else if (const auto* host_npc = net_npc(interact->npc)) {
                net_game->interact(1, host_npc->id);          // the host walks us up too (talk at 6)
            }
        } else if (interact && interact->npc <= -3000 && std::size_t(-3000 - interact->npc) < world.corpses.size()) {
            // Our corpse: the host's corpse unit (0x8e) nearest d2d's, its
            // items back as the host hands them (FUN_00548b00 case 0).
            const auto& corpse = world.corpses[std::size_t(-3000 - interact->npc)];
            float best = 10.f;
            for (const std::uint32_t id : net_game->corpses)
                if (const auto unit = net_game->units.find(id); unit != net_game->units.end())
                    if (const float apart = std::hypot(unit->second.x - subtile_x(corpse.x), unit->second.y - subtile_y(corpse.y)); apart < best) {
                        best = apart; net_operate_type = 0; net_operate = id; net_operate_ms = frame_now;
                    }
        } else if (interact && interact->npc <= -2000 && interact->npc > -2004) {
            // A town portal: ours by the host's id for this end (0x82), else its
            // TownPortal (object 59) nearest d2d's (they open apart:
            // deviations.md); operated once the host has us there, as it
            // warps us (0x15); d2d takes its own here.
            const int which = -2000 - interact->npc;
            if (which < 2 && net_game->portal_here != 0) {
                net_operate_type = 2; net_operate = net_game->portal_here; net_operate_ms = frame_now;
            } else if (const auto& entry = world.portal[std::size_t(which)]; entry.level) {
                if (const auto* host_portal = net_game->nearest(2, 59, (entry.x + float(entry.level->world_x)) * 5.f, (entry.y + float(entry.level->world_y)) * 5.f, 10.f)) {
                    net_operate_type = 2; net_operate = host_portal->id; net_operate_ms = frame_now;
                } else {
                    net_game->log.note(std::format("no host portal near ({:.0f}, {:.0f})", (entry.x + float(entry.level->world_x)) * 5.f, (entry.y + float(entry.level->world_y)) * 5.f));
                }
            }
        } else if (std::holds_alternative<cmd::Resurrect>(command)) {
            net_game->send_items({ d2d::net::d2gs::c2s::resurrect() });   // the host respawns us in town (0x15)
        } else if (const auto* travel = std::get_if<cmd::Waypoint>(&command); travel && travel->npc >= 0 && std::size_t(travel->npc) < level->npcs.size()) {
            const auto& npc = level->npcs[std::size_t(travel->npc)];
            if (const auto* object = net_game->nearest(2, -1, subtile_x(npc.x), subtile_y(npc.y), 4.f)) {
                net_game->interact(2, object->id);
                net_game->waypoint(object->id, travel->level);
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
auto Town::net_items(const Command& command) -> void {
        using namespace d2d::d2s;
        namespace c2s = d2d::net::d2gs::c2s;
        auto local = [&](int id) -> const Item* {
            const auto found = std::ranges::find(world.character.items, id, &Item::id);
            return found == world.character.items.end() ? nullptr : &*found;
        };
        auto host = [&](const Item* item) -> std::uint32_t {
            const std::uint32_t id = item ? net_game->host_item(*item) : 0;
            if (item && !id) net_game->log.note("no host id for our " + item->code + " (not sent)");
            return id;
        };
        const std::uint32_t held_id = world.held ? (net_cursor ? net_cursor : net_game->host_item(*world.held)) : 0;
        if (const auto* use = std::get_if<cmd::UseItem>(&command)) {
            const auto* item = local(use->item);
            if (const auto id = host(item); id && item->location == item_location::kBelt) net_game->send_items({ c2s::use_belt(id) });
            else if (id) net_game->send_items({ c2s::use_item(id, std::uint16_t((world.player.x + float(level->world_x)) * 5.f), std::uint16_t((world.player.y + float(level->world_y)) * 5.f)) });
        } else if (const auto* belt_use = std::get_if<cmd::UseBelt>(&command)) {
            const auto found = std::ranges::find_if(world.character.items, [&](const Item& item) { return item.location == item_location::kBelt && item.column == belt_use->slot; });
            if (found != world.character.items.end()) if (const auto id = host(&*found)) net_game->send_items({ c2s::use_belt(id) });
        } else if (const auto* to_cursor = std::get_if<cmd::ToCursor>(&command)) {
            const auto* item = local(to_cursor->item);
            if (const auto id = host(item)) {
                if (item->location == item_location::kEquipped) net_game->send_items({ c2s::body_to_cursor(std::uint16_t(item->slot)) });
                else net_game->send_items({ c2s::item_id_packet(item->location == item_location::kBelt ? 0x24 : 0x19, id) });
                net_cursor = id;
            }
        } else if (const auto* to_grid = std::get_if<cmd::ToGrid>(&command); to_grid && held_id) {
            // What's under the held item's footprint there: a swap (0x1f).
            const auto info = scene->rules.item_info.find(world.held->code);
            const int width = info != scene->rules.item_info.end() ? info->second.width : 1, height = info != scene->rules.item_info.end() ? info->second.height : 1;
            const Item* under = nullptr;
            for (const auto& item : world.character.items) {
                if (item.location != item_location::kStored || item.panel != to_grid->panel) continue;
                const auto item_size = scene->rules.item_info.find(item.code);
                const int item_w = item_size != scene->rules.item_info.end() ? item_size->second.width : 1, item_h = item_size != scene->rules.item_info.end() ? item_size->second.height : 1;
                if (item.column < to_grid->col + width && to_grid->col < item.column + item_w && item.row < to_grid->row + height && to_grid->row < item.row + item_h) { under = &item; break; }
            }
            const std::uint32_t buffer = to_grid->panel == item_panel::kStash ? 4 : to_grid->panel == item_panel::kCube ? 3 : 0;
            if (under) { if (const auto target = host(under)) net_game->send_items({ c2s::swap_grid(held_id, target, std::uint32_t(to_grid->col), std::uint32_t(to_grid->row)) }); net_cursor = net_game->host_item(*under); }
            else { net_game->send_items({ c2s::cursor_to_grid(held_id, std::uint32_t(to_grid->col), std::uint32_t(to_grid->row), buffer) }); net_cursor = 0; }
        } else if (const auto* to_body = std::get_if<cmd::ToBody>(&command); to_body && held_id) {
            const auto worn = std::ranges::find_if(world.character.items, [&](const Item& item) { return item.location == item_location::kEquipped && item.slot == to_body->slot; });
            const bool swap = worn != world.character.items.end();
            net_game->send_items({ c2s::equip(held_id, std::uint32_t(to_body->slot), swap) });
            net_cursor = swap ? net_game->host_item(*worn) : 0;
        } else if (const auto* to_belt = std::get_if<cmd::ToBelt>(&command); to_belt && held_id) {
            net_game->send_items({ c2s::cursor_to_belt(held_id, std::uint32_t(to_belt->box)) });
            net_cursor = 0;
        } else if (std::holds_alternative<cmd::Drop>(command) && held_id) {
            net_game->send_items({ c2s::item_id_packet(0x17, held_id) });
            net_cursor = 0;
        }
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
        for (const auto& [unit_key, unit] : net_game->units) {
            if (unit.type == 0) {
                if (unit.id == net_game->self_id) continue;
                View::OtherPlayer other{ .cls = unit.cls, .name = unit.name };
                other.unit.x = cell_x(unit.x);
                other.unit.y = cell_y(unit.y);
                other.unit.walking = unit.moving;
                if (unit.moving) other.unit.dir = direction16(unit.goal_x - unit.x, unit.goal_y - unit.y);
                view.others.push_back(std::move(other));
                continue;
            }
            if (unit.id == net_game->merc_id && unit.id != 0) continue;   // ours: the World's merc stands in
            if (unit.type != 1 || unit.cls < 0 || std::size_t(unit.cls) >= scene->monsters.types.size()) continue;   // objects and warps: d2d's own
            if (std::size_t(unit.cls) < scene->mon_is_npc.size() && scene->mon_is_npc[std::size_t(unit.cls)]) continue;
            auto found = net_monsters.find(unit.id);
            if (found == net_monsters.end())
                found = net_monsters.emplace(unit.id, make_monster(*scene, unit.cls, cell_x(unit.x), cell_y(unit.y), rng, net_game->difficulty)).first;
            auto& monster = found->second;
            monster.id = int(unit.id);
            if (unit.moving) monster.unit.dir = direction16(unit.goal_x - unit.x, unit.goal_y - unit.y);
            monster.unit.x = cell_x(unit.x);
            monster.unit.y = cell_y(unit.y);
            monster.unit.walking = unit.moving;
            // The host's unit command as a mode (DAT_006da4d8, net-packets.md
            // "The unit command"): dying plays DT once, then lies DD; an act
            // plays its mode, then stands or walks again.
            // ponytail: an act lasts 600 ms, not its animation's length.
            static constexpr std::array<std::string_view, 0x1e> kCommandMode = {
                "WL", "WL", "", "", "SC", "SC", "GH", "NU", "DT", "DD", "A1", "A1", "S1", "S1", "S2", "S2",
                "A2", "A2", "BL", "", "KB", "SQ", "SQ", "RN", "RN", "S1", "S3", "S3", "S4", "S4" };
            const auto since = net_game->steady_now() - unit.mode_ms;
            const std::string_view commanded = unit.mode >= 0 && std::size_t(unit.mode) < kCommandMode.size() ? kCommandMode[std::size_t(unit.mode)] : "";
            if (unit.mode == 8 || unit.mode == 9) monster.mode = unit.mode == 8 && since < 1500 ? "DT" : "DD";
            else if (unit.life == 0) monster.mode = "DD";
            else if (!commanded.empty() && commanded != "WL" && commanded != "NU" && since < 600) monster.mode = commanded;
            else monster.mode = unit.moving ? "WL" : "NU";
            if (monster.mode != monster.last_mode) monster.unit.mode_ms = world_ms;   // the animation starts over
            monster.last_mode = monster.mode;
            monster.hit_points = unit.life == 0 ? 0 : std::max(1, monster.stats.hit_points * unit.life / 128);
            view.monsters.push_back(monster);
        }
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
        // The host monster being attacked: held, the attack goes on (input).
        if (net_attack >= 0 && view.monster(net_attack) >= 0 && view.monsters[std::size_t(view.monster(net_attack))].alive()) {
            view.attack = net_attack;
            view.attack_skill = net_attack_skill;
        } else {
            net_attack = -1;
        }
    }

auto Town::new_game() -> void {
        world.new_game();
        skillbar.new_game();
    }

auto Town::open_game_menu() -> void {
        game_menu.restore_automap = automap.open;
        game_menu.restore_mini = mini.open;
        automap.open = mini.open = belt_open = false;
        inv_open = char_open = stash_open = cube_open = tree_open = quest_log.open = false;
        game_menu.expansion = character.expansion;
        game_menu.open = true;
        game_menu.show(0);
    }

auto Town::close_game_menu() -> void {
        game_menu.open = false;
        automap.open = game_menu.restore_automap;
        mini.open = game_menu.restore_mini;
    }

auto Town::update(std::vector<std::uint8_t>& framebuffer, const Mouse& frame_mouse, const std::vector<SDL_Keycode>& keys_this_frame,
                Screen& screen, Audio& audio, std::uint32_t frame_ms, std::uint32_t last_ms) -> void {
        now_ms = frame_ms;
        Mouse mouse = frame_mouse;             // the menu / mini-panel take their clicks from the rest
        // ESC handled globally in handle_sdl_events (returns to Title).
        // D2 movement: press or hold the left button on the ground
        // and the character walks toward that point (the target
        // tracks the cursor while held); the camera follows.
        for (const auto key : keys_this_frame) {
            if (game_menu.open && key != SDLK_ESCAPE) continue;   // the menu's input table (FUN_00467a70) has the keys
            if (key == SDLK_I) { inv_open = !inv_open; if (inv_open) tree_open = false; }
            if (key == SDLK_T) { tree_open = !tree_open; if (tree_open) inv_open = false; }   // both right-hand panels
            if (key == SDLK_R) send(cmd::Run{ !view.running });       // D2's run/walk toggle
            skillbar.key(key, mouse.x, mouse.y);                // F1-F8
            if (key == SDLK_GRAVE) belt_open = !belt_open;      // D2's "Show Belt" key
            if (key == SDLK_TAB) automap.open = !automap.open;  // D2's automap toggle
            if (key == SDLK_C) { char_open = !char_open; if (char_open) stash_open = cube_open = quest_log.open = false; }
            if (key == SDLK_Q) toggle_quest_log();
            if (key == SDLK_ESCAPE && view.dead) { send(cmd::Resurrect{}); continue; }
            if (key >= SDLK_1 && key <= SDLK_4) net.send(cmd::UseBelt{ int(key - SDLK_1) });
            if (key == SDLK_ESCAPE && skillbar.picking) { skillbar.picking = 0; continue; }   // the picker first
            // Esc (0x4690b0): the NPC's windows first, then the game menu
            // closes, else every Esc-closable panel at once (FUN_00456300;
            // not the automap or the mini-panel), else the menu opens.
            if (key == SDLK_ESCAPE) {
                if (waypoint.open) waypoint = {};
                else if (store.npc >= 0) { net.send(cmd::CloseTrade{}); inv_open = false; } // the store first
                else if (speech.npc >= 0) { speech = {}; menu_after_speech = -1; }   // then speech
                else if (npc_menu.npc >= 0) npc_menu = {};          // then the menu
                else if (game_menu.open) close_game_menu();
                else if (inv_open || char_open || stash_open || cube_open || tree_open || quest_log.open)   // then panels
                    inv_open = char_open = stash_open = cube_open = tree_open = quest_log.open = false;
                else open_game_menu();
            }
        }
        // The game menu takes every click while it's up; Exit saves and goes
        // to the roster. Its volumes go to d2d.cfg.
        // ponytail: Configure Controls (UI 0xb) isn't drawn; it closes the menu.
        bool hud_click = false;
        if (game_menu.open) {
            const auto action = game_menu.input(*scene, audio, mouse, keys_this_frame, frame_ms);
            if (game_menu.volume_changed && !cfg_file.empty())
                d2d::userdir::save_cfg(cfg_file, { { "master_volume", std::to_string(audio.master_volume) },
                                                   { "music_volume", std::to_string(audio.music_volume) } });
            game_menu.volume_changed = false;
            if (action != GameMenu::kNone) close_game_menu();
            if (action == GameMenu::kExit) { save(); screen = Screen::CharSelect; }
            hud_click = true;
        } else {
            // The mini-panel's buttons (FUN_0047ec50) and the HUD's button for it.
            const bool left_open = char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open;
            int action = -1;
            hud_click = mini.input(*scene, audio, mouse, left_open, inv_open || tree_open, action);
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
        if (inv_open) tree_open = false;       // the stash / a store opened the inventory
        const auto& lay = scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
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
        const bool over_belt =
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
            if (identify_with >= 0) identify_with = -1;                       // a right-click drops the pick
            else if (used != character.items.end() && (used->code == "isc" || used->code == "ibk")) identify_with = used->id;
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
                    cube_open = true; stash_open = char_open = false;
                }
            }
        // An open NPC menu takes every click: an entry runs its action,
        // anything else closes it.
        bool menu_click = false;
        const auto& unit = view.player;
        automap_reveal(*scene, *level, automap, unit.x, unit.y);
        for (const auto& neighbour : level->nearby)                   // what's in view across the edge
            if (neighbour.level->layer == level->layer)
                automap_reveal(*scene, *neighbour.level, automap, unit.x - float(neighbour.dx), unit.y - float(neighbour.dy));
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
            const float dx = cam_x - rain_cam_x, dy = cam_y - rain_cam_y;
            rain_cam_x = cam_x; rain_cam_y = cam_y;
            if (!level->rain) { rain.drops.clear(); rain.splashes.clear(); }
            else rain.tick(rain.rng, int(kScreenWidth), int(kScreenHeight), int(std::lround((dx - dy) * (kIsoW / 2))), int(std::lround((dx + dy) * (kIsoH / 2))),
                           d2d::rules::kDay[std::size_t(view.day.phase)].type);
            const int want = level->rain ? int(rain.volume() * 255.f) : 0;
            rain_vol = rain_vol < want ? std::min(want, rain_vol + 6) : std::max(want, rain_vol - 6);
        }
        if (rain_vol > 0 && audio.rain.sound != d2d::rules::sound_ids::kSceneRain) audio.play(audio.rain, *scene, d2d::rules::sound_ids::kSceneRain);
        if (rain_vol == 0 && audio.rain.sound) audio.stop(audio.rain);
        if (audio.rain.src && scene->sounds.size() > d2d::rules::sound_ids::kSceneRain)
            audio.set_gain(audio.rain, float(scene->sounds[d2d::rules::sound_ids::kSceneRain].volume) / 255.f * float(rain_vol) / 255.f);
        // Every Event Delay ticks, give or take a third, one of the event
        // sounds from the left or right (x +-450..750, y +-100 in game.exe's
        // units; the first within one gap of arriving).
        // ponytail: x becomes pan x / 750, y is dropped.
        {
            const std::uint32_t tick = frame_ms / 40;
            const int event_sound = day ? level->day_event : level->night_event, delay = level->event_delay;
            auto spread = [&](int spread_range) { return sound_rng(2 * spread_range + 1) - spread_range; };
            if (event_sound != amb_event) {
                amb_event = event_sound;
                amb_next = std::uint32_t(std::max(1, delay + spread(delay / 3)));
                amb_last = tick - std::uint32_t(sound_rng(int(amb_next)));
            }
            if (event_sound > 0 && tick - amb_last >= amb_next) {
                const int x = (sound_rng(2) ? 1 : -1) * (450 + sound_rng(301));
                audio.play_sfx(*scene, event_sound, 1.f, sound_rng(16), float(x) / 750.f);
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
            } else if (action == NpcMenuState::kGoEast) {
                net.send(cmd::GoEast{ who });
            } else if (action == NpcMenuState::kImbue) {
                // ponytail: no item panel (0x4b35b0 -> 0x4c0620); it takes
                // the item in hand.
                net.send(cmd::Imbue{ who });
            } else if (action == NpcMenuState::kQuest) {
                speech = start_speech(*scene, who, std::uint16_t(npc_menu_arg), frame_ms);
                net.send(cmd::QuestMessage{ who, npc_menu_arg });
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
                    net.send(cmd::CloseTrade{});
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
        const bool on_run = over_run_button(mouse.x, mouse.y);
        const bool run_click = mouse.press_this_frame && on_run;
        if (run_click) { run_down = true; audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0); }
        if (mouse.release_this_frame) {
            if (run_down && on_run) send(cmd::Run{ !view.running });
            run_down = false;
        }
        // Holding an item, a click on the world drops it (C→S 0x17).
        const bool bar_click = skillbar.click(mouse);
        if (held && mouse.press_this_frame && !item_click && !over_panel && !over_belt && !menu_click && !bar_click && !level_click && !run_click
            && npc_menu.npc < 0 && speech.npc < 0 && store.npc < 0 && have_world)
            net.send(cmd::Drop{ held->id });
        const bool over_ui = over_panel || over_belt || menu_click || npc_menu.npc >= 0 || item_click || held || bar_click || level_click || run_click
                          || hud_click || game_menu.open;
        // A press on the UI stays the UI's while the button is held: no walk
        // starts under a menu that just closed.
        if (mouse.press_this_frame) press_on_ui = over_ui;
        else if (!mouse.down) press_on_ui = false;
        if (have_world) walk(mouse, over_ui || press_on_ui, frame_ms, last_ms);
        if (world.went_east) { world.went_east = false; save(); screen = Screen::CharSelect; return; }
        play_cues(cues, audio, view.player.x, view.player.y, rng, frame_ms);
        draw(framebuffer, mouse, frame_ms);
    }

auto Town::quest_bits() const -> const d2d::rules::QuestBits& {
        return character.header.quests[std::size_t(std::clamp(character.header.active_difficulty(), 0, 2))];
    }

// ponytail: the player's act is Act 1's (0): d2d has no level past it.
auto Town::toggle_quest_log() -> void {
        if (quest_log.open) { quest_log.open = false; return; }
        quest_log_open(quest_log, quest_bits(), quest_state(), 0);
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
        if (view.dead) {                                     // a click, once the death has played, respawns
            if (mouse.press_this_frame) out.push_back(cmd::Resurrect{});
            return out;
        }
        if (over_ui) return out;
        // Screen -> world: invert the iso projection around the player,
        // who sits at (kW/2, kViewY).
        const float iso_u = float(mouse.x - int(kScreenWidth) / 2) / (kIsoW / 2);
        const float iso_v = float(mouse.y - kViewY) / (kIsoH / 2);
        const float world_x = cam_x + (iso_u + iso_v) / 2, world_y = cam_y + (iso_v - iso_u) / 2;
        const int hovered_monster_index = hovered_monster();
        const bool live = hovered_monster_index >= 0 && view.monsters[std::size_t(hovered_monster_index)].alive();
        if (mouse.press_this_frame) {
            if (live) out.push_back(cmd::UseSkill{ skillbar.left, world_x, world_y, view.monsters[std::size_t(hovered_monster_index)].id, true });
            else if (hovered_ground() >= 0) out.push_back(cmd::Pickup{ view.ground[std::size_t(hovered_ground())].id });
            else if (hovered_npc >= 0 || (hovered_npc <= -2000 && hovered_npc > -2004) || (hovered_npc <= -3000 && hovered_npc > -3016))
                out.push_back(cmd::Interact{ hovered_npc });
            else out.push_back(cmd::Move{ world_x, world_y, true });
        } else if (mouse.down) {                             // held: the attack goes on, else the walk re-aims
            if (const int attacked = view.monster(view.attack); attacked >= 0 && view.monsters[std::size_t(attacked)].alive())
                out.push_back(cmd::UseSkill{ view.attack_skill, world_x, world_y, view.attack, true });
            else
                out.push_back(cmd::Move{ world_x, world_y, false });
        }
        if (mouse.rpress_this_frame) out.push_back(cmd::UseSkill{ skillbar.right, world_x, world_y, live ? view.monsters[std::size_t(hovered_monster_index)].id : -1 });
        return out;
    }

auto Town::walk(const Mouse& mouse, bool over_ui, std::uint32_t frame_ms, std::uint32_t last_ms) -> void {
        frame_now = frame_ms;
        if (net_game) {
            net_game->pump(frame_ms, frame_ms - last_ms);
            // The host moved us (a correction, later warps and waypoints): there we are.
            // Only a spot on d2d's level: a warp's or waypoint's lands as d2d
            // takes it too (the same exit spot, FUN_005550b0).
            if (net_game->take_reassign() && level) {
                const float to_x = (net_game->self_x + 0.5f) / 5.f - float(level->world_x), to_y = (net_game->self_y + 0.5f) / 5.f - float(level->world_y);
                if (to_x >= 0 && to_y >= 0 && to_x < float(level->ds1.width()) && to_y < float(level->ds1.height())) {
                    world.player.x = to_x;
                    world.player.y = to_y;
                    world.player.walking = false;
                }
            }
            // The host killed us: d2d's death plays (DT, DD, the corpse); a
            // click then sends 0x41 and respawns here too.
            if (net_game->take_death() && !world.fight.dead()) world.fight.die(frame_ms);
            // The host's quest log news (0x5d): the Quest Log button, as the
            // single-player path raises it on a log change (FUN_004a2cb0).
            for (const int quest : net_game->quest_news) quest_log_notify(quest_log, quest);
            net_game->quest_news.clear();
            // Our stats as the host sets them: attributes, points, life / mana /
            // stamina and their maxima (8.8 fixed point, as in a save), level,
            // experience, gold. The host's word wins over d2d's own regen.
            for (const auto& change : net_game->stat_changes) {
                if (change.id < 0 || change.id > 15) continue;
                auto& value = world.character.stats.values[std::size_t(change.id)];
                value = change.add ? value + change.value : change.value;
                if (std::getenv("D2D_NET_STATS")) d2d::log::info("net stat {} = {}", change.id, value);
            }
            net_game->stat_changes.clear();
            // What the host put in our bags (a pick-up, a buy): where the host put
            // it, so it keeps matching the host's item.
            for (auto& item : net_game->picked) {
                item.id = world.next_item_id++;
                world.character.items.push_back(std::move(item));
                world.cues.cue("item_pickup", 0, world.player.x, world.player.y);
            }
            net_game->picked.clear();
            // The open store shows the host's stock (its ids, for 0x32), by tab.
            if (world.store.npc >= 0 && net_game->trade_npc && net_game->store_items.size() != net_store_shown) {
                net_store_shown = net_game->store_items.size();
                for (auto& tab : world.store.tabs) tab.clear();
                for (const auto& [id, stocked] : net_game->store_items) {
                    auto item = stocked;
                    item.id = int(id);
                    world.store.tabs[std::size_t(std::clamp(item.panel - 1, 0, 3))].push_back(std::move(item));   // page 2 weapons, 4 misc (Akara's, recorded)
                }
            }
            // Once, a while in: how many of our items the host's match (the net
            // log says which don't).
            if (!net_items_checked && net_game->steady_now() > 3000 && !net_game->own_items.empty()) {
                net_items_checked = true;
                int matched = 0;
                for (const auto& item : world.character.items) {
                    if (net_game->host_item(item)) ++matched;
                    else net_game->log.note("our " + item.code + " (location " + std::to_string(item.location) + ", panel " + std::to_string(item.panel) + " at "
                                            + std::to_string(item.column) + "," + std::to_string(item.row) + ", slot " + std::to_string(item.slot) + ") has no host item");
                }
                d2d::log::info("net: {} of our {} items match the host's ({} it has)", matched, world.character.items.size(), net_game->own_items.size());
            }
            // d2d's walks of its own (up to an NPC, an object, an item, a melee
            // target): the host's player follows where ours is, every 300 ms
            // (a target the host can't stand on, an NPC's spot, it'd refuse).
            if (world.player.walking && frame_ms - net_follow_ms >= 300) {
                net_follow_ms = frame_ms;
                net_game->move_to((world.player.x + float(level->world_x)) * 5.f, (world.player.y + float(level->world_y)) * 5.f, view.running);
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
                    // Talking: start the chat, then the trade (1) or gamble (2: unverified).
                    namespace c2s = d2d::net::d2gs::c2s;
                    net_game->trade_npc = std::uint32_t(net_trade_pending);
                    net_game->send_items({ c2s::npc_chat(true, 1, net_game->trade_npc), c2s::npc_action(net_trade_gamble ? 2 : 1, net_game->trade_npc) });
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
                if (object == net_game->units.end() || frame_ms - net_operate_ms > 8000) {
                    net_game->log.note("object " + std::to_string(net_operate) + " couldn't be reached on the host");
                    net_operate = 0;
                } else if (std::hypot(net_game->host_x - object->second.x, net_game->host_y - object->second.y) > 4.f) {
                    if (frame_ms - net_follow_ms >= 500) {
                        net_follow_ms = frame_ms;
                        net_game->move_to(object->second.x, object->second.y, view.running);
                    }
                } else if (net_game->steady_now() - net_game->walked_ms >= 500) {
                    net_game->interact(net_operate_type, net_operate);
                    net_operate = 0;
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
                           || (!world.player.walking && std::hypot((world.player.x + float(level->world_x)) * 5.f - float(found->second.x),
                                                                   (world.player.y + float(level->world_y)) * 5.f - float(found->second.y)) < 4.f)) {
                    net_game->pick_up(found->first);
                    net_pick = -1;
                }
            }
            // A warp d2d's player set off for: the host's warp unit there (0x13).
            if (world.take_warp >= 0 && world.take_warp != net_warp_sent && std::size_t(world.take_warp) < level->warps.size()) {
                const auto& warp = level->warps[std::size_t(world.take_warp)];
                if (const auto* unit = net_game->nearest(5, -1, (warp.unit_x + float(level->world_x)) * 5.f, (warp.unit_y + float(level->world_y)) * 5.f, 15.f)) net_game->interact(5, unit->id);
            }
            net_warp_sent = world.take_warp;
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
        for (const auto& command : input(mouse, over_ui)) send(command);
        // Fixed ticks of kTickMs; after a stall, a few to catch up, then the
        // clock skips ahead (game.exe catches up one frame at most).
        if (world_ms == 0 || frame_ms - world_ms > 1000) world_ms = frame_ms - std::min<std::uint32_t>(frame_ms - last_ms, kTickMs);
        bool ticked = false;
        for (int ticks = 0; frame_ms - world_ms >= kTickMs && ticks < 5; ++ticks) {
            prev_x = view.player.x; prev_y = view.player.y;
            world.tick(net.receive(), world_ms + kTickMs, world_ms);
            world_ms += kTickMs;
            ticked = true;
        }
        if (!ticked && view.level == world.level) return;
        publish();
        // What the World said, handled once the View it came in is here
        // (the hire list's offers come with it).
        for (const auto& event : view.events) handle(event, frame_ms);
        if (frame_ms - world_ms >= kTickMs) world_ms = frame_ms - (frame_ms - world_ms) % kTickMs;
    }

auto Town::handle(const Event& event, std::uint32_t frame_ms) -> void {
        if (const auto* level_changed = std::get_if<ev::LevelChanged>(&event)) {
            if (level_changed->from->layer != level->layer) {
                // A layer change saves the old layer's map and loads the
                // new one's (FUN_00458d40).
                save_automap(automap, automap_dir(world), character.name, scene->map_seed, level_changed->from->layer);
                const bool seen = other_automaps.count(level->layer) != 0;
                other_automaps[level_changed->from->layer] = std::move(automap);
                automap = std::move(other_automaps[level->layer]);
                if (!seen) load_automap(automap, automap_dir(world), character.name, scene->map_seed, level->layer);
                if (level_changed->keep_map) automap.open = other_automaps[level_changed->from->layer].open;
            }
            hovered_npc = -1;
            // The slide carries on in the new level's cells (a warp's
            // distance still snaps).
            prev_x += float(level_changed->from->world_x - level->world_x);
            prev_y += float(level_changed->from->world_y - level->world_y);
            npc_menu = {}; store = {}; speech = {}; waypoint = {}; menu_after_speech = -1;
            level_ms = frame_ms;                                  // its song comes in 3 s later
            return;
        }
        if (const auto* object_speech = std::get_if<ev::Speech>(&event)) {   // FUN_004a1600 mode 2: the speech box, heard at once
            speech = start_speech(*scene, object_speech->npc, std::uint16_t(object_speech->string), frame_ms);
            net.send(cmd::QuestMessage{ object_speech->npc, object_speech->string });
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
            waypoint = { .open = true, .npc = open_ui.npc };
            inv_open = char_open = stash_open = cube_open = quest_log.open = false;
            return;
        }
        // A quest message for the player plays at once (FUN_004a10e0 on
        // the first kind-0 one; hearing it is what the server acts on), the
        // menu after it.
        npc_quest = open_ui.quest;
        for (const auto& message : npc_quest)
            if (message.greet) {
                speech = start_speech(*scene, open_ui.npc, std::uint16_t(message.string), frame_ms);
                net.send(cmd::QuestMessage{ open_ui.npc, message.string });
                menu_after_speech = open_ui.npc;
                return;
            }
        open_menu(open_ui.npc);
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
                return !d2d::rules::qbit(quest_bits, d2d::rules::kRespecQuest, 0) && (d2d::rules::qbit(quest_bits, d2d::rules::kRespecQuest, 1) || difficulty == 2);
            }(), character.header.quest_flag(character.header.active_difficulty(), d2d::rules::AndyQuest::kQuest, 0),
            character.header.quest_flag(character.header.active_difficulty(), d2d::rules::ToolsQuest::kQuest, 1));
    }

auto Town::draw(std::vector<std::uint8_t>& framebuffer, const Mouse& mouse, std::uint32_t frame_ms) -> void {
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
        cam_x = jump ? unit.x : prev_x + (unit.x - prev_x) * blend;
        cam_y = jump ? unit.y : prev_y + (unit.y - prev_y) * blend;
        state_clock.now = frame_ms;
        view_units(*scene, view, cam_x, cam_y, &merc_label, extra, den_beams, frame_ms, &character.name, std::max(character.character_class, 0), &state_clock);
        std::erase_if(state_clock.seen, [&](const auto& entry) { return frame_ms - entry.second.last > 5000; });
        Unit player_look{};
        dress(*scene, player_look, -1, player_states(*scene, view), &state_clock);
        const bool town = level->id == d2d::rules::level_ids::kRogueEncampment;             // TN/TW in town, NU/WL outside
        // A dead player has no DD composite: DT held on its last frame.
        const auto cls = std::max(character.character_class, 0);
        if (pmode == kModeDD) unit.mode_ms = frame_ms - (scene->composite(cls, kModeDT, view.gfx).length_ms() - 1);
        int mode = pmode == kModeDD ? kModeDT : pmode >= 0 ? pmode : unit.walking ? (run_on ? kModeRN : town ? kModeTW : kModeWL) : town ? kModeTN : kModeNU;
        std::uint32_t mode_ms = unit.mode_ms;
        float rate = pmode >= 0 && pmode != kModeDD ? view.prate : 1.f;
        if (!view.seq.empty() && attack_mode(pmode)) { std::tie(mode, mode_ms) = view_seq(*scene, int(cls), view, frame_ms); rate = 1.f; }   // an SQ skill's frame
        const auto light = frame_light(*scene, view, cam_x, cam_y, den_beams, den_ambient(), extra, &player_look, frame_ms);
        render_ingame(framebuffer, *scene, *view.level, ui_cls,
                      view.gfx,
                      character.name, character.hardcore,
                      cam_x, cam_y, mode,
                      unit.dir, frame_ms, held ? -1 : mouse.x, held ? -1 : mouse.y, view.npc_states,
                      inv_open ? &character.items : nullptr,
                      char_open ? &character.stats : nullptr, &character.stats, &character.panel, mode_ms, &character.items,
                      &hovered_npc, stash_open || cube_open ? &character.items : nullptr, character.expansion, belt_open,
                      cube_open, &npc_menu, &speech, &automap, &store, stat_pressed,
                      nullptr, nullptr, nullptr, extra, rate, light.pal ? &light : nullptr, level->rain ? &rain : nullptr,
                      !(pmode == kModeDD && !view.corpses.empty()),    // dead, the corpse lies there instead
                      &player_look, alt_held || (SDL_GetModState() & SDL_KMOD_ALT) != 0,   // D2's "Show Items" (Alt)
                      Hud{ view.poisoned, view.running, run_down });
        view_overlays(framebuffer, *scene, view, hovered_monster());
        skillbar.draw(framebuffer, held ? -1 : mouse.x, held ? -1 : mouse.y);
        if (quest_log_was_open && !quest_log.open) {    // shut by any means (FUN_00455ae0 → FUN_004a28d0)
            quest_log.open = true;
            quest_log_close(quest_log, quest_bits(), quest_state());
        }
        quest_log_was_open = quest_log.open;
        if (quest_log.open && draw_quest_log(framebuffer, *scene, quest_log, quest_bits(), quest_state(), frame_ms, character.expansion, mouse.x, mouse.y))
            questdone_sound = true;                    // cursor_questdone
        if (tree_open)
            draw_skill_tree(framebuffer, *scene, int(kUiToSaveClass[ui_cls]), tree_tab, character.stats.skills, character.stats,
                            skill_pressed, held ? -1 : mouse.x, held ? -1 : mouse.y);
        if (waypoint.open)
            draw_waypoints(framebuffer, *scene, waypoint, character.header, character.expansion, level->id, mouse.x, mouse.y);
        draw_level_buttons(framebuffer, *scene, level_buttons_now(), mouse.x, mouse.y);   // after the panels, as the frame's draw
        draw_quest_log_button(framebuffer, *scene, quest_log_button_now(), quest_log.button_down, mouse.x, mouse.y);
        const bool left_open = char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open;
        mini.draw(framebuffer, *scene, left_open, inv_open || tree_open, mouse.x, mouse.y);
        if (game_menu.open) game_menu.draw(framebuffer, *scene);
    }

}  // namespace d2d::client
