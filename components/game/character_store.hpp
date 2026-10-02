// SPDX-License-Identifier: GPL-3.0-or-later
// The CharacterStore (docs/design/multiplayer.md, rule 7): where the game
// server loads characters from and writes them to, kept apart from the live
// game. Locally, the .d2s files in the save directory (d2s_write.hpp).
// A save is written only when the new bytes parse back as the same
// character; into a temp file renamed over the old one; and the first time
// d2d writes over a save it didn't make, the original is kept as .d2s.bak.
#pragma once

#include "gamedata.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <d2s_write.hpp>
#include <rules.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace d2d::game {

struct CharacterStore {
    std::filesystem::path dir;
    const d2d::d2s::ItemTables* tables = nullptr;

    [[nodiscard]] std::filesystem::path path(const std::string& name) const { return dir / (name + ".d2s"); }

    // Writes the character; "" when it's done, else why not (nothing written).
    std::string save(const d2d::d2s::Header& header, const d2d::d2s::Stats& stats, const std::vector<d2d::d2s::Item>& items,
                     const std::vector<d2d::d2s::Item>* corpse = nullptr) const {
        namespace fs = std::filesystem;
        if (!tables) return "no item tables";
        if (header.name.empty() || header.name.find_first_of("/\\.") != std::string::npos) return "bad name";
        const auto file = path(header.name);
        std::vector<char> raw;
        if (std::ifstream file_in(file, std::ios::binary); file_in) raw.assign(std::istreambuf_iterator<char>(file_in), {});
        std::vector<std::byte> out;
        try {
            out = d2d::d2s::write_save(std::as_bytes(std::span(raw)), header, stats, items, *tables, corpse);
            const auto back = d2d::d2s::parse_header(out);
            const auto bst = d2d::d2s::parse_stats(out, *tables);
            const auto bitems = d2d::d2s::parse_items(out, *tables);
            if (back.name != header.name || bst.get(d2d::d2s::kLevel) != stats.get(d2d::d2s::kLevel) || bitems.size() != items.size())
                return "the written save didn't read back";
        } catch (const std::exception& error) {
            return std::string("write failed: ") + error.what();
        }
        std::error_code error;
        fs::create_directories(dir, error);
        if (const auto bak = fs::path(file).replace_extension(".d2s.bak"); !raw.empty() && !fs::exists(bak))
            fs::copy_file(file, bak, error);
        const auto tmp = fs::path(file).replace_extension(".d2s.tmp");
        {
            std::ofstream file_out(tmp, std::ios::binary | std::ios::trunc);
            file_out.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
            if (!file_out) return "couldn't write " + tmp.string();
        }
        fs::rename(tmp, file, error);
        return error ? "couldn't replace " + file.string() + ": " + error.message() : std::string{};
    }
};

// A character made in d2d (the char-create screen): CharStats.txt's start
// (stats, life = vit + hpadd, mana = int, stamina; the start skill at 1;
// its items: rarm / larm in the hands, potions in the belt, the rest in
// the inventory), level 1, normal difficulty, no hotkeys.
// ponytail: stacks (javelins) roll their quantity as a drop does.
struct NewCharacter { d2d::d2s::Header header; d2d::d2s::Stats stats; std::vector<d2d::d2s::Item> items; };
inline NewCharacter new_character(const GameData& game_data, int cls, const std::string& name, bool hardcore, bool expansion, d2d::rules::Rng& rng) {
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
    if (!start.start_skill.empty()) {
        const auto& ids = game_data.skills.class_ids[class_index];
        for (std::size_t i = 0; i < ids.size() && i < stats.skills.size(); ++i)
            if (const auto* skill = game_data.skills.get(ids[i]); skill) {
                auto lower = [](std::string text) { for (auto& letter : text) letter = char(std::tolower(static_cast<unsigned char>(letter))); return text; };
                if (lower(skill->name) == lower(start.start_skill)) stats.skills[i] = 1;
            }
    }
    int belt = 0, inv = 0;
    for (const auto& entry : start.items)
        for (int k = 0; k < std::max(entry.count, 1); ++k) {
            auto item = d2d::rules::generate_item(game_data.rules, entry.code, 1, 2, rng);
            if (entry.loc == "rarm" || entry.loc == "larm") { item.location = 1; item.slot = entry.loc == "rarm" ? 4 : 5; }
            else if (entry.code.starts_with("hp") || entry.code.starts_with("mp")) { item.location = 2; item.column = belt++; }
            else { item.location = 0; item.panel = 1; item.column = inv++; }
            made.items.push_back(std::move(item));
        }
    return made;
}

}  // namespace d2d::game
