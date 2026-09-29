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
#include <monsters.hpp>
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
        view.pmode = fight.pmode;
        view.prate = fight.prate;
        view.seq.assign(fight.seq.begin(), fight.seq.end()); view.seq_frame_ms = fight.seq_frame_ms; view.seq_loop = fight.seq_loop;
        view.gfx = fight.gfx();
        if (merc && merc_npc) view.merc = View::Merc{ *merc, merc_npc, fight.merc_mode };
        for (const auto& pet : fight.pets) if (pet.where == level) view.pets.push_back({ pet.monster.npc, pet.monster.unit, pet.monster.mode });
        if (level == fight.mon_level) {
            // What's near the player (D2 sends the units of the rooms round
            // each player). ponytail: a radius of 28 cells, not rooms.
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
        for (int k = 0; k < 2; ++k)
            if (portal[std::size_t(k)].level == level)
                view.portals.push_back({ portal[std::size_t(k)].x, portal[std::size_t(k)].y, portal[std::size_t(1 - k)].level->id, portal[std::size_t(k)].born, k });
        view.npc_states = npc_states;
        if (now < fight.boost.until) view.boost = fight.boost.stats;
        view.aura = fight.aura;
        view.gold_lost = int(gold_lost);
        for (const auto& state : fight.self_states) view.buffs.push_back(state.skill);
        view.day = day;
        view.den_cleared = den.state >= 4;
        view.den_state = den.state; view.den_log = den.log; view.den_left = std::max(den_left, 0);
        {
            d2d::rules::StatSum sum{};
            (void)fight.player_fighter(nullptr, nullptr, nullptr, &sum);
            view.light_bonus = sum.size() > 89 ? int(sum[89]) : 0;
        }
        view.has_character = true;
        view.header = character.header; view.stats = character.stats; view.items = character.items; view.held = held;
        if (store.npc >= 0) view.store = store;
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

auto World::operate(int npc_index, std::uint32_t now_ms, int force ) -> void {   // force (devctl): the shrine row / trap to play
        using namespace d2d::d2s;
        const auto& object = level->npcs[std::size_t(npc_index)];
        const int diff = character.header.active_difficulty();
        if (object.operate_fn == 4 && object.locked) {           // a key from the inventory (FUN_0055f140: item type key)
            const auto key = std::ranges::find_if(character.items, [](const Item& item) { return item.location == 0 && item.panel == 1 && item.code == "key"; });
            if (key == character.items.end()) { d2d::log::info("I need a key."); return; }
            if (key->quantity > 1) --key->quantity;
            else character.items.erase(key);
        }
        operated[{ level, npc_index }] = now_ms;
        if (object.operate_fn == 4) {
            const auto& area_levels = game_data->area_level;
            auto alvl = [&](int id) { return std::size_t(id) < area_levels.size() ? area_levels[std::size_t(id)][std::size_t(std::clamp(diff, 0, 2))] : 1; };
            const auto [low, high] = d2d::rules::kChestLevels[0];
            const auto treasure_class = d2d::rules::chest_tc(0, diff, alvl(level->id), alvl(low), alvl(high));
            std::vector<d2d::rules::Drop> drops;
            const int rounds = d2d::rules::chest_rounds(object.locked, rng);
            for (int round = 0; round < rounds; ++round) d2d::rules::roll_drops(game_data->rules, treasure_class, alvl(level->id), rng, drops);
            for (const auto& dropped : drops) loot.put(dropped, object.x, object.y, alvl(level->id), now_ms);
            d2d::log::info("opened a chest: {} x{} ({} drops){}", treasure_class, rounds, drops.size(), object.trap ? std::format(", trap {}", object.trap) : "");
            if (const int trap = force >= 0 ? force : object.trap) spring_trap(trap, object.x, object.y, alvl(level->id), now_ms);
            return;
        }
        const int row = force >= 0 ? force : object.shrine;
        if (std::size_t(row) >= game_data->shrines.size()) return;
        const auto& shrine = game_data->shrines[std::size_t(row)];
        auto& stat_values = character.stats.values;
        d2d::rules::shrine_recharge(shrine, stat_values[kLife], stat_values[kMaxLife], stat_values[kMana], stat_values[kMaxMana]);
        if (auto boost = d2d::rules::shrine_boost(shrine, fight.player_combat.attack_rating); !boost.empty())
            fight.boost = { row, std::move(boost), now_ms + std::uint32_t(shrine.duration) * 40u };
        if (shrine.code == 17) open_portal_at(player.x + 1, player.y + 1, now_ms);   // portal (FUN_00582a30): 5 subtiles on each axis
        if (shrine.code == 18)                                  // gem: one up, or a chipped gem at the player's feet
            if (const auto code = d2d::rules::gem_shrine(game_data->rules, character.items, rng); !code.empty())
                loot.put({ .code = code }, player.x, player.y, 1, now_ms);
        // Storm (FUN_00582da0): everyone about loses Arg0 % of their life.
        // ponytail: "about" as within 30 cells (game.exe's unit search
        // over Arg1 isn't traced).
        if (shrine.code == 19) {
            stat_values[kLife] -= stat_values[kLife] * shrine.arg0 / 100;
            fight.merc_life -= fight.merc_life * shrine.arg0 / 100;
            if (fight.mon_level == level)
                for (auto& monster : fight.monsters)
                    if (monster.alive() && std::abs(monster.unit.x - object.x) < 30 && std::abs(monster.unit.y - object.y) < 30) monster.hit_points -= monster.hit_points * shrine.arg0 / 100;
        }
        // Exploding / Poison: Arg0 .. Arg1 - 1 exploding (opm) / choking gas
        // (gpm) potions at the player's feet (FUN_005830e0 / FUN_00583410).
        if (shrine.code == 21 || shrine.code == 22)
            for (int count = shrine.arg0 + rng(std::max(shrine.arg1 - shrine.arg0, 0)); count > 0; --count)
                loot.put({ .code = shrine.code == 21 ? "opm" : "gpm" }, player.x, player.y, 1, now_ms);
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
        d2d::log::info("shrine {} (code {}){}", row, shrine.code, shrine.code == 16 || shrine.code == 17 ? ", not built" : "");
    }

auto World::spring_trap(int trap, float x, float y, int alvl, std::uint32_t now_ms) -> void {
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
            const int count = int(rng.next() & 1) + 1;
            for (int k = 0; k < count; ++k) {
                const auto [free_x, free_y] = level->nearest_free(x + float(k) * 0.4f, y + 0.4f);
                auto monster = make_monster(*game_data, type, free_x, free_y, rng, diff);
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
            const float speed = cells_per_sec(vel), distance = std::max(std::hypot(dx, dy), 0.01f);
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
        }
        d2d::log::info("trap {}: {} at level {}", trap, name, lvl);
    }

auto World::enter(const Character& entering) -> void {
        character.character_class = entering.character_class; character.name = entering.name;
        character.appearance = entering.appearance; character.items = entering.items; character.stats = entering.stats; character.panel = entering.panel;
        character.expansion = entering.expansion; character.header = entering.header;
        // A corpse in the save (its corpse list, FUN_00533850) lies by the
        // camp's start.
        // ponytail: where game.exe puts it isn't traced; the save's x / y
        // aren't read.
        corpses.clear();
        if (!entering.corpse.empty() && game_data) {
            const auto& town = game_data->town;
            const auto [x, y] = town.nearest_free(town.start.first + 1.f, town.start.second + 1.f);
            corpses.push_back({ &town, x, y, 4, entering.corpse, 0, entering.appearance ? *entering.appearance : game_data->starting_gear[std::size_t(entering.header.cls % 7)] });
        }
        // A new game starts at full life, mana and stamina, whatever the
        // save held (D2's "save and exit to heal").
        for (const auto& [cur, max] : { std::pair{ d2d::d2s::kLife, d2d::d2s::kMaxLife }, { d2d::d2s::kMana, d2d::d2s::kMaxMana },
                                        { d2d::d2s::kStamina, d2d::d2s::kMaxStamina } })
            character.stats.values[cur] = character.stats.values[max];
        held.reset();
        store = {};
        level = &game_data->town;                     // a game starts in the camp (set_map_seed may have rebuilt it)
        npc_states = npc_start(*level);
        interact_npc = pick_item = take_warp = -1;
        wanted_near = nullptr;
        if (level->start.first >= 0) std::tie(player.x, player.y) = level->nearest_free(level->start.first, level->start.second);
        target_x = player.x; target_y = player.y;
        spawn_merc();
        new_game();
        for (std::size_t i = 0; i < level->npcs.size() && i < npc_states.size(); ++i)
            if (const int quest = level->npcs[i].quest)
                npc_states[i].hidden = !character.header.quest_flag(character.header.active_difficulty(), quest, 0);
    }

auto World::new_game() -> void {
        fight.new_game(character.header.active_difficulty());
        day = {};                                 // a new game starts at sunrise
        day_at = now;
        loot.ground.clear();
        loot.kept.clear();
        loot.ground_level = level;
        cues.due.clear();
        den = {};
        den.join(quests());
        den_left = -1;
        den_log_at = 0;
        operated.clear();
        fires.clear();
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
            merc = UnitState{ .x = player.x + 1, .y = player.y + 1 };
            std::tie(merc->x, merc->y) = level->nearest_free(merc->x, merc->y);
            merc_npc = &found->second.npc;
            fight.merc_joins();
        }
    }

