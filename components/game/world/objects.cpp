// SPDX-License-Identifier: GPL-3.0-or-later
// The World (world.hpp): objects: operating them, doors, explosions and traps. The rest is in
// this directory.
#include "world.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "log.hpp"

#include <combat.hpp>
#include <d2s_items.hpp>
#include <drops.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <format>
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

// FUN_00622b50 (mask 0x804): rules::sight_blocked from the unit to the
// object over the walk grid's missile barrier (0x04) and doors (0x800).
auto World::blast(const Npc& object, float x, float y, int size, std::int64_t life, int unit_level, int dexterity, int defense) -> int {
    if (in_town(level)) return 0;
    const auto wall = [&](int at_x, int at_y) { return level->blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x804); };
    const auto subtile = [](float cell) { return int(std::floor(cell * 5)); };
    if (d2d::rules::sight_blocked(subtile(x), subtile(y), size, subtile(object.x), subtile(object.y), object.size_x, wall)) return 0;
    return d2d::rules::object_blast(life, unit_level, dexterity, defense, object.seed);
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
                                                           [&](int forced) { return d2d::rules::chest_round(game_data->rules, treasure_class, object.seed, drops, forced); });
            for (const auto& code : opened.extra) drops.push_back({ .code = code });
            if (!fight.remote_monsters)              // a joined game: the host drops the chest's items (S->C 0x9c)
                for (const auto& dropped : drops) loot.put(dropped, object.x, object.y, here, fight.spawning.game, now_ms);
            return std::pair{ opened, drops.size() };
        };
        auto to_mode = [&](int mode) {               // FUN_00624690 and its footprint (FUN_00623830); its sound is the client's (Town::object_sounds)
            set_footprint(*level, object, object.collision >> mode & 1);
        };
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
            d2d::rules::chest_round(game_data->rules, d2d::rules::chest_tc(1, diff, here, alvl(low), alvl(high)), object.seed, drops, 4);
            for (std::uint32_t gold = fight.spawning.quests.next() % 5 + 5; gold > 0; --gold) drops.push_back({ .code = "gld" });
            for (const auto& dropped : drops) loot.put(dropped, object.x, object.y, here, fight.spawning.game, now_ms);
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
            if (use.refused) { d2d::log::info("I can't use this yet."); return; }   // voice 0x13
            if (!use.used) return;
            operated.try_emplace({ level, npc_index }, now_ms);
            to_mode(1);
            // The others: in the altar's room (FUN_0059a680) or the party
            // (0x59a040; local co-op is one party) in Act 2, their share and
            // voice; an amulet each who wants one (LAB_0059a770).
            const int operator_id = actor->id;
            int amulets = use.amulets;
            bool took_part = use.took_part;
            if (use.took_part) class_cue("act2_complete_tainted", now_ms);
            each_player([&](Player& player) {
                if (player.id == operator_id) return;
                if (tainted.open && (near_room(object.x, object.y, player.player.x, player.player.y) || act() == 1) && d2d::rules::TaintedSunQuest::credit(quests())) {
                    class_cue("act2_complete_tainted", now_ms);
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
            d2d::rules::chest_round(game_data->rules, d2d::rules::chest_tc(1, diff, here, alvl(low), alvl(high)), object.seed, drops, 4);
            for (std::uint32_t gold = fight.spawning.quests.next() % 5 + 5; gold > 0; --gold) drops.push_back({ .code = "gld" });
            for (const auto& dropped : drops) loot.put(dropped, object.x, object.y, here, fight.spawning.game, now_ms);
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
        // the quest seed (FUN_00585970), the part for one who needs it (its
        // Will not made, Khalim's Will not done), a chest's drops (FUN_00585b90).
        // ponytail: "needs it" is not carried (the per-player count's callbacks, LAB_005b8770.., read as that).
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
            if (!d2d::rules::quest::bit(quests(), K::kQuest, 0) && !carries(part) && !carries(K::kWillCode)) drops.push_back({ .code = part });
            const auto [low, high] = d2d::rules::kChestLevels[std::size_t(std::clamp(act(), 0, 2))];
            d2d::rules::chest_round(game_data->rules, d2d::rules::chest_tc(act(), diff, here, alvl(low), alvl(high)), object.seed, drops, 4);
            for (const auto& dropped : drops) loot.put(dropped, object.x, object.y, here, fight.spawning.game, now_ms);
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
        if (object.operate_fn == d2d::rules::operate_fn::kSewerStairs) return;   // FUN_005b84e0: open, its warp's taken as any
        // The compelling orb (OperateFn 53, FUN_005bb980): hit with Khalim's
        // Will in hand; the second hit breaks it: the Will goes, the orb
        // (0x16e) leaves, the object to mode 1. Else "I can't".
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
            music_cue("music_quest_compelling");
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
            for (auto& monster : fight.monsters) if (std::size_t(monster.type) < game_data->mon_bin.size() && monster.type == int(game_data->mon_bin[0x16e])) monster.gone = true;
            to_mode(1);
            doors[{ level, npc_index }] = Door{ 1, now_ms };
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
            gidbinn_timer = now_ms + d2d::rules::BladeQuest::kTimerTicks * kTickMs;
            gidbinn_altar = npc_index;
            d2d::log::info("The Blade of the Old Religion: the altar used");
            return;
        }
        // The hellgate (OperateFn 46, FUN_00584750): with word 0x16 bit 13 or 0
        // (Mephisto dead), FUN_00546ac0 from the Durance's third level: unless
        // word 0x17 has bit 0, Khalim's Will closed if open (word 0x12 bits 0, 13;
        // his eye, heart, brain and the flails taken) and word 0x17 bits 0, 13.
        // Then the fortress's waypoint and its start (FUN_0053aec0(0x67)).
        // ponytail: its sounds (0xca7, 0xb56) and FUN_0053d670(6)'s packet aren't sent.
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
                movie = 4;                                                         // S->C 0x61 {4}: Act04start
            }
            set_waypoint(waypoint_index(*game_data, d2d::rules::level_ids::kPandemoniumFortress));
            arrive(fortress, fortress->start.first, fortress->start.second, "the hellgate");
            return;
        }
        // The orifice (OperateFn 25, FUN_0059dc70): with the Horadric Staff
        // carried it takes it (FUN_0059dd80: the staff and its parts gone,
        // the Horadric Staff done, the way down to Duriel after its timer);
        // without, "I can't" (voice).
        // ponytail: game.exe opens a panel to drop the staff into (C->S 0x44 3); d2d places it on use.
        if (object.operate_fn == d2d::rules::operate_fn::kOrifice) {
            if (operated.contains({ level, npc_index }) || hosted_quests) return;
            if (!carries("hst")) { d2d::log::info("I can't use this yet."); return; }
            if (actor->held && actor->held->code == "hst") actor->held.reset();
            for (const char* code : { "hst", "vip", "msf" })
                if (const auto part = std::ranges::find(actor->character.items, std::string(code), &d2d::d2s::Item::code); part != actor->character.items.end()) actor->character.items.erase(part);
            operated[{ level, npc_index }] = now_ms;
            to_mode(1);
            tombs.staff_in(quests());
            // The party in act 2 not done (FUN_0059dbd0): bits 0 and 13, their staff and parts gone.
            const int placer = actor->id;
            each_player([&](Player& player) {
                auto& bits = quests();
                if (player.id == placer || act() != 1 || d2d::rules::quest::bit(bits, 10, 0)) return;
                d2d::rules::quest::set(bits, 10, 0); d2d::rules::quest::set(bits, 10, 13);
                for (const char* code : { "hst", "vip", "msf" }) while (carries(code)) take_carried(code);
            });
            d2d::log::info("The Horadric Staff: placed in the orifice");
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
        // Storm (FUN_00582da0): everyone about loses Arg0 % of their life.
        // ponytail: "about" as within 30 cells (game.exe's unit search
        // over Arg1 isn't traced).
        if (shrine.code == 19) {
            stat_values[kLife] -= stat_values[kLife] * shrine.arg0 / 100;
            actor->merc->hit_points -= actor->merc->hit_points * shrine.arg0 / 100;
            if (fight.mon_level == level)
                for (auto& monster : fight.monsters)
                    if (monster.alive() && std::abs(monster.unit.x - object.x) < 30 && std::abs(monster.unit.y - object.y) < 30) monster.hit_points -= monster.hit_points * shrine.arg0 / 100;
        }
        // Exploding / Poison: Arg0 .. Arg1 - 1 exploding (opm) / choking gas
        // (gpm) potions at the player's feet (FUN_005830e0 / FUN_00583410).
        if (shrine.code == 21 || shrine.code == 22)
            for (int count = shrine.arg0 + rng(std::max(shrine.arg1 - shrine.arg0, 0)); count > 0; --count)
                loot.put({ .code = shrine.code == 21 ? "opm" : "gpm" }, actor->player.x, actor->player.y, 1, fight.spawning.game, now_ms);
        if ((shrine.code == 19 || shrine.code == 21 || shrine.code == 22) && fight.mon_level == level)
            fight.shrine_missiles(shrine.code, object.x, object.y, int(actor->character.stats.get(kLevel)), now_ms);
        if (shrine.code == 20 && fight.mon_level == level) {   // warping (FUN_00583050): the nearest plain monster turns boss
            // ponytail: FUN_00582750's filter read as alive, not a boss, not
            // an NPC; FUN_0065a800's search range isn't traced.
            int best = -1;
            float best_distance = 1e9f;
            for (std::size_t k = 0; k < fight.monsters.size(); ++k) {
                const auto& monster = fight.monsters[k];
                const float distance = std::hypot(monster.unit.x - actor->player.x, monster.unit.y - actor->player.y);
                if (monster.alive() && monster.boss == d2d::rules::Boss::none && distance < best_distance) { best_distance = distance; best = int(k); }
            }
            if (best >= 0) {                               // FUN_005a4940: FUN_005a0760 (champions allowed), FUN_005a2120
                auto& monster = fight.monsters[std::size_t(best)];
                const auto boss = d2d::rules::roll_boss(game_data->umods, game_data->monsters.types[std::size_t(monster.type)], diff, true, rng);
                make_boss(*game_data, monster, boss.kind, boss.mods, -1, boss.name_seed, diff, rng, int(players.size()));
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
        } else if (door.operate_fn == d2d::rules::operate_fn::kTrapDoor) {            // trap door (FUN_00581eb0): opens, then down its room's warp (FUN_005550b0)
            // ponytail: the level's nearest warp for the room's warp unit (trap
            // doors are past act 1).
            if (state.mode == 0) mode = 2;
            else if (state.mode == 2 && !level->warps.empty())
                actor->take_warp = int(std::ranges::min_element(level->warps, {}, [&](const Level::Warp& warp) { return std::hypot(warp.x - door.x, warp.y - door.y); }) - level->warps.begin());
        } else if (now_ms - state.when >= 500) {       // a door (FUN_00581d40): is anyone in the doorway?
            // ponytail: a unit's own subtile, not its collision pattern.
            auto in_door = [&](float x, float y) {
                const int sub_x = int(x * 5) - (int(door.x * 5) - door.size_x / 2), sub_y = int(y * 5) - (int(door.y * 5) - door.size_y / 2);
                return sub_x >= 0 && sub_y >= 0 && sub_x < door.size_x && sub_y < door.size_y;
            };
            bool occupied = in_door(actor->player.x, actor->player.y) || (actor->merc && in_door(actor->merc->unit.x, actor->merc->unit.y));
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

// Rooms laid again (relevel) stamp every door as its preset left it; the
// doors used since go back to their mode's footprint.
// ponytail: every frame, a few doors; on relevel only if it shows.
auto World::reopen_doors() -> void {
        for (const auto& [key, door] : doors)
            if (key.first == level && std::size_t(key.second) < level->npcs.size())
                set_footprint(*level, level->npcs[std::size_t(key.second)], level->npcs[std::size_t(key.second)].collision >> door.mode & 1);
    }

auto World::set_door_mode(int npc_index, int mode, std::uint32_t now_ms) -> void {
        const auto& door = level->npcs[std::size_t(npc_index)];
        auto& state = doors.try_emplace({ level, npc_index }, Door{ mode_index(door.mode), 0 }).first->second;
        if (state.mode == mode) return;
        state = { mode, now_ms };
        set_footprint(*level, door, door.collision >> mode & 1);   // FUN_00623830 out / FUN_00620a70 back in
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
        // FUN_00584240: a physical blast (FUN_005dfa00 type 0) on each live
        // unit within 3 subtiles: every player, their merc and pets, then
        // the monsters.
        // ponytail: in that order, not the rooms' unit lists (the seed's
        // draws go by those); a monster's, pet's and the merc's life in whole
        // points (Monster::hit_points), not the game's 256ths.
        auto nearby = [&](float x, float y, float subtiles) { return std::hypot(x - barrel.x, y - barrel.y) * 5 <= subtiles; };
        each_player([&](Player& player) {
            const auto& fighter = player.player_combat;
            if (nearby(player.player.x, player.player.y, 3) && player.character.stats.values[kLife] > 0)
                take_blast(player.character.stats.values[kLife], d2d::rules::blast_taken(blast(barrel, player.player.x, player.player.y, 2, player.character.stats.values[kLife], int(player.character.stats.get(kLevel)),
                                                                                        int(player.character.stats.get(kDex) + player.psum[kDex]), fighter.defense),
                                                                                  fighter.dr_flat, fighter.dr_pct));
            if (player.merc && nearby(player.merc->unit.x, player.merc->unit.y, 3) && player.merc->hit_points > 0) {
                const auto merc_fighter = fight.merc_fighter();
                const int dexterity = player.merc_st.dex + int(fight.merc_gear().sum[std::size_t(kDex)]);
                player.merc->hit_points -= d2d::rules::blast_taken(blast(barrel, player.merc->unit.x, player.merc->unit.y, 2, std::int64_t(player.merc->hit_points) << 8, player.merc_st.level, dexterity, merc_fighter.defense),
                                                           merc_fighter.dr_flat, merc_fighter.dr_pct) >> 8;
            }
            for (auto& pet : player.pets) {
                if (pet.where != level || !pet.monster.alive() || !nearby(pet.monster.unit.x, pet.monster.unit.y, 3)) continue;
                const auto target = pet.monster.target(*game_data);
                const int damage = d2d::rules::blast_taken(blast(barrel, pet.monster.unit.x, pet.monster.unit.y, game_data->monsters.types[std::size_t(pet.monster.type)].size,
                                                                 std::int64_t(pet.monster.hit_points) << 8, pet.monster.stats.level, 0, pet.monster.stats.armor_class), 0, target.res[0]);
                fight.pet_hurt(pet, damage >> 8, now_ms);
            }
        });
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
        if (trap == 8 && d2d::rules::trap_undead(level->region[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))], 0) < 0) return;   // FUN_00582250
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
                auto monster = std::move(spawn_monsters(*game_data, std::span{ &spawn, 1 }, region, rng, diff, int(players.size())).front());
                set_mode(*game_data, monster, "S1", now_ms);
                monster.aware = true;
                crowd.units.push_back(&risen.emplace_back(std::move(monster)).unit);
                ++made;
            }
            for (auto& monster : risen) fight.add_monster(std::move(monster));
            d2d::log::info("trap 8: {} x{}", game_data->monsters.types[std::size_t(type)].id, made);
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
            Missile missile{ &missile_info, x, y, dx / distance * speed, dy / distance * speed, missile_heading(dx, dy), now_ms, now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, stats };
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
            shoot(actor->player.x - x, actor->player.y - y, float(missile_info.vel), std::make_shared<std::vector<int>>());
            fight.missiles.back().to = { actor->player.x, actor->player.y };
        }
        d2d::log::info("trap {}: {} at level {}", trap, name, lvl);
    }

}  // namespace d2d::game
