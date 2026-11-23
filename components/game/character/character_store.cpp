// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for character_store.hpp.
#include "character_store.hpp"

#include "gamedata.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <item_gen.hpp>
#include <item_price.hpp>
#include <rules.hpp>
#include <wear.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace d2d::game {

NewCharacter new_character(const GameData& game_data, int cls, const std::string& name, bool hardcore, bool expansion, d2d::rules::Rng& rng) {
    using namespace d2d::d2s;
    NewCharacter made;
    const auto class_index = std::size_t(std::clamp(cls, 0, 6));
    const auto& start = game_data.class_start[class_index];
    auto& header = made.header;
    header.version = kMaxVersion;
    header.name = name;
    header.cls = std::uint8_t(class_index);
    header.level = 1;
    header.status = std::uint8_t((hardcore ? 0x04 : 0) | (expansion ? 0x20 : 0));
    header.hotkeys.fill(0xffff);
    header.set_look(game_data.starting_gear[class_index]);
    header.difficulty = { 0x80, 0, 0 };
    auto& stats = made.stats;
    stats.values[kStr] = start.str; stats.values[kDex] = start.dex; stats.values[kEne] = start.ene; stats.values[kVit] = start.vit;
    stats.values[kLife] = stats.values[kMaxLife] = std::int64_t(start.vit + start.hpadd) << 8;
    stats.values[kMana] = stats.values[kMaxMana] = std::int64_t(start.ene) << 8;
    stats.values[kStamina] = stats.values[kMaxStamina] = std::int64_t(start.stamina) << 8;
    stats.values[kLevel] = 1;
    int start_skill = -1;
    if (!start.start_skill.empty()) {
        const auto& ids = game_data.skills.class_ids[class_index];
        for (std::size_t i = 0; i < ids.size() && i < stats.skills.size(); ++i)
            if (const auto* skill = game_data.skills.get(ids[i]); skill) {
                auto lower = [](std::string text) { for (auto& letter : text) letter = char(std::tolower(static_cast<unsigned char>(letter))); return text; };
                if (lower(skill->name) == lower(start.start_skill)) { stats.skills[i] = 1; start_skill = ids[i]; }
            }
    }
    int belt = 0, inv = 0;
    for (std::size_t slot = 0; slot < start.items.size(); ++slot)
        for (int k = 0; k < std::max(start.items[slot].count, 1); ++k) {
            const auto& entry = start.items[slot];
            auto item = d2d::rules::generate_item(game_data.rules, entry.code, 1, 2, rng);
            if (slot == 0 && start_skill >= 0) item.props.push_back({ d2d::d2s::kSingleSkill, start_skill, 1 });   // FUN_00534c70: each of the first slot's
            if (entry.loc == "rarm" || entry.loc == "larm") { item.location = d2d::d2s::item_location::kEquipped; item.slot = entry.loc == "rarm" ? d2d::d2s::body_location::kRightArm : d2d::d2s::body_location::kLeftArm; }
            else if (entry.code.starts_with("hp") || entry.code.starts_with("mp")) { item.location = d2d::d2s::item_location::kBelt; item.column = belt++; }
            else { item.location = d2d::d2s::item_location::kStored; item.panel = d2d::d2s::item_panel::kInventory; item.column = inv++; }
            if (const auto base = game_data.rules.item_base.find(entry.code); base != game_data.rules.item_base.end() && base->second.stackable)
                item.quantity = std::min(base->second.max_stack, 511);                     // FUN_00534c70: maxstack (FUN_006295b0)
            if (const auto* info = d2d::rules::info_of(game_data.rules, item); info && d2d::rules::type_is(game_data.rules, info->type, "bowq"))
                item.quantity = entry.loc.empty() ? 100 : 250;                             // FUN_00534d60: ItemTypes row 5
            made.items.push_back(std::move(item));
        }
    return made;
}

}  // namespace d2d::game
