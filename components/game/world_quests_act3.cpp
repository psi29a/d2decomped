// SPDX-License-Identifier: GPL-3.0-or-later
// Act 3's quests at the World's quest hooks (world.hpp: World::actN_*).
// A new act adds its own file like this one and a call at each hook.
#include "world.hpp"
#include "ai.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "log.hpp"
#include <d2s_items.hpp>
#include <monster_ids.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <uniques.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <utility>
#include <vector>

namespace d2d::game {

auto World::act3_log(d2d::rules::QuestLogStates& log, const d2d::rules::QuestBits& bits) const -> void {
        log[d2d::rules::GoldenBirdQuest::kQuest] = std::uint8_t(golden_bird.log_state(bits, carries(d2d::rules::GoldenBirdQuest::kFigurine), carries(d2d::rules::GoldenBirdQuest::kBird), !fight.remote_monsters));
        log[d2d::rules::BladeQuest::kQuest] = std::uint8_t(blade.log_state(bits, carries(d2d::rules::BladeQuest::kGidbinn)));
        log[d2d::rules::KhalimQuest::kQuest] = std::uint8_t(khalim.log_state(bits, khalim_parts(), temple.orb_broken));
        log[d2d::rules::BlackenedTempleQuest::kQuest] = std::uint8_t(temple.log_state(bits));
        log[d2d::rules::GuardianQuest::kQuest] = std::uint8_t(guardian.log_state(bits));
        log[d2d::rules::LamEsenQuest::kQuest] = std::uint8_t(lam_esen.log_state(bits, carries("bbb"), !fight.remote_monsters, d2d::rules::level_act(level->id) == 2));
    }

auto World::act3_join() -> void {
        fight.spawning.orb_smashed = d2d::rules::quest::bit(quests(), 0x12, 0);   // a3q5's join (FUN_005bb480): Khalim's Will done
        d2d::rules::HratliGreeting::join(quests(), game_bits);
        lam_esen = {};
        lam_esen.join(quests(), game_bits, carries(d2d::rules::LamEsenQuest::kTome));
        khalim = {};
        khalim.join(quests(), khalim_parts());
        temple = {};
        temple.join(quests(), game_bits);
        guardian = {};
        guardian.join(quests(), game_bits);
        bridge_check = 0;
        blade = {};
        blade.join(quests(), carries(d2d::rules::BladeQuest::kGidbinn));
        actor->had_gidbinn = carries(d2d::rules::BladeQuest::kGidbinn); gidbinn_timer = 0; gidbinn_altar = -1;
        golden_bird = {};
        golden_bird.join(quests(), carries(d2d::rules::GoldenBirdQuest::kBird), carries(d2d::rules::GoldenBirdQuest::kFigurine));
        actor->had_figurine = carries(d2d::rules::GoldenBirdQuest::kFigurine); actor->had_bird = carries(d2d::rules::GoldenBirdQuest::kBird);
    }

auto World::act3_enter(const Level& from) -> void {
        lam_esen.enter(quests(), from.id, level->id);
        guardian.enter(quests(), from.id, level->id, d2d::rules::quest::bit(game_bits, d2d::rules::GuardianQuest::kQuest, 13));
        khalim.enter(level->id);
        temple.enter(quests(), from.id, level->id, d2d::rules::quest::bit(game_bits, d2d::rules::BlackenedTempleQuest::kQuest, 13));
        blade.enter(quests(), from.id, level->id, carries(d2d::rules::BladeQuest::kGidbinn));
    }

auto World::act3_talk(std::vector<d2d::rules::QuestMsg>& out, int hc_idx) -> void {
        std::ranges::copy(d2d::rules::HratliGreeting::talk(quests(), hc_idx, actor->character.header.cls), std::back_inserter(out));
        std::ranges::copy(lam_esen.talk(quests(), hc_idx, carries(d2d::rules::LamEsenQuest::kTome)), std::back_inserter(out));
        std::ranges::copy(khalim.talk(quests(), hc_idx, khalim_parts(), !temple.orb_broken), std::back_inserter(out));
        std::ranges::copy(temple.talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(guardian.talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(blade.talk(quests(), hc_idx, carries(d2d::rules::BladeQuest::kGidbinn)), std::back_inserter(out));
        std::ranges::copy(golden_bird.talk(quests(), hc_idx, carries(d2d::rules::GoldenBirdQuest::kFigurine), carries(d2d::rules::GoldenBirdQuest::kBird)), std::back_inserter(out));
    }

auto World::act3_talk_closed(int hc_idx) -> void {
        blade.talk_closed(quests(), hc_idx, carries(d2d::rules::BladeQuest::kGidbinn));
        khalim.talk_closed(hc_idx);
    }

auto World::act3_said(int hc_idx, int message_string) -> void {
        d2d::rules::HratliGreeting::said(quests(), game_bits, hc_idx, message_string);
        // Lam Esen's Tome handed to Alkor (FUN_005b7740): the tome goes (FUN_00544160), 5 stat points.
        if (const auto heard = lam_esen.said(quests(), game_bits, hc_idx, message_string, carries(d2d::rules::LamEsenQuest::kTome), d2d::rules::level_act(level->id) == 2); heard.takes_tome || heard.stat_points) {
            if (heard.stat_points) class_cue("act3_complete_lamesen", now);
            if (heard.takes_tome) take_carried(d2d::rules::LamEsenQuest::kTome);
            actor->character.stats.values[d2d::d2s::kStatPts] += heard.stat_points;
            d2d::log::info("Lam Esen's Tome: handed to Alkor, +{} stat points", heard.stat_points);
        }
        // The Golden Bird (FUN_005ba320): the figurine for the bird at Meshif's, the bird to Alkor, his potion.
        using Bird = d2d::rules::GoldenBirdQuest;
        const auto bird = golden_bird.said(quests(), game_bits, hc_idx, message_string, carries(Bird::kFigurine), carries(Bird::kBird));
        if (bird.takes_figurine) take_carried(Bird::kFigurine);
        if (bird.gives_bird) { give_item(Bird::kBird, now); actor->had_bird = true; d2d::log::info("The Golden Bird: Meshif's golden bird"); }
        if (bird.takes_bird) take_carried(Bird::kBird);
        if (bird.gives_potion) { give_item(Bird::kPotion, now); d2d::log::info("The Golden Bird: Alkor's Potion of Life"); }
        // Khalim's Will (FUN_005b8060): Cain's bits; the Blackened Temple
        // (FUN_005bb210): Cain's done, Khalim's parts gone once his Will's done.
        khalim.said(quests(), hc_idx, message_string);
        guardian.said(quests(), hc_idx, message_string, d2d::rules::quest::bit(game_bits, d2d::rules::GuardianQuest::kQuest, 13));
        if (temple.said(quests(), game_bits, hc_idx, message_string, d2d::rules::quest::bit(game_bits, d2d::rules::BlackenedTempleQuest::kQuest, 13))) {
            using K = d2d::rules::KhalimQuest;
            for (const char* part : { K::kEyeCode, K::kHeartCode, K::kBrainCode, K::kFlailCode, K::kWillCode }) while (carries(part)) take_carried(part);
            d2d::log::info("The Blackened Temple: done, Khalim's parts taken");
        }
        // The Blade of the Old Religion (FUN_005b9240): Ormus takes the Gidbinn, his rare ring; Asheara's free mercenary (FUN_00579180).
        using Blade = d2d::rules::BladeQuest;
        const auto blade_heard = blade.said(quests(), game_bits, hc_idx, message_string, carries(Blade::kGidbinn));
        if (blade_heard.takes_gidbinn) {
            class_cue("act3_complete_blade", now);   // FUN_005b9520: unit event 0x41
            take_carried(Blade::kGidbinn);
            d2d::log::info("The Blade of the Old Religion: the Gidbinn to Ormus");
        }
        if (blade_heard.ring) {
            give_item(Blade::kRing, now, Blade::kRingLevel[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))], 6);
            d2d::log::info("The Blade of the Old Religion: Ormus's ring");
        }
        if (blade_heard.merc) {
            // FUN_00579180: the first one for hire, free, the list rolled anew.
            auto offers = hire_offers(act());
            if (!offers.empty()) {
                auto free_offer = offers.front();
                free_offer.cost = 0;
                d2d::rules::hire(free_offer, actor->character.header, actor->character.stats);
                for (auto& entry : hire_lists[std::size_t(act())]) if (d2d::rules::for_hire(entry)) { entry.hired = true; break; }
                hire_list(act());
                spawn_merc();
                d2d::log::info("The Blade of the Old Religion: Asheara's free mercenary");
            }
        }
    }

auto World::act3_alert(int hc_idx) -> bool {
        return d2d::rules::HratliGreeting::alert(quests(), hc_idx)
            || lam_esen.alert(quests(), hc_idx, carries(d2d::rules::LamEsenQuest::kTome))
            || khalim.alert(quests(), hc_idx, khalim_parts(), !temple.orb_broken)
            || temple.alert(quests(), hc_idx)
            || guardian.alert(quests(), hc_idx)
            || blade.alert(quests(), hc_idx, carries(d2d::rules::BladeQuest::kGidbinn))
            || d2d::rules::GoldenBirdQuest::alert(quests(), hc_idx, carries(d2d::rules::GoldenBirdQuest::kFigurine), carries(d2d::rules::GoldenBirdQuest::kBird));
    }

auto World::act3_kill(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        if (kill.type == d2d::rules::monster_ids::kMephisto && !hosted_quests) mephisto_died(kill, now_ms);
        if (!hosted_quests && std::ranges::contains(d2d::rules::BlackenedTempleQuest::kCouncil, kill.super)) {   // FUN_005bbc00
            using K = d2d::rules::KhalimQuest;
            const auto drops = temple.council_died(quests(), game_bits, d2d::rules::level_act(level->id) == 2, d2d::rules::quest::bit(quests(), K::kQuest, 0),
                                                   carries(K::kFlailCode) || carries(K::kWillCode), carries(d2d::rules::BlackenedTempleQuest::kCube));
            if (drops.flail) loot.put({ .code = K::kFlailCode }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
            if (drops.cube) loot.put({ .code = d2d::rules::BlackenedTempleQuest::kCube }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
            if (drops.last) {
                class_cue("act3_complete_blackened", now_ms);   // unit event 0x40
                d2d::log::info("The Blackened Temple: the Council is dead");
            }
        }
        if (!hosted_quests && blade.killed(kill.id)) {   // FUN_005b9980: the guardian drops the Gidbinn
            loot.put({ .code = d2d::rules::BladeQuest::kGidbinn }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
            blade.mark(quests(), carries(d2d::rules::BladeQuest::kGidbinn));
            d2d::log::info("The Blade of the Old Religion: the Gidbinn drops");
        }
        if (!hosted_quests && golden_bird.killed(quests(), kill.id)) {   // FUN_005bab60: its carrier drops the jade figurine
            loot.put({ .code = d2d::rules::GoldenBirdQuest::kFigurine }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
            d2d::log::info("The Golden Bird: the jade figurine drops on level {}", level->id);
        }
    }

auto World::act3_bind_new_bosses() -> void {
        auto made = std::exchange(fight.made_bosses, {});
        if (hosted_quests) return;
        for (const auto& boss : made) {
            if (!golden_bird.armed) break;
            if (d2d::rules::level_act(boss.level) == 2 && boss.type != 0x197 && !game_data->mon_is_npc[std::size_t(boss.type)])
                golden_bird.made(boss.id);
        }
    }

auto World::act3_update(std::uint32_t now_ms) -> void {
        if (d2d::rules::quest::bit(quests(), d2d::rules::BlackenedTempleQuest::kQuest, 0)) guardian.chain();   // a3q5's +0xf0
        mephisto_bridge(now_ms);
        // FUN_005a09e0 calls FUN_00544e80 when a qualifying boss is made,
        // including one in a nearby room before the player enters that level.
        act3_bind_new_bosses();
        if (const bool figurine = carries(d2d::rules::GoldenBirdQuest::kFigurine), bird = carries(d2d::rules::GoldenBirdQuest::kBird); !hosted_quests) {
            if (figurine && !actor->had_figurine && golden_bird.picked_up(quests(), false)) class_cue("act3_find_jade", now_ms);   // unit event 0x48
            if (bird && !actor->had_bird) golden_bird.picked_up(quests(), true);
            actor->had_figurine = figurine; actor->had_bird = bird;
        }
        // a3q3's timer (FUN_005b9a30): with the player by the altar, a unique
        // fetish (fetish11) and its pack come (FUN_005b9930 -> FUN_005a43e0).
        // ponytail: "in its room" is within 20 cells; it waits for the player.
        if (gidbinn_timer && now_ms >= gidbinn_timer && !blade.spawned && gidbinn_altar >= 0 && std::size_t(gidbinn_altar) < level->npcs.size()) {
            const auto& altar = level->npcs[std::size_t(gidbinn_altar)];
            if (std::hypot(actor->player.x - altar.x, actor->player.y - altar.y) < 20 && d2d::rules::BladeQuest::kGuardian < int(game_data->monsters.types.size())) {
                const int diff = actor->character.header.active_difficulty();
                auto fetish_lord = make_monster(*game_data, d2d::rules::BladeQuest::kGuardian, altar.x + 1, altar.y + 1, fight.rng, diff, true, int(players.size()));
                make_boss(*game_data, fetish_lord, d2d::rules::Boss::unique, {}, -1, int(fight.rng.next() & 0xffff), diff, fight.rng, int(players.size()));
                const int minion = game_data->monsters.types[std::size_t(d2d::rules::BladeQuest::kGuardian)].minion[0];
                const int party = 2 + int(fight.rng.next() % 6);
                fight.add_monster(std::move(fetish_lord));
                blade.spawned = true; blade.guardian = fight.monsters.back().id;
                act3_bind_new_bosses();
                for (int i = 0; minion >= 0 && i < party; ++i) {
                    auto fetish = make_monster(*game_data, minion, altar.x + float(i % 3) - 1, altar.y + float(i / 3), fight.rng, diff, true, int(players.size()));
                    fetish.boss = d2d::rules::Boss::minion;
                    fight.add_monster(std::move(fetish));
                }
                gidbinn_timer = 0;
                d2d::log::info("The Blade of the Old Religion: the guardians come");
            }
        }
        if (const bool gidbinn = carries(d2d::rules::BladeQuest::kGidbinn); !hosted_quests) {
            if (gidbinn && !actor->had_gidbinn && blade.picked_up(quests())) d2d::log::info("The Blade of the Old Religion: the Gidbinn (FUN_00553380)");
            actor->had_gidbinn = gidbinn;
        }
        // The tome picked up / dropped (a3q1's +0xb0 / +0xb4, FUN_005b7970 / FUN_005b7a00).
        // ponytail: polled from what's carried, not the item events.
        if (const bool tome = carries(d2d::rules::LamEsenQuest::kTome); !hosted_quests && tome && lam_esen.state == 3) lam_esen.picked_up();
        else if (!hosted_quests && !tome && lam_esen.state == 4) lam_esen.dropped();
    }

}  // namespace d2d::game
