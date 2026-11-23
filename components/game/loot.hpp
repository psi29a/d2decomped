// SPDX-License-Identifier: GPL-3.0-or-later
// Loot: what kills drop (treasure classes, components/rules/drops.hpp) lying
// on each level's floor, drawn with each item's flippy, and picking it
// up into the purse or the inventory.
#pragma once

#include "ai/units.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"
#include "inventory.hpp"
#include "item_text.hpp"
#include "log.hpp"

#include <d2s_items.hpp>
#include <drops.hpp>
#include <monster_ids.hpp>
#include <replenish.hpp>
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

struct Loot {
    const GameData* game_data;
    const Level* const& level;             // ClientSession's: where drops land
    d2d::rules::Rng& rng;
    Cues& cues;
    Character* character = nullptr;        // the acting player's (World::act_as)
    UnitState* player = nullptr;
    // Who stands on this level (the Crowd). Their 0x1000 / 0x2000 are not
    // in Level::walk. Null: no unit footprints (a level the player isn't on).
    const Crowd* standing = nullptr;
    // The picker's party alive on the level besides them (World's): they
    // share the gold picked up.
    std::function<std::vector<Character*>()> gold_sharers;
    std::function<void(std::string_view, std::uint32_t)> class_voice;   // Fight::class_voice
    // An item on the ground (gold: code "gld", `gold` coins).
    struct GroundItem {
        int id = -1;                         // its unit id (the server's): item.id's, set as it lands
        d2d::d2s::Item item;
        int gold = 0;
        float x = 0, y = 0;
        std::uint32_t now_ms = 0;                // when it dropped: the flippy plays from here
        std::string label;
        std::array<std::uint8_t, 3> rgb{ 255, 255, 255 };
        int dropped_by = 0;                  // gold a player (Hero::id) just dropped: autoloot leaves it to them till they step off it
        int owner = 0;                       // gold a player dropped (FUN_00535510 -> FUN_00621ce0): taken whole, not shared
    };
    std::vector<GroundItem> ground;        // ground_level's
    // The game's next item unit id, carried or lying (game +0xa0: one
    // counter per unit type, FUN_00552ee0); World::enter starts it at 1.
    int next_id = 1;
    // A ground item by unit id: its index in `ground`, or -1.
    [[nodiscard]] int index_of(int id) const {
        const auto found = std::ranges::find(ground, id, &GroundItem::id);
        return found == ground.end() ? -1 : int(found - ground.begin());
    }
    const Level* ground_level = nullptr;
    std::unordered_map<const Level*, std::vector<GroundItem>> kept;   // other levels' floors
    // The player went to `to`: what lies on its floor, the last level's kept.
    void enter(const Level* destination);

