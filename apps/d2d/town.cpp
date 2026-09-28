// The in-game client (town.hpp): Town, the view to units, the frame's
// light, the HUD overlays.
#include "town.hpp"
#include "audio.hpp"
#include "common.hpp"
#include "cursor.hpp"
#include "ingame.hpp"
#include "panels.hpp"
#include "scene.hpp"
#include "skilltree.hpp"
#include "speech_sound.hpp"
#include "store.hpp"
#include "ui.hpp"
#include "world_view.hpp"

#include "platform.hpp"

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
    scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, int(kScreenWidth) / 2 - scene.font.measure(name) / 2, top + 2, name);
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
    // 768 ms), then ON; named by where they lead (-2000 - which).
    for (const auto& portal : view.portals) {
        const Level* destination = scene.level(portal.destination);
        const bool opening = now_ms - portal.born < kPortalOpenMs;
        out.push_back({ portal.x, portal.y, &scene.npc_anim(scene.town_portal, opening ? "OP" : "ON"), 0, destination ? &destination->name : nullptr,
                        opening ? portal.born : portal.born + kPortalOpenMs, -2000 - portal.which });
        out.back().shadow = false;
    }
    if (view.merc)
        out.push_back({ view.merc->unit.x, view.merc->unit.y, &scene.npc_anim(*view.merc->npc, view.merc->mode), view.merc->unit.dir,
                        view.merc->mode == "DT" ? nullptr : merc_label, view.merc->unit.mode_ms, -2 });
    for (const auto& pet : view.pets) out.push_back({ pet.unit.x, pet.unit.y, &scene.npc_anim(pet.npc, pet.mode), pet.unit.dir, nullptr, pet.unit.mode_ms, -3 });
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
    for (const auto& near : view.level->nearby)                  // the torches over the level's edge
        for (const auto& npc : near.level->npcs) stamp(npc.x + float(near.dx), npc.y + float(near.dy), lit(npc, {}), true);
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
        cues.due.insert(cues.due.end(), view.sounds.begin(), view.sounds.end());
        if (!view.has_character) return;
        character.header = view.header; character.stats = view.stats; character.items = view.items;
        character.expansion = view.header.expansion();
        character.panel = panel_stats(*scene, character.header, character.items, character.stats);
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

auto Town::enter() -> void {
        automap.cells.clear();                    // a new game: nothing seen yet
        automap.revealed.clear();
        other_automaps.clear();
        quest_log = {};                           // done animations play again in a new game
        world.enter(character);
        skillbar.new_game();
        publish();
    }

auto Town::save() -> std::string { return world.save(); }

auto Town::operate(int npc_index, std::uint32_t frame_ms, int force ) -> void { world.operate(npc_index, frame_ms, force); }

auto Town::new_game() -> void {
        world.new_game();
        skillbar.new_game();
    }

