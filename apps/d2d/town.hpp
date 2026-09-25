// The town: the in-game character's world state (position, walking,
// panels, NPC menus, store, merc, ...) and one frame of play — input,
// movement, NPCs, then the render.
#pragma once

#include "window.hpp"

namespace {

// The hovered monster's name on its life bar, top centre: a dark red bar
// as wide as the name plus a margin, filled by its share of life left.
// ponytail: D2's own bar (game.exe draws it with the MonsterIndicators
// font and per-type colours) isn't traced; this is its look by eye.
void draw_monster_bar(std::vector<std::uint8_t>& fb, const Scene& s, const Monster& m) {
    const auto& name = m.npc.name;
    if (name.empty() || !m.alive()) return;
    const int w = std::max(s.font.measure(name) + 20, 120), h = s.font.line_height() + 4;
    const int x0 = int(kW) / 2 - w / 2, y0 = 10;
    const int filled = w * std::clamp(m.hp, 0, m.st.hp) / std::max(m.st.hp, 1);
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) {
            auto* p = fb.data() + (std::size_t(y) * kW + std::size_t(x)) * 4;
            const bool on = x - x0 < filled;
            p[0] = on ? 0x88 : 0x20; p[1] = on ? 0x08 : 0x10; p[2] = on ? 0x08 : 0x10;
        }
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    s.font.draw(fb, kW, kH, pal, int(kW) / 2 - s.font.measure(name) / 2, y0 + 2, name);
}

struct Town {
    const Scene* scene = nullptr;
    const Level* level = nullptr;          // where the character is: the town or the Blood Moor
    std::unordered_map<int, Automap> other_automaps;   // other Layers' maps, while elsewhere
    std::vector<int> not_there;            // levels walked toward that aren't built (logged once)
    std::uint32_t level_ms = 0;            // when the player entered `level`
    CharCreateUI& cc;                      // the in-game character (save, items, stats)
    bool have_world = false;
    UnitState player;                      // DS1 cells (x.5 = a cell centre); the camera follows
    std::vector<UnitState> npc_states;
    float target_x = 0, target_y = 0;
    bool  running = false;                 // R toggles, like D2's run/walk button
    bool  stash_open = false;
    std::optional<d2d::d2s::Item> held;   // the item on the cursor
    int   stat_pressed = -1;               // char panel stat button held down
    bool  tree_open = false;               // skill tree ('T')
    int   tree_tab = 1;                    // 1..3, bottom tab first (0x724bec starts at 1)
    int   skill_pressed = -1;              // skill icon held down
    std::optional<UnitState> merc;          // the save's mercenary, following
    std::vector<d2d::rules::MercOffer> hire_offers;   // Kashya's list while it's open
    const Npc* merc_npc = nullptr;
    std::string merc_label;
    bool  belt_open = false;               // belt popup (` key or a click on the belt)
    bool  cube_open = false;               // right-click the Horadric Cube item
    NpcMenuState npc_menu;                 // open NPC menu (npc < 0: none)
    Automap automap;                       // Tab
    Store store;                           // an open vendor store (npc < 0: none)
    WaypointUI waypoint;                   // the waypoint panel
    Speech speech;                         // NPC talking (npc < 0: none)
    std::vector<int> gossip_pick;          // per world NPC: chosen gossip topic, -1 = not yet
    d2d::rules::Rng rng{ 0x2545f491u };    // rolls: stock, talk topics, gambles, merc offers
    std::vector<Monster> monsters;         // the Blood Moor's (Level::spawns), kept while the game runs
    // Fighting: the monster being attacked (walked up to, then struck),
    // the player's non-walking mode (A1 attack, GH get-hit, DT dying, DD
    // dead; -1 none) and when it ends.
    int   attack_mon = -1;
    int   pmode = -1;
    std::uint32_t pmode_until = 0;
    bool  pstruck = false;                 // this swing's hit is resolved
    // Loot on the Blood Moor's floor (gold: code "gld", `gold` coins), and
    // the one being walked to for picking up.
    struct GroundItem {
        d2d::d2s::Item item;
        int gold = 0;
        float x = 0, y = 0;
        std::uint32_t ms = 0;                // when it dropped: the flippy plays from here
        std::string label;
        std::array<std::uint8_t, 3> rgb{ 255, 255, 255 };
    };
    std::vector<GroundItem> ground;
    int   pick_item = -1;
    int   hovered_npc = -1;                // Level::npcs index under the cursor (last frame); <= -10: monster -10 - i
    int   interact_npc = -1;               // clicked object being walked to
    bool  player_walked = false;           // `walking` as of the last frame
    bool  player_ran = false;
    bool  inv_open = false;   // 'I' — inventory panel
    bool  char_open = false;  // 'C' — character panel

    // Seeded to --start-cam-x/y, else the town start (Level::start),
    // else the map's middle; then the nearest free spot so we never start
    // inside a tent.
    Town(const Scene* s, CharCreateUI& c, int start_x = -1, int start_y = -1)
        : scene(s), level(s ? &s->town : nullptr), cc(c) {
        have_world = level && !level->dt1s.empty();
        player.x = (start_x >= 0 ? float(start_x) : have_world ? float(level->ds1.width() / 2) : 0.f) + 0.5f;
        player.y = (start_y >= 0 ? float(start_y) : have_world ? float(level->ds1.height() / 2) : 0.f) + 0.5f;
        if (have_world && start_x < 0 && start_y < 0 && level->start.first >= 0)
            std::tie(player.x, player.y) = level->start;
        if (have_world) std::tie(player.x, player.y) = level->nearest_free(player.x, player.y);
        if (scene) npc_states = npc_start(*level);
        if (scene) monsters = spawn_monsters(*scene, scene->moor, rng, 0);
        target_x = player.x; target_y = player.y;
        player.dir = 4;                    // south, facing the viewer
    }

