// SPDX-License-Identifier: GPL-3.0-or-later
// The View on the wire (the S -> C side, docs/design/multiplayer.md): what
// a client draws, as bytes. Everything that points into GameData goes as
// its key there (the level's id, a missile's name, the merc's hireling
// type, a fire's index) — GameData is the same on every machine.
// Decoded animation modes are interned, so the views stay valid.
// Each tick goes as what changed since the client's last View (ViewEncoder);
// a unit's look goes as strings, not table indices: docs/design/multiplayer.md
// lists that under "Still to change" (d2d's own wire, not game.exe's).
#pragma once

#include "ai/units.hpp"
#include "gamedata.hpp"
#include "inventory.hpp"
#include "log.hpp"
#include "loot.hpp"
#include "protocol.hpp"
#include "world.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <d2s_write.hpp>
#include <overlays.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::game {

namespace wire {
// A stable home for decoded mode names (the units keep string_views).
inline std::string_view intern(std::string text) {
    static std::unordered_set<std::string> pool;
    return *pool.insert(std::move(text)).first;
}
// An Npc's drawn fields, host to client.
inline void npc(Out& out, const Npc& npc_data) {
    out.str(npc_data.root).str(npc_data.code).str(npc_data.base_w).str(npc_data.name).i32(npc_data.light).i32(npc_data.overlay_class).i32(npc_data.colour).u8(npc_data.interact).u8(npc_data.composite_death);
    for (const auto& component : npc_data.comp) out.str(component);
}
// The client's copy of wire::npc's fields, in the same order.
inline Npc npc(In& input) {
    Npc npc;
    npc.root = input.str(); npc.code = input.str(); npc.base_w = input.str(); npc.name = input.str();
    npc.light = input.get<std::int32_t>(); npc.overlay_class = input.get<std::int32_t>(); npc.colour = input.get<std::int32_t>();
    npc.interact = input.get<std::uint8_t>() != 0;
    npc.composite_death = input.get<std::uint8_t>() != 0;
    for (auto& component : npc.comp) component = input.str();
    return npc;
}
inline void unit(Out& out, const UnitState& unit_state) {
    out.u32(unit_state.x.raw).u32(unit_state.y.raw).u8(unit_state.dir).u8(unit_state.walking).u8(unit_state.hidden).u8(unit_state.alert).u32(unit_state.mode_ms).str(unit_state.mode).u16(unit_state.says);
}
inline UnitState unit(In& input) {
    UnitState unit;
    unit.x.raw = input.get<std::uint32_t>(); unit.y.raw = input.get<std::uint32_t>();   // 16.16 subtiles (Fine)
    unit.dir = input.get<std::uint8_t>(); unit.walking = input.get<std::uint8_t>(); unit.hidden = input.get<std::uint8_t>(); unit.alert = input.get<std::uint8_t>();
    unit.mode_ms = input.get<std::uint32_t>();
    unit.mode = intern(input.str());
    unit.says = input.get<std::uint16_t>();
    return unit;
}
// Items as their save form (d2s_write.hpp), then their unit ids and a byte:
// 1 the wielded one (inventory +0x1c), 2 a charm short of its requirements
// (rules::settle_requirements' pass 1).
inline void items(Out& out, const std::vector<d2d::d2s::Item>& list, const d2d::d2s::ItemTables& item_tables) {
    d2d::d2s::detail::BitWriter writer;
    for (const auto& item : list) d2d::d2s::detail::write_item(writer, item, item_tables);
    out.u16(int(list.size())).u32(std::uint32_t(writer.out.size()));
    for (const auto byte : writer.out) out.u8(int(byte));
    for (const auto& item : list) out.i32(item.id).u8((item.wielded ? 1 : 0) | (item.charm_short ? 2 : 0));
}
inline std::vector<d2d::d2s::Item> items(In& input, const d2d::d2s::ItemTables& item_tables) {
    const int count = input.get<std::uint16_t>();
    const std::size_t bytes = input.get<std::uint32_t>();
    std::vector<d2d::d2s::Item> out;
    if (!input.ok || input.offset + bytes > input.bytes.size()) { input.ok = false; return out; }
    try {
        d2d::d2s::detail::Bits bits{ std::as_bytes(input.bytes.subspan(input.offset, bytes)), 0 };
        for (int i = 0; i < count; ++i) out.push_back(d2d::d2s::detail::item(bits, item_tables));
    } catch (const std::exception&) { input.ok = false; return out; }
    input.offset += bytes;
    for (auto& item : out) {
        item.id = input.get<std::int32_t>();
        const auto transient = input.get<std::uint8_t>();
        item.wielded = (transient & 1) != 0;
        item.charm_short = (transient & 2) != 0;
    }
    return out;
}
}  // namespace wire

// GameData's level with Levels.txt id `id`.
inline const Level* level_of(const GameData& game_data, int id) { return game_data.level(id); }

// What one client was last sent (the server keeps one per client): each
// section's bytes, each monster's look and state, each NPC's state. A
// View goes out as what changed since; after a level change, or when the
// client lost track, everything (a keyframe).
struct ViewEncoder {
    int level = -2;
    std::array<std::vector<std::uint8_t>, 9> section;
    std::unordered_map<int, std::vector<std::uint8_t>> look, state;
    std::vector<std::vector<std::uint8_t>> npc;
    void reset() { *this = {}; }
};

