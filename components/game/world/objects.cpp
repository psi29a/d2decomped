// SPDX-License-Identifier: GPL-3.0-or-later
// The World (world.hpp): objects: operating them, doors, explosions and traps. The rest is in
// this directory.
#include "world.hpp"

#include "ai.hpp"
#include "ai/units.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "level.hpp"
#include "log.hpp"

#include <combat.hpp>
#include <d2s_items.hpp>
#include <drops.hpp>
#include <level_ids.hpp>
#include <missiles.hpp>
#include <monsters.hpp>
#include <object_ids.hpp>
#include <protocol.hpp>
#include <quest_clock.hpp>
#include <quest_music.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <town_npcs.hpp>
#include <uniques.hpp>
#include <units.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::game {

// A blast's damage taken (FUN_0057c6c0): life under one point after it is 0.
static void take_blast(std::int64_t& life, int taken) {
    if (taken <= 0) return;
    life -= taken;
    if (life < 256) life = 0;
}

// Operating a shrine (FUN_00583c70: its Shrines.txt effect) or a chest
// (FUN_00585f60 / FUN_00585b90: it opens, its act's chest treasure class
// drops at the area level).
// In reach as World::tick tests it (FUN_005b0f50: rules::object_reach).
auto World::drop_wirt_gold(std::uint32_t now_ms) -> void {
    // FUN_00594630: one pile every ten frames, on the corpse's level.
    for (auto entry = wirt_gold.begin(); entry != wirt_gold.end();) {
        auto& [key, drop] = *entry;
        if (now_ms < drop.next_ms) { ++entry; continue; }
        const auto& [where, npc_index] = key;
        const auto& corpse = where->npcs[std::size_t(npc_index)];
        if (drop.left == 0) drop.left = 10 + fight.spawning.quests(10);
        loot.put({ .code = "gld" }, corpse.x, corpse.y, drop.item_level, fight.spawning.game, now_ms, false, 0, where);
        drop.next_ms = now_ms + 10 * kTickMs;
        entry = --drop.left == 0 ? wirt_gold.erase(entry) : std::next(entry);
    }
}

auto World::wall_to(const Npc& object, int x, int y, int size) const -> bool {
    const int to_x = int(std::floor(object.x * 5)), to_y = int(std::floor(object.y * 5));
    const int left = to_x - object.size_x / 2, top = to_y - object.size_y / 2;   // its footprint (FUN_0064d7c0)
    const auto wall = [&](int at_x, int at_y) {
        const bool own = at_x >= left && at_x < left + object.size_x && at_y >= top && at_y < top + object.size_y;
        return !own && level->blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x804);
    };
    return d2d::rules::sight_blocked(x, y, size, to_x, to_y, object.size_x, wall);
}

auto World::blast(const Npc& object, float x, float y, int size, std::int64_t life, int unit_level, int dexterity, int defense) -> int {
    if (in_town(level)) return 0;
    if (wall_to(object, int(std::floor(x * 5)), int(std::floor(y * 5)), size)) return 0;
    return d2d::rules::object_blast(life, unit_level, dexterity, defense, object.seed);
}

// A chest round's drops put as they're rolled (FUN_0055a550 for chest_round),
// round `object` on `onto` (null: this level).
auto World::chest_place(const Npc& object, int here, std::uint32_t now_ms, const Level* onto) -> d2d::rules::Placement {
    return [this, &object, here, now_ms, onto](const d2d::rules::Drop& dropped, int) {
        return fight.remote_monsters || loot.put(dropped, object.x, object.y, here, fight.spawning.game, now_ms, false, 0, onto);   // a joined game: the host drops them (S->C 0x9c)
    };
}

