// The devctl verbs that drive and read the game for scripted tests
// (docs/control_channel.md): input (click, key, move, wheel), debug
// set-ups, and reads of the game's state (state, items, npcs, monsters,
// ground, menu). The frame-level ones (info, screenshot, quit) stay in
// main.cpp.
#pragma once

#include "town.hpp"

namespace {

// A screen's name in `state` replies.
const char* screen_name(Screen s) {
    switch (s) {
        case Screen::Title:      return "title";
        case Screen::Credits:    return "credits";
        case Screen::CharSelect: return "charselect";
        case Screen::CharCreate: return "charcreate";
        case Screen::InGame:     return "ingame";
        case Screen::Video:      return "video";
        case Screen::Cinematics: return "cinematics";
    }
    return "?";
}

// Registers them on `ch`. They capture main()'s loop locals by reference;
// the channel is only pumped inside that loop, so the captures never
// outlive it.
void register_game_verbs(d2d::devctl::Channel& ch, Window& win, Screen& screen, CharSelectUI& csu, CharCreateUI& cc,
                         Town& t, const std::optional<Scene>& scene, Audio& audio) {
    auto click_verb = [&](const std::vector<std::string>& args, std::uint8_t button) {
        if (args.size() < 3) return std::string("err click <x> <y>\n");
        // Args are game pixels; queued events carry window coords and get
        // converted back by SDL_ConvertEventToRenderCoordinates on poll.
        float x = 0, y = 0;
        SDL_RenderCoordinatesToWindow(win.r, std::stof(args[1]), std::stof(args[2]), &x, &y);
        // Motion + down + up land in the next frame's poll. One frame is
        // enough: update_button and the slot picker both accept a press
        // and release in the same frame.
        SDL_Event ev{};
        ev.motion = { .type = SDL_EVENT_MOUSE_MOTION, .windowID = SDL_GetWindowID(win.w), .x = x, .y = y };
        SDL_PushEvent(&ev);
        for (auto type : { SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP }) {
            ev = {};
            ev.button = { .type = type, .windowID = SDL_GetWindowID(win.w),
                          .button = button,
                          .down = type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                          .clicks = 1, .x = x, .y = y };
            SDL_PushEvent(&ev);
        }
        return std::string("ok\n");
    };
    ch.on("click", [&](const std::vector<std::string>& a) { return click_verb(a, SDL_BUTTON_LEFT); });
    ch.on("rclick", [&](const std::vector<std::string>& a) { return click_verb(a, SDL_BUTTON_RIGHT); });
    ch.on("key", [&](const std::vector<std::string>& args) {
        if (args.size() < 2) return std::string("err key <name>\n");
        const SDL_Keycode k = SDL_GetKeyFromName(args[1].c_str());
        if (k == SDLK_UNKNOWN) return std::string("err unknown key\n");
        for (auto type : { SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP }) {
            SDL_Event ev{};
            ev.key.type = type;
            ev.key.windowID = SDL_GetWindowID(win.w);
            ev.key.key = k;
            ev.key.down = type == SDL_EVENT_KEY_DOWN;
            SDL_PushEvent(&ev);
        }
        return std::string("ok\n");
    });
    ch.on("move", [&](const std::vector<std::string>& args) {
        if (args.size() < 3) return std::string("err move <x> <y>\n");
        float x = 0, y = 0;
        SDL_RenderCoordinatesToWindow(win.r, std::stof(args[1]), std::stof(args[2]), &x, &y);
        SDL_Event ev{};
        ev.motion = { .type = SDL_EVENT_MOUSE_MOTION, .windowID = SDL_GetWindowID(win.w), .x = x, .y = y };
        SDL_PushEvent(&ev);
        return std::string("ok\n");
    });
    ch.on("wheel", [&](const std::vector<std::string>& args) {
        if (args.size() < 2) return std::string("err wheel <dy>\n");
        SDL_Event ev{};
        ev.wheel = { .type = SDL_EVENT_MOUSE_WHEEL, .windowID = SDL_GetWindowID(win.w),
                     .integer_y = std::stoi(args[1]) };
        SDL_PushEvent(&ev);
        return std::string("ok\n");
    });
    ch.on("debug", [&](const std::vector<std::string>& args) {
        if (args.size() >= 2 && args[1] == "automap" && scene) {    // reveal the whole level
            const auto& m = t.level->ds1;
            for (int y = 0; y < int(m.height()); y += 12)
                for (int x = 0; x < int(m.width()); x += 12) automap_reveal(*scene, *t.level, t.automap, float(x), float(y));
            return "ok " + std::to_string(t.automap.cells.size()) + "\n";
        }
        if (args.size() >= 2 && args[1] == "level" && t.level) {   // where the player is
            return std::format("ok {} {:.2f} {:.2f} {} {} {} {}\n", t.level->id, t.player.x, t.player.y,
                               t.level->ds1.width(), t.level->ds1.height(), t.level->world_x, t.level->world_y);
        }
        if (args.size() >= 4 && args[1] == "blocked" && t.level) {   // can a unit stand at (x, y)?
            return std::format("ok {}\n", int(t.level->unit_blocked(std::strtof(args[2].c_str(), nullptr),
                                                                   std::strtof(args[3].c_str(), nullptr))));
        }
        if (args.size() >= 4 && args[1] == "warp") {       // put the player at cell (x, y)
            t.player.x = t.target_x = std::strtof(args[2].c_str(), nullptr);
            t.player.y = t.target_y = std::strtof(args[3].c_str(), nullptr);
            t.player.walking = false;
            return std::string("ok\n");
        }
        if (args.size() >= 4 && args[1] == "stat") {       // set character stat <id 0..15> to <value>
            const int id = std::atoi(args[2].c_str());
            if (id < 0 || id > 15) return std::string("err stat 0..15\n");
            cc.stats.v[std::size_t(id)] = std::atoll(args[3].c_str());
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "attack") {      // the player's attack as combat sees it
            const auto f = t.fight.player_fighter();
            return std::format("ok dmg={}-{} ar={} def={} block={} dr={}%+{} mdr={} res={}/{}/{}/{} cb={} ds={} ow={} "
                               "ll={} ml={} ias={} wsm={} frw={} fhr={} fbr={} thorns={}+{}l cold={}-{} fire={}-{} light={}-{} "
                               "crit={} def_melee={} def_missile={} dodge={} avoid={} evade={}\n",
                               f.min, f.max, f.ar, f.defense, f.block, f.dr_pct, f.dr_flat, f.mdr, f.res[0], f.res[1], f.res[2],
                               f.res[3], f.crushing, f.deadly, f.open_wounds, f.life_steal, f.mana_steal, f.ias, f.wsm, f.frw,
                               f.fhr, f.fbr, f.thorns, f.thorns_light, f.elem[2].first, f.elem[2].second, f.elem[0].first,
                               f.elem[0].second, f.elem[1].first, f.elem[1].second, f.critical, f.def_melee, f.def_missile,
                               f.dodge, f.avoid, f.evade);
        }
        if (args.size() >= 3 && args[1] == "difficulty") { // play on difficulty d: a new game's monsters
            const int d = std::clamp(std::atoi(args[2].c_str()), 0, 2);
            for (int i = 0; i < 3; ++i) cc.header.difficulty[std::size_t(i)] &= 0x7f;
            cc.header.difficulty[std::size_t(d)] |= 0x80;
            t.new_game();
            return std::string("ok\n");
        }
        if (args.size() >= 3 && args[1] == "quest") {      // mark Act quest <q> done (bit 0), active difficulty
            const int q = std::atoi(args[2].c_str()), n = q * 16;
            if (q < 0 || q >= 48) return std::string("err quest 0..47\n");
            cc.header.quests[std::size_t(cc.header.active_difficulty())][std::size_t(n >> 3)] |= std::uint8_t(1 << (n & 7));
            for (std::size_t i = 0; i < t.level->npcs.size() && i < t.npc_states.size(); ++i)
                if (const int g = t.level->npcs[i].quest)
                    t.npc_states[i].hidden = !cc.header.quest_flag(cc.header.active_difficulty(), g, 0);
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "unid") {       // unidentify every carried item
            for (auto& it : cc.items) if (it.location == 0 && it.panel == 1) it.identified = false;
            return "ok " + std::to_string(d2d::rules::unidentified(cc.items)) + "\n";
        }
        if (args.size() >= 2 && args[1] == "wear") {       // halve worn items' durability
            for (auto& it : cc.items) if (it.location == 1) it.durability = d2d::rules::max_durability(it) / 2;
            return std::string("ok\n");
        }
        if (args.size() >= 3 && (args[1] == "statpts" || args[1] == "skillpts")) {   // grant unspent points
            cc.stats.v[args[1] == "statpts" ? d2d::d2s::kStatPts : d2d::d2s::kSkillPts] = std::atoi(args[2].c_str());
            return std::string("ok\n");
        }
        if (args.size() < 2 || args[1] != "collision")
            return std::string("err debug collision|automap|statpts <n>|skillpts <n>|wear|unid|level|blocked <x> <y>|warp <x> <y>|stat <id> <v>|quest <q>\n");
        g_debug_collision = !g_debug_collision;
        return std::string(g_debug_collision ? "ok on\n" : "ok off\n");
    });
    // Every item of the in-game character with its hover text, one item
    // per paragraph (headless check for the tooltips).
    ch.on("items", [&](const std::vector<std::string>&) {
        if (!scene) return std::string("err no scene\n");
        std::string out;
        for (const auto& it : cc.items) {
            out += "[" + it.code + " loc=" + std::to_string(it.location) + " slot=" + std::to_string(it.slot)
                 + " q=" + std::to_string(it.quality) + " panel=" + std::to_string(it.panel)
                 + " at=" + std::to_string(it.column) + "," + std::to_string(it.row) + "]\n";
            for (const auto& l : item_lines(*scene, it, int(cc.stats.get(d2d::d2s::kLevel))))
                out += "  " + l.text + "\n";
        }
        return out + "ok\n";
    });
    // Named NPCs/objects with their feet on screen (game pixels) and
    // whether they have an NPC menu: "<name>\t<x>\t<y>\t<menu 0/1>".
    ch.on("npcs", [&](const std::vector<std::string>&) {
        if (!scene) return std::string("err no scene\n");
        std::string out;
        for (std::size_t i = 0; i < t.level->npcs.size(); ++i) {
            const auto& n = t.level->npcs[i];
            if (n.name.empty() || (i < t.npc_states.size() && t.npc_states[i].hidden)) continue;
            const bool live = i < t.npc_states.size() && !n.path.empty();
            const float dx = (live ? t.npc_states[i].x : n.x) - t.player.x, dy = (live ? t.npc_states[i].y : n.y) - t.player.y;
            const int sx = int(kW) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
            const int sy = int(kH) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2)));
            const bool menu = std::ranges::any_of(kNpcMenus, [&](const NpcMenu& e) { return e.hc_idx == n.hc_idx; });
            out += n.name + "\t" + std::to_string(sx) + "\t" + std::to_string(sy) + "\t" + (menu ? "1" : "0") + "\n";
        }
        return out + "ok\n";
    });
    // The Blood Moor's monsters (kept while in town too):
    // "<id>\t<x>\t<y>\t<sx>\t<sy>\t<hp>/<max>\t<mode>" — cells in the moor,
    // feet on screen (game pixels, meaningful while in the moor).
    ch.on("monsters", [&](const std::vector<std::string>&) {
        if (!scene) return std::string("err no scene\n");
        std::string out;
        for (const auto& m : t.fight.monsters) {
            const float dx = m.u.x - t.player.x, dy = m.u.y - t.player.y;
            const int sx = int(kW) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
            const int sy = int(kH) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2)));
            out += std::format("{}\t{:.2f}\t{:.2f}\t{}\t{}\t{}/{}\t{}\n", scene->monsters.types[std::size_t(m.type)].id,
                               m.u.x, m.u.y, sx, sy, m.hp, m.st.hp, m.mode);
        }
        return out + "ok\n";
    });
    // Loot on the ground: "<code>\t<label>\t<sx>\t<sy>" (feet on screen, game pixels).
    ch.on("ground", [&](const std::vector<std::string>&) {
        std::string out;
        for (const auto& g : t.loot.ground) {
            const float dx = g.x - t.player.x, dy = g.y - t.player.y;
            out += std::format("{}\t{}\t{}\t{}\n", g.item.code, g.label,
                               int(kW) / 2 + int(std::lround((dx - dy) * (kIsoW / 2))),
                               int(kH) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))));
        }
        return out + "ok\n";
    });
    // The open NPC menu's lines: "<text>\t<x>\t<y>" with a point inside
    // each (game pixels), header first.
    ch.on("menu", [&](const std::vector<std::string>&) {
        std::string out;
        int base = t.npc_menu.y;
        for (const auto& l : t.npc_menu.lines) {
            base += l.height;
            out += l.text + "\t" + std::to_string(t.npc_menu.x + t.npc_menu.w / 2) + "\t"
                 + std::to_string(base - l.height / 2) + "\n";
        }
        return out + "ok\n";
    });
    ch.on("state", [&](const std::vector<std::string>&) {
        return std::string("screen=") + screen_name(screen)
             + " save=" + std::to_string(csu.selected)
             + " scroll=" + std::to_string(csu.scroll)
             + " class=" + std::to_string(cc.selected)
             + " name=" + cc.input_name
             + " hardcore=" + (cc.hardcore ? "1" : "0")
             + " cam=" + std::format("{:.2f},{:.2f}", t.player.x, t.player.y)
             + " walking=" + (t.player.walking ? "1" : "0") + " dir=" + std::to_string(t.player.dir)
             + " saves=" + std::to_string(scene ? scene->saves.size() : 0)
             + " stash=" + (t.stash_open ? "1" : "0")
             + " cube=" + (t.cube_open ? "1" : "0")
             + " store=" + std::to_string(t.store.npc >= 0 ? t.store.vendor : -1)
             + " gold=" + std::to_string(cc.stats.get(d2d::d2s::kGold))
             + " statpts=" + std::to_string(cc.stats.get(d2d::d2s::kStatPts))
             + " str=" + std::to_string(cc.stats.get(d2d::d2s::kStr))
             + " life=" + std::to_string(cc.stats.fixed(d2d::d2s::kLife)) + "/" + std::to_string(cc.stats.fixed(d2d::d2s::kMaxLife))
             + " level=" + std::to_string(cc.stats.get(d2d::d2s::kLevel)) + " exp=" + std::to_string(cc.stats.get(d2d::d2s::kExp))
             + " pmode=" + (t.fight.pmode >= 0 ? kModeCode[t.fight.pmode] : "-")
             + " missiles=" + std::to_string(t.fight.missiles.size())
             + " skillpts=" + std::to_string(cc.stats.get(d2d::d2s::kSkillPts))
             + " tree=" + (t.tree_open ? std::to_string(t.tree_tab) : "0")
             + " waypoint=" + std::to_string(t.waypoint.open ? int(scene->waypoint_levels[std::size_t(t.waypoint.tab)].size()) : 0)
             + " items=" + std::to_string(cc.items.size())
             + " held=" + (t.held ? t.held->code : "-")
             + " unid=" + std::to_string(d2d::rules::unidentified(cc.items))
             + " merc=" + (t.merc ? std::format("{:.1f},{:.1f}", t.merc->x, t.merc->y) + ":" + t.merc_npc->code : std::string("-"))
             + " menu=" + std::to_string(t.npc_menu.npc >= 0 ? int(t.npc_menu.lines.size()) : 0)
             + " automap=" + std::to_string(t.automap.open ? int(t.automap.cells.size()) : 0)
             + " speech=" + std::to_string(t.speech.npc >= 0 ? int(t.speech.lines.size()) : 0)
             + " voice=" + std::to_string(audio.voice_sound())
             + " music=" + std::to_string(audio.music.sound)
             + " music_old=" + std::to_string(audio.music_old.sound)
             + "\nok\n";
    });
}

}  // namespace
