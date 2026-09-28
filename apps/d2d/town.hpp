// The town: the in-game character's world state (position, walking,
// panels, NPC menus, store, merc, ...) and one frame of play — input,
// movement, NPCs, then the render.
#pragma once

#include "replication.hpp"
#include "skillbar.hpp"

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
    // The label under it (uniques and minions, d2d::rules::kUModLabel):
    // Demon / Undead, then its mods; a minion's "Minion". Champions have
    // none: their name says it.
    std::string label;
    using d2d::rules::Boss;
    const auto& t = s.monsters.types[std::size_t(m.type)];
    const std::uint16_t lead = t.demon ? d2d::rules::kDemonLabel : t.undead ? d2d::rules::kUndeadLabel : 0;
    if (lead) label = string_id(s, lead);
    if (m.boss == Boss::minion)
        label = (lead ? label + string_id(s, d2d::rules::kMinionSpace) : "") + string_id(s, d2d::rules::kMinionLabel);
    else if (m.boss == Boss::unique || m.boss == Boss::superunique)
        for (const int id : m.mods) {
            if (id < 0 || std::size_t(id) >= d2d::rules::kUModLabel.size() || !d2d::rules::kUModLabel[std::size_t(id)]) continue;
            const auto next = label + (label.empty() ? "" : " ") + string_id(s, d2d::rules::kUModLabel[std::size_t(id)]);
            if (s.font.measure(next) > 480) break;
            label = next;
        }
    else label.clear();
    if (!label.empty())
        s.font.draw(fb, kW, kH, pal, int(kW) / 2 - s.font.measure(label) / 2, y0 + h + 2, label);
}


// The client's drawing of what the World told it (View): the ground
// items, fires, the merc, pets, missiles and monsters near (cx, cy) as
// units the world draws by depth (npc -2 the merc, -3 pets, -10 - i
// monster i of the View, -1000 - i ground item i).
constexpr std::uint32_t kPortalOpenMs = 15 * 40 * 256 / 200;
void view_units(const Scene& s, const View& v, float cx, float cy, const std::string* merc_label, std::vector<Unit>& out,
                std::span<const View::Shot> fx = {}, std::uint32_t now_ms = 0) {
    auto in_view = [&](float x, float y) { return std::abs(x - cx) < 14 && std::abs(y - cy) < 14; };
    for (std::size_t i = 0; i < v.ground.size(); ++i) {
        const auto& g = v.ground[i];
        if (!in_view(g.x, g.y) || v.level == &s.town) continue;
        Unit u{ g.x, g.y, nullptr, 0, &g.label, g.ms, -1000 - int(i) };
        u.sprite = s.flippy(g.item.code);
        u.rgb = g.rgb;
        out.push_back(u);
    }
    for (const auto& f : v.fires) out.push_back({ f.x, f.y, &s.npc_anim(*f.npc, f.npc->mode), 0, nullptr, 0, -2 });
    // Town portals: opening (OP, FrameCnt1 15 at FrameDelta 200/256 a tick:
    // 768 ms), then ON; named by where they lead (-2000 - which).
    for (const auto& p : v.portals) {
        const Level* to = s.level(p.to);
        const bool opening = now_ms - p.born < kPortalOpenMs;
        out.push_back({ p.x, p.y, &s.npc_anim(s.town_portal, opening ? "OP" : "ON"), 0, to ? &to->name : nullptr,
                        opening ? p.born : p.born + kPortalOpenMs, -2000 - p.which });
        out.back().shadow = false;
    }
    if (v.merc)
        out.push_back({ v.merc->u.x, v.merc->u.y, &s.npc_anim(*v.merc->npc, v.merc->mode), v.merc->u.dir,
                        v.merc->mode == "DT" ? nullptr : merc_label, v.merc->u.mode_ms, -2 });
    for (const auto& p : v.pets) out.push_back({ p.u.x, p.u.y, &s.npc_anim(p.npc, p.mode), p.u.dir, nullptr, p.u.mode_ms, -3 });
    auto shot = [&](const View::Shot& m) {
        Unit u{ m.x, m.y, nullptr, m.dir, nullptr, m.born, -1 };
        u.missile = m.info;
        out.push_back(u);
    };
    for (const auto& m : v.missiles) shot(m);
    for (const auto& m : fx) shot(m);                 // the client's own (the Den's light beams)
    for (std::size_t i = 0; i < v.monsters.size(); ++i) {
        const auto& m = v.monsters[i];
        if (m.corpse_used || !in_view(m.u.x, m.u.y)) continue;
        out.push_back({ m.u.x, m.u.y, &s.npc_anim(m.npc, m.mode), m.u.dir, m.alive() ? &m.npc.name : nullptr, m.u.mode_ms, -10 - int(i) });
    }
}
// Over the world: the hovered (else attacked) monster's life bar, the
// death message.
void view_overlays(std::vector<std::uint8_t>& fb, const Scene& s, const View& v, int hovered) {
    if (hovered >= 0) draw_monster_bar(fb, s, v.monsters[std::size_t(hovered)]);
    else if (const int a = v.monster(v.attack); a >= 0) draw_monster_bar(fb, s, v.monsters[std::size_t(a)]);
    if (v.pmode == kModeDD) {                    // ponytail: D2's death screen text isn't traced
        const std::string msg = "You have died.  Click or press Esc to continue.";
        const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
        s.font.draw_tinted(fb, kW, kH, pal, int(kW) / 2 - s.font.measure(msg) / 2, int(kH) / 2 - 60, msg, 220, 60, 60);
    }
}
// An SQ skill's frame now: the mode and when it started, as Fight::seq_view.
std::pair<int, std::uint32_t> view_seq(const Scene& s, int cls, const View& v, std::uint32_t ms) {
    const auto i = std::size_t((ms - v.player.mode_ms) / std::max<std::uint32_t>(v.seq_frame_ms, 1));
    const auto& f = v.seq[v.seq_loop ? i % v.seq.size() : std::min(i, v.seq.size() - 1)];
    const auto mpf = s.composite(cls, f.mode, v.gfx).ms_per_frame();
    return { f.mode, ms - mpf * f.frame - mpf / 2 };
}

