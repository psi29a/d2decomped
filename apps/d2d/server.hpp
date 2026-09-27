// The game server's side (docs/design/multiplayer.md): the world a game
// runs — levels, the player's unit, the merc, NPCs, monsters, missiles,
// ground items, objects, the rolls — stepped by commands from the client
// (protocol.hpp). What the client needs to hear back goes out as Events.
// ponytail: one player; the character (cc) is shared with the client
// until the CharacterStore, and item moves, stat / skill points and the
// store still edit it client-side; the rng is shared too.
#pragma once

#include "protocol.hpp"
#include "fight.hpp"
#include "character_store.hpp"

#include <ctime>

namespace {

// What the server tells the client (the S -> C side, network.md).
namespace ev {
// The player's level changed (`from`): a crossing or warp (`keep_map`:
// the automap stays open) or a respawn.
struct LevelChanged { const Level* from = nullptr; bool keep_map = false; };
// Arrived at an object or NPC that opens a panel (game.exe's 0x58 "open
// UI" and the NPC interaction).
// A talk carries the NPC's quest messages for the player (quests.hpp).
struct OpenUI { enum Kind { stash, waypoint, talk, trade, hire } kind = stash; int npc = -1; std::vector<d2d::rules::QuestMsg> quest; };
}  // namespace ev
using Event = std::variant<ev::LevelChanged, ev::OpenUI>;

// What a client is told after each tick (the S -> C side, network.md):
// everything it draws and clicks on. In-process these are copies; a TCP
// transport sends them as bytes.
// ponytail: monsters go whole (the fields a client needs aren't picked yet);
// the character (cc), the store and the item in hand are still shared with
// the World; levels are pointers into the shared GameData (the same on every
// machine, from the map seed).
struct View {
    const Level* level = nullptr;
    UnitState player;
    bool running = false, dead = false;
    int pmode = -1;                        // Fight::pmode: A1, GH, BL, DT, DD ... (-1 none)
    float prate = 1.f;
    std::vector<d2d::rules::SeqFrame> seq;   // an SQ skill's frames while it plays
    std::uint32_t seq_frame_ms = 40;
    bool seq_loop = false;
    GameData::Appearance gfx{};               // what the character wears
    struct Merc { UnitState u; const Npc* npc = nullptr; std::string_view mode; };
    std::optional<Merc> merc;
    struct Pet { Npc npc; UnitState u; std::string_view mode; };
    std::vector<Pet> pets;                 // the player's summons on this level
    std::vector<Monster> monsters;         // the level's
    struct Shot { const GameData::MissileInfo* info = nullptr; float x = 0, y = 0; int dir = 0; std::uint32_t born = 0; };
    std::vector<Shot> missiles;
    int attack = -1, attack_skill = 0;     // the monster the player's attacking (unit id), with what
    std::vector<Loot::GroundItem> ground;  // the level's floor
    struct Fire { float x, y; const Npc* npc; };
    std::vector<Fire> fires;
    std::vector<UnitState> npc_states;     // the level's NPCs as they patrol
    std::vector<std::pair<int, int>> boost;   // the shrine boost's stats while it lasts
    int aura = 0;                          // the aura that's on
    // The player's own character (what only its owner is told): header,
    // stats, items with their unit ids, the item in hand; the open store's
    // stock and the hire list.
    bool has_character = false;
    d2d::d2s::Header header;
    d2d::d2s::Stats stats;
    std::vector<d2d::d2s::Item> items;
    std::optional<d2d::d2s::Item> held;
    std::optional<Store> store;
    std::vector<d2d::rules::MercOffer> hire_offers;
    std::vector<Cues::Cue> sounds;         // the world's sounds cued since the last View
    std::vector<Event> events;             // what the World said since the last View
    // A monster by unit id: its index in `monsters`, or -1.
    [[nodiscard]] int monster(int id) const {
        const auto it = std::ranges::find(monsters, id, &Monster::id);
        return id < 0 || it == monsters.end() ? -1 : int(it - monsters.begin());
    }
};

// The game's step: game.exe's frame, 1000 / 25 ms (FUN_0052fc20,
// docs/research/re/network.md).
constexpr std::uint32_t kTickMs = 40;

struct World {
    const GameData* scene = nullptr;
    CharCreateUI cc;                       // the character: the World's own (the client's is a copy of the View's)
    const Level* level = nullptr;          // where the player is: the town, the Blood Moor, the Den of Evil
    UnitState player;                      // DS1 cells (x.5 = a cell centre)
    std::optional<UnitState> merc;          // the save's mercenary, following
    const Npc* merc_npc = nullptr;
    std::vector<UnitState> npc_states;     // the level's NPCs as they patrol
    d2d::rules::Rng rng{ 0x2545f491u };    // rolls (the client's stock, talk topics, gambles, merc offers too)
    Cues  cues{ scene };                   // world sounds due later
    Loot  loot{ scene, level, cc, player, rng, cues };                       // on the ground (loot.hpp)
    Fight fight{ scene, level, cc, player, merc, merc_npc, rng, loot, cues };  // the fight (fight.hpp)
    float target_x = 0, target_y = 0;      // where the player is walking to
    bool  running = false;                 // run / walk (game.exe's 0x53 / 0x54)
    int   take_warp = -1;                  // the warp of `level` the player is walking to
    int   interact_npc = -1;               // the object / NPC being walked to
    int   pick_item = -1;                  // the ground item being walked to (its unit id)
    std::vector<int> not_there;            // levels walked toward that aren't built (logged once)
    std::map<std::pair<const Level*, int>, std::uint32_t> operated;   // shrines / chests used: when
    struct Fire { const Level* level; const Npc* npc; float x, y; };
    std::vector<Fire> fires;               // chest traps 5 / 7 left these burning
    std::uint32_t now = 0;                 // the tick's time
    std::array<int, 3> talking{ -1, -1, -1 };   // NPCs the client has a menu, speech or store open with (they stand)
    std::vector<Event> events;             // for the client, since it last looked
    const CharacterStore* characters = nullptr;   // where the character is saved
    std::optional<d2d::d2s::Item> held;    // the item in the player's hand (the cursor)
    Store store;                           // the NPC window open (npc < 0: none): its stock
    std::vector<d2d::rules::MercOffer> hire_offers;   // Kashya's list while it's open
    int next_item_id = 1;                  // the next item's unit id
    d2d::rules::DenQuest den;              // this game's Den of Evil
    int den_left = -1;                     // its monsters alive when last counted
    // The quest flags of the difficulty played.
    d2d::rules::QuestBits& quests() { return cc.header.quests[std::size_t(std::clamp(cc.header.active_difficulty(), 0, 2))]; }
    // Every item of the character has a unit id (new ones get theirs).
    void item_ids() {
        for (auto& it : cc.items) if (it.id < 0) it.id = next_item_id++;
        if (held && held->id < 0) held->id = next_item_id++;
    }

