// SPDX-License-Identifier: GPL-3.0-or-later
// The View on the wire (the S -> C side, docs/design/multiplayer.md): what
// a client draws, as bytes. Everything that points into GameData goes as
// its key there (the level's id, a missile's name, the merc's hireling
// type, a fire's index) — GameData is the same on every machine.
// Decoded animation modes are interned, so the views stay valid.
// ponytail: whole views every tick (no deltas); a unit's look as strings.
#pragma once

#include "ai.hpp"
#include "gamedata.hpp"
#include "inventory.hpp"
#include "log.hpp"
#include "loot.hpp"
#include "protocol.hpp"
#include "world.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <d2s_write.hpp>
#include <uniques.hpp>

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
inline void npc(Out& out, const Npc& npc_data) {
    out.str(npc_data.root).str(npc_data.code).str(npc_data.base_w).str(npc_data.name).i32(npc_data.light).i32(npc_data.overlay_class).i32(npc_data.colour);
    for (const auto& component : npc_data.comp) out.str(component);
}
inline Npc npc(In& input) {
    Npc npc;
    npc.root = input.str(); npc.code = input.str(); npc.base_w = input.str(); npc.name = input.str();
    npc.light = input.get<std::int32_t>(); npc.overlay_class = input.get<std::int32_t>(); npc.colour = input.get<std::int32_t>();
    for (auto& component : npc.comp) component = input.str();
    return npc;
}
inline void unit(Out& out, const UnitState& unit_state) {
    out.f32(unit_state.x).f32(unit_state.y).u8(unit_state.dir).u8(unit_state.walking).u8(unit_state.hidden).u8(unit_state.alert).u32(unit_state.mode_ms).str(unit_state.mode).u16(unit_state.says);
}
inline UnitState unit(In& input) {
    UnitState unit;
    unit.x = input.get<float>(); unit.y = input.get<float>();
    unit.dir = input.get<std::uint8_t>(); unit.walking = input.get<std::uint8_t>(); unit.hidden = input.get<std::uint8_t>(); unit.alert = input.get<std::uint8_t>();
    unit.mode_ms = input.get<std::uint32_t>();
    unit.mode = intern(input.str());
    unit.says = input.get<std::uint16_t>();
    return unit;
}
// Items as their save form (d2s_write.hpp), then their unit ids.
inline void items(Out& out, const std::vector<d2d::d2s::Item>& list, const d2d::d2s::ItemTables& item_tables) {
    d2d::d2s::detail::BitWriter writer;
    for (const auto& item : list) d2d::d2s::detail::write_item(writer, item, item_tables);
    out.u16(int(list.size())).u32(std::uint32_t(writer.out.size()));
    for (const auto byte : writer.out) out.u8(int(byte));
    for (const auto& item : list) out.i32(item.id);
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
    for (auto& item : out) item.id = input.get<std::int32_t>();
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
    std::array<std::vector<std::uint8_t>, 8> section;
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
}
inline void monster_look(In& input, Monster& monster) {
    monster.type = input.get<std::int32_t>(); monster.stats.hit_points = input.get<std::int32_t>(); monster.stats.level = input.get<std::int32_t>();
    monster.boss = d2d::rules::Boss(input.get<std::uint8_t>());
    monster.mods.clear();
    for (int k = input.get<std::uint8_t>(); k > 0 && input.ok; --k) monster.mods.push_back(input.get<std::uint8_t>());
    monster.npc = npc(input);
}
inline void monster_state(Out& out, const Monster& monster) {
    out.i32(monster.hit_points).u8(monster.corpse_used).str(monster.mode);
    unit(out, monster.unit);
    // What its states draw from (town.hpp monster_states): the timers change only as a state starts.
    out.u32(monster.poison_until).u32(monster.chill_until).u32(monster.stun_until).i32(monster.curse.skill).u32(monster.curse.until).i32(monster.cry.skill).u32(monster.cry.until).i32(monster.aura).u8(monster.in_aura);
}
inline void monster_state(In& input, Monster& monster) {
    monster.hit_points = input.get<std::int32_t>(); monster.corpse_used = input.get<std::uint8_t>(); monster.mode = intern(input.str());
    monster.unit = unit(input);
    monster.poison_until = input.get<std::uint32_t>(); monster.chill_until = input.get<std::uint32_t>(); monster.stun_until = input.get<std::uint32_t>();
    monster.curse.skill = input.get<std::int32_t>(); monster.curse.until = input.get<std::uint32_t>();
    monster.cry.skill = input.get<std::int32_t>(); monster.cry.until = input.get<std::uint32_t>(); monster.aura = input.get<std::int32_t>();
    monster.in_aura = input.get<std::uint8_t>() != 0;
}
}  // namespace wire

