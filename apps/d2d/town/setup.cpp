// SPDX-License-Identifier: GPL-3.0-or-later
// The in-game client (town.hpp): Town, the view to units, the frame's
// light, the HUD overlays.
// This part: publishing the world, entering, saving, the game menu and the quest log. The rest
// of Town (town.hpp) is in this directory, by subsystem.
#include "town.hpp"

#include "common.hpp"
#include "controls.hpp"
#include "ingame.hpp"
#include "internal.hpp"
#include "panels.hpp"
#include "party.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <cues.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <log.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <shake.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::client {

// Town
auto Town::publish() -> void {
        auto& owner = focused();                        // local co-op: the screen's player
        world.act_as(owner);
        auto out = world.view();
        world.act_as(world.lead());
        out.sounds = std::move(world.cues.due);
        world.cues.due.clear();
        out.events = std::move(owner.events);
        owner.events.clear();
        net.to_client = encode_view(*scene, out, view_enc);
        // D2D_VIEW_STATS=1: each View's size in the log (the replication budget).
        if (std::getenv("D2D_VIEW_STATS")) d2d::log::info("VIEWSTAT total={} monsters={} ground={} items={}", net.to_client.size(), out.monsters.size(), out.ground.size(), out.items.size());
        if (!apply_view(*scene, net.to_client, view)) {         // lost track: the next one is whole
            d2d::log::warn("a View didn't apply ({} bytes)", net.to_client.size());
            view_enc.reset();
            return;
        }
        if (!net_game && out.merc && view.merc) view.merc->fhr = out.merc->fhr;   // the wire has none: a local game keeps what World::view() computed
        level = view.level;
        if (net_game) net_overlay();
        cues.due.insert(cues.due.end(), view.sounds.begin(), view.sounds.end());
        if (!view.has_character) return;
        character.header = view.header; character.stats = view.stats; character.items = view.items;
        // A weapon switch brought the other set's skills: the buttons take them.
        if (int(view.header.weapon_set) != skillbar_set) {
            skillbar.left = int(view.header.left_skill); skillbar.right = int(view.header.right_skill); skillbar.left_item = skillbar.right_item = -1;
            skillbar_set = int(view.header.weapon_set);
        }
        character.expansion = view.header.expansion();
        character.panel = panel_stats(*scene, character.header, character.items, character.stats);
        character.panel.attack = view.attack_lines;
        // A quest's log state sent (S->C 0x5d, FUN_004a2cb0): the Quest Log
        // button, or the open log picks it.
        // ponytail: a change in the View's log state stands in for each send
        // (FUN_00544190); none at the game's first View (the join sends none).
        for (std::size_t quest = 1; !net_game && quest < view.quest_log.size(); ++quest)   // a joined game's come as 0x5d (net_quests)
            if (quest_log.sent_known && view.quest_log[quest] && view.quest_log[quest] != quest_log.sent[quest]) quest_log_notify(quest_log, int(quest));
        quest_log.sent = view.quest_log;
        quest_log.sent_known = true;
        held = view.held;
        if (view.store) {
            const auto keep = std::move(store);
            store = *view.store;
            if (keep.npc == store.npc) { store.tab = keep.tab; store.mode = keep.mode; store.pressed = keep.pressed; }
            else store.first_tab();                 // the tab isn't sent: the World's pick again
        } else {
            store = {};
        }
        hire_offers = view.hire_offers;
        if (const auto found = scene->mercs.find(character.header.merc_type); view.merc && found != scene->mercs.end())
            merc_label = merc_name(*scene, found->second, character.header.merc_name);
    }

// Where the automap's files go: beside the save (none with saving off).
std::filesystem::path automap_dir(const World& world) { return world.characters ? world.characters->dir : std::filesystem::path{}; }

auto Town::enter() -> void {
        shake = {}; quest_quakes.clear(); shake_rng = d2d::rules::Rng{ 666 };
        automap.cells.clear();                    // a new game: what the files beside the save kept
        automap.placed.clear();
        other_automaps.clear();
        quest_log = {};                           // done animations play again in a new game
        alert_clocks.clear();                      // Town is reused when a second game starts on the same level
        alert_level = nullptr;
        mini = {};                                // open (FUN_004567f0; ponytail: not the "Mini Panel" registry value)
        game_menu.open = false;
        controls.open = false;
        world.enter(character);
        if (scene) load_keys();
        coop.clear();                             // a new game: its first player alone
        lead_colour = 0;
        focus = 0;
        skillbar.new_game();
        hand_items.clear();
        publish();
        if (scene && level) rain_join();
        if (scene && level) load_automap(automap, automap_dir(world), character.name, scene->map_seed, level->layer, int(scene->automap_cels.frames_per_direction()));
    }

// The automap's new cells go out with the save, as on leaving a game
// (FUN_0045a5c0).
auto Town::save() -> std::string {
        if (scene && level) save_automap(automap, automap_dir(world), character.name, scene->map_seed, level->layer);
        world.each_player([&](World::Player& other) {    // the other co-op players' characters too
            if (&other == &hero) return;
            if (const auto why = world.save(); !why.empty()) d2d::log::warn("{}: {}", other.character.name, why);
        });
        return world.save();
    }

auto Town::operate(int npc_index, std::uint32_t frame_ms, int force ) -> void { world.operate(npc_index, frame_ms, force); }

auto Town::new_game() -> void {
        world.new_game();
        skillbar.new_game();
        hand_items.clear();
    }

auto Town::open_game_menu() -> void {
        if (help.open) toggle_help();                 // first: it may have brought the mini-panel up
        game_menu.restore_automap = automap.open;
        game_menu.restore_mini = mini.open;
        automap.open = mini.open = belt_open = false;
        inv_open = char_open = stash_open = cube_open = tree_open = quest_log.open = hireling.open = party.open = false;
        game_menu.expansion = character.expansion;
        game_menu.open = true;
        game_menu.show(0);
    }

auto Town::close_game_menu() -> void {
        game_menu.open = false;
        automap.open = game_menu.restore_automap;
        mini.open = game_menu.restore_mini;
    }

auto Town::quest_bits() const -> const d2d::rules::QuestBits& {
        return character.header.quests[std::size_t(std::clamp(character.header.active_difficulty(), 0, 2))];
    }

// The player's act (0-based): the act of the level the View has them on
// (acts 1 and 2 so far; a joined game's too).
auto Town::view_act() const -> int { return view.level ? d2d::rules::level_ids::act_of(view.level->id) : 0; }

auto Town::toggle_quest_log() -> void {
        if (quest_log.open) { quest_log.open = false; return; }
        quest_log_open(quest_log, quest_bits(), quest_state(), view_act());
        char_open = stash_open = cube_open = false;
    }

auto Town::quest_log_button_now() const -> QuestLogButton {
        const bool left_open = char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open;
        return quest_log_button(quest_log, left_open, inv_open || tree_open, char_open);
    }

auto Town::level_buttons_now() const -> LevelButtons {
        const bool left_open = char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open;
        auto out = level_buttons(character.stats, left_open, inv_open || tree_open, char_open, tree_open, store.npc >= 0);
        out.stats_down = stats_down;
        out.skills_down = skills_down;
        return out;
    }

auto Town::hovered_monster() const -> int {
        return hovered_npc <= -10 && hovered_npc > -1000 && std::size_t(-10 - hovered_npc) < view.monsters.size() ? -10 - hovered_npc : -1;
    }

auto Town::hovered_ground() const -> int {
        return hovered_npc <= -1000 && std::size_t(-1000 - hovered_npc) < view.ground.size() ? -1000 - hovered_npc : -1;
    }

}  // namespace d2d::client
