// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in touch prototype: separate per-character destinations, shared skill
// eligibility, targeting and gameplay commands. No platform code in the World.
#include "town.hpp"

#include "common.hpp"
#include "gamepad_map.hpp"
#include "item_use.hpp"
#include "skillpad.hpp"
#include "skilltree.hpp"
#include "store.hpp"
#include "touch_input.hpp"

#include <SDL3/SDL_keycode.h>

#include <d2s_items.hpp>
#include <dc6.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level_ids.hpp>
#include <log.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace d2d::client {

/**
 * Load the named character touch assignments independently of controller positions. Remove the
 * cached sheet if its sidecar cannot be opened.
 */
void Town::load_touch_sheet(const std::string& name) {
    if (key_dir.empty() || name.empty()) return;
    std::ifstream file(key_dir / (name + ".touchskills"));
    if (file) touch_sheets[sheet_key(name)] = skillpad::read(std::string{ std::istreambuf_iterator<char>(file), {} });
    else touch_sheets.erase(sheet_key(name));
}

/**
 * Persist a cached touch sheet through a checked temporary file and rename. Leave the original
 * destination in place on a failed write and report the failure.
 */
void Town::save_touch_sheet(const std::string& name) const {
    if (key_dir.empty() || name.empty()) return;
    const auto found = touch_sheets.find(sheet_key(name));
    if (found == touch_sheets.end()) return;
    const auto path = key_dir / (name + ".touchskills");
    auto temporary = path; temporary += ".tmp";
    std::ofstream file(temporary);
    if (!file) { d2d::log::warn("Touch assignments could not be written"); return; }
    file << skillpad::write(found->second);
    file.close();
    std::error_code error;
    if (file) std::filesystem::rename(temporary, path, error);
    if (!file || error) {
        d2d::log::warn("Touch assignments could not be written");
        std::filesystem::remove(temporary, error);
    }
}

/**
 * Resolve a controller/touch skill preview for the selected owner without issuing a command. Manual
 * aim uses screen drag distance; self casts, eligible corpses and Teleport each apply their own
 * target policy.
 */