auto Town::update(std::vector<std::uint8_t>& framebuffer, Mouse& mouse, const std::vector<SDL_Keycode>& keys_this_frame,
                Screen& screen, Audio& audio, std::uint32_t frame_ms, std::uint32_t last_ms) -> void {
        now_ms = frame_ms;
        // ESC handled globally in handle_sdl_events (returns to Title).
        // D2 movement: press or hold the left button on the ground
        // and the character walks toward that point (the target
        // tracks the cursor while held); the camera follows.
        for (const auto key : keys_this_frame) {
            if (key == SDLK_I) { inv_open = !inv_open; if (inv_open) tree_open = false; }
            if (key == SDLK_T) { tree_open = !tree_open; if (tree_open) inv_open = false; }   // both right-hand panels
            if (key == SDLK_R) net.send(cmd::Run{ !view.running });   // D2's run/walk toggle
            skillbar.key(key, mouse.x, mouse.y);                // F1-F8
            if (key == SDLK_GRAVE) belt_open = !belt_open;      // D2's "Show Belt" key
            if (key == SDLK_TAB) automap.open = !automap.open;  // D2's automap toggle
            if (key == SDLK_C) { char_open = !char_open; if (char_open) stash_open = cube_open = quest_log.open = false; }
            if (key == SDLK_Q) { quest_log.open = !quest_log.open; if (quest_log.open) char_open = stash_open = cube_open = false; }
            if (key == SDLK_ESCAPE && view.dead) { net.send(cmd::Resurrect{}); continue; }
            if (key >= SDLK_1 && key <= SDLK_4) net.send(cmd::UseBelt{ int(key - SDLK_1) });
            if (key == SDLK_ESCAPE && skillbar.picking) { skillbar.picking = 0; continue; }   // the picker first
            if (key == SDLK_ESCAPE) {
                if (waypoint.open) waypoint = {};
                else if (store.npc >= 0) { net.send(cmd::CloseTrade{}); inv_open = false; } // the store first
                else if (speech.npc >= 0) { speech = {}; menu_after_speech = -1; }   // then speech
                else if (npc_menu.npc >= 0) npc_menu = {};          // then the menu
                else if (inv_open || char_open || stash_open || cube_open || tree_open || quest_log.open)   // then panels
                    inv_open = char_open = stash_open = cube_open = tree_open = quest_log.open = false;
                else { save(); screen = Screen::CharSelect; }
            }
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
        if (mouse.press_this_frame && store.mode == 0 && npc_menu.npc < 0 && speech.npc < 0) {
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
            if (const auto* to_cursor = click.cmd ? std::get_if<cmd::ToCursor>(&*click.cmd) : nullptr) net.send(cmd::UseItem{ to_cursor->item });
        }
        // Quest log: a tab picks the act, an icon the quest.
        // Its buttons: close; questlast plays the selected quest's message again.
        if (quest_log.open) {
            const int button = quest_button_at(mouse.x, mouse.y);
            if (mouse.press_this_frame) { quest_log.close_down = button == 1; quest_log.last_down = button == 2; }
            if (mouse.release_this_frame) {
                if (quest_log.close_down && button == 1) quest_log.open = false;
                if (quest_log.last_down && button == 2)
                    for (const auto& entry : kQuestLog)
                        if (entry.act == quest_log.act && entry.slot == quest_log.slot)
                            if (const auto text = quest_text(character.header.quests[std::size_t(std::clamp(character.header.active_difficulty(), 0, 2))], entry.quest,
                                                          { view.den_state, view.den_log, view.den_left }); text.speech)
                                replay_speech = text.speech;
                quest_log.close_down = quest_log.last_down = false;
            }
        }
        if (quest_log.open && mouse.press_this_frame) {
            if (const int act = quest_tab_at(mouse.x, mouse.y); act >= 0) { quest_log.act = act; quest_log.slot = -1; }
            if (const int slot = quest_slot_at(*scene, mouse.x, mouse.y); slot >= 0) quest_log.slot = slot;
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
                if (item.code != "box" || item.location != 0) continue;
                const bool in_inv = inv_open && item.panel == 1, in_stash = stash_open && item.panel == 5;
                if (!in_inv && !in_stash) continue;
                const auto rect = grid_rect(*scene, in_inv ? lay : scene->stash_layout[character.expansion ? 1 : 0], item);
                if (mouse.x >= rect[0] && mouse.x < rect[0] + rect[2] && mouse.y >= rect[1] && mouse.y < rect[1] + rect[3]) {
                    cube_open = true; stash_open = char_open = false;
                }
            }
        // An open NPC menu takes every click: an entry runs (only
        // "cancel" so far — every entry closes it), anything else
        // closes it. ponytail: talk/trade/hire/gamble not built.
        bool menu_click = false;
        const auto& unit = view.player;
        automap_reveal(*scene, *level, automap, unit.x, unit.y);
        for (const auto& near : level->nearby)                   // what's in view across the edge
            if (near.level->layer == level->layer)
                automap_reveal(*scene, *near.level, automap, unit.x - float(near.dx), unit.y - float(near.dy));
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
        if (rain_vol > 0 && audio.rain.sound != 64) audio.play(audio.rain, *scene, 64);
        if (rain_vol == 0 && audio.rain.sound) audio.stop(audio.rain);
        if (audio.rain.src && scene->sounds.size() > 64)
            audio.set_gain(audio.rain, float(scene->sounds[64].volume) / 255.f * float(rain_vol) / 255.f);
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
        if (questdone_sound) { questdone_sound = false; audio.play_sfx(*scene, 14, 1.f, 0); }
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
            const int line_index = npc_menu.line_at(mouse.x, mouse.y);
            const auto action = line_index >= 0 ? npc_menu.lines[std::size_t(line_index)].action : NpcMenuState::kClose;
            const int npc_menu_arg = line_index >= 0 ? npc_menu.lines[std::size_t(line_index)].arg : -1;
            const int who = npc_menu.npc;
            const auto& npc = level->npcs[std::size_t(who)];
            const auto& state = view.npc_states[std::size_t(who)];
            const float dx = (npc.path.empty() ? npc.x : state.x) - view.player.x, dy = (npc.path.empty() ? npc.y : state.y) - view.player.y;
            const int screen_x = int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
            const int screen_y = int(kScreenHeight) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2)));
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
                net.send(cmd::Buy{ store_index });
            }
            // Sell: an inventory item; repair: that or a worn one.
            if (store.npc >= 0 && mouse.press_this_frame && (store.mode == 2 || store.mode == 3))
                for (std::size_t i = 0; i < character.items.size(); ++i) {
                    const auto& item = character.items[i];
                    const bool worn = item.location == 1 && item.slot >= 1 && item.slot <= 10;
                    if (!(item.location == 0 && item.panel == 1) && !(worn && store.mode == 3)) continue;
                    const auto rect = worn ? lay.slots[std::size_t(item.slot)] : grid_rect(*scene, lay, item);
                    if (mouse.x >= rect[0] && mouse.x < rect[0] + rect[2] && mouse.y >= rect[1] && mouse.y < rect[1] + rect[3]) {
                        if (store.mode == 2) net.send(cmd::Sell{ item.id });
                        else net.send(cmd::Repair{ item.id });
                        break;
                    }
                }
        }
        // Waypoint panel: tabs switch acts, cancel (or the row of the
        // level you're in) closes it.
        // ponytail: no travel yet — another row closes the panel too.
        if (waypoint.open) {
            const bool on_cancel = mouse.x >= kCharPanelX + 0x111 && mouse.x < kCharPanelX + 0x111 + 0x24
                                && mouse.y >= 60 + 0x183 && mouse.y < 60 + 0x183 + 0x22;
            waypoint.cancel_down = on_cancel && mouse.down;
            if (mouse.press_this_frame) {
                if (const int tab = waypoint_tab_at(character.header, character.expansion, mouse.x, mouse.y); tab >= 0) waypoint.tab = tab;
                else if (const int row = waypoint_row_at(*scene, waypoint, character.header, mouse.x, mouse.y); row >= 0) {
                    d2d::log::info("not implemented: waypoint travel to {}",
                                   scene->waypoint_levels[std::size_t(waypoint.tab)][std::size_t(row)].name);
                    waypoint = {};
                }
            }
            if (on_cancel && mouse.release_this_frame) waypoint = {};
        }
        // Holding an item, a click on the world drops it (C→S 0x17).
        const bool bar_click = skillbar.click(mouse);
        if (held && mouse.press_this_frame && !item_click && !over_panel && !over_belt && !menu_click && !bar_click
            && npc_menu.npc < 0 && speech.npc < 0 && store.npc < 0 && have_world)
            net.send(cmd::Drop{ held->id });
        const bool over_ui = over_panel || over_belt || menu_click || npc_menu.npc >= 0 || item_click || held || bar_click;
        if (have_world) walk(mouse, over_ui, frame_ms, last_ms);
        play_cues(cues, audio, view.player.x, view.player.y, rng, frame_ms);
        draw(framebuffer, mouse, frame_ms);
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
        // who sits at (kW/2, kH/2 + kIsoH/2).
        const float iso_u = float(mouse.x - int(kScreenWidth) / 2) / (kIsoW / 2);
        const float iso_v = float(mouse.y - int(kScreenHeight) / 2 - kIsoH / 2) / (kIsoH / 2);
        const float world_x = cam_x + (iso_u + iso_v) / 2, world_y = cam_y + (iso_v - iso_u) / 2;
        const int hovered_monster_index = hovered_monster();
        const bool live = hovered_monster_index >= 0 && view.monsters[std::size_t(hovered_monster_index)].alive();
        if (mouse.press_this_frame) {
            if (live) out.push_back(cmd::UseSkill{ skillbar.left, world_x, world_y, view.monsters[std::size_t(hovered_monster_index)].id, true });
            else if (hovered_ground() >= 0) out.push_back(cmd::Pickup{ view.ground[std::size_t(hovered_ground())].id });
            else if (hovered_npc >= 0 || (hovered_npc <= -2000 && hovered_npc > -2002) || (hovered_npc <= -3000 && hovered_npc > -3016))
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
        // The skill buttons: a change goes to the World (0x3c), which runs a
        // right-button aura (a Paladin's).
        if (std::uint32_t(skillbar.left) != character.header.left_skill) net.send(cmd::SelectSkill{ skillbar.left, true });
        if (std::uint32_t(skillbar.right) != character.header.right_skill || (view.aura != 0) != (scene->skills.get(skillbar.right) && scene->skills.get(skillbar.right)->aura))
            net.send(cmd::SelectSkill{ skillbar.right, false });
        // The skill shrine's +all skills while its boost lasts.
        skillbar.extra.clear();
        for (const auto& [id, value] : view.boost) if (id == 127) skillbar.extra.push_back({ .stat = 127, .value = value });
        // The NPC the client's talking with (it stands meanwhile), when that changes.
        if (const int talk = npc_menu.npc >= 0 ? npc_menu.npc : speech.npc >= 0 ? speech.npc : store.npc; talk != talking_sent) {
            net.send(cmd::Chat{ talk });
            talking_sent = talk;
        }
        for (const auto& command : input(mouse, over_ui)) net.send(command);
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
                other_automaps[level_changed->from->layer] = std::move(automap);
                automap = std::move(other_automaps[level->layer]);
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
        const auto& open_ui = std::get<ev::OpenUI>(event);
        if (open_ui.kind == ev::OpenUI::stash) { stash_open = inv_open = true; char_open = quest_log.open = false; return; }
        if (open_ui.kind == ev::OpenUI::trade) { inv_open = true; char_open = stash_open = cube_open = quest_log.open = false; return; }
        if (open_ui.kind == ev::OpenUI::hire) {
            npc_menu = open_hire_menu(*scene, open_ui.npc, hire_offers, character.stats.get(d2d::d2s::kGold) + character.stats.get(d2d::d2s::kGoldBank));
            return;
        }
        if (open_ui.kind == ev::OpenUI::waypoint) {
            waypoint = { .open = true };
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
            int(kScreenHeight) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))),
            int(character.stats.get(d2d::d2s::kLevel)), d2d::rules::unidentified(character.items), [&] {
                const int difficulty = character.header.active_difficulty();
                const auto& quest_bits = character.header.quests[std::size_t(std::clamp(difficulty, 0, 2))];
                return !d2d::rules::qbit(quest_bits, 41, 0) && (d2d::rules::qbit(quest_bits, 41, 1) || difficulty == 2);
            }());
    }