    std::vector<bool> found_uniques;       // the game's one-per-game uniques (game +0x1b24)
    int blessed_find = 0;                  // the wisps' blessing (stat 0x50) till blessed_until
    std::uint32_t blessed_until = 0;
    // A kill's loot (MonStats TreasureClass1 for the difficulty) round
    // where it fell, rolled off the monster's unit seed (+0x20: nothing
    // draws on it between the death and FUN_0055a6d0; Find Item rolls on
    // from where the kill left it). Magic and better come unidentified;
    // each lands where drop_at finds room.
    // `players` in the game, `nearby` of the killer's party on its level (NoDrop);
    // `merc_find` / `merc_gold`: the merc's own when it made the kill.
    void drop(Monster& monster, d2d::rules::Rng& game_seed, std::uint32_t now_ms, int players = 1, int nearby = 1, int merc_find = 0, int merc_gold = 0);
    // One drop round (x, y). A made item (FUN_00555230) takes two steps of
    // the game seed (+0xd0): its unit seed {low, 666} (FUN_00552df0), then
    // its own {low, 666} (FUN_00552e90); gold's coins come off the first.
    // It lands where drop_spot finds room from the dropper's subtile; gold
    // times (100 + gold_find) / 100 (FUN_005589a0). `onto`: a level the
    // player isn't on (the forge's gems), its floor kept.
    // False, nothing made and no seed step taken, when the spot search finds
    // no room (FUN_00555da0 returns 0, FUN_0055a550 makes no item).
    bool put(const d2d::rules::Drop& dropped, float x, float y, int ilvl, d2d::rules::Rng& game_seed, std::uint32_t now_ms, bool bovine = false,
             int gold_find = 0, const Level* onto = nullptr) {
        {
            GroundItem ground_item;
            const auto found = find_landing(x, y, onto);
            if (!found) return false;
            const auto& spot = *found;
            ground_item.x = spot.x;
            ground_item.y = spot.y;
            const Level* lands_on = spot.level == (onto ? onto : level) ? onto : spot.level;   // a neighbour's room: its floor
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
                                                            character->header.active_difficulty(), dropped.special);
                ground_item.item.identified = quality <= 3;
                d2d::rules::replenish_arm(game_data->rules, ground_item.item);   // FUN_00558d90's end (0x559117, 0x559120)
                const auto lines = item_lines(*game_data, ground_item.item, int(character->stats.get(d2d::d2s::kLevel)));
                if (!lines.empty()) { ground_item.label = lines[0].text; ground_item.rgb = lines[0].rgb; }
            }
            land(std::move(ground_item), now_ms, lands_on);
        }
        return true;
    }
    // FUN_00555da0 on the walk grids of the rooms FUN_0064ca50 finds
    // (FUN_00463740): the dropper's room and its near list (automap_rooms:
    // the next level's close rooms and a warp's room too), 0x27 off them.
    // Walls and object footprints (0x01) block, as do items already lying
    // there (0x200). On this level, a unit's footprint blocks too
    // (FUN_0064ea90, UnitShape::bit): 0x1000 for pattern 1 / 2, 0x2000 for
    // pattern 3 / 4, on the subtile, or the plus for pattern 2 / 4. The walk
    // grid does not hold those bits. `level`: the level whose room took the
    // spot (`onto`'s, this one's, or a neighbour's), (x, y) in its cells.
    struct Landing { const Level* level; float x, y; };
    [[nodiscard]] Landing landing(float x, float y, const Level* onto = nullptr) const {
        return find_landing(x, y, onto).value_or(Landing{ onto ? onto : level, x, y });
    }
    // landing, or none where FUN_00555da0 finds no room (drop_place): FUN_0055a550
    // then makes no item.
    [[nodiscard]] std::optional<Landing> find_landing(float x, float y, const Level* onto = nullptr) const;
    // landing's spot in `onto`'s cells (this level's), past its edge when a
    // neighbour's room took it.
    [[nodiscard]] std::pair<float, float> drop_at(float x, float y, const Level* onto = nullptr) const;
    // 0x1000 / 0x2000 where `standing` stamps them on subtile (at_x, at_y).
    // Pattern 0 and 5 stamp nothing (UnitShape::stamps). A pattern 0 probe
    // asks Crowd for that one subtile, including a pattern 2 / 4 plus.
    [[nodiscard]] int units_at(int at_x, int at_y) const {
        if (!standing) return 0;
        const float cell_x = (float(at_x) + 0.5f) / 5, cell_y = (float(at_y) + 0.5f) / 5;
        static constexpr UnitShape kBit1000{ 0, 0, 0x1000 };
        static constexpr UnitShape kBit2000{ 0, 0, 0x2000 };
        int bits = 0;
        if (standing->at(cell_x, cell_y, kBit1000, nullptr)) bits |= 0x1000;
        if (standing->at(cell_x, cell_y, kBit2000, nullptr)) bits |= 0x2000;
        return bits;
    }
    // 0x200 when an item lies on subtile (at_x, at_y) of `where`, else 0.
    [[nodiscard]] int items_at(const Level* where, int at_x, int at_y) const {
        const auto kept_floor = kept.find(where);
        if (where != ground_level && kept_floor == kept.end()) return 0;
        const auto& lying_items = where == ground_level ? ground : kept_floor->second;
        return std::ranges::any_of(lying_items, [&](const GroundItem& lying) { return int(std::floor(lying.x * 5)) == at_x && int(std::floor(lying.y * 5)) == at_y; }) ? 0x200 : 0;
    }
    // An item the player drops (C→S 0x17, FUN_00563c00): at the nearest
    // free spot to (x, y) (FUN_00555da0), named as its tooltip names it.
    void place(d2d::d2s::Item item, float x, float y, std::uint32_t now_ms);
    // Onto the floor: its flippy plays and its drop sound at its drop frame
    // (onto another level's: kept, unseen and unheard).
    void land(GroundItem ground_item, std::uint32_t now_ms, const Level* onto = nullptr);
    // Its unit id: a dropped item keeps its own, a new one takes the next.
    void number(GroundItem& ground_item);

    // Picking up: gold into the purse (up to 10000 per character level),
    // an item stacked, into the belt or the inventory (rules::pick_up); no
    // room plays the class's "can't carry" (message 0x17: FUN_004cb9c0,
    // the class's sound table at 0x72a008, +0x14) through class_voice, so
    // the same line again within 75 client frames (FUN_0044db00) is skipped.
    void take(std::size_t index, std::uint32_t now_ms);

};

}  // namespace d2d::game