touch::Target Town::resolve_skill(PadPlayer& player_pad, int skill, float dx, float dy, bool manual, const UnitState* owner, std::string_view owner_name) const {
    touch::Target target;
    target.skill = skill;
    if (skillpad::panel_index(skill) >= 0) { target.valid = true; return target; }
    if (!scene || !skill_known(owner_name.empty() ? std::string_view(character.name) : owner_name, skill)) return target;
    const auto* info = scene->skills.get(skill);
    if (!info || info->passive) return target;
    if (skillpad::portal_name(info->name)) {
        target.valid = portal_tome(owner_name.empty() ? std::string_view(character.name) : owner_name) >= 0 && level && !d2d::rules::level_ids::is_town(level->id);
        return target;
    }
    const auto& unit = owner ? *owner : view.player;
    target.valid = true;
    target.x = unit.x; target.y = unit.y;
    if (info->aura || self_cast(*info) || info->srvdofunc == d2d::rules::ServerDoFunction::kWarCry ||
        info->srvdofunc == d2d::rules::ServerDoFunction::kNova) return target;
    const bool teleport = info->srvdofunc == d2d::rules::ServerDoFunction::kTeleport;
    if (manual) {
        // A drag is a screen direction and distance, never an enemy chase.
        const float screen_x = std::clamp(dx * 2.f, -370.f, 370.f);
        const float screen_y = std::clamp(dy * 2.f, -250.f, 210.f);
        // Inverse isometric projection. Range grows with drag distance, and
        // the preview stays in the visible viewport instead of off-screen.
        target.x += screen_x / kIsoW + screen_y / kIsoH;
        target.y += screen_y / kIsoH - screen_x / kIsoW;
    } else {
        target.x += player_pad.face_x * (teleport ? 6.f : 3.f);
        target.y += player_pad.face_y * (teleport ? 6.f : 3.f);
    }
    if (info->target_corpse || info->srvdofunc == d2d::rules::ServerDoFunction::kCorpseExplosion) {
        const float wanted_x = manual ? target.x : unit.x, wanted_y = manual ? target.y : unit.y;
        float distance = manual ? 4.f : 12.f;
        target.valid = false;
        for (const auto& corpse : view.monsters)
            if (corpse.usable_corpse()) {
                const float apart = std::hypot(corpse.unit.x - wanted_x, corpse.unit.y - wanted_y);
                if (apart < distance) {
                    distance = apart; target.valid = true;
                    target.x = corpse.unit.x; target.y = corpse.unit.y; target.unit = corpse.id;
                }
            }
        return target;
    }
    if (teleport && level) {
        // Preview the same room constraint and nearest free subtile the
        // existing do-27 path uses (FUN_005ca360 / FUN_0064e7b0).
        if (d2d::rules::level_ids::is_town(level->id) || level->teleport == 0) target.valid = false;
        const int spot_x = int(std::floor(target.x * 5)), spot_y = int(std::floor(target.y * 5));
        const auto rooms = missile_rooms(*scene, *level, unit.x, unit.y);
        if (!std::ranges::any_of(rooms, [&](const auto& room) {
            const int sub_x = spot_x + level->world_x * 5, sub_y = spot_y + level->world_y * 5;
            return sub_x >= room[0] && sub_y >= room[1] && sub_x < room[0] + room[2] && sub_y < room[1] + room[3];
        })) target.valid = false;
        const auto spot = level->warp_spot(spot_x, spot_y);
        if (!spot) target.valid = false;
        else if (*spot != std::pair{ spot_x, spot_y }) {
            target.x = (float(spot->first) + .5f) / 5; target.y = (float(spot->second) + .5f) / 5;
        }
        if (level->teleport == 2) {
            const auto wall = [&](int at_x, int at_y) { return level->blocked((float(at_x) + .5f) / 5, (float(at_y) + .5f) / 5, 0x804); };
            if (d2d::rules::sight_blocked(int(std::floor(unit.x * 5)), int(std::floor(unit.y * 5)), 2, spot_x, spot_y, 2, wall)) target.valid = false;
        }
    }
    if (!manual && !teleport) {
        const auto picked = pad_pick(player_pad, unit, false, pad_aim(skill, player_pad.move.aim || player_pad.move.active));
        if (picked.kind == PadPick::kMonster) { target.x = picked.x; target.y = picked.y; target.unit = picked.id; }
    }
    target.left = skillpad::swings({ info->leftskill, !info->srvmissile.empty(), info->range });
    return target;
}

/**
 * Dispatch a committed skill preview after rechecking availability and player state. Panel
 * shortcuts open UI, portal bindings use a carried tome, and auras update the selected hand rather
 * than attacking a target.
 */
void Town::cast_resolved(PadPlayer& player_pad, const touch::Target& target) {
    if (target.valid && skillpad::panel_index(target.skill) >= 0) {
        skill_strip = skillpad::panel_index(target.skill);
        pad::Actions open; open.confirm = true; drive_strip(open);
        touch_input.clear();
        return;
    }
    if (!target.valid || !scene || !skill_known(character.name, target.skill) || view.dead) return;
    const auto* info = scene->skills.get(target.skill);
    if (!info) return;
    if (skillpad::portal_name(info->name)) {
        if (const int item = portal_tome(character.name); item >= 0) send(cmd::UseItem{ item });
        return;
    }
    if (info->aura) {
        const auto* current = scene->skills.get(skillbar.right);
        const auto choice = skillpad::aura_choice(view.aura, target.skill, skillbar.right, current && current->aura, player_pad.aura_return);
        player_pad.aura_return = choice.remembered;
        skillbar.choose(false, { choice.next, -1 });
        if (net_game) net_game->hand(choice.next, false);
        send(cmd::SelectSkill{ choice.next, false });
    } else send(cmd::UseSkill{ target.skill, target.x, target.y, target.unit, target.left, -1 });
}

