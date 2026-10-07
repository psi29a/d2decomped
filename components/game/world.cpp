// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for world.hpp: the World, the game server and its 25 Hz tick.
#include "world.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "inventory.hpp"
#include "log.hpp"
#include "npc_menu.hpp"
#include "protocol.hpp"

#include <combat.hpp>
#include <d2s_items.hpp>
#include <drops.hpp>
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::game {

// Levels.txt Waypoint: a level's index in the waypoint bits (FUN_00660e00), -1 none.
// `level`'s units states, the ones its rooms made since (populate) added as they start.
static void grow_states(std::vector<UnitState>& states, const Level& level) {
    if (states.size() >= level.npcs.size()) return;
    const auto fresh = npc_start(level);
    states.insert(states.end(), fresh.begin() + std::ptrdiff_t(states.size()), fresh.end());
}

static int waypoint_index(const GameData& game_data, int level_id) {
    for (const auto& act_levels : game_data.waypoint_levels)
        if (const auto found = std::ranges::find(act_levels, level_id, &GameData::WaypointLevel::level); found != act_levels.end()) return found->waypoint;
    return -1;
}

// FUN_00463740: whether subtile (x, y) lies in the room holding subtile
// (from_x, from_y) or in one of that room's near list (FUN_00619790).
// ponytail: rooms as gamedata.cpp's room_rects has them (a preset level's
// as 8x8 tiles), the near list as its closeness (FUN_0066bc20: under 6
// tiles apart on both axes); the level's own rooms only. Rooms are agent
// D's (units off a level's rooms).
static bool room_near_holds(const Level& level, int from_x, int from_y, int x, int y) {
    struct Rect { int x, y, width, height; };
    std::vector<Rect> rects;
    for (const auto& room : level.rooms) rects.push_back({ room.x, room.y, room.width, room.height });
    if (rects.empty())
        for (int top = 0; top < level.ds1.height(); top += 8)
            for (int left = 0; left < level.ds1.width(); left += 8) rects.push_back({ left, top, std::min(8, level.ds1.width() - left), std::min(8, level.ds1.height() - top) });
    const auto holds = [](const Rect& rect, int sub_x, int sub_y) {
        return sub_x >= rect.x * 5 && sub_y >= rect.y * 5 && sub_x < (rect.x + rect.width) * 5 && sub_y < (rect.y + rect.height) * 5;
    };
    const auto own = std::ranges::find_if(rects, [&](const Rect& rect) { return holds(rect, from_x, from_y); });
    if (own == rects.end()) return false;
    return std::ranges::any_of(rects, [&](const Rect& rect) {
        const int gap_x = own->x < rect.x ? rect.x - own->width - own->x : own->x - rect.width - rect.x;
        const int gap_y = own->y < rect.y ? rect.y - own->height - own->y : own->y - rect.height - rect.y;
        return gap_x < 6 && gap_y < 6 && holds(rect, x, y);
    });
}

auto World::quests() -> d2d::rules::QuestBits& { return character.header.quests[std::size_t(std::clamp(character.header.active_difficulty(), 0, 2))]; }

auto World::item_ids() -> void {
        for (auto& item : character.items) if (item.id < 0) item.id = next_item_id++;
        if (held && held->id < 0) held->id = next_item_id++;
    }

auto World::monster_colour(const Monster& monster) const -> int {
        const auto& npc = monster.npc;
        const int difficulty_index = std::clamp(monster.difficulty, 0, 2);
        int colour = npc.trans_lvl < 8 ? npc.trans_lvl + 2 : 2;
        if ((monster.boss == d2d::rules::Boss::unique || monster.boss == d2d::rules::Boss::superunique) && !npc.no_unique_shift)
            colour = int(std::uint32_t(monster.id) * 2654435761u % 30u) + 9;
        if (const int unique_trans = npc.utrans[std::size_t(difficulty_index)]; unique_trans != 0) colour = unique_trans == 255 ? 1 : unique_trans;
        if (monster.boss == d2d::rules::Boss::superunique && monster.super >= 0 && std::size_t(monster.super) < game_data->superuniques.size())
            if (const int unique_trans = game_data->superuniques[std::size_t(monster.super)].utrans[std::size_t(difficulty_index)]; unique_trans != 0) colour = unique_trans;
        return colour >= 30 ? 2 : colour;
    }

auto World::view() const -> View {
        View view;
        view.level = level;
        view.player = player;
        view.running = running;
        view.dead = fight.dead();
        view.poisoned = std::ranges::any_of(fight.regen, [](const Fight::Regen& regen_entry) { return regen_entry.poison; });
        view.chilled = fight.chill_rate(now) != 0;
        view.pmode = fight.pmode;
        view.prate = fight.prate;
        view.seq.assign(fight.seq.begin(), fight.seq.end()); view.seq_frame_ms = fight.seq_frame_ms; view.seq_loop = fight.seq_loop;
        view.gfx = fight.gfx();
        if (merc && merc_npc) view.merc = View::Merc{ merc->unit, merc_npc, merc->mode };
        for (const auto& pet : fight.pets) if (pet.where == level) view.pets.push_back({ pet.monster.npc, pet.monster.unit, pet.monster.mode });
        if (level == fight.mon_level) {
            // What's near the player (D2 sends the units of the rooms round
            // each player). ponytail: a radius of 28 cells, not rooms; waits for
            // networking (what a client is sent).
            for (const auto& monster : fight.monsters)
                if (std::abs(monster.unit.x - player.x) < 28 && std::abs(monster.unit.y - player.y) < 28) {
                    view.monsters.push_back(monster);
                    view.monsters.back().npc.colour = monster_colour(monster);
                }
            for (const auto& missile : fight.missiles) if (missile.info) view.missiles.push_back({ missile.info, missile.x, missile.y, missile.dir, missile.born });
        }
        if (fight.attack_mon >= 0 && std::size_t(fight.attack_mon) < fight.monsters.size()) view.attack = fight.monsters[std::size_t(fight.attack_mon)].id;
        view.attack_skill = fight.attack_skill;
        if (loot.ground_level == level) view.ground = loot.ground;
        for (const auto& fire : fires) if (fire.level == level) view.fires.push_back({ fire.x, fire.y, fire.npc });
        for (std::size_t k = 0; k < corpses.size(); ++k)
            if (corpses[k].level == level) view.corpses.push_back({ corpses[k].x, corpses[k].y, corpses[k].dir, corpses[k].gfx, int(k) });
        for (int k = 0; k < 4; ++k)
            if (portal[std::size_t(k)].level == level)
                view.portals.push_back({ portal[std::size_t(k)].x, portal[std::size_t(k)].y, k >= 2 ? portal[std::size_t(5 - k)].level ? portal[std::size_t(5 - k)].level->id : d2d::rules::CainQuest::kTristram : portal[std::size_t(1 - k)].level->id, portal[std::size_t(k)].born, k });
        if (cain_portal.level == level) view.portals.push_back({ cain_portal.x, cain_portal.y, 1, cain_portal.born, 4 });
        view.npc_states = npc_states;
        grow_states(view.npc_states, *level);
        view.npc_states.resize(level->npcs.size());
        for (const auto& neighbour : level->nearby) {           // then the neighbours', Level::nearby order
            const auto found = other_npcs.find(neighbour.level);
            auto states = found != other_npcs.end() ? found->second : npc_start(*neighbour.level);
            grow_states(states, *neighbour.level);
            states.resize(neighbour.level->npcs.size());
            view.npc_states.insert(view.npc_states.end(), states.begin(), states.end());
        }
        if (now < fight.boost.until) { view.boost = fight.boost.stats; view.boost_code = shrine_code(fight.boost.shrine); }
        view.aura = fight.aura;
        view.gold_lost = int(gold_lost);
        for (const auto& state : fight.self_states) view.buffs.push_back(state.skill);
        view.day = day;
        view.den_cleared = den.state >= 4;
        {   // FUN_00544190: a record's +0xe8 if it has one (Tools), else FUN_00543f90
            // ponytail: worked out as the view's built, not sent as it changes;
            // the send's skip for done quests (bits 0 / 15 without 13 / 14) reads as 0;
            // waits for networking (deltas).
            const auto& quest_bits = character.header.quests[std::size_t(std::clamp(character.header.active_difficulty(), 0, 2))];
            const int clvl = int(character.stats.get(d2d::d2s::kLevel));
            view.quest_log = { 0, std::uint8_t(den.log_state(quest_bits)), std::uint8_t(burial.log_state(quest_bits)),
                               std::uint8_t(tools.log_state(quest_bits, holding_malus(), clvl)), std::uint8_t(cain.log_state(quest_bits)),
                               std::uint8_t(tower.log_state(quest_bits)), std::uint8_t(andy.log_state(quest_bits)) };
            for (int quest = 1; quest < 7; ++quest) {
                const bool bit0 = d2d::rules::qbit(quest_bits, quest, 0) || d2d::rules::qbit(quest_bits, quest, 15);
                if (bit0 && !d2d::rules::qbit(quest_bits, quest, 13) && !d2d::rules::qbit(quest_bits, quest, 14)) view.quest_log[std::size_t(quest)] = 0;
            }
            const bool done[7] = { false, den.state >= 4, burial.state >= 4, tools.game_returned, cain.rescued_flag, tower.dead, andy.log == 0xd };
            for (std::size_t quest = 1; quest < 7; ++quest)
                view.game_quests[quest] = std::uint16_t((done[quest] ? 1 << 13 : 0) | (closed_at_join[quest] ? 1 << 15 : 0));
        }
        view.den_left = std::max(den_left, 0);
        {
            d2d::rules::StatSum sum{};
            (void)fight.player_fighter(nullptr, nullptr, nullptr, &sum);
            view.light_bonus = sum.size() > d2d::d2s::kLightRadius ? int(sum[d2d::d2s::kLightRadius]) : 0;
            view.attack_lines = character.panel.attack;
            if (store.npc >= 0) {
                view.store = store;
                view.store->reduced = int(sum[d2d::d2s::kReducedPrices]);   // the player's gear: off what the store charges
            }
        }
        view.has_character = true;
        view.header = character.header; view.stats = character.stats; view.items = character.items; view.held = held;
        view.hire_offers = hire_offers;
        return view;
    }

auto World::save() -> std::string {
        if (!characters) return "no character store";
        stow_held(*game_data, character.items, held);                  // the item in hand goes back first
        auto header = character.header;
        header.level = std::uint8_t(std::clamp<std::int64_t>(character.stats.get(d2d::d2s::kLevel), 1, 99));
        header.last_played = std::uint32_t(std::time(nullptr));
        header.map_id = game_data->map_seed;
        if (character.appearance)   // the save's tints leave states out (FUN_0062c100's state loop sets no tint byte)
            header.set_look(game_data->item_pieces.empty() || game_data->comp.empty() ? *character.appearance : game_data->look_of(character.items));
        // The corpse list holds one: the latest corpse's items (PlrSave2.cpp).
        // ponytail: game.exe's pick among several isn't traced.
        const std::vector<d2d::d2s::Item> no_corpse;
        const auto err = characters->save(header, fight.own_stats(), character.items, corpses.empty() ? &no_corpse : &corpses.back().items);
        if (err.empty()) character.header.last_played = header.last_played;
        d2d::log::info("save {}: {}", header.name, err.empty() ? "written" : err);
        return err;
    }

// A blast's damage taken (FUN_0057c6c0): life under one point after it is 0.
static void take_blast(std::int64_t& life, int taken) {
    if (taken <= 0) return;
    life -= taken;
    if (life < 256) life = 0;
}

// FUN_00622b50 (mask 0x804): rules::sight_blocked from the unit to the
// object over the walk grid's missile barrier (0x04) and doors (0x800).
auto World::blast(const Npc& object, float x, float y, int size, std::int64_t life, int unit_level, int dexterity, int defense) -> int {
    if (level == &game_data->town) return 0;
    const auto wall = [&](int at_x, int at_y) { return level->blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x804); };
    const auto subtile = [](float cell) { return int(std::floor(cell * 5)); };
    if (d2d::rules::sight_blocked(subtile(x), subtile(y), size, subtile(object.x), subtile(object.y), object.size_x, wall)) return 0;
    return d2d::rules::object_blast(life, unit_level, dexterity, defense, object.seed);
}

