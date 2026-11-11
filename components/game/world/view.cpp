// SPDX-License-Identifier: GPL-3.0-or-later
// The World (world.hpp): what each client is sent, and the save. The rest is in this
// directory.
#include "world.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "internal.hpp"
#include "inventory.hpp"
#include "log.hpp"
#include "protocol.hpp"

#include <combat.hpp>
#include <d2s_items.hpp>
#include <d2s_write.hpp>
#include <light.hpp>
#include <merc.hpp>
#include <monsters.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::game {

auto World::view() const -> View {
        View view;
        view.level = level;
        view.player = actor->player;
        view.running = actor->running;
        view.dead = fight.dead();
        view.poisoned = std::ranges::any_of(actor->regen, [](const Fight::Regen& regen_entry) { return regen_entry.poison; });
        view.chilled = fight.chill_rate(now) != 0;
        view.stamina_shrine = now < actor->boost.until && shrine_code(actor->boost.shrine) == 14;   // 14: the stamina shrine (state 0x88)
        view.stamina_blue = now < actor->stamina_pot.until || view.stamina_shrine;
        // The globes' preview: the client's stat 0x4a / 0x1a, S->C 0x18's
        // percents as last sent (life_update_frame) plus the gear's regen,
        // while the healthpot / manapot state is on.
        const auto potion_on = [&](bool life) {
            return std::ranges::any_of(actor->regen, [&](const Fight::Regen& entry) { return !entry.poison && now < entry.until && (life ? entry.life > 0 : entry.mana > 0); });
        };
        if (potion_on(true)) view.life_preview = actor->life_sent.life_percent + actor->player_combat.life_regen;
        if (potion_on(false)) view.mana_preview = actor->life_sent.mana_percent + int(actor->psum[d2d::d2s::kManaRecovery]);
        view.curse = now < actor->curses[0].until ? actor->curses[0].skill : -1;
        view.flash_overlay = actor->flash_overlay; view.flash_at = actor->flash_at;
        view.stunned = now < actor->stunned;
        view.pmode = actor->pmode;
        view.prate = actor->prate;
        view.walk_percent = walk_percent(*actor, now);
        view.seq.assign(actor->seq.begin(), actor->seq.end()); view.seq_frame_ms = actor->seq_frame_ms; view.seq_loop = actor->seq_loop;
        view.gfx = fight.gfx();
        if (actor->merc && actor->merc_npc) {
            view.merc = View::Merc{ actor->merc->unit, actor->merc_npc, actor->merc->mode, 0, actor->merc->flash_overlay, actor->merc->flash_at };
            const auto fighter = fight.merc_fighter();
            view.merc->fhr = fighter.fhr;
            const auto gear = fight.merc_gear();
            const auto& stats = actor->merc_st;
            View::MercSheet sheet;
            sheet.level = stats.level;
            sheet.str = int(stats.str + gear.sum[std::size_t(d2d::d2s::kStr)]);
            sheet.dex = int(stats.dex + gear.sum[std::size_t(d2d::d2s::kDex)]);
            sheet.min = fighter.min; sheet.max = fighter.max; sheet.defense = fighter.defense; sheet.res = fighter.res;
            sheet.life = actor->merc->hit_points; sheet.max_life = fight.merc_max_life();
            sheet.exp = actor->character.header.merc_exp;
            if (const auto* row = d2d::rules::hireling_row(game_data->rules, int(actor->character.header.merc_type), 1)) {
                sheet.next = std::int64_t(stats.level + 2) * row->exp_per_level * (stats.level + 1) * (stats.level + 1);   // the merc_stats level rule's next step
                if (const auto* at_level = d2d::rules::hireling_row(game_data->rules, int(actor->character.header.merc_type), stats.level)) sheet.cls = at_level->cls;
            }
            view.merc_sheet = sheet;
        }
        for (const auto& pet : actor->pets)
            if (pet.where == level) {
                const auto* skill = game_data->skills.get(pet.skill);
                view.pets.push_back({ pet.monster.npc, pet.monster.unit, pet.monster.mode, skill ? std::string_view(skill->pettype) : std::string_view{},
                                      std::clamp(pet.monster.hit_points * 100 / std::max(pet.monster.stats.hit_points, 1), 0, 100),
                                      pet.monster.flash_overlay, pet.monster.flash_at });
            }
        for (const auto& player : players) {                  // the party's mercs and summons, as ours
            if (player.get() == actor || !here(*player)) continue;   // split levels: this level's party
            if (player->merc && player->merc_npc)                  // its life isn't shown (our own pets' portraits only)
                view.party_pets.push_back({ player->merc->npc, player->merc->unit, player->merc->mode, "hireable" });
            for (const auto& pet : player->pets)
                if (pet.where == level) {
                    const auto* skill = game_data->skills.get(pet.skill);
                    view.party_pets.push_back({ pet.monster.npc, pet.monster.unit, pet.monster.mode, skill ? std::string_view(skill->pettype) : std::string_view{},
                                                std::clamp(pet.monster.hit_points * 100 / std::max(pet.monster.stats.hit_points, 1), 0, 100) });
                }
        }
        // Run on draws RN only while they walk and have stamina (FUN_0057f090), as our own player.
        const auto running = [](const Player& player) { return player.running && player.player.walking && player.character.stats.values[d2d::d2s::kStamina] > 0; };
        for (const auto& player : players)                    // the others in the game, as their client draws them
            if (player.get() != actor && here(*player)) {
                const int cls = std::max(player->character.character_class, 0);
                std::vector<std::string_view> states;
                for (const auto& state : player->self_states)
                    if (const auto* skill = now < state.until ? game_data->skills.get(state.skill) : nullptr) states.push_back(skill->aurastate);
                view.others.push_back({ player->player, cls, player->character.name, std::uint32_t(player->id),
                                        player->character.appearance ? *player->character.appearance : game_data->starting_gear[std::size_t(cls)],
                                        player->ghost ? -1 : player->pmode, running(*player), std::move(states), player->ghost, walk_percent(*player, now),
                                        player->flash_overlay, player->flash_at });
            }
        for (const auto& player : players)
            if (player->body && player->body->level == level)
                view.bodies.push_back({ player->body->x, player->body->y, player->body->dir, player->body->gfx, player->id, std::max(player->character.character_class, 0),
                                        player->body->raised, player->body->raisers });
        view.ghost = actor->ghost;
        view.all_dead = shared_screen() && !anyone_alive();
        if (level == fight.mon_level) {
            // What's near the player (D2 sends the units of the rooms round
            // each player). ponytail: a radius of 28 cells, not rooms; waits for
            // networking (what a client is sent).
            for (const auto& monster : fight.monsters)
                if (!monster.gone && std::abs(monster.unit.x - actor->player.x) < 28 && std::abs(monster.unit.y - actor->player.y) < 28) {
                    view.monsters.push_back(monster);
                    view.monsters.back().npc.colour = monster_colour(monster);
                }
            for (const auto& missile : fight.missiles) if (missile.info) view.missiles.push_back({ missile.info, missile.x, missile.y, missile.dir, missile.born, missile.sound, missile.anim_rate });
        }
        if (actor->attack_mon >= 0 && std::size_t(actor->attack_mon) < fight.monsters.size()) view.attack = fight.monsters[std::size_t(actor->attack_mon)].id;
        view.attack_skill = actor->attack_skill;
        if (loot.ground_level == level) view.ground = loot.ground;
        for (const auto& fire : fires) if (fire.level == level) view.fires.push_back({ fire.x, fire.y, fire.npc, fire.born });
        for (std::size_t k = 0; k < actor->corpses.size(); ++k)
            if (actor->corpses[k].level == level) view.corpses.push_back({ actor->corpses[k].x, actor->corpses[k].y, actor->corpses[k].dir, actor->corpses[k].gfx, int(k) });
        for (int k = 0; k < int(portal.size()); ++k)
            if (portal[std::size_t(k)].level == level)
                view.portals.push_back({ portal[std::size_t(k)].x, portal[std::size_t(k)].y,
                                         k == 6 ? d2d::rules::SevenTombsQuest::kLair
                                                : portal[std::size_t(k ^ 1)].level ? portal[std::size_t(k ^ 1)].level->id : d2d::rules::CainQuest::kTristram,
                                         portal[std::size_t(k)].born, k });
        if (cain_portal.level == level) view.portals.push_back({ cain_portal.x, cain_portal.y, 1, cain_portal.born, int(kGamePortals), cain_portal_closes });
        for (const auto& gate : cage_portal)
            if (gate.level == level) view.portals.push_back({ gate.x, gate.y, 1, gate.born, int(kGamePortals) });
        const auto keys_of = [&](const Level& listed) {
            for (const auto& npc : listed.npcs) view.npc_keys.push_back({ listed.id, npc.hc_idx, npc.root == "objects" ? npc.object_id : 0, npc.x, npc.y, npc.palace, npc.quest });
        };
        view.npc_states = npc_states;
        grow_states(view.npc_states, *level);
        view.npc_states.resize(level->npcs.size());
        keys_of(*level);
        for (const auto& neighbour : level->nearby) {           // then the neighbours', Level::nearby order
            const auto found = other_npcs.find(neighbour.level);
            auto states = found != other_npcs.end() ? found->second : npc_start(*neighbour.level);
            grow_states(states, *neighbour.level);
            states.resize(neighbour.level->npcs.size());
            view.npc_states.insert(view.npc_states.end(), states.begin(), states.end());
            keys_of(*neighbour.level);
        }
        if (now < actor->boost.until) { view.boost = actor->boost.stats; view.boost_code = shrine_code(actor->boost.shrine); }
        view.aura = actor->aura;
        view.gold_lost = int(actor->gold_lost);
        for (const auto& state : actor->self_states) view.buffs.push_back(state.skill);
        view.day = day;
        view.den_cleared = den.state >= 4;
        view.diablo_red = terror.released && (lead().character.header.expansion() || !terror.dead);   // event 13 only in a classic game (LAB_005b5369)
        {   // FUN_00544190: a record's +0xe8 if it has one (Tools), else FUN_00543f90
            // ponytail: worked out as the view's built, not sent as it changes;
            // the send's skip for done quests (bits 0 / 15 without 13 / 14) reads as 0;
            // waits for networking (deltas).
            const auto& quest_bits = actor->character.header.quests[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))];
            act1_log(view.quest_log, quest_bits);
            act2_log(view.quest_log, quest_bits);
            act3_log(view.quest_log, quest_bits);
            act4_log(view.quest_log, quest_bits);
            act5_log(view.quest_log, quest_bits);
            for (int quest = 1; quest < int(view.quest_log.size()); ++quest) {
                const bool bit0 = d2d::rules::quest::bit(quest_bits, quest, 0) || d2d::rules::quest::bit(quest_bits, quest, 15);
                if (bit0 && !d2d::rules::quest::bit(quest_bits, quest, 13) && !d2d::rules::quest::bit(quest_bits, quest, 14)) view.quest_log[std::size_t(quest)] = 0;
            }
            for (std::size_t quest = 1; quest < view.game_quests.size(); ++quest)
                view.game_quests[quest] = std::uint16_t((game_quest_done(quest) ? 1 << 13 : 0)
                                                        | (closed_at_join[quest] ? 1 << 15 : 0));
        }
        view.den_left = std::max(den_left, 0);
        {
            d2d::rules::StatSum sum{};
            (void)fight.player_fighter(nullptr, nullptr, nullptr, &sum);
            view.light_bonus = sum.size() > d2d::d2s::kLightRadius ? int(sum[d2d::d2s::kLightRadius]) : 0;
            view.panel = actor->character.panel;
            view.stat_sum.clear();
            for (std::size_t stat = 0; stat < actor->psum.size(); ++stat) if (actor->psum[stat] != 0) view.stat_sum.emplace_back(int(stat), int(actor->psum[stat]));
            if (actor->store.npc >= 0) {
                view.store = actor->store;
                view.store->reduced = int(sum[d2d::d2s::kReducedPrices]);   // the player's gear: off what the store charges
            }
        }
        view.has_character = true;
        view.header = actor->character.header; view.stats = actor->character.stats; view.items = actor->character.items; view.held = actor->held;
        view.hire_offers = hire_offers(act());
        return view;
    }