// The client (docs/design/multiplayer.md): input, panels, camera,
// drawing and sound, over a World (server.hpp) it sends commands to. The
// references below are the World's, for the code that reads them.
// The frame's light (FUN_00475800): the grid round the player at the
// level's own light or the day's, then each light stamped. Positions in
// eighths of a subtile (a cell is 40).
// ponytail: lights don't ease to a new radius (8 eighths a frame); light
// quality is taken as high (2: shadows on).
Lighting frame_light(const Scene& s, const View& v, float cam_x, float cam_y, std::span<const View::Shot> fx = {}, int ambient = -1) {
    Lighting l;
    if (!v.level || s.act1_lit[31].entries().empty()) return l;
    l.pal = &s.act1_lit;
    // The grid covers the view's corners (half the width in cells over 80
    // plus half the height over 40, halved, in subtiles) and the widest light
    // (18) past them, not game.exe's 48 (bugs.md #11).
    const int half = (int(kW) / 2 / (kIsoW / 2) + (int(kH) / 2 + kIsoH) / (kIsoH / 2)) * 5 / 2 + 18 + 1;
    if (ambient < 0) ambient = v.level->light >= 0 ? v.level->light : v.day.intensity();
    l.grid.reset(int(cam_x * 5), int(cam_y * 5), ambient, half * 2);
    for (int j = 0; j < l.grid.n; ++j)                            // what walls light (FUN_004756d0)
        for (int i = 0; i < l.grid.n; ++i)
            l.grid.blocked[std::size_t(j * l.grid.n + i)] =
                v.level->blocked((float(l.grid.x0 + i) + 0.5f) / 5, (float(l.grid.y0 + j) + 0.5f) / 5, 0x22);
    // Type 0 lights (the player's, objects') are shadowed by walls; type 1
    // (monsters', missiles') aren't (FUN_004755a0).
    auto stamp = [&](float x, float y, int radius, bool shadowed) {
        if (radius <= 0) return;
        if (shadowed) l.grid.stamp_shadowed(int(x * 40), int(y * 40), std::min(radius, 18) * 8, 255);
        else l.grid.stamp(int(x * 40), int(y * 40), std::min(radius, 18) * 8, 255);
    };
    stamp(cam_x, cam_y, std::max(0, 13 + v.light_bonus), true);   // FUN_00460930: 13 + the bonus, capped at 18
    static constexpr std::array<std::string_view, 8> kModes{ "NU", "OP", "ON", "S1", "S2", "S3", "S4", "S5" };
    auto lit = [&](const Npc& n, std::string_view mode) {
        const auto m = std::ranges::find(kModes, mode.empty() ? std::string_view(n.mode) : mode);
        return n.root == "objects" && m != kModes.end() ? int(n.lit[std::size_t(m - kModes.begin())]) : 0;
    };
    for (std::size_t i = 0; i < v.level->npcs.size(); ++i) {
        const auto& n = v.level->npcs[i];
        const auto* st = i < v.npc_states.size() ? &v.npc_states[i] : nullptr;
        if (st && st->hidden) continue;
        stamp(st ? st->x : n.x, st ? st->y : n.y, lit(n, st ? st->mode : std::string_view{}), true);
    }
    for (const auto& nb : v.level->nearby)                  // the torches over the level's edge
        for (const auto& n : nb.level->npcs) stamp(n.x + float(nb.dx), n.y + float(nb.dy), lit(n, {}), true);
    for (const auto& m : v.monsters) if (m.alive()) stamp(m.u.x, m.u.y, m.npc.light, false);
    for (const auto& p : v.portals) stamp(p.x, p.y, int(s.town_portal.lit[2]), true);   // Lit2 19 (ON); ponytail: Lit1 18 while opening
    for (const auto& m : v.missiles) if (m.info) stamp(m.x, m.y, m.info->light, false);
    for (const auto& m : fx) if (m.info) stamp(m.x, m.y, m.info->light, false);
    return l;
}