auto World::operate(int npc_index, std::uint32_t now_ms, int force ) -> void {   // force (devctl): the shrine row / trap to play
        using namespace d2d::d2s;
        const auto& object = level->npcs[std::size_t(npc_index)];
        const int diff = character.header.active_difficulty();
        if (object.operate_fn == d2d::rules::ToolsQuest::kStand) { malus_stand(npc_index, now_ms); return; }
        if (is_door(object.operate_fn)) { operate_door(npc_index, now_ms); return; }
        if (object.operate_fn == d2d::rules::operate_fn::kExplodingBarrel) { explode(npc_index, now_ms); return; }
        const auto& area_levels = game_data->area_level;
        auto alvl = [&](int id) { return std::size_t(id) < area_levels.size() ? area_levels[std::size_t(id)][std::size_t(std::clamp(diff, 0, 2))] : 1; };
        const int here = alvl(level->id);
        // Open a container (open_container) on the level's object seed, its
        // rounds the act's chest class (chest_round) off its own unit seed;
        // the items are made at the area level (FUN_0055a550), the extras too.
        auto open = [&] {
            const auto [low, high] = d2d::rules::kChestLevels[0];
            const auto treasure_class = d2d::rules::chest_tc(0, diff, here, alvl(low), alvl(high));
            std::vector<d2d::rules::Drop> drops;
            const auto opened = d2d::rules::open_container(object.operate_fn, object.object_id, object.locked, object.sparkle, fight.spawning.objects,
                                                           [&](int forced) { return d2d::rules::chest_round(game_data->rules, treasure_class, object.seed, drops, forced); });
            for (const auto& code : opened.extra) drops.push_back({ .code = code });
            if (!fight.remote_monsters)              // a joined game: the host drops the chest's items (S->C 0x9c)
                for (const auto& dropped : drops) loot.put(dropped, object.x, object.y, here, fight.spawning.game, now_ms);
            return std::pair{ opened, drops.size() };
        };
        auto to_mode = [&](int mode) {               // FUN_00624690, its sound (0x7295f8) and footprint (FUN_00623830)
            set_footprint(*level, object, object.collision >> mode & 1);
            if (const auto sound = d2d::rules::object_sound(object.object_id, mode); !sound.empty()) cues.cue(sound, now_ms, object.x, object.y);
        };
        // A trap object (30: "a trap", the exploding chest; FUN_00581cd0), in
        // mode 0: a physical then a fire blast on its opener (FUN_005dfa00
        // types 0, 1: blast), then mode 1; its event 1 (FUN_005417d0,
        // FrameCnt1 + 1 frames on) takes it to mode 2, the tick's OP -> ON.
        if (object.operate_fn == d2d::rules::operate_fn::kTrapObject) {
            if (operated.contains({ level, npc_index })) return;
            const auto& fighter = fight.player_combat;
            auto& life = character.stats.values[d2d::d2s::kLife];
            for (int kind = 0; kind < 2; ++kind) {
                const int damage = blast(object, player.x, player.y, 2, life, int(character.stats.get(kLevel)),
                                         int(character.stats.get(kDex) + fight.psum[kDex]), fighter.defense);
                take_blast(life, kind ? d2d::rules::blast_taken(damage, fighter.mdr, fighter.res[0]) : d2d::rules::blast_taken(damage, fighter.dr_flat, fighter.dr_pct));
            }
            operated[{ level, npc_index }] = now_ms;
            to_mode(1);
            d2d::log::info("trap object {} went off", npc_index);
            return;
        }
        if (object.operate_fn == d2d::rules::operate_fn::kWell) {               // a well (FUN_005858a0): 2 x Parm2 drinks (InitFn 16), NU -> OP -> ON
            auto& well = doors.try_emplace({ level, npc_index }, Door{ 0, 0 }).first->second;
            auto& stat_values = character.stats.values;
            if (well.mode >= 2) return;
            const bool drank = d2d::rules::well_drink(stat_values[kLife], stat_values[kMaxLife], stat_values[kMana], stat_values[kMaxMana],
                                                      stat_values[kStamina], stat_values[kMaxStamina]);
            if (!fight.cure(now_ms) && !drank) return;   // a cure counts as a drink too
            well = { well.mode + 1, now_ms };
            to_mode(well.mode);
            d2d::log::info("well {}: mode {}", npc_index, kObjectModes[std::size_t(well.mode)]);
            return;
        }
        // Containers (open_container): a casket (1) opens only when its round
        // drops. Caskets and barrels raise one of the level's undead
        // (FUN_005474c0 / FUN_00582280); all but the barrel spring their trap
        // (InitFn 2's; FUN_00582510: arm_trap). Opened, mode 1; event 1
        // (FUN_005417d0, FrameCnt1 + 1 frames on) takes it to mode 2: the
        // tick's OP -> ON.
        // ponytail: the barrel's opening step for a player (a swing at it:
        // FUN_006439b0 / FUN_00580a70) isn't here; it waits on the player's
        // modes in fight.cpp (agent G's).
        if (const int operate_id = object.operate_fn; operate_id == d2d::rules::operate_fn::kCasket || operate_id == d2d::rules::operate_fn::kUrn || operate_id == d2d::rules::operate_fn::kBarrel || operate_id == d2d::rules::operate_fn::kCorpse) {
            const auto [opened, drops] = open();
            if (!opened.opened) return;
            operated[{ level, npc_index }] = now_ms;
            to_mode(1);
            if (opened.undead) spring_trap(8, object.x, object.y, here, now_ms, 1);
            if (const int trap = force >= 0 ? force : object.trap; trap && operate_id != d2d::rules::operate_fn::kBarrel) arm_trap(trap, object.x, object.y, here, now_ms);
            d2d::log::info("opened object {} (op {})", npc_index, operate_id);
            return;
        }
        // An armor stand's armor (19) or a weapon rack's weapon (20)
        // (stand_item) off its room's seed at the area level less one, made
        // off that seed too (FUN_00558d90), its quality rolled (Drop quality
        // 0: stand_quality); a bookshelf (26) its book (open_container).
        if (const int operate_id = object.operate_fn; operate_id == d2d::rules::operate_fn::kArmorStand || operate_id == d2d::rules::operate_fn::kWeaponRack || operate_id == d2d::rules::operate_fn::kBookshelf) {
            operated[{ level, npc_index }] = now_ms;
            to_mode(2);
            const int ilvl = here > 1 ? here - 1 : here;
            if (operate_id == d2d::rules::operate_fn::kBookshelf) open();
            else if (auto& room_seeds = fight.spawning.levels[level].room_seeds; std::size_t(object.room) < room_seeds.size()) {
                auto& room_seed = room_seeds[std::size_t(object.room)];
                if (const auto code = d2d::rules::stand_item(game_data->rules, operate_id == d2d::rules::operate_fn::kWeaponRack, ilvl, room_seed); !code.empty())
                    loot.put({ .code = code, .quality = 0 }, object.x, object.y, ilvl, room_seed, now_ms);
            }
            d2d::log::info("opened object {} (op {})", npc_index, operate_id);
            return;
        }
        if (object.operate_fn == d2d::rules::operate_fn::kChest && object.locked) {           // a key from the inventory (FUN_0055f140: item type key)
            const auto key = std::ranges::find_if(character.items, [](const Item& item) { return item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory && item.code == "key"; });
            if (key == character.items.end()) { d2d::log::info("I need a key."); return; }
            if (key->quantity > 1) --key->quantity;
            else character.items.erase(key);
        }
        // The Moldy Tome (OperateFn 6, FUN_00594e70): opens once (mode 0 → 1),
        // read every time while the quest's on: message 127 to the player
        // (FUN_005456a0: S->C 0x27, mode 2, the tome's unit id, +10 the
        // message), which the client plays in the speech box (FUN_004a1600
        // → FUN_004a1320) and answers as heard (cmd::QuestMessage).
        if (object.operate_fn == d2d::rules::operate_fn::kMoldyTome) {
            operated.try_emplace({ level, npc_index }, now_ms);
            if (!closed_at_join[std::size_t(d2d::rules::TowerQuest::kQuest)]) events.push_back(ev::Speech{ npc_index, d2d::rules::TowerQuest::kTome });
            return;
        }
        if (object.operate_fn == d2d::rules::operate_fn::kCairnStone || object.operate_fn == d2d::rules::operate_fn::kGibbet || object.operate_fn == d2d::rules::operate_fn::kInifussTree) { cain_operate(npc_index, now_ms); return; }
        operated[{ level, npc_index }] = now_ms;
        if (object.operate_fn == d2d::rules::operate_fn::kChest) {
            const auto drops = open().second;
            d2d::log::info("opened a chest: {} drops{}", drops, object.trap ? std::format(", trap {}", object.trap) : "");
            if (const int trap = force >= 0 ? force : object.trap) arm_trap(trap, object.x, object.y, here, now_ms);
            return;
        }
        const int row = force >= 0 ? force : object.shrine;
        if (std::size_t(row) >= game_data->shrines.size()) return;
        const auto& shrine = game_data->shrines[std::size_t(row)];
        auto& stat_values = character.stats.values;
        d2d::rules::shrine_recharge(shrine, stat_values[kLife], stat_values[kMaxLife], stat_values[kMana], stat_values[kMaxMana]);
        // A booster's state ends the one before (curse = 1, FUN_0056e970);
        // the stamina state's end (FUN_00583a40) and its start fill stamina.
        if (auto boost = d2d::rules::shrine_boost(shrine, fight.player_combat.attack_rating); !boost.empty()) {
            if (shrine.code == 14 || shrine_code(fight.boost.shrine) == 14) stat_values[kStamina] = stat_values[kMaxStamina];
            fight.boost = { row, std::move(boost), now_ms + std::uint32_t(shrine.duration) * 40u };
        }
        if (const auto sound = d2d::rules::shrine_sound(shrine.code); !sound.empty()) cues.cue(sound, now_ms, object.x, object.y);
        if (shrine.code == 17) open_portal_at(player.x + 1, player.y + 1, now_ms);   // portal (FUN_00582a30): 5 subtiles on each axis
        if (shrine.code == 18)                                  // gem: one up, or a chipped gem at the player's feet
            if (const auto code = d2d::rules::gem_shrine(game_data->rules, character.items, rng); !code.empty())
                loot.put({ .code = code }, player.x, player.y, 1, fight.spawning.game, now_ms);
        // Storm (FUN_00582da0): everyone about loses Arg0 % of their life.
        // ponytail: "about" as within 30 cells (game.exe's unit search
        // over Arg1 isn't traced).
        if (shrine.code == 19) {
            stat_values[kLife] -= stat_values[kLife] * shrine.arg0 / 100;
            merc->hit_points -= merc->hit_points * shrine.arg0 / 100;
            if (fight.mon_level == level)
                for (auto& monster : fight.monsters)
                    if (monster.alive() && std::abs(monster.unit.x - object.x) < 30 && std::abs(monster.unit.y - object.y) < 30) monster.hit_points -= monster.hit_points * shrine.arg0 / 100;
        }
        // Exploding / Poison: Arg0 .. Arg1 - 1 exploding (opm) / choking gas
        // (gpm) potions at the player's feet (FUN_005830e0 / FUN_00583410).
        if (shrine.code == 21 || shrine.code == 22)
            for (int count = shrine.arg0 + rng(std::max(shrine.arg1 - shrine.arg0, 0)); count > 0; --count)
                loot.put({ .code = shrine.code == 21 ? "opm" : "gpm" }, player.x, player.y, 1, fight.spawning.game, now_ms);
        if ((shrine.code == 19 || shrine.code == 21 || shrine.code == 22) && fight.mon_level == level)
            fight.shrine_missiles(shrine.code, object.x, object.y, int(character.stats.get(kLevel)), now_ms);
        if (shrine.code == 20 && fight.mon_level == level) {   // warping (FUN_00583050): the nearest plain monster turns boss
            // ponytail: FUN_00582750's filter read as alive, not a boss, not
            // an NPC; FUN_0065a800's search range isn't traced.
            int best = -1;
            float best_distance = 1e9f;
            for (std::size_t k = 0; k < fight.monsters.size(); ++k) {
                const auto& monster = fight.monsters[k];
                const float distance = std::hypot(monster.unit.x - player.x, monster.unit.y - player.y);
                if (monster.alive() && monster.boss == d2d::rules::Boss::none && distance < best_distance) { best_distance = distance; best = int(k); }
            }
            if (best >= 0) {                               // FUN_005a4940: FUN_005a0760 (champions allowed), FUN_005a2120
                auto& monster = fight.monsters[std::size_t(best)];
                const auto boss = d2d::rules::roll_boss(game_data->umods, game_data->monsters.types[std::size_t(monster.type)], diff, true, rng);
                make_boss(*game_data, monster, boss.kind, boss.mods, -1, boss.name_seed, diff, rng);
                d2d::log::info("warping shrine: {} is now a {}", monster.npc.name, boss.kind == d2d::rules::Boss::champion ? "champion" : "unique");
            }
        }
        d2d::log::info("shrine {} (code {})", row, shrine.code);
    }

auto World::operate_door(int npc_index, std::uint32_t now_ms) -> void {
        const auto& door = level->npcs[std::size_t(npc_index)];
        auto& state = doors.try_emplace({ level, npc_index }, Door{ mode_index(door.mode), 0 }).first->second;
        int mode = -1;
        if (door.operate_fn == d2d::rules::operate_fn::kSecretDoor) {                   // secret door (FUN_00583ff0): slides open (OP), once
            if (state.mode == 0) mode = 1;
        } else if (door.operate_fn == d2d::rules::operate_fn::kTrapDoor) {            // trap door (FUN_00581eb0): opens, then down its room's warp (FUN_005550b0)
            // ponytail: the level's nearest warp for the room's warp unit (trap
            // doors are past act 1).
            if (state.mode == 0) mode = 2;
            else if (state.mode == 2 && !level->warps.empty())
                take_warp = int(std::ranges::min_element(level->warps, {}, [&](const Level::Warp& warp) { return std::hypot(warp.x - door.x, warp.y - door.y); }) - level->warps.begin());
        } else if (now_ms - state.when >= 500) {       // a door (FUN_00581d40): is anyone in the doorway?
            // ponytail: a unit's own subtile, not its collision pattern.
            auto in_door = [&](float x, float y) {
                const int sub_x = int(x * 5) - (int(door.x * 5) - door.size_x / 2), sub_y = int(y * 5) - (int(door.y * 5) - door.size_y / 2);
                return sub_x >= 0 && sub_y >= 0 && sub_x < door.size_x && sub_y < door.size_y;
            };
            bool occupied = in_door(player.x, player.y) || (merc && in_door(merc->unit.x, merc->unit.y));
            if (fight.mon_level == level)
                for (const auto& monster : fight.monsters) occupied = occupied || (monster.alive() && in_door(monster.unit.x, monster.unit.y));
            mode = d2d::rules::door_mode(state.mode, occupied);
        }
        if (mode >= 0) set_door_mode(npc_index, mode, now_ms);
    }

auto World::reduced_prices() const -> int {
        d2d::rules::StatSum sum{};
        (void)fight.player_fighter(nullptr, nullptr, nullptr, &sum);
        return int(sum[d2d::d2s::kReducedPrices]);
    }

auto World::set_door_mode(int npc_index, int mode, std::uint32_t now_ms) -> void {
        const auto& door = level->npcs[std::size_t(npc_index)];
        auto& state = doors.try_emplace({ level, npc_index }, Door{ mode_index(door.mode), 0 }).first->second;
        if (state.mode == mode) return;
        state = { mode, now_ms };
        set_footprint(*level, door, door.collision >> mode & 1);   // FUN_00623830 out / FUN_00620a70 back in
        if (const auto sound = d2d::rules::object_sound(door.object_id, mode); !sound.empty()) cues.cue(sound, now_ms, door.x, door.y);
        d2d::log::info("door {}: mode {}", npc_index, kObjectModes[std::size_t(mode)]);
    }

// A monster's door at a think (FUN_005b0f50): of the level's closed (mode
// 0) IsDoor objects, the nearest under 9 subtiles squared (FUN_005dd0b0
// mode 8, rules::door_pick); with MonsterOK, FUN_00584540 operates it
// (OperateFn 8) when in reach (FUN_00623660, rules::object_reach). Found,
// the monster stands 5 either way.
// ponytail: the level's objects in list order, not the near rooms' unit
// lists (a tie between two doors may go the other way); room unit lists
// are agent D's.
auto World::monster_door(const Monster& monster, std::uint32_t now_ms) -> bool {
        if (fight.mon_level != level) return false;
        const int x = int(std::floor(monster.unit.x * 5)), y = int(std::floor(monster.unit.y * 5));
        std::vector<std::pair<int, int>> spots;
        std::vector<int> door_indices;
        for (std::size_t i = 0; i < level->npcs.size(); ++i) {
            const auto& door = level->npcs[i];
            const auto state = doors.find({ level, int(i) });
            if (!door.door || (state != doors.end() ? state->second.mode : mode_index(door.mode)) != 0) continue;
            spots.emplace_back(int(door.x * 5) - x, int(door.y * 5) - y);
            door_indices.push_back(int(i));
        }
        const int pick = d2d::rules::door_pick(spots);
        if (pick < 0 || !level->npcs[std::size_t(door_indices[std::size_t(pick)])].monster_ok) return false;
        const auto& door = level->npcs[std::size_t(door_indices[std::size_t(pick)])];
        const auto [dx, dy] = spots[std::size_t(pick)];
        if (d2d::rules::object_reach(-dx, -dy, game_data->monsters.types[std::size_t(monster.type)].size, door.size_x, door.size_y))
            operate_door(door_indices[std::size_t(pick)], now_ms);
        return true;
    }