auto World::operate(int npc_index, std::uint32_t now_ms, int force ) -> void {   // force (devctl): the shrine row / trap to play
        using namespace d2d::d2s;
        const auto& object = level->npcs[std::size_t(npc_index)];
        const int diff = actor->character.header.active_difficulty();
        if (object.operate_fn == d2d::rules::ToolsQuest::kStand) { malus_stand(npc_index, now_ms); return; }
        if (is_door(object.operate_fn)) { operate_door(npc_index, now_ms); return; }
        if (object.operate_fn == d2d::rules::operate_fn::kExplodingBarrel) { explode(npc_index, now_ms); return; }
        const auto& area_levels = game_data->area_level;
        auto alvl = [&](int id) { return std::size_t(id) < area_levels.size() ? area_levels[std::size_t(id)][std::size_t(std::clamp(diff, 0, 2))] : 1; };
        const int here = alvl(level->id);
        // Open a container (open_container) on the level's object seed, its
        // rounds its level's act's chest class (chest_round, FUN_00585b90) off its own unit seed;
        // the items are made at the area level (FUN_0055a550), the extras too.
        auto open = [&] {
            const int act = d2d::rules::level_act(level->id);
            const auto [low, high] = d2d::rules::kChestLevels[std::size_t(act)];
            const auto treasure_class = d2d::rules::chest_tc(act, diff, here, alvl(low), alvl(high));
            std::vector<d2d::rules::Drop> drops;
            const auto opened = d2d::rules::open_container(object.operate_fn, object.object_id, object.locked, object.sparkle, fight.spawning.objects,
                                                           [&](int forced) { return d2d::rules::chest_round(game_data->rules, treasure_class, object.seed, drops, forced, chest_place(object, here, now_ms)); });
            for (const auto& code : opened.extra) {
                const d2d::rules::Drop extra{ .code = code };
                if (fight.remote_monsters || loot.put(extra, object.x, object.y, here, fight.spawning.game, now_ms)) drops.push_back(extra);
            }
            return std::pair{ opened, drops.size() };
        };
        auto to_mode = [&](int mode) {               // FUN_00624690 and its footprint (FUN_00623830); its sound is the client's (ClientSession::object_sounds)
            set_footprint(*level, object, object.collision >> mode & 1);
        };
        // Wirt's body (FUN_00583e70) drops the leg once and starts event 7.
        if (object.operate_fn == d2d::rules::operate_fn::kWirtBody) {
            if (object.preoperated || fight.remote_monsters || !operated.try_emplace({ level, npc_index }, now_ms).second) return;
            loot.put({ .code = "leg" }, object.x, object.y, here, fight.spawning.game, now_ms);
            to_mode(1);
            wirt_gold.emplace(std::pair{ level, npc_index }, WirtGold{ now_ms + 10 * kTickMs, 0, here });
            return;
        }
        // A trap object (30: "a trap", the exploding chest; FUN_00581cd0), in
        // mode 0: a physical then a fire blast on its opener (FUN_005dfa00
        // types 0, 1: blast), then mode 1; its event 1 (FUN_005417d0,
        // FrameCnt1 + 1 frames on) takes it to mode 2, the tick's OP -> ON.
        if (object.operate_fn == d2d::rules::operate_fn::kTrapObject) {
            if (operated.contains({ level, npc_index })) return;
            const auto& fighter = actor->player_combat;
            auto& life = actor->character.stats.values[d2d::d2s::kLife];
            for (int kind = 0; kind < 2; ++kind) {
                const int damage = blast(object, actor->player.x, actor->player.y, 2, life, int(actor->character.stats.get(kLevel)),
                                         int(actor->character.stats.get(kDex) + actor->psum[kDex]), fighter.defense);
                take_blast(life, kind ? d2d::rules::blast_taken(damage, fighter.mdr, fighter.res[0]) : d2d::rules::blast_taken(damage, fighter.dr_flat, fighter.dr_pct));
                actor->struck_by = { 2, object.object_id };
            }
            operated[{ level, npc_index }] = now_ms;
            to_mode(1);
            d2d::log::info("trap object {} went off", npc_index);
            return;
        }
        if (object.operate_fn == d2d::rules::operate_fn::kWell) {               // a well (FUN_005858a0): 2 x Parm2 drinks (InitFn 16), NU -> OP -> ON
            auto& well = doors.try_emplace({ level, npc_index }, Door{ 0, 0 }).first->second;
            auto& stat_values = actor->character.stats.values;
            if (well.mode >= 2) return;
            const bool drank = d2d::rules::well_drink(stat_values[kLife], stat_values[kMaxLife], stat_values[kMana], stat_values[kMaxMana],
                                                      stat_values[kStamina], stat_values[kMaxStamina]);
            if (!fight.cure(now_ms, true) && !drank) return;   // a cure counts as a drink too
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
        // tick's OP -> ON. A player opening a barrel kicks it (FUN_005868a0:
        // skill 1, Kick, its KK through FUN_00580a70 at the barrel) as it
        // breaks; the exploding barrel (FUN_00584330) has no kick. The kick's
        // hit (FUN_0056caf0 -> FUN_0057ca80) lands on players and monsters
        // only, so on the barrel it does nothing.
        if (const int operate_id = object.operate_fn; operate_id == d2d::rules::operate_fn::kCasket || operate_id == d2d::rules::operate_fn::kUrn || operate_id == d2d::rules::operate_fn::kBarrel || operate_id == d2d::rules::operate_fn::kCorpse) {
            if (operate_id == d2d::rules::operate_fn::kBarrel && actor->pmode < 0) {
                actor->swing_skill = 0;
                actor->attack_mon = -1;
                actor->seq = {};
                actor->smove = {};
                actor->kicks_left = 0;
                actor->player.dir = direction16(object.x - actor->player.x, object.y - actor->player.y);
                fight.set_pmode(kModeKK, now_ms);
            }
            const auto [opened, drops] = open();
            if (!opened.opened) return;
            operated[{ level, npc_index }] = now_ms;
            to_mode(1);
            if (operate_id == d2d::rules::operate_fn::kCorpse) cues.cue("object_corpse_loot", now_ms, object.x, object.y);
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
            const auto key = std::ranges::find_if(actor->character.items, [](const Item& item) { return item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory && item.code == "key"; });
            if (key == actor->character.items.end()) { d2d::log::info("I need a key."); return; }
            if (key->quantity > 1) --key->quantity;
            else actor->character.items.erase(key);
            cues.cue_on(unit_key(kSoundOnPlayer, actor->id), "item_key_used", now_ms, actor->player.x, actor->player.y);
        }
        // The Moldy Tome (OperateFn 6, FUN_00594e70): opens once (mode 0 → 1),
        // read every time while the quest's on: message 127 to the player
        // (FUN_005456a0: S->C 0x27, mode 2, the tome's unit id, +10 the
        // message), which the client plays in the speech box (FUN_004a1600
        // → FUN_004a1320) and answers as heard (cmd::QuestMessage).
        if (object.operate_fn == d2d::rules::operate_fn::kMoldyTome) {
            operated.try_emplace({ level, npc_index }, now_ms);
            if (!closed_at_join[std::size_t(d2d::rules::TowerQuest::kQuest)]) actor->events.push_back(ev::Speech{ npc_index, d2d::rules::TowerQuest::kTome });
            return;
        }
        // A teleport pad (OperateFn 27, FUN_00581bf0): another pad of its row
        // in its own room, else in a room near it (FUN_00619790's list),
        // and the player to a free spot by it (FUN_00581b50: FUN_0064e7b0,
        // size 3), a joined game's by the host (0x15). Used every time.
        if (object.operate_fn == d2d::rules::operate_fn::kTeleportPad) {
            if (fight.remote_monsters) return;
            auto pad_in = [&](int room) {
                for (std::size_t index = 0; index < level->npcs.size(); ++index) {
                    const auto& other = level->npcs[index];
                    if (int(index) != npc_index && other.root == "objects" && other.object_id == object.object_id && other.room == room) return int(index);
                }
                return -1;
            };
            int target = pad_in(object.room);
            for (const auto& [room_level, room] : near_rooms(*game_data, *level, object.x, object.y))
                if (target < 0 && room_level == level && room != object.room) target = pad_in(room);
            if (target < 0) return;
            const auto [spot_x, spot_y] = level->nearest_free(level->npcs[std::size_t(target)].x, level->npcs[std::size_t(target)].y);
            actor->player.x = spot_x; actor->player.y = spot_y;
            actor->player.path.clear(); actor->player.walking = false;
            actor->player.goal_x = spot_x; actor->player.goal_y = spot_y;
            actor->interact_npc = -1;
            bring_party(level, level, spot_x, spot_y, true, false);   // one screen: the party with them (the leash would undo a lone jump)
            d2d::log::info("teleport pad {} -> {}", npc_index, target);
            return;
        }
        // The Horadric Staff's chests (OperateFns 39 / 40 / 41): once (mode 0
        // only, FUN_00545850: mode 1, or 2 without a Mode1), the part, then
        // the act's chest class forced magic and rand(5) + 5 gold piles on the
        // quest seed; the shaft identified (item flag 0x10).
        if (const int operate_id = object.operate_fn; operate_id == d2d::rules::operate_fn::kCubeChest || operate_id == d2d::rules::operate_fn::kScrollChest || operate_id == d2d::rules::operate_fn::kShaftChest) {
            if (operated.contains({ level, npc_index })) return;
            operated[{ level, npc_index }] = now_ms;
            to_mode(object.op_frames ? 1 : 2);
            if (hosted_quests) return;
            each_player([&](Player&) {                         // a part for each player who wants one (LAB_00599700 ..)
                if (const auto part = staff.chest(operate_id, quests(), staff_items()); !part.empty()) {
                    loot.put({ .code = part }, object.x, object.y, here, fight.spawning.game, now_ms);
                    if (part == "msf") loot.ground.back().item.identified = true;
                }
            });
            const auto [low, high] = d2d::rules::kChestLevels[1];
            std::vector<d2d::rules::Drop> drops;
            d2d::rules::chest_round(game_data->rules, d2d::rules::chest_tc(1, diff, here, alvl(low), alvl(high)), object.seed, drops, 4, chest_place(object, here, now_ms));
            const auto rolled = drops.size();
            for (std::uint32_t gold = fight.spawning.quests.next() % 5 + 5; gold > 0; --gold) drops.push_back({ .code = "gld" });
            for (auto index = rolled; index < drops.size(); ++index) loot.put(drops[index], object.x, object.y, here, fight.spawning.game, now_ms);
            d2d::log::info("The Horadric Staff: chest {} opened ({} drops)", operate_id, drops.size());
            return;
        }
        // The Tainted Sun's altar (OperateFn 24, FUN_0059a7e0): mode 1, the
        // sun back, then at it a Viper Amulet each (identified, item flag
        // 0x10), the act's chest class with quality forced magic
        // (FUN_00585b90, EDX 4) and rand(5) + 5 gold piles on the quest seed
        // (FUN_00585970); a joined game's are the host's.
        if (object.operate_fn == d2d::rules::operate_fn::kSunAltar) {
            if (hosted_quests) return;
            const auto use = tainted.use_altar(quests(), game_bits, carries("vip"), carries("hst"));
            if (use.refused) { class_cue("impossible_1", now_ms); return; }   // voice 0x13
            if (!use.used) return;
            operated.try_emplace({ level, npc_index }, now_ms);
            to_mode(1);
            // The others: in the altar's room (FUN_0059a680) or the party
            // (0x59a040; local co-op is one party) in Act 2, their share and
            // voice; an amulet each who wants one (LAB_0059a770).
            const int operator_id = actor->id;
            int amulets = use.amulets;
            bool took_part = use.took_part;
            if (use.took_part) class_cue("act2_complete_tainted", d2d::rules::quest_line_due(0x34, now_ms));
            each_player([&](Player& player) {
                if (player.id == operator_id) return;
                if (tainted.open && (near_room(object.x, object.y, player.player.x, player.player.y) || act() == 1) && d2d::rules::TaintedSunQuest::credit(quests())) {
                    class_cue("act2_complete_tainted", d2d::rules::quest_line_due(0x34, now_ms));
                    took_part = true;
                }
                amulets += d2d::rules::TaintedSunQuest::wants_amulet(quests(), carries("vip"), carries("hst")) ? 1 : 0;
            });
            if (took_part) music_cue("music_quest_tainted");
            for (int amulet = 0; amulet < amulets; ++amulet) {
                loot.put({ .code = "vip" }, object.x, object.y, here, fight.spawning.game, now_ms);
                loot.ground.back().item.identified = true;
            }
            const auto [low, high] = d2d::rules::kChestLevels[1];
            std::vector<d2d::rules::Drop> drops;
            d2d::rules::chest_round(game_data->rules, d2d::rules::chest_tc(1, diff, here, alvl(low), alvl(high)), object.seed, drops, 4, chest_place(object, here, now_ms));
            const auto rolled = drops.size();
            for (std::uint32_t gold = fight.spawning.quests.next() % 5 + 5; gold > 0; --gold) drops.push_back({ .code = "gld" });
            for (auto index = rolled; index < drops.size(); ++index) loot.put(drops[index], object.x, object.y, here, fight.spawning.game, now_ms);
            d2d::log::info("The Tainted Sun: the altar, the sun's back ({} drops)", drops.size() + std::size_t(amulets));
            return;
        }
        // Lam Esen's tome on its stand (OperateFn 28, FUN_005b7a60): once, it
        // drops there and the stand goes to mode 2; one done gets nothing.
        if (object.operate_fn == d2d::rules::operate_fn::kLamTome) {
            if (hosted_quests || !lam_esen.take(quests())) return;
            loot.put({ .code = d2d::rules::LamEsenQuest::kTome }, object.x, object.y, here, fight.spawning.game, now_ms);
            doors[{ level, npc_index }] = Door{ 2, now_ms };
            to_mode(2);
            d2d::log::info("Lam Esen's Tome: taken from its stand");
            return;
        }
        // Khalim's chests (OperateFns 57 / 58 / 59): rand(5) + 5 gold piles on
        // the quest seed (FUN_00585970), one part for each player carrying
        // neither it nor Khalim's Will (LAB_005b8810 counts them into a3q2's
        // data +0xc; no quest bit read), a chest's drops (FUN_00585b90).
        if (const int operate_id = object.operate_fn; operate_id == d2d::rules::operate_fn::kKhalimHeart || operate_id == d2d::rules::operate_fn::kKhalimEye
                                                       || operate_id == d2d::rules::operate_fn::kKhalimBrain) {
            if (operated.contains({ level, npc_index })) return;
            operated[{ level, npc_index }] = now_ms;
            to_mode(object.op_frames ? 1 : 2);
            if (hosted_quests) return;
            using K = d2d::rules::KhalimQuest;
            std::vector<d2d::rules::Drop> drops;
            for (std::uint32_t gold = fight.spawning.quests.next() % 5 + 5; gold > 0; --gold) drops.push_back({ .code = "gld" });
            const char* part = operate_id == d2d::rules::operate_fn::kKhalimHeart ? K::kHeartCode : operate_id == d2d::rules::operate_fn::kKhalimEye ? K::kEyeCode : K::kBrainCode;
            each_player([&](Player&) { if (!carries(part) && !carries(K::kWillCode)) drops.push_back({ .code = part }); });
            const auto [low, high] = d2d::rules::kChestLevels[std::size_t(std::clamp(act(), 0, 2))];
            for (const auto& dropped : drops) loot.put(dropped, object.x, object.y, here, fight.spawning.game, now_ms);
            d2d::rules::chest_round(game_data->rules, d2d::rules::chest_tc(act(), diff, here, alvl(low), alvl(high)), object.seed, drops, 4, chest_place(object, here, now_ms));
            d2d::log::info("Khalim's Will: chest {} opened ({} drops)", operate_id, drops.size());
            return;
        }
        // The sewers' lever (OperateFn 45, FUN_005b8530): the stairs (object 366)
        // to mode 1, then 2: the way down to the sewers' second level opens.
        if (object.operate_fn == d2d::rules::operate_fn::kSewerLever) {
            if (hosted_quests || !khalim.pull_lever()) return;
            to_mode(1);
            cues.cue("object_sewer_water", now_ms, object.x, object.y);
            doors[{ level, npc_index }] = Door{ 1, now_ms };
            for (std::size_t i = 0; i < level->npcs.size(); ++i)
                if (level->npcs[i].operate_fn == d2d::rules::operate_fn::kSewerStairs) doors[{ level, int(i) }] = Door{ 2, now_ms };
            d2d::log::info("Khalim's Will: the sewers' stairs open");
            return;
        }
        // The stairs that go through a warp whose LvlWarp click box is empty (rows 60, 61, 64: no click takes those):
        // the sewers' (OperateFn 44, FUN_005b84e0 -> FUN_0059d9d0), a temple's doors (47, FUN_00581f60) and the
        // Durance's (50, FUN_00582180, which jumps into 47's mode 2). 47 shut (mode 0) opens: mode 1, its event 1 at
        // FrameCnt1 + 1 frames takes it to 2 (slime_doors_on). In mode 2 each sends its user through (FUN_005550b0) the
        // warp tile of its room; 47 / 50 without one there, of the first of its near rooms (FUN_00619790) with one.
        // ponytail: the first warp of a room in level->warps, where game.exe takes its room's units newest first.
        if (object.operate_fn == d2d::rules::operate_fn::kSewerStairs || object.operate_fn == d2d::rules::operate_fn::kStairs
            || object.operate_fn == d2d::rules::operate_fn::kDuranceStairs) {
            const auto state = doors.find({ level, npc_index });
            const int mode = state != doors.end() ? state->second.mode : mode_index(object.mode);
            if (object.operate_fn == d2d::rules::operate_fn::kStairs && mode == 0) { set_door_mode(npc_index, 1, now_ms); return; }
            if (mode != 2 || host_warps) return;
            const auto warp_in = [&](int room) {
                return std::ranges::find_if(level->warps, [&](const Level::Warp& warp) { return room >= 0 && room_of(*level, warp.unit_x, warp.unit_y) == room; });
            };
            const int room = room_of(*level, object.x, object.y);
            auto warp = warp_in(room);
            if (warp == level->warps.end() && object.operate_fn != d2d::rules::operate_fn::kSewerStairs)
                for (const auto& [near_level, near_room] : near_rooms(*game_data, *level, object.x, object.y))
                    if (near_level == level && near_room != room && (warp = warp_in(near_room)) != level->warps.end()) break;
            if (warp != level->warps.end()) travel_warp(*warp);
            return;
        }
        // The compelling orb (OperateFn 53, FUN_005bb980): hit with Khalim's
        // Will in hand; the second hit breaks it: the Will goes, the orb
        // (0x16e) leaves, the object to mode 1. Else "I can't".
        // The break's FUN_005dfee0 (ai-control.md): the orb monster's orbmist (row 0x170) from it to
        // object 386 (the marker, class 0x182, its DS1 preset), level 1, flags 0x420; its end (hit 35,
        // FUN_005abee0) puts 386 in mode 1 and queues its event 1 FrameCnt1 frames on (FUN_00581490: mode 2):
        // durance_stairs_due, as the mist lands (slime_doors_on).
        if (object.operate_fn == d2d::rules::operate_fn::kCompellingOrb) {
            if (hosted_quests || temple.orb_broken) return;
            // FUN_0063bef0: the weapon in hand (either arm, item type 0x2d;
            // the set in use: a switch moves its hands there).
            const bool wields_will = std::ranges::any_of(actor->character.items, [](const d2d::d2s::Item& item) {
                return item.location == d2d::d2s::item_location::kEquipped && item.code == d2d::rules::KhalimQuest::kWillCode
                    && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm);
            });
            if (!wields_will) { d2d::log::info("I can't use this yet."); return; }
            if (!temple.hit_orb(quests())) { d2d::log::info("The Blackened Temple: the orb's hit"); return; }
            take_carried(d2d::rules::KhalimQuest::kWillCode);
            // The party (FUN_005bb850): one done loses Khalim's pieces; one in act 3
            // gets bits 0 and 13, the pieces gone, and with word 0x15 bit 4 its bit 0.
            const int breaker = actor->id;
            each_player([&](Player& player) {
                namespace quest = d2d::rules::quest;
                if (player.id == breaker) return;
                auto& bits = quests();
                const auto take_pieces = [&] { for (const char* code : { "qey", "qhr", "qbr", "qf1", "qf2" }) while (carries(code)) take_carried(code); };
                if (quest::bit(bits, 0x12, 0)) { take_pieces(); return; }
                if (act() != 2) return;
                quest::set(bits, 0x12, 0); quest::set(bits, 0x12, 13);
                take_pieces();
                if (quest::bit(bits, 0x15, 4) && !quest::bit(bits, 0x15, 0)) quest::set(bits, 0x15, 0);
            });
            // The orb monster's death (FUN_005ddfc0, mode 0) runs its questcomplete
            // mod's client hook (0x4ad2f0, class 0x16e): the jingle, with object 386
            // in its near list (FUN_005dfee0 asserts it is), for each client seeing it.
            durance_stairs_due = { level, now_ms };
            for (auto& monster : fight.monsters)
                if (std::size_t(monster.type) < game_data->mon_bin.size() && monster.type == int(game_data->mon_bin[0x16e]) && !monster.gone) {
                    monster.gone = true;
                    music_cue_near("music_quest_compelling", monster.unit.x, monster.unit.y);
                    durance_stairs_due.at_ms = orb_mist(monster, now_ms);
                }
            to_mode(1);
            doors[{ level, npc_index }] = Door{ 1, now_ms };
            slime_doors_on(now_ms);
            fight.spawning.orb_smashed = true;
            d2d::log::info("Khalim's Will: the compelling orb broken");
            return;
        }
        // The Gidbinn's altar (OperateFn 31, FUN_005b9b40): mode 1, and the
        // first use starts a3q3's timer (7 ticks: the guardians).
        if (object.operate_fn == d2d::rules::operate_fn::kGidbinnAltar) {
            if (hosted_quests || operated.contains({ level, npc_index })) return;
            operated[{ level, npc_index }] = now_ms;
            if (!blade.use_altar(quests())) return;
            to_mode(1);
            gidbinn_in = d2d::rules::quest_timer_steps(d2d::rules::BladeQuest::kTimerTicks);
            gidbinn_altar = npc_index;
            gidbinn_level = level;
            d2d::log::info("The Blade of the Old Religion: the altar used");
            return;
        }
        // The hellgate (OperateFn 46, FUN_00584750): with word 0x16 bit 13 or 0
        // (Mephisto dead), FUN_00546ac0 from the Durance's third level: unless
        // word 0x17 has bit 0, Khalim's Will closed if open (word 0x12 bits 0, 13;
        // his eye, heart, brain and the flails taken) and word 0x17 bits 0, 13.
        // Then the fortress's waypoint and its start (FUN_0053aec0(0x67)).
        // FUN_0053d670(6) only sends the quest words to the player's client
        // (FUN_005531c0; Ghidra shows source lines 0xca7 / 0xb56 as its
        // argument), which d2d's view reads directly.
        if (act4_operate(npc_index, now_ms)) return;
        if (act5_operate(npc_index, now_ms)) return;
        if (object.operate_fn == d2d::rules::operate_fn::kHellgate) {
            auto& bits = quests();
            if (!d2d::rules::quest::bit(bits, 0x16, 13) && !d2d::rules::quest::bit(bits, 0x16, 0)) return;
            const Level* fortress = game_data->level(d2d::rules::level_ids::kPandemoniumFortress);
            if (!fortress || fortress->start.first < 0) { d2d::log::warn("The Pandemonium Fortress didn't build; the hellgate stays shut"); return; }
            if (level->id == d2d::rules::level_ids::kDuranceOfHateLevel3 && !d2d::rules::quest::bit(bits, 0x17, 0)) {
                if (!d2d::rules::quest::bit(bits, 0x12, 0)) {
                    d2d::rules::quest::set(bits, 0x12, 0); d2d::rules::quest::set(bits, 0x12, 13);
                    for (const char* code : { "qey", "qhr", "qbr", "qf1", "qf2" }) take_carried(code);
                }
                d2d::rules::quest::set(bits, 0x17, 0); d2d::rules::quest::set(bits, 0x17, 13);
                d2d::rules::NpcReturns::mark(congrats(), 2);                       // FUN_00544fa0(2): Kurast's
                movie = 4;                                                         // S->C 0x61 {4}: Act04start
            }
            set_waypoint(waypoint_index(*game_data, d2d::rules::level_ids::kPandemoniumFortress));
            arrive(fortress, fortress->start.first, fortress->start.second, "the hellgate");
            return;
        }
        // The orifice (OperateFn 25, FUN_0059dc70): in mode 0 with the Horadric Staff carried (FUN_00558110 'hst ')
        // the player's busy with it (FUN_00554120(player, 2, it)), it opens (mode 1) and S->C 0x58 kind 0 opens
        // the client's item panel (0x59dd6e); without, "I can't" (FUN_00553380). The staff goes in by C->S 0x44
        // (object_item).
        if (object.operate_fn == d2d::rules::operate_fn::kOrifice) {
            if (operated.contains({ level, npc_index }) || hosted_quests) return;
            if (!carries("hst")) { d2d::log::info("I can't use this yet."); return; }
            operated[{ level, npc_index }] = now_ms;
            to_mode(1);
            actor->events.push_back(ev::ServiceResult{ npc_index, 0 });
            return;
        }
        // Horazon's Journal (OperateFn 42, FUN_0059b970): opened once (mode
        // 1), read every time: its 396 (FUN_005456a0, as the Moldy Tome's),
        // and the quest's (ArcaneQuest::journal).
        if (object.operate_fn == d2d::rules::operate_fn::kJournal) {
            if (operated.try_emplace({ level, npc_index }, now_ms).second) to_mode(1);
            if (!hosted_quests && arcane.journal(object.room))
                each_player([&](Player&) { d2d::rules::ArcaneQuest::journal_credit(quests(), level->id == d2d::rules::ArcaneQuest::kSanctuary); });
            actor->events.push_back(ev::Speech{ npc_index, d2d::rules::ArcaneQuest::kJournal });
            return;
        }
        // The Sanctuary's portal (OperateFn 34, FUN_005846b0): shut, it opens
        // (mode 1); open, Palace Cellar 3 and the Sanctuary take the player to
        // each other's (FUN_0053aec0), by the portal there.
        if (object.operate_fn == d2d::rules::operate_fn::kSanctuaryPortal) {
            if (operated.try_emplace({ level, npc_index }, now_ms).second) { to_mode(1); return; }
            if (fight.remote_monsters) return;
            const int destination = level->id == d2d::rules::level_ids::kArcaneSanctuary ? d2d::rules::level_ids::kPalaceCellarLevel3 : d2d::rules::level_ids::kArcaneSanctuary;
            const Level* there = game_data->level(destination);
            if (!there || there->ds1.width() == 0) { d2d::log::info("not implemented: level {} (the Sanctuary's portal)", destination); return; }
            const auto other = std::ranges::find(there->npcs, object.object_id, &Npc::object_id);
            const float to_x = other == there->npcs.end() ? float(there->ds1.width()) / 2 : other->x, to_y = other == there->npcs.end() ? float(there->ds1.height()) / 2 : other->y;
            const auto [spot_x, spot_y] = there->nearest_free(to_x, to_y + 0.6f);
            arrive(there, spot_x, spot_y, "the Sanctuary's portal");
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
        auto& stat_values = actor->character.stats.values;
        d2d::rules::shrine_recharge(shrine, stat_values[kLife], stat_values[kMaxLife], stat_values[kMana], stat_values[kMaxMana]);
        // A booster's state ends the one before (curse = 1, FUN_0056e970);
        // the stamina state's end (FUN_00583a40) and its start fill stamina.
        if (auto boost = d2d::rules::shrine_boost(shrine, actor->player_combat.attack_rating); !boost.empty()) {
            if (shrine.code == 14 || shrine_code(actor->boost.shrine) == 14) stat_values[kStamina] = stat_values[kMaxStamina];
            actor->boost = { row, std::move(boost), now_ms + std::uint32_t(shrine.duration) * 40u };
        }
        if (const auto sound = d2d::rules::shrine_sound(shrine.code); !sound.empty()) cues.cue(sound, now_ms, object.x, object.y);
        // Portal (FUN_00582a30): the free spot nearest the player for a size
        // 3 (FUN_0064e7b0, mask 0x1c09), then the town portal from 5
        // subtiles on each axis of it (FUN_0056d130's own search).
        if (shrine.code == 17)
            if (const auto spot = free_spot_near(*level, crowd_now(), &actor->player, int(std::floor(actor->player.x * 5)), int(std::floor(actor->player.y * 5)), 3, 0x1c09))
                open_portal_at((float(spot->first + 5) + 0.5f) / 5, (float(spot->second + 5) + 0.5f) / 5, now_ms);
        if (shrine.code == 18)                                  // gem: one up, or a chipped gem at the player's feet
            if (const auto code = d2d::rules::gem_shrine(game_data->rules, actor->character.items, rng); !code.empty())
                loot.put({ .code = code }, actor->player.x, actor->player.y, 1, fight.spawning.game, now_ms);
        // Storm (FUN_00582da0): every live player and monster in the shrine's
        // room, when the box Arg1 subtiles about it fits inside (FUN_0065a6b0),
        // else in its near rooms (FUN_0065ac70: FUN_0065a710's room test
        // passes every room, its compares never fail; the filter LAB_00582710
        // has no distance), loses Arg0 % of its life's whole points.
        if (shrine.code == 19) {
            const int at_x = int(std::floor(object.x * 5)), at_y = int(std::floor(object.y * 5)), radius = shrine.arg1;
            const int own = room_of(*level, object.x, object.y);
            std::vector<int> rooms;
            if (const auto rect = own >= 0 ? level_room(*level, std::size_t(own)) : std::nullopt;
                rect && rect->x * 5 < at_x - radius && rect->y * 5 < at_y - radius && at_x + radius < (rect->x + rect->width) * 5 && at_y + radius < (rect->y + rect->height) * 5)
                rooms.push_back(own);
            else
                for (const auto& [room_level, room] : near_rooms(*game_data, *level, object.x, object.y))
                    if (room_level == level) rooms.push_back(room);
            const auto about = [&](float x, float y) { return std::ranges::contains(rooms, room_of(*level, x, y)); };
            const auto storm = [&](auto life) { return life - life * shrine.arg0 / 100; };
            for (const auto& player : players) {
                if (auto& life = player->character.stats.values[kLife]; life > 0 && about(player->player.x, player->player.y)) life -= (life >> 8) * shrine.arg0 / 100 << 8;
                if (player->merc && player->merc->hit_points > 0 && about(player->merc->unit.x, player->merc->unit.y)) player->merc->hit_points = storm(player->merc->hit_points);
            }
            if (fight.mon_level == level)
                for (auto& monster : fight.monsters)
                    if (monster.alive() && about(monster.unit.x, monster.unit.y)) monster.hit_points = storm(monster.hit_points);
        }
        // Exploding / Poison: Arg0 .. Arg1 - 1 exploding (opm) / choking gas
        // (gpm) potions at the player's feet (FUN_005830e0 / FUN_00583410).
        if (shrine.code == 21 || shrine.code == 22)
            for (int count = shrine.arg0 + rng(std::max(shrine.arg1 - shrine.arg0, 0)); count > 0; --count)
                loot.put({ .code = shrine.code == 21 ? "opm" : "gpm" }, actor->player.x, actor->player.y, 1, fight.spawning.game, now_ms);
        if ((shrine.code == 19 || shrine.code == 21 || shrine.code == 22) && fight.mon_level == level)
            fight.shrine_missiles(shrine.code, object.x, object.y, int(actor->character.stats.get(kLevel)), now_ms);
        if (shrine.code == 20 && fight.mon_level == level) {   // warping (FUN_00583050): the nearest plain monster turns boss
            // FUN_0065a800 over the player's near rooms, each room's units
            // newest first, the first at the least ai_distance (subtiles).
            // FUN_00582750 passes an evil monster in NU or WL, with mWL and a
            // Height, neither boss nor primeevil (MonStats +0xc & 0xc0) and
            // no champion, unique, superunique or minion (data +0x16 & 0x1f).
            std::vector<int> rooms;
            for (const auto& [room_level, room] : near_rooms(*game_data, *level, actor->player.x, actor->player.y))
                if (room_level == level) rooms.push_back(room);
            fight.restamp();                                         // the lists as they stand now (FUN_0064c2c0 re-files a unit as it changes rooms)
            std::vector<d2d::rules::RoomUnit> units;
            for (const auto& monster : fight.monsters) {
                const auto& kind = game_data->monsters.types[std::size_t(monster.type)];
                units.push_back({ room_of(*level, monster.unit.x, monster.unit.y),
                                  monster.alive() && monster.align == 0 && (monster.mode == "NU" || monster.mode == "WL") && kind.can_walk && kind.height != 0 &&
                                      !kind.boss_column && !kind.prime_evil && monster.boss == d2d::rules::Boss::none, monster.unit.room_stamp });
            }
            int best = -1, best_distance = 0xffff;
            const int player_x = int(std::floor(actor->player.x * 5)), player_y = int(std::floor(actor->player.y * 5));
            for (const int index : d2d::rules::room_order(units, rooms)) {
                const auto& monster = fight.monsters[std::size_t(index)];
                const int distance = d2d::rules::ai_distance(int(std::floor(monster.unit.x * 5)) - player_x, int(std::floor(monster.unit.y * 5)) - player_y);
                if (distance < best_distance) { best_distance = distance; best = index; }
            }
            if (best >= 0) {                               // FUN_005a4940: FUN_005a0760 (champions allowed), FUN_005a2120
                auto& monster = fight.monsters[std::size_t(best)];
                const auto boss = d2d::rules::roll_boss(game_data->umods, game_data->monsters.types[std::size_t(monster.type)], diff, true, rng);
                make_boss(*game_data, monster, boss.kind, boss.mods, -1, boss.name_seed, diff, rng, fight.game_players());
                cues.cue_on(unit_key(kSoundOnMonster, monster.id), "object_shrine_jail", now_ms, monster.unit.x, monster.unit.y);
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
        } else if (door.operate_fn == d2d::rules::operate_fn::kSlimeDoor) {           // the Maggot Lair's (FUN_005821a0): NU only, to OP (0x5821b8), once
            if (state.mode == 0) mode = 1;                                              // then ON by its event 1 (slime_doors_on)
        } else if (door.operate_fn == d2d::rules::operate_fn::kGate) {                // Harrogath's gate, the pen door (FUN_00582080)
            // 500 ms after its last change (+0xd4): shut (0) opens (1, then 2 by its event 1: slime_doors_on), open shuts.
            if (now_ms - state.when >= 500) mode = state.mode == 0 ? 1 : state.mode == 2 ? 0 : -1;
        } else if (door.operate_fn == d2d::rules::operate_fn::kTrapDoor) {            // trap door (FUN_00581eb0): opens, then down its room's warp (FUN_005550b0)
            // Mode 2: the first warp tile unit (type 5) of the door's room
            // (room +0x74's list); a room without one is game.exe's assert 0x40e.
            if (state.mode == 0) mode = 2;
            else if (state.mode == 2) {
                const int room = room_of(*level, door.x, door.y);
                const auto warp = std::ranges::find_if(level->warps, [&](const Level::Warp& candidate) { return room >= 0 && room_of(*level, candidate.unit_x, candidate.unit_y) == room; });
                if (warp != level->warps.end()) actor->take_warp = int(warp - level->warps.begin());
            }
        } else if (now_ms - state.when >= 500) {       // a door (FUN_00581d40): is anyone in the doorway?
            // Each unit by its collision pattern's own bits (rules::in_doorway); a player's is the plus.
            auto in_door = [&](int pattern, float x, float y) {
                return d2d::rules::in_doorway(pattern, int(x * 5), int(y * 5), int(door.x * 5), int(door.y * 5), door.size_x, door.size_y);
            };
            bool occupied = in_door(1, actor->player.x, actor->player.y) || (actor->merc && in_door(actor->merc->unit.shape.pattern, actor->merc->unit.x, actor->merc->unit.y));
            if (fight.mon_level == level)
                for (const auto& monster : fight.monsters) occupied = occupied || (monster.alive() && in_door(monster.unit.shape.pattern, monster.unit.x, monster.unit.y));
            // An 0x8000 object stamped in the doorway (the same rect, FUN_0064d800).
            bool blocker = false;
            for (int row = 0; row < door.size_y && !blocker; ++row)
                for (int column = 0; column < door.size_x && !blocker; ++column)
                    blocker = level->blocked((float(int(door.x * 5) - door.size_x / 2 + column) + 0.5f) / 5, (float(int(door.y * 5) - door.size_y / 2 + row) + 0.5f) / 5, 0x8000);
            bool key = false;                                // locked (FUN_0055f140: a key from the inventory, as a chest's)
            if (state.mode == 6) {
                const auto found = std::ranges::find_if(actor->character.items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory && item.code == "key"; });
                if (found == actor->character.items.end()) { d2d::log::info("I need a key."); return; }
                if (found->quantity > 1) --found->quantity;
                else actor->character.items.erase(found);
                key = true;
            }
            mode = d2d::rules::door_mode(state.mode, occupied, blocker, key);
        }
        if (mode >= 0) set_door_mode(npc_index, mode, now_ms);
    }

auto World::reduced_prices() const -> int {
        d2d::rules::StatSum sum{};
        (void)fight.player_fighter(nullptr, nullptr, nullptr, &sum);
        return int(sum[d2d::d2s::kReducedPrices]);
    }

// C->S 0x44 (FUN_005852e0): action 2, the panel cancelled: S->C 0x58 kind 1, the orifice back to mode 0, the
// player free. Action 3 with the item a staff ('hst '): kind 5, mode 1 then 2 (ON) and FUN_0059dd80: the staff
// and its parts gone, the Horadric Staff done, the way down to Duriel after its timer; another item: kind 4.
auto World::object_item(const cmd::ObjectItem& given, std::uint32_t now_ms) -> void {
        if (hosted_quests || std::size_t(given.npc) >= level->npcs.size()) return;
        const auto& object = level->npcs[std::size_t(given.npc)];
        const auto used = operated.find({ level, given.npc });
        if (object.operate_fn != d2d::rules::operate_fn::kOrifice || used == operated.end() || tombs.staff_placed || tombs.lair_open) return;
        if (!given.place) {
            operated.erase(used);
            set_footprint(*level, object, object.collision & 1);
            if (std::size_t(given.npc) < npc_states.size()) npc_states[std::size_t(given.npc)].mode = {};
            actor->events.push_back(ev::ServiceResult{ given.npc, 1 });
            return;
        }
        if (!actor->held || actor->held->id != given.item || actor->held->code != "hst") {
            actor->events.push_back(ev::ServiceResult{ given.npc, 4 });
            return;
        }
        actor->events.push_back(ev::ServiceResult{ given.npc, 5 });
        actor->held.reset();
        for (const char* code : { "hst", "vip", "msf" })
            if (const auto part = std::ranges::find(actor->character.items, std::string(code), &d2d::d2s::Item::code); part != actor->character.items.end()) actor->character.items.erase(part);
        const int on_ticks = object_op_ticks(object);
        used->second = now_ms - std::uint32_t(std::max(on_ticks, 0)) * kTickMs;   // ON now (FUN_00624690(it, 2))
        set_footprint(*level, object, object.collision >> 2 & 1);
        const auto staff_missile = game_data->missiles.find("horadricstaff");   // Missiles.txt 338
        tombs.staff_in(quests(), d2d::rules::SevenTombsQuest::staff_ticks(staff_missile != game_data->missiles.end() ? staff_missile->second.range : 440));
        // The party in act 2 not done (FUN_0059dbd0): bits 0 and 13, their staff and parts gone.
        const int placer = actor->id;
        each_player([&](Player& player) {
            auto& bits = quests();
            if (player.id == placer || act() != 1 || d2d::rules::quest::bit(bits, 10, 0)) return;
            d2d::rules::quest::set(bits, 10, 0); d2d::rules::quest::set(bits, 10, 13);
            for (const char* code : { "hst", "vip", "msf" }) while (carries(code)) take_carried(code);
        });
        // FUN_0059dd80 sends S->C 0x89 event 3 to the game; its client entry
        // (0x46b100 in the table at 0x7129d8) plays the jingle with the orifice
        // (object 152) near its player.
        music_cue_near("music_quest_horadric", object.x, object.y);
        // The same S->C 0x89 {3} to every player (FUN_00545760 at 0x59de02): the light show.
        each_player([&](Player& player) { player.events.push_back(ev::UniqueEvent{ 3 }); });
        d2d::log::info("The Horadric Staff: placed in the orifice");
    }

// Rooms laid again (relevel) stamp every door and object as its preset left
// it; the doors and the shrines / chests used since go back to their mode's
// footprint.
// Only after a relevel of this level (Level::lays moved on).
auto World::reopen_doors() -> void {
        if (std::exchange(doors_laid, std::pair{ level, level->lays }) == std::pair{ level, level->lays }) return;
        for (const auto& [key, door] : doors)
            if (key.first == level && std::size_t(key.second) < level->npcs.size())
                set_footprint(*level, level->npcs[std::size_t(key.second)], level->npcs[std::size_t(key.second)].collision >> door.mode & 1);
        // The used shrines and chests (World::operated) too: relevel stamps
        // the preset mode's footprint, and tick_level re-stamps one only when
        // its mode changes, so one already OP / ON is put back here.
        for (const auto& [key, when_ms] : operated) {
            const auto npc_index = std::size_t(key.second);
            if (key.first != level || npc_index >= level->npcs.size() || npc_index >= npc_states.size() || npc_states[npc_index].mode.empty()) continue;
            const auto& npc = level->npcs[npc_index];
            set_footprint(*level, npc, npc.collision >> mode_index(npc_states[npc_index].mode) & 1);
        }
    }

auto World::set_door_mode(int npc_index, int mode, std::uint32_t now_ms) -> void {
        const auto& door = level->npcs[std::size_t(npc_index)];
        auto& state = doors.try_emplace({ level, npc_index }, Door{ mode_index(door.mode), 0 }).first->second;
        if (state.mode == mode) return;
        state = { mode, now_ms };
        set_footprint(*level, door, door.collision >> mode & 1);   // FUN_00623830 out / FUN_00620a70 back in
        d2d::log::info("door {}: mode {}", npc_index, kObjectModes[std::size_t(mode)]);
    }

// This level's slime doors, gates and stairs OP long enough go ON (event 1, FUN_00581490).

// A slime door's event 1: with Mode2 (objects.bin +0x141, 0x582209)
// FUN_005821a0 queues it at FrameCnt1 + 1 frames on (FUN_005417d0,
// 0x58223c; a gate's FUN_00582080, a temple door's FUN_00581f60 the same, the Durance's stairs' FUN_005abee0
// FrameCnt1 on); FUN_00581490 takes a door still in mode 1 to mode 2 (ON), its
// footprint ON's (HasCollision2). The time is object_op_ticks', as the
// chests' tick: the sooner of that event and the client's OP counter's end.
// An event comes only while the game runs the door's room (one near a
// player: Fight::near_players, as the monsters'); one due while it wasn't
// comes as it's run again. A level no player is on
// runs no rooms.
auto World::slime_doors_on(std::uint32_t now_ms) -> void {
        if (durance_stairs_due.level == level && now_ms >= durance_stairs_due.at_ms) {
            for (std::size_t i = 0; i < level->npcs.size(); ++i)
                if (level->npcs[i].operate_fn == d2d::rules::operate_fn::kDuranceStairs) set_door_mode(int(i), 1, durance_stairs_due.at_ms);
            durance_stairs_due = {};
        }
        for (const auto& [key, state] : doors) {
            if (key.first != level || state.mode != 1 || std::size_t(key.second) >= level->npcs.size()) continue;
            const auto& door = level->npcs[std::size_t(key.second)];
            using namespace d2d::rules::operate_fn;
            if (!std::ranges::contains(std::array{ kSlimeDoor, kGate, kStairs, kDuranceStairs }, door.operate_fn) || !fight.near_players(door.x, door.y)) continue;
            const int on_ticks = object_op_ticks(door);
            if (on_ticks < 0) continue;
            const std::uint32_t on_ms = state.when + std::uint32_t(on_ticks) * kTickMs;
            if (now_ms >= on_ms) set_door_mode(key.second, 2, on_ms);
        }
    }

// A monster's door at a think (FUN_005b0f50): of the level's closed (mode
// 0) IsDoor objects, the nearest under 9 subtiles squared (FUN_005dd0b0
// mode 8, rules::door_pick); with MonsterOK, FUN_00584540 operates it
// (OperateFn 8) when in reach (FUN_00623660, rules::object_reach). Found,
// the monster stands 5 either way. The doors come in the search's order:
// the monster's near rooms, each room's units newest first (room_order).
auto World::monster_door(const Monster& monster, std::uint32_t now_ms) -> bool {
        if (fight.mon_level != level) return false;
        const int x = int(std::floor(monster.unit.x * 5)), y = int(std::floor(monster.unit.y * 5));
        std::vector<int> rooms;
        for (const auto& [room_level, room] : near_rooms(*game_data, *level, monster.unit.x, monster.unit.y))
            if (room_level == level) rooms.push_back(room);
        std::vector<d2d::rules::RoomUnit> units;
        units.reserve(level->npcs.size());
        for (std::size_t i = 0; i < level->npcs.size(); ++i) {
            const auto& door = level->npcs[i];
            const auto state = doors.find({ level, int(i) });
            units.push_back({ room_of(*level, door.x, door.y), door.door && (state != doors.end() ? state->second.mode : mode_index(door.mode)) == 0 });
        }
        std::vector<std::pair<int, int>> spots;
        std::vector<int> door_indices;
        for (const int index : d2d::rules::room_order(units, rooms)) {
            const auto& door = level->npcs[std::size_t(index)];
            spots.emplace_back(int(door.x * 5) - x, int(door.y * 5) - y);
            door_indices.push_back(index);
        }
        const int pick = d2d::rules::door_pick(spots);
        if (pick < 0 || !level->npcs[std::size_t(door_indices[std::size_t(pick)])].monster_ok) return false;
        const auto& door = level->npcs[std::size_t(door_indices[std::size_t(pick)])];
        const auto [dx, dy] = spots[std::size_t(pick)];
        if (d2d::rules::object_reach(-dx, -dy, game_data->monsters.types[std::size_t(monster.type)].size, door.size_x, door.size_y))
            operate_door(door_indices[std::size_t(pick)], now_ms);
        return true;
    }

// An exploding barrel (FUN_00584330 / FUN_00584240): open, it hurts
// whoever's within 3 subtiles and sets off the unopened ones nearer
// than 3.
auto World::explode(int npc_index, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        const auto& barrel = level->npcs[std::size_t(npc_index)];
        operated[{ level, npc_index }] = now_ms;
        // FUN_00584240 walks the barrel's own room's unit list (room +0x74, next
        // +0xe8; room stamps, rules::room_order), the barrel itself skipped: a
        // player not dead (mode 0x11) or a monster (the mercs and pets too) not
        // in DD (0xc) within 3 subtiles takes a physical blast (FUN_005dfa00
        // type 0) on the barrel's seed, life in 256ths; an exploding barrel
        // (class 0xb) still in mode 0 under 3 off (FUN_006416d0) goes off there
        // and then (FUN_00584330), the walk going on after it.
        // ponytail: the other barrels list last (objects keep no room stamp); game.exe's walk puts
        // an object group's barrel, made after the room's preset monsters, ahead of them.
        auto nearby = [&](float x, float y, float subtiles) { return std::hypot(x - barrel.x, y - barrel.y) * 5 <= subtiles; };
        const int room = room_of(*level, barrel.x, barrel.y);
        const auto in_room = [&](float x, float y) { return level->rooms.empty() || room_of(*level, x, y) == room; };
        enum class Kind { player, merc, pet, monster, other_barrel };
        struct Listed { Kind kind; Player* player = nullptr; Fight::Pet* pet = nullptr; std::size_t index = 0; };
        fight.restamp();                                             // the lists as they stand now (FUN_0064c2c0 re-files a unit as it changes rooms)
        std::vector<Listed> listed;
        std::vector<d2d::rules::RoomUnit> order;
        const auto add = [&](Listed entry, float x, float y, std::uint32_t stamp) {
            if (!in_room(x, y)) return;
            listed.push_back(entry);
            order.push_back({ 0, true, stamp });
        };
        each_player([&](Player& player) {
            add({ Kind::player, &player }, player.player.x, player.player.y, player.player.room_stamp);
            if (player.merc) add({ Kind::merc, &player }, player.merc->unit.x, player.merc->unit.y, player.merc->unit.room_stamp);
            for (auto& pet : player.pets)
                if (pet.where == level) add({ Kind::pet, &player, &pet }, pet.monster.unit.x, pet.monster.unit.y, pet.monster.unit.room_stamp);
        });
        if (fight.mon_level == level)
            for (std::size_t k = 0; k < fight.monsters.size(); ++k)
                add({ Kind::monster, nullptr, nullptr, k }, fight.monsters[k].unit.x, fight.monsters[k].unit.y, fight.monsters[k].unit.room_stamp);
        for (std::size_t k = 0; k < level->npcs.size(); ++k)
            if (int(k) != npc_index && level->npcs[k].object_id == d2d::rules::object_ids::kExplodingBarrel)
                add({ Kind::other_barrel, nullptr, nullptr, k }, level->npcs[k].x, level->npcs[k].y, 0);
        const std::vector<int> one_room{ 0 };
        for (const int listed_at : d2d::rules::room_order(order, one_room)) {
            const auto& entry = listed[std::size_t(listed_at)];
            switch (entry.kind) {
            case Kind::player: {
                auto& player = *entry.player;
                const auto& fighter = player.player_combat;
                if (!nearby(player.player.x, player.player.y, 3) || player.character.stats.values[kLife] <= 0) break;
                take_blast(player.character.stats.values[kLife], d2d::rules::blast_taken(blast(barrel, player.player.x, player.player.y, 2, player.character.stats.values[kLife], int(player.character.stats.get(kLevel)),
                                                                                        int(player.character.stats.get(kDex) + player.psum[kDex]), fighter.defense),
                                                                                  fighter.dr_flat, fighter.dr_pct));
                player.struck_by = { 2, barrel.object_id };
                break;
            }
            case Kind::merc: {
                auto& player = *entry.player;
                auto& merc = *player.merc;
                if (!nearby(merc.unit.x, merc.unit.y, 3) || merc.mode == "DD" || merc.hit_points <= 0) break;
                Player* const was = actor;
                act_as(player);
                const auto merc_fighter = fight.merc_fighter();
                const int dexterity = player.merc_st.dex + int(fight.merc_gear().sum[std::size_t(kDex)]);
                const int taken = d2d::rules::blast_taken(blast(barrel, merc.unit.x, merc.unit.y, 2, merc.life_fine(), player.merc_st.level, dexterity, merc_fighter.defense),
                                                          merc_fighter.dr_flat, merc_fighter.dr_pct);
                std::int64_t life = merc.life_fine();
                take_blast(life, taken);                                 // under one point is dead, as the player's
                merc.set_life_fine(int(life));
                act_as(*was);
                break;
            }
            case Kind::pet: {
                auto& pet = *entry.pet;
                if (!pet.monster.alive() || pet.monster.mode == "DD" || !nearby(pet.monster.unit.x, pet.monster.unit.y, 3)) break;
                Player* const was = actor;
                act_as(*entry.player);
                const auto target = pet.monster.target(*game_data);
                const int damage = d2d::rules::blast_taken(blast(barrel, pet.monster.unit.x, pet.monster.unit.y, game_data->monsters.types[std::size_t(pet.monster.type)].size,
                                                                 pet.monster.life_fine(), pet.monster.stats.level, 0, pet.monster.stats.armor_class), 0, target.res[0]);
                fight.pet_hurt(pet, damage >> 8, now_ms, 0, false, damage & 0xff);
                act_as(*was);
                break;
            }
            case Kind::monster: {
                auto& monster = fight.monsters[entry.index];
                if (!monster.alive() || monster.mode == "DD" || !nearby(monster.unit.x, monster.unit.y, 3)) break;
                const auto target = monster.target(*game_data);
                const int damage = d2d::rules::blast_taken(blast(barrel, monster.unit.x, monster.unit.y, game_data->monsters.types[std::size_t(monster.type)].size,
                                                                 monster.life_fine(), monster.stats.level, 0, monster.stats.armor_class), 0, target.res[0]);
                if (hurt(*game_data, monster, damage >> 8, now_ms, damage & 0xff)) fight.killed(entry.index, now_ms);
                break;
            }
            case Kind::other_barrel: {
                const auto& other = level->npcs[entry.index];
                const int apart = d2d::rules::unit_range(int(std::floor(other.x * 5)) - int(std::floor(barrel.x * 5)), int(std::floor(other.y * 5)) - int(std::floor(barrel.y * 5)),
                                                         barrel.size_x, other.size_x);
                if (!other.preoperated && !operated.contains({ level, int(entry.index) }) && apart < 3) explode(int(entry.index), now_ms);
                break;
            }
            }
        }
        set_footprint(*level, barrel, barrel.collision >> 1 & 1);
        d2d::log::info("barrel {} exploded", npc_index);
    }

auto World::arm_trap(int trap, float x, float y, int alvl, std::uint32_t now_ms) -> void {
        if (trap == 8 && d2d::rules::trap_undead(level->region[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))], 0) < 0) return;   // FUN_00582250
        traps.push_back({ level, trap, x, y, alvl, now_ms + 35 * kTickMs, false });
    }

