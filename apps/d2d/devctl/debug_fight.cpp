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
        const auto& fighter = session.hero.player_combat;                         // as combat sees it this frame (self states included)
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
        for (auto& monster : session.world.fight.monsters)
            if (monster.alive() && monster.boss == d2d::rules::Boss::none
                && (!best || std::hypot(monster.unit.x - session.player.x, monster.unit.y - session.player.y) < std::hypot(best->unit.x - session.player.x, best->unit.y - session.player.y)))
                best = &monster;
        if (!best) return std::string("err no monster\n");
        make_boss(*scene, *best, d2d::rules::Boss::unique, mods, -1, session.world.rng(0x10000), session.hero.character.header.active_difficulty(), session.world.rng);
        return std::format("ok #{} {} aura={} lvl={}\n", best->id, best->npc.name, best->aura, best->aura_lvl);
    });
    add("charges", [this](const Args& args) -> Reply {   // hold n charges of charge-up skill id (tests)
        if (args.size() < 4) return std::nullopt;
        auto& fight = session.fight;
        const int id = std::atoi(args[2].c_str());
        std::erase_if(session.hero.charges, [&](const auto& charge) { return charge.skill == id; });
        session.hero.charges.push_back({ id, fight.skill_level ? fight.skill_level(id) : 1, std::clamp(std::atoi(args[3].c_str()), 1, 3), ~0u });
        return std::string("ok\n");
    });
    add("corpses", [this](const Args& /*args*/) -> Reply {   // the player's corpses: level, cell, items
        std::string out;
        for (const auto& corpse : session.hero.corpses) out += std::format("{}\t{:.1f}\t{:.1f}\t{}\n", corpse.level ? corpse.level->id : -1, corpse.x, corpse.y, corpse.items.size());
        return out + std::format("ok view={} gfx_tr={}\n", session.view.corpses.size(), int(session.view.gfx[1]));
    });
    add("die", [this](const Args& args) -> Reply {   // the player (or player <id>, local co-op) dies where they stand
        const int id = args.size() >= 3 ? std::atoi(args[2].c_str()) : session.hero.id;
        const auto found = std::ranges::find(session.world.players, id, [](const auto& player) { return player->id; });
        if (found == session.world.players.end()) return std::string("err no such player\n");
        session.world.act_as(**found);
        (*found)->character.stats.values[d2d::d2s::kLife] = 0;
        session.world.fight.set_pmode(kModeDT, session.world.now);
        session.world.act_as(session.world.lead());
        return std::string("ok\n");
    });
    add("difficulty", [this](const Args& args) -> Reply {   // play on difficulty d: a new game's monsters
        if (args.size() < 3) return std::nullopt;
        const int difficulty = std::clamp(std::atoi(args[2].c_str()), 0, 2);
        for (int i = 0; i < 3; ++i) session.hero.character.header.difficulty[std::size_t(i)] &= 0x7f;   // the World's character
        session.hero.character.header.difficulty[std::size_t(difficulty)] |= 0x80;
        // A new game on it, as a real one starts: the levels built again for
        // the difficulty (set_map_seed: LvlMaze Rooms(N) / (H)), the World
        // entered with its own character (items, stats) in the act's town.
        static_cast<Character&>(session.character) = session.hero.character;
        if (scene) set_map_seed(*scene, scene->map_seed, difficulty);
        session.enter();
        return std::string("ok\n");
    });
    add("kill", [this](const Args& args) -> Reply {   // kill the level's monsters but <n>, those of MonStats id <spare>, those over <cells> off (quests)
        if (!scene) return std::nullopt;
        int keep = args.size() >= 3 && args[2] != "spare" && args[2] != "within" ? std::atoi(args[2].c_str()) : 0;
        const auto spare_at = std::ranges::find(args, std::string("spare")), within_at = std::ranges::find(args, std::string("within"));
        if ((spare_at != args.end() && spare_at + 1 == args.end()) || (within_at != args.end() && within_at + 1 == args.end())) return std::string("err spare <MonStats id> / within <cells>\n");
        const std::string spare = spare_at != args.end() ? *(spare_at + 1) : "";
        const float within = within_at != args.end() ? float(std::atof((within_at + 1)->c_str())) : 1e9f;
        // In town the fight still holds the last level's monsters (Fight::enter keeps them): not this level's.
        if (session.world.fight.mon_level != session.level) return std::string("ok\n");
        auto& monsters = session.world.fight.monsters;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].alive() && (spare.empty() || scene->monsters.types[std::size_t(monsters[i].type)].id != spare)
                && std::hypot(monsters[i].unit.x - session.hero.player.x, monsters[i].unit.y - session.hero.player.y) <= within
                && keep-- <= 0 && hurt(*scene, monsters[i], monsters[i].hit_points, session.world.now)) session.world.fight.killed(i, session.world.now);
        return std::string("ok\n");
    });
    add("pets", [this](const Args& /*args*/) -> Reply {   // each pet: row level life dmg th ac res ranged/aura
        std::string out;
        for (const auto& pet : session.hero.pets)
            out += std::format("{} L{} hp={}/{} dmg={}-{} th={} ac={} res={},{},{},{} fire={}-{} ranged={}:{} aura={}:{} here={} at={:.1f},{:.1f} {} walk={} path={} goal={:.1f},{:.1f} target={} cast={} ctrl={},{},{} charges={} buffs={}\n",
                               pet.monster.npc.name, pet.monster.stats.level, pet.monster.hit_points, pet.monster.stats.hit_points, pet.monster.stats.a1_min, pet.monster.stats.a1_max, pet.monster.stats.to_hit, pet.monster.stats.armor_class,
                               pet.res[0], pet.res[1], pet.res[2], pet.res[3], pet.fire_lo, pet.fire_hi, pet.ranged, pet.ranged_level, pet.aura,
                               pet.aura_level, pet.where == session.level, pet.monster.unit.x, pet.monster.unit.y, pet.monster.mode,
                               pet.monster.unit.walking, pet.monster.unit.path.size(), pet.monster.unit.goal_x, pet.monster.unit.goal_y, pet.target, pet.cast,
                               pet.ctrl[0], pet.ctrl[1], pet.ctrl[2], [&] {
                                   std::string held;
                                   for (const auto& charge : pet.charges) held += std::format("{}{}:{}", held.empty() ? "" : ",", charge.skill, charge.count);
                                   if (held.empty()) held = "-";
                                   return held;
                               }(), [&] {
                                   std::string active;
                                   for (const auto& buff : pet.buffs) active += std::format("{}{}", active.empty() ? "" : ",", buff.skill);
                                   if (active.empty()) active = "-";
                                   return active;
                               }());
        return out + "ok\n";
    });
    add("quest", [this](const Args& args) -> Reply {   // mark Act quest <q> done (bit 0), active difficulty
        if (args.size() < 3) return std::nullopt;
        const int quest = std::atoi(args[2].c_str()), bit = quest * 16;
        if (quest < 0 || quest >= 48) return std::string("err quest 0..47\n");
        auto& header = session.hero.character.header;
        auto& quest_bits = session.world.quests();
        if (args.size() >= 4 && args[3] == "reset") {   // not started; the game's Den quest too
            quest_bits[std::size_t(bit >> 3)] = quest_bits[std::size_t(bit >> 3) + 1] = 0;
            session.world.den = {};
            session.world.den_left = -1;
        }
        if (args.size() >= 4)                          // show / reset: its 16 bits, the Den's state
            return std::format("ok bits={:#06x} den={} skillpts={} states={},{},{},{},{},{} radament={} log={} tainted={} log={} dark={} eclipse={} light={} arcane={} log={} summoner={} log={} portal={} tombs={} log={} true={} lair={} a5={},{},{},{},{} logs={},{},{},{},{}\n", quest_bits[std::size_t(bit >> 3)] | quest_bits[std::size_t(bit >> 3) + 1] << 8,
                               session.world.den.state, session.hero.character.stats.get(d2d::d2s::kSkillPts), session.world.den.state, session.world.burial.state,
                               session.world.tools.state, session.world.cain.state, session.world.tower.state, session.world.andy.state, session.world.radament.state, session.world.radament.log,
                               session.world.tainted.state, session.world.tainted.log, session.world.tainted.dark, session.world.day.eclipse, session.world.day.intensity(),
                               session.world.arcane.state, session.world.arcane.log, session.world.summoner.state, session.world.summoner.log, session.world.arcane.portal_made,
                               session.world.tombs.state, session.world.tombs.log, session.world.tombs.true_tomb, session.world.tombs.lair_open,
                               session.world.siege.state, session.world.rescue.state, session.world.prison.state, session.world.betrayal.state, session.world.rite.state,
                               session.world.siege.log, session.world.rescue.log, session.world.prison.log, session.world.betrayal.log, session.world.rite.log);
        header.quests[std::size_t(header.active_difficulty())][std::size_t(bit >> 3)] |= std::uint8_t(1 << (bit & 7));
        switch (quest) {                                // as a new game would see it (FUN_00546270): its join, then the chain from quest 1
        case 1: session.world.den.join(quest_bits); break;
        case 2: session.world.burial.join(quest_bits); break;
        case 3: session.world.tools.join(quest_bits); break;
        case 4: session.world.cain.join(quest_bits, false, false); break;
        case 5: session.world.tower.join(quest_bits); break;
        case 6: session.world.andy.join(quest_bits); break;
        default: break;
        }
        if (quest >= 1 && quest <= 6) session.world.chain(1);
        for (std::size_t i = 0; i < session.level->npcs.size() && i < session.npc_states.size(); ++i)
            if (const int gated_quest = session.level->npcs[i].quest)
                session.npc_states[i].hidden = !header.quest_flag(header.active_difficulty(), gated_quest, 0);
        return std::string("ok\n");
    });
    add("release", [this](const Args& /*args*/) -> Reply {   // release the charges on the nearest live monster
        auto& fight = session.fight;
        int best = -1; float best_distance = 1e9f;
        for (std::size_t i = 0; i < fight.monsters.size(); ++i)
            if (const float distance = std::hypot(fight.monsters[i].unit.x - session.player.x, fight.monsters[i].unit.y - session.player.y); fight.monsters[i].alive() && distance < best_distance) { best_distance = distance; best = int(i); }
        if (best < 0) return std::string("err no monster\n");
        fight.release(std::size_t(best), session.world_ms);   // the World's clock, as its own releases
        return std::string("ok ") + std::to_string(fight.missiles.size()) + "\n";
    });
    add("states", [this](const Args& /*args*/) -> Reply {   // the player's buffs; each cursed / cried monster
        std::string out = std::format("absorb={} maxlife={} maxmana={} def={} buffs=", session.hero.absorb_pool,
                                      character.stats.fixed(d2d::d2s::kMaxLife), character.stats.fixed(d2d::d2s::kMaxMana), session.hero.player_combat.defense);
        for (const auto& state : session.hero.self_states) out += std::format("{}:{},", state.skill, state.level);
        out += "\n";
        for (const auto& monster : session.fight.monsters)
            if (monster.curse.skill >= 0 || !monster.states.empty() || monster.stun_until > 0)
                out += std::format("{} hp={} curse={} cry={} dmg%={} speed%={} reflect%={} align={} stun={} ai={}{} afraid={}\n", monster.npc.name, monster.hit_points,
                                   monster.curse.skill, monster.states.empty() ? -1 : monster.states.front().skill, monster.dmg_pct, monster.speed_pct, monster.reflect_pct, monster.align,
                                   monster.stun_until, monster.special, monster.other ? "/other" : "", int(monster.afraid));
        return out + "ok\n";
    });
}

}  // namespace d2d::client