auto World::explode(int npc_index, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        const auto& barrel = level->npcs[std::size_t(npc_index)];
        operated[{ level, npc_index }] = now_ms;
        if (const auto sound = d2d::rules::object_sound(barrel.object_id, 1); !sound.empty()) cues.cue(sound, now_ms, barrel.x, barrel.y);
        // FUN_00584240: a physical blast (FUN_005dfa00 type 0) on each live
        // unit within 3 subtiles.
        // ponytail: the level's units in the order player, merc, monsters,
        // not its room's unit list (the seed's draws go by that order); a
        // monster's and the merc's life in whole points, not the game's
        // 256ths; the merc's dexterity isn't kept (MercStats), so its roll
        // takes 255, the 65 % floor every hireling's own dexterity puts it at.
        auto nearby = [&](float x, float y, float subtiles) { return std::hypot(x - barrel.x, y - barrel.y) * 5 <= subtiles; };
        const auto& fighter = fight.player_combat;
        if (nearby(player.x, player.y, 3) && character.stats.values[kLife] > 0)
            take_blast(character.stats.values[kLife], d2d::rules::blast_taken(blast(barrel, player.x, player.y, 2, character.stats.values[kLife], int(character.stats.get(kLevel)),
                                                                                    int(character.stats.get(kDex) + fight.psum[kDex]), fighter.defense),
                                                                              fighter.dr_flat, fighter.dr_pct));
        if (merc && nearby(merc->unit.x, merc->unit.y, 3) && merc->hit_points > 0) {
            const auto merc_fighter = fight.merc_fighter();
            merc->hit_points -= d2d::rules::blast_taken(blast(barrel, merc->unit.x, merc->unit.y, 2, std::int64_t(merc->hit_points) << 8, fight.merc_st.level, 255, merc_fighter.defense),
                                                       merc_fighter.dr_flat, merc_fighter.dr_pct) >> 8;
        }
        if (fight.mon_level == level)
            for (std::size_t k = 0; k < fight.monsters.size(); ++k) {
                auto& monster = fight.monsters[k];
                if (!monster.alive() || !nearby(monster.unit.x, monster.unit.y, 3)) continue;
                const auto target = monster.target(*game_data);
                const int damage = d2d::rules::blast_taken(blast(barrel, monster.unit.x, monster.unit.y, game_data->monsters.types[std::size_t(monster.type)].size,
                                                                 std::int64_t(monster.hit_points) << 8, monster.stats.level, 0, monster.stats.armor_class), 0, target.res[0]);
                if (hurt(*game_data, monster, damage >> 8, now_ms)) fight.killed(k, now_ms);
            }
        for (std::size_t k = 0; k < level->npcs.size(); ++k)          // the next barrels along (class 11, still NU)
            if (const auto& other = level->npcs[k]; other.object_id == d2d::rules::object_ids::kExplodingBarrel && !other.preoperated && !operated.contains({ level, int(k) }) && std::hypot(other.x - barrel.x, other.y - barrel.y) * 5 < 3)
                explode(int(k), now_ms);
        set_footprint(*level, barrel, barrel.collision >> 1 & 1);
        d2d::log::info("barrel {} exploded", npc_index);
    }

auto World::arm_trap(int trap, float x, float y, int alvl, std::uint32_t now_ms) -> void {
        if (trap == 8 && d2d::rules::trap_undead(level->region[std::size_t(std::clamp(character.header.active_difficulty(), 0, 2))], 0) < 0) return;   // FUN_00582250
        traps.push_back({ level, trap, x, y, alvl, now_ms + 35 * kTickMs });
    }

auto World::spring_traps(std::uint32_t now_ms) -> void {
        for (auto armed = traps.begin(); armed != traps.end();) {
            if (now_ms < armed->due) { ++armed; continue; }
            const Trap due = *armed;
            armed = traps.erase(armed);
            if (due.level == level) spring_trap(d2d::rules::trap_on_level(due.trap, level->id), due.x, due.y, due.alvl, now_ms);
        }
    }

auto World::spring_trap(int trap, float x, float y, int alvl, std::uint32_t now_ms, int undead) -> void {
        const int diff = std::clamp(character.header.active_difficulty(), 0, 2);
        if (trap == 5 || trap == 7) {                  // FUN_00582380: large at the chest, small a subtile east
            fires.push_back({ level, &game_data->trap_fires[0], x, y });
            fires.push_back({ level, &game_data->trap_fires[1], x + 0.2f, y });
            d2d::log::info("trap {}: fire", trap);
            return;
        }
        if (trap == 8) {                               // FUN_005822f0: 1 or 2 of the level's undead family
            const int fam = d2d::rules::trap_undead(level->region[std::size_t(diff)], 0);
            if (fam < 0 || fight.mon_level != level) { d2d::log::info("trap 8: no undead here"); return; }
            // FUN_0063ec70: the level's own variant of the family.
            // ponytail: from the region, else the family's first (FUN_006510c0's step not traced).
            int type = fam;
            const auto& types = game_data->monsters.types;
            for (const int monstats_row : level->region[std::size_t(diff)]) if (types[std::size_t(monstats_row)].base == types[std::size_t(fam)].base) { type = monstats_row; break; }
            const int count = undead ? undead : int(rng.next() & 1) + 1;
            for (int k = 0; k < count; ++k) {
                const auto [free_x, free_y] = level->nearest_free(x + float(k) * 0.4f, y + 0.4f);
                auto monster = make_monster(*game_data, type, free_x, free_y, rng, diff);
                // Its unit seed: a step of the game seed (FUN_00552df0), what its drops roll off.
                // ponytail: its look doesn't draw on it first, as spawn_monsters' does
                // (monster spawning: agent D's).
                monster.seed = d2d::rules::Rng{ fight.spawning.game.next() };
                monster.aware = true;
                fight.add_monster(std::move(monster));
            }
            d2d::log::info("trap 8: {} x{}", types[std::size_t(type)].id, count);
            return;
        }
        const auto* name = std::size_t(trap) < d2d::rules::kTrapMissile.size() ? d2d::rules::kTrapMissile[std::size_t(trap)] : "";
        const auto found = game_data->missiles.find(name);
        if (!*name || found == game_data->missiles.end()) { d2d::log::info("trap {}: not built", trap); return; }
        const auto& missile_info = found->second;
        const int lvl = d2d::rules::kTrapLevel[std::size_t(diff)];
        d2d::rules::MissileDamage damage;
        if (const auto skill_found = game_data->skills.by_name.find(missile_info.skill); !missile_info.skill.empty() && skill_found != game_data->skills.by_name.end()) {
            d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, lvl, &rng };
            damage = d2d::rules::missile_damage(game_data->skills, *game_data->skills.get(skill_found->second), env, lvl);
        } else {
            damage = d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev, missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
        }
        d2d::rules::MonStats stats;
        stats.level = alvl;
        stats.to_hit = missile_info.to_hit ? alvl * 10 : 1 << 20;       // ToHit 0: always hits
        stats.a2_min = missile_info.min; stats.a2_max = std::max(missile_info.max, missile_info.min);
        // In 256ths a frame; poison's over its length.
        auto pts = [&](int points) { const std::int64_t value = points; return int(damage.etype == 3 ? value * std::max(damage.elen, 1) >> 8 : value >> 8); };
        if (damage.etype >= 0) stats.elements[0] = { damage.etype, 100, pts(damage.elo), pts(damage.ehi), damage.elen, "A2" };
        auto shoot = [&](float dx, float dy, float vel, const std::shared_ptr<std::vector<int>>& struck) {
            const float speed = missile_speed(int(vel * 256) * 75 / 100), distance = std::max(std::hypot(dx, dy), 0.01f);   // FUN_0059fa30's x 75 / 100
            Missile missile{ &missile_info, x, y, dx / distance * speed, dy / distance * speed, direction32(dx, dy), now_ms, now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, stats };
            missile.struck = struck;
            fight.missiles.push_back(std::move(missile));
        };
        const auto ring = std::make_shared<std::vector<int>>();
        if (trap == 3) {                               // PrimePoisonNova: 8 at Param1 << 6, 8 between at Param2 << 6
            for (std::size_t k = 0; k < d2d::rules::kPoisonNova.size(); ++k) {
                const auto [offset_x, offset_y] = d2d::rules::kPoisonNova[k];
                shoot(float(offset_x), float(offset_y), float(k % 2 ? missile_info.param2 : missile_info.param1) / 4, ring);   // << 6 of a Vel's << 8
            }
        } else if (trap == 4) {                        // Trap Nova (do 22): the nova's 64
            for (int k = 0; k < 64; ++k) {
                const float angle = float(k) * 6.2831853f / 64;
                shoot(std::cos(angle), std::sin(angle), float(missile_info.vel), ring);
            }
        } else {
            shoot(player.x - x, player.y - y, float(missile_info.vel), std::make_shared<std::vector<int>>());
            fight.missiles.back().to = { player.x, player.y };
        }
        d2d::log::info("trap {}: {} at level {}", trap, name, lvl);
    }

auto World::enter(const Character& entering) -> void {
        character.character_class = entering.character_class; character.name = entering.name;
        character.appearance = entering.appearance; character.items = entering.items; character.stats = entering.stats; character.panel = entering.panel;
        character.expansion = entering.expansion; character.header = entering.header; character.merc_items = entering.merc_items;
        // A corpse in the save (its corpse list, FUN_00533850) lies by the
        // camp's start.
        // ponytail: where game.exe puts it isn't traced; the save's x / y
        // aren't read.
        corpses.clear();
        if (!entering.corpse.empty() && game_data) {
            const auto& town = game_data->town;
            const auto [spot_x, spot_y] = town.nearest_free(town.start.first + 1.f, town.start.second + 1.f);
            corpses.push_back({ &town, spot_x, spot_y, 4, entering.corpse, 0, entering.appearance ? *entering.appearance : game_data->starting_gear[std::size_t(entering.header.cls % 7)] });
        }
        // A new game starts at full life, mana and stamina, whatever the
        // save held (D2's "save and exit to heal").
        for (const auto& [cur, max] : { std::pair{ d2d::d2s::kLife, d2d::d2s::kMaxLife }, { d2d::d2s::kMana, d2d::d2s::kMaxMana },
                                        { d2d::d2s::kStamina, d2d::d2s::kMaxStamina } })
            character.stats.values[cur] = character.stats.values[max];
        held.reset();
        store = {};
        level = &game_data->town;                     // a game starts in the camp (set_map_seed may have rebuilt it)
        other_npcs.clear();
        npc_states = npc_start(*level);
        interact_npc = pick_item = take_warp = -1;
        wanted_near = nullptr;
        if (level->start.first >= 0) std::tie(player.x, player.y) = level->nearest_free(level->start.first, level->start.second);
        target_x = player.x; target_y = player.y;
        spawn_merc();
        new_game();
        for (std::size_t i = 0; i < level->npcs.size() && i < npc_states.size(); ++i)
            if (level->npcs[i].quest == d2d::rules::CainQuest::kQuest) npc_states[i].hidden = !cain.camp_cain;
    }

auto World::new_game() -> void {
        fight.new_game(character.header.active_difficulty());
        set_waypoint(0);                          // the town's is always active (FUN_00661030 forces index 0 as the save loads)
        day = {};                                 // a new game starts at sunrise
        day_at = now;
        loot.ground.clear();
        loot.kept.clear();
        loot.ground_level = level;
        cues.due.clear();
        // The first join (FUN_00546270): quests the player's done or closed
        // are shut for the game (FUN_00544410, game flag 15), every quest's
        // join runs, then the chain from quest 1.
        for (int quest = 1; quest < 7; ++quest)
            closed_at_join[std::size_t(quest)] = d2d::rules::qbit(quests(), quest, 0) || d2d::rules::qbit(quests(), quest, 15);
        den = {};
        den.join(quests());
        andy = {};
        andy.join(quests());
        burial = {};
        burial.join(quests());
        tower = {};
        tower.join(quests());
        tools = {};
        tools.join(quests(), carries("hdm"));
        cain = {};
        cain.join(quests(), carries("bks"), carries("bkd"));
        chain(1);
        cain.camp_spawn();
        den_left = -1;
        den_log_at = 0;
        operated.clear();
        // The levels as built: no room populated (the units come again as
        // they do), doors back as they were made.
        for (const auto& [id, built] : game_data->levels) {
            if (!built) continue;
            auto& level_rw = const_cast<Level&>(*built);         // GameData owns its levels mutable
            if (level_rw.npcs.size() > level_rw.npc_base && level_rw.walk.size() == level_rw.tile_walk.size()) {
                level_rw.npcs.resize(level_rw.npc_base);
                level_rw.walk = level_rw.tile_walk;
                stamp_footprints(level_rw);
            }
            for (const auto& npc : built->npcs)
                if (is_door(npc.operate_fn)) set_footprint(*built, npc, npc.collision >> mode_index(npc.mode) & 1);
        }
        npc_states.resize(std::min(npc_states.size(), level->npcs.size()));
        other_npcs.clear();
        doors.clear();
        fires.clear();
        traps.clear();
        treasure.clear();
        cain_walk = {};
        cain_portal = {};
        portal = {};
        take_portal = -1;
        take_corpse = -1;
        last_pmode = -1;
        pick_item = -1;
    }

auto World::spawn_merc() -> void {
        merc.reset();
        const auto& header = character.header;
        if (const auto found = game_data->mercs.find(header.merc_type); header.merc_seed && !header.merc_dead && found != game_data->mercs.end()) {
            merc = Monster{};
            merc->npc = found->second.npc;
            merc->unit.shape = merc->npc.shape;
            merc->difficulty = header.active_difficulty();
            std::tie(merc->unit.x, merc->unit.y) = level->nearest_free(player.x + 1, player.y + 1);
            merc_npc = &found->second.npc;
            fight.merc_joins();
        }
    }