/**
 * Route a logical touch event to assignment, inventory/panel interaction or field gestures. Return
 * true for consumed events; field gestures retain separate movement and skill ownership.
 */
bool Town::touch_event(std::int64_t finger, touch::Phase phase, float x, float y) {
    if (!touch_enabled) return false;
    if (phase == touch::Phase::Down && touch::placed(touch::kDone).contains(x, y)) {
        touch_input.clear(); touch_item_finger = -1;
        if (touch_assign != skillpad::kUnset) { touch_assign = skillpad::kUnset; tree_open = touch_assignment_from_tree; touch_assignment_from_tree = false; return true; }
        focus_keys.push_back(SDLK_ESCAPE);
        return true;
    }
    if (touch_assign != skillpad::kUnset) {
        if (phase == touch::Phase::Down && touch::placed(touch::kMenu).contains(x, y)) { touch_assign = skillpad::kInventory; return true; }
        touch_input.event(finger, phase, x, y, {}, false, true);
        if (touch_input.destination >= 0) {
            auto [found, inserted] = touch_sheets.try_emplace(sheet_key(character.name));
            if (inserted) found->second.slots.fill(skillpad::kEmpty);
            found->second.set_slot(touch_input.alternate, touch_input.destination, touch_assign);
            save_touch_sheet(character.name);
            const auto* info = scene ? scene->skills.get(touch_assign) : nullptr;
            touch_feedback = std::to_string(touch_input.destination + 1) + " . " + (info ? info->name : std::string(skillpad::action_label(touch_assign, true)));
            touch_input.destination = -1;
        }
        return true;
    }
    if (!pad_field()) {
        touch_input.clear();
        if (phase == touch::Phase::Cancel && finger == touch_item_finger) {
            touch_item_finger = -1; touch_item = touch_stock = -1; return true;
        }
        if (phase == touch::Phase::Move && finger == touch_item_finger && std::hypot(x - touch_item_x, y - touch_item_y) > 18) {
            touch_item_finger = -1; touch_item = touch_stock = -1; return true;
        }
        if (phase == touch::Phase::Up && finger == touch_item_finger) {
            touch_item_finger = -1;
            if (touch_stock >= 0 && store.npc >= 0) { send(cmd::Buy{ touch_stock, store.tab }); touch_stock = -1; return true; }
            const auto found = std::ranges::find(character.items, touch_item, &d2d::d2s::Item::id);
            if (found != character.items.end() && !touch_sold) {
                const auto action = item_use(*found);
                if (action.kind == ItemUseKind::Identify) identify_with = found->id;
                else if (action.kind != ItemUseKind::None) send(cmd::UseItem{ found->id });
                else if (store.npc >= 0 && action.label.empty()) send(cmd::ToCursor{ found->id });
            }
            return true;
        }
        if (phase == touch::Phase::Down && scene && identify_with < 0 && !held) {
            touch_stock = store.npc >= 0 ? store_item_at(*scene, store, int(x), int(y)) : -1;
            const auto* item = bag_at(int(x), int(y));
            const auto action = item ? item_use(*item) : ItemUse{};
            if (touch_stock >= 0 || (item && (!action.label.empty() || (store.npc >= 0 && d2d::rules::sellable(scene->rules, *item))))) {
                touch_item_finger = finger; touch_item = item ? item->id : -1;
                touch_item_start = now_ms; touch_sold = false;
                touch_item_x = x; touch_item_y = y;
                touch_item_hint = touch_stock >= 0 ? "Tap . Buy" : std::string(action.label);
                if (item && store.npc >= 0 && d2d::rules::sellable(scene->rules, *item)) touch_item_hint += " . Hold item to sell";
                return true;
            }
        }
        if (tree_open && scene) {
            if (phase == touch::Phase::Down && x >= 410 && x < 650 && y >= 545 && y < 595 && touch_selected >= 0) {
                touch_assign = touch_selected; touch_feedback.clear(); touch_assignment_from_tree = true; tree_open = false; return true;
            }
            const int cls = std::max(character.character_class, 0);
            const int index = skill_at(*scene, cls, tree_tab, int(x), int(y));
            const auto& ids = scene->skills.class_ids[std::size_t(cls)];
            if (index >= 0 && std::size_t(index) < ids.size()) {
                if (phase == touch::Phase::Down) touch_selected = skill_known(character.name, ids[std::size_t(index)]) ? ids[std::size_t(index)] : skillpad::kEmpty;
                return true; // selection is distinct from spending a skill point
            }
        }
        return false; // other panels use their ordinary pointer input
    }
    if (phase == touch::Phase::Down && touch::placed(touch::Box{ 400, 0, 135, 60 }).contains(x, y)) {
        touch_input.clear(); tree_open = true; inv_open = false; return true;
    }
    touch::Target target;
    bool immediate = false;
    if (phase == touch::Phase::Down)
        for (int index = 0; index < 4; ++index)
            if (touch::placed(touch::kSkills[index]).contains(x, y)) {
                target = touch_previews[std::size_t(index)];
                const auto* info = scene ? scene->skills.get(target.skill) : nullptr;
                immediate = info && (info->aura || self_cast(*info));
            }
    touch_input.event(finger, phase, x, y, target, immediate);
    if (touch_input.aiming && touch_input.dragged)
        touch_input.preview = resolve_skill(touch_player, touch_input.preview.skill, touch_input.aim_x - touch_input.origin_x,
                                          touch_input.aim_y - touch_input.origin_y, true);
    return true;
}

