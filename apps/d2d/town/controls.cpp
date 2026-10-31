// SPDX-License-Identifier: GPL-3.0-or-later
// Town (town.hpp): keys, hotkeys, speech, the party rows and help. The rest is in this
// directory.
#include "town.hpp"

#include "common.hpp"
#include "controls.hpp"
#include "gamepad_map.hpp"
#include "ingame.hpp"
#include "keyconfig.hpp"
#include "panels.hpp"
#include "party.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <cues.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <log.hpp>
#include <userdir.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::client {

// The character's key config (FUN_0046aae0): its <name>.key when that's
// whole, else the save folder's default.key, else the built-in table;
// then kept as <name>.key.
auto Town::load_keys() -> void {
        const auto read = [](const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary);
            return std::vector<std::uint8_t>{ std::istreambuf_iterator<char>(file), {} };
        };
        key_table = keys::defaults();
        if (!key_dir.empty()) {
            if (const auto own = keys::read_character(read(key_dir / (character.name + ".key")))) { key_table = *own; }
            else {
                if (const auto shared = keys::read_default(read(key_dir / "default.key")); shared && keys::valid(*shared)) key_table = *shared;
                save_keys(false);
            }
        }
        update_hotkey_names();
    }

// <name>.key, and with `shared` the save folder's default.key (Accept,
// FUN_00469780).
auto Town::save_keys(bool shared) const -> void {
        if (key_dir.empty()) return;
        const auto write = [](const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!file) d2d::log::warn("{}: not written", path.string());
        };
        write(key_dir / (character.name + ".key"), keys::write_character(key_table));
        if (shared) write(key_dir / "default.key", keys::write_default(key_table));
    }

auto Town::update_hotkey_names() -> void {
        for (int slot = 0; slot < 16; ++slot) {
            const int action = slot < 8 ? keys::kSkill1 + slot : keys::kSkill9 + slot - 8;
            auto key = keys::key_of(key_table, action, 1);
            if (key == keys::kNone) key = keys::key_of(key_table, action, 0);
            const auto id = keys::name_id(key);
            skillbar.hotkey_names[std::size_t(slot)] = key == keys::kNone ? std::string{} : id ? string_id(*scene, id) : std::string(1, char(key & 0xff));
        }
    }

// Accept (0x4a5020): the keys kept, the pad's settings to d2d.cfg, back to the game.
auto Town::accept_controls() -> void {
        key_table = controls.table;
        save_keys(true);
        if (controls.click_rows && controls.clicks != click_keys) {   // d2d's click keys: d2d.cfg, not the .key
            click_keys = controls.clicks;
            if (!cfg_file.empty() && !d2d::userdir::save_cfg(cfg_file, { { "click_attack", keys::click_text(click_keys[keys::kClickAttack]) },
                                                                        { "click_skill", keys::click_text(click_keys[keys::kClickSkill]) } }))
                d2d::log::warn("d2d.cfg: the click keys not saved");
        }
        update_hotkey_names();
        if (!(controls.pad == pad_config)) {
            set_pad_config(controls.pad);
            pad_config_changed = true;
            if (!cfg_file.empty() && !d2d::userdir::save_cfg(cfg_file, pad::save_config(pad_config))) d2d::log::warn("d2d.cfg: the pad's settings not saved");
        }
    }

auto Town::function_held(int action) const -> bool {
        return key_held(keys::key_of(key_table, action, 0)) || key_held(keys::key_of(key_table, action, 1));
    }

// Say (0x468f80..0x468ff0): phrase 0x19 + n to the server (C→S 0x3f),
// which sets it on the player; every client then plays the class's
// voice (FUN_004cb9c0: its sound table's +0x20, - 0x32 + phrase * 2: one
// of the phrase's two takes). English shows no text with it (FUN_004a0200
// only does for languages 6, 7 and 9).
auto Town::say_sound(int cls, int phrase) -> int {
        static constexpr std::array<int, 7> kChatSound = { 0xb79, 0xcb1, 0xc15, 0xc63, 0xbc7, 0xd4d, 0xcff };   // amazon_chat_a_1 ...
        return kChatSound[std::size_t(std::clamp(cls, 0, 6))] + 2 * std::clamp(phrase, 0, 7);
    }

auto Town::say(int phrase) -> void {
        audio_say = say_sound(int(character.header.cls), phrase);
        if (net_game) net_game->phrase(std::uint16_t(0x19 + phrase));
    }

// Clear Screen (FUN_0044c6b0): every closable panel shuts, and the automap.
auto Town::clear_screen() -> void {
        inv_open = char_open = stash_open = cube_open = tree_open = quest_log.open = belt_open = hireling.open = party.open = false;
        if (help.open) toggle_help();
        automap.open = false;
        skillbar.picking = 0;
    }

// The Help screen (0x4689d0: UI 0x21): it brings the mini-panel up while
// it's open (FUN_004966c0, 0x7bef00) and puts it away after (FUN_00494fc0).
// The Party screen's roster (FUN_0049a640): the other players of the
// joined game, by unit id.
// ponytail: game.exe's roster keeps the order they joined in.
auto Town::party_rows() const -> std::vector<PartyPanel::Row> {
        std::vector<PartyPanel::Row> rows;
        const auto area_name = [&](int area) -> std::string {
            if (!scene || area <= 0) return {};
            const auto found = scene->level_names.find(area);
            if (found == scene->level_names.end()) return {};
            const auto text = lookup_string(*scene, std::string_view(found->second));
            return text ? u16_to_latin1(*text) : found->second;
        };
        if (coop_hud_hidden()) {                      // local co-op: everyone else on the screen, all in its one party
            for (const auto& member : world.players)
                if (member->id != focus_id())
                    rows.push_back({ std::uint32_t(member->id), member->character.name, int(member->character.header.cls), int(member->character.stats.get(d2d::d2s::kLevel)),
                                     1, 1, 0, area_name(level ? level->id : 0), false });
            return rows;
        }
        if (!net_game) return party_preview;
        for (const auto& [unit_key, unit] : net_game->units) {
            if (unit.type != 0 || unit.id == net_game->self_id || unit.name.empty()) continue;
            PartyPanel::Row row{ unit.id, unit.name, unit.cls, unit.level, unit.party, unit.relation, unit.flags, {}, (unit.flags & 8) != 0 };
            row.area = area_name(unit.area);
            rows.push_back(std::move(row));
        }
        std::ranges::sort(rows, {}, &PartyPanel::Row::id);
        return rows;
    }

auto Town::party_own() const -> PartyPanel::Own {
        PartyPanel::Own own{ character.name, int(character.stats.get(d2d::d2s::kLevel)), party_preview.empty() ? 0xffff : 1, level && d2d::rules::level_ids::is_town(level->id) };
        if (coop_hud_hidden()) { own.party = 1; own.fixed = true; }   // the screen's player (`character`), in local co-op's one party
        if (net_game)
            if (const auto self = net_game->units.find(std::uint64_t(net_game->self_id)); self != net_game->units.end()) own.party = self->second.party;
        return own;
    }

auto Town::toggle_help() -> void {
        help.open = !help.open;
        if (help.open && !mini.open) { mini.open = true; help_opened_mini = true; }
        if (!help.open && help_opened_mini) { mini.open = false; help_opened_mini = false; }
    }

auto Town::pad_field_or_free() const -> bool {
        return have_world && !game_menu.open && !controls.open && npc_menu.npc < 0 && store.npc < 0 && !inv_open && !stash_open && !cube_open;
    }

}  // namespace d2d::client
