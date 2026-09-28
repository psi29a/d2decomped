// The View on the wire (the S -> C side, docs/design/multiplayer.md): what
// a client draws, as bytes. Everything that points into the Scene goes as
// its key there (the level's id, a missile's name, the merc's hireling
// type, a fire's index) — the Scene is the same data on every machine.
// Decoded animation modes are interned, so the views stay valid.
// ponytail: whole views every tick (no deltas); a unit's look as strings.
#pragma once

#include "ai.hpp"
#include "gamedata.hpp"
#include "log.hpp"
#include "loot.hpp"
#include "protocol.hpp"
#include "world.hpp"

#include <d2s_write.hpp>

#include <unordered_set>

namespace d2d::game {

namespace wire {
// A stable home for decoded mode names (the units keep string_views).
inline std::string_view intern(std::string s) {
    static std::unordered_set<std::string> pool;
    return *pool.insert(std::move(s)).first;
}
inline void npc(Out& o, const Npc& n) {
    o.str(n.root).str(n.code).str(n.base_w).str(n.name).i32(n.light).i32(n.overlay_class).i32(n.colour);
    for (const auto& c : n.comp) o.str(c);
}
inline Npc npc(In& in) {
    Npc n;
    n.root = in.str(); n.code = in.str(); n.base_w = in.str(); n.name = in.str();
    n.light = in.get<std::int32_t>(); n.overlay_class = in.get<std::int32_t>(); n.colour = in.get<std::int32_t>();
    for (auto& c : n.comp) c = in.str();
    return n;
}
inline void unit(Out& o, const UnitState& u) {
    o.f32(u.x).f32(u.y).u8(u.dir).u8(u.walking).u8(u.hidden).u8(u.alert).u32(u.mode_ms).str(u.mode);
}
inline UnitState unit(In& in) {
    UnitState u;
    u.x = in.get<float>(); u.y = in.get<float>();
    u.dir = in.get<std::uint8_t>(); u.walking = in.get<std::uint8_t>(); u.hidden = in.get<std::uint8_t>(); u.alert = in.get<std::uint8_t>();
    u.mode_ms = in.get<std::uint32_t>();
    u.mode = intern(in.str());
    return u;
}
// Items as their save form (d2s_write.hpp), then their unit ids.
inline void items(Out& o, const std::vector<d2d::d2s::Item>& list, const d2d::d2s::ItemTables& t) {
    d2d::d2s::detail::BitWriter w;
    for (const auto& it : list) d2d::d2s::detail::write_item(w, it, t);
    o.u16(int(list.size())).u32(std::uint32_t(w.out.size()));
    for (const auto b : w.out) o.u8(int(b));
    for (const auto& it : list) o.i32(it.id);
}
inline std::vector<d2d::d2s::Item> items(In& in, const d2d::d2s::ItemTables& t) {
    const int n = in.get<std::uint16_t>();
    const std::size_t bytes = in.get<std::uint32_t>();
    std::vector<d2d::d2s::Item> out;
    if (!in.ok || in.at + bytes > in.b.size()) { in.ok = false; return out; }
    try {
        d2d::d2s::detail::Bits bs{ std::as_bytes(in.b.subspan(in.at, bytes)), 0 };
        for (int i = 0; i < n; ++i) out.push_back(d2d::d2s::detail::item(bs, t));
    } catch (const std::exception&) { in.ok = false; return out; }
    in.at += bytes;
    for (auto& it : out) it.id = in.get<std::int32_t>();
    return out;
}
}  // namespace wire

// The Scene's level with Levels.txt id `id`.
inline const Level* level_of(const GameData& s, int id) { return s.level(id); }

// What one client was last sent (the server keeps one per client): each
// section's bytes, each monster's look and state, each NPC's state. A
// View goes out as what changed since; after a level change, or when the
// client lost track, everything (a keyframe).
struct ViewEncoder {
    int level = -2;
    std::array<std::vector<std::uint8_t>, 8> section;
    std::unordered_map<int, std::vector<std::uint8_t>> look, state;
    std::vector<std::vector<std::uint8_t>> npc;
    void reset() { *this = {}; }
};

namespace wire {
// A section: 0 when it's what the client has, else 1, its length, its bytes.
inline void section(Out& o, std::vector<std::uint8_t>& last, std::vector<std::uint8_t> now) {
    if (now == last) { o.u8(0); return; }
    o.u8(1).u32(std::uint32_t(now.size()));
    o.b.insert(o.b.end(), now.begin(), now.end());
    last = std::move(now);
}
inline void monster_look(Out& o, const Monster& m) {
    o.i32(m.type).i32(m.st.hp).i32(m.st.level).u8(int(m.boss)).u8(int(m.mods.size()));
    for (const int md : m.mods) o.u8(md);
    npc(o, m.npc);
}
inline void monster_look(In& in, Monster& m) {
    m.type = in.get<std::int32_t>(); m.st.hp = in.get<std::int32_t>(); m.st.level = in.get<std::int32_t>();
    m.boss = d2d::rules::Boss(in.get<std::uint8_t>());
    m.mods.clear();
    for (int k = in.get<std::uint8_t>(); k > 0 && in.ok; --k) m.mods.push_back(in.get<std::uint8_t>());
    m.npc = npc(in);
}
inline void monster_state(Out& o, const Monster& m) {
    o.i32(m.hp).u8(m.corpse_used).str(m.mode);
    unit(o, m.u);
    // What its states draw from (town.hpp monster_states): the timers change only as a state starts.
    o.u32(m.poison_until).u32(m.chill_until).u32(m.stun_until).i32(m.curse.skill).u32(m.curse.until).i32(m.cry.skill).u32(m.cry.until).i32(m.aura).u8(m.in_aura);
}
inline void monster_state(In& in, Monster& m) {
    m.hp = in.get<std::int32_t>(); m.corpse_used = in.get<std::uint8_t>(); m.mode = intern(in.str());
    m.u = unit(in);
    m.poison_until = in.get<std::uint32_t>(); m.chill_until = in.get<std::uint32_t>(); m.stun_until = in.get<std::uint32_t>();
    m.curse.skill = in.get<std::int32_t>(); m.curse.until = in.get<std::uint32_t>();
    m.cry.skill = in.get<std::int32_t>(); m.cry.until = in.get<std::uint32_t>(); m.aura = in.get<std::int32_t>();
    m.in_aura = in.get<std::uint8_t>() != 0;
}
}  // namespace wire

// The View as what changed since `enc` last sent (encode_view) and, on
// the client, that change applied to its last View (apply_view).
inline std::vector<std::uint8_t> encode_view(const GameData& s, const View& v, ViewEncoder& enc) {
    wire::Out o;
    o.u8(0x70);                                              // our own id: game.exe has no packet for a whole view
    const int lvl = v.level ? v.level->id : -1;
    const bool key = enc.level != lvl;
    if (key) { enc.reset(); enc.level = lvl; }
    o.u8(key).i32(lvl);
    // 0: the player, what it wears and does, its merc, target, aura, boost.
    {
        wire::Out c;
        wire::unit(c, v.player);
        c.u8(v.running).u8(v.dead).i32(v.pmode).f32(v.prate).u32(v.seq_frame_ms).u8(v.seq_loop);
        c.u16(int(v.seq.size()));
        for (const auto& f : v.seq) c.u8(f.mode).u8(f.frame).u8(f.event);
        for (const auto g : v.gfx) c.u8(g);
        std::string merc_type;
        if (v.merc) for (const auto& [k, m] : s.mercs) if (&m.npc == v.merc->npc) merc_type = std::to_string(k);
        c.u8(v.merc && !merc_type.empty());
        if (v.merc && !merc_type.empty()) { c.str(merc_type).str(v.merc->mode); wire::unit(c, v.merc->u); }
        c.i32(v.attack).i32(v.attack_skill).i32(v.aura).i32(v.day.phase).i32(v.day.time).u8(v.den_cleared).i32(v.light_bonus).i32(v.den_state).i32(v.den_log).i32(v.den_left);
        c.u16(int(v.boost.size()));
        for (const auto& [st, val] : v.boost) c.i32(st).i32(val);
        c.i32(v.gold_lost);
        c.u16(int(v.buffs.size()));
        for (const int k : v.buffs) c.i32(k);
        wire::section(o, enc.section[0], std::move(c.b));
    }
    // 1: pets, 2: missiles.
    {
        wire::Out c;
        c.u16(int(v.pets.size()));
        for (const auto& p : v.pets) { wire::npc(c, p.npc); wire::unit(c, p.u); c.str(p.mode); }
        wire::section(o, enc.section[1], std::move(c.b));
    }
    {
        wire::Out c;
        c.u16(int(v.missiles.size()));
        for (const auto& m : v.missiles) c.str(m.info->name).f32(m.x).f32(m.y).u8(m.dir).u32(m.born);
        wire::section(o, enc.section[2], std::move(c.b));
    }
    // 3: the ground, 4: fires.
    {
        wire::Out c;
        c.u16(int(v.ground.size()));
        for (const auto& g : v.ground)
            c.i32(g.id).str(g.item.code).i32(g.gold).f32(g.x).f32(g.y).u32(g.ms).str(g.label).u8(g.rgb[0]).u8(g.rgb[1]).u8(g.rgb[2]);
        wire::section(o, enc.section[3], std::move(c.b));
    }
    {
        wire::Out c;
        c.u16(int(v.fires.size()));
        for (const auto& f : v.fires) c.f32(f.x).f32(f.y).u8(f.npc == &s.trap_fires[1]);
        c.u16(int(v.portals.size()));
        for (const auto& p : v.portals) c.f32(p.x).f32(p.y).i32(p.to).u32(p.born).u8(p.which);
        c.u16(int(v.corpses.size()));
        for (const auto& k : v.corpses) {
            c.f32(k.x).f32(k.y).u8(k.dir).u8(k.which);
            for (const auto g : k.gfx) c.u8(g);
        }
        wire::section(o, enc.section[4], std::move(c.b));
    }
    // The owner's character: 5 its header (in its save form), 7 its stats
    // and skills (they change with every tick of regeneration), 6 its items,
    // the item in hand and the store's stock; then the hire list.
    {
        wire::Out c;
        std::vector<std::byte> save;
        if (s.item_tables && v.has_character)
            try { save = d2d::d2s::write_save({}, v.header, {}, {}, *s.item_tables); }
            catch (const std::exception& e) { d2d::log::warn("the character didn't encode: {}", e.what()); }
        c.u32(std::uint32_t(save.size()));
        for (const auto b : save) c.u8(int(b));
        wire::section(o, enc.section[5], std::move(c.b));
    }
    {
        wire::Out c;
        for (const auto x : v.stats.v) c.u32(std::uint32_t(x));   // the save's widths: 32 bits at most
        for (const auto k : v.stats.skills) c.u8(k);
        wire::section(o, enc.section[7], std::move(c.b));
    }
    {
        wire::Out c;
        c.u8(s.item_tables && v.has_character);
        if (s.item_tables && v.has_character) {
            wire::items(c, v.items, *s.item_tables);
            c.u8(v.held.has_value());
            if (v.held) wire::items(c, { *v.held }, *s.item_tables);
            c.u8(v.store.has_value());
            if (v.store) {
                const auto& st = *v.store;
                c.i32(st.npc).i32(st.vendor).i32(st.hc_idx).str(st.npc_id).u8(st.gamble);
                c.u16(int(st.perm.size()));
                for (const auto& p : st.perm) c.str(p);
                for (const auto& tab : st.tabs) wire::items(c, tab, *s.item_tables);
            }
        }
        wire::section(o, enc.section[6], std::move(c.b));
    }
    {
        wire::Out c;
        c.u16(int(v.hire_offers.size()));
        for (const auto& h : v.hire_offers)
            c.i32(h.id).i32(h.level).i32(h.life).i32(h.str).i32(h.dex).i32(h.cost).i32(h.def).i32(h.dmg_min).i32(h.dmg_max)
             .u32(h.exp).u32(h.seed).i32(h.name);
        o.u8(1).u32(std::uint32_t(c.b.size()));                  // small: always
        o.b.insert(o.b.end(), c.b.begin(), c.b.end());
    }
    // Monsters: the looks and states that changed, and those gone.
    {
        std::vector<std::pair<int, std::vector<std::uint8_t>>> looks, states;
        std::unordered_set<int> here;
        for (const auto& m : v.monsters) {
            here.insert(m.id);
            wire::Out l, t;
            wire::monster_look(l, m);
            wire::monster_state(t, m);
            if (auto& was = enc.look[m.id]; was != l.b) { looks.emplace_back(m.id, l.b); was = std::move(l.b); }
            if (auto& was = enc.state[m.id]; was != t.b) { states.emplace_back(m.id, t.b); was = std::move(t.b); }
        }
        std::vector<int> gone;
        for (auto it = enc.look.begin(); it != enc.look.end();)
            if (!here.contains(it->first)) { gone.push_back(it->first); enc.state.erase(it->first); it = enc.look.erase(it); }
            else ++it;
        for (const auto* list : { &looks, &states }) {
            o.u16(int(list->size()));
            for (const auto& [id, b] : *list) { o.i32(id); o.b.insert(o.b.end(), b.begin(), b.end()); }
        }
        o.u16(int(gone.size()));
        for (const int id : gone) o.i32(id);
    }
    // NPCs as they patrol: those that changed.
    {
        enc.npc.resize(v.npc_states.size());
        std::vector<std::size_t> changed;
        std::vector<std::vector<std::uint8_t>> now(v.npc_states.size());
        for (std::size_t i = 0; i < v.npc_states.size(); ++i) {
            wire::Out u;
            wire::unit(u, v.npc_states[i]);
            if (u.b != enc.npc[i]) { changed.push_back(i); enc.npc[i] = u.b; }
            now[i] = std::move(u.b);
        }
        o.u16(int(v.npc_states.size())).u16(int(changed.size()));
        for (const auto i : changed) { o.u16(int(i)); o.b.insert(o.b.end(), now[i].begin(), now[i].end()); }
    }
    // What happened: events and sounds (never repeated).
    o.u16(int(v.events.size()));
    for (const auto& e : v.events) {
        if (const auto* lc = std::get_if<ev::LevelChanged>(&e)) o.u8(0).i32(lc->from ? lc->from->id : -1).u8(lc->keep_map);
        else {
            const auto& ui = std::get<ev::OpenUI>(e);
            o.u8(1).u8(int(ui.kind)).i32(ui.npc).u16(int(ui.quest.size()));
            for (const auto& q : ui.quest) o.i32(q.string).u8(q.greet);
        }
    }
    o.u16(int(v.sounds.size()));
    for (const auto& c : v.sounds) o.u32(c.at).i32(c.sound).f32(c.x).f32(c.y);
    return o.b;
}

// The client's side: a View message applied to its last View `v`. False for
// anything malformed (the client asks for a keyframe).
inline bool apply_view(const GameData& s, std::span<const std::uint8_t> b, View& v) {
    if (b.empty() || b[0] != 0x70) return false;
    wire::In in{ b };
    auto u8 = [&] { return int(in.get<std::uint8_t>()); };
    auto u16 = [&] { return int(in.get<std::uint16_t>()); };
    auto i32 = [&] { return int(in.get<std::int32_t>()); };
    auto f32 = [&] { return in.get<float>(); };
    auto u32 = [&] { return in.get<std::uint32_t>(); };
    if (u8()) { v.monsters.clear(); v.npc_states.clear(); }   // a keyframe
    v.level = level_of(s, i32());
    // A section's body, or nothing when it's unchanged.
    auto body = [&](auto&& read) {
        if (!u8()) return;
        const std::size_t n = u32(), end = in.at + n;
        if (!in.ok || end > b.size()) { in.ok = false; return; }
        read();
        if (in.at != end) in.ok = false;
    };
    body([&] {
        v.player = wire::unit(in);
        v.running = u8(); v.dead = u8(); v.pmode = i32(); v.prate = f32(); v.seq_frame_ms = u32(); v.seq_loop = u8();
        v.seq.clear();
        for (int n = u16(); n > 0 && in.ok; --n) {
            const auto m = std::uint8_t(u8()), f = std::uint8_t(u8()), e = std::uint8_t(u8());
            v.seq.push_back({ m, f, e });
        }
        for (auto& g : v.gfx) g = std::uint8_t(u8());
        v.merc.reset();
        if (u8()) {
            const auto type = in.str();
            const auto mode = wire::intern(in.str());
            const auto u = wire::unit(in);
            if (const auto it = s.mercs.find(std::atoi(type.c_str())); it != s.mercs.end()) v.merc = View::Merc{ u, &it->second.npc, mode };
        }
        v.attack = i32(); v.attack_skill = i32(); v.aura = i32(); v.day.phase = i32(); v.day.time = i32(); v.den_cleared = u8() != 0; v.light_bonus = i32(); v.den_state = i32(); v.den_log = i32(); v.den_left = i32();
        v.boost.clear();
        for (int n = u16(); n > 0 && in.ok; --n) { const int st = i32(); v.boost.emplace_back(st, i32()); }
        v.gold_lost = i32();
        v.buffs.clear();
        for (int n = u16(); n > 0 && in.ok; --n) v.buffs.push_back(i32());
    });
    body([&] {
        v.pets.clear();
        for (int n = u16(); n > 0 && in.ok; --n) {
            auto npc = wire::npc(in);
            const auto u = wire::unit(in);
            v.pets.push_back({ std::move(npc), u, wire::intern(in.str()) });
        }
    });
    body([&] {
        v.missiles.clear();
        for (int n = u16(); n > 0 && in.ok; --n) {
            const auto name = in.str();
            const float x = f32(), y = f32();
            const int dir = u8();
            const auto born = u32();
            if (const auto it = s.missiles.find(name); it != s.missiles.end()) v.missiles.push_back({ &it->second, x, y, dir, born });
        }
    });
    body([&] {
        v.ground.clear();
        for (int n = u16(); n > 0 && in.ok; --n) {
            Loot::GroundItem g;
            g.id = i32(); g.item.code = in.str(); g.gold = i32(); g.x = f32(); g.y = f32(); g.ms = u32(); g.label = in.str();
            for (auto& c : g.rgb) c = std::uint8_t(u8());
            v.ground.push_back(std::move(g));
        }
    });
    body([&] {
        v.fires.clear();
        for (int n = u16(); n > 0 && in.ok; --n) {
            const float x = f32(), y = f32();
            v.fires.push_back({ x, y, &s.trap_fires[u8() ? 1 : 0] });
        }
        v.portals.clear();
        for (int n = u16(); n > 0 && in.ok; --n) {
            View::Portal p{};
            p.x = f32(); p.y = f32(); p.to = i32(); p.born = u32(); p.which = u8();
            v.portals.push_back(p);
        }
        v.corpses.clear();
        for (int n = u16(); n > 0 && in.ok; --n) {
            View::Corpse k{};
            k.x = f32(); k.y = f32(); k.dir = u8(); k.which = u8();
            for (auto& g : k.gfx) g = std::uint8_t(u8());
            v.corpses.push_back(k);
        }
    });
    body([&] {
        const std::size_t n = u32();
        if (n == 0 || !s.item_tables) { v.has_character = false; in.at += n; return; }
        if (in.at + n > b.size()) { in.ok = false; return; }
        try { v.header = d2d::d2s::parse_header(std::as_bytes(b.subspan(in.at, n))); }
        catch (const std::exception&) { in.ok = false; return; }
        in.at += n;
        v.has_character = true;
    });
    body([&] {
        for (auto& x : v.stats.v) x = u32();
        for (auto& k : v.stats.skills) k = std::uint8_t(u8());
    });
    body([&] {
        if (!u8() || !s.item_tables) return;
        v.items = wire::items(in, *s.item_tables);
        v.held.reset();
        if (u8()) if (auto h = wire::items(in, *s.item_tables); h.size() == 1) v.held = std::move(h[0]);
        v.store.reset();
        if (u8()) {
            Store st;
            st.npc = i32(); st.vendor = i32(); st.hc_idx = i32(); st.npc_id = in.str(); st.gamble = u8();
            for (int k = u16(); k > 0 && in.ok; --k) st.perm.push_back(in.str());
            for (auto& tab : st.tabs) tab = wire::items(in, *s.item_tables);
            st.header = v.header;
            v.store = std::move(st);
        }
    });
    body([&] {
        v.hire_offers.clear();
        for (int k = u16(); k > 0 && in.ok; --k) {
            d2d::rules::MercOffer h;
            h.id = i32(); h.level = i32(); h.life = i32(); h.str = i32(); h.dex = i32(); h.cost = i32(); h.def = i32();
            h.dmg_min = i32(); h.dmg_max = i32(); h.exp = u32(); h.seed = u32(); h.name = i32();
            v.hire_offers.push_back(h);
        }
    });
    // Monsters: looks (new ones too), states, the gone.
    auto mon = [&](int id) -> Monster& {
        const int i = v.monster(id);
        if (i >= 0) return v.monsters[std::size_t(i)];
        v.monsters.emplace_back().id = id;
        return v.monsters.back();
    };
    for (int n = u16(); n > 0 && in.ok; --n) {
        auto& m = mon(i32());
        wire::monster_look(in, m);
        if (m.type < 0 || std::size_t(m.type) >= s.monsters.types.size()) return false;
    }
    for (int n = u16(); n > 0 && in.ok; --n) wire::monster_state(in, mon(i32()));
    for (int n = u16(); n > 0 && in.ok; --n) {
        const int i = v.monster(i32());
        if (i >= 0) v.monsters.erase(v.monsters.begin() + i);
    }
    // NPCs.
    v.npc_states.resize(std::size_t(u16()));
    for (int n = u16(); n > 0 && in.ok; --n) {
        const auto i = std::size_t(u16());
        auto u = wire::unit(in);
        if (i < v.npc_states.size()) v.npc_states[i] = u;
    }
    v.events.clear();
    for (int k = u16(); k > 0 && in.ok; --k) {
        if (u8() == 0) {
            const Level* from = level_of(s, i32());
            const bool keep = u8();
            if (from) v.events.push_back(ev::LevelChanged{ from, keep });
        } else {
            const auto kind = ev::OpenUI::Kind(u8());
            ev::OpenUI ui{ kind, i32() };
            for (int q = u16(); q > 0 && in.ok; --q) { const int str = i32(); ui.quest.push_back({ str, u8() != 0 }); }
            v.events.push_back(std::move(ui));
        }
    }
    v.sounds.clear();
    for (int k = u16(); k > 0 && in.ok; --k) {
        const auto at = u32();
        const int snd = i32();
        const float x = f32(), y = f32();
        v.sounds.push_back({ at, snd, x, y });
    }
    return in.ok && in.at == b.size() && v.level;
}

}  // namespace d2d::game