/**
 * Consume touch state into movement, attack, belt, item-sale and committed skill commands.
 * Invalidate gestures when field input loses ownership, and require a fresh attack press before
 * pickup.
 */
void Town::drive_touch() {
    if (!touch_enabled) return;
    const int panel_state = (store.npc >= 0 ? store.npc + 2 : 0) + (inv_open ? 1000 : 0) + (tree_open ? 2000 : 0) +
                            (char_open ? 4000 : 0) + (game_menu.open ? 8000 : 0) + (skillbar.picking ? 16000 : 0);
    if (panel_state != touch_panel_state) {
        touch_item_finger = -1; touch_panel_state = panel_state;
    }
    if (touch_item_finger != -1 && store.npc >= 0 && !touch_sold && touch_item >= 0 && now_ms - touch_item_start >= 550) {
        const auto found = std::ranges::find(character.items, touch_item, &d2d::d2s::Item::id);
        if (found != character.items.end() && scene && !store.gamble && d2d::rules::sellable(scene->rules, *found)) {
            send(cmd::Sell{ found->id }); touch_sold = true;
        }
    }
    if (!pad_field() || view.dead) {
        const Send stop = [this](const Command& command) { send(command); };
        pad_walk(touch_player, view.player, view.dead, false, stop);
        touch_input.clear(); return;
    }
    if (touch_input.menu) { touch_input.clear(); inv_open = true; tree_open = false; return; }
    const Send out = [this](const Command& command) { send(command); };
    touch_player.move = { touch_input.move_x, touch_input.move_y, std::hypot(touch_input.move_x, touch_input.move_y) > 0.01f };
    if (touch_player.move.active) {
        const auto face = pad::world_step(touch_input.move_x, touch_input.move_y, kIsoW, kIsoH, 1.f);
        touch_player.face_x = face.x; touch_player.face_y = face.y;
    }
    pad_walk(touch_player, view.player, false, true, out);
    if (touch_input.attack_press) touch_attack_used = false;
    if (touch_input.attack_down && !touch_attack_used) {
        const auto picked = touch_input.attack_press ? touch_attack_pick : pad_pick(touch_player, view.player, false);
        if (picked.kind == PadPick::kItem) { send(cmd::Pickup{ picked.id }); touch_attack_used = true; }
        else if (picked.kind == PadPick::kThing) { send(cmd::Interact{ picked.id }); touch_attack_used = true; }
        else if (picked.kind == PadPick::kMonster) send(cmd::UseSkill{ 0, picked.x, picked.y, picked.id, true, -1 });
    }
    if (touch_input.belt >= 0) send(cmd::UseBelt{ touch_input.belt });
    for (const auto& target : touch_input.casts) cast_resolved(touch_player, target);

}

