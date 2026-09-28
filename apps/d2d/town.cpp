// The in-game client: view to units, the frame's light, the HUD overlays (town.hpp).
#include "town.hpp"

namespace d2d::app {

// town.hpp
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

void view_units(const Scene& s, const View& v, float cx, float cy, const std::string* merc_label, std::vector<Unit>& out,
                std::span<const View::Shot> fx , std::uint32_t now_ms , const std::string* corpse_name , int cls ,
                StateClock* clk) {
    auto in_view = [&](float x, float y) { return std::abs(x - cx) < 14 && std::abs(y - cy) < 14; };
    for (std::size_t i = 0; i < v.ground.size(); ++i) {
        const auto& g = v.ground[i];
        if (!in_view(g.x, g.y)) continue;
        Unit u{ g.x, g.y, nullptr, 0, &g.label, g.ms, -1000 - int(i) };
        u.sprite = s.flippy(g.item.code);
        u.cmap = s.item_map(g.item, false);   // FUN_00471ec0: the character's colours
        u.rgb = g.rgb;
        out.push_back(u);
    }
    for (const auto& f : v.fires) out.push_back({ f.x, f.y, &s.npc_anim(*f.npc, f.npc->mode), 0, nullptr, 0, -2 });
    // The player's corpses: lying (DT's last frame, as the dead player is
    // drawn), in what they wore; named by the player (-3000 - which).
    for (const auto& k : v.corpses) {
        const auto& anim = s.composite(cls, kModeDT, k.gfx);
        out.push_back({ k.x, k.y, &anim, k.dir, corpse_name, now_ms - (anim.length_ms() - 1), -3000 - k.which });
    }
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
        out.back().overlay_class = m.npc.overlay_class;
        out.back().cmap = s.monster_map(m.npc);
        dress(s, out.back(), m.id, monster_states(s, m, now_ms, v.aura), clk);
    }
}

void view_overlays(std::vector<std::uint8_t>& fb, const Scene& s, const View& v, int hovered) {
    if (hovered >= 0) draw_monster_bar(fb, s, v.monsters[std::size_t(hovered)]);
    else if (const int a = v.monster(v.attack); a >= 0) draw_monster_bar(fb, s, v.monsters[std::size_t(a)]);
    // The death screen (FUN_00453100): youdiedhardcore then youdiedinst,
    // centred, from H/2 - 94 down 48 px each; then in Font30, red, centred,
    // 48 px apart: string 0x13e8 "Your deeds of valor..." (hardcore) or
    // 0x13e6 "Death takes its toll of %d Gold" (when goldlost > 0), and in
    // Nightmare / Hell (softcore) 0x13e7 "You have lost experience".
    // ponytail: FUN_00502680's anchor taken as the cel's bottom, centred.
    if (v.pmode == kModeDD) {
        const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
        int y = int(kH) / 2 - 0x5e;
        for (const auto* spr : { &s.you_died, &s.you_died_inst }) {
            int w = 0;                                   // its frames side by side (FUN_00502680)
            for (std::uint32_t k = 0; k < spr->frames_per_direction(); ++k) w += int(spr->frame(0, k).width);
            for (std::uint32_t k = 0, x = std::uint32_t(int(kW) / 2 - w / 2); k < spr->frames_per_direction(); ++k) {
                const auto& f = spr->frame(0, k);
                blit_sprite(fb, f, pal, int(x), y - int(f.height) + 1);
                x += f.width;
            }
            y += 0x30;
        }
        const auto& f30 = s.font30.line_height() > 0 ? s.font30 : s.font;
        auto line = [&](const std::string& t) {
            if (!t.empty()) f30.draw_tinted(fb, kW, kH, pal, int(kW) / 2 - f30.measure(t) / 2, y - f30.line_height() + 1, t, 255, 77, 77);
            y += 0x30;
        };
        const bool hc = v.header.hardcore();
        if (hc) line(string_id(s, 0x13e8));
        else if (v.gold_lost > 0) {
            auto fmt = string_id(s, 0x13e6);
            if (const auto at = fmt.find("%d"); at != std::string::npos) fmt.replace(at, 2, std::to_string(v.gold_lost));
            line(fmt);
        }
        if (!hc && v.header.active_difficulty() > 0) line(string_id(s, 0x13e7));
    }
}

std::pair<int, std::uint32_t> view_seq(const Scene& s, int cls, const View& v, std::uint32_t ms) {
    const auto i = std::size_t((ms - v.player.mode_ms) / std::max<std::uint32_t>(v.seq_frame_ms, 1));
    const auto& f = v.seq[v.seq_loop ? i % v.seq.size() : std::min(i, v.seq.size() - 1)];
    const auto mpf = s.composite(cls, f.mode, v.gfx).ms_per_frame();
    return { f.mode, ms - mpf * f.frame - mpf / 2 };
}

