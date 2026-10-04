// SPDX-License-Identifier: GPL-3.0-or-later
// Loot: what kills drop (treasure classes, components/rules/drops.hpp) lying
// on each level's floor, drawn with each item's flippy, and picking it
// up into the purse or the inventory.
#pragma once

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"
#include "inventory.hpp"
#include "item_text.hpp"
#include "log.hpp"

#include <d2s_items.hpp>
#include <drops.hpp>
#include <monster_ids.hpp>
#include <rules.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

struct Loot {
    const GameData* game_data;
    const Level* const& level;             // Town's: where drops land
    Character& character;
    UnitState& player;
    d2d::rules::Rng& rng;
    Cues& cues;
    // An item on the ground (gold: code "gld", `gold` coins).
    struct GroundItem {
        int id = -1;                         // its unit id (the server's)
        d2d::d2s::Item item;
        int gold = 0;
        float x = 0, y = 0;
        std::uint32_t now_ms = 0;                // when it dropped: the flippy plays from here
        std::string label;
        std::array<std::uint8_t, 3> rgb{ 255, 255, 255 };
    };
    std::vector<GroundItem> ground;        // ground_level's
    int next_id = 1;                       // the next ground item's unit id
    // A ground item by unit id: its index in `ground`, or -1.
    [[nodiscard]] int index_of(int id) const {
        const auto found = std::ranges::find(ground, id, &GroundItem::id);
        return found == ground.end() ? -1 : int(found - ground.begin());
    }
    const Level* ground_level = nullptr;
    std::unordered_map<const Level*, std::vector<GroundItem>> kept;   // other levels' floors
    // The player went to `to`: what lies on its floor, the last level's kept.
    void enter(const Level* destination) {
        if (destination == ground_level) return;
        if (ground_level) kept[ground_level] = std::move(ground);
        ground = std::move(kept[destination]);
        ground_level = destination;
    }

    std::vector<bool> found_uniques;       // the game's one-per-game uniques (game +0x1b24)
    // A kill's loot (MonStats TreasureClass1 for the difficulty) round
    // where it fell, rolled off the monster's unit seed (+0x20: nothing
    // draws on it between the death and FUN_0055a6d0; Find Item rolls on
    // from where the kill left it). Magic and better come unidentified;
    // each lands where drop_at finds room.
    void drop(Monster& monster, d2d::rules::Rng& game_seed, std::uint32_t now_ms) {
        const int diff = character.header.active_difficulty();
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
        const auto& header = character.header;
        const bool quest_open = type_info.tc_quest_id && !quest_tc.empty() && !header.quest_flag(diff, type_info.tc_quest_id, 15)
                                && !header.quest_flag(diff, type_info.tc_quest_id, 1) && !header.quest_flag(diff, type_info.tc_quest_id, type_info.tc_quest_cp);
        const auto rolled = d2d::rules::tc_upgrade(game_data->rules, quest_open ? quest_tc : treasure_class, diff > 0 && !type_info.tc_fixed ? monster.stats.level : 0);
        // The killer's magic find (stat 80) and gold find (79) from its gear.
        // ponytail: one player; the player's kills only (no minion's owner,
        // FUN_0058f0d0), gear only (no skill / state find).
        int magic_find = 0, gold_find = 0;
        for (const auto& prop : gear_props(*game_data, character.items)) {
            if (prop.stat == d2d::d2s::kMagicFind) magic_find += prop.value;
            if (prop.stat == d2d::d2s::kGoldFind) gold_find += prop.value;
        }
        constexpr int kMost = 6;
        std::vector<d2d::rules::Drop> drops;
        d2d::rules::roll_drops(game_data->rules, rolled, monster.stats.level, monster.seed, drops, 1, magic_find, kMost);
        // FUN_0055a6d0 counts the drop before FUN_005589a0 (0x55af4c): the
        // one that fills the count leaves without gold find. A Hell Bovine's
        // drops are flagged 1 (FUN_0055a550): the Cow King set (29) drops.
        for (std::size_t i = 0; i < drops.size(); ++i)
            put(drops[i], monster.unit.x, monster.unit.y, monster.stats.level, game_seed, now_ms, monster.type == d2d::rules::monster_ids::kHellBovine,
                int(i) + 1 < kMost ? gold_find : 0);
    }
    // One drop round (x, y). A made item (FUN_00555230) takes two steps of
    // the game seed (+0xd0): its unit seed {low, 666} (FUN_00552df0), then
    // its own {low, 666} (FUN_00552e90); gold's coins come off the first.
    // It lands where drop_spot finds room from the dropper's subtile; gold
    // times (100 + gold_find) / 100 (FUN_005589a0).
    void put(const d2d::rules::Drop& dropped, float x, float y, int ilvl, d2d::rules::Rng& game_seed, std::uint32_t now_ms, bool bovine = false,
             int gold_find = 0) {
        {
            GroundItem ground_item;
            std::tie(ground_item.x, ground_item.y) = drop_at(x, y);
            ground_item.now_ms = now_ms;
            if (dropped.code == "gld") {
                ground_item.item.code = "gld";
                d2d::rules::Rng unit_seed{ game_seed.next() };
                game_seed.next();
                ground_item.gold = dropped.gold ? dropped.gold : d2d::rules::gold_amount(ilvl, dropped.mul, unit_seed) * (100 + gold_find) / 100;
                ground_item.label = std::to_string(ground_item.gold) + " Gold";
            } else {
                d2d::rules::Rng unit_seed{ game_seed.next() }, item_seed{ game_seed.next() };
                const int quality = dropped.quality ? dropped.quality : d2d::rules::stand_quality(game_data->rules, dropped.code, ilvl, item_seed);
                ground_item.item = d2d::rules::generate_item(game_data->rules, dropped.code, ilvl, quality, item_seed, &unit_seed, &found_uniques, bovine,
                                                            character.header.active_difficulty());
                ground_item.item.identified = quality <= 3;
                const auto lines = item_lines(*game_data, ground_item.item, int(character.stats.get(d2d::d2s::kLevel)));
                if (!lines.empty()) { ground_item.label = lines[0].text; ground_item.rgb = lines[0].rgb; }
            }
            land(std::move(ground_item), now_ms);
        }
    }
    // FUN_00555da0 on the level's walk grid: walls and object footprints
    // (0x01) block, as do items already lying there (0x200).
    // ponytail: units (0x1000/0x2000) don't block; one level, no neighbours'.
    [[nodiscard]] std::pair<float, float> drop_at(float x, float y) const {
        const int width = level->ds1.width() * 5, height = level->ds1.height() * 5;
        if (level->walk.empty()) return { x, y };
        const auto [spot_x, spot_y] = drop_spot(game_data->field, int(std::floor(x * 5)), int(std::floor(y * 5)), [&](int at_x, int at_y) {
            if (at_x < 0 || at_y < 0 || at_x >= width || at_y >= height) return 0x27;
            int flags = level->walk[std::size_t(at_y) * std::size_t(width) + std::size_t(at_x)] & 0x01;
            if (ground_level == level)
                for (const auto& lying : ground)
                    if (int(std::floor(lying.x * 5)) == at_x && int(std::floor(lying.y * 5)) == at_y) flags |= 0x200;
            return flags;
        });
        return { (float(spot_x) + 0.5f) / 5, (float(spot_y) + 0.5f) / 5 };
    }
    // An item the player drops (C→S 0x17, FUN_00563c00): at the nearest
    // free spot to (x, y) (FUN_00555da0), named as its tooltip names it.
    void place(d2d::d2s::Item item, float x, float y, std::uint32_t now_ms) {
        GroundItem ground_item;
        std::tie(ground_item.x, ground_item.y) = level->nearest_free(x, y);
        ground_item.now_ms = now_ms;
        const auto lines = item_lines(*game_data, item, int(character.stats.get(d2d::d2s::kLevel)));
        if (!lines.empty()) { ground_item.label = lines[0].text; ground_item.rgb = lines[0].rgb; }
        ground_item.item = std::move(item);
        ground_item.item.location = d2d::d2s::item_location::kGround;
        land(std::move(ground_item), now_ms);
    }
    // Onto the floor: its flippy plays and its drop sound at its drop frame.
    void land(GroundItem ground_item, std::uint32_t now_ms) {
        if (const auto info = game_data->rules.item_info.find(ground_item.item.code);
            info == game_data->rules.item_info.end() || info->second.flippy.empty()) return;   // nothing to show on the ground
        cues.cue("item_flippy", now_ms, ground_item.x, ground_item.y);
        if (const auto info = game_data->rules.item_info.find(ground_item.item.code); info != game_data->rules.item_info.end())
            cues.cue(info->second.drop_sound, now_ms + std::uint32_t(info->second.drop_frame) * 40, ground_item.x, ground_item.y);
        ground_item.id = next_id++;
        ground.push_back(std::move(ground_item));
    }

