// The town: the in-game character's world state (position, walking,
// panels, NPC menus, store, merc, ...) and one frame of play — input,
// movement, NPCs, then the render.
#pragma once

#include "server.hpp"
#include "skillbar.hpp"

namespace {

// The client (docs/design/multiplayer.md): input, panels, camera,
// drawing and sound, over a World (server.hpp) it sends commands to. The
// references below are the World's, for the code that reads them.
struct Town {
    const Scene* scene = nullptr;
    CharCreateUI& cc;                      // the in-game character (save, items, stats)
    World world;                           // the game server's side (in-process: single player)
    const Level*& level = world.level;     // where the character is: the town, the Blood Moor, the Den of Evil
    UnitState& player = world.player;      // the camera follows
    std::vector<UnitState>& npc_states = world.npc_states;
    std::optional<UnitState>& merc = world.merc;
    const Npc*& merc_npc = world.merc_npc;
    bool& running = world.running;         // R toggles, like D2's run/walk button
    d2d::rules::Rng& rng = world.rng;
    Cues& cues = world.cues;
    Loot& loot = world.loot;
    Fight& fight = world.fight;
    std::map<std::pair<const Level*, int>, std::uint32_t>& operated = world.operated;
    std::vector<World::Fire>& fires = world.fires;
    float& target_x = world.target_x;
    float& target_y = world.target_y;
    int& take_warp = world.take_warp;
    int& interact_npc = world.interact_npc;
    int& pick_item = world.pick_item;
    std::vector<Command> queued;           // sent since the World's last tick
    std::uint32_t world_ms = 0;            // the World's clock: when it last ticked
    float prev_x = 0, prev_y = 0;          // the player a tick before: the camera slides between the two
    float cam_x = 0, cam_y = 0;            // where the camera is this frame
    std::unordered_map<int, Automap> other_automaps;   // other Layers' maps, while elsewhere
    std::uint32_t level_ms = 0;            // when the player entered `level`
    bool have_world = false;
    bool  stash_open = false;
    std::optional<d2d::d2s::Item> held;   // the item on the cursor
    int   stat_pressed = -1;               // char panel stat button held down
    bool  tree_open = false;               // skill tree ('T')
    int   tree_tab = 1;                    // 1..3, bottom tab first (0x724bec starts at 1)
    int   skill_pressed = -1;              // skill icon held down
    std::vector<d2d::rules::MercOffer> hire_offers;   // Kashya's list while it's open
    std::string merc_label;
    bool  belt_open = false;               // belt popup (` key or a click on the belt)
    bool  cube_open = false;               // right-click the Horadric Cube item
    NpcMenuState npc_menu;                 // open NPC menu (npc < 0: none)
    Automap automap;                       // Tab
    Store store;                           // an open vendor store (npc < 0: none)
    WaypointUI waypoint;                   // the waypoint panel
    Speech speech;                         // NPC talking (npc < 0: none)
    std::vector<int> gossip_pick;          // per world NPC: chosen gossip topic, -1 = not yet
    SkillBar skillbar{ scene, cc };        // the skill buttons, picker and hotkeys (skillbar.hpp)
    int   hovered_npc = -1;                // Level::npcs index under the cursor (last frame); <= -10: monster -10 - i
    std::uint32_t now_ms = 0;              // this frame's ms (devctl)   // shrines / chests used: when
    bool  player_walked = false;           // `walking` as of the last frame
    bool  player_ran = false;
    bool  inv_open = false;   // 'I' — inventory panel
    bool  char_open = false;  // 'C' — character panel

    Town(const Scene* s, CharCreateUI& c, int start_x = -1, int start_y = -1)
        : scene(s), cc(c), world(s, c, start_x, start_y) {
        have_world = level && !level->dt1s.empty();
        fight.skill_base = [this](int id) { return skillbar.base_level(id); };
        fight.skill_level = [this](int id) { return skillbar.level(id); };
    }

    // Saving the character: the item on the cursor goes back first, then
    // the World writes it.
    std::string save() {
        stow_held(*scene, cc.items, held);
        return world.save();
    }

    // devctl: operate object i now, as the server would on arrival.
    void operate(int i, std::uint32_t ms, int force = -1) { world.operate(i, ms, force); }

    // A fresh game for the character: the Blood Moor's monsters at its
    // difficulty, no loot about.
    void new_game() {
        world.new_game();
        skillbar.new_game();
    }