auto World::respawn(std::uint32_t now_ms) -> void {
        if (level != &game_data->town) {
            events.push_back(ev::LevelChanged{ level, false });
            level = &game_data->town;
            npc_states = npc_start(*level);
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
            if (merc) shift(*merc);
            fight.pets_cross(level, neighbour.level, dx, dy);
            events.push_back(ev::LevelChanged{ level, true });
            level = neighbour.level;
            fight.enter(level);
            loot.enter(level);
            npc_states = npc_start(*level);
            interact_npc = -1;
            d2d::log::info("level: {} at ({:.1f}, {:.1f})", level_name(*level), player.x, player.y);
            return;
        }
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
        return level.id == 1 ? "Rogue Encampment" : level.id == 2 ? "Blood Moor" : level.id == 8 ? "Den of Evil" : "?";
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
        const auto back = std::ranges::find(destination->warps, level->id, &Level::Warp::destination);
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
            merc->path.clear();
            std::tie(merc->x, merc->y) = level->nearest_free(free_x + 1, free_y + 1);
        }
        fight.enter(level);
        fight.rooms_up(*level, player.x, player.y, true);
        loot.enter(level);
        npc_states = npc_start(*level);
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
        if (purse - lost > 0) loot.put({ .code = "gld", .gold = int(purse - lost) }, player.x, player.y, 1, now_ms);
        stat_values[kGold] = 0;
        gold_lost = lost;
        d2d::log::info("died: {} experience and {} gold lost; {} gold on the ground", exp_lost, lost, purse - lost);
    }