    // Picking up: gold into the purse (up to 10000 per character level),
    // an item stacked, into the belt or the inventory (rules::pick_up); no
    // room plays the class's "can't carry" (message 0x17: FUN_004cb9c0,
    // the class's sound table at 0x72a008, +0x14).
    // ponytail: the client's repeat guard on that voice (the same sound
    // again within 75 of FUN_0044db00's clock) isn't kept.
    void take(std::size_t index) {
        using namespace d2d::d2s;
        auto& ground_item = ground[index];
        if (ground_item.item.code == "gld") {
            const auto cap = character.stats.get(kLevel) * 10000, room = std::max<std::int64_t>(cap - character.stats.get(kGold), 0);
            const auto taken = std::min<std::int64_t>(ground_item.gold, room);
            if (taken <= 0) return;
            character.stats.values[kGold] += taken;
            cues.cue("item_gold", 0, player.x, player.y);
            if ((ground_item.gold -= int(taken)) > 0) { ground_item.label = std::to_string(ground_item.gold) + " Gold"; return; }
        } else {
            const auto& lay = game_data->inv_layout[std::size_t(std::max(character.character_class, 0))];
            const int boxes = game_data->belts[std::size_t(belt_index(*game_data, character.items))].boxes;
            const auto taken = d2d::rules::pick_up(game_data->rules, character.items, ground_item.item, lay.cols ? lay.cols : 10, lay.rows ? lay.rows : 4, boxes);
            if (taken == d2d::rules::Pickup::kNoRoom) {
                static constexpr std::array<const char*, 7> kCantCarry{ "amazon_cantcarry_1", "sorceress_cantcarry_1", "necromancer_cantcarry_1",
                    "paladin_cantcarry_1", "barbarian_cantcarry_1", "druid_cantcarry_1", "assassin_cant_carry" };
                d2d::log::info("no room for {}", ground_item.label);
                cues.cue(kCantCarry[std::size_t(std::clamp(character.character_class, 0, 6))], 0, player.x, player.y);
            }
            if (taken != d2d::rules::Pickup::kGone) return;
            cues.cue("item_pickup", 0, player.x, player.y);
        }
        ground.erase(ground.begin() + std::ptrdiff_t(index));
    }

};

}  // namespace d2d::game