auto World::spring_traps(std::uint32_t now_ms) -> void {
        std::vector<Trap> due_traps;
        for (auto armed = traps.begin(); armed != traps.end();) {
            if (now_ms < armed->due) { ++armed; continue; }
            due_traps.push_back(*armed);
            armed = traps.erase(armed);
        }
        for (const Trap& due : due_traps) {
            if (due.level != level) continue;
            if (due.think) trap_think(due.trap, due.x, due.y, due.alvl, due.due);
            else spring_trap(d2d::rules::trap_on_level(due.trap, level->id), due.x, due.y, due.alvl, due.due);
        }
    }

// A chest's trap (the table at 0x732cec, docs/research/re/objects.md
// "Trap monsters"). 5 / 7 leave two fires (no damage traced); 8
// raises the level's undead. `undead`: 8's count (0: 1 or 2); a
// casket's or barrel's undead is one.

// The trap monster acts once and is gone, so its shot goes straight
// from the chest at the target its monster search picks: missile
// level 1 / 4 / 8 by difficulty; a row with a Skill (chainlightning)
// carries that skill's damage, the others their own columns.
// trapfirebolt bursts into its fireexplode as any missile does (its
// ExplosionMissile, missiles_update). Its first think is two frames
// after FUN_00582420 makes it; only a searched target within aip1
// (Trap-Missile 25 / 30 / 32 by difficulty; Trap-Poison / Trap-Nova
// 20) gets the act. Trap 1's chainlightning doesn't hop: its hops
// (missile data +0x28) are set only through FUN_0064a710, which the
// mode's missile path (FUN_005a6d50, FUN_0056ecb0, FUN_0059fa30)
// never calls, so hit function 12 (FUN_005aa730: hops over 1) finds
// none.
auto World::spring_trap(int trap, float x, float y, int alvl, std::uint32_t now_ms, int undead) -> void {
        cues.cue("object_trap_trigger", now_ms, x, y);
        const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        if (trap == 5 || trap == 7) {                  // FUN_00582380: large at the chest, small a subtile east
            fires.push_back({ level, &game_data->trap_fires[0], x, y });
            fires.push_back({ level, &game_data->trap_fires[1], x + 0.2f, y });
            d2d::log::info("trap {}: fire", trap);
            return;
        }
        // Trap 8 (FUN_005822f0): (a step of the object seed, game +0x10f0,
        // & 1) + 1 of the level's undead family; each (FUN_00582280) by
        // FUN_005b2490: the level's own of the family (level_monster), mode
        // 8 (S1), round the chest in its room (FUN_005b23c0 -> FUN_005b2a00,
        // spawn_spot) at radius -1, else 1, else 3; none: not made. Its unit
        // seed a step of the game seed (FUN_00552df0), its look, variant and
        // props drawn on it (spawn_monsters).
        if (trap == 8) {
            const int fam = d2d::rules::trap_undead(level->region[std::size_t(diff)], 0);
            if (fam < 0 || fight.mon_level != level) { d2d::log::info("trap 8: no undead here"); return; }
            const int count = undead ? undead : int(fight.spawning.objects.next() & 1) + 1;
            const int type = level_monster(*game_data, *level, fam);
            Crowd crowd = crowd_now();
            const int chest_x = int(std::floor(x * 5)), chest_y = int(std::floor(y * 5)), room = room_of(*level, x, y);
            const auto& region = std::size_t(level->id) < fight.spawning.regions.size() ? fight.spawning.regions[std::size_t(level->id)] : d2d::rules::Region{};
            int made = 0;
            std::vector<Monster> risen;
            risen.reserve(std::size_t(count));
            for (int k = 0; k < count; ++k) {
                int at_x = 0, at_y = 0;
                auto& in_play = fight.spawning.levels[level];
                if (!std::ranges::any_of(std::array{ -1, 1, 3 }, [&](int radius) { return spawn_spot(*game_data, *level, crowd, &in_play, type, chest_x, chest_y, radius, room, at_x, at_y); })) continue;
                const d2d::rules::Spawn spawn{ .type = type, .x = at_x, .y = at_y, .seed = fight.spawning.game.next() };
                auto monster = std::move(spawn_monsters(*game_data, std::span{ &spawn, 1 }, region, rng, diff, fight.game_players(), level->id).front());
                set_mode(*game_data, monster, "S1", now_ms);
                monster.aware = true;
                crowd.units.push_back(&risen.emplace_back(std::move(monster)).unit);
                ++made;
            }
            for (auto& monster : risen) fight.add_monster(std::move(monster));
            d2d::log::info("trap 8: {} x{}", game_data->monsters.types[std::size_t(type)].id, made);
            return;
        }
        // FUN_00582420 makes the trap monster now. FUN_00573780 files its
        // first think two frames on; its Trap-* AI acts once and then dies.
        if (trap > 0 && trap < 7) {
            traps.push_back({ level, trap, x, y, alvl, now_ms + 2 * kTickMs, true });
            return;
        }
    }

