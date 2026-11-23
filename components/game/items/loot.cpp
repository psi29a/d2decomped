// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for loot.hpp: the drops, where they land and the ground they lie on.
#include "loot.hpp"

#include "ai/units.hpp"
#include "character.hpp"
#include "gamedata.hpp"
#include "inventory.hpp"
#include "item_text.hpp"
#include "level.hpp"
#include "spawning.hpp"

#include <d2s_items.hpp>
#include <drops.hpp>
#include <log.hpp>
#include <monster_ids.hpp>
#include <rules.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

auto Loot::enter(const Level* destination) -> void {
    if (destination == ground_level) return;
    if (ground_level) kept[ground_level] = std::move(ground);
    ground = std::move(kept[destination]);
    ground_level = destination;
}

auto Loot::drop(Monster& monster, d2d::rules::Rng& game_seed, std::uint32_t now_ms, int players, int nearby, int merc_find, int merc_gold) -> void {
    const int diff = character->header.active_difficulty();
    // Champions drop from TreasureClass2, uniques from 3, superuniques
    // from their SuperUniques TC (for the difficulty).
    const auto difficulty_index = std::size_t(std::clamp(diff, 0, 2));
    const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
    const std::string& treasure_class = monster.super >= 0 && std::size_t(monster.super) < game_data->superuniques.size() && !game_data->superuniques[std::size_t(monster.super)].treasure_classes[difficulty_index].empty()
                                ? game_data->superuniques[std::size_t(monster.super)].treasure_classes[difficulty_index]
                          : monster.boss == d2d::rules::Boss::champion && !type_info.tc_champion[difficulty_index].empty() ? type_info.tc_champion[difficulty_index]
                          : monster.boss == d2d::rules::Boss::unique && !type_info.tc_unique[difficulty_index].empty()     ? type_info.tc_unique[difficulty_index]
                                                                                               : type_info.diff[difficulty_index].treasure_class;
    // FUN_005a6600: TreasureClass4 while its quest isn't done; then
    // FUN_0055afa0 moves it on by the monster's level past normal.
    const auto& quest_tc = type_info.tc_quest[difficulty_index];
    const auto& header = character->header;
    const bool quest_open = type_info.tc_quest_id && !quest_tc.empty() && !header.quest_flag(diff, type_info.tc_quest_id, 15)
                            && !header.quest_flag(diff, type_info.tc_quest_id, 1) && !header.quest_flag(diff, type_info.tc_quest_id, type_info.tc_quest_cp);
    const auto rolled = d2d::rules::tc_upgrade(game_data->rules, quest_open ? quest_tc : treasure_class, diff > 0 && !type_info.tc_fixed ? monster.stats.level : 0);
    // The killer's magic find (stat 80) and gold find (79) and its
    // owner's (FUN_005589a0, FUN_0058f0d0): the player's gear, + the
    // merc's for its kill (a pet has none); no 1.14d skill or state
    // gives either, the wisps' dance aside.
    int magic_find = merc_find, gold_find = merc_gold;
    for (const auto& prop : gear_props(*game_data, character->items)) {
        if (prop.stat == d2d::d2s::kMagicFind) magic_find += prop.value;
        if (prop.stat == d2d::d2s::kGoldFind) gold_find += prop.value;
    }
    if (now_ms < blessed_until) magic_find += blessed_find;   // the wisps' dance (Fight::wisp_blessing)
    constexpr int kMost = 6;
    std::vector<d2d::rules::Drop> drops;
    // FUN_0055a6d0 counts the drop before FUN_005589a0 (0x55af4c): the
    // one that fills the count leaves without gold find. A Hell Bovine's
    // drops are flagged 1 (FUN_0055a550): the Cow King set (29) drops.
    // Each is put as it is rolled (FUN_0055a550 places it, FUN_00555da0, as
    // the roll makes it): a drop that finds no floor is not made, not
    // counted, and takes nothing off the game seed.
    d2d::rules::roll_drops(game_data->rules, rolled, monster.stats.level, monster.seed, drops, players, magic_find, kMost, 0, nearby, std::max(monster.players, 1),
                           [&](const d2d::rules::Drop& dropped, int made) {
                               return put(dropped, monster.unit.x, monster.unit.y, monster.stats.level, game_seed, now_ms,
                                          monster.type == d2d::rules::monster_ids::kHellBovine, made + 1 < kMost ? gold_find : 0);
                           });
}

auto Loot::find_landing(float x, float y, const Level* onto) const -> std::optional<Landing> {
    const Level* where = onto ? onto : level;
    if (where->walk.empty()) return Landing{ where, x, y };
    const auto rooms = automap_rooms(*game_data, *where, x, y);
    // The room holding subtile (at_x, at_y) of `where`, in act subtiles.
    const auto room_at = [&](int at_x, int at_y) -> const AutomapRoom* {
        const int act_x = where->world_x * 5 + at_x, act_y = where->world_y * 5 + at_y;
        for (const auto& room : rooms)
            if (room.level && act_x >= room.x * 5 && act_y >= room.y * 5 && act_x < (room.x + room.width) * 5 && act_y < (room.y + room.height) * 5) return &room;
        return nullptr;
    };
    const auto cell_flags = [&](int at_x, int at_y) {
        const auto* room = room_at(at_x, at_y);
        if (!room) return 0x27;
        const Level& holder = *room->level;
        const int local_x = at_x + (where->world_x - holder.world_x) * 5, local_y = at_y + (where->world_y - holder.world_y) * 5;
        const int width = holder.ds1.width() * 5;
        if (local_x < 0 || local_y < 0 || local_x >= width || std::size_t(local_y) * std::size_t(width) + std::size_t(local_x) >= holder.walk.size()) return 0x27;
        const int flags = (holder.walk[std::size_t(local_y) * std::size_t(width) + std::size_t(local_x)] & 0x01) | items_at(&holder, local_x, local_y);
        return &holder == level ? flags | units_at(local_x, local_y) : flags;
    };
    const auto placed = drop_place(game_data->field, int(std::floor(x * 5)), int(std::floor(y * 5)), cell_flags);
    if (!placed) return std::nullopt;
    const auto [spot_x, spot_y] = *placed;
    const auto* room = room_at(spot_x, spot_y);
    const Level* holder = room ? room->level : where;
    return Landing{ holder, (float(spot_x) + 0.5f) / 5 + float(where->world_x - holder->world_x), (float(spot_y) + 0.5f) / 5 + float(where->world_y - holder->world_y) };
}

