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
    auto click_verb = [&win = win](const std::vector<std::string>& args, std::uint8_t button) {
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
    // By value: click_verb is this function's local (a [&] would dangle).
    ch.on("click", [click_verb](const std::vector<std::string>& a) { return click_verb(a, SDL_BUTTON_LEFT); });
    ch.on("rclick", [click_verb](const std::vector<std::string>& a) { return click_verb(a, SDL_BUTTON_RIGHT); });
    // Save the character now (character_store.hpp), as leaving the game does.
    ch.on("save", [&](const std::vector<std::string>&) {
        if (screen != Screen::InGame) return std::string("err not in a game\n");
        const auto err = t.save();
        return err.empty() ? std::string("ok\n") : "err " + err + "\n";
    });
    // A command straight to the World, as a client sends one (protocol.hpp);
    // applied at its next tick.
    ch.on("cmd", [&](const std::vector<std::string>& a) {
        auto f = [&](std::size_t i) { return i < a.size() ? std::stof(a[i]) : 0.f; };
        auto n = [&](std::size_t i, int d = -1) { return i < a.size() ? std::atoi(a[i].c_str()) : d; };
        const std::string k = a.size() > 1 ? a[1] : "";
        if (k == "move" && a.size() >= 4) t.net.send(cmd::Move{ f(2), f(3), true });
        else if (k == "skill" && a.size() >= 5) t.net.send(cmd::UseSkill{ n(2, 0), f(3), f(4), n(5), n(6, 0) != 0 });
        else if (k == "interact" && a.size() >= 3) t.net.send(cmd::Interact{ n(2) });
        else if (k == "pickup" && a.size() >= 3) t.net.send(cmd::Pickup{ n(2) });
        else if (k == "resurrect") t.net.send(cmd::Resurrect{});
        else if (k == "stat" && a.size() >= 3) t.net.send(cmd::StatPoint{ n(2, 0), n(3, 1) });
        else if (k == "skillpt" && a.size() >= 3) t.net.send(cmd::SkillPoint{ n(2, 0) });
        else if (k == "select" && a.size() >= 4) t.net.send(cmd::SelectSkill{ n(2, 0), n(3, 0) != 0 });
        else if (k == "belt" && a.size() >= 3) t.net.send(cmd::UseBelt{ n(2, 0) });
        else return std::string("err cmd move <x> <y> | skill <id> <x> <y> [unit] [left] | interact <npc> | pickup <unit> | resurrect"
                                " | stat <stat> [n] | skillpt <index> | select <skill> <left> | belt <slot>\n");
        return std::string("ok\n");
    });
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
        if (args.size() >= 2 && args[1] == "warps" && t.level) {     // the level's warps: index, cell, where to
            std::string out;
            for (std::size_t i = 0; i < t.level->warps.size(); ++i)
                out += std::format("{}\t{:.1f}\t{:.1f}\t{}\n", i, t.level->warps[i].x, t.level->warps[i].y, t.level->warps[i].to);
            return out + "ok\n";
        }
        if (args.size() >= 3 && args[1] == "enter" && t.level) {     // stand by warp i as if clicked: the next frame takes it
            const auto i = std::size_t(std::atoi(args[2].c_str()));
            if (i >= t.level->warps.size()) return std::string("err no such warp\n");
            std::tie(t.player.x, t.player.y) = t.level->nearest_free(t.level->warps[i].x + 0.5f, t.level->warps[i].y + 1.5f);
            t.target_x = t.player.x; t.target_y = t.player.y;
            t.player.walking = false;
            t.world.take_warp = int(i);
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "objects" && t.level) {   // shrines / chests: index, cell, kind, shrine row, mode
            std::string out;
            for (std::size_t i = 0; i < t.level->npcs.size(); ++i)
                if (const auto& n = t.level->npcs[i]; n.operate_fn == 2 || n.operate_fn == 4)
                    out += std::format("{}\t{:.1f}\t{:.1f}\t{}\t{}\t{}\t{}\n", i, n.x, n.y, n.operate_fn == 2 ? "shrine" : "chest",
                                       n.operate_fn == 2 ? n.shrine : n.trap, n.locked ? "locked" : "-",
                                       i < t.npc_states.size() && !t.npc_states[i].mode.empty() ? t.npc_states[i].mode : std::string_view(n.mode));
            return out + "ok\n";
        }
        if (args.size() >= 3 && args[1] == "operate" && t.level) {   // operate shrine / chest i where it stands
            const int i = std::atoi(args[2].c_str());
            if (i < 0 || std::size_t(i) >= t.level->npcs.size()) return std::string("err no such object\n");
            t.operate(i, t.now_ms, args.size() >= 4 ? std::atoi(args[3].c_str()) : -1);
            return std::format("ok life={} mana={} boost={}\n", t.world.cc.stats.fixed(d2d::d2s::kLife), t.world.cc.stats.fixed(d2d::d2s::kMana), t.fight.boost.shrine);
        }
        if (args.size() >= 4 && args[1] == "stat") {       // set character stat <id 0..15> to <value>
            const int id = std::atoi(args[2].c_str());
            if (id < 0 || id > 15) return std::string("err stat 0..15\n");
            t.world.cc.stats.v[std::size_t(id)] = std::atoll(args[3].c_str());   // the World's character: the client's follows
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "attack") {      // the player's attack as combat sees it
            const auto& f = t.fight.pf;                         // as combat sees it this frame (self states included)
            return std::format("ok dmg={}-{} ar={} def={} block={} dr={}%+{} mdr={} res={}/{}/{}/{} cb={} ds={} ow={} "
                               "ll={} ml={} ias={} wsm={} frw={} fhr={} fbr={} thorns={}+{}l cold={}-{} fire={}-{} light={}-{} "
                               "crit={} def_melee={} def_missile={} dodge={} avoid={} evade={} ar%={} mcrit={} wblock={}\n",
                               f.min, f.max, f.ar, f.defense, f.block, f.dr_pct, f.dr_flat, f.mdr, f.res[0], f.res[1], f.res[2],
                               f.res[3], f.crushing, f.deadly, f.open_wounds, f.life_steal, f.mana_steal, f.ias, f.wsm, f.frw,
                               f.fhr, f.fbr, f.thorns, f.thorns_light, f.elem[2].first, f.elem[2].second, f.elem[0].first,
                               f.elem[0].second, f.elem[1].first, f.elem[1].second, f.critical, f.def_melee, f.def_missile,
                               f.dodge, f.avoid, f.evade, f.ar_pct, f.mastery_crit, f.weapon_block);
        }
        if (args.size() >= 4 && args[1] == "charges") {    // hold n charges of charge-up skill id (tests)
            auto& f = t.fight;
            const int id = std::atoi(args[2].c_str());
            std::erase_if(f.charges, [&](const auto& c) { return c.skill == id; });
            f.charges.push_back({ id, f.skill_level ? f.skill_level(id) : 1, std::clamp(std::atoi(args[3].c_str()), 1, 3), ~0u });
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "release") {     // release the charges on the nearest live monster
            auto& f = t.fight;
            int best = -1; float bd = 1e9f;
            for (std::size_t i = 0; i < f.monsters.size(); ++i)
                if (const float d = std::hypot(f.monsters[i].u.x - t.player.x, f.monsters[i].u.y - t.player.y); f.monsters[i].alive() && d < bd) { bd = d; best = int(i); }
            if (best < 0) return std::string("err no monster\n");
            f.release(std::size_t(best), std::uint32_t(SDL_GetTicks()));
            return std::string("ok ") + std::to_string(f.missiles.size()) + "\n";
        }
        if (args.size() >= 2 && args[1] == "light") {      // the light grid round the player, a level (0..31) a subtile
            const auto l = frame_light(*t.scene, t.view, t.cam_x, t.cam_y);
            std::string out;
            for (int y = 0; y < d2d::rules::LightGrid::kN; ++y) {
                for (int x = 0; x < d2d::rules::LightGrid::kN; ++x) out += "0123456789abcdefghijklmnopqrstuv"[l.grid.v[std::size_t(y * 48 + x)] >> 3];
                out += '\n';
            }
            return out + "ok " + std::to_string(l.grid.x0) + " " + std::to_string(l.grid.y0) + "\n";
        }
        if (args.size() >= 2 && args[1] == "day") {        // the time of day: `debug day [degrees]`
            auto& d = t.world.day;
            if (args.size() >= 3) {
                const int deg = ((std::atoi(args[2].c_str()) % 360) + 360) % 360;
                d.time = deg * d2d::rules::kDayScale;
                d.phase = 2;                           // the last phase to start by then
                for (int p = 0; p < 6; ++p) {
                    const int st = d2d::rules::kDay[std::size_t(p)].start;
                    if (st <= deg && st > d2d::rules::kDay[std::size_t(d.phase)].start) d.phase = p;
                }
            }
            return "ok phase=" + std::to_string(d.phase) + " time=" + std::to_string(d.time) + " intensity=" + std::to_string(d.intensity()) + "\n";
        }
        if (args.size() >= 3 && args[1] == "difficulty") { // play on difficulty d: a new game's monsters
            const int d = std::clamp(std::atoi(args[2].c_str()), 0, 2);
            for (int i = 0; i < 3; ++i) t.world.cc.header.difficulty[std::size_t(i)] &= 0x7f;   // the World's character
            t.world.cc.header.difficulty[std::size_t(d)] |= 0x80;
            t.new_game();
            return std::string("ok\n");
        }
        if (args.size() >= 3 && args[1] == "quest") {      // mark Act quest <q> done (bit 0), active difficulty
            const int q = std::atoi(args[2].c_str()), n = q * 16;
            if (q < 0 || q >= 48) return std::string("err quest 0..47\n");
            auto& h = t.world.cc.header;
            auto& f = t.world.quests();
            if (args.size() >= 4 && args[3] == "reset") {   // not started; the game's Den quest too
                f[std::size_t(n >> 3)] = f[std::size_t(n >> 3) + 1] = 0;
                t.world.den = {};
                t.world.den_left = -1;
            }
            if (args.size() >= 4)                          // show / reset: its 16 bits, the Den's state
                return std::format("ok bits={:#06x} den={} skillpts={}\n", f[std::size_t(n >> 3)] | f[std::size_t(n >> 3) + 1] << 8,
                                   t.world.den.state, t.world.cc.stats.get(d2d::d2s::kSkillPts));
            h.quests[std::size_t(h.active_difficulty())][std::size_t(n >> 3)] |= std::uint8_t(1 << (n & 7));
            for (std::size_t i = 0; i < t.level->npcs.size() && i < t.npc_states.size(); ++i)
                if (const int g = t.level->npcs[i].quest)
                    t.npc_states[i].hidden = !h.quest_flag(h.active_difficulty(), g, 0);
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "unid") {       // unidentify every carried item
            for (auto& it : t.world.cc.items) if (it.location == 0 && it.panel == 1) it.identified = false;
            return "ok " + std::to_string(d2d::rules::unidentified(t.world.cc.items)) + "\n";
        }
        if (args.size() >= 3 && args[1] == "boss" && scene) {   // the nearest plain monster: a unique with <mod>...
            std::vector<int> mods;
            for (std::size_t k = 2; k < args.size(); ++k) mods.push_back(std::atoi(args[k].c_str()));
            Monster* best = nullptr;
            for (auto& m : t.world.fight.monsters)
                if (m.alive() && m.boss == d2d::rules::Boss::none
                    && (!best || std::hypot(m.u.x - t.player.x, m.u.y - t.player.y) < std::hypot(best->u.x - t.player.x, best->u.y - t.player.y)))
                    best = &m;
            if (!best) return std::string("err no monster\n");
            make_boss(*scene, *best, d2d::rules::Boss::unique, mods, -1, t.world.rng(0x10000), t.world.cc.header.active_difficulty(), t.world.rng);
            return std::format("ok #{} {} aura={} lvl={}\n", best->id, best->npc.name, best->aura, best->aura_lvl);
        }
        if (args.size() >= 2 && args[1] == "kill" && scene) {   // kill the level's monsters but <n> (quests)
            int keep = args.size() >= 3 ? std::atoi(args[2].c_str()) : 0;
            for (auto& m : t.world.fight.monsters)
                if (m.alive() && keep-- <= 0) hurt(*scene, m, m.hp, t.world.now);
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "clearinv") {   // empty the inventory grid (tests that need room)
            std::erase_if(t.world.cc.items, [](const auto& it) { return it.location == 0 && it.panel == 1; });
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "wear") {       // halve worn items' durability
            for (auto& it : t.world.cc.items) if (it.location == 1) it.durability = d2d::rules::max_durability(it) / 2;
            return std::string("ok\n");
        }
        if (args.size() >= 4 && args[1] == "points" && scene) {   // give class skill <id> n points (tests)
            const auto& ids = scene->skills.class_ids[std::size_t(kUiToSaveClass[std::max(cc.selected, 0)])];
            const auto it = std::ranges::find(ids, std::atoi(args[2].c_str()));
            if (it == ids.end()) return std::string("err not a class skill\n");
            t.world.cc.stats.skills[std::size_t(it - ids.begin())] = std::uint8_t(std::clamp(std::atoi(args[3].c_str()), 0, 99));
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "passives" && scene) {   // the passives' stats: stat=value[/itype]
            std::string out = "ok";
            for (const auto& p : d2d::rules::passive_stats(scene->skills, t.fight.calc_env()))
                out += std::format(" {}={}{}{}", p.stat, p.value, p.itype.empty() ? "" : "/", p.itype);
            return out + "\n";
        }
        if (args.size() >= 2 && args[1] == "unbuilt" && scene) {   // class skills no path in Fight takes
            std::string out;
            for (const auto& k : scene->skills.rows) {
                if (k.cls.empty() || k.id < 0) continue;
                auto& f = t.fight;
                const bool built = skill_built(k) || self_cast(k) || f.missile_skill(k) || f.spot_skill(k) || f.summon_skill(k) || k.aura
                                || (k.srvstfunc == 0 && k.srvdofunc == 0 && (k.passive_stat[0] >= 0 || k.passive));
                if (!built) out += std::format("{} {} st{} do{}\n", k.id, k.name, k.srvstfunc, k.srvdofunc);
            }
            return out + "ok\n";
        }
        if (args.size() >= 2 && args[1] == "states") {    // the player's buffs; each cursed / cried monster
            std::string out = std::format("absorb={} maxlife={} maxmana={} def={} buffs=", t.fight.absorb_pool,
                                          cc.stats.fixed(d2d::d2s::kMaxLife), cc.stats.fixed(d2d::d2s::kMaxMana), t.fight.pf.defense);
            for (const auto& st : t.fight.self_states) out += std::format("{}:{},", st.skill, st.level);
            out += "\n";
            for (const auto& m : t.fight.monsters)
                if (m.curse.skill >= 0 || m.cry.skill >= 0)
                    out += std::format("{} hp={} curse={} cry={} dmg%={} speed%={} reflect%={} flee={} blind={}\n", m.npc.name, m.hp,
                                       m.curse.skill, m.cry.skill, m.dmg_pct, m.speed_pct, m.reflect_pct, m.flee_until, m.blind_until);
            return out + "ok\n";
        }
        if (args.size() >= 2 && args[1] == "pets") {      // each pet: row level life dmg th ac res ranged/aura
            std::string out;
            for (const auto& p : t.fight.pets)
                out += std::format("{} L{} hp={}/{} dmg={}-{} th={} ac={} res={},{},{},{} fire={}-{} ranged={}:{} aura={}:{} here={}\n",
                                   p.m.npc.name, p.m.st.level, p.m.hp, p.m.st.hp, p.m.st.a1_min, p.m.st.a1_max, p.m.st.th, p.m.st.ac,
                                   p.res[0], p.res[1], p.res[2], p.res[3], p.fire_lo, p.fire_hi, p.ranged, p.ranged_level, p.aura,
                                   p.aura_level, p.where == t.level);
            return out + "ok\n";
        }
        if (args.size() >= 4 && args[1] == "skill") {     // put skill <id> on the left / right button, if usable
            const int id = std::atoi(args[3].c_str());
            const bool left = args[2] == "left";
            if (!t.skillbar.usable(id, left)) return std::string("err not usable\n");
            (left ? t.skillbar.left : t.skillbar.right) = id;
            return std::string("ok\n");
        }
        if (args.size() >= 3 && (args[1] == "statpts" || args[1] == "skillpts")) {   // grant unspent points
            t.world.cc.stats.v[args[1] == "statpts" ? d2d::d2s::kStatPts : d2d::d2s::kSkillPts] = std::atoi(args[2].c_str());
            return std::string("ok\n");
        }
        if (args.size() < 2 || args[1] != "collision")
            return std::string("err debug collision|automap|statpts <n>|skillpts <n>|wear|unid|level|blocked <x> <y>|warp <x> <y>|stat <id> <v>|quest <q>|skill left|right <id>|points <id> <n>\n");
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
                 + std::format(" size={}x{}", d2d::rules::item_size(scene->rules, it.code).first, d2d::rules::item_size(scene->rules, it.code).second)
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
            out += std::format("{}\t{}\t{}\t{}\t{}\t{:.1f}\t{:.1f}\n", n.name, sx, sy, menu ? 1 : 0, i, dx + t.player.x, dy + t.player.y);
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
            static constexpr std::array<const char*, 5> kBoss = { "-", "champion", "unique", "superunique", "minion" };
            std::string mods;
            for (const int md : m.mods) mods += (mods.empty() ? "" : ",") + std::to_string(md);
            out += std::format("{}\t{:.2f}\t{:.2f}\t{}\t{}\t{}/{}\t{}\t{}\t{}\tlvl{}\t{}\t#{}\n", scene->monsters.types[std::size_t(m.type)].id,
                               m.u.x, m.u.y, sx, sy, m.hp, m.st.hp, m.mode, kBoss[std::size_t(m.boss)], mods.empty() ? "-" : mods,
                               m.st.level, m.npc.name, m.id);
        }
        return out + "ok\n";
    });
    // Loot on the ground: "<code>\t<label>\t<sx>\t<sy>" (feet on screen, game pixels).
    ch.on("ground", [&](const std::vector<std::string>&) {
        std::string out;
        for (const auto& g : t.loot.ground) {
            const float dx = g.x - t.player.x, dy = g.y - t.player.y;
            out += std::format("{}\t{}\t{}\t{}\t#{}\n", g.item.code, g.label,
                               int(kW) / 2 + int(std::lround((dx - dy) * (kIsoW / 2))),
                               int(kH) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))), g.id);
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
             + " name=" + (screen == Screen::CharSelect && scene && csu.selected >= 0 && std::size_t(csu.selected) < scene->saves.size()
                           ? std::string(scene->saves[std::size_t(csu.selected)].name) : cc.input_name)
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
             + " mana=" + std::to_string(cc.stats.fixed(d2d::d2s::kMana)) + "/" + std::to_string(cc.stats.fixed(d2d::d2s::kMaxMana))
             + " level=" + std::to_string(cc.stats.get(d2d::d2s::kLevel)) + " exp=" + std::to_string(cc.stats.get(d2d::d2s::kExp))
             + " pmode=" + (t.fight.pmode >= 0 ? kModeCode[t.fight.pmode] : "-")
             + " missiles=" + std::to_string(t.fight.missiles.size())
             + " pets=" + [&] { std::string o; for (const auto& p : t.fight.pets) o += (o.empty() ? "" : ",") + std::to_string(p.m.hp) + "/" + std::to_string(p.m.st.hp) + ":" + std::string(p.m.mode); return o.empty() ? std::string("-") : o; }()
             + " lskill=" + std::to_string(t.skillbar.left) + " rskill=" + std::to_string(t.skillbar.right)
             + " picker=" + std::to_string(t.skillbar.picking)
             + " charges=" + [&] {
                   std::string c;
                   for (const auto& g : t.fight.charges) c += std::format("{}{}:{}", c.empty() ? "" : ",", g.skill, g.count);
                   return c.empty() ? std::string("-") : c;
               }()
             + " skillpts=" + std::to_string(cc.stats.get(d2d::d2s::kSkillPts))
             + " tree=" + (t.tree_open ? std::to_string(t.tree_tab) : "0")
             + " waypoint=" + std::to_string(t.waypoint.open ? int(scene->waypoint_levels[std::size_t(t.waypoint.tab)].size()) : 0)
             + " items=" + std::to_string(cc.items.size())
             + " held=" + (t.held ? t.held->code : "-")
             + " unid=" + std::to_string(d2d::rules::unidentified(cc.items))
             + " merc=" + (t.merc ? std::format("{:.1f},{:.1f}", t.merc->x, t.merc->y) + ":" + t.world.merc_npc->code : std::string("-"))
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