auto World::swap_npcs(const Level* from) -> void {
        arrived_at = now;
        if (from && from != level) andy.enter(quests(), from->id, level->id);
        if (from && from != level) burial.enter(quests(), from->id, level->id);
        if (from && from != level) tower.enter(quests(), from->id, level->id);
        if (from && from != level) tools.enter(quests(), from->id);
        if (from && from != level) cain.enter(quests(), from->id, level->id);
        if (from) other_npcs[from] = std::move(npc_states);
        if (const auto found = other_npcs.find(level); found != other_npcs.end()) {
            npc_states = std::move(found->second);
            other_npcs.erase(found);
        } else {
            npc_states = npc_start(*level);
        }
        grow_states(npc_states, *level);
        for (std::size_t i = 0; i < level->npcs.size() && i < npc_states.size(); ++i)   // camp Cain (FUN_00592960)
            if (level->npcs[i].quest == d2d::rules::CainQuest::kQuest)
                npc_states[i].hidden = level->npcs[i].hc_idx == d2d::rules::monster_ids::kCain ? !(cain_walk.npc == int(i) && cain_walk.stage >= 0) : !cain.camp_cain;
        // The stones come up (FUN_005935e0): the portal again, the stones lit.
        if (level->id == d2d::rules::CainQuest::kStony && cain.stones_init())
            for (std::size_t i = 0; i < level->npcs.size(); ++i) {
                const auto& stone = level->npcs[i];
                if (stone.operate_fn != d2d::rules::operate_fn::kCairnStone) continue;
                operated.try_emplace({ level, int(i) }, 0u);
                if (stone.object_id == d2d::rules::object_ids::kStoneAlpha && !portal[2].level) {
                    const auto [spot_x, spot_y] = level->nearest_free(stone.x + 4, stone.y + 4);
                    portal[2] = { level, spot_x, spot_y, now };
                }
            }
    }

auto World::respawn(std::uint32_t now_ms) -> void {
        if (level != &game_data->town) {
            events.push_back(ev::LevelChanged{ level, false });
            const Level* from = level;
            level = &game_data->town;
            swap_npcs(from);
            interact_npc = pick_item = -1;
        }
        take_warp = -1;
        std::tie(player.x, player.y) = level->start.first >= 0 ? level->start : std::pair{ player.x, player.y };
        std::tie(player.x, player.y) = level->nearest_free(player.x, player.y);
        target_x = player.x; target_y = player.y;
        fight.revive(now_ms);
        d2d::log::info("respawned in the Rogue Encampment");
    }

auto World::cross_level() -> void {
        const float world_x = float(level->world_x) + target_x, world_y = float(level->world_y) + target_y;
        bool known = level->inside(target_x, target_y);
        for (const auto& neighbour : level->nearby) known = known || neighbour.level->inside(target_x - float(neighbour.dx), target_y - float(neighbour.dy));
        if (!known)
            for (const auto& placement : game_data->act1_layout)
                if (world_x >= float(placement.x) && world_y >= float(placement.y) && world_x < float(placement.x + placement.width) && world_y < float(placement.y + placement.height)
                    && std::ranges::find(not_there, placement.level) == not_there.end()) {
                    not_there.push_back(placement.level);
                    d2d::log::info("not implemented: level {} (the player headed there from level {})", placement.level, level->id);
                }
        if (level->inside(player.x, player.y)) return;
        for (const auto& neighbour : level->nearby) {
            if (!neighbour.level->inside(player.x - float(neighbour.dx), player.y - float(neighbour.dy))) continue;
            const float dx = float(neighbour.dx), dy = float(neighbour.dy);
            auto shift = [&](UnitState& unit) {
                unit.x -= dx; unit.y -= dy;
                unit.goal_x -= dx; unit.goal_y -= dy;
                for (auto& [step_x, step_y] : unit.path) { step_x -= dx; step_y -= dy; }
            };
            shift(player);
            target_x -= dx; target_y -= dy;
            if (merc) shift(merc->unit);
            fight.pets_cross(level, neighbour.level, dx, dy);
            events.push_back(ev::LevelChanged{ level, true });
            const Level* from = level;
            level = neighbour.level;
            fight.enter(level);
            loot.enter(level);
            swap_npcs(from);
            interact_npc = -1;
            d2d::log::info("level: {} at ({:.1f}, {:.1f})", level_name(*level), player.x, player.y);
            return;
        }
    }