auto World::make_corpse() -> void {
        Corpse corpse{ level, player.x, player.y, player.dir, {}, exp_lost * 75 / 100, fight.gfx() };
        for (auto item_it = character.items.begin(); item_it != character.items.end();)
            if (item_it->location == 1) { corpse.items.push_back(std::move(*item_it)); item_it = character.items.erase(item_it); }
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
            const bool worn = std::ranges::any_of(character.items, [&](const Item& worn_item) { return worn_item.location == 1 && worn_item.slot == item_it->slot; });
            if (!worn && item_it->slot >= 1 && item_it->slot <= 12) { character.items.push_back(std::move(*item_it)); item_it = corpse.items.erase(item_it); continue; }
            std::optional<Item> held_item = *item_it;
            held_item->location = 0;
            std::vector<const Item*> inv;
            for (const auto& x : character.items) if (x.location == 0 && x.panel == 1) inv.push_back(&x);
            const auto [width, height] = d2d::rules::item_size(game_data->rules, held_item->code);
            if (const auto [x, y] = d2d::rules::free_spot(game_data->rules, inv, layout.cols, layout.rows, width, height); x >= 0
                && d2d::rules::put_in_grid(game_data->rules, character.items, held_item, 1, layout.cols, layout.rows, x, y)) { item_it = corpse.items.erase(item_it); continue; }
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
        const auto [x, y] = level->nearest_free(portal_x, portal_y);
        const auto [town_x, town_y] = town.nearest_free(town.portal_spot.first, town.portal_spot.second);
        portal = { Portal{ level, x, y, now_ms }, Portal{ &town, town_x, town_y, now_ms } };
        cues.cue("object_townportal", now_ms, x, y);
        d2d::log::info("town portal: {} ({:.1f}, {:.1f}) <-> camp ({:.1f}, {:.1f})", level_name(*level), x, y, town_x, town_y);
    }

