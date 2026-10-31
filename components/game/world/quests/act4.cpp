// SPDX-License-Identifier: GPL-3.0-or-later
// Act 4's quests at the World's quest hooks (world.hpp: World::actN_*;
// rules/quests_act4.hpp, docs/research/re/quests-act4.md).
#include "world.hpp"
#include "ai.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "log.hpp"
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <quests_act4.hpp>
#include <rules.hpp>
#include <shake.hpp>
#include <uniques.hpp>
#include <algorithm>
#include <array>

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::game {

namespace {
// A seal's boss and where it comes, by its OperateFn (FUN_005b6b70 / 6bc0 /
// 6c10: the seal's spot plus these subtiles; [0x744304]+0xb28..).
struct SealBoss { int operate_fn, superunique, off_x, off_y; };
constexpr std::array<SealBoss, 3> kSealBosses{ { { d2d::rules::operate_fn::kSealInfector, 0x24, -12, -52 },
                                                 { d2d::rules::operate_fn::kSealDeSeis, 0x25, -39, 33 },
                                                 { d2d::rules::operate_fn::kSealVizier, 0x26, 32, 16 } } };
constexpr std::uint32_t kFlashFrames = 27;   // object 131's InitFn 59: event 7 at +0x1b frames
}  // namespace

// A seal's superunique and its minions at subtile (x, y) on the sanctuary
// (FUN_00545c30 -> FUN_0054e600).
auto World::seal_boss(const Level& where, int superunique, int x, int y) -> void {
        if (std::size_t(superunique) >= game_data->superuniques.size()) return;
        const int diff = actor->character.header.active_difficulty();
        const auto& row = game_data->superuniques[std::size_t(superunique)];
        const auto [at_x, at_y] = where.nearest_free((float(x) + 0.5f) / 5, (float(y) + 0.5f) / 5);
        auto boss = make_monster(*game_data, row.type, at_x, at_y, fight.rng, diff, false, int(players.size()));
        make_boss(*game_data, boss, d2d::rules::Boss::superunique, row.mods, superunique, 0, diff, fight.rng, int(players.size()));
        boss.aware = true;
        const int minion = game_data->monsters.types[std::size_t(row.type)].minion[0] >= 0 ? game_data->monsters.types[std::size_t(row.type)].minion[0] : row.type;
        const int count = row.min_grp + int(fight.rng.next() % std::uint32_t(std::max(row.max_grp - row.min_grp + 1, 1)));
        fight.add_monster(std::move(boss), &where);
        for (int i = 0; i < count; ++i) {
            const auto [minion_x, minion_y] = where.nearest_free(at_x + float(i % 3 - 1) * 0.6f, at_y + float(i / 3 - 1) * 0.6f);
            auto follower = make_monster(*game_data, minion, minion_x, minion_y, fight.rng, diff, false, int(players.size()));
            make_boss(*game_data, follower, d2d::rules::Boss::minion, {}, -1, 0, diff, fight.rng, int(players.size()));
            follower.aware = true;
            fight.add_monster(std::move(follower), &where);
        }
        d2d::log::info("{} comes (superunique {})", row.name, superunique);
    }

auto World::act4_log(d2d::rules::QuestLogStates& log, const d2d::rules::QuestBits& bits) const -> void {
        log[d2d::rules::IzualQuest::kQuest] = std::uint8_t(izual.log_state(bits));
        log[d2d::rules::HellforgeQuest::kQuest] = std::uint8_t(hellforge.log_state(bits));
        log[d2d::rules::TerrorQuest::kQuest] = std::uint8_t(terror.log_state(bits));
    }

auto World::act4_join() -> void {
        izual = {};
        izual.join(quests());
        hellforge = {};
        hellforge.join(quests(), carries(d2d::rules::HellforgeQuest::kStone));
        terror = {};
        terror.join(quests());
        izual_ghost_at = diablo_at = diablo_dead_at = gems_at = 0;
        gem_rounds = 0;
        forge_inited = false;
        seal_bosses.clear();
        fortress_warp_done = end_movie = game_closed = false;
        actor->had_soulstone = carries(d2d::rules::HellforgeQuest::kStone);
        act4_chain();
    }

// Each record's +0xf0 (a4q1 LAB_005b38e0 -> a4q3 LAB_005b5f40 -> a4q2
// LAB_005b4530): a quest done or shut opens the next.
auto World::act4_chain() -> void {
        if (izual.open && izual.state != 5) return;
        hellforge.opened();
        if (hellforge.open && hellforge.state != 5) return;
        terror.opened();
    }

auto World::act4_enter(const Level& /*from*/) -> void {
        izual.enter(quests(), level->id);
        hellforge.enter(quests(), level->id);
        terror.enter(quests(), level->id);
    }

auto World::act4_talk_closed(int hc_idx) -> void {
        izual.talk_closed(hc_idx);
        hellforge.talk_closed(hc_idx);
        terror.talk_closed(hc_idx);
    }

auto World::act4_talk(std::vector<d2d::rules::QuestMsg>& out, int hc_idx) -> void {
        std::ranges::copy(d2d::rules::Act4Opening::talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(izual.talk(quests(), hc_idx, actor->id), std::back_inserter(out));
        std::ranges::copy(hellforge.talk(quests(), hc_idx, carries(d2d::rules::HellforgeQuest::kStone), actor->id), std::back_inserter(out));
        std::ranges::copy(terror.talk(quests(), hc_idx, actor->character.header.expansion()), std::back_inserter(out));
    }

auto World::act4_said(int hc_idx, int message_string) -> void {
        d2d::rules::Act4Opening::said(quests(), hc_idx, message_string);
        // The Fallen Angel's reward (FUN_005b39a0): 2 skill points (FUN_006272b0 stat 5).
        if (const auto heard = izual.said(quests(), hc_idx, message_string, actor->id); heard.skill_points || heard.chain) {
            actor->character.stats.values[d2d::d2s::kSkillPts] += heard.skill_points;
            if (heard.skill_points) d2d::log::info("The Fallen Angel: Tyrael's reward, +{} skill points", heard.skill_points);
            if (heard.chain) act4_chain();
        }
        // The Hellforge (FUN_005b6100): Cain's soulstone (FUN_005466b0 "mss "), his reward.
        if (const auto heard = hellforge.said(quests(), hc_idx, message_string, actor->id); heard.gives_stone || heard.chain) {
            if (heard.gives_stone) { give_item(d2d::rules::HellforgeQuest::kStone, now); actor->had_soulstone = true; d2d::log::info("The Hellforge: Cain's soulstone"); }
            if (heard.chain) act4_chain();
        }
        // Terror's End (FUN_005b4670): LoD, Tyrael's portal 5 cells east of him (FUN_005b45e0).
        if (terror.said(quests(), hc_idx, message_string, actor->character.header.expansion()).portal && game_data->builder) {
            const auto tyrael = std::ranges::find(level->npcs, d2d::rules::TerrorQuest::kTyrael, &Npc::hc_idx);
            if (tyrael != level->npcs.end()) {
                auto& here = const_cast<Level&>(*level);            // GameData owns its levels mutable (gamedata.cpp)
                auto rgn = d2d::rules::Rng{ fight.rng.next() };
                add_object(*game_data, game_data->builder->objects, game_data->builder->obj_row, here, d2d::rules::object_ids::kLastPortal,
                           int(tyrael->x * 5) + 5, int(tyrael->y * 5), rgn);
                d2d::log::info("Terror's End: Tyrael opens the way to Harrogath");
            }
        }
    }

auto World::act4_alert(int hc_idx) -> bool {
        return d2d::rules::Act4Opening::alert(quests(), hc_idx) || izual.alert(quests(), hc_idx) || hellforge.alert(quests(), hc_idx)
            || terror.alert(quests(), hc_idx, actor->character.header.expansion());
    }

auto World::act4_kill(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        if (hosted_quests) return;
        if (kill.type == d2d::rules::IzualQuest::kIzual) {                // FUN_005b4020, his ghost 3 ticks on
            // The killer and every player in his room or one next to it
            // (FUN_005b3f20) bits 13 and 1; then each with bit 13 its line
            // (LAB_005b3de0 -> sound event 0x4a), the rest bit 14 (LAB_005b3cd0).
            namespace quest = d2d::rules::quest;
            constexpr int kIzualQuest = d2d::rules::IzualQuest::kQuest;
            each_player([&](Player& player) {                  // kill.killer 0: no player's kill
                if ((kill.killer != 0 && player.id == kill.killer) || near_room(kill.x, kill.y, player.player.x, player.player.y)) izual.killed(quests(), kill.x, kill.y);
            });
            each_player([&](Player&) {
                auto& bits = quests();
                if (quest::bit(bits, kIzualQuest, 13)) class_cue("act4_free_izual", now_ms);
                else if (!quest::bit(bits, kIzualQuest, 0) && !quest::bit(bits, kIzualQuest, 1)) quest::set(bits, kIzualQuest, 14);
            });
            music_cue("music_quest_izual");
            izual_ghost_at = now_ms + 3 * kTickMs;
            d2d::log::info("The Fallen Angel: Izual is dead");
        }
        if (kill.type == d2d::rules::HellforgeQuest::kHephasto) {         // FUN_005b65d0: the Hellforge Hammer
            loot.put({ .code = d2d::rules::HellforgeQuest::kHammer }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
            d2d::log::info("The Hellforge: Hephasto drops the hammer");
        }
        if (std::ranges::contains(d2d::rules::TerrorQuest::kSealBosses, kill.super) && terror.boss_died()) diablo_release(now_ms);
        if (kill.type == d2d::rules::TerrorQuest::kDiablo && !terror.dead) {   // FUN_005b52e0 -> FUN_005b4d20
            const bool expansion = actor->character.header.expansion();
            // Every player in his room or one next to it (FUN_005b5140), not
            // done, takes part (FUN_005b4d20); the rest bit 14 (LAB_005b4970).
            // Classic: the next difficulty opens (FUN_00538680); with bit 13 the
            // client plays the end on leaving (S->C 0x5d -> FUN_004a2cb0: DAT_007a0604);
            // the 90 / 95 s timer runs (act4_update). LoD goes on to Tyrael's portal.
            namespace quest = d2d::rules::quest;
            constexpr int kTerrorQuest = d2d::rules::TerrorQuest::kQuest;
            each_player([&](Player& player) {
                auto& bits = quests();
                if (near_room(kill.x, kill.y, player.player.x, player.player.y)) {
                    const bool was_done = quest::bit(bits, kTerrorQuest, 0) || quest::bit(bits, kTerrorQuest, 1);
                    const bool their_expansion = player.character.header.expansion();
                    terror.diablo_died(bits, their_expansion, player.id);
                    if (!was_done && !their_expansion) open_act(4);
                } else if (!quest::bit(bits, kTerrorQuest, 0) && !quest::bit(bits, kTerrorQuest, 1)) quest::set(bits, kTerrorQuest, 14);
            });
            if (!terror.dead) { terror.dead = true; if (terror.open) { terror.log = 13; terror.news = true; } }   // no one by him: the record's death all the same
            if (!expansion) {
                diablo_dead_at = std::max(now_ms, 1u);
                end_movie = d2d::rules::quest::bit(quests(), kTerrorQuest, 13);
            }
            d2d::log::info("Terror's End: Diablo is dead{}", expansion ? "" : " (classic: the next difficulty is open)");
        }
    }

// FUN_005b5230 (once): every monster in the Chaos Sanctuary but Diablo, not
// dead and not a pet, dies (mode 0, FUN_005a7e60 / FUN_005a7c20); Diablo
// comes 10 ticks on (FUN_005b4be0 -> FUN_005b4b60).
auto World::diablo_release(std::uint32_t now_ms) -> void {
        if (terror.released) return;
        terror.released = true;
        // FUN_005b5230 broadcasts the quest flag; FUN_0046b290 starts
        // diablo appears only for clients in the Chaos Sanctuary.
        if (level->id == d2d::rules::level_ids::kChaosSanctuary)
            each_player([](Player& player) { player.events.push_back(ev::QuestQuake{ d2d::rules::QuestQuake::diablo }); });
        if (level->id == d2d::rules::level_ids::kChaosSanctuary)
            for (std::size_t i = 0; i < fight.monsters.size(); ++i)
                if (auto& monster = fight.monsters[i]; monster.alive() && monster.type != d2d::rules::TerrorQuest::kDiablo) {
                    monster.hit_points = 0;
                    set_mode(*game_data, monster, "DT", now_ms);
                    fight.killed(i, now_ms, false);
                }
        diablo_at = now_ms + d2d::rules::TerrorQuest::kRelease * kTickMs;
        d2d::log::info("Terror's End: the seals are broken, Diablo comes");
    }

auto World::act4_update(std::uint32_t now_ms) -> void {
        if (hosted_quests) return;
        // Izual's first think (FUN_005b4390): the Fallen Angel's log sent again.
        for (auto& monster : fight.monsters)
            if (std::exchange(monster.greeted, false)) {
                izual.greet();
                d2d::log::info("The Fallen Angel: Izual's first think, the log at {}", izual.log);
            }
        // a4q1's timer (FUN_005b3e60): the log 3, then Izual's ghost (0x196)
        // where he fell, an NPC.
        if (izual_ghost_at && now_ms >= izual_ghost_at) izual.ghost_timer();
        if (izual_ghost_at && now_ms >= izual_ghost_at && izual.ghost_due) {
            izual_ghost_at = 0; izual.ghost_due = false;
            if (const auto& ghost = game_data->mon_npc[std::size_t(d2d::rules::IzualQuest::kGhost)]; !ghost.code.empty()) {
                auto npc = ghost;
                std::tie(npc.x, npc.y) = level->nearest_free(izual.spot_x, izual.spot_y);
                const_cast<Level&>(*level).npcs.push_back(std::move(npc));   // GameData owns its levels mutable (gamedata.cpp)
                d2d::log::info("The Fallen Angel: Izual's ghost");
            }
        }
        if (izual_ghost_at && now_ms >= izual_ghost_at) izual_ghost_at = 0;
        // A log moved with flag 1 (FUN_00544300): sent again (S->C 0x5d) to a
        // player it's for (act4_detail::told).
        for (auto [news, quest] : { std::pair{ &izual.news, d2d::rules::IzualQuest::kQuest }, { &hellforge.news, d2d::rules::HellforgeQuest::kQuest },
                                    { &terror.news, d2d::rules::TerrorQuest::kQuest } })
            if (std::exchange(*news, false) && d2d::rules::act4_detail::told(quests(), quest)) actor->events.push_back(ev::QuestNews{ quest });
        // The seals' bosses at their flash's event 7 (FUN_005b5750 -> FUN_00545c30).
        const Level* sanctuary = game_data->level(d2d::rules::level_ids::kChaosSanctuary);
        for (auto due = seal_bosses.begin(); due != seal_bosses.end();)
            if (now_ms >= due->at && sanctuary) { seal_boss(*sanctuary, due->superunique, due->x, due->y); due = seal_bosses.erase(due); }
            else ++due;
        // The forge's gems (event 7, FUN_005b6710): a round each 20 frames,
        // round 4 a perfect, 3 and 2 flawless, 1 plain, rand(7) on the quest
        // seed (FUN_005438b0; skulls the seventh); LoD, a rune rand(11) on by
        // difficulty after the last. They fall at the forge wherever the
        // player is (the River of Flame's floor kept till they're back).
        const Level* river = game_data->level(d2d::rules::level_ids::kRiverOfFlame);
        const Npc* forge = nullptr;
        if (river)
            if (const auto found = std::ranges::find(river->npcs, d2d::rules::operate_fn::kHellforge, &Npc::operate_fn); found != river->npcs.end()) forge = &*found;
        // The forge's InitFn (48, FUN_005b5a20) as its room comes up.
        // ponytail: once; d2d's rooms stay up (player_moved), game.exe's
        // come up again after one's freed and run it again.
        if (!forge_inited && forge)
            if (const auto made = fight.spawning.levels.find(river); made != fight.spawning.levels.end())
                if (const int room = room_of(*river, forge->x, forge->y); room >= 0 && std::ranges::contains(made->second.order, std::size_t(room))) {
                    forge_inited = true;
                    hellforge.forge_seen();
                }
        if (gems_at && now_ms >= gems_at && forge) {
            const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
            const auto& area_levels = game_data->area_level;
            const int alvl = std::size_t(river->id) < area_levels.size() ? area_levels[std::size_t(river->id)][std::size_t(diff)] : 1;
            const auto& table = d2d::rules::HellforgeQuest::kGems[std::size_t(gem_rounds == 4 ? 2 : gem_rounds == 1 ? 0 : 1)];
            for (int share = 0; share < std::max(gem_shares, 1); ++share) {   // +0x14: one each player it credited
                const char* gem = table[std::size_t(fight.spawning.quests(7))];
                loot.put({ .code = gem }, forge->x, forge->y, alvl, fight.spawning.game, now_ms, false, 0, river);
                d2d::log::info("The Hellforge: round {}, {}", gem_rounds, gem);
            }
            if (--gem_rounds > 0) gems_at = now_ms + d2d::rules::HellforgeQuest::kRoundFrames * kTickMs;
            else {
                gems_at = 0;
                if (actor->character.header.expansion()) {
                    const int rune = d2d::rules::HellforgeQuest::kRuneFirst[std::size_t(diff)] + fight.spawning.quests(11);
                    loot.put({ .code = std::format("r{:02}", rune) }, forge->x, forge->y, alvl, fight.spawning.game, now_ms, false, 0, river);
                    d2d::log::info("The Hellforge: the rune r{:02}", rune);
                }
            }
        }
        // a4q2's timer (FUN_005b4be0): 10 ticks on, Diablo at his start
        // point (object 255) in the sanctuary, wherever the player is.
        if (diablo_at && now_ms >= diablo_at && sanctuary && !terror.diablo_made) {
            const auto start = std::ranges::find(sanctuary->npcs, d2d::rules::object_ids::kDiabloStart, &Npc::object_id);
            if (start != sanctuary->npcs.end()) {
                const int diff = actor->character.header.active_difficulty();
                const auto [at_x, at_y] = sanctuary->nearest_free(start->x, start->y);
                auto diablo = make_monster(*game_data, d2d::rules::TerrorQuest::kDiablo, at_x, at_y, fight.rng, diff, true, int(players.size()));
                if (const auto boss = d2d::rules::act_boss(diablo.type, game_data->monsters.types[std::size_t(diablo.type)].base)) {   // FUN_005b1cf0
                    diablo.boss = d2d::rules::Boss::unique;
                    diablo.mods = boss->mods;
                    diablo.half_freeze = boss->half_freeze;
                }
                diablo.aware = true;
                fight.add_monster(std::move(diablo), sanctuary);
                terror.diablo_made = true; diablo_at = 0;
                d2d::log::info("Terror's End: Diablo");
            }
        }
        // a4q2's timer after Diablo, classic (FUN_005b4be0, wall-clock ms): 90 s
        // each player (FUN_005b4a80): with bit 13 to the fortress (FUN_0053aec0
        // level 103), without it the client leaves (S->C 0x50 -> C->S 0x69);
        // 95 s the game closes (FUN_00530590).
        // The 75 s step (FUN_0052e2a0) saves each client's character to the
        // realm (game +0x6a 1 / 2, FUN_00532400); a single player or TCP/IP
        // game (+0x6a 3) does nothing, nor does d2d.
        if (diablo_dead_at && now_ms - diablo_dead_at >= 90000 && !fortress_warp_done) {
            fortress_warp_done = true;
            const Level* fortress = game_data->level(d2d::rules::level_ids::kPandemoniumFortress);
            if (d2d::rules::quest::bit(quests(), d2d::rules::TerrorQuest::kQuest, 13) && fortress && fortress->start.first >= 0)
                arrive(fortress, fortress->start.first, fortress->start.second, "the end of Terror's End");
            else game_closed = true;
        }
        if (diablo_dead_at && now_ms - diablo_dead_at >= 95000) {
            diablo_dead_at = 0;
            game_closed = true;
            d2d::log::info("Terror's End: the game closes");
        }
        // The soulstone picked up (LAB_005b5a90): the Hellforge open.
        // ponytail: polled from what's carried, not the item event.
        if (const bool stone = carries(d2d::rules::HellforgeQuest::kStone); stone != actor->had_soulstone) {
            if (stone) hellforge.stone_picked(quests());
            actor->had_soulstone = stone;
        }
    }

// Act 4's quest objects (World::operate): true when it was one.
auto World::act4_operate(int npc_index, std::uint32_t now_ms) -> bool {
        using namespace d2d::rules::operate_fn;
        const auto& object = level->npcs[std::size_t(npc_index)];
        // The Hellforge (OperateFn 49, FUN_005b5c10): the soulstone set in,
        // then three blows of the hammer in hand; its event 7 at mode 3's
        // FrameCnt3 starts the gems (act4_update).
        if (object.operate_fn == kHellforge) {
            if (hosted_quests) return true;
            const bool in_hand = std::ranges::any_of(actor->character.items, [](const d2d::d2s::Item& item) {
                return item.code == d2d::rules::HellforgeQuest::kHammer && item.location == d2d::d2s::item_location::kEquipped
                    && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm); });
            const auto struck = hellforge.operate(quests(), carries(d2d::rules::HellforgeQuest::kStone), in_hand);
            if (struck.takes_stone) { take_carried(d2d::rules::HellforgeQuest::kStone); doors[{ level, npc_index }] = Door{ 2, now_ms }; d2d::log::info("The Hellforge: the soulstone set"); }
            if (struck.done) {
                take_carried(d2d::rules::HellforgeQuest::kHammer);
                // Every other player in act 4 not done (FUN_005b5b00): their hammer
                // and soulstone taken, bits 13 and 1; each a gem a round (+0x14).
                gem_shares = 1;
                const int striker = actor->id;
                each_player([&](Player& player) {
                    auto& bits = quests();
                    constexpr int kForgeQuest = d2d::rules::HellforgeQuest::kQuest;
                    if (player.id == striker || act() != 3 || d2d::rules::quest::bit(bits, kForgeQuest, 0) || d2d::rules::quest::bit(bits, kForgeQuest, 1)) return;
                    while (carries(d2d::rules::HellforgeQuest::kHammer)) take_carried(d2d::rules::HellforgeQuest::kHammer);
                    while (carries(d2d::rules::HellforgeQuest::kStone)) take_carried(d2d::rules::HellforgeQuest::kStone);
                    d2d::rules::quest::set(bits, kForgeQuest, 13); d2d::rules::quest::set(bits, kForgeQuest, 1);
                    ++gem_shares;
                });
                doors[{ level, npc_index }] = Door{ 3, now_ms };
                gem_rounds = d2d::rules::HellforgeQuest::kRounds;
                gems_at = now_ms + d2d::rules::HellforgeQuest::kStrikeFrames * kTickMs;
                cues.cue("quest_hellforge_smash", now_ms, object.x, object.y);
                music_cue("music_quest_forge");
                d2d::log::info("The Hellforge: the soulstone is shattered");
            }
            return true;
        }
        // The seals (52, 54..56): open once (mode 1). 54..56 (FUN_005b6b70 /
        // 6bc0 / 6c10) keep the seal's spot plus their offset and put the
        // flash (object 131, FUN_005b6ad0) at a free spot near it
        // (FUN_00545340: size 3, mask 0x3f11, 100 rings); only with the flash
        // made does the seal open (FUN_005b5630). The flash's event 7, 27
        // frames on (InitFn 59), makes the boss there (act4_update). All five
        // open and the three dead release Diablo.
        if (object.operate_fn == kSeal || object.operate_fn == kSealInfector || object.operate_fn == kSealDeSeis || object.operate_fn == kSealVizier) {
            if (hosted_quests || operated.contains({ level, npc_index })) return true;
            for (const auto& [operate_fn, superunique, off_x, off_y] : kSealBosses)
                if (object.operate_fn == operate_fn) {
                    const auto spot = level->free_area(int(std::floor(object.x * 5)) + off_x, int(std::floor(object.y * 5)) + off_y, 3, 0x3f11, 100);
                    if (!spot) return true;
                    const std::uint32_t event7 = now_ms + kFlashFrames * kTickMs;
                    if (!game_data->afterglow.code.empty())
                        fires.push_back({ level, &game_data->afterglow, (float(spot->first) + 0.5f) / 5, (float(spot->second) + 0.5f) / 5, now_ms, event7 });
                    seal_bosses.push_back({ superunique, spot->first, spot->second, event7 });
                }
            operated[{ level, npc_index }] = now_ms;
            if (terror.seal(object.object_id)) diablo_release(now_ms);
            return true;
        }
        // Tyrael's portal (OperateFn 73, FUN_005b5880): LoD, Terror's End done or
        // in this game: word 0x1c bits 0 and 13, then to Harrogath (FUN_0054b830(0x6d, 5):
        // its start, its waypoint).
        // The first time, the client not busy: S->C 0x61 {5} (ACT04END, then
        // in LoD D2X_INTRO).
        if (object.operate_fn == kLastPortal) {
            if (!actor->character.header.expansion() || (!d2d::rules::quest::bit(quests(), d2d::rules::TerrorQuest::kQuest, 0) && !d2d::rules::quest::bit(quests(), d2d::rules::TerrorQuest::kQuest, 13))) return true;
            if (!d2d::rules::quest::bit(quests(), 0x1c, 0)) { d2d::rules::quest::set(quests(), 0x1c, 0); d2d::rules::quest::set(quests(), 0x1c, 13); movie = 5; }
            const Level* harrogath = game_data->level(d2d::rules::level_ids::kHarrogath);
            if (!harrogath || harrogath->start.first < 0) { d2d::log::warn("Harrogath didn't build; Tyrael's portal stays shut"); return true; }
            set_waypoint(waypoint_index(*game_data, d2d::rules::level_ids::kHarrogath));
            arrive(harrogath, harrogath->start.first, harrogath->start.second, "Tyrael's portal");
            return true;
        }
        return false;
    }

}  // namespace d2d::game