Lighting frame_light(const Scene& s, const View& v, float cam_x, float cam_y, std::span<const View::Shot> fx , int ambient ,
                     std::span<const Unit> units , const Unit* player_look , std::uint32_t now) {
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
    struct Ease { int r8 = 0; std::uint32_t at = 0, seen = 0; };
    static std::unordered_map<std::uint64_t, Ease> eases;         // by light: its radius now, in eighths
    auto eased = [&](std::uint64_t key, int radius, int first) {
        const int want = std::clamp(radius, 0, 18) * 8;
        auto [it, fresh] = eases.try_emplace(key, Ease{ std::clamp(first, 0, 18) * 8, now, now });
        auto& e = it->second;
        if (const auto steps = int((now - e.at) / 40); steps > 0) {
            e.r8 = e.r8 < want ? std::min(want, e.r8 + 8 * steps) : std::max(want, e.r8 - 8 * steps);
            e.at += std::uint32_t(steps) * 40;
        }
        e.seen = now;
        return e.r8;
    };
    auto stamp8 = [&](float x, float y, int r8, bool shadowed) {
        if (r8 <= 0) return;
        if (shadowed) l.grid.stamp_shadowed(int(x * 40), int(y * 40), r8, 255);
        else l.grid.stamp(int(x * 40), int(y * 40), r8, 255);
    };
    auto stamp = [&](float x, float y, int radius, bool shadowed) { stamp8(x, y, std::min(radius, 18) * 8, shadowed); };
    auto lamp = [&](std::uint64_t key, float x, float y, int radius, bool shadowed, int first = -1) {
        stamp8(x, y, eased(key, radius, first < 0 ? radius : first), shadowed);
    };
    lamp(1, cam_x, cam_y, std::max(0, 13 + v.light_bonus), true);   // FUN_00460930: 13 + the bonus, capped at 18
    static constexpr std::array<std::string_view, 8> kModes{ "NU", "OP", "ON", "S1", "S2", "S3", "S4", "S5" };
    auto lit = [&](const Npc& n, std::string_view mode) {
        const auto m = std::ranges::find(kModes, mode.empty() ? std::string_view(n.mode) : mode);
        return n.root == "objects" && m != kModes.end() ? int(n.lit[std::size_t(m - kModes.begin())]) : 0;
    };
    for (std::size_t i = 0; i < v.level->npcs.size(); ++i) {
        const auto& n = v.level->npcs[i];
        const auto* st = i < v.npc_states.size() ? &v.npc_states[i] : nullptr;
        if (st && st->hidden) continue;
        lamp(2ull << 32 | i, st ? st->x : n.x, st ? st->y : n.y, lit(n, st ? st->mode : std::string_view{}), true);
    }
    for (const auto& nb : v.level->nearby)                  // the torches over the level's edge
        for (const auto& n : nb.level->npcs) stamp(n.x + float(nb.dx), n.y + float(nb.dy), lit(n, {}), true);
    for (const auto& m : v.monsters) if (m.alive()) lamp(4ull << 32 | std::uint32_t(m.id), m.u.x, m.u.y, m.npc.light, false);
    for (const auto& p : v.portals)                        // Lit1 (OP) while it opens, then Lit2 (ON)
        lamp(5ull << 32 | std::uint32_t(p.which), p.x, p.y, int(s.town_portal.lit[now - p.born < kPortalOpenMs ? 1 : 2]), true);
    for (const auto& m : v.missiles) if (m.info) stamp(m.x, m.y, m.info->light, false);
    for (const auto& m : fx) if (m.info) stamp(m.x, m.y, m.info->light, false);
    // States' overlays light their unit (Overlay.txt: InitRadius growing to
    // Radius, FUN_00474160 / FUN_00474290); a plain light, its colour
    // dropped like every light's here.
    auto overs = [&](const Unit& u, float x, float y) {
        for (std::size_t k = 0; k < u.overs.size(); ++k) {
            const auto& o = u.overs[k];
            if (o.o->radius > 0 && (!o.once || (now - o.start) * std::uint32_t(std::max(o.o->rate, 1)) / 640 < std::uint32_t(o.o->frames)))
                lamp(6ull << 56 ^ std::uint64_t(reinterpret_cast<std::uintptr_t>(o.o)) ^ std::uint64_t(std::uint32_t(u.npc)) << 40 ^ o.start,
                     x, y, o.o->radius, false, o.o->init_radius);
        }
    };
    for (const auto& u : units) overs(u, u.x, u.y);
    if (player_look) overs(*player_look, cam_x, cam_y);
    std::erase_if(eases, [&](const auto& e) { return now - e.second.seen > 2000; });
    return l;
}


}  // namespace d2d::app
