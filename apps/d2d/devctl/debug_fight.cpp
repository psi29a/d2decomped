// SPDX-License-Identifier: GPL-3.0-or-later
// devctl `debug` sub-verbs for fights, monsters, pets, states and quests
// (docs/control_channel.md). In name order: a new one goes where it sorts.

#include "debug.hpp"

#include <d2s_items.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace d2d::client {

void DebugVerbs::add_fight() {
    add("attack", [this](const Args& /*args*/) -> Reply {   // the player's attack as combat sees it
        const auto& fighter = town.hero.player_combat;                         // as combat sees it this frame (self states included)
        return std::format("ok dmg={}-{} ar={} def={} block={} dr={}%+{} mdr={} res={}/{}/{}/{} cb={} ds={} ow={} "
                           "ll={} ml={} ias={} wsm={} frw={} fhr={} fbr={} thorns={}+{}l cold={}-{} fire={}-{} light={}-{} "
                           "crit={} def_melee={} def_missile={} dodge={} avoid={} evade={} ar%={} mcrit={} wblock={}\n",
                           fighter.min, fighter.max, fighter.attack_rating, fighter.defense, fighter.block, fighter.dr_pct, fighter.dr_flat, fighter.mdr, fighter.res[0], fighter.res[1], fighter.res[2],
                           fighter.res[3], fighter.crushing, fighter.deadly, fighter.open_wounds, fighter.life_steal, fighter.mana_steal, fighter.ias, fighter.wsm, fighter.frw,
                           fighter.fhr, fighter.fbr, fighter.thorns, fighter.thorns_light, fighter.elem[2].first, fighter.elem[2].second, fighter.elem[0].first,
                           fighter.elem[0].second, fighter.elem[1].first, fighter.elem[1].second, fighter.critical, fighter.def_melee, fighter.def_missile,
                           fighter.dodge, fighter.avoid, fighter.evade, fighter.ar_pct, fighter.mastery_crit, fighter.weapon_block);
    });
    add("boss", [this](const Args& args) -> Reply {   // the nearest plain monster: a unique with <mod>...
        if (args.size() < 3 || !scene) return std::nullopt;
        std::vector<int> mods;
        for (std::size_t k = 2; k < args.size(); ++k) mods.push_back(std::atoi(args[k].c_str()));
        Monster* best = nullptr;
        for (auto& monster : town.world.fight.monsters)
            if (monster.alive() && monster.boss == d2d::rules::Boss::none
                && (!best || std::hypot(monster.unit.x - town.player.x, monster.unit.y - town.player.y) < std::hypot(best->unit.x - town.player.x, best->unit.y - town.player.y)))
                best = &monster;
        if (!best) return std::string("err no monster\n");
        make_boss(*scene, *best, d2d::rules::Boss::unique, mods, -1, town.world.rng(0x10000), town.hero.character.header.active_difficulty(), town.world.rng);
        return std::format("ok #{} {} aura={} lvl={}\n", best->id, best->npc.name, best->aura, best->aura_lvl);
    });
    add("charges", [this](const Args& args) -> Reply {   // hold n charges of charge-up skill id (tests)
        if (args.size() < 4) return std::nullopt;
        auto& fight = town.fight;
        const int id = std::atoi(args[2].c_str());
        std::erase_if(town.hero.charges, [&](const auto& charge) { return charge.skill == id; });
        town.hero.charges.push_back({ id, fight.skill_level ? fight.skill_level(id) : 1, std::clamp(std::atoi(args[3].c_str()), 1, 3), ~0u });
        return std::string("ok\n");
    });
    add("corpses", [this](const Args& /*args*/) -> Reply {   // the player's corpses: level, cell, items
        std::string out;
        for (const auto& corpse : town.hero.corpses) out += std::format("{}\t{:.1f}\t{:.1f}\t{}\n", corpse.level ? corpse.level->id : -1, corpse.x, corpse.y, corpse.items.size());
        return out + std::format("ok view={} gfx_tr={}\n", town.view.corpses.size(), int(town.view.gfx[1]));
    });
    add("die", [this](const Args& args) -> Reply {   // the player (or player <id>, local co-op) dies where they stand
        const int id = args.size() >= 3 ? std::atoi(args[2].c_str()) : town.hero.id;
        const auto found = std::ranges::find(town.world.players, id, [](const auto& player) { return player->id; });
        if (found == town.world.players.end()) return std::string("err no such player\n");
        town.world.act_as(**found);
        (*found)->character.stats.values[d2d::d2s::kLife] = 0;
        town.world.fight.set_pmode(kModeDT, town.world.now);
        town.world.act_as(town.world.lead());
        return std::string("ok\n");
    });
    add("difficulty", [this](const Args& args) -> Reply {   // play on difficulty d: a new game's monsters
        if (args.size() < 3) return std::nullopt;
        const int difficulty = std::clamp(std::atoi(args[2].c_str()), 0, 2);
        for (int i = 0; i < 3; ++i) town.hero.character.header.difficulty[std::size_t(i)] &= 0x7f;   // the World's character
        town.hero.character.header.difficulty[std::size_t(difficulty)] |= 0x80;
        // A new game on it, as a real one starts: the levels built again for
        // the difficulty (set_map_seed: LvlMaze Rooms(N) / (H)), the World
        // entered with its own character (items, stats) in the act's town.
        static_cast<Character&>(town.character) = town.hero.character;
        if (scene) set_map_seed(*scene, scene->map_seed, difficulty);
        town.enter();
        return std::string("ok\n");
    });
    add("kill", [this](const Args& args) -> Reply {   // kill the level's monsters but <n>, those of MonStats id <spare>, those over <cells> off (quests)
        if (!scene) return std::nullopt;
        int keep = args.size() >= 3 && args[2] != "spare" && args[2] != "within" ? std::atoi(args[2].c_str()) : 0;
        const auto spare_at = std::ranges::find(args, std::string("spare")), within_at = std::ranges::find(args, std::string("within"));
        if ((spare_at != args.end() && spare_at + 1 == args.end()) || (within_at != args.end() && within_at + 1 == args.end())) return std::string("err spare <MonStats id> / within <cells>\n");
        const std::string spare = spare_at != args.end() ? *(spare_at + 1) : "";
        const float within = within_at != args.end() ? float(std::atof((within_at + 1)->c_str())) : 1e9f;
        auto& monsters = town.world.fight.monsters;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].alive() && (spare.empty() || scene->monsters.types[std::size_t(monsters[i].type)].id != spare)
                && std::hypot(monsters[i].unit.x - town.hero.player.x, monsters[i].unit.y - town.hero.player.y) <= within
                && keep-- <= 0 && hurt(*scene, monsters[i], monsters[i].hit_points, town.world.now)) town.world.fight.killed(i, town.world.now);
        return std::string("ok\n");
    });
    add("pets", [this](const Args& /*args*/) -> Reply {   // each pet: row level life dmg th ac res ranged/aura
        std::string out;
        for (const auto& pet : town.hero.pets)
            out += std::format("{} L{} hp={}/{} dmg={}-{} th={} ac={} res={},{},{},{} fire={}-{} ranged={}:{} aura={}:{} here={} at={:.1f},{:.1f} {} walk={} path={} goal={:.1f},{:.1f} target={} cast={} ctrl={},{},{} charges={} buffs={}\n",
                               pet.monster.npc.name, pet.monster.stats.level, pet.monster.hit_points, pet.monster.stats.hit_points, pet.monster.stats.a1_min, pet.monster.stats.a1_max, pet.monster.stats.to_hit, pet.monster.stats.armor_class,
                               pet.res[0], pet.res[1], pet.res[2], pet.res[3], pet.fire_lo, pet.fire_hi, pet.ranged, pet.ranged_level, pet.aura,
                               pet.aura_level, pet.where == town.level, pet.monster.unit.x, pet.monster.unit.y, pet.monster.mode,
                               pet.monster.unit.walking, pet.monster.unit.path.size(), pet.monster.unit.goal_x, pet.monster.unit.goal_y, pet.target, pet.cast,
                               pet.ctrl[0], pet.ctrl[1], pet.ctrl[2], [&] {
                                   std::string held;
                                   for (const auto& charge : pet.charges) held += std::format("{}{}:{}", held.empty() ? "" : ",", charge.skill, charge.count);
                                   return held.empty() ? std::string("-") : held;
                               }(), [&] {
                                   std::string active;
                                   for (const auto& buff : pet.buffs) active += std::format("{}{}", active.empty() ? "" : ",", buff.skill);
                                   return active.empty() ? std::string("-") : active;
                               }());
        return out + "ok\n";
    });
    add("quest", [this](const Args& args) -> Reply {   // mark Act quest <q> done (bit 0), active difficulty
        if (args.size() < 3) return std::nullopt;
        const int quest = std::atoi(args[2].c_str()), bit = quest * 16;
        if (quest < 0 || quest >= 48) return std::string("err quest 0..47\n");
        auto& header = town.hero.character.header;
        auto& quest_bits = town.world.quests();
        if (args.size() >= 4 && args[3] == "reset") {   // not started; the game's Den quest too
            quest_bits[std::size_t(bit >> 3)] = quest_bits[std::size_t(bit >> 3) + 1] = 0;
            town.world.den = {};
            town.world.den_left = -1;
        }
        if (args.size() >= 4)                          // show / reset: its 16 bits, the Den's state
            return std::format("ok bits={:#06x} den={} skillpts={} states={},{},{},{},{},{} radament={} log={} tainted={} log={} dark={} eclipse={} light={} arcane={} log={} summoner={} log={} portal={} tombs={} log={} true={} lair={} a5={},{},{},{},{} logs={},{},{},{},{}\n", quest_bits[std::size_t(bit >> 3)] | quest_bits[std::size_t(bit >> 3) + 1] << 8,
                               town.world.den.state, town.hero.character.stats.get(d2d::d2s::kSkillPts), town.world.den.state, town.world.burial.state,
                               town.world.tools.state, town.world.cain.state, town.world.tower.state, town.world.andy.state, town.world.radament.state, town.world.radament.log,
                               town.world.tainted.state, town.world.tainted.log, town.world.tainted.dark, town.world.day.eclipse, town.world.day.intensity(),
                               town.world.arcane.state, town.world.arcane.log, town.world.summoner.state, town.world.summoner.log, town.world.arcane.portal_made,
                               town.world.tombs.state, town.world.tombs.log, town.world.tombs.true_tomb, town.world.tombs.lair_open,
                               town.world.siege.state, town.world.rescue.state, town.world.prison.state, town.world.betrayal.state, town.world.rite.state,
                               town.world.siege.log, town.world.rescue.log, town.world.prison.log, town.world.betrayal.log, town.world.rite.log);
        header.quests[std::size_t(header.active_difficulty())][std::size_t(bit >> 3)] |= std::uint8_t(1 << (bit & 7));
        switch (quest) {                                // as a new game would see it (FUN_00546270): its join, then the chain from quest 1
        case 1: town.world.den.join(quest_bits); break;
        case 2: town.world.burial.join(quest_bits); break;
        case 3: town.world.tools.join(quest_bits); break;
        case 4: town.world.cain.join(quest_bits, false, false); break;
        case 5: town.world.tower.join(quest_bits); break;
        case 6: town.world.andy.join(quest_bits); break;
        default: break;
        }
        if (quest >= 1 && quest <= 6) town.world.chain(1);
        for (std::size_t i = 0; i < town.level->npcs.size() && i < town.npc_states.size(); ++i)
            if (const int gated_quest = town.level->npcs[i].quest)
                town.npc_states[i].hidden = !header.quest_flag(header.active_difficulty(), gated_quest, 0);
        return std::string("ok\n");
    });
    add("release", [this](const Args& /*args*/) -> Reply {   // release the charges on the nearest live monster
        auto& fight = town.fight;
        int best = -1; float best_distance = 1e9f;
        for (std::size_t i = 0; i < fight.monsters.size(); ++i)
            if (const float distance = std::hypot(fight.monsters[i].unit.x - town.player.x, fight.monsters[i].unit.y - town.player.y); fight.monsters[i].alive() && distance < best_distance) { best_distance = distance; best = int(i); }
        if (best < 0) return std::string("err no monster\n");
        fight.release(std::size_t(best), town.world_ms);   // the World's clock, as its own releases
        return std::string("ok ") + std::to_string(fight.missiles.size()) + "\n";
    });
    add("states", [this](const Args& /*args*/) -> Reply {   // the player's buffs; each cursed / cried monster
        std::string out = std::format("absorb={} maxlife={} maxmana={} def={} buffs=", town.hero.absorb_pool,
                                      character.stats.fixed(d2d::d2s::kMaxLife), character.stats.fixed(d2d::d2s::kMaxMana), town.hero.player_combat.defense);
        for (const auto& state : town.hero.self_states) out += std::format("{}:{},", state.skill, state.level);
        out += "\n";
        for (const auto& monster : town.fight.monsters)
            if (monster.curse.skill >= 0 || monster.cry.skill >= 0 || monster.stun_until > 0)
                out += std::format("{} hp={} curse={} cry={} dmg%={} speed%={} reflect%={} align={} stun={} ai={}{} afraid={}\n", monster.npc.name, monster.hit_points,
                                   monster.curse.skill, monster.cry.skill, monster.dmg_pct, monster.speed_pct, monster.reflect_pct, monster.align,
                                   monster.stun_until, monster.special, monster.other ? "/other" : "", int(monster.afraid));
        return out + "ok\n";
    });
}

}  // namespace d2d::client