// Trap-Missile (FUN_005fb5b0) uses A1's missile; Trap-Poison and Trap-Nova
// (FUN_005fb900 / FUN_005fb9b0) use Skill1. Their driver first runs
// FUN_005dd7f0; the AI acts only on its picked target within aip1, then its
// aip2 of one makes the following think kill it.
auto World::trap_think(int trap, float x, float y, int alvl, std::uint32_t now_ms) -> void {
        struct TargetPoint { float x = 0, y = 0; };
        const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        std::vector<d2d::rules::SearchFoe> searched;
        std::vector<TargetPoint> targets;
        const int trap_x = int(std::floor(x * 5)), trap_y = int(std::floor(y * 5));
        const auto add_target = [&](float target_x, float target_y, bool pet, bool dead) {
            searched.push_back({ d2d::rules::ai_distance(int(std::floor(target_x * 5)) - trap_x, int(std::floor(target_y * 5)) - trap_y), pet, false, dead });
            targets.push_back({ target_x, target_y });
        };
        each_player([&](Player& player) {
            add_target(player.player.x, player.player.y, false, player.ghost || player.character.stats.values[d2d::d2s::kLife] <= 0);
            if (player.merc) add_target(player.merc->unit.x, player.merc->unit.y, true, !player.merc->alive());
            for (const auto& pet : player.pets)
                if (pet.where == level) add_target(pet.monster.unit.x, pet.monster.unit.y, true, !pet.monster.alive());
        });
        static constexpr std::array<int, 3> kMissileReach{ 25, 30, 32 };
        const int reach = trap == 1 || trap == 2 || trap == 6 ? kMissileReach[std::size_t(diff)] : 20;
        const int target_index = d2d::rules::trap_target(searched, reach);
        if (target_index < 0) {
            d2d::log::info("trap {}: no target within {} subtiles", trap, reach);
            return;
        }
        const TargetPoint target = targets[std::size_t(target_index)];
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
        stats.to_hit = alvl * 10;
        stats.no_hit_roll = !missile_info.to_hit;                       // ToHit 0: no roll (FUN_005adf10)
        stats.a2_min = missile_info.min; stats.a2_max = std::max(missile_info.max, missile_info.min);
        put_missile_element(stats, damage);            // poison per frame for ELen frames (the record's), others in whole points
        auto shoot = [&](float dx, float dy, float vel, const std::shared_ptr<std::vector<int>>& struck) {
            const float speed = missile_speed(int(vel * 256) * 75 / 100), distance = std::max(std::hypot(dx, dy), 0.01f);   // FUN_0059fa30's x 75 / 100
            Missile missile{ &missile_info, x, y, dx / distance * speed, dy / distance * speed, missile_heading(dx, dy), now_ms, now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, stats };
            missile.struck = struck;
            // FUN_0059fa30 flag 2 (FUN_005ccd10, FUN_0056d400): a ring's offset in subtiles from
            // the trap's; the missile at the player puts its own point below.
            missile.aim_at = offset_target(x, y, int(std::lround(dx)), int(std::lround(dy)));
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
                const auto [aim_x, aim_y] = d2d::rules::kNovaRing[std::size_t(k)];
                shoot(float(aim_x), float(aim_y), float(missile_info.vel), ring);
            }
        } else {
            shoot(target.x - x, target.y - y, float(missile_info.vel), std::make_shared<std::vector<int>>());
            fight.missiles.back().aim_at = { target.x, target.y };
        }
        d2d::log::info("trap {}: {} at level {}", trap, name, lvl);
    }