// The View as what changed since `enc` last sent (encode_view) and, on
// the client, that change applied to its last View (apply_view).
inline std::vector<std::uint8_t> encode_view(const GameData& game_data, const View& view, ViewEncoder& enc) {
    wire::Out out;
    out.u8(0x70);                                              // our own id: game.exe has no packet for a whole view
    const int lvl = view.level ? view.level->id : -1;
    const bool key = enc.level != lvl;
    if (key) { enc.reset(); enc.level = lvl; }
    out.u8(key).i32(lvl);
    // 0: the player, what it wears and does, its merc, target, aura, boost.
    {
        wire::Out chunk;
        wire::unit(chunk, view.player);
        chunk.u8(view.running).u8(view.dead).u8(view.poisoned).u8(view.chilled).i32(view.pmode).f32(view.prate).u32(view.seq_frame_ms).u8(view.seq_loop);
        chunk.u16(int(view.seq.size()));
        for (const auto& frame : view.seq) chunk.u8(frame.mode).u8(frame.frame).u8(frame.event);
        for (const auto look_byte : view.gfx) chunk.u8(look_byte);
        std::string merc_type;
        if (view.merc) for (const auto& [merc_key, merc] : game_data.mercs) if (&merc.npc == view.merc->npc) merc_type = std::to_string(merc_key);
        chunk.u8(view.merc && !merc_type.empty());
        if (view.merc && !merc_type.empty()) { chunk.str(merc_type).str(view.merc->mode); wire::unit(chunk, view.merc->unit); }
        chunk.i32(view.attack).i32(view.attack_skill).i32(view.aura).i32(view.day.phase).i32(view.day.time).u8(view.den_cleared).i32(view.light_bonus);
        for (std::size_t quest = 0; quest < 7; ++quest) chunk.u8(view.quest_log[quest]).u16(view.game_quests[quest]);
        chunk.i32(view.den_left);
        for (const auto& line : view.attack_lines)
            chunk.i32(line.skill).u8(line.damage).i32(line.min).i32(line.max).u8(line.damage_colour).i32(line.attack_rating).u8(line.ar_colour);
        chunk.u16(int(view.boost.size()));
        for (const auto& [stat, val] : view.boost) chunk.i32(stat).i32(val);
        chunk.u8(view.boost_code);
        chunk.i32(view.gold_lost);
        chunk.u16(int(view.buffs.size()));
        for (const int buff : view.buffs) chunk.i32(buff);
        wire::section(out, enc.section[0], std::move(chunk.bytes));
    }
    // 1: pets, 2: missiles.
    {
        wire::Out chunk;
        chunk.u16(int(view.pets.size()));
        for (const auto& pet : view.pets) { wire::npc(chunk, pet.npc); wire::unit(chunk, pet.unit); chunk.str(pet.mode); }
        wire::section(out, enc.section[1], std::move(chunk.bytes));
    }
    {
        wire::Out chunk;
        chunk.u16(int(view.missiles.size()));
        for (const auto& missile : view.missiles) chunk.str(missile.info->name).f32(missile.x).f32(missile.y).u8(missile.dir).u32(missile.born);
        wire::section(out, enc.section[2], std::move(chunk.bytes));
    }
    // 3: the ground, 4: fires.
    {
        wire::Out chunk;
        chunk.u16(int(view.ground.size()));
        for (const auto& ground_item : view.ground)
            chunk.i32(ground_item.id).str(ground_item.item.code).i32(ground_item.gold).f32(ground_item.x).f32(ground_item.y).u32(ground_item.now_ms).str(ground_item.label).u8(ground_item.rgb[0]).u8(ground_item.rgb[1]).u8(ground_item.rgb[2]);
        wire::section(out, enc.section[3], std::move(chunk.bytes));
    }
    {
        wire::Out chunk;
        chunk.u16(int(view.fires.size()));
        for (const auto& fire : view.fires) chunk.f32(fire.x).f32(fire.y).u8(fire.npc == &game_data.trap_fires[1]);
        chunk.u16(int(view.portals.size()));
        for (const auto& portal : view.portals) chunk.f32(portal.x).f32(portal.y).i32(portal.destination).u32(portal.born).u8(portal.which);
        chunk.u16(int(view.corpses.size()));
        for (const auto& corpse : view.corpses) {
            chunk.f32(corpse.x).f32(corpse.y).u8(corpse.dir).u8(corpse.which);
            for (const auto look_byte : corpse.gfx) chunk.u8(look_byte);
        }
        wire::section(out, enc.section[4], std::move(chunk.bytes));
    }
    // The owner's character: 5 its header (in its save form), 7 its stats
    // and skills (they change with every tick of regeneration), 6 its items,
    // the item in hand and the store's stock; then the hire list.
    {
        wire::Out chunk;
        std::vector<std::byte> save;
        if (game_data.item_tables && view.has_character)
            try { save = d2d::d2s::write_save({}, view.header, {}, {}, *game_data.item_tables); }
            catch (const std::exception& error) { d2d::log::warn("the character didn't encode: {}", error.what()); }
        chunk.u32(std::uint32_t(save.size()));
        for (const auto byte : save) chunk.u8(int(byte));
        wire::section(out, enc.section[5], std::move(chunk.bytes));
    }
    {
        wire::Out chunk;
        for (const auto x : view.stats.values) chunk.u32(std::uint32_t(x));   // the save's widths: 32 bits at most
        for (const auto skill_level : view.stats.skills) chunk.u8(skill_level);
        wire::section(out, enc.section[7], std::move(chunk.bytes));
    }
    {
        wire::Out chunk;
        chunk.u8(game_data.item_tables && view.has_character);
        if (game_data.item_tables && view.has_character) {
            wire::items(chunk, view.items, *game_data.item_tables);
            chunk.u8(view.held.has_value());
            if (view.held) wire::items(chunk, { *view.held }, *game_data.item_tables);
            chunk.u8(view.store.has_value());
            if (view.store) {
                const auto& store = *view.store;
                chunk.i32(store.npc).i32(store.vendor).i32(store.hc_idx).str(store.npc_id).u8(store.gamble);
                chunk.u16(int(store.perm.size()));
                for (const auto& perm_code : store.perm) chunk.str(perm_code);
                for (const auto& tab : store.tabs) wire::items(chunk, tab, *game_data.item_tables);
            }
        }
        wire::section(out, enc.section[6], std::move(chunk.bytes));
    }
    {
        wire::Out chunk;
        chunk.u16(int(view.hire_offers.size()));
        for (const auto& offer : view.hire_offers)
            chunk.i32(offer.id).i32(offer.level).i32(offer.life).i32(offer.str).i32(offer.dex).i32(offer.cost).i32(offer.def).i32(offer.dmg_min).i32(offer.dmg_max)
             .u32(offer.exp).u32(offer.seed).i32(offer.name);
        out.u8(1).u32(std::uint32_t(chunk.bytes.size()));                  // small: always
        out.bytes.insert(out.bytes.end(), chunk.bytes.begin(), chunk.bytes.end());
    }
    // Monsters: the looks and states that changed, and those gone.
    {
        std::vector<std::pair<int, std::vector<std::uint8_t>>> looks, states;
        std::unordered_set<int> here;
        for (const auto& monster : view.monsters) {
            here.insert(monster.id);
            wire::Out look_chunk, state_chunk;
            wire::monster_look(look_chunk, monster);
            wire::monster_state(state_chunk, monster);
            if (auto& was = enc.look[monster.id]; was != look_chunk.bytes) { looks.emplace_back(monster.id, look_chunk.bytes); was = std::move(look_chunk.bytes); }
            if (auto& was = enc.state[monster.id]; was != state_chunk.bytes) { states.emplace_back(monster.id, state_chunk.bytes); was = std::move(state_chunk.bytes); }
        }
        std::vector<int> gone;
        for (auto entry = enc.look.begin(); entry != enc.look.end();)
            if (!here.contains(entry->first)) { gone.push_back(entry->first); enc.state.erase(entry->first); entry = enc.look.erase(entry); }
            else ++entry;
        for (const auto* list : { &looks, &states }) {
            out.u16(int(list->size()));
            for (const auto& [id, encoded] : *list) { out.i32(id); out.bytes.insert(out.bytes.end(), encoded.begin(), encoded.end()); }
        }
        out.u16(int(gone.size()));
        for (const int id : gone) out.i32(id);
    }
    // NPCs as they patrol: those that changed.
    {
        enc.npc.resize(view.npc_states.size());
        std::vector<std::size_t> changed;
        std::vector<std::vector<std::uint8_t>> now(view.npc_states.size());
        for (std::size_t i = 0; i < view.npc_states.size(); ++i) {
            wire::Out unchanged;
            wire::unit(unchanged, view.npc_states[i]);
            if (unchanged.bytes != enc.npc[i]) { changed.push_back(i); enc.npc[i] = unchanged.bytes; }
            now[i] = std::move(unchanged.bytes);
        }
        out.u16(int(view.npc_states.size())).u16(int(changed.size()));
        for (const auto index : changed) { out.u16(int(index)); out.bytes.insert(out.bytes.end(), now[index].begin(), now[index].end()); }
    }
    // What happened: events and sounds (never repeated).
    out.u16(int(view.events.size()));
    for (const auto& event : view.events) {
        if (const auto* level_changed = std::get_if<ev::LevelChanged>(&event)) out.u8(0).i32(level_changed->from ? level_changed->from->id : -1).u8(level_changed->keep_map);
        else {
            const auto& open_ui = std::get<ev::OpenUI>(event);
            out.u8(1).u8(int(open_ui.kind)).i32(open_ui.npc).u16(int(open_ui.quest.size()));
            for (const auto& message : open_ui.quest) out.i32(message.string).u8(message.greet);
        }
    }
    out.u16(int(view.sounds.size()));
    for (const auto& cue : view.sounds) out.u32(cue.when_ms).i32(cue.sound).f32(cue.x).f32(cue.y);
    return out.bytes;
}