    // The character's merc (cc.header) next to the player, if alive.
    void spawn_merc() {
        world.spawn_merc();
        if (const auto m = scene->mercs.find(cc.header.merc_type); merc && m != scene->mercs.end())
            merc_label = merc_name(*scene, m->second, cc.header.merc_name);
    }

    // One InGame frame: keys, panels, clicks, walking, NPCs, then the render.
    // Esc with nothing open goes back to the roster (screen).
    void update(std::vector<std::uint8_t>& fb, Mouse& mouse, const std::vector<SDL_Keycode>& keys_this_frame,
                Screen& screen, Audio& audio, std::uint32_t ms, std::uint32_t last_ms) {
        now_ms = ms;
        // ESC handled globally in handle_sdl_events (returns to Title).
        // D2 movement: press or hold the left button on the ground
        // and the character walks toward that point (the target
        // tracks the cursor while held); the camera follows.
        for (const auto k : keys_this_frame) {
            if (k == SDLK_I) { inv_open = !inv_open; if (inv_open) tree_open = false; }
            if (k == SDLK_T) { tree_open = !tree_open; if (tree_open) inv_open = false; }   // both right-hand panels
            if (k == SDLK_R) running = !running;              // D2's run/walk toggle
            skillbar.key(k, mouse.x, mouse.y);                // F1-F8
            if (k == SDLK_GRAVE) belt_open = !belt_open;      // D2's "Show Belt" key
            if (k == SDLK_TAB) automap.open = !automap.open;  // D2's automap toggle
            if (k == SDLK_C) { char_open = !char_open; if (char_open) stash_open = cube_open = false; }
            if (k == SDLK_ESCAPE && fight.dead()) { if (fight.pmode == kModeDD) world.respawn(ms); continue; }
            if (k >= SDLK_1 && k <= SDLK_4) queued.push_back(cmd::UseBelt{ int(k - SDLK_1) });
            if (k == SDLK_ESCAPE && skillbar.picking) { skillbar.picking = 0; continue; }   // the picker first
            if (k == SDLK_ESCAPE) {
                if (waypoint.open) waypoint = {};
                else if (store.npc >= 0) { store = {}; inv_open = false; } // the store first
                else if (speech.npc >= 0) speech = {};              // then speech
                else if (npc_menu.npc >= 0) npc_menu = {};          // then the menu
                else if (inv_open || char_open || stash_open || cube_open || tree_open)   // then panels
                    inv_open = char_open = stash_open = cube_open = tree_open = false;
                else { save(); screen = Screen::CharSelect; }
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
                if (skill_pressed >= 0 && sk == skill_pressed) queued.push_back(cmd::SkillPoint{ sk });
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
                    queued.push_back(cmd::StatPoint{ kStatButtons[std::size_t(sb)].stat, n });
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
        for (const auto& n : level->nearby)                   // what's in view across the edge
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
        const bool bar_click = skillbar.click(mouse);
        const bool over_ui = over_panel || over_belt || menu_click || npc_menu.npc >= 0 || item_click || held || bar_click;
        if (have_world) walk(mouse, over_ui, ms, last_ms);
        cues.play(audio, player.x, player.y, rng, ms);
        draw(fb, mouse, ms);
    }

    // The monster / ground item under the cursor (hovered_npc -10 - i / -1000 - i), or -1.
    [[nodiscard]] int hovered_monster() const {
        return hovered_npc <= -10 && hovered_npc > -1000 && std::size_t(-10 - hovered_npc) < fight.monsters.size() ? -10 - hovered_npc : -1;
    }
    [[nodiscard]] int hovered_ground() const {
        return hovered_npc <= -1000 && std::size_t(-1000 - hovered_npc) < loot.ground.size() ? -1000 - hovered_npc : -1;
    }

    // The client's side of a click: what it asks the server for
    // (protocol.hpp). A held left button re-aims the walk; a press picks
    // what's under the cursor: a monster to attack, an item, an object or
    // NPC, else the ground. The right button uses the right skill there.
    [[nodiscard]] std::vector<Command> input(const Mouse& mouse, bool over_ui) const {
        std::vector<Command> out;
        if (fight.dead()) {                                  // a click, once the death has played, respawns
            if (mouse.press_this_frame) out.push_back(cmd::Resurrect{});
            return out;
        }
        if (over_ui) return out;
        // Screen -> world: invert the iso projection around the player,
        // who sits at (kW/2, kH/2 + kIsoH/2).
        const float u = float(mouse.x - int(kW) / 2) / (kIsoW / 2);
        const float v = float(mouse.y - int(kH) / 2 - kIsoH / 2) / (kIsoH / 2);
        const float wx = cam_x + (u + v) / 2, wy = cam_y + (v - u) / 2;
        const int hm = hovered_monster();
        const bool live = hm >= 0 && fight.monsters[std::size_t(hm)].alive();
        if (mouse.press_this_frame) {
            if (live) out.push_back(cmd::UseSkill{ skillbar.left, wx, wy, fight.monsters[std::size_t(hm)].id, true });
            else if (hovered_ground() >= 0) out.push_back(cmd::Pickup{ loot.ground[std::size_t(hovered_ground())].id });
            else if (hovered_npc >= 0) out.push_back(cmd::Interact{ hovered_npc });
            else out.push_back(cmd::Move{ wx, wy, true });
        } else if (mouse.down) {                             // held: the attack goes on, else the walk re-aims
            const int am = fight.attack_mon;
            if (am >= 0 && std::size_t(am) < fight.monsters.size() && fight.monsters[std::size_t(am)].alive())
                out.push_back(cmd::UseSkill{ fight.attack_skill, wx, wy, fight.monsters[std::size_t(am)].id, true });
            else
                out.push_back(cmd::Move{ wx, wy, false });
        }
        if (mouse.rpress_this_frame) out.push_back(cmd::UseSkill{ skillbar.right, wx, wy, live ? fight.monsters[std::size_t(hm)].id : -1 });
        return out;
    }

    // The game this frame: what the player asks for (input), the World's
    // step, then what it told the client (events).
    void walk(const Mouse& mouse, bool over_ui, std::uint32_t ms, std::uint32_t last_ms) {
        // The skill buttons: a change goes to the World (0x3c), which runs a
        // right-button aura (a Paladin's).
        if (std::uint32_t(skillbar.left) != cc.header.left_skill) queued.push_back(cmd::SelectSkill{ skillbar.left, true });
        if (std::uint32_t(skillbar.right) != cc.header.right_skill || (fight.aura != 0) != (scene->skills.get(skillbar.right) && scene->skills.get(skillbar.right)->aura))
            queued.push_back(cmd::SelectSkill{ skillbar.right, false });
        // The skill shrine's +all skills while its boost lasts.
        skillbar.extra.clear();
        if (ms < fight.boost.until)
            for (const auto& [id, v] : fight.boost.stats) if (id == 127) skillbar.extra.push_back({ .stat = 127, .value = v });
        world.talking = { npc_menu.npc, speech.npc, store.npc };
        std::ranges::move(input(mouse, over_ui), std::back_inserter(queued));
        // Fixed ticks of kTickMs; after a stall, a few to catch up, then the
        // clock skips ahead (game.exe catches up one frame at most).
        if (world_ms == 0 || ms - world_ms > 1000) world_ms = ms - std::min<std::uint32_t>(ms - last_ms, kTickMs);
        for (int n = 0; ms - world_ms >= kTickMs && n < 5; ++n) {
            prev_x = player.x; prev_y = player.y;
            world.tick(queued, world_ms + kTickMs, world_ms);
            world_ms += kTickMs;
            queued.clear();
            for (const auto& e : world.events) handle(e, ms);
            world.events.clear();
        }
        if (ms - world_ms >= kTickMs) world_ms = ms - (ms - world_ms) % kTickMs;
    }

    // What the World told the client.
    void handle(const Event& e, std::uint32_t ms) {
        if (const auto* lc = std::get_if<ev::LevelChanged>(&e)) {
            if (lc->from->layer != level->layer) {
                other_automaps[lc->from->layer] = std::move(automap);
                automap = std::move(other_automaps[level->layer]);
                if (lc->keep_map) automap.open = other_automaps[lc->from->layer].open;
            }
            hovered_npc = pick_item = -1;
            prev_x = player.x; prev_y = player.y;           // no slide across levels
            npc_menu = {}; store = {}; speech = {}; waypoint = {};
            level_ms = ms;                                  // its song comes in 3 s later
            return;
        }
        const auto& ui = std::get<ev::OpenUI>(e);
        if (ui.kind == ev::OpenUI::stash) { stash_open = inv_open = true; char_open = false; return; }
        if (ui.kind == ev::OpenUI::waypoint) {
            waypoint = { .open = true };
            inv_open = char_open = stash_open = cube_open = false;
            return;
        }
        // The NPC's feet on screen, as render_world projects them.
        const auto& o = level->npcs[std::size_t(ui.npc)];
        const auto& st = npc_states[std::size_t(ui.npc)];
        const float dx = (o.path.empty() ? o.x : st.x) - player.x, dy = (o.path.empty() ? o.y : st.y) - player.y;
        npc_menu = open_npc_menu(*scene, *level, ui.npc,
            int(kW) / 2 + int(std::lround((dx - dy) * (kIsoW / 2))),
            int(kH) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))),
            int(cc.stats.get(d2d::d2s::kLevel)), d2d::rules::unidentified(cc.items));
    }

