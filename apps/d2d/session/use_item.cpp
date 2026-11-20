// SPDX-License-Identifier: GPL-3.0-or-later
// ClientSession (session.hpp): the item panel an NPC's service opens (use_item.hpp,
// game.exe's .\UI\UseItem.cpp): its clicks, keys and answer. The rest is in
// this directory.
#include "session.hpp"

#include "audio.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include "ui.hpp"
#include "use_item.hpp"

#include <d2s_items.hpp>
#include <item_text.hpp>
#include <rules.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace d2d::client {

// Charsi's, Larzuk's or Anya's menu entry (0x4b35b0 / its twins): the menu
// shuts, the panel opens (0x4c0620 -> FUN_004c0380: Esc's close-all first,
// FUN_00456300) with the inventory beside it. The talk goes on while it's up.
auto ClientSession::open_use_item(int npc) -> void {
    if (!level || std::size_t(npc) >= level->npcs.size()) return;
    use_item::open(use_item, npc, level->npcs[std::size_t(npc)].hc_idx, now_ms);
    char_open = stash_open = cube_open = tree_open = quest_log.open = party.open = hireling.open = false;
    waypoint = {};
    inv_open = true;
}

// FUN_004c0090: shut; a held item back on the cursor (FUN_00468070).
// The inventory goes with it: the open sets UI 0xe alone (FUN_004c0380, after the close-all), whose
// flag is what draws the inventory beside the panel (FUN_00456ee0), and the close clears it.
auto ClientSession::close_use_item() -> void {
    if (use_item.open()) inv_open = false;
    use_item = {};
}

// The panel's handlers come first while it's up (FUN_004c0380 hangs them,
// 0x727950): every press and release is its own, the inventory's only in
// state 1; Esc and Space shut it. The right button's press and release
// (0x204 / 0x205) are the left's (FUN_004bfc50 / FUN_004c04e0).
auto ClientSession::drive_use_item(Mouse& mouse, std::vector<SDL_Keycode>& keys, Audio& audio) -> void {
    if (!use_item.open() || !scene) return;
    const auto play = [&](const use_item::Outcome& out) {
        for (const int sound : out.sounds)
            if (sound > 0) audio.play_sfx(*scene, sound, 1.f, 0);
    };
    const auto act = [&](const use_item::Outcome& out) {
        play(out);
        if (out.act == use_item::Act::kPlace && held) {
            use_item.item = held->id;
            if (const auto found = scene->sound_index.find(d2d::game::item_sound(*scene, *held, false)); found != scene->sound_index.end())
                audio.play_sfx(*scene, found->second, 1.f, 0);   // FUN_004c1d60
        }
        if (out.act == use_item::Act::kSend) {
            if (use_item.kind == 0) send(cmd::ObjectItem{ use_item.npc, use_item.item, true });   // FUN_00478700(object, item, 3)
            else send(cmd::Imbue{ use_item.npc, use_item.item });                                 // FUN_004b2370
        }
        if (out.act == use_item::Act::kClose) {
            if (use_item.kind == 0) send(cmd::ObjectItem{ use_item.npc, -1, false });            // FUN_00478700(object, 0, 2)
            close_use_item();
        }
    };
    if (std::ranges::any_of(keys, [](SDL_Keycode key) { return key == SDLK_ESCAPE || key == SDLK_SPACE; })) {
        act(use_item::key(use_item));
        std::erase_if(keys, [](SDL_Keycode key) { return key == SDLK_ESCAPE || key == SDLK_SPACE; });
        if (!use_item.open()) return;
    }
    const Scene::InvLayout lay = touch_enabled ? touch_inv_layout() : scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
    const bool in_inventory = inv_open && mouse.x >= lay.panel_x && mouse.x < lay.panel_x + 320 && mouse.y >= lay.panel_y && mouse.y < lay.panel_y + 432;
    const int close_x = lay.panel_x + 0x12, close_bottom = lay.panel_y + 416;
    const bool on_close_box = inv_open && mouse.x >= close_x && mouse.x < close_x + 0x20 && mouse.y > close_bottom - 0x20 && mouse.y <= close_bottom;
    const bool accepted = use_item.kind == 0 ? held && use_item::object_takes(held->code, held->identified)
                          : held && held->identified
                          && (use_item.hc_idx == use_item::kCharsi   ? d2d::rules::imbuable(scene->rules, *held, character.expansion)
                              : use_item.hc_idx == use_item::kLarzuk ? d2d::rules::socketable(scene->rules, *held)
                              : use_item.hc_idx == use_item::kAnya   && d2d::rules::nameable(scene->rules, *held));
    bool inventory_press = false;
    for (const bool pressed : { mouse.press_this_frame, mouse.rpress_this_frame }) {
        if (!pressed || !use_item.open()) continue;
        const auto out = use_item::press(use_item, mouse.x, mouse.y, now_ms, in_inventory, on_close_box, held.has_value(), accepted);
        inventory_press = inventory_press || out.act == use_item::Act::kInventory;
        act(out);
    }
    for (const bool released : { mouse.release_this_frame, mouse.rrelease_this_frame })
        if (released && use_item.open()) act(use_item::release(use_item, mouse.x, mouse.y, on_close_box));
    // The inventory takes its own clicks in state 1, nothing else does.
    const bool inventory_own = use_item.state == use_item::State::kOpen && in_inventory && !on_close_box;
    if (!inventory_press && !inventory_own) mouse.down = mouse.press_this_frame = mouse.rpress_this_frame = false;
    if (!inventory_own) mouse.release_this_frame = mouse.rrelease_this_frame = false;
}

// S->C 0x58 for the panel (FUN_004c0550): 6 / 7 shut it, the talk ends with it; 5 plays
// Sounds.txt row 5 (the orifice's +6 set) as the staff goes in.
auto ClientSession::use_item_reply(int kind, Audio* audio) -> void {
    if (kind == 5 && scene && audio) audio->play_sfx(*scene, 5, 1.f, 0);
    if (use_item::reply(use_item, kind)) close_use_item();
}

}  // namespace d2d::client