struct Town {
    const Scene* scene = nullptr;
    CharCreateUI& cc;                      // the in-game character (save, items, stats)
    World world;                           // the game server's side (in-process: single player)
    const Level* level = nullptr;          // where the character is (the View's): the town, the Blood Moor, the Den of Evil
    d2d::rules::Rng rng{ 0x7f4a7c15u };    // the client's own rolls (which gossip, sound variations)
    Cues cues{ scene };                    // the world's sounds the View brought, due to play
    // The World's state by name, for the devctl verbs (admin and tests, on
    // the server's side); the client itself reads only `view`.
    UnitState& player = world.player;
    std::vector<UnitState>& npc_states = world.npc_states;
    std::optional<UnitState>& merc = world.merc;
    Loot& loot = world.loot;
    Fight& fight = world.fight;
    float& target_x = world.target_x;
    float& target_y = world.target_y;
    LocalTransport net;                    // the commands to the World, as their wire form (single player)
    View view;                             // what the World told the client after its last tick
    ViewEncoder view_enc;                  // the host's memory of what this client was sent
    int talking_sent = -1;                 // the NPC last reported as talked to (cmd::Chat)
    std::uint32_t world_ms = 0;            // the World's clock: when it last ticked
    float prev_x = 0, prev_y = 0;          // the player a tick before: the camera slides between the two
    float cam_x = 0, cam_y = 0;            // where the camera is this frame
    std::unordered_map<int, Automap> other_automaps;   // other Layers' maps, while elsewhere
    std::uint32_t level_ms = 0;            // when the player entered `level`
    bool have_world = false;
    bool  stash_open = false;
    std::optional<d2d::d2s::Item> held;   // the item on the cursor (the View's)
    int   stat_pressed = -1;               // char panel stat button held down
    bool  tree_open = false;               // skill tree ('T')
    QuestLog quest_log;                    // the quest log ('Q'), where the character panel goes
    int   tree_tab = 1;                    // 1..3, bottom tab first (0x724bec starts at 1)
    int   skill_pressed = -1;              // skill icon held down
    std::vector<d2d::rules::MercOffer> hire_offers;   // Kashya's list while it's open (the View's)
    std::string merc_label;
    bool  belt_open = false;               // belt popup (` key or a click on the belt)
    bool  cube_open = false;               // right-click the Horadric Cube item
    NpcMenuState npc_menu;                 // open NPC menu (npc < 0: none)
    Automap automap;                       // Tab
    Store store;                           // an open vendor store (npc < 0: none): the View's stock, the client's tab and buttons
    WaypointUI waypoint;                   // the waypoint panel
    Speech speech;                         // NPC talking (npc < 0: none)
    std::vector<d2d::rules::QuestMsg> npc_quest;   // what the NPC being talked to has on quests
    // The level's random ambient sound (SoundEnviron Day / Night Event,
    // FUN_004e42e0): its sound, the gap to the next and the last one's
    // time, in sound ticks (25 Hz).
    int amb_event = 0;
    std::uint32_t amb_next = 0, amb_last = 0;
    d2d::rules::Rng sound_rng{ 0x5eed5u };
    std::vector<int> gossip_pick;          // per world NPC: chosen gossip topic, -1 = not yet
    SkillBar skillbar{ scene, cc };        // the skill buttons, picker and hotkeys (skillbar.hpp)
    int   hovered_npc = -1;                // Level::npcs index under the cursor (last frame); <= -10: monster -10 - i
    std::uint32_t now_ms = 0;              // this frame's ms (devctl)   // shrines / chests used: when
    bool  player_walked = false;           // `walking` as of the last frame
    std::uint32_t walk_ms = 0;             // when the player last started or stopped walking (the client's clock)
    bool  player_ran = false;
    bool  inv_open = false;   // 'I' — inventory panel
    bool  char_open = false;  // 'C' — character panel

    Town(const Scene* s, CharCreateUI& c, int start_x = -1, int start_y = -1)
        : scene(s), cc(c), world(s, start_x, start_y) {
        have_world = world.level && !world.level->dt1s.empty();
        view = world.view();
        level = view.level;
    }

    // What the World tells the client: its View, over as bytes too. The
    // character in it becomes the client's (the panels draw it); the store
    // keeps the client's tab and buttons.
    void publish() {
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
        cc.header = view.header; cc.stats = view.stats; cc.items = view.items;
        cc.expansion = view.header.expansion();
        cc.panel = panel_stats(*scene, cc.header, cc.items, cc.stats);
        held = view.held;
        if (view.store) {
            const auto keep = store;
            store = *view.store;
            if (keep.npc == store.npc) { store.tab = keep.tab; store.mode = keep.mode; store.pressed = keep.pressed; }
        } else {
            store = {};
        }
        hire_offers = view.hire_offers;
        if (const auto m = scene->mercs.find(cc.header.merc_type); view.merc && m != scene->mercs.end())
            merc_label = merc_name(*scene, m->second, cc.header.merc_name);
    }