auto Town::draw(std::vector<std::uint8_t>& framebuffer, const Mouse& mouse, std::uint32_t frame_ms) -> void {
        auto& unit = view.player;                           // the client's copy: its animation clock is the client's
        const int pmode = view.pmode;
        if (const bool running = unit.walking && view.running; pmode < 0 && (unit.walking != player_walked || running != player_ran)) {
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
        const bool town = level->id == 1;             // TN/TW in town, NU/WL outside
        // A dead player has no DD composite: DT held on its last frame.
        const auto cls = std::max(character.character_class, 0);
        if (pmode == kModeDD) unit.mode_ms = frame_ms - (scene->composite(cls, kModeDT, view.gfx).length_ms() - 1);
        int mode = pmode == kModeDD ? kModeDT : pmode >= 0 ? pmode : unit.walking ? (view.running ? kModeRN : town ? kModeTW : kModeWL) : town ? kModeTN : kModeNU;
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
                      &player_look, alt_held || (SDL_GetModState() & SDL_KMOD_ALT) != 0);   // D2's "Show Items" (Alt)
        view_overlays(framebuffer, *scene, view, hovered_monster());
        skillbar.draw(framebuffer, held ? -1 : mouse.x, held ? -1 : mouse.y);
        if (quest_log.open
            && draw_quest_log(framebuffer, *scene, quest_log, character.header.quests[std::size_t(std::clamp(character.header.active_difficulty(), 0, 2))],
                              { view.den_state, view.den_log, view.den_left }, frame_ms))
            questdone_sound = true;                    // cursor_questdone
        if (tree_open)
            draw_skill_tree(framebuffer, *scene, int(kUiToSaveClass[ui_cls]), tree_tab, character.stats.skills, character.stats,
                            skill_pressed, held ? -1 : mouse.x, held ? -1 : mouse.y);
        if (waypoint.open)
            draw_waypoints(framebuffer, *scene, waypoint, character.header, character.expansion, 1, mouse.x, mouse.y);
    }

}  // namespace d2d::client