auto Loot::drop_at(float x, float y, const Level* onto) const -> std::pair<float, float> {
    const Level* where = onto ? onto : level;
    const auto spot = landing(x, y, onto);
    return { spot.x + float(spot.level->world_x - where->world_x), spot.y + float(spot.level->world_y - where->world_y) };
}

auto Loot::place(d2d::d2s::Item item, float x, float y, std::uint32_t now_ms) -> void {
    GroundItem ground_item;
    std::tie(ground_item.x, ground_item.y) = level->nearest_free(x, y);
    ground_item.now_ms = now_ms;
    const auto lines = item_lines(*game_data, item, int(character->stats.get(d2d::d2s::kLevel)));
    if (!lines.empty()) { ground_item.label = lines[0].text; ground_item.rgb = lines[0].rgb; }
    ground_item.item = std::move(item);
    ground_item.item.location = d2d::d2s::item_location::kGround;
    land(std::move(ground_item), now_ms);
}

auto Loot::land(GroundItem ground_item, std::uint32_t now_ms, const Level* onto) -> void {
    if (const auto info = game_data->rules.item_info.find(ground_item.item.code);
        info == game_data->rules.item_info.end() || info->second.flippy.empty()) return;   // nothing to show on the ground
    number(ground_item);
    if (onto && onto != ground_level) { kept[onto].push_back(std::move(ground_item)); return; }
    cues.cue("item_flippy", now_ms, ground_item.x, ground_item.y);
    const auto [sound, frame] = flippy_sound(*game_data, ground_item.item);
    cues.cue(sound, now_ms + std::uint32_t(frame) * 40, ground_item.x, ground_item.y);
    ground.push_back(std::move(ground_item));
}

auto Loot::number(GroundItem& ground_item) -> void {
    if (ground_item.item.id < 0) ground_item.item.id = next_id++;
    ground_item.id = ground_item.item.id;
}

auto Loot::take(std::size_t index, std::uint32_t now_ms) -> void {
    using namespace d2d::d2s;
    auto& ground_item = ground[index];
    if (ground_item.item.code == "gld") {
        const auto room = [](const Character& who) { return std::max<std::int64_t>(who.stats.get(kLevel) * 10000 - who.stats.get(kGold), 0); };
        // Shared (FUN_00540900) unless a player owns it (FUN_0055c850): with
        // the party alive on the level, each other member gets pile /
        // members (to what their purse holds), the picker the rest; what
        // the picker's purse can't hold stays.
        std::int64_t pile = ground_item.gold;
        if (const auto& sharers = gold_sharers && ground_item.owner == 0 ? gold_sharers() : std::vector<Character*>{}; !sharers.empty()) {
            const std::int64_t share = pile / std::int64_t(sharers.size() + 1);
            for (Character* other : sharers) {
                const auto given = std::min(share, room(*other));
                other->stats.values[kGold] += given;
                pile -= given;
            }
        }
        const auto taken = std::min(pile, room(*character));
        if (pile == ground_item.gold && taken <= 0) return;
        character->stats.values[kGold] += taken;
        cues.cue("item_gold", 0, player->x, player->y);
        if ((ground_item.gold = int(pile - taken)) > 0) { ground_item.label = std::to_string(ground_item.gold) + " Gold"; return; }
    } else {
        const auto& lay = game_data->inv_layout[std::size_t(std::max(character->character_class, 0))];
        const int boxes = game_data->belts[std::size_t(belt_index(*game_data, character->items))].boxes;
        const auto worn_by = wearer(*game_data, character->header.cls, character->items, character->stats);
        const auto taken = d2d::rules::pick_up(game_data->rules, character->items, ground_item.item, lay.cols ? lay.cols : 10, lay.rows ? lay.rows : 4, boxes,
                                               &worn_by, required_level(*game_data, ground_item.item, worn_by.cls));
        if (taken == d2d::rules::Pickup::kNoRoom) {
            static constexpr std::array<const char*, 7> kCantCarry{ "amazon_cantcarry_1", "sorceress_cantcarry_1", "necromancer_cantcarry_1",
                "paladin_cantcarry_1", "barbarian_cantcarry_1", "druid_cantcarry_1", "assassin_cant_carry" };
            d2d::log::info("no room for {}", ground_item.label);
            if (class_voice) class_voice(kCantCarry[std::size_t(std::clamp(character->character_class, 0, 6))], now_ms);
        }
        if (taken != d2d::rules::Pickup::kGone) return;
        settle_requirements(*game_data, *character);   // FUN_00563560 -> FUN_0055dbc0
        cues.cue("item_pickup", 0, player->x, player->y);
    }
    ground.erase(ground.begin() + std::ptrdiff_t(index));
}

}  // namespace d2d::game