    // At --start-cam-x/y, else the town start (Level::start), else the
    // map's middle; then the nearest free spot so we never start inside a
    // tent.
    World(const GameData* s, int start_x, int start_y) : scene(s), level(s ? &s->town : nullptr) {
        const bool have_world = level && !level->dt1s.empty();
        player.x = (start_x >= 0 ? float(start_x) : have_world ? float(level->ds1.width() / 2) : 0.f) + 0.5f;
        player.y = (start_y >= 0 ? float(start_y) : have_world ? float(level->ds1.height() / 2) : 0.f) + 0.5f;
        if (have_world && start_x < 0 && start_y < 0 && level->start.first >= 0)
            std::tie(player.x, player.y) = level->start;
        if (have_world) std::tie(player.x, player.y) = level->nearest_free(player.x, player.y);
        if (scene) npc_states = npc_start(*level);
        if (scene) fight.new_game(0);
        target_x = player.x; target_y = player.y;
        player.dir = 4;                    // south, facing the viewer
        // The character's skill levels for the fight, the skill shrine's
        // +all skills (item_allskills) while its boost lasts.
        fight.skill_base = [this](int id) { return skill_base_level(*scene, cc, id); };
        fight.skill_level = [this](int id) {
            std::vector<d2d::d2s::ItemProp> extra;
            if (now < fight.boost.until)
                for (const auto& [st, v] : fight.boost.stats) if (st == 127) extra.push_back({ .stat = 127, .value = v });
            return skill_level(*scene, cc, id, extra);
        };
    }
    World(const World&) = delete;

