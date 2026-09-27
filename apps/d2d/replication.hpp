// The View on the wire (the S -> C side, docs/design/multiplayer.md): what
// a client draws, as bytes. Everything that points into the Scene goes as
// its key there (the level's id, a missile's name, the merc's hireling
// type, a fire's index) — the Scene is the same data on every machine.
// Decoded animation modes are interned, so the views stay valid.
// ponytail: whole views every tick (no deltas); a unit's look as strings.
#pragma once

#include "server.hpp"

#include <d2s_write.hpp>

#include <unordered_set>

namespace {

namespace wire {
// A stable home for decoded mode names (the units keep string_views).
inline std::string_view intern(std::string s) {
    static std::unordered_set<std::string> pool;
    return *pool.insert(std::move(s)).first;
}
inline void npc(Out& o, const Npc& n) {
    o.str(n.root).str(n.code).str(n.base_w).str(n.name);
    for (const auto& c : n.comp) o.str(c);
}
inline Npc npc(In& in) {
    Npc n;
    n.root = in.str(); n.code = in.str(); n.base_w = in.str(); n.name = in.str();
    for (auto& c : n.comp) c = in.str();
    return n;
}
inline void unit(Out& o, const UnitState& u) {
    o.f32(u.x).f32(u.y).u8(u.dir).u8(u.walking).u8(u.hidden).u32(u.mode_ms).str(u.mode);
}
inline UnitState unit(In& in) {
    UnitState u;
    u.x = in.get<float>(); u.y = in.get<float>();
    u.dir = in.get<std::uint8_t>(); u.walking = in.get<std::uint8_t>(); u.hidden = in.get<std::uint8_t>();
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
inline const Level* level_of(const Scene& s, int id) {
    for (const Level* l : { &s.town, &s.moor, &s.den }) if (l->id == id) return l;
    return nullptr;
}

inline std::vector<std::uint8_t> encode_view(const Scene& s, const View& v) {
    wire::Out o;
    o.u8(0x70);                                              // our own id: game.exe has no packet for a whole view
    o.i32(v.level ? v.level->id : -1);
    wire::unit(o, v.player);
    o.u8(v.running).u8(v.dead).i32(v.pmode).f32(v.prate).u32(v.seq_frame_ms).u8(v.seq_loop);
    o.u16(int(v.seq.size()));
    for (const auto& f : v.seq) o.u8(f.mode).u8(f.frame).u8(f.event);
    for (const auto g : v.gfx) o.u8(g);
    std::string merc_type;
    if (v.merc) for (const auto& [k, m] : s.mercs) if (&m.npc == v.merc->npc) merc_type = std::to_string(k);
    o.u8(v.merc && !merc_type.empty());
    if (v.merc && !merc_type.empty()) { o.str(merc_type).str(v.merc->mode); wire::unit(o, v.merc->u); }
    o.u16(int(v.pets.size()));
    for (const auto& p : v.pets) { wire::npc(o, p.npc); wire::unit(o, p.u); o.str(p.mode); }
    o.u16(int(v.monsters.size()));
    for (const auto& m : v.monsters) {
        o.i32(m.id).i32(m.type).i32(m.hp).i32(m.st.hp).i32(m.st.level).u8(int(m.boss)).u8(m.corpse_used).str(m.mode);
        o.u8(int(m.mods.size()));
        for (const int md : m.mods) o.u8(md);
        wire::npc(o, m.npc);
        wire::unit(o, m.u);
    }
    o.u16(int(v.missiles.size()));
    for (const auto& m : v.missiles) o.str(m.info->name).f32(m.x).f32(m.y).u8(m.dir).u32(m.born);
    o.i32(v.attack).i32(v.attack_skill);
    o.u16(int(v.ground.size()));
    for (const auto& g : v.ground)
        o.i32(g.id).str(g.item.code).i32(g.gold).f32(g.x).f32(g.y).u32(g.ms).str(g.label).u8(g.rgb[0]).u8(g.rgb[1]).u8(g.rgb[2]);
    o.u16(int(v.fires.size()));
    for (const auto& f : v.fires) o.f32(f.x).f32(f.y).u8(f.npc == &s.trap_fires[1]);
    o.u16(int(v.npc_states.size()));
    for (const auto& u : v.npc_states) wire::unit(o, u);
    o.u16(int(v.boost.size()));
    for (const auto& [st, val] : v.boost) o.i32(st).i32(val);
    o.i32(v.aura);
    // The owner's character: its save form (header, stats, skills), then its
    // items and the item in hand; the store's stock; the hire list.
    std::vector<std::byte> save;
    if (s.item_tables)
        try { save = d2d::d2s::write_save({}, v.header, v.stats, {}, *s.item_tables); }
        catch (const std::exception& e) { d2d::log::warn("the character didn't encode: {}", e.what()); }
    o.u32(std::uint32_t(save.size()));
    for (const auto b : save) o.u8(int(b));
    if (!save.empty()) {
        wire::items(o, v.items, *s.item_tables);
        o.u8(v.held.has_value());
        if (v.held) wire::items(o, { *v.held }, *s.item_tables);
        o.u8(v.store.has_value());
        if (v.store) {
            const auto& st = *v.store;
            o.i32(st.npc).i32(st.vendor).i32(st.hc_idx).str(st.npc_id).u8(st.gamble);
            o.u16(int(st.perm.size()));
            for (const auto& p : st.perm) o.str(p);
            for (const auto& tab : st.tabs) wire::items(o, tab, *s.item_tables);
        }
    }
    o.u16(int(v.hire_offers.size()));
    for (const auto& h : v.hire_offers)
        o.i32(h.id).i32(h.level).i32(h.life).i32(h.str).i32(h.dex).i32(h.cost).i32(h.def).i32(h.dmg_min).i32(h.dmg_max)
         .u32(h.exp).u32(h.seed).i32(h.name);
    return o.b;
}

// A View back from the wire; nullopt for anything malformed.
inline std::optional<View> decode_view(const Scene& s, std::span<const std::uint8_t> b) {
    if (b.empty() || b[0] != 0x70) return std::nullopt;
    wire::In in{ b };
    View v;
    auto u8 = [&] { return int(in.get<std::uint8_t>()); };
    auto u16 = [&] { return int(in.get<std::uint16_t>()); };
    auto i32 = [&] { return int(in.get<std::int32_t>()); };
    auto f32 = [&] { return in.get<float>(); };
    auto u32 = [&] { return in.get<std::uint32_t>(); };
    v.level = level_of(s, i32());
    v.player = wire::unit(in);
    v.running = u8(); v.dead = u8(); v.pmode = i32(); v.prate = f32(); v.seq_frame_ms = u32(); v.seq_loop = u8();
    for (int n = u16(); n > 0 && in.ok; --n) {
        const auto m = std::uint8_t(u8()), f = std::uint8_t(u8()), e = std::uint8_t(u8());
        v.seq.push_back({ m, f, e });
    }
    for (auto& g : v.gfx) g = std::uint8_t(u8());
    if (u8()) {
        const auto type = in.str();
        const auto mode = wire::intern(in.str());
        const auto u = wire::unit(in);
        if (const auto it = s.mercs.find(std::atoi(type.c_str())); it != s.mercs.end()) v.merc = View::Merc{ u, &it->second.npc, mode };
    }
    for (int n = u16(); n > 0 && in.ok; --n) {
        auto npc = wire::npc(in);
        const auto u = wire::unit(in);
        v.pets.push_back({ std::move(npc), u, wire::intern(in.str()) });
    }
    for (int n = u16(); n > 0 && in.ok; --n) {
        Monster m;
        m.id = i32(); m.type = i32(); m.hp = i32(); m.st.hp = i32(); m.st.level = i32();
        m.boss = d2d::rules::Boss(u8()); m.corpse_used = u8(); m.mode = wire::intern(in.str());
        for (int k = u8(); k > 0 && in.ok; --k) m.mods.push_back(u8());
        m.npc = wire::npc(in);
        m.u = wire::unit(in);
        if (m.type < 0 || std::size_t(m.type) >= s.monsters.types.size()) return std::nullopt;
        v.monsters.push_back(std::move(m));
    }
    for (int n = u16(); n > 0 && in.ok; --n) {
        const auto name = in.str();
        const float x = f32(), y = f32();
        const int dir = u8();
        const auto born = u32();
        if (const auto it = s.missiles.find(name); it != s.missiles.end()) v.missiles.push_back({ &it->second, x, y, dir, born });
    }
    v.attack = i32(); v.attack_skill = i32();
    for (int n = u16(); n > 0 && in.ok; --n) {
        Loot::GroundItem g;
        g.id = i32(); g.item.code = in.str(); g.gold = i32(); g.x = f32(); g.y = f32(); g.ms = u32(); g.label = in.str();
        for (auto& c : g.rgb) c = std::uint8_t(u8());
        v.ground.push_back(std::move(g));
    }
    for (int n = u16(); n > 0 && in.ok; --n) {
        const float x = f32(), y = f32();
        v.fires.push_back({ x, y, &s.trap_fires[u8() ? 1 : 0] });
    }
    for (int n = u16(); n > 0 && in.ok; --n) v.npc_states.push_back(wire::unit(in));
    for (int n = u16(); n > 0 && in.ok; --n) { const int st = i32(); v.boost.emplace_back(st, i32()); }
    v.aura = i32();
    if (const std::size_t n = u32(); n > 0 && in.ok && in.at + n <= b.size() && s.item_tables) {
        const auto save = std::as_bytes(b.subspan(in.at, n));
        try {
            v.header = d2d::d2s::parse_header(save);
            v.stats = d2d::d2s::parse_stats(save, *s.item_tables);
        } catch (const std::exception&) { return std::nullopt; }
        v.has_character = true;
        in.at += n;
        v.items = wire::items(in, *s.item_tables);
        if (u8()) if (auto h = wire::items(in, *s.item_tables); h.size() == 1) v.held = std::move(h[0]);
        if (u8()) {
            Store st;
            st.npc = i32(); st.vendor = i32(); st.hc_idx = i32(); st.npc_id = in.str(); st.gamble = u8();
            for (int k = u16(); k > 0 && in.ok; --k) st.perm.push_back(in.str());
            for (auto& tab : st.tabs) tab = wire::items(in, *s.item_tables);
            st.header = v.header;
            v.store = std::move(st);
        }
    } else if (n > 0) return std::nullopt;
    for (int k = u16(); k > 0 && in.ok; --k) {
        d2d::rules::MercOffer h;
        h.id = i32(); h.level = i32(); h.life = i32(); h.str = i32(); h.dex = i32(); h.cost = i32(); h.def = i32();
        h.dmg_min = i32(); h.dmg_max = i32(); h.exp = u32(); h.seed = u32(); h.name = i32();
        v.hire_offers.push_back(h);
    }
    if (!in.ok || in.at != b.size() || !v.level) return std::nullopt;
    return v;
}

}  // namespace