namespace wire {
// A section: 0 when it's what the client has, else 1, its length, its bytes.
inline void section(Out& out, std::vector<std::uint8_t>& last, std::vector<std::uint8_t> now) {
    if (now == last) { out.u8(0); return; }
    out.u8(1).u32(std::uint32_t(now.size()));
    out.bytes.insert(out.bytes.end(), now.begin(), now.end());
    last = std::move(now);
}
inline void monster_look(Out& out, const Monster& monster) {
    out.i32(monster.type).i32(monster.stats.hit_points).i32(monster.stats.level).u8(int(monster.boss)).u8(int(monster.mods.size()));
    for (const int mod : monster.mods) out.u8(mod);
    npc(out, monster.npc);
    out.i32(monster.anim_pct).i32(monster.attack_pct).i32(monster.walk_pct).i32(monster.aura_rate).i32(monster.props.ias).i32(monster.props.fcr);
    // Its resists' sources (Monster::target): the hover bar's immunity lines (rules::immunity_lines).
    out.u8(monster.difficulty);
    for (const int resist : monster.boss_res) out.i32(resist);
}
inline void monster_look(In& input, Monster& monster) {
    monster.type = input.get<std::int32_t>(); monster.stats.hit_points = input.get<std::int32_t>(); monster.stats.level = input.get<std::int32_t>();
    monster.boss = d2d::rules::Boss(input.get<std::uint8_t>());
    monster.mods.clear();
    for (int k = input.get<std::uint8_t>(); k > 0 && input.ok; --k) monster.mods.push_back(input.get<std::uint8_t>());
    monster.npc = npc(input);
    monster.anim_pct = input.get<std::int32_t>();
    monster.attack_pct = input.get<std::int32_t>();
    monster.walk_pct = input.get<std::int32_t>();
    monster.aura_rate = input.get<std::int32_t>();
    monster.props.ias = input.get<std::int32_t>();
    monster.props.fcr = input.get<std::int32_t>();
    monster.difficulty = std::clamp(int(input.get<std::uint8_t>()), 0, 2);
    for (auto& resist : monster.boss_res) resist = input.get<std::int32_t>();
}
// One-shot overlays (rules::OneShots): a count, then {row, start} each.
inline void one_shots(Out& out, const d2d::rules::OneShots& list) {
    const auto count = std::min<std::size_t>(list.size(), 255);
    out.u8(int(count));
    for (std::size_t k = 0; k < count; ++k) out.i32(list[k].row).u32(list[k].start);
}
inline d2d::rules::OneShots one_shots(In& input) {
    d2d::rules::OneShots list(input.get<std::uint8_t>());
    for (auto& shot : list) { shot.row = input.get<std::int32_t>(); shot.start = input.get<std::uint32_t>(); }
    return list;
}
inline void monster_state(Out& out, const Monster& monster) {
    out.i32(monster.hit_points).u8(monster.corpse_used).str(monster.mode);
    unit(out, monster.unit);
    // What its states draw from (session.hpp monster_states): the timers change only as a state starts.
    out.u32(monster.poison_until).u32(monster.chill_until).u32(monster.stun_until).i32(monster.curse.skill).u32(monster.curse.until);
    out.u8(int(std::min<std::size_t>(monster.states.size(), 255)));
    for (std::size_t k = 0; k < std::min<std::size_t>(monster.states.size(), 255); ++k) out.i32(monster.states[k].skill).u32(monster.states[k].until);
    out.i32(monster.aura).u8(monster.in_aura).u8(monster.buried).u8(monster.aloft).u8(monster.riding).str(monster.seq_mode).i32(monster.seq_frame);
    for (const int cut : monster.res_cut) out.i32(cut);   // the hover's immunity lines (Fight::target_of)
    out.u8(monster.align).u8(monster.rising);   // a converted one: no hover, no pick (Monster::ally_not_interact); a Horde in its rise's S1: not targetable
    one_shots(out, monster.one_shots);   // its one-shot overlays (its thinks', its casts')
}
inline void monster_state(In& input, Monster& monster) {
    monster.hit_points = input.get<std::int32_t>(); monster.corpse_used = input.get<std::uint8_t>(); monster.mode = intern(input.str());
    monster.unit = unit(input);
    monster.poison_until = input.get<std::uint32_t>(); monster.chill_until = input.get<std::uint32_t>(); monster.stun_until = input.get<std::uint32_t>();
    monster.curse.skill = input.get<std::int32_t>(); monster.curse.until = input.get<std::uint32_t>();
    monster.states.resize(input.get<std::uint8_t>());
    for (auto& held : monster.states) { held.skill = input.get<std::int32_t>(); held.until = input.get<std::uint32_t>(); }
    monster.aura = input.get<std::int32_t>();
    monster.in_aura = input.get<std::uint8_t>() != 0;
    monster.buried = input.get<std::uint8_t>() != 0;
    monster.aloft = input.get<std::uint8_t>() != 0;
    monster.riding = input.get<std::uint8_t>() != 0;
    monster.seq_mode = intern(input.str()); monster.seq_frame = input.get<std::int32_t>();
    for (auto& cut : monster.res_cut) cut = input.get<std::int32_t>();
    monster.align = input.get<std::uint8_t>();
    monster.rising = input.get<std::uint8_t>() != 0;
    monster.one_shots = one_shots(input);
}
}  // namespace wire

// The View as what changed since `enc` last sent (encode_view) and, on
// the client, that change applied to its last View (apply_view).
std::vector<std::uint8_t> encode_view(const GameData& game_data, const View& view, ViewEncoder& enc);

// The client's side: a View message applied to its last View `v`. False for
// anything malformed (the client asks for a keyframe).
bool apply_view(const GameData& game_data, std::span<const std::uint8_t> bytes, View& view);

}  // namespace d2d::game