    // What the client is told (View).
    [[nodiscard]] View view() const {
        View v;
        v.level = level;
        v.player = player;
        v.running = running;
        v.dead = fight.dead();
        v.pmode = fight.pmode;
        v.prate = fight.prate;
        v.seq.assign(fight.seq.begin(), fight.seq.end()); v.seq_frame_ms = fight.seq_frame_ms; v.seq_loop = fight.seq_loop;
        v.gfx = fight.gfx();
        if (merc && merc_npc) v.merc = View::Merc{ *merc, merc_npc, fight.merc_mode };
        for (const auto& p : fight.pets) if (p.where == level) v.pets.push_back({ p.m.npc, p.m.u, p.m.mode });
        if (level == fight.mon_level) {
            // What's near the player (D2 sends the units of the rooms round
            // each player). ponytail: a radius of 28 cells, not rooms.
            for (const auto& m : fight.monsters)
                if (std::abs(m.u.x - player.x) < 28 && std::abs(m.u.y - player.y) < 28) v.monsters.push_back(m);
            for (const auto& m : fight.missiles) if (m.info) v.missiles.push_back({ m.info, m.x, m.y, m.dir, m.born });
        }
        if (fight.attack_mon >= 0 && std::size_t(fight.attack_mon) < fight.monsters.size()) v.attack = fight.monsters[std::size_t(fight.attack_mon)].id;
        v.attack_skill = fight.attack_skill;
        if (loot.ground_level == level) v.ground = loot.ground;
        for (const auto& f : fires) if (f.level == level) v.fires.push_back({ f.x, f.y, f.npc });
        v.npc_states = npc_states;
        if (now < fight.boost.until) v.boost = fight.boost.stats;
        v.aura = fight.aura;
        v.has_character = true;
        v.header = cc.header; v.stats = cc.stats; v.items = cc.items; v.held = held;
        if (store.npc >= 0) v.store = store;
        v.hire_offers = hire_offers;
        return v;
    }

    // The character to the CharacterStore: the save's header with what the
    // game changed (level, when last played, the gear's look), its stats and
    // items. "" when it's written, else why not.
    std::string save() {
        if (!characters) return "no character store";
        stow_held(*scene, cc.items, held);                  // the item in hand goes back first
        auto h = cc.header;
        h.level = std::uint8_t(std::clamp<std::int64_t>(cc.stats.get(d2d::d2s::kLevel), 1, 99));
        h.last_played = std::uint32_t(std::time(nullptr));
        if (cc.appearance) h.appearance = *cc.appearance;
        const auto err = characters->save(h, cc.stats, cc.items);
        if (err.empty()) cc.header.last_played = h.last_played;
        d2d::log::info("save {}: {}", h.name, err.empty() ? "written" : err);
        return err;
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
        // Storm (FUN_00582da0): everyone about loses Arg0 % of their life.
        // ponytail: "about" as within 30 cells (game.exe's unit search
        // over Arg1 isn't traced).
        if (s.code == 19) {
            v[kLife] -= v[kLife] * s.arg0 / 100;
            fight.merc_life -= fight.merc_life * s.arg0 / 100;
            if (fight.mon_level == level)
                for (auto& m : fight.monsters)
                    if (m.alive() && std::abs(m.u.x - o.x) < 30 && std::abs(m.u.y - o.y) < 30) m.hp -= m.hp * s.arg0 / 100;
        }
        // Exploding / Poison: Arg0 .. Arg1 - 1 exploding (opm) / choking gas
        // (gpm) potions at the player's feet (FUN_005830e0 / FUN_00583410).
        if (s.code == 21 || s.code == 22)
            for (int n = s.arg0 + rng(std::max(s.arg1 - s.arg0, 0)); n > 0; --n)
                loot.put({ .code = s.code == 21 ? "opm" : "gpm" }, player.x, player.y, 1, ms);
        if ((s.code == 19 || s.code == 21 || s.code == 22) && fight.mon_level == level)
            fight.shrine_missiles(s.code, o.x, o.y, int(cc.stats.get(kLevel)), ms);
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
        d2d::log::info("shrine {} (code {}){}", row, s.code, s.code == 16 || s.code == 17 ? ", not built" : "");
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
    // A player enters with their character (a save loaded, or made): the
    // World takes its copy, the merc comes along, a fresh game; the Act 1
    // quest-gated NPCs (Cain) are there once their quest is done.
    void enter(const CharCreateUI& c) {
        cc.selected = c.selected; cc.input_name = c.input_name; cc.hardcore = c.hardcore;
        cc.appearance = c.appearance; cc.items = c.items; cc.stats = c.stats; cc.panel = c.panel;
        cc.expansion = c.expansion; cc.header = c.header;
        // A new game starts at full life, mana and stamina, whatever the
        // save held (D2's "save and exit to heal").
        for (const auto& [cur, max] : { std::pair{ d2d::d2s::kLife, d2d::d2s::kMaxLife }, { d2d::d2s::kMana, d2d::d2s::kMaxMana },
                                        { d2d::d2s::kStamina, d2d::d2s::kMaxStamina } })
            cc.stats.v[cur] = cc.stats.v[max];
        held.reset();
        store = {};
        spawn_merc();
        new_game();
        for (std::size_t i = 0; i < level->npcs.size() && i < npc_states.size(); ++i)
            if (const int q = level->npcs[i].quest)
                npc_states[i].hidden = !cc.header.quest_flag(cc.header.active_difficulty(), q, 0);
    }

    void new_game() {
        fight.new_game(cc.header.active_difficulty());
        loot.ground.clear();
        loot.kept.clear();
        loot.ground_level = level;
        cues.due.clear();
        den = {};
        den.join(quests());
        den_left = -1;
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
            fight.merc_joins();
        }
    }