/**
 * Draw the optional touch controls, assignment destinations and aim/cancel feedback. Reuse the
 * skill preview and current item hints without committing gameplay actions.
 */
void Town::draw_touch(std::vector<std::uint8_t>& framebuffer) {
    if (!touch_enabled || !scene || !have_world) return;
    const auto& font = scene->font_small;
    const auto& palette = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    const auto button = [&](touch::Box box, const std::string& text, bool active = false) {
        for (int pixel_y = int(box.y); pixel_y < int(box.y + box.height); ++pixel_y)
            for (int pixel_x = int(box.x); pixel_x < int(box.x + box.width); ++pixel_x) {
                if (pixel_x < 0 || pixel_x >= int(kScreenWidth) || pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
                auto* pixel = &framebuffer[(std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)) * 4];
                const bool round = pad_field() && (box.y == touch::placed(touch::kAttack).y || box.y == touch::placed(touch::kSkills[0]).y || box.y == touch::placed(touch::kSkills[1]).y);
                const float radius_x = box.width / 2, radius_y = box.height / 2;
                const float ellipse = std::hypot((float(pixel_x) - box.x - radius_x) / radius_x, (float(pixel_y) - box.y - radius_y) / radius_y);
                if (round && ellipse > 1) continue;
                const bool edge = round ? ellipse > .94f : pixel_x == int(box.x) || pixel_y == int(box.y) || pixel_x == int(box.x + box.width) - 1 || pixel_y == int(box.y + box.height) - 1;
                if (edge) { pixel[0] = 199; pixel[1] = 179; pixel[2] = 119; }
                else { pixel[0] /= 2; pixel[1] /= 2; pixel[2] /= 2; }
            }
        std::string label = text;
        while (!label.empty() && font.measure(label) > int(box.width) - 8) label.pop_back();
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, palette, int(box.x) + 4, int(box.y) + 12, label,
                         active ? 255 : 199, active ? 220 : 179, 119);
    };
    button(touch::placed(touch::kDone), touch_assign != skillpad::kUnset || skillbar.picking == 3 ? "Done" : pad_field() ? "Menu" : "Back");
    if (touch_assign != skillpad::kUnset) {
        button(touch::placed(touch::kMenu), "Assign Inventory");
        button(touch::placed(touch::kPage), touch_input.alternate ? "II" : "I");
        const auto found = touch_sheets.find(sheet_key(character.name));
        for (int index = 0; index < 4; ++index) {
            const int id = found == touch_sheets.end() ? -1 : found->second.slot(touch_input.alternate, index);
            const auto* info = scene->skills.get(id);
            button(touch::placed(touch::kSkills[index]), std::to_string(index + 1) + ": " + (info ? info->name : "Empty"));
        }
        const auto* info = scene->skills.get(touch_assign);
        font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 310, 170, "Assign: " + (info ? info->name : std::string(skillpad::action_label(touch_assign, true))));
        font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 310, 195, touch_feedback);
        return;
    }
    if (tree_open) {
        if (touch_selected >= 0) button(touch::placed({ 410, 545, 240, 50 }), "Assign to touch button");
        return;
    }
    if (!pad_field()) {
        font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 300, 75,
            identify_with >= 0 ? "Tap an unidentified item . Back cancels" : touch_item_hint);
        return;
    }
    touch_attack_pick = pad_pick(touch_player, view.player, true);
    const auto found = touch_sheets.find(sheet_key(character.name));
    for (int index = 0; index < 4; ++index) {
        const int skill = found == touch_sheets.end() ? skillpad::kEmpty : found->second.slot(touch_input.alternate, index);
        touch_previews[std::size_t(index)] = resolve_skill(touch_player, skill);
    }
    button(touch::placed(touch::kMenu), "Inventory");
    button(touch::placed({ 400, 12, 135, 48 }), "Skills");
    // A generous hit area with a compact thumb-pad drawing above the HUD.
    const auto ring = [&](float centre_x, float centre_y, float radius) {
        for (int pixel_y = int(centre_y - radius - 1); pixel_y <= int(centre_y + radius + 1); ++pixel_y)
            for (int pixel_x = int(centre_x - radius - 1); pixel_x <= int(centre_x + radius + 1); ++pixel_x)
                if (pixel_x >= 0 && pixel_x < int(kScreenWidth) && pixel_y >= 0 && pixel_y < int(kScreenHeight) &&
                    std::abs(std::hypot(float(pixel_x) - centre_x, float(pixel_y) - centre_y) - radius) < 1.5f) {
                    auto* pixel = &framebuffer[(std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)) * 4];
                    pixel[0] = 199; pixel[1] = 179; pixel[2] = 119;
                }
    };
    ring(145 + touch::safe_inset.left, 445 - touch::safe_inset.bottom, 60);
    ring(145 + touch::safe_inset.left + touch_input.move_x * 40, 445 - touch::safe_inset.bottom + touch_input.move_y * 40, 16);
    button(touch::placed(touch::kAttack), "Attack / Use");
    button(touch::placed(touch::kPage), touch_input.alternate ? "II (tap: I)" : "I (tap: II)");
    for (int index = 0; index < 4; ++index) {
        button(touch::placed(touch::kBelts[index]), "Belt " + std::to_string(index + 1));
        const auto& target = touch_previews[std::size_t(index)];
        const auto* info = scene->skills.get(target.skill);
        button(touch::placed(touch::kSkills[index]), info ? info->name : target.skill < -2 ? std::string(skillpad::action_label(target.skill, false)) : "Empty", info && info->id == view.aura);
        if (info) {
            int icon_class = -1;
            for (int column = 0; column < 7; ++column)
                if (info->cls == d2d::rules::kClassCode[std::size_t(column)]) icon_class = column;
            const auto& art = icon_class >= 0 ? scene->skill_icons[std::size_t(icon_class)] : scene->generic_skill_icons;
            if (info->icon >= 0 && std::uint32_t(info->icon) < art.frames_per_direction())
                blit_sprite(framebuffer, art.frame(0, std::uint32_t(info->icon)), palette,
                            int(touch::placed(touch::kSkills[index]).x) + 32, int(touch::placed(touch::kSkills[index]).y) + 23);
        }
    }
    if (view.aura > 0) {
        const auto* aura = scene->skills.get(view.aura);
        if (aura) font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 310, 65, "Aura: " + aura->name);
    }
    font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 300, 85, pick_label(touch_attack_pick));
    if (touch_input.aiming) {
        button(touch::placed(touch::kCancel), "Drag here to cancel");
        const auto& target = touch_input.preview;
        const int screen_x = int(kScreenWidth) / 2 + int((target.x - cam_x - target.y + cam_y) * kIsoW / 2);
        const int screen_y = kViewY + int((target.x - cam_x + target.y - cam_y) * kIsoH / 2);
        button({ float(screen_x - 12), float(screen_y - 12), 24, 24 }, target.valid ? "+" : "X");
        const auto* info = scene->skills.get(target.skill);
        font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 300, 120,
            (info ? info->name : std::string("Skill")) + (target.valid ? " - release to cast" : " - no eligible target"));
    }
}

} // namespace d2d::client