auto World::use_portal(std::uint32_t now_ms) -> void {
        if (take_portal < 0 || !portal[0].level) return;
        const auto& taken = portal[std::size_t(take_portal)];
        if (taken.level != level || std::hypot(taken.x - player.x, taken.y - player.y) > 2.f) {
            if (!player.walking) take_portal = -1;
            return;
        }
        const auto& other_end = portal[std::size_t(1 - take_portal)];
        take_portal = -1;
        cues.cue("player_townportal_enter", now_ms, player.x, player.y);
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
            if (repair->item < 0) { d2d::rules::store_repair_all(tables, store, character.items, character.stats); return true; }
            const auto found = std::ranges::find(character.items, repair->item, &d2d::d2s::Item::id);
            if (found != character.items.end()) d2d::rules::store_repair(tables, store, *found, character.stats);
            return true;
        }
        if (std::holds_alternative<cmd::Identify>(command)) { d2d::rules::identify_all(character.items); return true; }
        if (const auto* hire = std::get_if<cmd::Hire>(&command)) {
            if (hire->offer >= 0 && std::size_t(hire->offer) < hire_offers.size() && d2d::rules::hire(hire_offers[std::size_t(hire->offer)], character.header, character.stats))
                spawn_merc();
            return true;
        }
        if (std::holds_alternative<cmd::CloseTrade>(command)) { store = {}; return true; }
        // Akara's reset (the 0x38 handler for her, hcIdx 0x94): while quest
        // 41 (the Den of Evil's reward) is open, stats and skills go back,
        // then it's used (FUN_0058fd50: bit 0 on, 1 off).
        if (const auto* respec = std::get_if<cmd::Respec>(&command)) {
            using d2d::rules::qbit;
            if (std::size_t(respec->npc) >= level->npcs.size() || level->npcs[std::size_t(respec->npc)].hc_idx != d2d::rules::DenQuest::kAkara
                || !qbit(quests(), 41, 1)) return true;
            const auto cls = std::size_t(std::clamp<int>(character.header.cls, 0, 6));
            const auto& start = game_data->class_start[cls];
            d2d::rules::respec(character.stats, { start.str, start.ene, start.dex, start.vit }, game_data->class_gains[cls]);
            d2d::rules::qset(quests(), 41, 0);
            d2d::rules::qset(quests(), 41, 1, false);
            d2d::log::info("Akara reset the stat and skill points");
            return true;
        }
        if (const auto* run = std::get_if<cmd::Run>(&command)) { running = run->running; return true; }
        if (const auto* chat = std::get_if<cmd::Chat>(&command)) { talking = { chat->npc, -1, -1 }; return true; }
        if (const auto* message = std::get_if<cmd::QuestMessage>(&command)) {   // only what that NPC has to say
            if (std::size_t(message->npc) >= level->npcs.size()) return true;
            const int hc_idx = level->npcs[std::size_t(message->npc)].hc_idx;
            if (!std::ranges::contains(den.talk(quests(), hc_idx), message->string, &d2d::rules::QuestMsg::string)) return true;
            if (den.said(quests(), hc_idx, message->string)) {
                ++character.stats.values[d2d::d2s::kSkillPts];
                d2d::log::info("Den of Evil: Akara's reward, a skill point");
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
            const auto* layout = to_grid->panel == 1 ? &game_data->inv_layout[std::size_t(cls)] : to_grid->panel == 4 ? &game_data->cube_layout
                          : to_grid->panel == 5 ? &game_data->stash_layout[character.expansion ? 1 : 0] : nullptr;
            if (held && layout) d2d::rules::put_in_grid(tables, character.items, held, to_grid->panel, layout->cols, layout->rows, to_grid->col, to_grid->row);
            return;
        }
        if (const auto* to_body = std::get_if<cmd::ToBody>(&command)) {
            if (held && to_body->slot >= 1 && to_body->slot <= 10) d2d::rules::equip(tables, character.items, held, to_body->slot, wearer(cls, character.items, character.stats));
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
            if (interact->npc <= -2000 && interact->npc > -2002) {                       // a town portal (the client names them -2000 - k)
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
            const bool usable = (npc.operate_fn == 2 || npc.operate_fn == 4) && !npc.preoperated && !operated.contains({ level, interact->npc });
            if (npc.operate_fn == 32 || npc.operate_fn == 23 || usable || (npc.root == "monsters" && menu)) interact_npc = interact->npc;
            return;
        }
        const auto& use_skill = std::get<cmd::UseSkill>(command);
        const auto* skill = game_data->skills.get(use_skill.skill);
        const int monster_index = fight.monster_index(use_skill.unit);
        const bool live = monster_index >= 0 && fight.monsters[std::size_t(monster_index)].alive();
        if (skill && self_cast(*skill)) {                                            // Holy Shield: where the player stands
            if (fight.cast(use_skill.skill, now_ms)) player.walking = false;
        } else if (skill && skill->srvdofunc == 76 && monster_index < 0) {                      // Whirlwind to that point (FUN_005d8f50)
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
        for (; now_ms - day_at >= kTickMs; day_at += kTickMs) day.step();
        fight.update_fighters(now_ms);
        // Used shrines and chests: OP while it plays, then ON; a shrine back
        // to NU after its reset time (Shrines.txt, minutes; 0 never).
        for (auto entry = operated.begin(); entry != operated.end();) {
            const auto& [key, when_ms] = *entry;
            const auto npc_index = std::size_t(key.second);
            const auto& npc = key.first->npcs[npc_index];
            const int reset = npc.operate_fn == 2 && std::size_t(npc.shrine) < game_data->shrines.size() ? game_data->shrines[std::size_t(npc.shrine)].reset : 0;
            const bool back = reset > 0 && now_ms - when_ms >= std::uint32_t(reset) * 60000u;
            if (key.first == level && npc_index < npc_states.size())
                npc_states[npc_index].mode = back ? std::string_view{} : now_ms - when_ms < std::uint32_t(npc.op_frames) * 40u ? "OP" : "ON";
            entry = back ? operated.erase(entry) : std::next(entry);
        }
        Crowd crowd;                           // who's in whose way this frame
        crowd.units.push_back(&player);
        if (merc) crowd.units.push_back(&*merc);
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            if (!level->npcs[i].path.empty() && !npc_states[i].hidden) crowd.units.push_back(&npc_states[i]);
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
                if (npc.operate_fn == 2 || npc.operate_fn == 4) {
                    operate(interact_npc, now_ms);
                } else if (npc.operate_fn == 32) {
                    events.push_back(ev::OpenUI{ ev::OpenUI::stash, interact_npc });
                } else if (npc.operate_fn == 23) {
                    // Touching it activates it (the town's: wp 0).
                    // ponytail: town only; a wilderness waypoint would need its level.
                    character.header.waypoints[std::size_t(character.header.active_difficulty())][0] |= 1;
                    events.push_back(ev::OpenUI{ ev::OpenUI::waypoint, interact_npc });
                } else {
                    if (d2d::rules::is_healer(npc.hc_idx)) d2d::rules::heal(character.stats);
                    // In Hell, a Den of Evil done before the reset existed
                    // opens it on meeting Akara (FUN_0058fd20: quest 41 bits
                    // 13 and 1).
                    if (npc.hc_idx == d2d::rules::DenQuest::kAkara && character.header.active_difficulty() == 2) {
                        auto& quest_bits = quests();
                        using d2d::rules::qbit;
                        if (qbit(quest_bits, 1, 0) && !qbit(quest_bits, 41, 1) && !qbit(quest_bits, 41, 0)) { d2d::rules::qset(quest_bits, 41, 13); d2d::rules::qset(quest_bits, 41, 1); }
                    }
                    events.push_back(ev::OpenUI{ ev::OpenUI::talk, interact_npc, den.talk(quests(), npc.hc_idx) });
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
                loot.take(ground_index);
                pick_item = -1; player.walking = false; player.path.clear();
            } else {
                target_x = ground_item.x; target_y = ground_item.y; player.walking = true;
            }
        }
        if (const auto target = fight.engage(now_ms)) { std::tie(target_x, target_y) = *target; player.walking = true; }
        if (player.walking && fight.pmode < 0) {
            // A route to the target, re-planned when the target
            // moves off its end (dragging, a walking NPC).
            if (player.path.empty() || std::hypot(player.goal_x - target_x, player.goal_y - target_y) > 0.3f) {
                player.path = walk_path(*level, player.x, player.y, target_x, target_y, crowd, &player);
                player.goal_x = target_x; player.goal_y = target_y;
            }
            const auto save_class = std::size_t(std::max(character.character_class, 0));
            // Faster run/walk: its effective % (150 x v / (150 + v)) on velocity.
            const float vel = float(running ? game_data->run_velocity[save_class] : game_data->walk_velocity[save_class])
                            * float(100 + d2d::rules::effective_speed(fight.player_combat.frw, 150)) / 100.f;
            player.walking = follow_path(*level, player, cells_per_sec(vel) * elapsed, crowd);
            if (!player.walking) player.path.clear();
        }
        npc_patrol(*level, npc_states, talking, now_ms, elapsed, crowd);
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            npc_states[i].alert = den.alert(quests(), level->npcs[i].hc_idx);
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
        cross_level();
        fight.rooms_up(*level, player.x, player.y, false);
        if (!fight.dead()) fight.apply_regen(now_ms, last_ms);
    }

}  // namespace d2d::game