auto World::save_header() -> d2d::d2s::Header {
        stow_held(*game_data, actor->character.items, actor->held);                  // the item in hand goes back first
        auto header = actor->character.header;
        if (header.right_skill == std::uint32_t(kResurrectSkill)) header.right_skill = actor->right_before;   // co-op's only: never in a save
        if (header.right_swap == std::uint32_t(kResurrectSkill)) header.right_swap = 0;
        header.level = std::uint8_t(std::clamp<std::int64_t>(actor->character.stats.get(d2d::d2s::kLevel), 1, 99));
        header.last_played = std::uint32_t(std::time(nullptr));
        header.map_id = game_data->map_seed;
        if (actor->character.appearance)   // the save's tints leave states out (FUN_0062c100's state loop sets no tint byte)
            header.set_look(game_data->item_pieces.empty() || game_data->comp.empty() ? *actor->character.appearance : game_data->look_of(actor->character.items));
        return header;
    }

// The corpse list holds one (PlrSave2.cpp): rules::corpse_to_save's, by its repair at Malah.
auto World::saved_corpse() -> const std::vector<d2d::d2s::Item>* {
        static const std::vector<d2d::d2s::Item> no_corpse;
        std::vector<const std::vector<d2d::d2s::Item>*> corpses;
        for (const auto& corpse : actor->corpses) corpses.push_back(&corpse.items);
        constexpr int kMalah = 0x201;
        const int kept = d2d::rules::corpse_to_save(game_data->rules, corpses, game_data->mon_npc[std::size_t(game_data->mon_bin[kMalah])].id, actor->character.header, reduced_prices());
        return kept < 0 ? &no_corpse : corpses[std::size_t(kept)];
    }

auto World::save_bytes(std::span<const std::byte> raw) -> std::expected<std::vector<std::byte>, std::string> {
        if (!game_data->item_tables) return std::unexpected("no item tables");
        // A save mid game (a joiner's every few minutes): the item in hand stays in hand.
        auto items = actor->character.items;
        auto held = actor->held;
        const auto header = save_header();
        std::swap(items, actor->character.items);
        actor->held = std::move(held);
        try {
            auto out = d2d::d2s::write_save(raw, header, fight.own_stats(), items, *game_data->item_tables,
                                            saved_corpse(), &actor->character.merc_items);
            actor->character.header.last_played = header.last_played;
            return out;
        } catch (const std::exception& error) {
            return std::unexpected(std::string("write failed: ") + error.what());
        }
    }

auto World::save() -> std::string {
        if (!characters) return "no character store";
        const auto header = save_header();
        const auto err = characters->save(header, fight.own_stats(), actor->character.items, saved_corpse(), &actor->character.merc_items);
        if (err.empty()) actor->character.header.last_played = header.last_played;
        d2d::log::info("save {}: {}", header.name, err.empty() ? "written" : err);
        return err;
    }

}  // namespace d2d::game
