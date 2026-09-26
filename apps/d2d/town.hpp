// The town: the in-game character's world state (position, walking,
// panels, NPC menus, store, merc, ...) and one frame of play — input,
// movement, NPCs, then the render.
#pragma once

#include "protocol.hpp"
#include "skillbar.hpp"

namespace {

struct Town {
    const Scene* scene = nullptr;
    const Level* level = nullptr;          // where the character is: the town, the Blood Moor, the Den of Evil
    int take_warp = -1;                    // the warp of `level` the player clicked and is walking to
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
    Cues  cues{ scene };                   // world sounds due later
    Loot  loot{ scene, level, cc, player, rng, cues };                       // on the ground (loot.hpp)
    Fight fight{ scene, level, cc, player, merc, merc_npc, rng, loot, cues };  // the fight (fight.hpp)
    SkillBar skillbar{ scene, cc };        // the skill buttons, picker and hotkeys (skillbar.hpp)
    int   pick_item = -1;
    int   hovered_npc = -1;                // Level::npcs index under the cursor (last frame); <= -10: monster -10 - i
    int   interact_npc = -1;               // clicked object being walked to
    std::map<std::pair<const Level*, int>, std::uint32_t> operated;
    struct Fire { const Level* level; const Npc* npc; float x, y; };
    std::vector<Fire> fires;               // chest traps 5 / 7 left these burning
    std::uint32_t now_ms = 0;              // this frame's ms (devctl)   // shrines / chests used: when
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
        if (scene) fight.new_game(0);
        target_x = player.x; target_y = player.y;
        player.dir = 4;                    // south, facing the viewer
        fight.skill_base = [this](int id) { return skillbar.base_level(id); };
        fight.skill_level = [this](int id) { return skillbar.level(id); };
    }

    // Operating a shrine (FUN_00583c70: its Shrines.txt effect) or a chest
    // (FUN_00585f60 / FUN_00585b90: it opens, its act's chest treasure class
    // drops at the area level).
    // ponytail: magic shrines (16..22) other than gem and warping only
    // log; D2's operate range is 2 cells here.
    void operate(int i, std::uint32_t ms, int force = -1) {   // force (devctl): the shrine row / trap to play
        using namespace d2d::d2s;
        const auto& o = level->npcs[std::size_t(i)];
        const int diff = cc.header.active_difficulty();
        if (o.operate_fn == 4 && o.locked) {           // a key from the inventory (FUN_0055f140: item type key)
            const auto key = std::ranges::find_if(cc.items, [](const Item& it) { return it.location == 0 && it.panel == 1 && it.code == "key"; });
            if (key == cc.items.end()) { d2d::log::info("I need a key."); return; }
            if (key->quantity > 1) --key->quantity;
            else cc.items.erase(key);
        }
        operated[{ level, i }] = ms;
        if (o.operate_fn == 4) {
            const auto& al = scene->area_level;
            auto alvl = [&](int id) { return std::size_t(id) < al.size() ? al[std::size_t(id)][std::size_t(std::clamp(diff, 0, 2))] : 1; };
            const auto [lo, hi] = d2d::rules::kChestLevels[0];
            const auto tc = d2d::rules::chest_tc(0, diff, alvl(level->id), alvl(lo), alvl(hi));
            std::vector<d2d::rules::Drop> drops;
            const int rounds = d2d::rules::chest_rounds(o.locked, rng);
            for (int n = 0; n < rounds; ++n) d2d::rules::roll_drops(scene->rules, tc, alvl(level->id), rng, drops);
            for (const auto& d : drops) loot.put(d, o.x, o.y, alvl(level->id), ms);
            d2d::log::info("opened a chest: {} x{} ({} drops){}", tc, rounds, drops.size(), o.trap ? std::format(", trap {}", o.trap) : "");
            if (const int trap = force >= 0 ? force : o.trap) spring_trap(trap, o.x, o.y, alvl(level->id), ms);
            return;
        }
        const int row = force >= 0 ? force : o.shrine;
        if (std::size_t(row) >= scene->shrines.size()) return;
        const auto& s = scene->shrines[std::size_t(row)];
        auto& v = cc.stats.v;
        d2d::rules::shrine_recharge(s, v[kLife], v[kMaxLife], v[kMana], v[kMaxMana]);
        if (auto b = d2d::rules::shrine_boost(s, fight.pf.ar); !b.empty())
            fight.boost = { row, std::move(b), ms + std::uint32_t(s.duration) * 40u };
        if (s.code == 18)                                  // gem: one up, or a chipped gem at the player's feet
            if (const auto code = d2d::rules::gem_shrine(scene->rules, cc.items, rng); !code.empty())
                loot.put({ .code = code }, player.x, player.y, 1, ms);
        if (s.code == 20 && fight.mon_level == level) {   // warping (FUN_00583050): the nearest plain monster turns boss
            // ponytail: FUN_00582750's filter read as alive, not a boss, not
            // an NPC; FUN_0065a800's search range isn't traced.
            int best = -1;
            float bd = 1e9f;
            for (std::size_t k = 0; k < fight.monsters.size(); ++k) {
                const auto& m = fight.monsters[k];
                const float d = std::hypot(m.u.x - player.x, m.u.y - player.y);
                if (m.alive() && m.boss == d2d::rules::Boss::none && d < bd) { bd = d; best = int(k); }
            }
            if (best >= 0) {                               // FUN_005a4940: FUN_005a0760 (champions allowed), FUN_005a2120
                auto& m = fight.monsters[std::size_t(best)];
                const auto b = d2d::rules::roll_boss(scene->umods, scene->monsters.types[std::size_t(m.type)], diff, true, rng);
                make_boss(*scene, m, b.kind, b.mods, -1, b.name_seed, diff, rng);
                d2d::log::info("warping shrine: {} is now a {}", m.npc.name, b.kind == d2d::rules::Boss::champion ? "champion" : "unique");
            }
        }
        d2d::log::info("shrine {} (code {}){}", row, s.code, s.code >= 16 && s.code != 18 && s.code != 20 ? ", not built" : "");
    }