    // Into the game with the character the client has (a save loaded, or
    // made): the World takes it.
    void enter() {
        automap.cells.clear();                    // a new game: nothing seen yet
        automap.revealed.clear();
        other_automaps.clear();
        quest_log = {};                           // done animations play again in a new game
        world.enter(cc);
        skillbar.new_game();
        publish();
    }

    // Saving the character: the item on the cursor goes back first, then
    // the World writes it.
    std::string save() { return world.save(); }

    // devctl: operate object i now, as the server would on arrival.
    void operate(int i, std::uint32_t ms, int force = -1) { world.operate(i, ms, force); }

    // A fresh game for the character: the Blood Moor's monsters at its
    // difficulty, no loot about.
    void new_game() {
        world.new_game();
        skillbar.new_game();
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
            if (k == SDLK_R) net.send(cmd::Run{ !view.running });   // D2's run/walk toggle
            skillbar.key(k, mouse.x, mouse.y);                // F1-F8
            if (k == SDLK_GRAVE) belt_open = !belt_open;      // D2's "Show Belt" key
            if (k == SDLK_TAB) automap.open = !automap.open;  // D2's automap toggle
            if (k == SDLK_C) { char_open = !char_open; if (char_open) stash_open = cube_open = quest_log.open = false; }
            if (k == SDLK_Q) { quest_log.open = !quest_log.open; if (quest_log.open) char_open = stash_open = cube_open = false; }
            if (k == SDLK_ESCAPE && view.dead) { net.send(cmd::Resurrect{}); continue; }
            if (k >= SDLK_1 && k <= SDLK_4) net.send(cmd::UseBelt{ int(k - SDLK_1) });
            if (k == SDLK_ESCAPE && skillbar.picking) { skillbar.picking = 0; continue; }   // the picker first
            if (k == SDLK_ESCAPE) {
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
        const auto& lay = scene->inv_layout[std::size_t(kUiToSaveClass[std::max(cc.selected, 0)])];
        const bool over_panel =
            (tree_open && mouse.x >= 400 && mouse.x < 720 && mouse.y >= 60 && mouse.y < 540) ||
            (inv_open && mouse.x >= lay.panel_x && mouse.x < lay.panel_x + 320
                      && mouse.y >= lay.panel_y && mouse.y < lay.panel_y + 432) ||
            ((char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open) && mouse.x >= kCharPanelX && mouse.x < kCharPanelX + 320
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
                net.send(cmd::Sell{ held->id });
                item_click = true;
            } else {
                const auto cl = item_cursor_command(*scene, cc.items, held, int(kUiToSaveClass[std::max(cc.selected, 0)]),
                                                    { inv_open, stash_open, cube_open, belt_open, cc.expansion }, mouse.x, mouse.y);
                if (cl.cmd) net.send(*cl.cmd);
                item_click = cl.consumed;
            }
        }
        if (mouse.press_this_frame && over_belt && mouse.y >= b0[2] && !item_click) belt_open = !belt_open;
        // A right-click on a carried item uses it: a potion in the inventory,
        // stash or belt is drunk.
        if (mouse.rpress_this_frame && !held && store.npc < 0 && npc_menu.npc < 0 && speech.npc < 0) {
            const auto cl = item_cursor_command(*scene, cc.items, held, int(kUiToSaveClass[std::max(cc.selected, 0)]),
                                                { inv_open, stash_open, cube_open, belt_open, cc.expansion }, mouse.x, mouse.y);
            if (const auto* p = cl.cmd ? std::get_if<cmd::ToCursor>(&*cl.cmd) : nullptr) net.send(cmd::UseItem{ p->item });
        }
        // Quest log: a tab picks the act, an icon the quest.
        // Its buttons: close; questlast plays the selected quest's message again.
        if (quest_log.open) {
            const int b = quest_button_at(mouse.x, mouse.y);
            if (mouse.press_this_frame) { quest_log.close_down = b == 1; quest_log.last_down = b == 2; }
            if (mouse.release_this_frame) {
                if (quest_log.close_down && b == 1) quest_log.open = false;
                if (quest_log.last_down && b == 2)
                    for (const auto& e : kQuestLog)
                        if (e.act == quest_log.act && e.slot == quest_log.slot)
                            if (const auto t = quest_text(cc.header.quests[std::size_t(std::clamp(cc.header.active_difficulty(), 0, 2))], e.quest,
                                                          { view.den_state, view.den_log, view.den_left }); t.speech)
                                replay_speech = t.speech;
                quest_log.close_down = quest_log.last_down = false;
            }
        }
        if (quest_log.open && mouse.press_this_frame) {
            if (const int a = quest_tab_at(mouse.x, mouse.y); a >= 0) { quest_log.act = a; quest_log.slot = -1; }
            if (const int k = quest_slot_at(*scene, mouse.x, mouse.y); k >= 0) quest_log.slot = k;
        }
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
                if (skill_pressed >= 0 && sk == skill_pressed) net.send(cmd::SkillPoint{ sk });
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
                    net.send(cmd::StatPoint{ kStatButtons[std::size_t(sb)].stat, n });
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
        const auto& me = view.player;
        automap_reveal(*scene, *level, automap, me.x, me.y);
        for (const auto& n : level->nearby)                   // what's in view across the edge
            if (n.level->layer == level->layer)
                automap_reveal(*scene, *n.level, automap, me.x - float(n.dx), me.y - float(n.dy));
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
        // ponytail: the ambience switches at once; game.exe fades it.
        if (audio.music.sound > 0 && level->song > 0 && audio.music.sound != level->song && ms - level_ms >= 75 * 40)
            audio.crossfade_music(*scene, level->song);
        if (audio.music.sound > 0 && audio.music.sound == level->song && audio.ambience.sound != amb) {
            audio.play(audio.ambience, *scene, amb);
            audio.ambience.sound = amb;           // tried: not again every frame
        }
        // The weather, a client frame at a time where it rains (FUN_00473f50;
        // leaving, the drops go and the cycle waits), and its sound: Sounds.txt
        // 64 scene_rain at the density's volume, easing 6 a tick
        // (FUN_004e42e0).
        if (ms - rain_ms > 1000) rain_ms = ms;
        for (; ms - rain_ms >= 40; rain_ms += 40) {
            den_tick(ms);
            const float dx = cam_x - rain_cam_x, dy = cam_y - rain_cam_y;
            rain_cam_x = cam_x; rain_cam_y = cam_y;
            if (!level->rain) { rain.drops.clear(); rain.splashes.clear(); }
            else rain.tick(rain.rng, int(kW), int(kH), int(std::lround((dx - dy) * (kIsoW / 2))), int(std::lround((dx + dy) * (kIsoH / 2))),
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
            const std::uint32_t tick = ms / 40;
            const int ev = day ? level->day_event : level->night_event, delay = level->event_delay;
            auto spread = [&](int n) { return sound_rng(2 * n + 1) - n; };
            if (ev != amb_event) {
                amb_event = ev;
                amb_next = std::uint32_t(std::max(1, delay + spread(delay / 3)));
                amb_last = tick - std::uint32_t(sound_rng(int(amb_next)));
            }
            if (ev > 0 && tick - amb_last >= amb_next) {
                const int x = (sound_rng(2) ? 1 : -1) * (450 + sound_rng(301));
                audio.play_sfx(*scene, ev, 1.f, sound_rng(16), float(x) / 750.f);
                amb_last = tick;
                amb_next = std::uint32_t(std::max(1, delay + spread(delay / 3)));
            }
        }
        if (questdone_sound) { questdone_sound = false; audio.play_sfx(*scene, 14, 1.f, 0); }
        if (replay_speech) {
            const auto v = std::ranges::find_if(kSpeechSound, [&](const auto& e) { return e.first == replay_speech; });
            if (v != kSpeechSound.end()) audio.play_voice(*scene, v->second);
            replay_speech = 0;
        }
        if (speech.npc >= 0 && speech.voice == 0) {
            speech.voice = -1;
            const auto v = std::ranges::find_if(kSpeechSound, [&](const auto& e) { return e.first == speech.string; });
            if (v != kSpeechSound.end()) { speech.voice = v->second; audio.play_voice(*scene, v->second); }
        }
        if (speech.npc >= 0 && (speech.done(ms) || mouse.press_this_frame)) {
            menu_click = mouse.press_this_frame;          // a click skips the speech
            speech = {};
            if (menu_after_speech >= 0 && std::size_t(menu_after_speech) < level->npcs.size()) open_menu(menu_after_speech);
            menu_after_speech = -1;
        } else if (npc_menu.npc >= 0 && mouse.press_this_frame) {
            const int li = npc_menu.line_at(mouse.x, mouse.y);
            const auto action = li >= 0 ? npc_menu.lines[std::size_t(li)].action : NpcMenuState::kClose;
            const int npc_menu_arg = li >= 0 ? npc_menu.lines[std::size_t(li)].arg : -1;
            const int who = npc_menu.npc;
            const auto& n = level->npcs[std::size_t(who)];
            const auto& st = view.npc_states[std::size_t(who)];
            const float dx = (n.path.empty() ? n.x : st.x) - view.player.x, dy = (n.path.empty() ? n.y : st.y) - view.player.y;
            const int sx = int(kW) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
            const int sy = int(kH) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2)));
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
                npc_menu = open_talk_menu(*scene, *level, who, sx, sy, npc_quest);
            } else if (action == NpcMenuState::kRespec) {
                npc_menu = open_respec_menu(*scene, who, sx, sy);
            } else if (action == NpcMenuState::kRespecOk) {
                net.send(cmd::Respec{ who });
            } else if (action == NpcMenuState::kQuest) {
                speech = start_speech(*scene, who, std::uint16_t(npc_menu_arg), ms);
                net.send(cmd::QuestMessage{ who, npc_menu_arg });
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
                if (on && mouse.release_this_frame && i == 3 && repairer) net.send(cmd::Repair{ -1 });
                if (on && mouse.release_this_frame && i == 3 && store_button_frames(store)[3] == 10) {
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
            const int si = store.npc >= 0 ? store_item_at(*scene, store, mouse.x, mouse.y) : -1;
            if (si >= 0 && (mouse.rpress_this_frame || (mouse.press_this_frame && store.mode == 1)))
            {
                net.send(cmd::Buy{ si });
            }
            // Sell: an inventory item; repair: that or a worn one.
            if (store.npc >= 0 && mouse.press_this_frame && (store.mode == 2 || store.mode == 3))
                for (std::size_t i = 0; i < cc.items.size(); ++i) {
                    const auto& it = cc.items[i];
                    const bool worn = it.location == 1 && it.slot >= 1 && it.slot <= 10;
                    if (!(it.location == 0 && it.panel == 1) && !(worn && store.mode == 3)) continue;
                    const auto r = worn ? lay.slots[std::size_t(it.slot)] : grid_rect(*scene, lay, it);
                    if (mouse.x >= r[0] && mouse.x < r[0] + r[2] && mouse.y >= r[1] && mouse.y < r[1] + r[3]) {
                        if (store.mode == 2) net.send(cmd::Sell{ it.id });
                        else net.send(cmd::Repair{ it.id });
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
        cues.play(audio, view.player.x, view.player.y, rng, ms);
        draw(fb, mouse, ms);
    }

    // The monster / ground item under the cursor (hovered_npc -10 - i / -1000 - i), or -1.
    [[nodiscard]] int hovered_monster() const {
        return hovered_npc <= -10 && hovered_npc > -1000 && std::size_t(-10 - hovered_npc) < view.monsters.size() ? -10 - hovered_npc : -1;
    }
    [[nodiscard]] int hovered_ground() const {
        return hovered_npc <= -1000 && std::size_t(-1000 - hovered_npc) < view.ground.size() ? -1000 - hovered_npc : -1;
    }

    // The client's side of a click: what it asks the server for
    // (protocol.hpp). A held left button re-aims the walk; a press picks
    // what's under the cursor: a monster to attack, an item, an object or
    // NPC, else the ground. The right button uses the right skill there.
    [[nodiscard]] std::vector<Command> input(const Mouse& mouse, bool over_ui) const {
        std::vector<Command> out;
        if (view.dead) {                                     // a click, once the death has played, respawns
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
        const bool live = hm >= 0 && view.monsters[std::size_t(hm)].alive();
        if (mouse.press_this_frame) {
            if (live) out.push_back(cmd::UseSkill{ skillbar.left, wx, wy, view.monsters[std::size_t(hm)].id, true });
            else if (hovered_ground() >= 0) out.push_back(cmd::Pickup{ view.ground[std::size_t(hovered_ground())].id });
            else if (hovered_npc >= 0 || (hovered_npc <= -2000 && hovered_npc > -2002)) out.push_back(cmd::Interact{ hovered_npc });
            else out.push_back(cmd::Move{ wx, wy, true });
        } else if (mouse.down) {                             // held: the attack goes on, else the walk re-aims
            if (const int am = view.monster(view.attack); am >= 0 && view.monsters[std::size_t(am)].alive())
                out.push_back(cmd::UseSkill{ view.attack_skill, wx, wy, view.attack, true });
            else
                out.push_back(cmd::Move{ wx, wy, false });
        }
        if (mouse.rpress_this_frame) out.push_back(cmd::UseSkill{ skillbar.right, wx, wy, live ? view.monsters[std::size_t(hm)].id : -1 });
        return out;
    }

    // The game this frame: what the player asks for (input), the World's
    // step, then what it told the client (events).
    void walk(const Mouse& mouse, bool over_ui, std::uint32_t ms, std::uint32_t last_ms) {
        // The skill buttons: a change goes to the World (0x3c), which runs a
        // right-button aura (a Paladin's).
        if (std::uint32_t(skillbar.left) != cc.header.left_skill) net.send(cmd::SelectSkill{ skillbar.left, true });
        if (std::uint32_t(skillbar.right) != cc.header.right_skill || (view.aura != 0) != (scene->skills.get(skillbar.right) && scene->skills.get(skillbar.right)->aura))
            net.send(cmd::SelectSkill{ skillbar.right, false });
        // The skill shrine's +all skills while its boost lasts.
        skillbar.extra.clear();
        for (const auto& [id, v] : view.boost) if (id == 127) skillbar.extra.push_back({ .stat = 127, .value = v });
        // The NPC the client's talking with (it stands meanwhile), when that changes.
        if (const int talk = npc_menu.npc >= 0 ? npc_menu.npc : speech.npc >= 0 ? speech.npc : store.npc; talk != talking_sent) {
            net.send(cmd::Chat{ talk });
            talking_sent = talk;
        }
        for (const auto& c : input(mouse, over_ui)) net.send(c);
        // Fixed ticks of kTickMs; after a stall, a few to catch up, then the
        // clock skips ahead (game.exe catches up one frame at most).
        if (world_ms == 0 || ms - world_ms > 1000) world_ms = ms - std::min<std::uint32_t>(ms - last_ms, kTickMs);
        bool ticked = false;
        for (int n = 0; ms - world_ms >= kTickMs && n < 5; ++n) {
            prev_x = view.player.x; prev_y = view.player.y;
            world.tick(net.receive(), world_ms + kTickMs, world_ms);
            world_ms += kTickMs;
            ticked = true;
        }
        if (!ticked && view.level == world.level) return;
        publish();
        // What the World said, handled once the View it came in is here
        // (the hire list's offers come with it).
        for (const auto& e : view.events) handle(e, ms);
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
            hovered_npc = -1;
            // The slide carries on in the new level's cells (a warp's
            // distance still snaps).
            prev_x += float(lc->from->world_x - level->world_x);
            prev_y += float(lc->from->world_y - level->world_y);
            npc_menu = {}; store = {}; speech = {}; waypoint = {}; menu_after_speech = -1;
            level_ms = ms;                                  // its song comes in 3 s later
            return;
        }
        const auto& ui = std::get<ev::OpenUI>(e);
        if (ui.kind == ev::OpenUI::stash) { stash_open = inv_open = true; char_open = quest_log.open = false; return; }
        if (ui.kind == ev::OpenUI::trade) { inv_open = true; char_open = stash_open = cube_open = quest_log.open = false; return; }
        if (ui.kind == ev::OpenUI::hire) {
            npc_menu = open_hire_menu(*scene, ui.npc, hire_offers, cc.stats.get(d2d::d2s::kGold) + cc.stats.get(d2d::d2s::kGoldBank));
            return;
        }
        if (ui.kind == ev::OpenUI::waypoint) {
            waypoint = { .open = true };
            inv_open = char_open = stash_open = cube_open = quest_log.open = false;
            return;
        }
        // A quest message for the player plays at once (FUN_004a10e0 on
        // the first kind-0 one; hearing it is what the server acts on), the
        // menu after it.
        npc_quest = ui.quest;
        for (const auto& q : npc_quest)
            if (q.greet) {
                speech = start_speech(*scene, ui.npc, std::uint16_t(q.string), ms);
                net.send(cmd::QuestMessage{ ui.npc, q.string });
                menu_after_speech = ui.npc;
                return;
            }
        open_menu(ui.npc);
    }
    int menu_after_speech = -1;                    // the NPC whose menu opens once its quest speech ends
    bool questdone_sound = false;                  // the quest log's done animation began (draw → update)
    int replay_speech = 0;                         // questlast: a quest message to play again
    d2d::rules::Rain rain;                         // the weather (its state lasts the session, like game.exe's)
    std::uint32_t rain_ms = 0;
    float rain_cam_x = 0, rain_cam_y = 0;          // the camera at the last weather tick
    int rain_vol = 0;                              // the rain sound's volume 0..255
    // The Den of Evil cleared (docs/research/re/quests.md "The Den lights
    // up"): S→C 0x2d event 0 starts a count (FUN_0046b0c0); for 30 frames
    // the Den's ambient falls from 80 (FUN_0046bd50), then its rooms get
    // light beams, missile denofevillight, three to a room at free spots
    // (FUN_0046b0d0 / FUN_0046af70), and keep them (client func 23).
    int den_flash = -1;
    bool den_seen = false, den_lit = false;
    std::vector<View::Shot> den_beams;
    [[nodiscard]] int den_ambient() const {
        if (!level || level->id != d2d::rules::DenQuest::kDen || !view.den_cleared || den_lit) return -1;
        return den_flash < 0 ? 80 : int(d2d::rules::cos512(den_flash * 128 / 30) * 80.0f);
    }
    void den_tick(std::uint32_t ms) {
        if (view.den_cleared && !den_seen && level && level->id == d2d::rules::DenQuest::kDen) den_flash = 0;
        den_seen = view.den_cleared;
        if (!view.den_cleared) { den_flash = -1; den_lit = false; den_beams.clear(); return; }
        if (!den_lit && den_flash >= 0 && ++den_flash > 29) den_lit = true;
        if (!level || level->id != d2d::rules::DenQuest::kDen) { den_beams.clear(); return; }
        if (!den_lit || !den_beams.empty()) return;
        const auto m = scene->missiles.find("denofevillight");
        if (m == scene->missiles.end()) return;
        for (const auto& rm : level->rooms)
            for (int tries = 0, made = 0; tries < 25 && made < 3; ++tries) {
                const int sx = rm.x * 5 + rain.rng(rm.w * 5), sy = rm.y * 5 + rain.rng(rm.h * 5);
                const float x = (float(sx) + 0.5f) / 5, y = (float(sy) + 0.5f) / 5;
                if (level->blocked_here(x, y, 0x05)) continue;
                den_beams.push_back({ &m->second, x, y, 0, ms });
                ++made;
            }
        d2d::log::info("Den of Evil: {} light beams in {} rooms", den_beams.size(), level->rooms.size());
    }
    // NPC `npc`'s menu, placed by its feet on screen as render_world
    // projects them.
    void open_menu(int npc) {
        const auto& o = level->npcs[std::size_t(npc)];
        const auto& st = view.npc_states[std::size_t(npc)];
        const float dx = (o.path.empty() ? o.x : st.x) - view.player.x, dy = (o.path.empty() ? o.y : st.y) - view.player.y;
        npc_menu = open_npc_menu(*scene, *level, npc,
            int(kW) / 2 + int(std::lround((dx - dy) * (kIsoW / 2))),
            int(kH) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))),
            int(cc.stats.get(d2d::d2s::kLevel)), d2d::rules::unidentified(cc.items), [&] {
                const int d = cc.header.active_difficulty();
                const auto& f = cc.header.quests[std::size_t(std::clamp(d, 0, 2))];
                return !d2d::rules::qbit(f, 41, 0) && (d2d::rules::qbit(f, 41, 1) || d == 2);
            }());
    }

    // The frame: the world with its units, the open panels, the tree and
    // the waypoint panel.
    void draw(std::vector<std::uint8_t>& fb, const Mouse& mouse, std::uint32_t ms) {
        auto& me = view.player;                           // the client's copy: its animation clock is the client's
        const int pmode = view.pmode;
        if (const bool m = me.walking && view.running; pmode < 0 && (me.walking != player_walked || m != player_ran)) {
            player_walked = me.walking; player_ran = m; walk_ms = ms;
        }
        if (pmode < 0) me.mode_ms = std::max(me.mode_ms, walk_ms);     // a swing's end restarts it too
        const int ui_cls = std::max(cc.selected, 0);
        // Monsters in view, as units the world draws by depth.
        std::vector<Unit> extra;
        // The camera (and the player's unit) between the World's last two
        // ticks; a jump (a warp, devctl) snaps.
        // ponytail: the other units move at the tick rate, as game.exe draws them.
        const float a = std::clamp(float(ms - world_ms) / float(kTickMs), 0.f, 1.f);
        const bool jump = std::hypot(me.x - prev_x, me.y - prev_y) > 2.f;
        cam_x = jump ? me.x : prev_x + (me.x - prev_x) * a;
        cam_y = jump ? me.y : prev_y + (me.y - prev_y) * a;
        view_units(*scene, view, cam_x, cam_y, &merc_label, extra, den_beams, ms);
        const bool town = level->id == 1;             // TN/TW in town, NU/WL outside
        // A dead player has no DD composite: DT held on its last frame.
        const auto cls = kUiToSaveClass[std::max(cc.selected, 0)];
        if (pmode == kModeDD) me.mode_ms = ms - (scene->composite(cls, kModeDT, view.gfx).length_ms() - 1);
        int mode = pmode == kModeDD ? kModeDT : pmode >= 0 ? pmode : me.walking ? (view.running ? kModeRN : town ? kModeTW : kModeWL) : town ? kModeTN : kModeNU;
        std::uint32_t mode_ms = me.mode_ms;
        float rate = pmode >= 0 && pmode != kModeDD ? view.prate : 1.f;
        if (!view.seq.empty() && attack_mode(pmode)) { std::tie(mode, mode_ms) = view_seq(*scene, int(cls), view, ms); rate = 1.f; }   // an SQ skill's frame
        const auto light = frame_light(*scene, view, cam_x, cam_y, den_beams, den_ambient());
        render_ingame(fb, *scene, *view.level, ui_cls,
                      view.gfx,
                      cc.input_name, cc.hardcore,
                      cam_x, cam_y, mode,
                      me.dir, ms, held ? -1 : mouse.x, held ? -1 : mouse.y, view.npc_states,
                      inv_open ? &cc.items : nullptr,
                      char_open ? &cc.stats : nullptr, &cc.stats, &cc.panel, mode_ms, &cc.items,
                      &hovered_npc, stash_open || cube_open ? &cc.items : nullptr, cc.expansion, belt_open,
                      cube_open, &npc_menu, &speech, &automap, &store, stat_pressed,
                      nullptr, nullptr, nullptr, extra, rate, light.pal ? &light : nullptr, level->rain ? &rain : nullptr);
        view_overlays(fb, *scene, view, hovered_monster());
        skillbar.draw(fb, held ? -1 : mouse.x, held ? -1 : mouse.y);
        if (quest_log.open
            && draw_quest_log(fb, *scene, quest_log, cc.header.quests[std::size_t(std::clamp(cc.header.active_difficulty(), 0, 2))],
                              { view.den_state, view.den_log, view.den_left }, ms))
            questdone_sound = true;                    // cursor_questdone
        if (tree_open)
            draw_skill_tree(fb, *scene, int(kUiToSaveClass[ui_cls]), tree_tab, cc.stats.skills, cc.stats,
                            skill_pressed, held ? -1 : mouse.x, held ? -1 : mouse.y);
        if (waypoint.open)
            draw_waypoints(fb, *scene, waypoint, cc.header, cc.expansion, 1, mouse.x, mouse.y);
    }
};

}  // namespace