// FUN_005dfee0: the marker (FUN_0065a620: object class 0x182 in the orb's room; the level's
// one, its preset unit), then a missile 0x170 (orbmist) at its spot from the orb, flags 0x420,
// level 1 (FUN_0059fa30). It ends as it reaches the marker (hit 35 there); drawn only.
auto World::orb_mist(const Monster& orb, std::uint32_t now_ms) -> std::uint32_t {
        constexpr int kMarker = 0x182;
        const auto marker = std::ranges::find_if(level->units, [](const d2d::drlg::Unit& unit) { return unit.type == d2d::rules::unit_type::kObject && unit.id == kMarker; });
        const auto mist = game_data->missiles.find("orbmist");
        if (marker == level->units.end() || mist == game_data->missiles.end()) return now_ms;
        const float to_x = (float(marker->x) + 0.5f) / 5, to_y = (float(marker->y) + 0.5f) / 5;
        const float dx = to_x - orb.unit.x, dy = to_y - orb.unit.y;
        const std::size_t had = fight.missiles.size();
        andariel_missile(*game_data, orb, "orbmist", dx, dy, now_ms, fight.missiles, 1);
        if (fight.missiles.size() == had) return now_ms;
        auto& sent = fight.missiles.back();
        sent.visual_only = true;
        const float speed = missile_speed(mist->second, 1);
        if (speed > 0) sent.dies = std::min(sent.dies, now_ms + std::uint32_t(std::hypot(dx, dy) / speed * 1000.f));
        d2d::log::info("Khalim's Will: the orb's mist to the stairs");
        return sent.dies;
    }

}  // namespace d2d::game