    // The frame: the world with its units, the open panels, the tree and
    // the waypoint panel.
    void draw(std::vector<std::uint8_t>& fb, const Mouse& mouse, std::uint32_t ms) {
        const int pmode = fight.pmode;
        if (const bool m = player.walking && running; pmode < 0 && (player.walking != player_walked || m != player_ran)) {
            player_walked = player.walking; player_ran = m; player.mode_ms = ms;
        }
        const int ui_cls = std::max(cc.selected, 0);
        // Monsters in view, as units the world draws by depth.
        std::vector<Unit> extra;
        // The camera (and the player's unit) between the World's last two
        // ticks; a jump (a warp, devctl) snaps.
        // ponytail: the other units move at the tick rate, as game.exe draws them.
        const float a = std::clamp(float(ms - world_ms) / float(kTickMs), 0.f, 1.f);
        const bool jump = std::hypot(player.x - prev_x, player.y - prev_y) > 2.f;
        cam_x = jump ? player.x : prev_x + (player.x - prev_x) * a;
        cam_y = jump ? player.y : prev_y + (player.y - prev_y) * a;
        if (level != &scene->town) loot.units(cam_x, cam_y, extra);
        for (const auto& f : fires)
            if (f.level == level) extra.push_back({ f.x, f.y, &scene->npc_anim(*f.npc, f.npc->mode), 0, nullptr, 0, -2 });
        fight.units(&merc_label, extra);
        const bool town = level->id == 1;             // TN/TW in town, NU/WL outside
        // A dead player has no DD composite: DT held on its last frame.
        if (pmode == kModeDD) player.mode_ms = ms - (fight.player_anim(kModeDT).length_ms() - 1);
        int mode = pmode == kModeDD ? kModeDT : pmode >= 0 ? pmode : player.walking ? (running ? kModeRN : town ? kModeTW : kModeWL) : town ? kModeTN : kModeNU;
        std::uint32_t mode_ms = player.mode_ms;
        float rate = pmode >= 0 && pmode != kModeDD ? fight.prate : 1.f;
        if (!fight.seq.empty() && attack_mode(pmode)) { std::tie(mode, mode_ms) = fight.seq_view(ms); rate = 1.f; }   // an SQ skill's frame
        render_ingame(fb, *scene, *level, ui_cls,
                      fight.gfx(),
                      cc.input_name, cc.hardcore,
                      cam_x, cam_y, mode,
                      player.dir, ms, held ? -1 : mouse.x, held ? -1 : mouse.y, npc_states,
                      inv_open ? &cc.items : nullptr,
                      char_open ? &cc.stats : nullptr, &cc.stats, &cc.panel, mode_ms, &cc.items,
                      &hovered_npc, stash_open || cube_open ? &cc.items : nullptr, cc.expansion, belt_open,
                      cube_open, &npc_menu, &speech, &automap, &store, stat_pressed,
                      nullptr, nullptr, nullptr, extra, rate);
        fight.overlays(fb, hovered_monster());
        skillbar.draw(fb, held ? -1 : mouse.x, held ? -1 : mouse.y);
        if (tree_open)
            draw_skill_tree(fb, *scene, int(kUiToSaveClass[ui_cls]), tree_tab, cc.stats.skills, cc.stats,
                            skill_pressed, held ? -1 : mouse.x, held ? -1 : mouse.y);
        if (waypoint.open)
            draw_waypoints(fb, *scene, waypoint, cc.header, cc.expansion, 1, mouse.x, mouse.y);
    }
};

}  // namespace
