// SPDX-License-Identifier: GPL-3.0-or-later
// Act 5's quests at the World's quest hooks (world.hpp: World::actN_*;
// rules/quests_act5.hpp, docs/research/re/quests-act5.md).
#include "world.hpp"
#include "ai.hpp"
#include "character.hpp"
#include "gamedata.hpp"
#include "log.hpp"
#include <d2s_items.hpp>
#include <combat.hpp>
#include <level_ids.hpp>
#include <missiles.hpp>
#include <monsters.hpp>
#include <quests.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::game {

namespace {
int subtile(float cells) { return int(std::floor(cells * 5)); }
// FUN_00619790's list (room2 +8): the player's room is the dead one's or on
// its near list.
bool rooms_near(const GameData& game_data, const Level& where, float x, float y, float dead_x, float dead_y) {
    const int room = room_of(where, x, y);
    return room >= 0 && std::ranges::contains(near_rooms(game_data, where, dead_x, dead_y), std::pair{ &where, room });
}
}  // namespace

auto World::act5_log(d2d::rules::QuestLogStates& log, const d2d::rules::QuestBits& bits) const -> void {
        log[d2d::rules::SiegeQuest::kQuest] = std::uint8_t(siege.log_state(bits));
        log[d2d::rules::RescueQuest::kQuest] = std::uint8_t(rescue.log_state(bits));
        log[d2d::rules::PrisonQuest::kQuest] = std::uint8_t(prison.log_state(bits, carries(d2d::rules::PrisonQuest::kPotionCode)));
        log[d2d::rules::BetrayalQuest::kQuest] = std::uint8_t(betrayal.log_state(bits));
        log[d2d::rules::RiteQuest::kQuest] = std::uint8_t(rite.log_state(bits));
        log[d2d::rules::EveQuest::kQuest] = std::uint8_t(eve.log_state(bits));
    }

auto World::act5_join() -> void {
        const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        siege = {};
        siege.join(quests(), d2d::rules::Act5Opening::heard(actor->character.header.intros[std::size_t(diff)], d2d::rules::Act5Opening::kMalah));
        rescue = {};
        rescue.join(quests());
        prison = {};
        prison.join(quests(), carries(d2d::rules::PrisonQuest::kPotionCode));
        betrayal = {};
        betrayal.join(quests());
        betrayal_log_at = 0;
        rite = {};
        rite.join(quests());
        statues_at = statues_back_at = 0;
        rite_died = false;
        eve = {};
        if (eve.join(quests())) open_act(5);                // FUN_00538680: the next difficulty
        tyrael_at = 0;
        ubers_dead = 0;
        cages_seen = 0;
        cage_portal = {};
        fana_at = thaw_at = 0;
        fana = { nullptr, -1 };
        nihlathak_made = false;
        act5_chain();
    }

// Each record's +0xf0 (a5q1 FUN_00587560 -> a5q2 FUN_005883e0 -> a5q3
// FUN_00589160 -> a5q4 FUN_0058b140): a quest done or shut opens the next;
// a5q4 passes on from state 4 (Nihlathak dead).
auto World::act5_chain() -> void {
        if (siege.open && siege.state != 5) return;
        rescue.opened();
        if (rescue.open && rescue.state != 5) return;
        prison.opened();
        if (prison.open && prison.state < 5) return;
        betrayal.opened();
        if (betrayal.open && betrayal.state < 4) return;
        rite.opened();
        if (rite.open && rite.state != 5) return;
        eve.opened(quests(), d2d::rules::level_ids::act_of(level->id) == 4);
    }

auto World::act5_enter(const Level& from) -> void {
        siege.enter(quests(), from.id, level->id);
        rescue.enter(quests(), from.id, level->id);
        prison.enter(quests(), from.id, level->id);
        act5_hide();
        betrayal.enter(quests(), from.id, level->id);
        eve.enter(quests(), from.id, level->id);
        rite.enter(quests(), from.id, level->id, int(actor->character.stats.get(d2d::d2s::kLevel)), actor->character.header.active_difficulty());
    }

auto World::act5_talk_closed(int hc_idx) -> void {
        siege.talk_closed(hc_idx);
        rescue.talk_closed(hc_idx);
        prison.talk_closed(hc_idx);
        rite.talk_closed(hc_idx);
        // Tyrael's talk closed, once (LAB_0058d870 -> FUN_0058d7d0): the last
        // portal (object 565) 5 subtiles east of each player in the Chamber.
        if (eve.talk_closed(hc_idx) && level->id == d2d::rules::EveQuest::kChamber && game_data->builder) {
            auto rgn = d2d::rules::Rng{ fight.rng.next() };
            const auto [spot_x, spot_y] = level->nearest_free(actor->player.x + 1.f, actor->player.y);
            add_object(*game_data, game_data->builder->objects, game_data->builder->obj_row, const_cast<Level&>(*level), d2d::rules::EveQuest::kLastPortal, subtile(spot_x), subtile(spot_y), rgn);
            d2d::log::info("Eve of Destruction: Tyrael opens the way home");
        }
        // Anya's red portal to Nihlathak's Temple (FUN_0058af90: FUN_0056d130,
        // object 60 at her spot +10, +5 subtiles), its twin at the temple's
        // portal spot.
        if (betrayal.talk_closed(hc_idx)) {
            const auto anya = std::ranges::find(level->npcs, d2d::rules::BetrayalQuest::kAnya, &Npc::hc_idx);
            const Level* nihlathaks = game_data->level(d2d::rules::level_ids::kNihlathaksTemple);
            if (anya == level->npcs.end() || !nihlathaks || nihlathaks->ds1.width() == 0) { d2d::log::warn("Betrayal of Harrogath: no Anya or no temple; her portal stays shut"); return; }
            const auto [spot_x, spot_y] = level->nearest_free(anya->x + 10.f / 5, anya->y + 5.f / 5);
            portal[kTemplePortal] = { level, spot_x, spot_y, now };
            const auto [to_x, to_y] = nihlathaks->portal_spot.first >= 0 ? nihlathaks->nearest_free(nihlathaks->portal_spot.first, nihlathaks->portal_spot.second)
                                                                    : nihlathaks->nearest_free(float(nihlathaks->ds1.width()) / 2, float(nihlathaks->ds1.height()) / 2);
            portal[kTemplePortal + 1] = { nihlathaks, to_x, to_y, now };
            cues.cue("object_townportal", now, spot_x, spot_y);
            d2d::log::info("Betrayal of Harrogath: Anya opens the way to Nihlathak's Temple");
        }
    }