// The client's side: a View message applied to its last View `v`. False for
// anything malformed (the client asks for a keyframe).
inline bool apply_view(const GameData& game_data, std::span<const std::uint8_t> bytes, View& view) {
    if (bytes.empty() || bytes[0] != 0x70) return false;
    wire::In input{ bytes };
    auto byte = [&] { return int(input.get<std::uint8_t>()); };
    auto u16 = [&] { return int(input.get<std::uint16_t>()); };
    auto i32 = [&] { return int(input.get<std::int32_t>()); };
    auto f32 = [&] { return input.get<float>(); };
    auto u32 = [&] { return input.get<std::uint32_t>(); };
    if (byte()) { view.monsters.clear(); view.npc_states.clear(); }   // a keyframe
    view.level = level_of(game_data, i32());
    // A section's body, or nothing when it's unchanged.
    auto body = [&](auto&& read) {
        if (!byte()) return;
        const std::size_t length = u32(), end = input.offset + length;
        if (!input.ok || end > bytes.size()) { input.ok = false; return; }
        read();
        if (input.offset != end) input.ok = false;
    };
    body([&] {
        view.player = wire::unit(input);
        view.running = byte(); view.dead = byte(); view.poisoned = byte(); view.chilled = byte(); view.pmode = i32(); view.prate = f32(); view.seq_frame_ms = u32(); view.seq_loop = byte();
        view.seq.clear();
        for (int count = u16(); count > 0 && input.ok; --count) {
            const auto mode = std::uint8_t(byte()), frame = std::uint8_t(byte()), event = std::uint8_t(byte());
            view.seq.push_back({ mode, frame, event });
        }
        for (auto& look_byte : view.gfx) look_byte = std::uint8_t(byte());
        view.merc.reset();
        if (byte()) {
            const auto type = input.str();
            const auto mode = wire::intern(input.str());
            const auto unit = wire::unit(input);
            if (const auto found = game_data.mercs.find(std::atoi(type.c_str())); found != game_data.mercs.end()) view.merc = View::Merc{ unit, &found->second.npc, mode };
        }
        view.attack = i32(); view.attack_skill = i32(); view.aura = i32(); view.day.phase = i32(); view.day.time = i32(); view.den_cleared = byte() != 0; view.light_bonus = i32(); 
        for (std::size_t quest = 0; quest < 7; ++quest) { view.quest_log[quest] = std::uint8_t(byte()); view.game_quests[quest] = std::uint16_t(u16()); }
        view.den_left = i32();
        for (auto& line : view.attack_lines) {
            line.skill = i32(); line.damage = byte() != 0; line.min = i32(); line.max = i32(); line.damage_colour = byte();
            line.attack_rating = i32(); line.ar_colour = byte();
        }
        view.boost.clear();
        for (int count = u16(); count > 0 && input.ok; --count) { const int stat = i32(); view.boost.emplace_back(stat, i32()); }
        view.boost_code = byte();
        view.gold_lost = i32();
        view.buffs.clear();
        for (int count = u16(); count > 0 && input.ok; --count) view.buffs.push_back(i32());
    });
    body([&] {
        view.pets.clear();
        for (int count = u16(); count > 0 && input.ok; --count) {
            auto npc = wire::npc(input);
            const auto unit = wire::unit(input);
            view.pets.push_back({ std::move(npc), unit, wire::intern(input.str()) });
        }
    });
    body([&] {
        view.missiles.clear();
        for (int count = u16(); count > 0 && input.ok; --count) {
            const auto name = input.str();
            const float x = f32(), y = f32();
            const int dir = byte();
            const auto born = u32();
            if (const auto found = game_data.missiles.find(name); found != game_data.missiles.end()) view.missiles.push_back({ &found->second, x, y, dir, born });
        }
    });
    body([&] {
        view.ground.clear();
        for (int count = u16(); count > 0 && input.ok; --count) {
            Loot::GroundItem ground_item;
            ground_item.id = i32(); ground_item.item.code = input.str(); ground_item.gold = i32(); ground_item.x = f32(); ground_item.y = f32(); ground_item.now_ms = u32(); ground_item.label = input.str();
            for (auto& channel : ground_item.rgb) channel = std::uint8_t(byte());
            view.ground.push_back(std::move(ground_item));
        }
    });
    body([&] {
        view.fires.clear();
        for (int count = u16(); count > 0 && input.ok; --count) {
            const float x = f32(), y = f32();
            view.fires.push_back({ x, y, &game_data.trap_fires[byte() ? 1 : 0] });
        }
        view.portals.clear();
        for (int count = u16(); count > 0 && input.ok; --count) {
            View::Portal portal{};
            portal.x = f32(); portal.y = f32(); portal.destination = i32(); portal.born = u32(); portal.which = byte();
            view.portals.push_back(portal);
        }
        view.corpses.clear();
        for (int count = u16(); count > 0 && input.ok; --count) {
            View::Corpse corpse{};
            corpse.x = f32(); corpse.y = f32(); corpse.dir = byte(); corpse.which = byte();
            for (auto& look_byte : corpse.gfx) look_byte = std::uint8_t(byte());
            view.corpses.push_back(corpse);
        }
    });
    body([&] {
        const std::size_t length = u32();
        if (length == 0 || !game_data.item_tables) { view.has_character = false; input.offset += length; return; }
        if (input.offset + length > bytes.size()) { input.ok = false; return; }
        try { view.header = d2d::d2s::parse_header(std::as_bytes(bytes.subspan(input.offset, length))); }
        catch (const std::exception&) { input.ok = false; return; }
        input.offset += length;
        view.has_character = true;
    });
    body([&] {
        for (auto& x : view.stats.values) x = u32();
        for (auto& skill_level : view.stats.skills) skill_level = std::uint8_t(byte());
    });
    body([&] {
        if (!byte() || !game_data.item_tables) return;
        view.items = wire::items(input, *game_data.item_tables);
        view.held.reset();
        if (byte()) if (auto held_items = wire::items(input, *game_data.item_tables); held_items.size() == 1) view.held = std::move(held_items[0]);
        view.store.reset();
        if (byte()) {
            Store store;
            store.npc = i32(); store.vendor = i32(); store.hc_idx = i32(); store.npc_id = input.str(); store.gamble = byte();
            for (int k = u16(); k > 0 && input.ok; --k) store.perm.push_back(input.str());
            for (auto& tab : store.tabs) tab = wire::items(input, *game_data.item_tables);
            store.header = view.header;
            view.store = std::move(store);
        }
    });
    body([&] {
        view.hire_offers.clear();
        for (int k = u16(); k > 0 && input.ok; --k) {
            d2d::rules::MercOffer offer;
            offer.id = i32(); offer.level = i32(); offer.life = i32(); offer.str = i32(); offer.dex = i32(); offer.cost = i32(); offer.def = i32();
            offer.dmg_min = i32(); offer.dmg_max = i32(); offer.exp = u32(); offer.seed = u32(); offer.name = i32();
            view.hire_offers.push_back(offer);
        }
    });
    // Monsters: looks (new ones too), states, the gone.
    auto mon = [&](int id) -> Monster& {
        const int monster_index = view.monster(id);
        if (monster_index >= 0) return view.monsters[std::size_t(monster_index)];
        view.monsters.emplace_back().id = id;
        return view.monsters.back();
    };
    for (int count = u16(); count > 0 && input.ok; --count) {
        auto& monster = mon(i32());
        wire::monster_look(input, monster);
        if (monster.type < 0 || std::size_t(monster.type) >= game_data.monsters.types.size()) return false;
    }
    for (int count = u16(); count > 0 && input.ok; --count) wire::monster_state(input, mon(i32()));
    for (int count = u16(); count > 0 && input.ok; --count) {
        const int monster_index = view.monster(i32());
        if (monster_index >= 0) view.monsters.erase(view.monsters.begin() + monster_index);
    }
    // NPCs.
    view.npc_states.resize(std::size_t(u16()));
    for (int count = u16(); count > 0 && input.ok; --count) {
        const auto npc_index = std::size_t(u16());
        auto unit = wire::unit(input);
        if (npc_index < view.npc_states.size()) view.npc_states[npc_index] = unit;
    }
    view.events.clear();
    for (int k = u16(); k > 0 && input.ok; --k) {
        if (byte() == 0) {
            const Level* from = level_of(game_data, i32());
            const bool keep = byte();
            if (from) view.events.push_back(ev::LevelChanged{ from, keep });
        } else {
            const auto kind = ev::OpenUI::Kind(byte());
            ev::OpenUI open_ui{ kind, i32() };
            for (int count = u16(); count > 0 && input.ok; --count) { const int str = i32(); open_ui.quest.push_back({ str, byte() != 0 }); }
            view.events.push_back(std::move(open_ui));
        }
    }
    view.sounds.clear();
    for (int k = u16(); k > 0 && input.ok; --k) {
        const auto when_ms = u32();
        const int snd = i32();
        const float x = f32(), y = f32();
        view.sounds.push_back({ when_ms, snd, x, y });
    }
    return input.ok && input.offset == bytes.size() && view.level;
}

}  // namespace d2d::game