    // A fresh game for the character: the Blood Moor's monsters at its
    // difficulty, no loot about.
    void new_game() {
        monsters = spawn_monsters(*scene, scene->moor, rng, cc.header.active_difficulty());
        ground.clear();
        attack_mon = pick_item = -1;
        pmode = -1;
    }

    // The character's merc (cc.header) next to the player, if alive.
    void spawn_merc() {
        merc.reset();
        const auto& h = cc.header;
        if (const auto m = scene->mercs.find(h.merc_type); h.merc_seed && !h.merc_dead && m != scene->mercs.end()) {
            merc = UnitState{ .x = player.x + 1, .y = player.y + 1 };
            std::tie(merc->x, merc->y) = level->nearest_free(merc->x, merc->y);
            merc_npc = &m->second.npc;
            merc_label = merc_name(*scene, m->second, h.merc_name);
        }
    }

    // One InGame frame: keys, panels, clicks, walking, NPCs, then the render.
    // Esc with nothing open goes back to the roster (screen).
    void update(std::vector<std::uint8_t>& fb, Mouse& mouse, const std::vector<SDL_Keycode>& keys_this_frame,
                Screen& screen, Audio& audio, std::uint32_t ms, std::uint32_t last_ms) {
        // ESC handled globally in handle_sdl_events (returns to Title).
        // D2 movement: press or hold the left button on the ground
        // and the character walks toward that point (the target
        // tracks the cursor while held); the camera follows.
        for (const auto k : keys_this_frame) {
            if (k == SDLK_I) { inv_open = !inv_open; if (inv_open) tree_open = false; }
            if (k == SDLK_T) { tree_open = !tree_open; if (tree_open) inv_open = false; }   // both right-hand panels
            if (k == SDLK_R) running = !running;              // D2's run/walk toggle
            if (k == SDLK_GRAVE) belt_open = !belt_open;      // D2's "Show Belt" key
            if (k == SDLK_TAB) automap.open = !automap.open;  // D2's automap toggle
            if (k == SDLK_C) { char_open = !char_open; if (char_open) stash_open = cube_open = false; }
            if (k == SDLK_ESCAPE && dead()) { respawn(ms); continue; }
            if (k == SDLK_ESCAPE) {
                if (waypoint.open) waypoint = {};
                else if (store.npc >= 0) { store = {}; inv_open = false; } // the store first
                else if (speech.npc >= 0) speech = {};              // then speech
                else if (npc_menu.npc >= 0) npc_menu = {};          // then the menu
                else if (inv_open || char_open || stash_open || cube_open || tree_open)   // then panels
                    inv_open = char_open = stash_open = cube_open = tree_open = false;
                else { stow_held(*scene, cc.items, held); screen = Screen::CharSelect; }
            }
        }
        if (inv_open) tree_open = false;       // the stash / a store opened the inventory
        const auto& lay = scene->inv_layout[std::size_t(kUiToSaveClass[std::max(cc.selected, 0)])];
        const bool over_panel =
            (tree_open && mouse.x >= 400 && mouse.x < 720 && mouse.y >= 60 && mouse.y < 540) ||
            (inv_open && mouse.x >= lay.panel_x && mouse.x < lay.panel_x + 320
                      && mouse.y >= lay.panel_y && mouse.y < lay.panel_y + 432) ||
            ((char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open) && mouse.x >= kCharPanelX && mouse.x < kCharPanelX + 320
                       && mouse.y >= kCharPanelY && mouse.y < kCharPanelY + 432);
        // The belt: its HUD strip (row 1's boxes) toggles the popup;
        // strip and open popup take the click instead of the world.
        const auto& belt = scene->belts[std::size_t(belt_index(*scene, cc.items))];
        const auto& b0 = belt.box[0];
        const auto& b3 = belt.box[3];
        const bool over_belt =
            mouse.x >= b0[0] && mouse.x <= b3[1]
            && ((mouse.y >= b0[2] && mouse.y <= b0[3])
                || (belt_open && mouse.y >= belt.box[std::size_t(std::max(belt.boxes - 1, 0))][2]
                              && mouse.y <= b0[3]));
        // The item cursor (not with a trade button toggled on, or a
        // menu or speech up). Held over the store's stock, a click
        // sells the item.
        bool item_click = false;
        if (mouse.press_this_frame && store.mode == 0 && npc_menu.npc < 0 && speech.npc < 0) {
            if (held && store.npc >= 0 && mouse.x >= 96 && mouse.x < 96 + 10 * 29
                && mouse.y >= 123 && mouse.y < 123 + 10 * 29) {
                cc.items.push_back(std::move(*held));
                held.reset();
                d2d::rules::store_sell(scene->rules, store, cc.items.size() - 1, cc.items, cc.stats);
                item_click = true;
            } else {
                item_click = item_cursor_click(*scene, cc.items, held, cc.stats,
                                               int(kUiToSaveClass[std::max(cc.selected, 0)]),
                                               { inv_open, stash_open, cube_open, belt_open, cc.expansion },
                                               mouse.x, mouse.y);
            }
        }
        if (mouse.press_this_frame && over_belt && mouse.y >= b0[2] && !item_click) belt_open = !belt_open;
        // Skill tree: tabs switch on press; a skill icon pressed and
        // released spends a point (FUN_004ab7e0 / FUN_004abc30).
        if (tree_open) {
            const int cls = int(kUiToSaveClass[std::max(cc.selected, 0)]);
            const int sk = skill_at(*scene, cls, tree_tab, mouse.x, mouse.y);
            if (mouse.press_this_frame) {
                if (const int t = skill_tab_at(mouse.x, mouse.y); t > 0) tree_tab = t;
                skill_pressed = cc.stats.get(d2d::d2s::kSkillPts) > 0
                    && d2d::rules::can_learn(scene->rules, cls, sk, cc.stats.skills, int(cc.stats.get(d2d::d2s::kLevel))) ? sk : -1;
            }
            if (mouse.release_this_frame) {
                if (skill_pressed >= 0 && sk == skill_pressed)
                    d2d::rules::learn_skill(scene->rules, cls, sk, cc.stats.skills, cc.stats);
                skill_pressed = -1;
            }
        }
        // Char panel stat buttons: press, then release on the same
        // button spends a point (Shift: all of them), FUN_004a78c0.
        if (char_open && cc.stats.get(d2d::d2s::kStatPts) > 0) {
            const int sb = stat_button_at(mouse.x, mouse.y);
            if (mouse.press_this_frame && sb >= 0) stat_pressed = sb;
            if (mouse.release_this_frame) {
                if (stat_pressed >= 0 && sb == stat_pressed) {
                    const int n = (SDL_GetModState() & SDL_KMOD_SHIFT) ? int(cc.stats.get(d2d::d2s::kStatPts)) : 1;
                    d2d::rules::spend_stat_points(cc.stats, kStatButtons[std::size_t(sb)].stat, n,
                        scene->class_gains[std::size_t(kUiToSaveClass[std::max(cc.selected, 0)])]);
                }
                stat_pressed = -1;
            }
        } else {
            stat_pressed = -1;
        }
        // Right-clicking the Horadric Cube ("box") in the inventory
        // or the stash opens it in the left panel, as D2 does.
        if (mouse.rpress_this_frame)
            for (const auto& it : cc.items) {
                if (it.code != "box" || it.location != 0) continue;
                const bool in_inv = inv_open && it.panel == 1, in_stash = stash_open && it.panel == 5;
                if (!in_inv && !in_stash) continue;
                const auto r = grid_rect(*scene, in_inv ? lay : scene->stash_layout[cc.expansion ? 1 : 0], it);
                if (mouse.x >= r[0] && mouse.x < r[0] + r[2] && mouse.y >= r[1] && mouse.y < r[1] + r[3]) {
                    cube_open = true; stash_open = char_open = false;
                }
            }
        // An open NPC menu takes every click: an entry runs (only
        // "cancel" so far — every entry closes it), anything else
        // closes it. ponytail: talk/trade/hire/gamble not built.
        bool menu_click = false;
        automap_reveal(*scene, *level, automap, player.x, player.y);
        for (const auto& n : level->near)                   // what's in view across the edge
            if (n.level->layer == level->layer)
                automap_reveal(*scene, *n.level, automap, player.x - float(n.dx), player.y - float(n.dy));
        // The NPC's voice (FUN_004a10e0 plays FUN_004e0650's sound for
        // the speech string) follows the speech box.
        if (speech.npc < 0 && audio.voice.src) audio.stop_voice();
        // The level's SoundEnviron (Levels.txt SoundEnv -> Song, Day
        // Ambience). ponytail: the Rogue Encampment's, env 1: song
        // 4673 music_town_1, ambience 70; no night or events yet.
        if (audio.music.sound == 0) {
            audio.play_music(*scene, level->song);
            audio.play(audio.ambience, *scene, level->ambience);
            if (audio.music.sound == 0) audio.music.sound = -1;   // don't retry every frame
        }
        // Another level's song: game.exe (FUN_004dcaa0) switches 75 sound
        // ticks (3 s) into the new level, so skirting an edge doesn't flip
        // it, and cross-fades (Audio::crossfade_music).
        if (audio.music.sound > 0 && level->song > 0 && audio.music.sound != level->song && ms - level_ms >= 75 * 40) {
            audio.crossfade_music(*scene, level->song);
            if (audio.ambience.sound != level->ambience) audio.play(audio.ambience, *scene, level->ambience);
        }
        if (speech.npc >= 0 && speech.voice == 0) {
            speech.voice = -1;
            const auto v = std::ranges::find_if(kSpeechSound, [&](const auto& e) { return e.first == speech.string; });
            if (v != kSpeechSound.end()) { speech.voice = v->second; audio.play_voice(*scene, v->second); }
        }
        if (speech.npc >= 0 && (speech.done(ms) || mouse.press_this_frame)) {
            menu_click = mouse.press_this_frame;          // a click skips the speech
            speech = {};
        } else if (npc_menu.npc >= 0 && mouse.press_this_frame) {
            const int li = npc_menu.line_at(mouse.x, mouse.y);
            const auto action = li >= 0 ? npc_menu.lines[std::size_t(li)].action : NpcMenuState::kClose;
            const int npc_menu_arg = li >= 0 ? npc_menu.lines[std::size_t(li)].arg : -1;
            const int who = npc_menu.npc;
            const auto& n = level->npcs[std::size_t(who)];
            const auto& st = npc_states[std::size_t(who)];
            const float dx = (n.path.empty() ? n.x : st.x) - player.x, dy = (n.path.empty() ? n.y : st.y) - player.y;
            const int sx = int(kW) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
            const int sy = int(kH) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2)));
            npc_menu = {};
            if (action == NpcMenuState::kHire) {
                // Kashya's list. ponytail: the server's offer count
                // isn't traced; five, rolled when the list opens.
                hire_offers.clear();
                for (int k = 0; k < 5; ++k)
                    if (auto o = d2d::rules::merc_offer(scene->rules, cc.expansion, 0, cc.header.active_difficulty(),
                                                        int(cc.stats.get(d2d::d2s::kLevel)), rng))
                        hire_offers.push_back(*o);
                npc_menu = open_hire_menu(*scene, who, hire_offers,
                                          cc.stats.get(d2d::d2s::kGold) + cc.stats.get(d2d::d2s::kGoldBank));
            } else if (action == NpcMenuState::kHireOffer) {
                const int k = npc_menu_arg;
                if (k >= 0 && std::size_t(k) < hire_offers.size()
                    && d2d::rules::hire(hire_offers[std::size_t(k)], cc.header, cc.stats))
                    spawn_merc();
            } else if (action == NpcMenuState::kIdentify) {
                d2d::rules::identify_all(cc.items);
            } else if (action == NpcMenuState::kGamble) {
                store = d2d::rules::open_gamble(scene->rules, n.id, int(cc.stats.get(d2d::d2s::kLevel)));
                store.npc = who;
                store.header = cc.header;
                inv_open = true; char_open = stash_open = cube_open = false;
            } else if (action == NpcMenuState::kTrade) {
                store = open_store(*scene, *level, who, rng);
                store.header = cc.header;
                inv_open = true; char_open = stash_open = cube_open = false;
            } else if (action == NpcMenuState::kTalk) {
                npc_menu = open_talk_menu(*scene, *level, who, sx, sy);
            } else if (action == NpcMenuState::kIntro || action == NpcMenuState::kGossip) {
                const auto t = std::ranges::find_if(kNpcTalk, [&](const NpcTalk& e) { return e.hc_idx == n.hc_idx; });
                if (t != kNpcTalk.end() && !t->topics.empty()) {
                    const int cls = int(kUiToSaveClass[std::max(cc.selected, 0)]);
                    if (gossip_pick.size() != level->npcs.size()) gossip_pick.assign(level->npcs.size(), -1);
                    int topic;
                    auto done = [&](int q) { return cc.header.quest_flag(cc.header.active_difficulty(), q, 0); };
                    if (action == NpcMenuState::kIntro) {
                        topic = talk_topic(*t, true, cls, rng, done);
                    } else {
                        if (gossip_pick[std::size_t(who)] < 0)
                            gossip_pick[std::size_t(who)] = talk_topic(*t, false, cls, rng, done);
                        topic = gossip_pick[std::size_t(who)];
                    }
                    speech = start_speech(*scene, who, t->topics[std::size_t(topic)].string, ms);
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
                const int bx = kCharPanelX - 1 + kBtnX[i], by = 476 - 32 + 1;
                const bool on = mouse.x >= bx && mouse.x < bx + 32 && mouse.y >= by && mouse.y < by + 32;
                if (on && mouse.down) store.pressed[std::size_t(i)] = true;
                if (on && mouse.release_this_frame && i < 2) store.mode = store.mode == i + 1 ? 0 : i + 1;
                // Repair vendors: repair (6) toggles like buy/sell,
                // repair all (18) fixes everything worn and carried.
                const bool repairer = store_button_frames(store)[2] == 6;
                if (on && mouse.release_this_frame && i == 2 && repairer) store.mode = store.mode == 3 ? 0 : 3;
                if (on && mouse.release_this_frame && i == 3 && repairer)
                    d2d::rules::store_repair_all(scene->rules, store, cc.items, cc.stats);
                if (on && mouse.release_this_frame && i == 3 && store_button_frames(store)[3] == 10) {
                    store = {}; inv_open = false;
                    break;
                }
            }
            if (mouse.press_this_frame && mouse.y >= 60 && mouse.y <= 90
                && mouse.x >= kCharPanelX && mouse.x < kCharPanelX + 320)
                store.tab = (mouse.x - kCharPanelX) / 80;
            // Right-click on stock buys; with Buy or Sell toggled on,
            // a left click buys the stock item / sells your item.
            const int si = store.npc >= 0 ? store_item_at(*scene, store, mouse.x, mouse.y) : -1;
            if (si >= 0 && (mouse.rpress_this_frame || (mouse.press_this_frame && store.mode == 1)))
            {
                if (store.gamble) {
                        d2d::rules::store_gamble(scene->rules, store, si, cc.items, cc.stats, rng);
                    } else {
                    d2d::rules::store_buy(scene->rules, store, si, cc.items, cc.stats);
                }
            }
            // Sell: an inventory item; repair: that or a worn one.
            if (store.npc >= 0 && mouse.press_this_frame && (store.mode == 2 || store.mode == 3))
                for (std::size_t i = 0; i < cc.items.size(); ++i) {
                    const auto& it = cc.items[i];
                    const bool worn = it.location == 1 && it.slot >= 1 && it.slot <= 10;
                    if (!(it.location == 0 && it.panel == 1) && !(worn && store.mode == 3)) continue;
                    const auto r = worn ? lay.slots[std::size_t(it.slot)] : grid_rect(*scene, lay, it);
                    if (mouse.x >= r[0] && mouse.x < r[0] + r[2] && mouse.y >= r[1] && mouse.y < r[1] + r[3]) {
                        if (store.mode == 2) d2d::rules::store_sell(scene->rules, store, i, cc.items, cc.stats);
                        else d2d::rules::store_repair(scene->rules, store, cc.items[i], cc.stats);
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
                if (const int t = waypoint_tab_at(cc.header, cc.expansion, mouse.x, mouse.y); t >= 0) waypoint.tab = t;
                else if (const int row = waypoint_row_at(*scene, waypoint, cc.header, mouse.x, mouse.y); row >= 0) {
                    d2d::log::info("not implemented: waypoint travel to {}",
                                   scene->waypoint_levels[std::size_t(waypoint.tab)][std::size_t(row)].name);
                    waypoint = {};
                }
            }
            if (on_cancel && mouse.release_this_frame) waypoint = {};
        }
        // Holding an item, the world doesn't take clicks.
        // ponytail: D2 drops it on the ground; no ground items yet.
        const bool over_ui = over_panel || over_belt || menu_click || npc_menu.npc >= 0 || item_click || held;
        if (have_world) walk(mouse, over_ui, ms, float(ms - last_ms) / 1000.f);
        if (have_world) cross_level(ms);
        draw(fb, mouse, ms);
    }

    // What the character wears (the save's appearance, else the class's starting gear).
    [[nodiscard]] const Scene::Appearance& gfx() const {
        return cc.appearance ? *cc.appearance : scene->starting_gear[std::size_t(kUiToSaveClass[std::max(cc.selected, 0)])];
    }
    [[nodiscard]] const Scene::PlayerAnim& player_anim(int mode) const {
        return scene->composite(kUiToSaveClass[std::max(cc.selected, 0)], mode, gfx());
    }
    void set_pmode(int mode, std::uint32_t ms) {
        pmode = mode;
        player.mode_ms = ms;
        player.walking = false;
        player.path.clear();
        pmode_until = mode == kModeDD ? 0 : ms + player_anim(mode).length_ms();
    }
    [[nodiscard]] bool dead() const { return pmode == kModeDT || pmode == kModeDD; }

    // Back in camp after dying: at the town start with full life. Monsters
    // stay as they are.
    // ponytail: D2 leaves a corpse holding the gear and takes gold; not yet.
    void respawn(std::uint32_t ms) {
        using namespace d2d::d2s;
        if (level != &scene->town) {
            if (scene->town.layer != level->layer) {
                other_automaps[level->layer] = std::move(automap);
                automap = std::move(other_automaps[scene->town.layer]);
            }
            level = &scene->town;
            npc_states = npc_start(*level);
            level_ms = ms;
        }
        std::tie(player.x, player.y) = level->start.first >= 0 ? level->start : std::pair{ player.x, player.y };
        std::tie(player.x, player.y) = level->nearest_free(player.x, player.y);
        target_x = player.x; target_y = player.y;
        cc.stats.v[kLife] = cc.stats.v[kMaxLife];
        pmode = -1; player.mode_ms = ms; attack_mon = -1;
        if (merc) { merc->x = player.x + 1; merc->y = player.y + 1; merc->path.clear(); }
        d2d::log::info("respawned in the Rogue Encampment");
    }

    // The player's swing: the hit lands on the attack's event frame (MonStats
    // defense, attack rating from dexterity, ToHitFactor and items); a kill
    // gives experience (and maybe levels) and scatters a Fallen pack.
    // ponytail: no weapon speed (WSM/IAS) on the swing's length.
    void strike(std::uint32_t ms) {
        if (pstruck || attack_mon < 0 || ms < player.mode_ms + player_anim(kModeA1).action_ms()) return;
        pstruck = true;
        auto& m = monsters[std::size_t(attack_mon)];
        if (!m.alive() || std::hypot(m.u.x - player.x, m.u.y - player.y) > kMeleeReach + 0.5f) return;
        const auto sc = std::size_t(kUiToSaveClass[std::max(cc.selected, 0)]);
        const auto a = d2d::rules::player_attack(scene->rules, cc.items, cc.stats, scene->class_gains[sc].to_hit);
        const int clvl = int(cc.stats.get(d2d::d2s::kLevel));
        if (rng(100) >= d2d::rules::hit_chance(a.ar, m.st.ac, clvl, m.st.level)) return;
        if (!hurt(*scene, m, rng.range(a.min, a.max), ms)) return;
        const auto exp = d2d::rules::kill_exp(m.st.exp, clvl, m.st.level);
        const int up = d2d::rules::gain_exp(cc.stats, exp, scene->exp_next, scene->class_gains[sc]);
        d2d::log::info("killed {} (+{} exp){}", m.npc.name, exp, up ? std::format(", level {}", cc.stats.get(d2d::d2s::kLevel)) : "");
        if (up) cc.panel = panel_stats(*scene, cc.header, cc.items, cc.stats);
        fallen_scatter(*scene, monsters, std::size_t(attack_mon), rng, ms);
        drop_loot(m, ms);
        attack_mon = -1;
    }

    // The monster / ground item under the cursor (hovered_npc -10 - i / -1000 - i), or -1.
    [[nodiscard]] int hovered_monster() const {
        return hovered_npc <= -10 && hovered_npc > -1000 && std::size_t(-10 - hovered_npc) < monsters.size() ? -10 - hovered_npc : -1;
    }
    [[nodiscard]] int hovered_ground() const {
        return hovered_npc <= -1000 && std::size_t(-1000 - hovered_npc) < ground.size() ? -1000 - hovered_npc : -1;
    }

    // A kill's loot (MonStats TreasureClass1 for the difficulty) round
    // where it fell. Magic and better come unidentified.
    // ponytail: D2 spreads drops by its own pattern (not traced); here each
    // goes to the nearest free spot within half a cell.
    void drop_loot(const Monster& m, std::uint32_t ms) {
        const int diff = cc.header.active_difficulty();
        const auto& tc = scene->monsters.types[std::size_t(m.type)].diff[std::size_t(std::clamp(diff, 0, 2))].tc;
        std::vector<d2d::rules::Drop> drops;
        d2d::rules::roll_drops(scene->rules, tc, m.st.level, rng, drops);
        for (const auto& d : drops) {
            GroundItem g;
            std::tie(g.x, g.y) = level->nearest_free(m.u.x + float(rng(11) - 5) / 10, m.u.y + float(rng(11) - 5) / 10);
            g.ms = ms;
            if (d.code == "gld") {
                g.item.code = "gld";
                g.gold = d.gold;
                g.label = std::to_string(d.gold) + " Gold";
            } else {
                g.item = d2d::rules::generate_item(scene->rules, d.code, m.st.level, d.quality, rng);
                g.item.identified = d.quality <= 3;
                const auto lines = item_lines(*scene, g.item, int(cc.stats.get(d2d::d2s::kLevel)));
                if (!lines.empty()) { g.label = lines[0].text; g.rgb = lines[0].rgb; }
            }
            if (!scene->flippy(g.item.code)) continue;
            ground.push_back(std::move(g));
        }
    }

    // Picking up: gold into the purse (up to 10000 per character level),
    // an item into the first inventory spot it fits.
    // ponytail: potions don't go to the belt first; no "no room" sound.
    void take(std::size_t i) {
        using namespace d2d::d2s;
        auto& g = ground[i];
        if (g.item.code == "gld") {
            const auto cap = cc.stats.get(kLevel) * 10000, room = std::max<std::int64_t>(cap - cc.stats.get(kGold), 0);
            const auto n = std::min<std::int64_t>(g.gold, room);
            if (n <= 0) return;
            cc.stats.v[kGold] += n;
            if ((g.gold -= int(n)) > 0) { g.label = std::to_string(g.gold) + " Gold"; return; }
        } else {
            const auto& lay = scene->inv_layout[std::size_t(kUiToSaveClass[std::max(cc.selected, 0)])];
            std::vector<const Item*> inv;
            for (const auto& x : cc.items) if (x.location == 0 && x.panel == 1) inv.push_back(&x);
            const auto [w, h] = d2d::rules::item_size(scene->rules, g.item.code);
            const auto [x, y] = d2d::rules::free_spot(scene->rules, inv, lay.cols ? lay.cols : 10, lay.rows ? lay.rows : 4, w, h);
            if (x < 0) { d2d::log::info("no room for {}", g.label); return; }
            g.item.location = 0; g.item.panel = 1; g.item.column = x; g.item.row = y;
            cc.items.push_back(std::move(g.item));
        }
        ground.erase(ground.begin() + std::ptrdiff_t(i));
    }

    // Leaving the level: past its edge, collision and drawing already
    // use the level next to it in the act (Level::near), so the player
    // walks straight on; once they stand outside this map they belong to
    // that level — everything moves by the offset between the two. A
    // click toward a level the layout placed but d2d doesn't build yet
    // (Cold Plains) is logged.
    void cross_level(std::uint32_t ms) {
        const float wx = float(level->world_x) + target_x, wy = float(level->world_y) + target_y;
        bool known = level->inside(target_x, target_y);
        for (const auto& n : level->near) known = known || n.level->inside(target_x - float(n.dx), target_y - float(n.dy));
        if (!known)
            for (const auto& p : scene->act1_layout)
                if (wx >= float(p.x) && wy >= float(p.y) && wx < float(p.x + p.w) && wy < float(p.y + p.h)
                    && std::ranges::find(not_there, p.level) == not_there.end()) {
                    not_there.push_back(p.level);
                    d2d::log::info("not implemented: level {} (the player headed there from level {})", p.level, level->id);
                }
        if (level->inside(player.x, player.y)) return;
        for (const auto& n : level->near) {
            if (!n.level->inside(player.x - float(n.dx), player.y - float(n.dy))) continue;
            const float dx = float(n.dx), dy = float(n.dy);
            auto shift = [&](UnitState& u) {
                u.x -= dx; u.y -= dy;
                u.goal_x -= dx; u.goal_y -= dy;
                for (auto& [px, py] : u.path) { px -= dx; py -= dy; }
            };
            shift(player);
            target_x -= dx; target_y -= dy;
            if (merc) shift(*merc);
            if (n.level->layer != level->layer) {
                other_automaps[level->layer] = std::move(automap);
                automap = std::move(other_automaps[n.level->layer]);
                automap.open = other_automaps[level->layer].open;
            }
            level = n.level;
            npc_states = npc_start(*level);
            hovered_npc = interact_npc = -1;
            npc_menu = {}; store = {}; speech = {}; waypoint = {};
            level_ms = ms;                                  // its song comes in 3 s later
            d2d::log::info("level: {} at ({:.1f}, {:.1f})", level->id == 1 ? "Rogue Encampment" : "Blood Moor", player.x, player.y);
            return;
        }
    }

    // Walking: a click on the ground (not over the UI) sets the target and
    // an object to operate on arrival; the player follows a walk_path; NPCs
    // patrol; the merc follows.
    void walk(const Mouse& mouse, bool over_ui, std::uint32_t ms, float dt) {
        Crowd crowd;                           // who's in whose way this frame
        crowd.units.push_back(&player);
        if (merc) crowd.units.push_back(&*merc);
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            if (!level->npcs[i].path.empty() && !npc_states[i].hidden) crowd.units.push_back(&npc_states[i]);
        const bool in_moor = level == &scene->moor;
        if (in_moor)                           // the monsters around the player
            for (auto& m : monsters)
                if (m.alive() && std::abs(m.u.x - player.x) < 12 && std::abs(m.u.y - player.y) < 12) crowd.units.push_back(&m.u);
        // Dead: the death plays out, then a click (or Esc) respawns in camp.
        if (dead()) {
            if (pmode == kModeDT && ms >= pmode_until) set_pmode(kModeDD, ms);
            if (pmode == kModeDD && mouse.press_this_frame) respawn(ms);
            return;
        }
        // A swing or a flinch holds the player in place until it ends; held
        // down on the same monster, the next swing follows.
        if (pmode == kModeA1) {
            strike(ms);
            if (ms >= pmode_until) {
                pmode = -1; player.mode_ms = ms;
                if (!mouse.down) attack_mon = -1;
            }
        } else if (pmode == kModeGH && ms >= pmode_until) {
            pmode = -1; player.mode_ms = ms;
        }
        const bool busy = pmode >= 0;
        if (!busy && (mouse.down || mouse.press_this_frame) && !over_ui) {
            // Screen -> world: invert the iso projection around
            // the player, who sits at (kW/2, kH/2 + kIsoH/2).
            const float u = float(mouse.x - int(kW) / 2) / (kIsoW / 2);
            const float v = float(mouse.y - int(kH) / 2 - kIsoH / 2) / (kIsoH / 2);
            target_x = player.x + (u + v) / 2;
            target_y = player.y + (v - u) / 2;
            player.walking = true;
            // Clicking an object you can operate walks to it
            // first (D2 operates on arrival).
            interact_npc = -1;
            if (mouse.press_this_frame) attack_mon = pick_item = -1;
            if (mouse.press_this_frame && hovered_monster() >= 0 && monsters[std::size_t(hovered_monster())].alive())
                attack_mon = hovered_monster();       // walk up to it, then attack
            if (mouse.press_this_frame && hovered_ground() >= 0) pick_item = hovered_ground();   // walk to it, pick it up
            if (mouse.press_this_frame && hovered_npc >= 0) {
                const auto& o = level->npcs[std::size_t(hovered_npc)];
                const bool menu = std::ranges::any_of(kNpcMenus, [&](const NpcMenu& e) { return e.hc_idx == o.hc_idx; });
                if (o.operate_fn == 32 || o.operate_fn == 23 || (o.root == "monsters" && menu)) {
                    interact_npc = hovered_npc;
                    const auto& st = npc_states[std::size_t(hovered_npc)];
                    target_x = o.path.empty() ? o.x : st.x;
                    target_y = o.path.empty() ? o.y : st.y;
                }
            }
        }
        // Close enough to the stash: open it with the inventory.
        // ponytail: 2 cells, not D2's per-object operate range.
        if (interact_npc >= 0) {
            const auto& o = level->npcs[std::size_t(interact_npc)];
            const auto& st = npc_states[std::size_t(interact_npc)];
            const float ox = o.path.empty() ? o.x : st.x, oy = o.path.empty() ? o.y : st.y;
            if (std::hypot(ox - player.x, oy - player.y) < 2.f) {
                if (o.operate_fn == 32) {
                    stash_open = inv_open = true; char_open = false;
                } else if (o.operate_fn == 23) {
                    // Touching it activates it (the town's: wp 0).
                    // ponytail: town only; a wilderness waypoint would need its level.
                    cc.header.waypoints[std::size_t(cc.header.active_difficulty())][0] |= 1;
                    waypoint = { .open = true };
                    inv_open = char_open = stash_open = cube_open = false;
                } else {
                    // The NPC's feet on screen, as render_world projects them.
                    const float dx = ox - player.x, dy = oy - player.y;
                    if (d2d::rules::is_healer(o.hc_idx)) d2d::rules::heal(cc.stats);
                    npc_menu = open_npc_menu(*scene, *level, interact_npc,
                        int(kW) / 2 + int(std::lround((dx - dy) * (kIsoW / 2))),
                        int(kH) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))),
                        int(cc.stats.get(d2d::d2s::kLevel)), d2d::rules::unidentified(cc.items));
                }
                player.walking = false; interact_npc = -1;
            } else if (!player.walking) {
                interact_npc = -1;                         // blocked on the way
            } else {
                target_x = ox; target_y = oy;              // follow a walking NPC
            }
        }
        if (pick_item >= 0 && !busy) {
            const auto& g = ground[std::size_t(pick_item)];
            if (std::hypot(g.x - player.x, g.y - player.y) <= 1.f) {
                take(std::size_t(pick_item));
                pick_item = -1; player.walking = false; player.path.clear();
            } else {
                target_x = g.x; target_y = g.y; player.walking = true;
            }
        }
        if (attack_mon >= 0 && !busy) {
            const auto& m = monsters[std::size_t(attack_mon)];
            if (!m.alive()) attack_mon = -1;
            else if (std::hypot(m.u.x - player.x, m.u.y - player.y) <= kMeleeReach) {
                player.dir = direction16(m.u.x - player.x, m.u.y - player.y);
                set_pmode(kModeA1, ms);
                pstruck = false;
            } else {
                target_x = m.u.x; target_y = m.u.y; player.walking = true;
            }
        }
        if (player.walking && pmode < 0) {
            // A route to the target, re-planned when the target
            // moves off its end (dragging, a walking NPC).
            if (player.path.empty() || std::hypot(player.goal_x - target_x, player.goal_y - target_y) > 0.3f) {
                player.path = walk_path(*level, player.x, player.y, target_x, target_y, crowd, &player);
                player.goal_x = target_x; player.goal_y = target_y;
            }
            const auto sc = std::size_t(kUiToSaveClass[std::max(cc.selected, 0)]);
            const float vel = float(running ? scene->run_velocity[sc] : scene->walk_velocity[sc]);
            player.walking = follow_path(*level, player, cells_per_sec(vel) * dt, crowd);
            if (!player.walking) player.path.clear();
        }
        npc_patrol(*level, npc_states, { npc_menu.npc, speech.npc, store.npc }, ms, dt, crowd);
        // Monsters think while the player is near (D2 runs the rooms
        // around each player); what they hit comes off the player's life,
        // and a hit of a twelfth of max life or more makes them flinch (GH).
        if (in_moor) {
            Foe foe{ player.x, player.y, int(cc.panel.defense), int(cc.stats.get(d2d::d2s::kLevel)), true };
            for (auto& m : monsters)
                if (std::abs(m.u.x - player.x) < 30 && std::abs(m.u.y - player.y) < 30)
                    monster_update(*scene, *level, m, foe, rng, ms, dt, crowd);
            if (foe.damage > 0) {
                using namespace d2d::d2s;
                cc.stats.v[kLife] -= std::int64_t(foe.damage) << 8;
                if (cc.stats.v[kLife] <= 0) {
                    cc.stats.v[kLife] = 0;
                    set_pmode(kModeDT, ms);
                    attack_mon = -1;
                    d2d::log::info("the player died");
                } else if (std::int64_t(foe.damage) * 12 >= cc.stats.fixed(kMaxLife) && pmode != kModeA1) {
                    set_pmode(kModeGH, ms);
                }
            }
        }
        if (merc) {
            const auto sc = std::size_t(kUiToSaveClass[std::max(cc.selected, 0)]);
            merc_follow(*level, *merc, player.x, player.y,
                        cells_per_sec(float(scene->run_velocity[sc])) * 1.1f, ms, dt, crowd);
        }
    }

    // The frame: the world with its units, the open panels, the tree and
    // the waypoint panel.
    void draw(std::vector<std::uint8_t>& fb, const Mouse& mouse, std::uint32_t ms) {
        if (const bool m = player.walking && running; pmode < 0 && (player.walking != player_walked || m != player_ran)) {
            player_walked = player.walking; player_ran = m; player.mode_ms = ms;
        }
        const int ui_cls = std::max(cc.selected, 0);
        // Monsters in view, as units the world draws by depth.
        std::vector<Unit> extra;
        if (level == &scene->moor)
            for (std::size_t i = 0; i < ground.size(); ++i) {
                const auto& g = ground[i];
                if (std::abs(g.x - player.x) >= 14 || std::abs(g.y - player.y) >= 14) continue;
                Unit u{ g.x, g.y, nullptr, 0, &g.label, g.ms, -1000 - int(i) };
                u.sprite = scene->flippy(g.item.code);
                u.rgb = g.rgb;
                extra.push_back(u);
            }
        if (level == &scene->moor)
            for (std::size_t i = 0; i < monsters.size(); ++i) {
                const auto& m = monsters[i];
                if (std::abs(m.u.x - player.x) >= 14 || std::abs(m.u.y - player.y) >= 14) continue;
                extra.push_back({ m.u.x, m.u.y, &scene->npc_anim(m.npc, m.mode), m.u.dir,
                                  m.alive() ? &m.npc.name : nullptr, m.u.mode_ms, -10 - int(i) });
            }
        const bool town = level->id == 1;             // TN/TW in town, NU/WL outside
        // A dead player has no DD composite: DT held on its last frame.
        if (pmode == kModeDD) player.mode_ms = ms - (player_anim(kModeDT).length_ms() - 1);
        const int mode = pmode == kModeDD ? kModeDT : pmode >= 0 ? pmode : player.walking ? (running ? kModeRN : town ? kModeTW : kModeWL) : town ? kModeTN : kModeNU;
        render_ingame(fb, *scene, *level, ui_cls,
                      gfx(),
                      cc.input_name, cc.hardcore,
                      player.x, player.y, mode,
                      player.dir, ms, held ? -1 : mouse.x, held ? -1 : mouse.y, npc_states,
                      inv_open ? &cc.items : nullptr,
                      char_open ? &cc.stats : nullptr, &cc.stats, &cc.panel, player.mode_ms, &cc.items,
                      &hovered_npc, stash_open || cube_open ? &cc.items : nullptr, cc.expansion, belt_open,
                      cube_open, &npc_menu, &speech, &automap, &store, stat_pressed,
                      merc_npc, merc ? &*merc : nullptr, &merc_label, extra);
        if (hovered_monster() >= 0)
            draw_monster_bar(fb, *scene, monsters[std::size_t(hovered_monster())]);
        else if (attack_mon >= 0)
            draw_monster_bar(fb, *scene, monsters[std::size_t(attack_mon)]);
        if (pmode == kModeDD) {                    // ponytail: D2's death screen text isn't traced
            const std::string msg = "You have died.  Click or press Esc to continue.";
            const auto& pal = scene->act1_pal.entries().empty() ? scene->pal : scene->act1_pal;
            scene->font.draw_tinted(fb, kW, kH, pal, int(kW) / 2 - scene->font.measure(msg) / 2, int(kH) / 2 - 60, msg, 220, 60, 60);
        }
        if (tree_open)
            draw_skill_tree(fb, *scene, int(kUiToSaveClass[ui_cls]), tree_tab, cc.stats.skills, cc.stats,
                            skill_pressed, held ? -1 : mouse.x, held ? -1 : mouse.y);
        if (waypoint.open)
            draw_waypoints(fb, *scene, waypoint, cc.header, cc.expansion, 1, mouse.x, mouse.y);
    }
};

}  // namespace