auto World::act5_talk(std::vector<d2d::rules::QuestMsg>& out, int hc_idx) -> void {
        const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        const bool potion = carries(d2d::rules::PrisonQuest::kPotionCode);
        std::ranges::copy(d2d::rules::Act5Opening::talk(actor->character.header.intros[std::size_t(diff)], hc_idx, actor->character.header.cls), std::back_inserter(out));
        std::ranges::copy(siege.talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(rescue.talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(prison.talk(quests(), hc_idx, potion), std::back_inserter(out));
        std::ranges::copy(betrayal.talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(rite.talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(eve.talk(quests(), hc_idx), std::back_inserter(out));
    }

auto World::act5_said(int hc_idx, int message_string) -> void {
        const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        if (d2d::rules::Act5Opening::said(actor->character.header.intros[std::size_t(diff)], hc_idx, message_string)) siege.malah_heard();
        if (siege.said(quests(), hc_idx, message_string)) act5_chain();
        // Rescue on Mount Arreat (FUN_00587b00): Qual-Kehk's runes, one by one (FUN_005466b0).
        if (const auto heard = rescue.said(quests(), hc_idx, message_string); heard.runes || heard.chain) {
            for (int rune = 0; rune < heard.runes; ++rune) give_item(d2d::rules::RescueQuest::kRunes[std::size_t(rune)], now);
            if (heard.runes) d2d::log::info("Rescue on Mount Arreat: Qual-Kehk's {} runes", heard.runes);
            if (heard.chain) act5_chain();
        }
        // Prison of Ice (FUN_00589580): Malah's potion and scroll (FUN_005466b0,
        // normal); Anya's item, rare at the player's level, rand(count) on the
        // quest seed (FUN_005438b0); Anya home (FUN_005890b0, FUN_005891d0).
        const auto heard = prison.said(quests(), hc_idx, message_string, carries(d2d::rules::PrisonQuest::kPotionCode), carries(d2d::rules::PrisonQuest::kScrollCode),
                                       actor->character.header.active_difficulty(), actor->character.header.cls, [&] { return fight.spawning.quests.next(); });
        if (heard.give) {
            give_item(heard.give, now, heard.rare ? int(actor->character.stats.get(d2d::d2s::kLevel)) : 1, heard.rare ? 6 : 2);
            d2d::log::info("Prison of Ice: {} gives {}", hc_idx == d2d::rules::PrisonQuest::kAnya ? "Anya" : "Malah", heard.give);
        }
        if (heard.anya_home) {
            act5_hide();
            d2d::log::info("Prison of Ice: Anya goes home");
        }
        if (betrayal.said(quests(), hc_idx, message_string)) act5_chain();
        if (rite.said(quests(), hc_idx, message_string, int(actor->character.stats.get(d2d::d2s::kLevel)), actor->character.header.active_difficulty()))
            statues_at = now + d2d::rules::RiteQuest::kWakeFrames * kTickMs;
        d2d::rules::EveQuest::said(quests(), hc_idx, message_string);
    }

auto World::act5_alert(int hc_idx) -> bool {
        const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        return d2d::rules::Act5Opening::alert(actor->character.header.intros[std::size_t(diff)], hc_idx) || siege.alert(quests(), hc_idx) || rescue.alert(quests(), hc_idx)
            || prison.alert(quests(), hc_idx, carries(d2d::rules::PrisonQuest::kPotionCode)) || betrayal.alert(quests(), hc_idx) || rite.alert(quests(), hc_idx) || d2d::rules::EveQuest::alert(quests(), hc_idx);
    }

auto World::act5_kill(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        if (hosted_quests) return;
        // Shenk the Overseer (superunique 0x2a, bound to 0x1f: FUN_00587330).
        if (kill.super == d2d::rules::SiegeQuest::kShenk) {   // each player in his room or its near list; the rest bit 14
            each_player([&](Player& player) { siege.killed(quests(), rooms_near(*game_data, *level, player.player.x, player.player.y, kill.x, kill.y)); });
            d2d::log::info("Siege on Harrogath: Shenk is dead");
        }
        // Rescue on Mount Arreat (FUN_00588040, units bound to 0x20): a door
        // broken frees the living prisoners within 15 of it; a prisoner dead.
        if (std::size_t(kill.type) < game_data->monsters.types.size()) {
            const int base = game_data->monsters.types[std::size_t(kill.type)].base;
            if (base == d2d::rules::RescueQuest::kDoor) {
                std::vector<int> near_ids;
                for (const auto& prisoner : fight.monsters)
                    if (prisoner.alive() && game_data->monsters.types[std::size_t(prisoner.type)].base == d2d::rules::RescueQuest::kPrisoner
                        && rescue.cage_of(prisoner.id) >= 0 && !std::ranges::contains(rescue.freed_ids, prisoner.id)
                        && d2d::rules::ai_distance(subtile(prisoner.unit.x) - subtile(kill.x), subtile(prisoner.unit.y) - subtile(kill.y)) < d2d::rules::RescueQuest::kFreeReach)   // FUN_006416d0
                        near_ids.push_back(prisoner.id);
                rescue.door_broken(quests(), near_ids);
                d2d::log::info("Rescue on Mount Arreat: a door broken, {} freed", near_ids.size());
            }
            if (base == d2d::rules::RescueQuest::kPrisoner && rescue.cage_of(kill.id) >= 0) {
                rescue.prisoner_died(quests(), kill.id);
                d2d::log::info("Rescue on Mount Arreat: a prisoner dead ({} caged dead)", rescue.dead);
            }
        }
        // The Ancients (FUN_0058c9a0): the last dead wins the Rite for the
        // players on the Summit of the level (FUN_0058c7e0: bits 13 and 0, the
        // experience FUN_0058c5c0), the rest bit 14 (FUN_0058c780); the chain.
        // Each while they're up: its statue due back, and its death missile
        // (FUN_0058c8d0: "ancient death center", flags 0x420) from where it
        // fell to its statue, its range the frames to get there (flag 0x400,
        // FUN_0059fa30: FUN_006417f0's subtile distance, at least 1, << 16 over
        // velocity << 4).
        const auto ancient = std::ranges::find_if(d2d::rules::RiteQuest::kAncients, [&](int row) { return kill.type == int(game_data->mon_bin[std::size_t(row)]); });
        const int statue = ancient == d2d::rules::RiteQuest::kAncients.end() ? -1 : d2d::rules::RiteQuest::kStatueOf[std::size_t(ancient - d2d::rules::RiteQuest::kAncients.begin())];
        if (statue >= 0 && rite.ancient_fell(statue)) {
            const auto statue_at = std::ranges::find(level->npcs, d2d::rules::RiteQuest::kStatues[std::size_t(statue)], &Npc::object_id);
            const auto center = game_data->missiles.find("ancient death center"), cloud = game_data->missiles.find("ancient death cloud");
            if (statue_at != level->npcs.end() && center != game_data->missiles.end() && cloud != game_data->missiles.end()) {
                const int velocity = d2d::rules::missile_velocity(center->second.vel, center->second.vel_lev, 1);
                const float dx = statue_at->x - kill.x, dy = statue_at->y - kill.y, length = std::max(std::hypot(dx, dy), 0.01f), speed = missile_speed(velocity);
                const int apart = std::max(d2d::rules::ai_distance(subtile(statue_at->x) - subtile(kill.x), subtile(statue_at->y) - subtile(kill.y)), 1);
                const auto frames = velocity > 0 ? std::uint32_t((std::int64_t(apart) << 16) / (std::int64_t(velocity) << 4)) : 0u;
                Missile missile{ &center->second, kill.x, kill.y, dx / length * speed, dy / length * speed, missile_facing(kill.x, kill.y, dx, dy), now_ms, now_ms + std::max(frames, 1u) * kTickMs, {} };
                missile.visual_only = true; missile.owner = kill.id; missile.sub = &cloud->second;
                missile.to = std::pair{ statue_at->x, statue_at->y };
                fight.missiles.push_back(std::move(missile));
            }
        }
        if (statue >= 0 && rite.ancient_died()) {
            each_player([&](Player&) {
                const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
                auto& stats = actor->character.stats;
                if (!rite.reward(quests(), level->id == d2d::rules::RiteQuest::kSummit, int(stats.get(d2d::d2s::kLevel)), diff)) return;
                const auto clvl = std::size_t(std::max<std::int64_t>(stats.get(d2d::d2s::kLevel), 1));
                const auto& next = game_data->exp_next;
                std::int64_t exp = d2d::rules::RiteQuest::kExperience[std::size_t(diff)];
                if (clvl + 1 < next.size()) exp = std::min(exp, next[clvl + 1] - next[clvl]);   // FUN_00611800 rows clvl+1 and clvl
                else exp = 0;
                const auto save_class = std::size_t(std::max(actor->character.character_class, 0));
                if (exp > 0 && d2d::rules::gain_exp(stats, exp, next, game_data->class_gains[save_class]))
                    actor->character.panel = panel_stats(*game_data, actor->character.header, actor->character.items, stats);
                d2d::log::info("Rite of Passage: +{} exp", exp);
            });
            // Object 561 where the last fell (FUN_00555230): OperateFn 69, their farewell.
            if (game_data->builder) {
                auto rgn = d2d::rules::Rng{ fight.rng.next() };
                add_object(*game_data, game_data->builder->objects, game_data->builder->obj_row, const_cast<Level&>(*level), d2d::rules::RiteQuest::kFarewell, subtile(kill.x), subtile(kill.y), rgn);
            }
            act5_chain();
            d2d::log::info("Rite of Passage: the Ancients are dead");
        }
        // Baal (baalcrab, bound to 0x24: FUN_0058df20): those in the Chamber
        // done (FUN_0058dd30: bits 13 and 0, the next difficulty), the rest
        // bit 14; his light fades 650 frames (missile 625), then Tyrael.
        // A killer without bit 0 till now: a gold pile at Baal per player
        // done here (record +0x88), each 1500..2999 + 6000 x difficulty on
        // the quest seed (FUN_005438b0 -> FUN_004bc500, FUN_0055b030); then
        // the game saved (FUN_0052e2a0).
        if (kill.type == int(game_data->mon_bin[std::size_t(d2d::rules::EveQuest::kBaal)]) && eve.state != 5) {
            eve.killed();
            const auto killer = std::ranges::find(players, kill.killer, [](const auto& player) { return player->id; });
            const bool first = killer != players.end() && !d2d::rules::quest::bit((*killer)->character.header.quests[std::size_t(std::clamp((*killer)->character.header.active_difficulty(), 0, 2))], d2d::rules::EveQuest::kQuest, 0);
            // The passes (FUN_0058dea0, LAB_0058dd90, LAB_0058dde0): those in the
            // Chamber; then, with one of them done, the party's in Act 5 bits 0 and
            // 13 (FUN_0058dc60); then the rest bit 14.
            int done_here = 0;
            const bool in_chamber = level->id == d2d::rules::EveQuest::kChamber;
            each_player([&](Player&) { if (in_chamber && d2d::rules::EveQuest::reward(quests(), true)) { open_act(5); ++done_here; } });
            if (done_here > 0 && act() == 4) party_bits_all(d2d::rules::EveQuest::kQuest, { 0, 13 });
            each_player([&](Player&) { if (!in_chamber || !d2d::rules::quest::bit(quests(), d2d::rules::EveQuest::kQuest, 13)) d2d::rules::EveQuest::reward(quests(), false); });
            if (first) {
                const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
                for (int pile = 0; pile < done_here; ++pile) {
                    const int gold = d2d::rules::store_range(fight.spawning.quests, diff * 6000 + 1500, std::min(diff * 6000 + 3000, 0xffff));
                    loot.put({ .code = "gld", .gold = gold }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
                }
                d2d::log::info("Eve of Destruction: gold for Baal's killer, {} pile(s)", done_here);
            }
            if (killer != players.end()) save_due = true;
            baal_fell = { kill.x, kill.y };
            tyrael_at = now_ms + d2d::rules::EveQuest::kTyraelFrames * kTickMs;
            d2d::log::info("Eve of Destruction: Baal is dead");
        }
        // The Tristram ubers (mod 22, FUN_005e0070): uber Mephisto, Diablo and
        // Baal (0x2c0 / 0x2c1 / 0x2c5) each marked; the three dead: the Hellfire
        // Torch (cm2, unique: FUN_00559a30 quality 7) and a Standard of Heroes
        // (std, normal) per player where the last fell.
        // ponytail: the item level is the uber's; FUN_00559a30's own isn't traced.
        for (const auto& [bit, uber] : { std::pair{ 1, 0x2c0 }, std::pair{ 2, 0x2c1 }, std::pair{ 4, 0x2c5 } })
            if (kill.type == int(game_data->mon_bin[std::size_t(uber)]) && !(ubers_dead & bit) && (ubers_dead |= bit) == 7) {
                loot.put({ .code = "cm2", .quality = 7 }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
                each_player([&](Player&) { loot.put({ .code = "std" }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms); });
                d2d::log::info("Uber Tristram: the three are dead, the Hellfire Torch drops");
            }
        // Nihlathak (MonStats base 0x20e, bound to 0x22: FUN_0058b7a0).
        if (std::size_t(kill.type) < game_data->monsters.types.size() && game_data->monsters.types[std::size_t(kill.type)].base == d2d::rules::BetrayalQuest::kNihlathak) {
            bool timer = false;                        // each player by him (FUN_0058b7a0's pass)
            each_player([&](Player& player) { timer |= betrayal.killed(quests(), rooms_near(*game_data, *level, player.player.x, player.player.y, kill.x, kill.y)); });
            if (timer) betrayal_log_at = now_ms + d2d::rules::BetrayalQuest::kLogTicks * kTickMs;
            d2d::log::info("Betrayal of Harrogath: Nihlathak is dead");
        }
    }

// a5q2's cages and prisoners. A cage's InitFn (62, FUN_005886a0) runs as
// its room populates (objgroups.cpp cage_prisoners: its slot, its five
// prisoners, FUN_00588600, bound to 0x20); here each cage comes into the
// quest in that order and each prisoner into its cage. Each prisoner's view
// for its think (ai.cpp's Wussie), then what it asked: the log 5, its
// cage's portal (FUN_005888d0: at the broken door's spot +2 subtiles east,
// FUN_00555230 object 189: theirs alone), gone through it (FUN_00588880).
// ponytail: the portal's fallback spots (x - 2, then FUN_00545340) are the
// nearest free one.
auto World::rescue_update(std::uint32_t now_ms) -> void {
        for (; cages_seen < fight.spawning.rescue_cages.size(); ++cages_seen) {
            const auto& [cage_level, cage_x, cage_y] = fight.spawning.rescue_cages[cages_seen];
            const int slot = rescue.cage(cage_x, cage_y);
            d2d::log::info("Rescue on Mount Arreat: cage {} up (level {})", slot, cage_level);
        }
        for (const auto& prisoner : fight.monsters)
            if (prisoner.cage >= 0 && prisoner.cage < d2d::rules::RescueQuest::kCages && rescue.cage_of(prisoner.id) < 0) {
                const auto& ids = rescue.cages[std::size_t(prisoner.cage)].ids;
                const auto free_slot = std::ranges::find(ids, -1);
                if (free_slot != ids.end()) rescue.prisoner_made(prisoner.cage, int(free_slot - ids.begin()), prisoner.id);
            }
        const auto door_down = [&](float x, float y) {                  // FUN_00588e10: a dead prisondoor in the player's rooms
            const auto rooms = near_rooms(*game_data, *level, x, y);
            return std::ranges::any_of(fight.monsters, [&](const Monster& door) {
                return !door.alive() && game_data->monsters.types[std::size_t(door.type)].base == d2d::rules::RescueQuest::kDoor
                    && std::ranges::contains(rooms, std::pair{ level, room_of(*level, door.unit.x, door.unit.y) }); });
        };
        for (auto& prisoner : fight.monsters) {
            const int slot = rescue.cage_of(prisoner.id);
            if (slot < 0) continue;
            auto& view = prisoner.wussie;
            if (std::exchange(view.left, false)) { rescue.went(slot); d2d::log::info("Rescue on Mount Arreat: a prisoner through the portal"); }
            if (!prisoner.alive()) continue;
            const auto& gate = cage_portal[std::size_t(slot)];
            view.portal = gate.level == level;
            if (view.portal) { view.portal_x = subtile(gate.x); view.portal_y = subtile(gate.y); }
            view.cage_left = rescue.cages[std::size_t(slot)].gone > 0;
            // FUN_005ddf20: the nearest player within 16 (FUN_005dc380).
            const Player* nearest = nullptr;
            int best = 16;
            for (const auto& other : players) {
                const int dist = d2d::rules::ai_distance(subtile(other->player.x) - subtile(prisoner.unit.x), subtile(other->player.y) - subtile(prisoner.unit.y));
                if (dist < best) { best = dist; nearest = other.get(); }
            }
            view.player = nearest != nullptr;
            view.door_broken = nearest && door_down(nearest->player.x, nearest->player.y);
            if (nearest) view.player_id = nearest->id;
            if (std::exchange(view.cried, false)) rescue.cried();
            if (std::exchange(view.open_portal, false) && rescue.portal_due(slot)) {
                // FUN_005888d0: the broken door within 15 of it, in the player's rooms.
                const Monster* door = nullptr;
                if (nearest) {
                    const auto rooms = near_rooms(*game_data, *level, nearest->player.x, nearest->player.y);
                    for (const auto& other : fight.monsters)
                        if (!other.alive() && game_data->monsters.types[std::size_t(other.type)].base == d2d::rules::RescueQuest::kDoor
                            && std::ranges::contains(rooms, std::pair{ level, room_of(*level, other.unit.x, other.unit.y) })
                            && d2d::rules::ai_distance(subtile(other.unit.x) - subtile(prisoner.unit.x), subtile(other.unit.y) - subtile(prisoner.unit.y)) < 15) { door = &other; break; }
                }
                if (!door) continue;
                const auto [spot_x, spot_y] = level->nearest_free(door->unit.x + 2.f / 5, door->unit.y);
                cage_portal[std::size_t(slot)] = { level, spot_x, spot_y, now_ms };
                cues.cue("object_townportal", now_ms, spot_x, spot_y);
                const auto opener = std::ranges::find(players, view.player_id, [](const auto& player) { return player->id; });
                Player& was = *actor;
                act_as(opener == players.end() ? lead() : **opener);
                rescue.portal_opened(quests(), slot);
                // All in: with 12 freed the opener's party in act 5 shares it
                // (LAB_005887a0 -> FUN_00587e60), then the rest bit 14 (LAB_00587f60).
                if (rescue.all_in()) {
                    const bool shared = rescue.freed >= 12 && d2d::rules::quest::bit(quests(), d2d::rules::RescueQuest::kQuest, 13);
                    const int opener_id = actor->id;
                    each_player([&](Player& player) {
                        if (shared && player.id != opener_id && act() == 4) rescue.party_share(quests());
                        d2d::rules::RescueQuest::someone_else(quests());
                    });
                }
                act_as(was);
                d2d::log::info("Rescue on Mount Arreat: cage {}'s portal ({} freed, {} dead of {})", slot, rescue.freed, rescue.dead, rescue.made);
            }
        }
    }

// Who's gone, as each level comes up: Harrogath's Nihlathak once the record's
// +0x88 is set (FUN_005893e0 -> FUN_00589340: unit flag 1); fana once she's
// thawed (removed, FUN_0064c370); drehyaiced once Anya's home (FUN_005890b0);
// Anya's frozen till then (not in town: InitFn 66 makes her at +0x84 2).
auto World::act5_hide() -> void {
        // Anya in town (InitFn 66, FUN_0058eac0 / FUN_005891d0): within 1 of her
        // marker (459) once +0x84 is 2.
        if (const Level* town = game_data->level(d2d::rules::level_ids::kHarrogath); town && prison.anya == 2
            && std::ranges::none_of(town->npcs, [](const Npc& npc) { return npc.hc_idx == d2d::rules::PrisonQuest::kAnya; })) {
            const auto marker = std::ranges::find(town->npcs, d2d::rules::PrisonQuest::kAnyaMarker, &Npc::object_id);
            const auto& anya = game_data->mon_npc[std::size_t(game_data->mon_bin[std::size_t(d2d::rules::PrisonQuest::kAnya)])];
            if (marker != town->npcs.end() && !anya.code.empty()) {
                auto npc = anya;
                std::tie(npc.x, npc.y) = town->nearest_free(marker->x, marker->y);
                const_cast<Level&>(*town).npcs.push_back(std::move(npc));   // GameData owns its levels mutable (gamedata.cpp)
            }
        }
        for (std::size_t i = 0; i < level->npcs.size() && i < npc_states.size(); ++i) {
            const auto& npc = level->npcs[i];
            if (level->id == d2d::rules::level_ids::kHarrogath && npc.hc_idx == d2d::rules::act5_detail::kNihlathak) npc_states[i].hidden = prison.town_left;
            if (npc.object_id == d2d::rules::PrisonQuest::kFana) npc_states[i].hidden = prison.anya != 0 && !prison.thawing;
            if (npc.hc_idx == d2d::rules::PrisonQuest::kFrozen) npc_states[i].hidden = prison.anya == 2;
        }
    }

// a5q3's objects: the Frozen River's marker (460) as its room comes up, fana
// 25 frames on at its spot (event 7, FUN_0058a500; again till made) and her
// InitFn (74: the log 2); the thaw, a tick a step; the Halls of Vaught's
// marker (462, InitFn 69, FUN_0058a6c0) with +0x88 set: superunique 60,
// Nihlathak, at its spot (FUN_0054e600), once.
// ponytail: the marker's event reruns (fana not made) are one try.
auto World::prison_update(std::uint32_t now_ms) -> void {
        const auto made_rooms = fight.spawning.levels.find(level);
        const auto room_up = [&](const Npc& npc) {
            const int room = room_of(*level, npc.x, npc.y);
            return made_rooms != fight.spawning.levels.end() && room >= 0 && std::ranges::contains(made_rooms->second.order, std::size_t(room));
        };
        const auto marker = [&](int object_id) { return std::ranges::find(level->npcs, object_id, &Npc::object_id); };
        if (const auto river = marker(d2d::rules::PrisonQuest::kRiverMarker); river != level->npcs.end() && prison.open && !prison.fana_made && room_up(*river)) {
            if (!fana_at) fana_at = now_ms + d2d::rules::PrisonQuest::kFanaFrames * kTickMs;
            else if (now_ms >= fana_at && game_data->builder) {
                auto rgn = d2d::rules::Rng{ fight.rng.next() };
                add_object(*game_data, game_data->builder->objects, game_data->builder->obj_row, const_cast<Level&>(*level), d2d::rules::PrisonQuest::kFana,
                           subtile(river->x), subtile(river->y), rgn);
                fana = { level, int(level->npcs.size()) - 1 };
                prison.fana_made = true; fana_at = 0;
                prison.fana_seen();
                d2d::log::info("Prison of Ice: frozen Anya");
            }
        }
        if (prison.thawing && fana.first && now_ms >= thaw_at) {
            thaw_at = now_ms + kTickMs;
            if (const int step = prison.thaw_tick(); step == 1) doors[fana] = Door{ 2, now_ms };
            else if (step == 2) {
                const auto& ice = fana.first->npcs[std::size_t(fana.second)];
                const auto& anya = game_data->mon_npc[std::size_t(game_data->mon_bin[std::size_t(d2d::rules::PrisonQuest::kFrozen)])];
                if (!anya.code.empty()) {
                    auto npc = anya;
                    std::tie(npc.x, npc.y) = fana.first->nearest_free(ice.x, ice.y);
                    const_cast<Level&>(*fana.first).npcs.push_back(std::move(npc));   // GameData owns its levels mutable (gamedata.cpp)
                }
                act5_chain();
                d2d::log::info("Prison of Ice: Anya thaws");
            }
        }
        if (const auto throne = marker(d2d::rules::PrisonQuest::kNihlathakMarker); throne != level->npcs.end() && prison.town_left && !nihlathak_made && room_up(*throne)) {
            nihlathak_made = true;
            seal_boss(*level, d2d::rules::PrisonQuest::kNihlathakBoss, subtile(throne->x), subtile(throne->y));
        }
    }

// a5q5 on the Summit: the statues' event 7, 20 frames after the speech
// (FUN_0058c0e0: each to mode 4, its Ancient at its spot, FUN_00545c30);
// the Ancients asleep till then; a death there puts it back (FUN_0058c000:
// the Ancients gone, statues and altar to mode 0).
// An Ancient fallen: the return timer every 2 frames (FUN_0058bd50), its
// statue to mode 1, then mode 2 after FrameCnt1 (its event 1).
// ponytail: one try per statue.
auto World::rite_update(std::uint32_t now_ms) -> void {
        if (level->id != d2d::rules::RiteQuest::kSummit) return;
        const auto object_index = [&](int object_id) { return int(std::ranges::find(level->npcs, object_id, &Npc::object_id) - level->npcs.begin()); };
        if (rite.returning && !statues_back_at) statues_back_at = now_ms + 2 * kTickMs;
        if (statues_back_at && now_ms >= statues_back_at) {
            statues_back_at = 0;
            const auto back = rite.statues_back();
            for (std::size_t which = 0; which < back.size(); ++which)
                if (const int statue = object_index(d2d::rules::RiteQuest::kStatues[which]); back[which] && statue < int(level->npcs.size())) {
                    doors[{ level, statue }] = Door{ 1, now_ms };
                    d2d::log::info("Rite of Passage: statue {} returns", which);
                }
        }
        for (const int object_id : d2d::rules::RiteQuest::kStatues)
            if (const int statue = object_index(object_id); statue < int(level->npcs.size()))
                if (const auto state = doors.find({ level, statue }); state != doors.end() && state->second.mode == 1
                    && now_ms - state->second.when >= std::uint32_t(level->npcs[std::size_t(statue)].op_frames) * kTickMs)
                    state->second = Door{ 2, now_ms };
        if (statues_at && now_ms >= statues_at) {
            statues_at = 0;
            for (int which = 0; which < 3; ++which) {
                const int statue = object_index(d2d::rules::RiteQuest::kStatues[std::size_t(which)]);
                if (statue >= int(level->npcs.size())) continue;
                if (const int super = rite.statue_wakes(which); super >= 0) {
                    doors[{ level, statue }] = Door{ 4, now_ms };
                    seal_boss(*level, super, subtile(level->npcs[std::size_t(statue)].x), subtile(level->npcs[std::size_t(statue)].y));
                }
            }
        }
        for (auto& monster : fight.monsters)
            if (std::ranges::any_of(d2d::rules::RiteQuest::kAncients, [&](int ancient) { return monster.type == int(game_data->mon_bin[std::size_t(ancient)]); }))
                monster.asleep = !rite.awake;
        if (!fight.dead()) { rite_died = false; return; }
        if (std::exchange(rite_died, true) || !rite.reset()) return;
        for (auto& monster : fight.monsters)
            if (std::ranges::any_of(d2d::rules::RiteQuest::kAncients, [&](int ancient) { return monster.type == int(game_data->mon_bin[std::size_t(ancient)]); })) monster.gone = true;
        for (const int object_id : { 474, 475, 476, d2d::rules::RiteQuest::kAltar })
            if (const int found = object_index(object_id); found < int(level->npcs.size())) doors[{ level, found }] = Door{ 0, now_ms };
        statues_at = statues_back_at = 0;
        d2d::log::info("Rite of Passage: the Ancients go back to sleep");
    }

// a5q6: the throne's waves (each its Subject's group 13 subtiles south of
// Baal), Baal off the throne (baalcrabstairs at his spot), his walk to the
// Chamber's portal, Tyrael (0x209, within 4 of 5 subtiles NW of where Baal
// fell: FUN_0058e920 -> FUN_005b2f20).
auto World::eve_update(std::uint32_t now_ms) -> void {
        const auto portal_at = std::ranges::find(level->npcs, d2d::rules::EveQuest::kChamberPortal, &Npc::object_id);
        std::vector<Monster> born;
        for (auto& monster : fight.monsters) {
            if (const int wave = std::exchange(monster.baal_wave, -1); wave >= 0) {
                seal_boss(*level, wave, subtile(monster.unit.x), subtile(monster.unit.y) + 13);
                d2d::log::info("Eve of Destruction: wave {}", wave - d2d::rules::EveQuest::kFirstSubject + 1);
            }
            if (std::exchange(monster.baal_leaves, false)) {
                born.push_back(make_monster(*game_data, int(game_data->mon_bin[std::size_t(d2d::rules::EveQuest::kBaalStairs)]), monster.unit.x, monster.unit.y, fight.rng,
                                            actor->character.header.active_difficulty(), true, int(players.size())));
                monster.gone = true;
                d2d::log::info("Eve of Destruction: Baal leaves his throne");
            }
            if (portal_at != level->npcs.end()) monster.chamber_portal = std::pair{ subtile(portal_at->x), subtile(portal_at->y) };
            if (std::exchange(monster.baal_in, false)) { eve.baal_gone_in(); d2d::log::info("Eve of Destruction: Baal goes into the Worldstone Chamber"); }
        }
        for (auto& monster : born) fight.add_monster(std::move(monster), level);
        if (tyrael_at && now_ms >= tyrael_at) {
            tyrael_at = 0;
            const Level* chamber = game_data->level(d2d::rules::EveQuest::kChamber);
            const auto& tyrael = game_data->mon_npc[std::size_t(game_data->mon_bin[std::size_t(d2d::rules::EveQuest::kTyrael)])];
            if (chamber && !tyrael.code.empty()) {
                auto npc = tyrael;
                std::tie(npc.x, npc.y) = chamber->nearest_free(baal_fell.first - 1.f, baal_fell.second - 1.f);
                const_cast<Level&>(*chamber).npcs.push_back(std::move(npc));   // GameData owns its levels mutable (gamedata.cpp)
                d2d::log::info("Eve of Destruction: Tyrael comes");
            }
        }
    }

// Frozen Anya (OperateFn 67, FUN_0058abc0): the potion taken, her thaw.
// The Summit's altar (65, FUN_0058d310): the Ancients speak, mode 1. Its
// stairs (66, FUN_0058d400): shut till the Rite's won here (or done); then
// mode 0 opens them, mode 2 leads to the Worldstone Keep (level 128, by its
// way back up).
// ponytail: the stairs open at once (mode 2), not after their event 1.
auto World::act5_operate(int npc_index, std::uint32_t now_ms) -> bool {
        const auto& object = level->npcs[std::size_t(npc_index)];
        if (object.object_id == d2d::rules::RiteQuest::kAltar) {
            if (hosted_quests) return true;
            if (rite.altar(quests())) {
                doors[{ level, npc_index }] = Door{ 1, now_ms };
                statues_at = now_ms + d2d::rules::RiteQuest::kWakeFrames * kTickMs;
                d2d::log::info("Rite of Passage: the Ancients speak");
            }
            return true;
        }
        // The Throne's portal (563, OperateFn 70, FUN_0058e6a0): with Baal gone
        // in, to the Chamber (FUN_0053aec0(0x84, 11): its town-portal tile);
        // elsewhere back to the Throne.
        if (object.object_id == d2d::rules::EveQuest::kChamberPortal) {
            const bool on_throne = level->id == d2d::rules::EveQuest::kThrone;
            if (on_throne && !eve.baal_in) return true;
            const Level* dest = game_data->level(on_throne ? d2d::rules::EveQuest::kChamber : d2d::rules::EveQuest::kThrone);
            if (!dest || dest->ds1.width() == 0) { d2d::log::warn("Eve of Destruction: level not built"); return true; }
            const auto spot = on_throne && dest->portal_spot.first >= 0 ? dest->portal_spot : dest->start.first >= 0 ? dest->start : std::pair{ float(dest->ds1.width()) / 2, float(dest->ds1.height()) / 2 };
            const auto [to_x, to_y] = dest->nearest_free(spot.first, spot.second);
            arrive(dest, to_x, to_y, "the Worldstone Chamber's portal");
            return true;
        }
        // The last portal (565, OperateFn 72, FUN_0058e740): with bit 13,
        // home to Harrogath (FUN_0053aec0(0x6d)), the game saved
        // (FUN_0052e2a0), then the client not busy (FUN_00535060): its
        // interact cleared, busy (+0x4c), cinematic 7 (D2X_OUT: S->C 0x61,
        // FUN_0053d940); bit 10.
        if (object.object_id == d2d::rules::EveQuest::kLastPortal) {
            if (!d2d::rules::quest::bit(quests(), d2d::rules::EveQuest::kQuest, 13)) return true;
            const Level* town = game_data->level(d2d::rules::level_ids::kHarrogath);
            if (town && town->start.first >= 0) arrive(town, town->start.first, town->start.second, "the last portal");
            save_due = true;
            movie = d2d::rules::EveQuest::kEndMovie;
            d2d::rules::quest::set(quests(), d2d::rules::EveQuest::kQuest, 10);
            return true;
        }
        // The Ancients' farewell (561, OperateFn 69, FUN_0058d5e0): the Rite
        // done, bit 4 not yet: their 20169 (heard: bit 4).
        if (object.object_id == d2d::rules::RiteQuest::kFarewell) {
            if (d2d::rules::quest::bit(quests(), d2d::rules::RiteQuest::kQuest, 0) && !d2d::rules::quest::bit(quests(), d2d::rules::RiteQuest::kQuest, 4))
                rite.said(quests(), -1, d2d::rules::RiteQuest::kAside, 0, 0);
            return true;
        }
        if (object.object_id == d2d::rules::RiteQuest::kStairs) {
            if (!rite.stairs_pass(quests())) { d2d::log::info("Rite of Passage: the stairs stay shut"); return true; }
            auto& door = doors[{ level, npc_index }];
            if (door.mode != 2) { door = Door{ 2, now_ms }; return true; }
            const Level* keep = game_data->level(128);
            if (!keep || keep->ds1.width() == 0) { d2d::log::warn("The Worldstone Keep didn't build"); return true; }
            const auto back = std::ranges::find(keep->warps, d2d::rules::RiteQuest::kSummit, &Level::Warp::destination);
            // As use_warp: at the way back up, then its ExitWalk on.
            arrive(keep, back != keep->warps.end() ? back->unit_x : float(keep->ds1.width()) / 2, back != keep->warps.end() ? back->unit_y : float(keep->ds1.height()) / 2, "the Summit's stairs");
            if (back != keep->warps.end()) {
                actor->target_x = actor->player.x + back->exit_x; actor->target_y = actor->player.y + back->exit_y;
                actor->player.walking = true;
            }
            return true;
        }
        if (object.object_id != d2d::rules::PrisonQuest::kFana) return false;
        if (hosted_quests) return true;
        if (prison.thaw(quests(), carries(d2d::rules::PrisonQuest::kPotionCode))) {
            take_carried(d2d::rules::PrisonQuest::kPotionCode);
            // LAB_00589aa0 (FUN_005537d0): the rest, bit 14.
            each_player([&](Player&) {
                auto& bits = quests();
                constexpr int quest = d2d::rules::PrisonQuest::kQuest;
                if (!d2d::rules::quest::bit(bits, quest, 0) && !d2d::rules::quest::bit(bits, quest, 1) && !d2d::rules::quest::bit(bits, quest, 13)) d2d::rules::quest::set(bits, quest, 14);
            });
            thaw_at = now_ms + kTickMs;
            d2d::log::info("Prison of Ice: the potion poured");
        } else d2d::log::info("Prison of Ice: no potion (20131)");
        return true;
    }

auto World::act5_update(std::uint32_t now_ms) -> void {
        if (hosted_quests) return;
        rescue_update(now_ms);
        prison_update(now_ms);
        rite_update(now_ms);
        eve_update(now_ms);
        if (betrayal_log_at && now_ms >= betrayal_log_at) { betrayal_log_at = 0; betrayal.log_timer(); }
        // A log moved with flag 1 (FUN_00544300): sent again (S->C 0x5d) to a
        // player it's for (act5_detail::told).
        for (auto [news, quest] : { std::pair{ &siege.news, d2d::rules::SiegeQuest::kQuest }, { &rescue.news, d2d::rules::RescueQuest::kQuest },
                                    { &prison.news, d2d::rules::PrisonQuest::kQuest }, { &betrayal.news, d2d::rules::BetrayalQuest::kQuest } })
            if (std::exchange(*news, false) && d2d::rules::act5_detail::told(quests(), quest)) actor->events.push_back(ev::QuestNews{ quest });
        if (std::exchange(eve.news, false) && d2d::rules::act5_detail::told(quests(), d2d::rules::EveQuest::kQuest)) actor->events.push_back(ev::QuestNews{ d2d::rules::EveQuest::kQuest });
        if (std::exchange(rite.news, false) && d2d::rules::act5_detail::told(quests(), d2d::rules::RiteQuest::kQuest)) actor->events.push_back(ev::QuestNews{ d2d::rules::RiteQuest::kQuest });
    }

}  // namespace d2d::game