auto World::quest_talk(int hc_idx) -> std::vector<d2d::rules::QuestMsg> {
        auto out = den.talk(quests(), hc_idx);
        std::ranges::copy(andy.talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(burial.talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(tower.talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(tools.talk(quests(), hc_idx, holding_malus(), int(character.stats.get(d2d::d2s::kLevel))), std::back_inserter(out));
        std::ranges::copy(cain.talk(quests(), hc_idx, carries("bks")), std::back_inserter(out));
        return out;
    }

auto World::holding_malus() const -> bool {
        return (held && held->code == "hdm") || std::ranges::contains(character.items, std::string("hdm"), &d2d::d2s::Item::code);
    }

// The malus stand (OperateFn 21, FUN_00591ac0): at clvl 8 the malus drops
// from it (FUN_00559a30) and it's used (mode 2); too low, "I can't"
// (FUN_00553380).
// ponytail: the refusal's a log line, not the class's voice; the stand
// starts NU where game.exe's InitFn 15 has it ON with the quest off.
auto World::malus_stand(int npc_index, std::uint32_t now_ms) -> void {
        const auto& object = level->npcs[std::size_t(npc_index)];
        const auto stand = tools.operate(quests(), int(character.stats.get(d2d::d2s::kLevel)));
        if (stand == d2d::rules::ToolsQuest::Stand::refuse) d2d::log::info("I can't use this yet.");
        if (stand == d2d::rules::ToolsQuest::Stand::drop || !tools.active) operated.try_emplace({ level, npc_index }, now_ms);
        if (stand != d2d::rules::ToolsQuest::Stand::drop) return;
        const auto& area_levels = game_data->area_level;
        const int alvl = std::size_t(level->id) < area_levels.size() ? area_levels[std::size_t(level->id)][std::size_t(std::clamp(character.header.active_difficulty(), 0, 2))] : 1;
        loot.put({ .code = "hdm" }, object.x, object.y, alvl, fight.spawning.game, now_ms);
        d2d::log::info("Tools of the Trade: the Horadric Malus");
    }

// Andariel's death hook (FUN_005965a0): the quest's bits; the player's
// kill for it opens Act 2 (the save's progression, FUN_00538680) and
// drops two chipped gems and a standard one (0x7361dc / 0x736444).
// ponytail: loot's rng, not the game's quest rng (game+0x10f4); quests:
// agent A's.
auto World::andariel_died(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        if (!andy.killed(quests(), level->id == d2d::rules::level_ids::kCatacombsLevel4)) return;
        const int diff = character.header.active_difficulty();
        character.header.progression = std::uint8_t(std::max<int>(character.header.progression, diff * (character.header.expansion() ? 5 : 4) + 1));
        for (const auto* codes : { d2d::rules::AndyQuest::kChipped, d2d::rules::AndyQuest::kChipped, d2d::rules::AndyQuest::kStandard })
            loot.put({ codes[loot.rng.next() % 7] }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
        d2d::log::info("Sisters to the Slaughter: Andariel is dead, return to Warriv");
    }

// Blood Raven's death hook (FUN_00590ec0).
// ponytail: "near" is the player in the Burial Grounds, not the killer's
// room or its neighbours (FUN_00590c40); quests: agent A's.
auto World::blood_raven_died(std::uint32_t now_ms) -> void {
        if (!burial.killed(quests(), level->id == d2d::rules::BurialQuest::kBurial)) return;
        static constexpr const char* kClass[7] = { "amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin" };
        if (character.header.cls < 7) cues.cue(std::format("{}_act1_complete_burial", kClass[character.header.cls]), now_ms, player.x, player.y);
        d2d::log::info("Sisters' Burial Grounds: Blood Raven is dead, return to Kashya");
    }

// Kashya's reward (FUN_00579180): the first offer of her hire list as the
// merc, free — none if there's a merc already (LoD: even a dead one).
// The offer leaves the list (packet 0x50 subtype 2).
// ponytail: an unopened list is rolled here (game.exe: the client asks
// for it as her menu opens: waits for networking); an emptied one isn't
// regenerated (FUN_00577010).
auto World::kashya_merc() -> void {
        const auto& header = character.header;
        if (header.merc_seed && (character.expansion || !header.merc_dead)) return;
        if (hire_offers.empty())
            for (int k = 0; k < 5; ++k)
                if (auto offer = d2d::rules::merc_offer(game_data->rules, character.expansion, 0, header.active_difficulty(), int(character.stats.get(d2d::d2s::kLevel)), rng)) hire_offers.push_back(*offer);
        if (hire_offers.empty()) return;
        auto offer = hire_offers.front();
        offer.cost = 0;
        d2d::rules::hire(offer, character.header, character.stats);
        hire_offers.erase(hire_offers.begin());
        spawn_merc();
        d2d::log::info("Sisters' Burial Grounds: Kashya's reward, a mercenary");
    }

// The Countess's death hook (FUN_00595710): the quest's bits and, for the
// player's kill, their complete_tower line (unit event 0x25). Her drop is
// her SuperUniques TC's (negative picks: an item and a rune roll).
auto World::countess_died(std::uint32_t now_ms) -> void {
        const bool quest_kill = tower.killed(quests(), level->id == d2d::rules::TowerQuest::kCellar);
        // Her treasure (FUN_005954f0, dead and not yet made: d+0x118 / 0x119):
        // a towerchestspawner (missile 332) at each chest of cellar 5 (object
        // 371, InitFn 47 lists up to 8).
        // The invisible owner (monster 326 at her death spot, FUN_005b2f20)
        // only owns the spawners, which d2d plays without one.
        // ponytail: the chests are the level's, not the ones whose rooms came
        // up (room units: agent D's).
        if (tower.dead && !tower.treasure && level->id == d2d::rules::TowerQuest::kCellar) {
            tower.treasure = true;
            for (std::size_t i = 0; i < level->npcs.size() && treasure.size() < 8; ++i)
                if (level->npcs[i].root == "objects" && level->npcs[i].object_id == d2d::rules::object_ids::kLargeChestR) treasure.push_back({ level, int(i), d2d::rules::TowerQuest::kTreasureFrames });
        }
        if (!quest_kill) return;
        static constexpr const char* kClass[7] = { "amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin" };
        if (character.header.cls < 7) cues.cue(std::format("{}_act1_complete_tower", kClass[character.header.cls]), now_ms, player.x, player.y);
        d2d::log::info("The Forgotten Tower: the Countess is dead");
    }

// The towerchestspawner's frame (FUN_005af300; Missiles.txt row 332:
// Range 400, Param1 150, Param2 2, Param3 5). At Range - Param1 left the
// chest opens (FUN_00585e00, mode 0 only): the act's chest class three
// times with quality forced magic (FUN_00585b90, EDX 4), then two each of
// hp / mp by act (+2 past normal: 0x731f4c / 0x731f60, FUN_00558450 /
// FUN_005584c0), then mode 2. From then on every Param2 * 4 frames a gold
// pile at the area level, rand(2r + 1) - r subtiles off (r = Param3).
// The rounds roll off the chest's unit seed (chest_round).
// ponytail: the world's rng stands in for the missile's seed; it runs only while the player's on its level (game.exe: while
// its room's up); the end event (unit event 0x5c at 1 left) isn't sent;
// the missile's lifetime taken as Range (LevRange 1 at level 0).
auto World::tower_treasure(std::uint32_t now_ms) -> void {
        using Tower = d2d::rules::TowerQuest;
        for (auto& spawner : treasure) {
            if (spawner.level != level || spawner.left <= 0) continue;
            const auto& chest = level->npcs[std::size_t(spawner.npc)];
            const int diff = std::clamp(character.header.active_difficulty(), 0, 2);
            const auto& area_levels = game_data->area_level;
            auto alvl = [&](int id) { return std::size_t(id) < area_levels.size() ? area_levels[std::size_t(id)][std::size_t(diff)] : 1; };
            const int here = alvl(level->id);
            if (spawner.left == Tower::kTreasureFrames - Tower::kTreasureOpen && !operated.contains({ level, spawner.npc })) {
                const auto [low, high] = d2d::rules::kChestLevels[0];
                const auto treasure_class = d2d::rules::chest_tc(0, diff, here, alvl(low), alvl(high));
                std::vector<d2d::rules::Drop> drops;
                for (int round = 0; round < 3; ++round) d2d::rules::chest_round(game_data->rules, treasure_class, chest.seed, drops, 4);
                for (const auto* code : { "hp", "hp", "mp", "mp" }) drops.push_back({ std::string(code) + (diff ? "3" : "1") });
                for (const auto& dropped : drops) loot.put(dropped, chest.x, chest.y, here, fight.spawning.game, now_ms);
                operated[{ level, spawner.npc }] = now_ms;
                d2d::log::info("The Forgotten Tower: the Countess's chest opens ({} drops)", drops.size());
            }
            if (spawner.left < Tower::kTreasureFrames - Tower::kTreasureOpen && spawner.left % (Tower::kTreasureEvery * 4) == 0) {
                const int radius = Tower::kTreasureRadius;
                const float off_x = float(rng(2 * radius + 1) - radius) / 5, off_y = float(rng(2 * radius + 1) - radius) / 5;
                const auto [spot_x, spot_y] = level->nearest_free(chest.x + off_x, chest.y + off_y);
                loot.put({ .code = "gld" }, spot_x, spot_y, here, fight.spawning.game, now_ms);
            }
            --spawner.left;
        }
    }

// Tristram Cain's AI (FUN_005e7880), a think each time he stops: the
// first notes where he stands (FUN_005944b0) and heads 3 subtiles on
// (x + 3, y + 3); up to 6 goes while more than 1 off; then the town
// portal (FUN_005943b0: object 189, ECX 2 / EDX 0xbd to FUN_00555230)
// at the noted spot, in the room near his own that holds it
// (FUN_00463740); none does: the spot moves on 3 subtiles on each axis
// and no portal opens. Then back to the portal (FUN_00594450: his own
// spot when there's none) for up to 6 more thinks, stepping in
// (FUN_005944f0: camp Cain due).
// ponytail: the thinks' idles (1, 20 frames) stand in for the AI's own
// tick; the portal stays for the game (FUN_005944f0 doesn't remove it;
// InitFn 61 isn't traced); the Search for Cain: agent A's.
auto World::cain_step(std::uint32_t now_ms, float elapsed) -> void {
        if (cain_walk.npc < 0 || level->id != d2d::rules::CainQuest::kTristram || std::size_t(cain_walk.npc) >= npc_states.size()) return;
        const auto& npc = level->npcs[std::size_t(cain_walk.npc)];
        auto& unit = npc_states[std::size_t(cain_walk.npc)];
        if (unit.walking) {
            unit.walking = walk_on(*level, unit, cells_per_sec(npc.velocity) * elapsed);
            if (unit.walking) return;
            unit.path.clear();
        }
        if (now_ms < cain_walk.next) return;
        const auto cell = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
        const int at_x = int(std::floor(unit.x * 5)), at_y = int(std::floor(unit.y * 5));
        const auto walk = [&](int x, int y) {
            unit.walking = set_off(*level, unit, cell(x), cell(y), false);
        };
        const auto off = [&](int x, int y) { return d2d::rules::ai_distance(x - at_x, y - at_y); };
        if (cain_walk.stage < 0) {
            unit.hidden = false;
            cain_walk.spot_x = at_x; cain_walk.spot_y = at_y;
            cain_walk.x = at_x + 3; cain_walk.y = at_y + 3;
            cain_walk.stage = 1;
            cain_walk.next = now_ms + 40;
            return;
        }
        if (cain_walk.stage < 2) {
            if (off(cain_walk.x, cain_walk.y) > 1 && cain_walk.tries < 6) { ++cain_walk.tries; walk(cain_walk.x, cain_walk.y); return; }
            if (room_near_holds(*level, at_x, at_y, cain_walk.spot_x, cain_walk.spot_y)) {
                cain_portal = { level, cell(cain_walk.spot_x), cell(cain_walk.spot_y), now_ms };
                cues.cue("object_townportal", now_ms, cain_portal.x, cain_portal.y);
                d2d::log::info("Search for Cain: Cain opens a portal at subtile ({}, {})", cain_walk.spot_x, cain_walk.spot_y);
            } else {
                cain_walk.spot_x += 3; cain_walk.spot_y += 3;
            }
            cain_walk.stage = 2;
            cain_walk.next = now_ms + 20 * 40;
            return;
        }
        const bool opened = cain_portal.level == level;
        if (++cain_walk.stage < 8 && opened && off(cain_walk.spot_x, cain_walk.spot_y) != 0) { walk(cain_walk.spot_x, cain_walk.spot_y); return; }
        cain.portal_entered();
        unit.hidden = true;
        cain_walk.npc = -1;
        d2d::log::info("Search for Cain: Cain has gone to the camp");
    }

auto World::carries(std::string_view code) const -> bool { return std::ranges::contains(character.items, code, &d2d::d2s::Item::code); }

// The tree (OperateFn 12) drops the scroll by it; the stones (9) light in
// the deciphered scroll's order, the fifth opening the way to Tristram at
// StoneLambda (x + 6, y - 3); the Gibbet (10) frees Cain.
// ponytail: the world's rng stands in for the game's quest rng
// (game+0x10f4); CairnStones' missile and the portal's red look aren't
// drawn. Cain walks out (cain_step); with no room for him the portal
// FUN_0056d130 opens at x + 6, y + 6 is the player's own pair. Quests:
// agent A's.
auto World::cain_operate(int npc_index, std::uint32_t now_ms) -> void {
        using Cain = d2d::rules::CainQuest;
        const auto& object = level->npcs[std::size_t(npc_index)];
        const bool fresh = !operated.contains({ level, npc_index });
        auto& quest_bits = quests();
        if (object.operate_fn == d2d::rules::operate_fn::kInifussTree) {
            if (!cain.tree(quest_bits, carries("bks") || carries("bkd"))) return;
            operated[{ level, npc_index }] = now_ms;
            loot.put({ .code = "bks" }, object.x, object.y, 1, fight.spawning.game, now_ms);
            d2d::log::info("Search for Cain: the Scroll of Inifuss");
            return;
        }
        if (object.operate_fn == d2d::rules::operate_fn::kGibbet) {
            if (!cain.gibbet(quest_bits, fresh)) return;
            operated[{ level, npc_index }] = now_ms;
            const auto found = std::ranges::find(level->npcs, d2d::rules::monster_ids::kCain, &Npc::hc_idx);
            const bool spawned = found != level->npcs.end();
            cain.rescued(spawned);
            if (spawned) cain_walk = { .npc = int(found - level->npcs.begin()), .next = now_ms + std::uint32_t(object.op_frames) * 40 };
            else open_portal_at(object.x + 6, object.y + 6, now_ms);
            d2d::log::info("Search for Cain: Cain is free");
            return;
        }
        if (!cain.ordered) {
            cain.stone_order(rng.low, rng.high);
            d2d::log::info("Search for Cain: the stones' order {} {} {} {} {}", cain.order[0], cain.order[1], cain.order[2], cain.order[3], cain.order[4]);
        }
        const auto lit = cain.stone(quest_bits, object.object_id, carries("bkd"), fresh);
        if (lit == Cain::Stone::none) return;
        operated[{ level, npc_index }] = now_ms;
        if (lit == Cain::Stone::lit) return;
        character.items.erase(std::ranges::find(character.items, std::string_view("bkd"), &d2d::d2s::Item::code));
        const auto lambda = std::ranges::find(level->npcs, 21, &Npc::object_id);
        const auto [spot_x, spot_y] = level->nearest_free(lambda == level->npcs.end() ? object.x : lambda->x + 6, lambda == level->npcs.end() ? object.y : lambda->y - 3);
        portal[2] = { level, spot_x, spot_y, now_ms };
        cues.cue("object_townportal", now_ms, spot_x, spot_y);
        d2d::log::info("Search for Cain: the way to Tristram at ({:.1f}, {:.1f})", spot_x, spot_y);
    }

auto World::den_count(std::uint32_t now_ms) -> void {
        if (level->id != d2d::rules::DenQuest::kDen || fight.mon_level != level) return;
        const int left = int(std::ranges::count_if(fight.monsters, &Monster::alive));
        if (den_left >= 0 && left < den_left) {
            const auto kill = den.killed(quests(), left);
            if (kill == d2d::rules::DenQuest::Kill::few) d2d::log::info("Den of Evil: {}", left == 1 ? "one monster left" : std::format("monsters remaining: {}", left));
            if (kill == d2d::rules::DenQuest::Kill::cleared) {
                d2d::log::info("Den of Evil: cleared");
                den_log_at = now_ms + 8 * kTickMs;     // the log's "Return to Akara" 8 ticks on (LAB_00590230)
                static constexpr const char* kClass[7] = { "amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin" };
                if (d2d::rules::qbit(quests(), 1, 13) && character.header.cls < 7)
                    cues.cue(std::format("{}_act1_complete_den", kClass[character.header.cls]), now_ms, player.x, player.y);
            }
        }
        den_left = left;
    }

auto World::level_name(const Level& level) -> const char* {
        return !level.name.empty() ? level.name.c_str() : level.id == d2d::rules::level_ids::kRogueEncampment ? "Rogue Encampment" : "?";   // Levels.txt LevelName
    }

// A joined game's swing at a host monster (apps/d2d netgame): the
// player turns to (x, y) and plays the skill's swing; no monster here to
// hit, the host resolves it (Fight's hits guard on attack_mon < 0).
auto World::display_swing(int skill, float x, float y, std::uint32_t now_ms) -> void {
        if (fight.pmode >= 0 || fight.dead()) return;
        fight.attack_mon = -1;
        fight.attack_skill = skill;
        interact_npc = pick_item = take_warp = -1;
        player.walking = false;
        player.dir = direction16(x - player.x, y - player.y);
        fight.start_swing(now_ms);
    }

auto World::use_warp() -> void {
        if (take_warp < 0 || std::size_t(take_warp) >= level->warps.size()) return;
        const auto warp = level->warps[std::size_t(take_warp)];
        if (std::hypot(warp.x + 0.5f - player.x, warp.y + 0.5f - player.y) > 2.f) {
            if (!player.walking) take_warp = -1;                                // stopped short
            return;
        }
        take_warp = -1;
        const Level* destination = game_data->level(warp.destination);                               // built by now, or waited for
        if (!destination || destination->ds1.width() == 0) {
            d2d::log::info("not implemented: level {} (a warp from level {})", warp.destination, level->id);
            return;
        }
        // FUN_005550b0: the other side's warp tile unit (FUN_006195a0), the
        // free spot nearest it, then a walk of its LvlWarp ExitWalk from there.
        // The tile's linked (FUN_0066c220) to the other side's k-th slot back
        // here, k its own rank among its row's slots there; none placed: the
        // lowest slot back that is.
        // ponytail: several tiles of one slot take the first built (game.exe:
        // the first room of its list, then the first unit of its room): room
        // unit lists are agent D's.
        auto back = std::ranges::find_if(destination->warps, [&](const Level::Warp& other) { return other.destination == level->id && other.pair == warp.pair; });
        if (back == destination->warps.end())
            for (auto it = destination->warps.begin(); it != destination->warps.end(); ++it)
                if (it->destination == level->id && (back == destination->warps.end() || it->slot < back->slot)) back = it;
        const float arrive_x = back == destination->warps.end() ? float(destination->ds1.width()) / 2 : back->unit_x;
        const float arrive_y = back == destination->warps.end() ? float(destination->ds1.height()) / 2 : back->unit_y;
        arrive(destination, arrive_x, arrive_y, "a warp");
        if (back != destination->warps.end()) {
            target_x = player.x + back->exit_x; target_y = player.y + back->exit_y;
            player.walking = true;
        }
    }

auto World::arrive(const Level* destination, float arrive_x, float arrive_y, const char* how) -> void {
        const auto [free_x, free_y] = destination->nearest_free(arrive_x, arrive_y);
        const float dx = player.x - free_x, dy = player.y - free_y;
        fight.pets_cross(level, destination, dx, dy);
        events.push_back(ev::LevelChanged{ level, true });
        const Level* from = level;
        level = destination;
        player.x = free_x; player.y = free_y;
        player.walking = false; player.path.clear();
        target_x = free_x; target_y = free_y;
        if (merc) {
            merc->unit.path.clear();
            std::tie(merc->unit.x, merc->unit.y) = level->nearest_free(free_x + 1, free_y + 1);
        }
        fight.footstep(int(std::floor(free_x * 5)), int(std::floor(free_y * 5)), now);   // an arrival is a footstep (FUN_00554ea0)
        fight.warp_spot = { int(std::floor(free_x * 5)), int(std::floor(free_y * 5)) };
        fight.enter(level);
        fight.rooms_up(*level, player.x, player.y, true);
        std::tie(player.x, player.y) = level->nearest_free(player.x, player.y);   // off what the room just made
        loot.enter(level);
        swap_npcs(from);
        interact_npc = pick_item = take_warp = take_portal = -1;
        if (level->id == d2d::rules::DenQuest::kDen) den.enter_den(quests());
        d2d::log::info("level: {} at ({:.1f}, {:.1f}), through {} from {}", level_name(*level), free_x, free_y, how, level_name(*from));
    }

auto World::death_penalty(std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        auto& stat_values = character.stats.values;
        const int lvl = int(stat_values[kLevel]);
        const int difficulty = character.header.active_difficulty();
        static constexpr int kDeathExp[3] = { 0, 5, 10 };
        exp_lost = 0;
        if (lvl > 1 && std::size_t(lvl) < game_data->exp_next.size()) {
            const auto low = game_data->exp_next[std::size_t(lvl - 1)], high = game_data->exp_next[std::size_t(lvl)];
            if (const auto loss = kDeathExp[std::clamp(difficulty, 0, 2)] * (high - low) / 100; loss > 0) {
                const auto experience = std::max(stat_values[kExp] - loss, low + 1);
                exp_lost = std::max<std::int64_t>(stat_values[kExp] - experience, 0);
                stat_values[kExp] = experience;
            }
        }
        const std::int64_t purse = stat_values[kGold], total = purse + stat_values[kGoldBank];
        std::int64_t lost = std::min(lvl, 20) * total / 100;
        if (total - lost < std::int64_t(lvl) * 500) lost = std::max<std::int64_t>(0, total - std::int64_t(lvl) * 500);
        lost = std::min(lost, purse);
        if (purse - lost > 0 && !fight.remote_monsters)       // a joined game: the host drops it (0x9c)
            loot.put({ .code = "gld", .gold = int(purse - lost) }, player.x, player.y, 1, fight.spawning.game, now_ms);
        stat_values[kGold] = 0;
        gold_lost = lost;
        d2d::log::info("died: {} experience and {} gold lost; {} gold on the ground", exp_lost, lost, purse - lost);
    }

auto World::make_corpse() -> void {
        Corpse corpse{ level, player.x, player.y, player.dir, {}, exp_lost * 75 / 100, fight.gfx() };
        for (auto item_it = character.items.begin(); item_it != character.items.end();)
            if (item_it->location == d2d::d2s::item_location::kEquipped) { corpse.items.push_back(std::move(*item_it)); item_it = character.items.erase(item_it); }
            else ++item_it;
        if (held) { corpse.items.push_back(std::move(*held)); corpse.items.back().location = 1; held.reset(); }   // ponytail: the held item's slot
        if (corpses.size() >= 16) corpses.erase(corpses.begin());
        corpses.push_back(std::move(corpse));
    }

auto World::take_corpse_items(std::size_t corpse_index, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        auto& corpse = corpses[corpse_index];
        character.stats.values[kExp] += corpse.exp;
        corpse.exp = 0;
        const auto& layout = game_data->inv_layout[std::size_t(character.header.cls % 7)];
        for (auto item_it = corpse.items.begin(); item_it != corpse.items.end();) {
            const bool worn = std::ranges::any_of(character.items, [&](const Item& worn_item) { return worn_item.location == d2d::d2s::item_location::kEquipped && worn_item.slot == item_it->slot; });
            if (!worn && item_it->slot >= d2d::d2s::body_location::kFirst && item_it->slot <= d2d::d2s::body_location::kLeftArmSwitch) { character.items.push_back(std::move(*item_it)); item_it = corpse.items.erase(item_it); continue; }
            std::optional<Item> held_item = *item_it;
            held_item->location = d2d::d2s::item_location::kStored;
            std::vector<const Item*> inv;
            for (const auto& x : character.items) if (x.location == d2d::d2s::item_location::kStored && x.panel == d2d::d2s::item_panel::kInventory) inv.push_back(&x);
            const auto [width, height] = d2d::rules::item_size(game_data->rules, held_item->code);
            if (const auto [column, row] = d2d::rules::free_spot(game_data->rules, inv, layout.cols, layout.rows, width, height); column >= 0
                && d2d::rules::put_in_grid(game_data->rules, character.items, held_item, d2d::d2s::item_panel::kInventory, layout.cols, layout.rows, column, row)) { item_it = corpse.items.erase(item_it); continue; }
            ++item_it;
        }
        if (corpse.items.empty()) corpses.erase(corpses.begin() + std::ptrdiff_t(corpse_index));
        cues.cue("item_pickup", now_ms, player.x, player.y);
    }

auto World::read_portal(std::vector<d2d::d2s::Item>::iterator scroll, std::uint32_t now_ms) -> void {
        if (level == &game_data->town || fight.dead() || fight.pmode >= 0) return;
        const bool book = scroll->code == "tbk";
        if (book && scroll->quantity <= 0) return;
        if (!fight.cast_scroll(book ? 220 : 219, now_ms)) return;
        if (book) --scroll->quantity;
        else character.items.erase(scroll);
        cues.cue("player_townportal_cast", now_ms, player.x, player.y);
    }

auto World::open_portal(std::uint32_t now_ms) -> void { open_portal_at(player.x, player.y + 0.6f, now_ms); }

auto World::open_portal_at(float portal_x, float portal_y, std::uint32_t now_ms) -> void {
        const auto& town = game_data->town;
        if (level == &town || town.portal_spot.first < 0) return;
        const auto [spot_x, spot_y] = level->nearest_free(portal_x, portal_y);
        const auto [town_x, town_y] = town.nearest_free(town.portal_spot.first, town.portal_spot.second);
        portal[0] = { level, spot_x, spot_y, now_ms };
        portal[1] = { &town, town_x, town_y, now_ms };
        cues.cue("object_townportal", now_ms, spot_x, spot_y);
        d2d::log::info("town portal: {} ({:.1f}, {:.1f}) <-> camp ({:.1f}, {:.1f})", level_name(*level), spot_x, spot_y, town_x, town_y);
    }

auto World::use_portal(std::uint32_t now_ms) -> void {
        if (take_portal < 0 || !portal[std::size_t(take_portal)].level) return;
        const auto& taken = portal[std::size_t(take_portal)];
        if (taken.level != level || std::hypot(taken.x - player.x, taken.y - player.y) > 2.f) {
            if (!player.walking) take_portal = -1;
            return;
        }
        cues.cue("player_townportal_enter", now_ms, player.x, player.y);
        if (take_portal == 2) {                       // to Tristram, by its own portal back (object 60)
            take_portal = -1;
            const Level* tristram = game_data->level(d2d::rules::CainQuest::kTristram);
            if (!tristram || tristram->ds1.width() == 0) { d2d::log::info("not implemented: level 38 (the Cairn Stones' portal)"); return; }
            const auto back = std::ranges::find(tristram->npcs, 60, &Npc::object_id);
            // ponytail: no object 60 among Tristram's presets (the server makes it); its twin stands at the map's centre (agent A's).
            const float back_x = back == tristram->npcs.end() ? float(tristram->ds1.width()) / 2 : back->x, back_y = back == tristram->npcs.end() ? float(tristram->ds1.height()) / 2 : back->y;
            if (portal[3].level != tristram) portal[3] = { tristram, back_x, back_y, now_ms };
            arrive(tristram, portal[3].x, portal[3].y + 0.6f, "the Cairn Stones' portal");
            return;
        }
        if (take_portal == 3) {                       // back to the Cairn Stones
            take_portal = -1;
            arrive(portal[2].level, portal[2].x, portal[2].y + 0.6f, "the Cairn Stones' portal");
            return;
        }
        const auto& other_end = portal[std::size_t(1 - take_portal)];
        take_portal = -1;
        arrive(other_end.level, other_end.x, other_end.y + 0.6f, "a town portal");
    }

auto World::deal(const Command& command) -> bool {
        const auto& tables = game_data->rules;
        const int clvl = int(character.stats.get(d2d::d2s::kLevel));
        if (const auto* trade = std::get_if<cmd::OpenTrade>(&command)) {
            if (std::size_t(trade->npc) >= level->npcs.size()) return true;
            store = trade->gamble ? d2d::rules::open_gamble(tables, level->npcs[std::size_t(trade->npc)].id, clvl) : open_store(*game_data, *level, trade->npc, rng);
            store.npc = trade->npc;
            store.header = character.header;
            events.push_back(ev::OpenUI{ ev::OpenUI::trade, trade->npc });
            return true;
        }
        if (const auto* hire = std::get_if<cmd::OpenHire>(&command)) {
            // ponytail: the server's offer count isn't traced; five.
            hire_offers.clear();
            for (int k = 0; k < 5; ++k)
                if (auto offer = d2d::rules::merc_offer(tables, character.expansion, 0, character.header.active_difficulty(), clvl, rng)) hire_offers.push_back(*offer);
            events.push_back(ev::OpenUI{ ev::OpenUI::hire, hire->npc });
            return true;
        }
        if (const auto* buy = std::get_if<cmd::Buy>(&command)) {
            if (store.npc < 0 || buy->stock < 0) return true;
            store.reduced = reduced_prices();
            if (buy->tab >= 0 && buy->tab < 4) store.tab = buy->tab;   // the client's tab: the world's stays where the store opened
            if (std::size_t(buy->stock) >= store.tabs[std::size_t(store.tab)].size()) return true;
            if (store.gamble) d2d::rules::store_gamble(tables, store, buy->stock, character.items, character.stats, rng);
            else d2d::rules::store_buy(tables, store, buy->stock, character.items, character.stats);
            return true;
        }
        if (const auto* sell = std::get_if<cmd::Sell>(&command)) {
            if (store.npc < 0) return true;
            if (held && held->id == sell->item) { character.items.push_back(std::move(*held)); held.reset(); }
            const auto found = std::ranges::find(character.items, sell->item, &d2d::d2s::Item::id);
            if (found != character.items.end()) d2d::rules::store_sell(tables, store, std::size_t(found - character.items.begin()), character.items, character.stats);
            return true;
        }
        if (const auto* repair = std::get_if<cmd::Repair>(&command)) {
            if (store.npc < 0) return true;
            store.reduced = reduced_prices();
            if (repair->item < 0) { d2d::rules::store_repair_all(tables, store, character.items, character.stats); return true; }
            const auto found = std::ranges::find(character.items, repair->item, &d2d::d2s::Item::id);
            if (found != character.items.end()) d2d::rules::store_repair(tables, store, *found, character.stats);
            return true;
        }
        // Cain's identify (FUN_00578460): 100 gold an item, all at once, unless
        // quest 4's done or its reward's due. ponytail: the purse only.
        if (std::holds_alternative<cmd::Identify>(command)) {
            auto& gold = character.stats.values[d2d::d2s::kGold];
            const int fee = d2d::rules::qbit(quests(), 4, 0) || d2d::rules::qbit(quests(), 4, 1) ? 0 : d2d::rules::unidentified(character.items) * 100;
            if (gold < fee) return true;
            gold -= fee;
            d2d::rules::identify_all(character.items);
            return true;
        }
        // A Scroll of Identify (isc, used up) or a Tome (ibk, a charge, stat
        // 0x46) on an unidentified item (FUN_00561ed0).
        // ponytail: no sound or message for an empty tome.
        if (const auto* with = std::get_if<cmd::IdentifyWith>(&command)) {
            auto& items = character.items;
            const auto scroll = std::ranges::find(items, with->scroll, &d2d::d2s::Item::id);
            const auto item = std::ranges::find(items, with->item, &d2d::d2s::Item::id);
            if (scroll == items.end() || item == items.end() || scroll == item || item->identified) return true;
            if (scroll->code == "ibk" && scroll->quantity > 0) --scroll->quantity;
            else if (scroll->code != "isc") return true;
            item->identified = true;
            if (scroll->code == "isc") items.erase(scroll);
            return true;
        }
        if (const auto* hire = std::get_if<cmd::Hire>(&command)) {
            if (hire->offer >= 0 && std::size_t(hire->offer) < hire_offers.size() && d2d::rules::hire(hire_offers[std::size_t(hire->offer)], character.header, character.stats))
                spawn_merc();
            return true;
        }
        // A hire NPC brings the dead merc back (FUN_00579c00): paid, full
        // life, beside the player (FUN_00579aa0).
        // ponytail: no 0x5b..0x5a messages (refused / done) back to the client.
        if (const auto* resurrect = std::get_if<cmd::ResurrectMerc>(&command)) {
            auto& header = character.header;
            if (std::size_t(resurrect->npc) >= level->npcs.size() || std::ranges::find(d2d::rules::kMercNpcs, level->npcs[std::size_t(resurrect->npc)].hc_idx) == d2d::rules::kMercNpcs.end())
                return true;
            const int merc_level = d2d::rules::merc_stats(game_data->rules, header.merc_type, header.merc_exp).level;
            if (d2d::rules::resurrect_merc(merc_level, header, character.stats)) {
                spawn_merc();
                d2d::log::info("the merc is back");
            }
            return true;
        }
        if (std::holds_alternative<cmd::CloseTrade>(command)) { store = {}; return true; }
        // Akara's reset (the 0x38 handler for her, hcIdx 0x94): while quest
        // 41 (the Den of Evil's reward) is open, stats and skills go back,
        // then it's used (FUN_0058fd50: bit 0 on, 1 off).
        if (const auto* respec = std::get_if<cmd::Respec>(&command)) {
            using d2d::rules::qbit;
            if (std::size_t(respec->npc) >= level->npcs.size() || level->npcs[std::size_t(respec->npc)].hc_idx != d2d::rules::monster_ids::kAkara
                || !qbit(quests(), d2d::rules::kRespecQuest, 1)) return true;
            const auto cls = std::size_t(std::clamp<int>(character.header.cls, 0, 6));
            const auto& start = game_data->class_start[cls];
            d2d::rules::respec(character.stats, { start.str, start.ene, start.dex, start.vit }, game_data->class_gains[cls]);
            d2d::rules::qset(quests(), d2d::rules::kRespecQuest, 0);
            d2d::rules::qset(quests(), d2d::rules::kRespecQuest, 1, false);
            d2d::log::info("Akara reset the stat and skill points");
            return true;
        }
        // Warriv's caravan (FUN_00579d60, hcIdx 0x9b): once quest 6 is done,
        // to Lut Gholein; Act 2 opens (FUN_005467e0: quest 7 bits 0 and 13)
        // and its town's waypoint is active.
        if (const auto* east = std::get_if<cmd::GoEast>(&command)) {
            using d2d::rules::qbit;
            if (std::size_t(east->npc) >= level->npcs.size() || level->npcs[std::size_t(east->npc)].hc_idx != d2d::rules::monster_ids::kWarriv
                || !qbit(quests(), d2d::rules::AndyQuest::kQuest, 0)) return true;
            if (!qbit(quests(), 7, 0)) { d2d::rules::qset(quests(), 7, 0); d2d::rules::qset(quests(), 7, 13); }
            andy.enter(quests(), level->id, d2d::rules::level_ids::kLutGholein);
            cain.enter(quests(), level->id, d2d::rules::level_ids::kLutGholein);   // the Rogues get him if he's still caged (FUN_00596de0 / FUN_00597310)
            set_waypoint(waypoint_index(*game_data, d2d::rules::level_ids::kLutGholein));
            auto& difficulty = character.header.difficulty[std::size_t(std::clamp(character.header.active_difficulty(), 0, 2))];
            difficulty = std::uint8_t((difficulty & ~7) | 0x80 | 1);   // the save's act: game.exe starts it in Lut Gholein
            went_east = true;
            d2d::log::info("not implemented: Act 2 (Warriv's caravan to Lut Gholein); the game ends here");
            return true;
        }
        // Charsi's imbue (FUN_00579d60, kind 0 at hcIdx 0x9a): while it's
        // due (quest 3 bit 1), the item in hand made anew, rare; the
        // reward's used (FUN_00591790).
        // ponytail: no S->C 0x58 result (waits for networking); the new item stays in hand.
        if (const auto* imbue = std::get_if<cmd::Imbue>(&command)) {
            if (std::size_t(imbue->npc) >= level->npcs.size() || level->npcs[std::size_t(imbue->npc)].hc_idx != d2d::rules::monster_ids::kCharsi
                || !d2d::rules::qbit(quests(), d2d::rules::ToolsQuest::kQuest, 1) || !held || !d2d::rules::imbuable(game_data->rules, *held)) return true;
            auto made = d2d::rules::imbue_item(game_data->rules, *held, int(character.stats.get(d2d::d2s::kLevel)), rng);
            made.location = held->location; made.panel = held->panel; made.column = held->column; made.row = held->row;
            held = std::move(made);
            tools.imbued(quests());
            d2d::log::info("Tools of the Trade: Charsi imbued the {}", held->code);
            return true;
        }
        if (const auto* run = std::get_if<cmd::Run>(&command)) { running = run->running; return true; }
        if (const auto* chat = std::get_if<cmd::Chat>(&command)) {
            if (chat->npc < 0 && std::size_t(talking[0]) < level->npcs.size()) {
                andy.talk_closed(level->npcs[std::size_t(talking[0])].hc_idx);
                burial.talk_closed(quests(), level->npcs[std::size_t(talking[0])].hc_idx);
                tools.talk_closed(quests(), level->npcs[std::size_t(talking[0])].hc_idx);
                cain.talk_closed(quests(), level->npcs[std::size_t(talking[0])].hc_idx);
            }
            talking = { chat->npc, -1, -1 };
            return true;
        }
        // FUN_0054c5d0 / FUN_00584f60: not within 10 s of the last level
        // change, only to another active waypoint; arrival by its preset
        // (tile + 3 subtiles, FUN_0066ad80), which lights up if it's dark.
        if (const auto* travel = std::get_if<cmd::Waypoint>(&command)) {
            const int index = waypoint_index(*game_data, travel->level);
            if (now - arrived_at < 10000 || std::size_t(travel->npc) >= level->npcs.size() || level->npcs[std::size_t(travel->npc)].operate_fn != d2d::rules::operate_fn::kWaypoint
                || travel->level == level->id || !character.header.waypoint(character.header.active_difficulty(), index)) return true;
            const Level* destination = game_data->level(travel->level);
            if (!destination || destination->ds1.width() == 0) { d2d::log::info("not implemented: level {} (a waypoint)", travel->level); return true; }
            // Its preset (objects.txt SubClass 0x40): its room makes it as the player lands.
            const auto& subclass = game_data->obj_subclass;
            const auto unit = std::ranges::find_if(destination->units, [&](const auto& preset) {
                return preset.type == d2d::rules::unit_type::kObject && preset.id >= 0 && std::size_t(preset.id) < subclass.size() && (subclass[std::size_t(preset.id)] & d2d::rules::object_ids::kSubclassWaypoint);
            });
            const bool known = unit != destination->units.end();
            const float arrive_x = known ? std::floor((float(unit->x) + 0.5f) / 5) + 0.6f : float(destination->ds1.width()) / 2;
            const float arrive_y = known ? std::floor((float(unit->y) + 0.5f) / 5) + 0.6f : float(destination->ds1.height()) / 2;
            arrive(destination, arrive_x, arrive_y, "a waypoint");
            if (const auto found = std::ranges::find(destination->npcs, 23, &Npc::operate_fn); found != destination->npcs.end() && found->mode == "NU")
                operated.try_emplace({ destination, int(found - destination->npcs.begin()) }, now);
            return true;
        }
        if (const auto* message = std::get_if<cmd::QuestMessage>(&command)) {   // only what that NPC has to say
            if (std::size_t(message->npc) >= level->npcs.size()) return true;
            if (level->npcs[std::size_t(message->npc)].operate_fn == d2d::rules::operate_fn::kMoldyTome) {   // the tome's 127 (FUN_00594960: no NPC check)
                if (message->string == d2d::rules::TowerQuest::kTome && !closed_at_join[std::size_t(d2d::rules::TowerQuest::kQuest)]) { tower.read_tome(quests()); d2d::log::info("The Forgotten Tower: read the Moldy Tome"); }
                return true;
            }
            const int hc_idx = level->npcs[std::size_t(message->npc)].hc_idx;
            if (!std::ranges::contains(quest_talk(hc_idx), message->string, &d2d::rules::QuestMsg::string)) return true;
            if (tower.said(quests(), hc_idx, message->string)) { d2d::log::info("The Forgotten Tower: done"); chain(5); }   // FUN_00594960
            if (andy.said(quests(), hc_idx, message->string)) {
                d2d::log::info("Sisters to the Slaughter: done, Warriv's caravan goes east");
            }
            const int tools_was = tools.state;
            if (tools.said(quests(), hc_idx, message->string, holding_malus())) {   // FUN_00544160: the malus goes
                if (tools_was == 4 && tools.state == 5) chain(3);   // FUN_00591490: its own +0xf0
                else if (tools.active && tools.carried_in) andy.chain();   // data+0xa1: quest 6's +0xf0
                if (held && held->code == "hdm") held.reset();
                else if (const auto malus = std::ranges::find(character.items, std::string("hdm"), &d2d::d2s::Item::code); malus != character.items.end()) character.items.erase(malus);
                d2d::log::info("Tools of the Trade: the malus is back, Charsi's imbue is due");
            }
            using Said = d2d::rules::CainQuest::Said;
            const auto scroll = std::ranges::find(character.items, std::string_view("bks"), &d2d::d2s::Item::code);
            const bool cain_flagged = cain.rescued_flag;
            const auto cain_said = cain.said(quests(), hc_idx, message->string, scroll != character.items.end());
            if (cain_said == Said::ring && !cain_flagged && d2d::rules::qbit(quests(), 4, 13)) chain(4);   // FUN_00592250: the game's (4, 13) goes on
            if (cain_said == Said::decipher) {        // ponytail: bks becomes bkd where it lies (same size), not a new item (agent A's)
                scroll->code = "bkd";
                d2d::log::info("Search for Cain: Akara deciphered the scroll");
            }
            if (cain_said == Said::ring) {            // FUN_005466b0: into the inventory, else at the feet
                static constexpr int kIlvl[3] = { 7, 30, 60 };
                const int diff = std::clamp(character.header.active_difficulty(), 0, 2);
                loot.put({ .code = "rin", .quality = diff ? 6 : 4 }, player.x, player.y, kIlvl[diff], fight.spawning.game, now);
                loot.take(loot.ground.size() - 1);
                cues.cue("item_ring", now, player.x, player.y);   // FUN_005458e0(4): S->C 0x5d flags 2, the client's item_ring (FUN_004a2cb0)
                d2d::log::info("Search for Cain: done, Akara's ring");
            }
            const int den_was = den.state, burial_was = burial.state;
            if (den.said(quests(), hc_idx, message->string)) {
                ++character.stats.values[d2d::d2s::kSkillPts];
                d2d::log::info("Den of Evil: Akara's reward, a skill point");
                if (den_was != 5 && den.state == 5) chain(1);   // FUN_0058fdd0
            }
            if (burial.said(quests(), hc_idx, message->string)) {
                kashya_merc();
                if (burial_was != 5 && burial.state == 5) chain(2);   // FUN_00590980
            }
            return true;
        }
        return false;
    }

auto World::apply(const Command& command, std::uint32_t now_ms) -> void {
        // The character's own: through whatever the player's doing.
        if (const auto* stat_point = std::get_if<cmd::StatPoint>(&command)) {
            if (stat_point->stat < 0 || stat_point->stat > 3 || stat_point->count < 1) return;
            d2d::rules::spend_stat_points(character.stats, stat_point->stat, std::min<int>(stat_point->count, int(character.stats.get(d2d::d2s::kStatPts))),
                                          game_data->class_gains[std::size_t(character.header.cls)]);
            return;
        }
        if (const auto* skill_point = std::get_if<cmd::SkillPoint>(&command)) {
            if (character.stats.get(d2d::d2s::kSkillPts) > 0
                && d2d::rules::can_learn(game_data->rules, character.header.cls, skill_point->skill, character.stats.skills, int(character.stats.get(d2d::d2s::kLevel))))
                d2d::rules::learn_skill(game_data->rules, character.header.cls, skill_point->skill, character.stats.skills, character.stats);
            return;
        }
        if (const auto* select = std::get_if<cmd::SelectSkill>(&command)) {
            (select->left ? character.header.left_skill : character.header.right_skill) = std::uint32_t(select->skill);
            if (!select->left) if (const auto* skill = game_data->skills.get(select->skill)) fight.aura = skill->aura ? select->skill : 0;
            return;
        }
        if (const auto* use = std::get_if<cmd::UseItem>(&command)) {
            const auto found = std::ranges::find(character.items, use->item, &d2d::d2s::Item::id);
            if (found != character.items.end() && (found->code == "tsc" || found->code == "tbk")) read_portal(found, now_ms);
            else fight.drink_item(use->item, now_ms);
            return;
        }
        if (const auto* use_belt = std::get_if<cmd::UseBelt>(&command)) {
            if (use_belt->slot >= 0 && use_belt->slot < 4 && !fight.dead()) fight.drink(use_belt->slot, now_ms);
            return;
        }
        // The cursor (cursor.hpp works out which from a click).
        const auto& tables = game_data->rules;
        const int cls = character.header.cls;
        if (const auto* to_cursor = std::get_if<cmd::ToCursor>(&command)) {
            const auto found = std::ranges::find(character.items, to_cursor->item, &d2d::d2s::Item::id);
            if (!held && found != character.items.end()) d2d::rules::pick_up(character.items, held, std::size_t(found - character.items.begin()));
            return;
        }
        if (const auto* drop = std::get_if<cmd::Drop>(&command)) {           // FUN_0054ab40: the item in hand, not while dead
            if (held && held->id == drop->item && !fight.dead()) {
                loot.enter(level);
                loot.place(std::move(*held), player.x, player.y, now_ms);
                held.reset();
            }
            return;
        }
        if (const auto* to_grid = std::get_if<cmd::ToGrid>(&command)) {
            const auto* layout = to_grid->panel == d2d::d2s::item_panel::kInventory ? &game_data->inv_layout[std::size_t(cls)] : to_grid->panel == d2d::d2s::item_panel::kCube ? &game_data->cube_layout
                          : to_grid->panel == d2d::d2s::item_panel::kStash ? &game_data->stash_layout[character.expansion ? 1 : 0] : nullptr;
            if (held && layout) d2d::rules::put_in_grid(tables, character.items, held, to_grid->panel, layout->cols, layout->rows, to_grid->col, to_grid->row);
            return;
        }
        if (const auto* to_body = std::get_if<cmd::ToBody>(&command)) {
            if (held && to_body->slot >= d2d::d2s::body_location::kFirst && to_body->slot <= d2d::d2s::body_location::kLast) d2d::rules::equip(tables, character.items, held, to_body->slot, wearer(*game_data, cls, character.items, character.stats));
            return;
        }
        if (const auto* to_belt = std::get_if<cmd::ToBelt>(&command)) {
            const auto& belt = game_data->belts[std::size_t(belt_index(*game_data, character.items))];
            if (held && to_belt->box >= 0 && to_belt->box < belt.boxes) d2d::rules::put_in_belt(tables, character.items, held, to_belt->box, belt.boxes);
            return;
        }
        if (deal(command)) return;
        if (fight.pmode >= 0) return;
        const bool in_moor = level != &game_data->town;
        auto walk_to = [&](float x, float y, bool fresh) {
            target_x = x; target_y = y;
            player.walking = true;
            interact_npc = -1;
            if (fresh) take_portal = take_corpse = -1;
            if (!fresh) return;
            fight.attack_mon = pick_item = -1;
            take_warp = -1;                                                  // a click on a warp: go through it
            for (std::size_t i = 0; i < level->warps.size(); ++i)
                if (std::hypot(level->warps[i].x + 0.5f - x, level->warps[i].y + 0.5f - y) < 2.f) take_warp = int(i);
        };
        if (const auto* move = std::get_if<cmd::Move>(&command)) { walk_to(move->x, move->y, move->fresh); return; }
        if (const auto* pickup = std::get_if<cmd::Pickup>(&command)) {                  // walk to it, pick it up
            const int ground_index = loot.index_of(pickup->item);
            if (ground_index < 0) return;
            walk_to(loot.ground[std::size_t(ground_index)].x, loot.ground[std::size_t(ground_index)].y, true);
            pick_item = pickup->item;
            return;
        }
        if (const auto* interact = std::get_if<cmd::Interact>(&command)) {               // walk to it; operate or talk on arrival
            if (interact->npc <= -3000 && interact->npc > -3000 - int(corpses.size())) {   // one's corpse (-3000 - k)
                const auto& body = corpses[std::size_t(-3000 - interact->npc)];
                if (body.level != level) return;
                walk_to(body.x, body.y, true);
                take_corpse = -3000 - interact->npc;
                return;
            }
            if (interact->npc <= -2000 && interact->npc > -2004) {                       // a town portal (the client names them -2000 - k)
                const auto& entry_portal = portal[std::size_t(-2000 - interact->npc)];
                if (entry_portal.level != level) return;
                walk_to(entry_portal.x, entry_portal.y, true);
                take_portal = -2000 - interact->npc;
                return;
            }
            if (std::size_t(interact->npc) >= level->npcs.size()) return;
            const auto& npc = level->npcs[std::size_t(interact->npc)];
            const auto& state = npc_states[std::size_t(interact->npc)];
            const float npc_x = npc.path.empty() ? npc.x : state.x, npc_y = npc.path.empty() ? npc.y : state.y;
            walk_to(npc_x, npc_y, true);
            const bool menu = std::ranges::any_of(kNpcMenus, [&](const NpcMenu& menu_entry) { return menu_entry.hc_idx == npc.hc_idx; });
            const bool usable = is_door(npc.operate_fn) || npc.operate_fn == d2d::rules::operate_fn::kWell || npc.operate_fn == d2d::rules::operate_fn::kMoldyTome || (operable(npc.operate_fn) && !npc.preoperated && !operated.contains({ level, interact->npc }));
            const bool quest_object = npc.operate_fn == d2d::rules::operate_fn::kCairnStone || npc.operate_fn == d2d::rules::operate_fn::kGibbet || npc.operate_fn == d2d::rules::operate_fn::kInifussTree;   // the quest says what it does
            if (npc.operate_fn == d2d::rules::operate_fn::kStash || npc.operate_fn == d2d::rules::operate_fn::kWaypoint || npc.operate_fn == d2d::rules::ToolsQuest::kStand || usable || quest_object || (npc.root == "monsters" && menu)) interact_npc = interact->npc;
            return;
        }
        const auto& use_skill = std::get<cmd::UseSkill>(command);
        const auto* skill = game_data->skills.get(use_skill.skill);
        const int monster_index = fight.monster_index(use_skill.unit);
        const bool live = monster_index >= 0 && fight.monsters[std::size_t(monster_index)].alive();
        if (skill && self_cast(*skill)) {                                            // Holy Shield: where the player stands
            if (fight.cast(use_skill.skill, now_ms)) player.walking = false;
        } else if (skill && skill->srvdofunc == ServerDoFunction::kWhirlwind && monster_index < 0) {                      // Whirlwind to that point (FUN_005d8f50)
            fight.move_x = use_skill.x; fight.move_y = use_skill.y;
            fight.attack_mon = -1;
            fight.attack_skill = use_skill.skill;
            interact_npc = pick_item = -1;
            player.walking = false;
            player.dir = direction16(fight.move_x - player.x, fight.move_y - player.y);
            fight.start_swing(now_ms);
        } else if (skill && !live && (fight.missile_skill(*skill) || fight.spot_skill(*skill)) && (in_moor || skill->in_town)) {
            interact_npc = pick_item = -1;                                   // a missile, Teleport, Corpse Explosion at the spot
            if (fight.cast_missile(use_skill.skill, use_skill.x, use_skill.y, now_ms)) player.walking = false;
        } else if (skill && !live && fight.summon_skill(*skill) && in_moor) {        // Raise Skeleton: the corpse there
            interact_npc = pick_item = -1;
            if (fight.cast_summon(use_skill.skill, use_skill.x, use_skill.y, now_ms)) player.walking = false;
        } else if (live) {                                                   // walk up to it, then attack
            target_x = use_skill.x; target_y = use_skill.y;
            player.walking = true;
            interact_npc = pick_item = -1;
            fight.attack_mon = monster_index;
            fight.attack_skill = use_skill.skill;
        }
    }

// A frame's stamina (FUN_00580c20 / FUN_00580810 event 3): a run outside
// town drains it (FUN_0057f240), at 0 the run is a walk (FUN_0057f090: no
// run at stamina 0); then it regens by mode (FUN_00580500).
// ponytail: the run stays a run while stamina lasts and walks at 0 each
// frame, not at the next move as game.exe; the same since a walk at 0 never
// regens.
auto World::stamina_frame() -> void {
        using namespace d2d::d2s;
        if (fight.dead()) return;
        const bool town = level == &game_data->town;
        auto& stamina = character.stats.values[kStamina];
        const bool run = player.walking && fight.pmode < 0 && running && stamina > 0;
        if (run && !town) {
            int armor_speed = 0;
            for (const auto& item : character.items)
                if (item.location == d2d::d2s::item_location::kEquipped && item.slot == d2d::d2s::body_location::kTorso)
                    if (const auto found = game_data->rules.item_base.find(item.code); found != game_data->rules.item_base.end()) armor_speed = found->second.speed;
            stamina = std::max<std::int64_t>(0, stamina - d2d::rules::stamina_drain(game_data->run_drain[std::size_t(std::max(character.character_class, 0))],
                                                                                  armor_speed, int(fight.psum[kStaminaDrainPercent])));
        }
        const int mode = fight.pmode >= 0 ? -1 : player.walking ? (run ? kModeRN : town ? kModeTW : kModeWL) : town ? kModeTN : kModeNU;
        stamina = d2d::rules::stamina_regen(stamina, character.stats.values[kMaxStamina], mode, int(fight.psum[kStaminaRecoveryBonus]));
    }

auto World::tick(const std::vector<Command>& cmds, std::uint32_t now_ms, std::uint32_t last_ms) -> void {
        const float elapsed = float(now_ms - last_ms) / 1000.f;
        now = now_ms;
        // The look follows what's worn (compcode::look, the save header's bytes).
        if (!game_data->item_pieces.empty() && !game_data->comp.empty()) {
            std::vector<std::string_view> active_states;
            for (const auto& state : fight.self_states) if (const auto* skill = game_data->skills.get(state.skill)) active_states.push_back(skill->aurastate);
            character.appearance = game_data->look_of(character.items, active_states);
        }
        // Levels: finished builds come in; the ones next to the player's
        // start building when it changes (GameData::level).
        game_data->poll_levels();
        if (level != wanted_near) { want_nearby(*game_data, *level); wanted_near = level; }
        item_ids();
        // The day moves a frame a tick (40 ms); a stall doesn't fast-forward it.
        if (now_ms - day_at > 1000) day_at = now_ms;
        for (; now_ms - day_at >= kTickMs; day_at += kTickMs) {
            day.step();
            if (andy.tick() && level->id == d2d::rules::level_ids::kCatacombsLevel4) fight.portal_due = true;   // tick 10 of her death (FUN_00596490)
            burial.tick();
            tower.tick();
            tower_treasure(day_at);
            stamina_frame();
        }
        if (fight.boost.until && now_ms >= fight.boost.until) {     // the state runs out
            if (shrine_code(fight.boost.shrine) == 14) character.stats.values[d2d::d2s::kStamina] = character.stats.values[d2d::d2s::kMaxStamina];
            fight.boost = {};
        }
        fight.update_fighters(now_ms);
        spring_traps(now_ms);
        // Used shrines and chests: OP while it plays, then ON; a shrine back
        // to NU after its reset time (Shrines.txt Reset x 1200 + 1 frames;
        // 0 never).
        for (auto entry = operated.begin(); entry != operated.end();) {
            const auto& [key, when_ms] = *entry;
            const auto npc_index = std::size_t(key.second);
            const auto& npc = key.first->npcs[npc_index];
            const int reset = npc.operate_fn == d2d::rules::operate_fn::kShrine && std::size_t(npc.shrine) < game_data->shrines.size() ? game_data->shrines[std::size_t(npc.shrine)].reset : 0;
            const bool back = reset > 0 && now_ms - when_ms >= d2d::rules::shrine_reset_frames(reset) * kTickMs;
            if (key.first == level && npc_index < npc_states.size()) {
                npc_states[npc_index].mode = back ? std::string_view{} : now_ms - when_ms < std::uint32_t(npc.op_frames) * 40u ? "OP" : "ON";
                // Its message overhead (ShrMsgN, 3683 + row: FUN_00661110 on
                // "%d", 4 chars x 8 + 125 frames).
                npc_states[npc_index].says = std::uint16_t(npc.operate_fn == d2d::rules::operate_fn::kShrine && !back && now_ms - when_ms < 157u * kTickMs ? 3683 + npc.shrine : 0);
            }
            entry = back ? operated.erase(entry) : std::next(entry);
        }
        for (const auto& [key, door] : doors)                  // doors in the mode they were left in
            if (key.first == level && std::size_t(key.second) < npc_states.size())
                std::tie(npc_states[std::size_t(key.second)].mode, npc_states[std::size_t(key.second)].mode_ms) = std::pair{ kObjectModes[std::size_t(door.mode)], door.when };
        Crowd crowd;                           // who's in whose way this frame
        crowd.units.push_back(&player);
        if (merc) crowd.units.push_back(&merc->unit);
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            if (level->npcs[i].root != "objects" && !npc_states[i].hidden) crowd.units.push_back(&npc_states[i]);
        const bool in_moor = level != &game_data->town;     // outside: this level's monsters are about
        if (in_moor) fight.crowd(crowd);       // the monsters around the player
        // Dead: the death plays out, then Resurrect respawns in camp; a
        // swing or a flinch holds the player in place until it ends, and
        // an attack goes on while the button's held (the left skill sent
        // again).
        const bool button = std::ranges::any_of(cmds, [](const Command& command) {
            const auto* use_skill = std::get_if<cmd::UseSkill>(&command);
            return std::holds_alternative<cmd::Move>(command) || (use_skill && use_skill->left);
        });
        const bool resurrect = std::ranges::any_of(cmds, [](const Command& command) { return std::holds_alternative<cmd::Resurrect>(command); });
        if (fight.player_modes(button, now_ms, elapsed) && resurrect) respawn(now_ms);
        if (fight.pmode != last_pmode) {                 // death's stages: the penalty as it starts, the corpse as it ends
            if (fight.pmode == kModeDT) death_penalty(now_ms);
            if (fight.pmode == kModeDD) make_corpse();
            last_pmode = fight.pmode;
        }
        if (!fight.dead()) {
        const bool busy = fight.pmode >= 0;
        for (const auto& command : cmds) if (!std::holds_alternative<cmd::Resurrect>(command)) apply(command, now_ms);
        // Close enough to the stash: open it with the inventory.
        // ponytail: 2 cells, not D2's per-object operate range.
        if (interact_npc >= 0) {
            const auto& npc = level->npcs[std::size_t(interact_npc)];
            const auto& state = npc_states[std::size_t(interact_npc)];
            const float npc_x = npc.path.empty() ? npc.x : state.x, npc_y = npc.path.empty() ? npc.y : state.y;
            if (std::hypot(npc_x - player.x, npc_y - player.y) < 2.f) {
                if (operable(npc.operate_fn) || npc.operate_fn == d2d::rules::operate_fn::kMoldyTome || npc.operate_fn == d2d::rules::ToolsQuest::kStand || npc.operate_fn == d2d::rules::operate_fn::kCairnStone || npc.operate_fn == d2d::rules::operate_fn::kGibbet || npc.operate_fn == d2d::rules::operate_fn::kInifussTree) {
                    operate(interact_npc, now_ms);
                } else if (npc.operate_fn == d2d::rules::operate_fn::kStash) {
                    events.push_back(ev::OpenUI{ ev::OpenUI::stash, interact_npc });
                } else if (npc.operate_fn == d2d::rules::operate_fn::kWaypoint) {
                    // FUN_00584e30: touching activates this level's waypoint
                    // (the town's, index 0, always is); an unlit one lights
                    // up (mode 1 → 2) and the panel waits for the next touch.
                    set_waypoint(0);
                    set_waypoint(waypoint_index(*game_data, level->id));
                    if (npc.mode == "NU" && !operated.contains({ level, interact_npc })) operated[{ level, interact_npc }] = now_ms;
                    else events.push_back(ev::OpenUI{ ev::OpenUI::waypoint, interact_npc });
                } else {
                    if (d2d::rules::is_healer(npc.hc_idx)) { d2d::rules::heal(character.stats); fight.cure(now_ms); }
                    // In Hell, a Den of Evil done before the reset existed
                    // opens it on meeting Akara (FUN_0058fd20: quest 41 bits
                    // 13 and 1).
                    if (npc.hc_idx == d2d::rules::monster_ids::kAkara && character.header.active_difficulty() == 2) {
                        auto& quest_bits = quests();
                        using d2d::rules::qbit;
                        constexpr int kRespec = d2d::rules::kRespecQuest;
                        if (qbit(quest_bits, d2d::rules::DenQuest::kQuest, 0) && !qbit(quest_bits, kRespec, 1) && !qbit(quest_bits, kRespec, 0)) { d2d::rules::qset(quest_bits, kRespec, 13); d2d::rules::qset(quest_bits, kRespec, 1); }
                    }
                    events.push_back(ev::OpenUI{ ev::OpenUI::talk, interact_npc, quest_talk(npc.hc_idx) });
                }
                player.walking = false; interact_npc = -1;
            } else if (!player.walking) {
                interact_npc = -1;                         // blocked on the way
            } else {
                target_x = npc_x; target_y = npc_y;              // follow a walking NPC
            }
        }
        if (pick_item >= 0 && loot.index_of(pick_item) < 0) pick_item = -1;   // someone took it
        if (pick_item >= 0 && !busy) {
            const auto ground_index = std::size_t(loot.index_of(pick_item));
            const auto& ground_item = loot.ground[ground_index];
            if (std::hypot(ground_item.x - player.x, ground_item.y - player.y) <= 1.f) {
                const bool malus = ground_item.item.code == "hdm";
                loot.take(ground_index);
                if (malus && holding_malus() && tools.picked_up(quests())) d2d::log::info("Tools of the Trade: the malus picked up (the player's line, FUN_00553380)");
                pick_item = -1; player.walking = false; player.path.clear();
            } else {
                target_x = ground_item.x; target_y = ground_item.y; player.walking = true;
            }
        }
        if (autoloot_gold && !fight.dead())                  // d2d: walking over gold picks it up (game.exe wants a click)
            for (std::size_t i = loot.ground.size(); i-- > 0;)
                if (const auto& ground_item = loot.ground[i]; ground_item.item.code == "gld" && std::hypot(ground_item.x - player.x, ground_item.y - player.y) <= 0.5f) {
                    if (loot.index_of(pick_item) == int(i)) pick_item = -1;
                    loot.take(i);
                }
        bool to_unit = interact_npc >= 0 && std::size_t(interact_npc) < level->npcs.size() && level->npcs[std::size_t(interact_npc)].root == "monsters";
        if (const auto target = fight.engage(now_ms)) { std::tie(target_x, target_y) = *target; player.walking = true; to_unit = true; }
        if (player.walking && fight.pmode < 0) {
            // A route to the target (path type 7), re-planned when the
            // target moves off its end (dragging, a walking NPC) and when
            // it ran out short of it (FUN_006503f0's last test).
            // ponytail: a target unit re-paths at 0.3 cells, not game.exe's
            // 5 subtiles off SP2.
            const auto subtile = [](float value) { return int(std::floor(value * 5)); };
            if (player.path.empty() || std::hypot(player.goal_x - target_x, player.goal_y - target_y) > 0.3f) {
                player.path = player_walk(*level, player.x, player.y, target_x, target_y, to_unit, crowd, &player);
                player.goal_x = target_x; player.goal_y = target_y;
            }
            const auto save_class = std::size_t(std::max(character.character_class, 0));
            // FUN_00623f50: the walk velocity x a % (at least 25) of 100 (run: run x 100 / walk),
            // faster run/walk's effective % (150 x v / (150 + v)) and chill's -50.
            const int walk = std::max(game_data->walk_velocity[save_class], 1);
            const int base = running && character.stats.values[d2d::d2s::kStamina] > 0 ? game_data->run_velocity[save_class] * 100 / walk : 100;
            const float vel = float(walk * std::max(base + d2d::rules::effective_speed(fight.player_combat.frw, 150) + fight.chill_rate(now_ms), 25)) / 100.f;
            const float before_x = player.x, before_y = player.y;
            player.walking = follow_path(*level, player, cells_per_sec(vel) * elapsed, crowd);
            // The merc's think reads the player's footsteps (FUN_00580c20): walking or
            // running, 25 ms on from the last, over 45 (squared) from it.
            if (const int step_x = int(std::floor(player.x * 5)), step_y = int(std::floor(player.y * 5)); now_ms > fight.footstep_ms + 25) {
                const auto [last_x, last_y] = fight.footsteps[std::size_t(fight.footstep_cursor == 0 ? 0x13 : fight.footstep_cursor - 1)];
                if ((step_x - last_x) * (step_x - last_x) + (step_y - last_y) * (step_y - last_y) > 0x2d) fight.footstep(step_x, step_y, now_ms);
            }
            if (!player.walking) {
                player.path.clear();
                // Short of the target with a step made: it paths again from here.
                player.walking = (subtile(player.x) != subtile(target_x) || subtile(player.y) != subtile(target_y))
                                 && (player.x != before_x || player.y != before_y);
            }
        }
        npc_patrol(*game_data, *level, npc_states, talking, now_ms, elapsed, crowd, &player);
        cain_step(now_ms, elapsed);
        for (const auto& neighbour : level->nearby) {           // over the edge, still in play
            auto& states = other_npcs[neighbour.level];
            if (states.size() > neighbour.level->npcs.size()) states = npc_start(*neighbour.level);   // a new game's
            grow_states(states, *neighbour.level);
            npc_patrol(*game_data, *neighbour.level, states, { -1, -1, -1 }, now_ms, elapsed, Crowd{});
        }
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            npc_states[i].alert = den.alert(quests(), level->npcs[i].hc_idx) || andy.alert(quests(), level->npcs[i].hc_idx)
                                  || burial.alert(quests(), level->npcs[i].hc_idx) || tower.alert(quests(), level->npcs[i].hc_idx)
                                  || tools.alert(quests(), level->npcs[i].hc_idx, holding_malus(), int(character.stats.get(d2d::d2s::kLevel)))
                                  || cain.alert(quests(), level->npcs[i].hc_idx, carries("bks"));
        // The player's mode as the merc's think reads it: 2 walk, 3 run, 6 walk in town.
        fight.owner_mode = !player.walking ? 1 : running && character.stats.values[d2d::d2s::kStamina] > 0 ? 3 : in_moor ? 2 : 6;
        fight.world(in_moor, now_ms, elapsed, crowd);
        den_count(now_ms);
        if (den_log_at && now_ms >= den_log_at) { den.log = 5; den_log_at = 0; }
        use_warp();
        if (fight.portal_due) { fight.portal_due = false; open_portal(now_ms); }
        if (take_corpse >= 0 && std::size_t(take_corpse) < corpses.size() && corpses[std::size_t(take_corpse)].level == level) {
            const auto& corpse = corpses[std::size_t(take_corpse)];
            if (std::hypot(corpse.x - player.x, corpse.y - player.y) < 2.f) { take_corpse_items(std::size_t(take_corpse), now_ms); take_corpse = -1; }
            else if (!player.walking) take_corpse = -1;
        }
        use_portal(now_ms);
        }
        for (const auto& kill : fight.kills) if (kill.type == d2d::rules::monster_ids::kAndariel) andariel_died(kill, now_ms);   // a pet's kill too, the player dead
        for (const auto& kill : fight.kills) if (kill.type == d2d::rules::monster_ids::kBloodRaven) blood_raven_died(now_ms);
        for (const auto& kill : fight.kills) if (kill.super == d2d::rules::TowerQuest::kCountess) countess_died(now_ms);
        fight.kills.clear();
        cross_level();
        fight.rooms_up(*level, player.x, player.y, false);
        grow_states(npc_states, *level);
        if (!fight.dead()) fight.apply_regen(now_ms, last_ms);
    }

}  // namespace d2d::game