    // A chest's trap (the table at 0x732cec, docs/research/re/objects.md
    // "Trap monsters"). The trap monster acts once and is gone, so its
    // shot goes straight from the chest at the player: missile level 1 / 4
    // / 8 by difficulty; a row with a Skill (chainlightning) carries that
    // skill's damage, the others their own columns. 5 / 7 leave two fires
    // (no damage traced); 8 raises the level's undead.
    // ponytail: the AI's range check (aip1) is skipped — the player opening
    // the chest is always close; chainlightning doesn't hop; trapfirebolt's
    // fireexplode isn't spawned.
    void spring_trap(int trap, float x, float y, int alvl, std::uint32_t ms) {
        const int diff = std::clamp(cc.header.active_difficulty(), 0, 2);
        if (trap == 5 || trap == 7) {                  // FUN_00582380: large at the chest, small a subtile east
            fires.push_back({ level, &scene->trap_fires[0], x, y });
            fires.push_back({ level, &scene->trap_fires[1], x + 0.2f, y });
            d2d::log::info("trap {}: fire", trap);
            return;
        }
        if (trap == 8) {                               // FUN_005822f0: 1 or 2 of the level's undead family
            const int fam = d2d::rules::trap_undead(level->region[std::size_t(diff)], 0);
            if (fam < 0 || fight.mon_level != level) { d2d::log::info("trap 8: no undead here"); return; }
            // FUN_0063ec70: the level's own variant of the family.
            // ponytail: from the region, else the family's first (FUN_006510c0's step not traced).
            int type = fam;
            const auto& types = scene->monsters.types;
            for (const int r : level->region[std::size_t(diff)]) if (types[std::size_t(r)].base == types[std::size_t(fam)].base) { type = r; break; }
            const int n = int(rng.next() & 1) + 1;
            for (int k = 0; k < n; ++k) {
                const auto [fx, fy] = level->nearest_free(x + float(k) * 0.4f, y + 0.4f);
                auto m = make_monster(*scene, type, fx, fy, rng, diff);
                m.aware = true;
                fight.add_monster(std::move(m));
            }
            d2d::log::info("trap 8: {} x{}", types[std::size_t(type)].id, n);
            return;
        }
        const auto* name = std::size_t(trap) < d2d::rules::kTrapMissile.size() ? d2d::rules::kTrapMissile[std::size_t(trap)] : "";
        const auto it = scene->missiles.find(name);
        if (!*name || it == scene->missiles.end()) { d2d::log::info("trap {}: not built", trap); return; }
        const auto& mi = it->second;
        const int lvl = d2d::rules::kTrapLevel[std::size_t(diff)];
        d2d::rules::MissileDamage md;
        if (const auto k = scene->skills.by_name.find(mi.skill); !mi.skill.empty() && k != scene->skills.by_name.end()) {
            d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, lvl, &rng };
            md = d2d::rules::missile_damage(scene->skills, *scene->skills.get(k->second), env, lvl);
        } else {
            md = d2d::rules::row_damage(mi.etype, mi.emin, mi.emax, mi.emin_lev, mi.emax_lev, mi.hitshift, mi.elen, mi.elen_lev, lvl);
        }
        d2d::rules::MonStats st;
        st.level = alvl;
        st.th = mi.to_hit ? alvl * 10 : 1 << 20;       // ToHit 0: always hits
        st.a2_min = mi.min; st.a2_max = std::max(mi.max, mi.min);
        // In 256ths a frame; poison's over its length.
        auto pts = [&](int e) { const std::int64_t v = e; return int(md.etype == 3 ? v * std::max(md.elen, 1) >> 8 : v >> 8); };
        if (md.etype >= 0) st.el[0] = { md.etype, 100, pts(md.elo), pts(md.ehi), md.elen, "A2" };
        auto shoot = [&](float dx, float dy, float vel, const std::shared_ptr<std::vector<int>>& struck) {
            const float speed = cells_per_sec(vel), d = std::max(std::hypot(dx, dy), 0.01f);
            Missile m{ &mi, x, y, dx / d * speed, dy / d * speed, direction32(dx, dy), ms, ms + std::uint32_t(std::max(mi.range, 1)) * 40, st };
            m.struck = struck;
            fight.missiles.push_back(std::move(m));
        };
        const auto ring = std::make_shared<std::vector<int>>();
        if (trap == 3) {                               // PrimePoisonNova: 8 at Param1 << 6, 8 between at Param2 << 6
            for (std::size_t k = 0; k < d2d::rules::kPoisonNova.size(); ++k) {
                const auto [ox, oy] = d2d::rules::kPoisonNova[k];
                shoot(float(ox), float(oy), float(k % 2 ? mi.param2 : mi.param1) / 4, ring);   // << 6 of a Vel's << 8
            }
        } else if (trap == 4) {                        // Trap Nova (do 22): the nova's 64
            for (int k = 0; k < 64; ++k) {
                const float a = float(k) * 6.2831853f / 64;
                shoot(std::cos(a), std::sin(a), float(mi.vel), ring);
            }
        } else {
            shoot(player.x - x, player.y - y, float(mi.vel), std::make_shared<std::vector<int>>());
        }
        d2d::log::info("trap {}: {} at level {}", trap, name, lvl);
    }

    // A fresh game for the character: the Blood Moor's monsters at its
    // difficulty, no loot about.
    void new_game() {
        fight.new_game(cc.header.active_difficulty());
        skillbar.new_game();
        loot.ground.clear();
        loot.kept.clear();
        loot.ground_level = level;
        cues.due.clear();
        operated.clear();
        fires.clear();
        pick_item = -1;
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
            fight.merc_joins();
        }
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
            if (k == SDLK_ESCAPE && fight.dead()) { respawn(ms); continue; }
            if (k >= SDLK_1 && k <= SDLK_4) fight.drink(int(k - SDLK_1), ms);
            if (k == SDLK_ESCAPE && skillbar.picking) { skillbar.picking = 0; continue; }   // the picker first
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
        if (have_world) walk(mouse, over_ui, ms, float(ms - last_ms) / 1000.f);
        if (have_world) cross_level(ms);
        if (!fight.dead()) fight.apply_regen(ms, last_ms);
        cues.play(audio, player.x, player.y, rng, ms);
        draw(fb, mouse, ms);
    }

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
        take_warp = -1;
        std::tie(player.x, player.y) = level->start.first >= 0 ? level->start : std::pair{ player.x, player.y };
        std::tie(player.x, player.y) = level->nearest_free(player.x, player.y);
        target_x = player.x; target_y = player.y;
        fight.revive(ms);
        d2d::log::info("respawned in the Rogue Encampment");
    }

    // The monster / ground item under the cursor (hovered_npc -10 - i / -1000 - i), or -1.
    [[nodiscard]] int hovered_monster() const {
        return hovered_npc <= -10 && hovered_npc > -1000 && std::size_t(-10 - hovered_npc) < fight.monsters.size() ? -10 - hovered_npc : -1;
    }
    [[nodiscard]] int hovered_ground() const {
        return hovered_npc <= -1000 && std::size_t(-1000 - hovered_npc) < loot.ground.size() ? -1000 - hovered_npc : -1;
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
        for (const auto& n : level->nearby) known = known || n.level->inside(target_x - float(n.dx), target_y - float(n.dy));
        if (!known)
            for (const auto& p : scene->act1_layout)
                if (wx >= float(p.x) && wy >= float(p.y) && wx < float(p.x + p.w) && wy < float(p.y + p.h)
                    && std::ranges::find(not_there, p.level) == not_there.end()) {
                    not_there.push_back(p.level);
                    d2d::log::info("not implemented: level {} (the player headed there from level {})", p.level, level->id);
                }
        if (level->inside(player.x, player.y)) return;
        for (const auto& n : level->nearby) {
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
            fight.pets_cross(level, n.level, dx, dy);
            if (n.level->layer != level->layer) {
                other_automaps[level->layer] = std::move(automap);
                automap = std::move(other_automaps[n.level->layer]);
                automap.open = other_automaps[level->layer].open;
            }
            level = n.level;
            fight.enter(level);
            loot.enter(level);
            npc_states = npc_start(*level);
            hovered_npc = interact_npc = -1;
            npc_menu = {}; store = {}; speech = {}; waypoint = {};
            level_ms = ms;                                  // its song comes in 3 s later
            d2d::log::info("level: {} at ({:.1f}, {:.1f})", level_name(*level), player.x, player.y);
            return;
        }
    }
    [[nodiscard]] static const char* level_name(const Level& l) {
        return l.id == 1 ? "Rogue Encampment" : l.id == 2 ? "Blood Moor" : l.id == 8 ? "Den of Evil" : "?";
    }

    // Taking a warp (a cave mouth): a click by one walks there; close to
    // it, the player goes to the level it leads to and stands at that
    // level's warp back, at its ExitWalk. Everything with the player
    // (merc, pets) comes along; the automap and monsters are the new level's.
    // ponytail: "close" is 2 cells of the warp's cell, not LvlWarp's
    // Select box; arriving puts the player on the first warp back.
    void use_warp(std::uint32_t ms) {
        if (take_warp < 0 || std::size_t(take_warp) >= level->warps.size()) return;
        const auto w = level->warps[std::size_t(take_warp)];
        if (std::hypot(w.x + 0.5f - player.x, w.y + 0.5f - player.y) > 2.f) {
            if (!player.walking) take_warp = -1;                                // stopped short
            return;
        }
        take_warp = -1;
        const Level* to = w.to == 1 ? &scene->town : w.to == 2 ? &scene->moor : w.to == 8 ? &scene->den : nullptr;
        if (!to || to->ds1.width() == 0) {
            d2d::log::info("not implemented: level {} (a warp from level {})", w.to, level->id);
            return;
        }
        const auto back = std::ranges::find(to->warps, level->id, &Level::Warp::to);
        const float ax = back == to->warps.end() ? float(to->ds1.width()) / 2 : back->x + back->exit_x;
        const float ay = back == to->warps.end() ? float(to->ds1.height()) / 2 : back->y + back->exit_y;
        const auto [px, py] = to->nearest_free(ax, ay);
        const float dx = player.x - px, dy = player.y - py;
        fight.pets_cross(level, to, dx, dy);
        if (to->layer != level->layer) {
            other_automaps[level->layer] = std::move(automap);
            automap = std::move(other_automaps[to->layer]);
            automap.open = other_automaps[level->layer].open;
        }
        const Level* from = level;
        level = to;
        player.x = px; player.y = py;
        player.walking = false; player.path.clear();
        target_x = px; target_y = py;
        if (merc) {
            merc->path.clear();
            std::tie(merc->x, merc->y) = level->nearest_free(px + 1, py + 1);
        }
        fight.enter(level);
        loot.enter(level);
        npc_states = npc_start(*level);
        hovered_npc = interact_npc = pick_item = -1;
        npc_menu = {}; store = {}; speech = {}; waypoint = {};
        level_ms = ms;
        d2d::log::info("level: {} at ({:.1f}, {:.1f}), through a warp from {}", level_name(*level), px, py, level_name(*from));
    }

    // The client's side of a click: what it asks the server for
    // (protocol.hpp). A held left button re-aims the walk; a press picks
    // what's under the cursor: a monster to attack, an item, an object or
    // NPC, else the ground. The right button uses the right skill there.
    [[nodiscard]] std::vector<Command> input(const Mouse& mouse, bool over_ui) const {
        std::vector<Command> out;
        if (fight.pmode >= 0 || over_ui) return out;
        // Screen -> world: invert the iso projection around the player,
        // who sits at (kW/2, kH/2 + kIsoH/2).
        const float u = float(mouse.x - int(kW) / 2) / (kIsoW / 2);
        const float v = float(mouse.y - int(kH) / 2 - kIsoH / 2) / (kIsoH / 2);
        const float wx = player.x + (u + v) / 2, wy = player.y + (v - u) / 2;
        const int hm = hovered_monster();
        const bool live = hm >= 0 && fight.monsters[std::size_t(hm)].alive();
        if (mouse.press_this_frame) {
            if (live) out.push_back(cmd::UseSkill{ skillbar.left, wx, wy, fight.monsters[std::size_t(hm)].id });
            else if (hovered_ground() >= 0) out.push_back(cmd::Pickup{ hovered_ground() });
            else if (hovered_npc >= 0) out.push_back(cmd::Interact{ hovered_npc });
            else out.push_back(cmd::Move{ wx, wy, true });
        } else if (mouse.down) {
            out.push_back(cmd::Move{ wx, wy, false });
        }
        if (mouse.rpress_this_frame) out.push_back(cmd::UseSkill{ skillbar.right, wx, wy, live ? fight.monsters[std::size_t(hm)].id : -1 });
        return out;
    }

    // The server's side: a command from the player, checked and applied
    // (a busy or dead player's were dropped by input()).
    void apply(const Command& c, std::uint32_t ms) {
        const bool in_moor = level != &scene->town;
        auto walk_to = [&](float x, float y, bool fresh) {
            target_x = x; target_y = y;
            player.walking = true;
            interact_npc = -1;
            if (!fresh) return;
            fight.attack_mon = pick_item = -1;
            take_warp = -1;                                                  // a click on a warp: go through it
            for (std::size_t i = 0; i < level->warps.size(); ++i)
                if (std::hypot(level->warps[i].x + 0.5f - x, level->warps[i].y + 0.5f - y) < 2.f) take_warp = int(i);
        };
        if (const auto* m = std::get_if<cmd::Move>(&c)) { walk_to(m->x, m->y, m->fresh); return; }
        if (const auto* p = std::get_if<cmd::Pickup>(&c)) {                  // walk to it, pick it up
            if (std::size_t(p->item) >= loot.ground.size()) return;
            walk_to(loot.ground[std::size_t(p->item)].x, loot.ground[std::size_t(p->item)].y, true);
            pick_item = p->item;
            return;
        }
        if (const auto* in = std::get_if<cmd::Interact>(&c)) {               // walk to it; operate or talk on arrival
            if (std::size_t(in->npc) >= level->npcs.size()) return;
            const auto& o = level->npcs[std::size_t(in->npc)];
            const auto& st = npc_states[std::size_t(in->npc)];
            const float ox = o.path.empty() ? o.x : st.x, oy = o.path.empty() ? o.y : st.y;
            walk_to(ox, oy, true);
            const bool menu = std::ranges::any_of(kNpcMenus, [&](const NpcMenu& e) { return e.hc_idx == o.hc_idx; });
            const bool usable = (o.operate_fn == 2 || o.operate_fn == 4) && !operated.contains({ level, in->npc });
            if (o.operate_fn == 32 || o.operate_fn == 23 || usable || (o.root == "monsters" && menu)) interact_npc = in->npc;
            return;
        }
        const auto& k = std::get<cmd::UseSkill>(c);
        const auto* s = scene->skills.get(k.skill);
        const int mi = fight.monster_index(k.unit);
        const bool live = mi >= 0 && fight.monsters[std::size_t(mi)].alive();
        if (s && self_cast(*s)) {                                            // Holy Shield: where the player stands
            if (fight.cast(k.skill, ms)) player.walking = false;
        } else if (s && s->srvdofunc == 76 && mi < 0) {                      // Whirlwind to that point (FUN_005d8f50)
            fight.move_x = k.x; fight.move_y = k.y;
            fight.attack_mon = -1;
            fight.attack_skill = k.skill;
            interact_npc = pick_item = -1;
            player.walking = false;
            player.dir = direction16(fight.move_x - player.x, fight.move_y - player.y);
            fight.start_swing(ms);
        } else if (s && !live && (fight.missile_skill(*s) || fight.spot_skill(*s)) && (in_moor || s->in_town)) {
            interact_npc = pick_item = -1;                                   // a missile, Teleport, Corpse Explosion at the spot
            if (fight.cast_missile(k.skill, k.x, k.y, ms)) player.walking = false;
        } else if (s && !live && fight.summon_skill(*s) && in_moor) {        // Raise Skeleton: the corpse there
            interact_npc = pick_item = -1;
            if (fight.cast_summon(k.skill, k.x, k.y, ms)) player.walking = false;
        } else if (live) {                                                   // walk up to it, then attack
            target_x = k.x; target_y = k.y;
            player.walking = true;
            interact_npc = pick_item = -1;
            fight.attack_mon = mi;
            fight.attack_skill = k.skill;
        }
    }

    // Walking: a click on the ground (not over the UI) sets the target and
    // an object to operate on arrival; the player follows a walk_path; NPCs
    // patrol; the merc follows.
    void walk(const Mouse& mouse, bool over_ui, std::uint32_t ms, float dt) {
        // An aura on the right button is on (a Paladin's; D2 runs the right
        // skill's aura).
        if (const auto* ra = scene->skills.get(skillbar.right)) fight.aura = ra->aura ? skillbar.right : 0;
        // The skill shrine's +all skills while its boost lasts.
        skillbar.extra.clear();
        if (ms < fight.boost.until)
            for (const auto& [id, v] : fight.boost.stats) if (id == 127) skillbar.extra.push_back({ .stat = 127, .value = v });
        fight.update_fighters(ms);
        // Used shrines and chests: OP while it plays, then ON; a shrine back
        // to NU after its reset time (Shrines.txt, minutes; 0 never).
        for (auto it = operated.begin(); it != operated.end();) {
            const auto& [key, at] = *it;
            const auto i = std::size_t(key.second);
            const auto& o = key.first->npcs[i];
            const int reset = o.operate_fn == 2 && std::size_t(o.shrine) < scene->shrines.size() ? scene->shrines[std::size_t(o.shrine)].reset : 0;
            const bool back = reset > 0 && ms - at >= std::uint32_t(reset) * 60000u;
            if (key.first == level && i < npc_states.size())
                npc_states[i].mode = back ? std::string_view{} : ms - at < std::uint32_t(o.op_frames) * 40u ? "OP" : "ON";
            it = back ? operated.erase(it) : std::next(it);
        }
        Crowd crowd;                           // who's in whose way this frame
        crowd.units.push_back(&player);
        if (merc) crowd.units.push_back(&*merc);
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            if (!level->npcs[i].path.empty() && !npc_states[i].hidden) crowd.units.push_back(&npc_states[i]);
        const bool in_moor = level != &scene->town;     // outside: this level's monsters are about
        if (in_moor) fight.crowd(crowd);       // the monsters around the player
        // Dead: the death plays out, then a click (or Esc) respawns in camp;
        // a swing or a flinch holds the player in place until it ends.
        if (fight.player_modes(mouse, ms, dt)) respawn(ms);
        if (fight.dead()) return;
        const bool busy = fight.pmode >= 0;
        for (const auto& c : input(mouse, over_ui)) apply(c, ms);
        // Close enough to the stash: open it with the inventory.
        // ponytail: 2 cells, not D2's per-object operate range.
        if (interact_npc >= 0) {
            const auto& o = level->npcs[std::size_t(interact_npc)];
            const auto& st = npc_states[std::size_t(interact_npc)];
            const float ox = o.path.empty() ? o.x : st.x, oy = o.path.empty() ? o.y : st.y;
            if (std::hypot(ox - player.x, oy - player.y) < 2.f) {
                if (o.operate_fn == 2 || o.operate_fn == 4) {
                    operate(interact_npc, ms);
                } else if (o.operate_fn == 32) {
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
            const auto& g = loot.ground[std::size_t(pick_item)];
            if (std::hypot(g.x - player.x, g.y - player.y) <= 1.f) {
                loot.take(std::size_t(pick_item));
                pick_item = -1; player.walking = false; player.path.clear();
            } else {
                target_x = g.x; target_y = g.y; player.walking = true;
            }
        }
        if (const auto to = fight.engage(ms)) { std::tie(target_x, target_y) = *to; player.walking = true; }
        if (player.walking && fight.pmode < 0) {
            // A route to the target, re-planned when the target
            // moves off its end (dragging, a walking NPC).
            if (player.path.empty() || std::hypot(player.goal_x - target_x, player.goal_y - target_y) > 0.3f) {
                player.path = walk_path(*level, player.x, player.y, target_x, target_y, crowd, &player);
                player.goal_x = target_x; player.goal_y = target_y;
            }
            const auto sc = std::size_t(kUiToSaveClass[std::max(cc.selected, 0)]);
            // Faster run/walk: its effective % (150 x v / (150 + v)) on velocity.
            const float vel = float(running ? scene->run_velocity[sc] : scene->walk_velocity[sc])
                            * float(100 + d2d::rules::effective_speed(fight.pf.frw, 150)) / 100.f;
            player.walking = follow_path(*level, player, cells_per_sec(vel) * dt, crowd);
            if (!player.walking) player.path.clear();
        }
        npc_patrol(*level, npc_states, { npc_menu.npc, speech.npc, store.npc }, ms, dt, crowd);
        fight.world(in_moor, ms, dt, crowd);
        use_warp(ms);
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
        if (level != &scene->town) loot.units(player.x, player.y, extra);
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
                      player.x, player.y, mode,
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