    // Back in camp after dying: at the town start with full life. Monsters
    // stay as they are.
    // ponytail: D2 leaves a corpse holding the gear and takes gold; not yet.
    void respawn(std::uint32_t ms) {
        if (level != &scene->town) {
            events.push_back(ev::LevelChanged{ level, false });
            level = &scene->town;
            npc_states = npc_start(*level);
            interact_npc = pick_item = -1;
        }
        take_warp = -1;
        std::tie(player.x, player.y) = level->start.first >= 0 ? level->start : std::pair{ player.x, player.y };
        std::tie(player.x, player.y) = level->nearest_free(player.x, player.y);
        target_x = player.x; target_y = player.y;
        fight.revive(ms);
        d2d::log::info("respawned in the Rogue Encampment");
    }

    // Leaving the level: past its edge, collision and drawing already
    // use the level next to it in the act (Level::near), so the player
    // walks straight on; once they stand outside this map they belong to
    // that level — everything moves by the offset between the two. A
    // click toward a level the layout placed but d2d doesn't build yet
    // (Cold Plains) is logged.
    void cross_level() {
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
            events.push_back(ev::LevelChanged{ level, true });
            level = n.level;
            fight.enter(level);
            loot.enter(level);
            npc_states = npc_start(*level);
            interact_npc = -1;
            d2d::log::info("level: {} at ({:.1f}, {:.1f})", level_name(*level), player.x, player.y);
            return;
        }
    }
    // The Den's monsters after a death (a1q1.cpp FUN_00590260): the last
    // five are counted down in the quest log; the last one clears it, and a
    // player who's earned the reward says so (event 0x23: the class's
    // act1_complete_den, LAB_005900e0).
    // ponytail: counted when the number drops (game.exe: on each death);
    // the quest log isn't drawn, so the count goes to the log.
    void den_count(std::uint32_t ms) {
        if (level->id != d2d::rules::DenQuest::kDen || fight.mon_level != level) return;
        const int left = int(std::ranges::count_if(fight.monsters, &Monster::alive));
        if (den_left >= 0 && left < den_left) {
            const auto k = den.killed(quests(), left);
            if (k == d2d::rules::DenQuest::Kill::few) d2d::log::info("Den of Evil: {}", left == 1 ? "one monster left" : std::format("monsters remaining: {}", left));
            if (k == d2d::rules::DenQuest::Kill::cleared) {
                d2d::log::info("Den of Evil: cleared");
                static constexpr const char* kClass[7] = { "amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin" };
                if (d2d::rules::qbit(quests(), 1, 13) && cc.header.cls < 7)
                    cues.cue(std::format("{}_act1_complete_den", kClass[cc.header.cls]), ms, player.x, player.y);
            }
        }
        den_left = left;
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
    void use_warp() {
        if (take_warp < 0 || std::size_t(take_warp) >= level->warps.size()) return;
        const auto w = level->warps[std::size_t(take_warp)];
        if (std::hypot(w.x + 0.5f - player.x, w.y + 0.5f - player.y) > 2.f) {
            if (!player.walking) take_warp = -1;                                // stopped short
            return;
        }
        take_warp = -1;
        const Level* to = scene->level(w.to);                               // built by now, or waited for
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
        events.push_back(ev::LevelChanged{ level, true });
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
        interact_npc = pick_item = -1;
        if (level->id == d2d::rules::DenQuest::kDen) den.enter_den(quests());
        d2d::log::info("level: {} at ({:.1f}, {:.1f}), through a warp from {}", level_name(*level), px, py, level_name(*from));
    }

    // NPC deals (protocol.hpp): the windows, buying, selling, repairing,
    // identifying, hiring. True when `c` was one.
    bool deal(const Command& c) {
        const auto& t = scene->rules;
        const int clvl = int(cc.stats.get(d2d::d2s::kLevel));
        if (const auto* p = std::get_if<cmd::OpenTrade>(&c)) {
            if (std::size_t(p->npc) >= level->npcs.size()) return true;
            store = p->gamble ? d2d::rules::open_gamble(t, level->npcs[std::size_t(p->npc)].id, clvl) : open_store(*scene, *level, p->npc, rng);
            store.npc = p->npc;
            store.header = cc.header;
            events.push_back(ev::OpenUI{ ev::OpenUI::trade, p->npc });
            return true;
        }
        if (const auto* p = std::get_if<cmd::OpenHire>(&c)) {
            // ponytail: the server's offer count isn't traced; five.
            hire_offers.clear();
            for (int k = 0; k < 5; ++k)
                if (auto o = d2d::rules::merc_offer(t, cc.expansion, 0, cc.header.active_difficulty(), clvl, rng)) hire_offers.push_back(*o);
            events.push_back(ev::OpenUI{ ev::OpenUI::hire, p->npc });
            return true;
        }
        if (const auto* p = std::get_if<cmd::Buy>(&c)) {
            if (store.npc < 0 || p->stock < 0) return true;
            if (store.gamble) d2d::rules::store_gamble(t, store, p->stock, cc.items, cc.stats, rng);
            else d2d::rules::store_buy(t, store, p->stock, cc.items, cc.stats);
            return true;
        }
        if (const auto* p = std::get_if<cmd::Sell>(&c)) {
            if (store.npc < 0) return true;
            if (held && held->id == p->item) { cc.items.push_back(std::move(*held)); held.reset(); }
            const auto it = std::ranges::find(cc.items, p->item, &d2d::d2s::Item::id);
            if (it != cc.items.end()) d2d::rules::store_sell(t, store, std::size_t(it - cc.items.begin()), cc.items, cc.stats);
            return true;
        }
        if (const auto* p = std::get_if<cmd::Repair>(&c)) {
            if (store.npc < 0) return true;
            if (p->item < 0) { d2d::rules::store_repair_all(t, store, cc.items, cc.stats); return true; }
            const auto it = std::ranges::find(cc.items, p->item, &d2d::d2s::Item::id);
            if (it != cc.items.end()) d2d::rules::store_repair(t, store, *it, cc.stats);
            return true;
        }
        if (std::holds_alternative<cmd::Identify>(c)) { d2d::rules::identify_all(cc.items); return true; }
        if (const auto* p = std::get_if<cmd::Hire>(&c)) {
            if (p->offer >= 0 && std::size_t(p->offer) < hire_offers.size() && d2d::rules::hire(hire_offers[std::size_t(p->offer)], cc.header, cc.stats))
                spawn_merc();
            return true;
        }
        if (std::holds_alternative<cmd::CloseTrade>(c)) { store = {}; return true; }
        // Akara's reset (the 0x38 handler for her, hcIdx 0x94): while quest
        // 41 (the Den of Evil's reward) is open, stats and skills go back,
        // then it's used (FUN_0058fd50: bit 0 on, 1 off).
        if (const auto* p = std::get_if<cmd::Respec>(&c)) {
            using d2d::rules::qbit;
            if (std::size_t(p->npc) >= level->npcs.size() || level->npcs[std::size_t(p->npc)].hc_idx != d2d::rules::DenQuest::kAkara
                || !qbit(quests(), 41, 1)) return true;
            const auto cls = std::size_t(std::clamp<int>(cc.header.cls, 0, 6));
            const auto& b = scene->class_start[cls];
            d2d::rules::respec(cc.stats, { b.str, b.ene, b.dex, b.vit }, scene->class_gains[cls]);
            d2d::rules::qset(quests(), 41, 0);
            d2d::rules::qset(quests(), 41, 1, false);
            d2d::log::info("Akara reset the stat and skill points");
            return true;
        }
        if (const auto* p = std::get_if<cmd::Run>(&c)) { running = p->on; return true; }
        if (const auto* p = std::get_if<cmd::Chat>(&c)) { talking = { p->npc, -1, -1 }; return true; }
        if (const auto* p = std::get_if<cmd::QuestMessage>(&c)) {   // only what that NPC has to say
            if (std::size_t(p->npc) >= level->npcs.size()) return true;
            const int hc = level->npcs[std::size_t(p->npc)].hc_idx;
            if (!std::ranges::contains(den.talk(quests(), hc), p->string, &d2d::rules::QuestMsg::string)) return true;
            if (den.said(quests(), hc, p->string)) {
                ++cc.stats.v[d2d::d2s::kSkillPts];
                d2d::log::info("Den of Evil: Akara's reward, a skill point");
            }
            return true;
        }
        return false;
    }

    // A command from the player, checked and applied. A busy player's
    // are dropped (game.exe's dispatcher, FUN_0054d750 / FUN_0057eec0).
    void apply(const Command& c, std::uint32_t ms) {
        // The character's own: through whatever the player's doing.
        if (const auto* p = std::get_if<cmd::StatPoint>(&c)) {
            if (p->stat < 0 || p->stat > 3 || p->count < 1) return;
            d2d::rules::spend_stat_points(cc.stats, p->stat, std::min<int>(p->count, int(cc.stats.get(d2d::d2s::kStatPts))),
                                          scene->class_gains[std::size_t(cc.header.cls)]);
            return;
        }
        if (const auto* p = std::get_if<cmd::SkillPoint>(&c)) {
            if (cc.stats.get(d2d::d2s::kSkillPts) > 0
                && d2d::rules::can_learn(scene->rules, cc.header.cls, p->skill, cc.stats.skills, int(cc.stats.get(d2d::d2s::kLevel))))
                d2d::rules::learn_skill(scene->rules, cc.header.cls, p->skill, cc.stats.skills, cc.stats);
            return;
        }
        if (const auto* p = std::get_if<cmd::SelectSkill>(&c)) {
            (p->left ? cc.header.left_skill : cc.header.right_skill) = std::uint32_t(p->skill);
            if (!p->left) if (const auto* s = scene->skills.get(p->skill)) fight.aura = s->aura ? p->skill : 0;
            return;
        }
        if (const auto* p = std::get_if<cmd::UseBelt>(&c)) {
            if (p->slot >= 0 && p->slot < 4 && !fight.dead()) fight.drink(p->slot, ms);
            return;
        }
        // The cursor (cursor.hpp works out which from a click).
        const auto& t = scene->rules;
        const int cls = cc.header.cls;
        if (const auto* p = std::get_if<cmd::ToCursor>(&c)) {
            const auto it = std::ranges::find(cc.items, p->item, &d2d::d2s::Item::id);
            if (!held && it != cc.items.end()) d2d::rules::pick_up(cc.items, held, std::size_t(it - cc.items.begin()));
            return;
        }
        if (const auto* p = std::get_if<cmd::ToGrid>(&c)) {
            const auto* L = p->panel == 1 ? &scene->inv_layout[std::size_t(cls)] : p->panel == 4 ? &scene->cube_layout
                          : p->panel == 5 ? &scene->stash_layout[cc.expansion ? 1 : 0] : nullptr;
            if (held && L) d2d::rules::put_in_grid(t, cc.items, held, p->panel, L->cols, L->rows, p->col, p->row);
            return;
        }
        if (const auto* p = std::get_if<cmd::ToBody>(&c)) {
            if (held && p->slot >= 1 && p->slot <= 10) d2d::rules::equip(t, cc.items, held, p->slot, wearer(cls, cc.items, cc.stats));
            return;
        }
        if (const auto* p = std::get_if<cmd::ToBelt>(&c)) {
            const auto& B = scene->belts[std::size_t(belt_index(*scene, cc.items))];
            if (held && p->box >= 0 && p->box < B.boxes) d2d::rules::put_in_belt(t, cc.items, held, p->box, B.boxes);
            return;
        }
        if (deal(c)) return;
        if (fight.pmode >= 0) return;
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
            const int gi = loot.index_of(p->item);
            if (gi < 0) return;
            walk_to(loot.ground[std::size_t(gi)].x, loot.ground[std::size_t(gi)].y, true);
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

    // One step of the game: the player's commands, then the world. The
    // player walks a walk_path to the target (and operates or talks on
    // arrival); NPCs patrol; the merc follows; the monsters and missiles
    // (Fight::world); crossing into the next level or through a warp;
    // potions and regeneration.
    const Level* wanted_near = nullptr;               // whose neighbours were last asked for
    void tick(const std::vector<Command>& cmds, std::uint32_t ms, std::uint32_t last_ms) {
        const float dt = float(ms - last_ms) / 1000.f;
        now = ms;
        // Levels: finished builds come in; the ones next to the player's
        // start building when it changes (GameData::level).
        scene->poll_levels();
        if (level != wanted_near) { want_nearby(*scene, *level); wanted_near = level; }
        item_ids();
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
        // Dead: the death plays out, then Resurrect respawns in camp; a
        // swing or a flinch holds the player in place until it ends, and
        // an attack goes on while the button's held (the left skill sent
        // again).
        const bool button = std::ranges::any_of(cmds, [](const Command& c) {
            const auto* k = std::get_if<cmd::UseSkill>(&c);
            return std::holds_alternative<cmd::Move>(c) || (k && k->left);
        });
        const bool resurrect = std::ranges::any_of(cmds, [](const Command& c) { return std::holds_alternative<cmd::Resurrect>(c); });
        if (fight.player_modes(button, ms, dt) && resurrect) respawn(ms);
        if (!fight.dead()) {
        const bool busy = fight.pmode >= 0;
        for (const auto& c : cmds) if (!std::holds_alternative<cmd::Resurrect>(c)) apply(c, ms);
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
                    events.push_back(ev::OpenUI{ ev::OpenUI::stash, interact_npc });
                } else if (o.operate_fn == 23) {
                    // Touching it activates it (the town's: wp 0).
                    // ponytail: town only; a wilderness waypoint would need its level.
                    cc.header.waypoints[std::size_t(cc.header.active_difficulty())][0] |= 1;
                    events.push_back(ev::OpenUI{ ev::OpenUI::waypoint, interact_npc });
                } else {
                    if (d2d::rules::is_healer(o.hc_idx)) d2d::rules::heal(cc.stats);
                    // In Hell, a Den of Evil done before the reset existed
                    // opens it on meeting Akara (FUN_0058fd20: quest 41 bits
                    // 13 and 1).
                    if (o.hc_idx == d2d::rules::DenQuest::kAkara && cc.header.active_difficulty() == 2) {
                        auto& f = quests();
                        using d2d::rules::qbit;
                        if (qbit(f, 1, 0) && !qbit(f, 41, 1) && !qbit(f, 41, 0)) { d2d::rules::qset(f, 41, 13); d2d::rules::qset(f, 41, 1); }
                    }
                    events.push_back(ev::OpenUI{ ev::OpenUI::talk, interact_npc, den.talk(quests(), o.hc_idx) });
                }
                player.walking = false; interact_npc = -1;
            } else if (!player.walking) {
                interact_npc = -1;                         // blocked on the way
            } else {
                target_x = ox; target_y = oy;              // follow a walking NPC
            }
        }
        if (pick_item >= 0 && loot.index_of(pick_item) < 0) pick_item = -1;   // someone took it
        if (pick_item >= 0 && !busy) {
            const auto gi = std::size_t(loot.index_of(pick_item));
            const auto& g = loot.ground[gi];
            if (std::hypot(g.x - player.x, g.y - player.y) <= 1.f) {
                loot.take(gi);
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
        npc_patrol(*level, npc_states, talking, ms, dt, crowd);
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            npc_states[i].alert = den.alert(quests(), level->npcs[i].hc_idx);
        fight.world(in_moor, ms, dt, crowd);
        den_count(ms);
        use_warp();
        }
        cross_level();
        if (!fight.dead()) fight.apply_regen(ms, last_ms);
    }

};

}  // namespace
