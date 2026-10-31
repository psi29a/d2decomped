// SPDX-License-Identifier: GPL-3.0-or-later
// devctl `debug` sub-verbs for the hero's stats, skills and items
// (docs/control_channel.md). In name order: a new one goes where it sorts.

#include "debug.hpp"

#include <d2s_items.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>

namespace d2d::client {

void DebugVerbs::add_hero() {
    add("clearinv", [this](const Args& /*args*/) -> Reply {   // empty the inventory grid (tests that need room)
        std::erase_if(town.hero.character.items, [](const auto& item) { return item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory; });
        return std::string("ok\n");
    });
    add("equip", [this](const Args& args) -> Reply {   // a normal <code> in body slot <n>, replacing what's there (tests)
        if (args.size() < 4 || !scene) return std::nullopt;
        auto& items = town.hero.character.items;
        const int slot = std::atoi(args[3].c_str());
        std::erase_if(items, [&](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == slot; });
        auto seed = town.world.rng;
        auto item = d2d::rules::generate_item(scene->rules, args[2], 1, 2, town.world.rng, &seed);
        item.location = d2d::d2s::item_location::kEquipped;
        item.slot = std::uint8_t(slot);
        items.push_back(item);
        return std::format("ok quantity={}\n", item.quantity);
    });
    add("give", [this](const Args& args) -> Reply {   // an item <code> [quality] at the player's feet, the area level's
        if (args.size() < 3) return std::nullopt;
        town.world.loot.put({ .code = args[2], .quality = args.size() >= 4 ? std::atoi(args[3].c_str()) : 2 }, town.hero.player.x, town.hero.player.y, 30,
                            town.world.fight.spawning.game, town.world.now);
        return std::format("ok #{}\n", town.world.loot.ground.back().id);
    });
    add("itemat", [this](const Args& args) -> Reply {   // where carried item <id> sits on screen with the inventory open: its box's middle
        if (args.size() < 3 || !scene) return std::nullopt;
        const auto found = std::ranges::find(town.hero.character.items, std::atoi(args[2].c_str()), &d2d::d2s::Item::id);
        if (found == town.hero.character.items.end()) return std::string("err no such item\n");
        const auto& layout = scene->inv_layout[std::size_t(std::clamp(town.hero.character.character_class, 0, 6))];
        std::array<int, 4> rect{};
        if (found->location == d2d::d2s::item_location::kStored && found->panel == d2d::d2s::item_panel::kInventory) {
            const auto info = scene->rules.item_info.find(found->code);
            const int width = info != scene->rules.item_info.end() ? info->second.width : 1, height = info != scene->rules.item_info.end() ? info->second.height : 1;
            rect = { layout.grid_x + found->column * layout.box_w, layout.grid_y + found->row * layout.box_h, width * layout.box_w, height * layout.box_h };
        } else if (found->location == d2d::d2s::item_location::kEquipped && found->slot >= d2d::d2s::body_location::kFirst && found->slot <= d2d::d2s::body_location::kLast) {
            rect = layout.slots[std::size_t(found->slot)];
        } else return std::string("err not in the inventory panel\n");
        return std::format("ok {} {}\n", rect[0] + rect[2] / 2, rect[1] + rect[3] / 2);
    });
    add("passives", [this](const Args& /*args*/) -> Reply {   // the passives' stats: stat=value[/itype]
        if (!scene) return std::nullopt;
        std::string out = "ok";
        for (const auto& passive : d2d::rules::passive_stats(scene->skills, town.fight.calc_env()))
            out += std::format(" {}={}{}{}", passive.stat, passive.value, passive.itype.empty() ? "" : "/", passive.itype);
        return out + "\n";
    });
    add("points", [this](const Args& args) -> Reply {   // give class skill <id> n points (tests)
        if (args.size() < 4 || !scene) return std::nullopt;
        const auto& ids = scene->skills.class_ids[std::size_t(std::max(town.hero.character.character_class, 0))];
        const auto found = std::ranges::find(ids, std::atoi(args[2].c_str()));
        if (found == ids.end()) return std::string("err not a class skill\n");
        town.hero.character.stats.skills[std::size_t(found - ids.begin())] = std::uint8_t(std::clamp(std::atoi(args[3].c_str()), 0, 99));
        return std::string("ok\n");
    });
    add("skill", [this](const Args& args) -> Reply {   // put skill <id> on the left / right button, if usable
        if (args.size() < 4) return std::nullopt;
        const int id = std::atoi(args[3].c_str());
        const bool left = args[2] == "left";
        if (!town.skillbar.usable(id, left)) return std::string("err not usable\n");
        town.skillbar.choose(left, { id, -1 });
        return std::string("ok\n");
    });
    const auto grant_points = [this](const Args& args) -> Reply {   // grant unspent points
        if (args.size() < 3) return std::nullopt;
        town.hero.character.stats.values[args[1] == "statpts" ? d2d::d2s::kStatPts : d2d::d2s::kSkillPts] = std::atoi(args[2].c_str());
        return std::string("ok\n");
    };
    add("skillpts", grant_points);
    add("stat", [this](const Args& args) -> Reply {   // set character stat <id 0..15> to <value>
        if (args.size() < 4) return std::nullopt;
        const int id = std::atoi(args[2].c_str());
        if (id < 0 || id > 15) return std::string("err stat 0..15\n");
        town.hero.character.stats.values[std::size_t(id)] = std::atoll(args[3].c_str());   // the World's character: the client's follows
        return std::string("ok\n");
    });
    add("statpts", grant_points);
    add("unbuilt", [this](const Args& /*args*/) -> Reply {   // class skills no path in Fight takes
        if (!scene) return std::nullopt;
        std::string out;
        for (const auto& skill : scene->skills.rows) {
            if (skill.cls.empty() || skill.id < 0) continue;
            auto& fight = town.fight;
            const bool built = skill_built(skill) || self_cast(skill) || fight.missile_skill(skill) || fight.spot_skill(skill) || fight.summon_skill(skill) || skill.aura
                            || (skill.srvstfunc == d2d::rules::ServerStartFunction::kNone && skill.srvdofunc == d2d::rules::ServerDoFunction::kNone && (skill.passive_stat[0] >= 0 || skill.passive));
            if (!built) out += std::format("{} {} st{} do{}\n", skill.id, skill.name, int(skill.srvstfunc), int(skill.srvdofunc));
        }
        return out + "ok\n";
    });
    add("unid", [this](const Args& /*args*/) -> Reply {   // unidentify every carried item
        for (auto& item : town.hero.character.items) if (item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory) item.identified = false;
        return "ok " + std::to_string(d2d::rules::unidentified(town.hero.character.items)) + "\n";
    });
    add("wear", [this](const Args& /*args*/) -> Reply {   // halve worn items' durability
        for (auto& item : town.hero.character.items) if (item.location == d2d::d2s::item_location::kEquipped) item.durability = d2d::rules::max_durability(item) / 2;
        return std::string("ok\n");
    });
}

}  // namespace d2d::client
