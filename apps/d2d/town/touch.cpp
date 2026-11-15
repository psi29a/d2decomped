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

namespace {

/** True when the contextual interact chrome should accept touches and block field taps. */
bool touch_interact_visible(const Town::PadPick& pick) {
    return pick.kind == Town::PadPick::kItem || pick.kind == Town::PadPick::kThing;
}

/** Short Immortal-style label for the contextual interact button. */
std::string touch_interact_label(const Town& town, const Town::PadPick& pick) {
    if (pick.kind == Town::PadPick::kItem) return "Get";
    if (pick.kind != Town::PadPick::kThing) return {};
    if (pick.id <= -3000) return "Get";
    if (pick.id < 0) return "Use";
    if (town.level && std::size_t(pick.id) < town.level->npcs.size())
        return town.level->npcs[std::size_t(pick.id)].root == "objects" ? "Use" : "...";
    return "Use";
}

} // namespace

auto Town::touch_compact_field() const -> bool {
    return touch_enabled && !touch::g_touch_classic_hud && pad_field() && !automap.open;
}

Scene::InvLayout Town::touch_inv_layout() const {
    const int cls = std::max(character.character_class, 0);
    if (!scene) return {};
    Scene::InvLayout lay = scene->inv_layout[std::size_t(cls)];
    if (!touch_enabled || !inv_open) return lay;
    const int dx = touch::kTouchInvPanelX - lay.panel_x, dy = touch::kTouchInvPanelY - lay.panel_y;
    lay.panel_x += dx;
    lay.panel_y += dy;
    lay.grid_x += dx;
    lay.grid_y += dy;
    for (auto& slot : lay.slots) {
        slot[0] += dx;
        slot[1] += dy;
    }
    return lay;
}

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
    if (pad_field()) touch_interact_pick = pad_pick(touch_player, view.player, true, {}, false);
    if (phase == touch::Phase::Down && touch::placed(touch::kDone).contains(x, y)) {
        touch_input.clear(); touch_item_finger = -1; touch_ground_finger = -1;
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
            const int tree_panel_right = touch::kTouchTreeRight;
            const int assign_x = tree_panel_right - 80;
            if (phase == touch::Phase::Down && x >= assign_x && x < assign_x + 240 && y >= 545 && y < 595 && touch_selected >= 0) {
                touch_assign = touch_selected; touch_feedback.clear(); touch_assignment_from_tree = true; tree_open = false; return true;
            }
            const int cls = std::max(character.character_class, 0);
            const int index = skill_at(*scene, cls, tree_tab, int(x), int(y), tree_panel_right);
            const auto& ids = scene->skills.class_ids[std::size_t(cls)];
            if (index >= 0 && std::size_t(index) < ids.size()) {
                if (phase == touch::Phase::Down) touch_selected = skill_known(character.name, ids[std::size_t(index)]) ? ids[std::size_t(index)] : skillpad::kEmpty;
                return true; // selection is distinct from spending a skill point
            }
        }
        return false; // other panels use their ordinary pointer input
    }
    if (phase == touch::Phase::Down && touch::placed(touch::kSkillsBar).contains(x, y)) {
        touch_input.clear(); tree_open = true; inv_open = false; return true;
    }
    if (phase == touch::Phase::Down && touch::placed(touch::kMap).contains(x, y)) {
        touch_input.clear();
        automap.open = !automap.open;
        if (automap.open) automap.mini = 0;
        return true;
    }
    if (pad_field()) {
        if (phase == touch::Phase::Down && touch_interact_visible(touch_interact_pick) &&
            touch::placed(touch::kInteract).contains(x, y)) {
            if (touch_interact_pick.kind == PadPick::kItem) send(cmd::Pickup{ touch_interact_pick.id });
            else if (touch_interact_pick.kind == PadPick::kThing) send(cmd::Interact{ touch_interact_pick.id });
            return true;
        }
        if (phase == touch::Phase::Down && !touch::chrome_hit(x, y, touch_interact_visible(touch_interact_pick))) {
            touch_ground_finger = finger; touch_ground_x = x; touch_ground_y = y;
        } else if (finger == touch_ground_finger) {
            if (phase == touch::Phase::Move &&
                std::hypot(x - touch_ground_x, y - touch_ground_y) > 14.f)
                touch_ground_finger = -1;
            if (phase == touch::Phase::Up && touch_ground_finger == finger) {
                const float iso_u = float(x - int(kScreenWidth) / 2) / (kIsoW / 2);
                const float iso_v = float(y - kViewY) / (kIsoH / 2);
                const float world_x = cam_x + (iso_u + iso_v) / 2, world_y = cam_y + (iso_v - iso_u) / 2;
                send(cmd::Move{ world_x, world_y, true });
                touch_ground_finger = -1;
            }
            if (phase == touch::Phase::Cancel) touch_ground_finger = -1;
        }
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
    touch_attack_pick = pad_pick(touch_player, view.player, false);
    touch_interact_pick = pad_pick(touch_player, view.player, true, {}, false);
    if (touch_input.attack_down && scene) {
        const auto* attack_info = scene->skills.get(0);
        if (attack_info && !attack_info->passive && skill_known(character.name, 0)) {
            const bool swing = skillpad::swings({ attack_info->leftskill, !attack_info->srvmissile.empty(), attack_info->range });
            const bool moving = touch_player.move.aim || touch_player.move.active;
            const PadPick pick = pad_pick(touch_player, view.player, false, pad_aim(0, moving));
            if (pick.kind == PadPick::kMonster) {
                touch_player.target = pick.id;
                out(cmd::UseSkill{ 0, pick.x, pick.y, pick.id, swing, -1 });
            } else {
                touch_player.target = -1;
                const UnitState& unit = view.player;
                out(cmd::UseSkill{ 0, unit.x + touch_player.face_x * 3.f, unit.y + touch_player.face_y * 3.f, -1, swing, -1 });
            }
        }
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
    const auto darken = [&](int pixel_x, int pixel_y, std::uint8_t alpha) {
        if (pixel_x < 0 || pixel_x >= int(kScreenWidth) || pixel_y < 0 || pixel_y >= int(kScreenHeight)) return;
        auto* pixel = &framebuffer[(std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)) * 4];
        const float scale = 1.f - alpha / 255.f * .72f;
        pixel[0] = std::uint8_t(pixel[0] * scale);
        pixel[1] = std::uint8_t(pixel[1] * scale);
        pixel[2] = std::uint8_t(pixel[2] * scale);
    };
    const auto halo = [&](float centre_x, float centre_y, float radius, std::uint8_t alpha = 110) {
        for (int pixel_y = int(centre_y - radius); pixel_y <= int(centre_y + radius); ++pixel_y)
            for (int pixel_x = int(centre_x - radius); pixel_x <= int(centre_x + radius); ++pixel_x) {
                if (pixel_x < 0 || pixel_x >= int(kScreenWidth) || pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
                if (std::hypot(float(pixel_x) - centre_x, float(pixel_y) - centre_y) > radius) continue;
                darken(pixel_x, pixel_y, alpha);
            }
    };
    const auto ring = [&](float centre_x, float centre_y, float radius, bool faint = false) {
        for (int pixel_y = int(centre_y - radius - 1); pixel_y <= int(centre_y + radius + 1); ++pixel_y)
            for (int pixel_x = int(centre_x - radius - 1); pixel_x <= int(centre_x + radius + 1); ++pixel_x)
                if (pixel_x >= 0 && pixel_x < int(kScreenWidth) && pixel_y >= 0 && pixel_y < int(kScreenHeight) &&
                    std::abs(std::hypot(float(pixel_x) - centre_x, float(pixel_y) - centre_y) - radius) < 1.5f) {
                    auto* pixel = &framebuffer[(std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)) * 4];
                    pixel[0] = faint ? 120 : 199; pixel[1] = faint ? 108 : 179; pixel[2] = faint ? 72 : 119;
                }
    };
    const auto button = [&](touch::Box box, const std::string& text, bool active = false, bool round = false, bool gold = false) {
        for (int pixel_y = int(box.y); pixel_y < int(box.y + box.height); ++pixel_y)
            for (int pixel_x = int(box.x); pixel_x < int(box.x + box.width); ++pixel_x) {
                if (pixel_x < 0 || pixel_x >= int(kScreenWidth) || pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
                auto* pixel = &framebuffer[(std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)) * 4];
                const float radius_x = box.width / 2, radius_y = box.height / 2;
                const float ellipse = std::hypot((float(pixel_x) - box.x - radius_x) / radius_x, (float(pixel_y) - box.y - radius_y) / radius_y);
                if (round && ellipse > 1) continue;
                const bool edge = round ? ellipse > .92f && ellipse < 1.02f : pixel_x == int(box.x) || pixel_y == int(box.y) || pixel_x == int(box.x + box.width) - 1 || pixel_y == int(box.y + box.height) - 1;
                if (edge) {
                    pixel[0] = gold ? 220 : 199; pixel[1] = gold ? 185 : 179; pixel[2] = gold ? 72 : 119;
                } else {
                    const float dim = round ? .22f : .5f;
                    pixel[0] = std::uint8_t(pixel[0] * dim); pixel[1] = std::uint8_t(pixel[1] * dim); pixel[2] = std::uint8_t(pixel[2] * dim);
                }
            }
        std::string label = text;
        while (!label.empty() && font.measure(label) > int(box.width) - 8) label.pop_back();
        const int label_x = round ? int(box.x + (box.width - font.measure(label)) / 2) : int(box.x) + 4;
        const int label_y = round ? int(box.y + box.height * .62f) - 6 : int(box.y) + 12;
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, palette, label_x, label_y, label,
                         gold ? 255 : active ? 255 : 199, gold ? 220 : active ? 220 : 179, gold ? 100 : 119);
    };
    button(touch::placed(touch::kDone), touch_assign != skillpad::kUnset || skillbar.picking == 3 ? "Done" : pad_field() ? "..." : "Back", false, false);
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
        font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 12, 118, "Skills . Done closes");
        if (touch_selected >= 0) {
            const int assign_x = touch::kTouchTreeRight - 80;
            button({ float(assign_x), 545, 240, 50 }, "Assign to touch button");
        }
        return;
    }
    if (!pad_field()) {
        if (inv_open) font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 12, 36, "Inventory . Done closes");
        font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 300, 75,
            identify_with >= 0 ? "Tap an unidentified item . Back cancels" : touch_item_hint);
        return;
    }
    const auto found = touch_sheets.find(sheet_key(character.name));
    for (int index = 0; index < 4; ++index) {
        const int skill = found == touch_sheets.end() ? skillpad::kEmpty : found->second.slot(touch_input.alternate, index);
        touch_previews[std::size_t(index)] = resolve_skill(touch_player, skill);
    }
    const touch::Layout chrome = touch::layout_boxes();
    if (touch_compact_field()) {
        using namespace d2d::d2s;
        const auto& stats = character.stats;
        const int life = int(stats.values[kLife] >> 8), max_life = std::max(1, int(stats.values[kMaxLife] >> 8));
        const int mana = int(stats.values[kMana] >> 8), max_mana = std::max(1, int(stats.values[kMaxMana] >> 8));
        const int bar_x = int(chrome.menu.x), bar_y = 118;
        const int bar_w = 118, bar_h = 8;
        const auto bar = [&](int y, int cur, int max, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
            const int fill = std::clamp(cur * bar_w / max, 0, bar_w);
            for (int pixel_y = y; pixel_y < y + bar_h; ++pixel_y)
                for (int pixel_x = bar_x; pixel_x < bar_x + bar_w; ++pixel_x) {
                    if (pixel_x < 0 || pixel_y < 0 || pixel_x >= int(kScreenWidth) || pixel_y >= int(kScreenHeight)) continue;
                    auto* pixel = &framebuffer[(std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)) * 4];
                    const bool edge = pixel_x == bar_x || pixel_y == y || pixel_x == bar_x + bar_w - 1 || pixel_y == y + bar_h - 1;
                    const bool full = pixel_x < bar_x + fill;
                    if (edge) {
                        pixel[0] = 199; pixel[1] = 179; pixel[2] = 119;
                    } else if (full) {
                        pixel[0] = red; pixel[1] = green; pixel[2] = blue;
                    } else {
                        pixel[0] = std::uint8_t(pixel[0] * .25f); pixel[1] = std::uint8_t(pixel[1] * .25f); pixel[2] = std::uint8_t(pixel[2] * .25f);
                    }
                }
            font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, bar_x, y - 12,
                      (y == bar_y ? "Life " : "Mana ") + std::to_string(cur) + "/" + std::to_string(max));
        };
        bar(bar_y, life, max_life, 180, 40, 40);
        bar(bar_y + 20, mana, max_mana, 40, 40, 200);
    }
    button(chrome.menu, "Inv", false, true);
    button(chrome.skills_bar, "Skl", false, true);
    if (scene->ctrl_panel.frames_per_direction() > 3) {
        const auto& mini = scene->ctrl_panel.frame(0, 3);
        blit_sprite(framebuffer, mini, palette, int(chrome.map.x) - 2, int(chrome.map.y) - 6);
    }
    button(chrome.map, automap.open ? "M*" : "Map", false, true);
    const touch::Box move_pad = chrome.move;
    const float stick_x = move_pad.x + move_pad.width * .5f;
    const float stick_y = move_pad.y + move_pad.height * .55f;
    halo(stick_x, stick_y, touch::kStickOuter + 6);
    ring(stick_x, stick_y, touch::kStickOuter);
    ring(stick_x, stick_y, touch::kStickInner, true);
    ring(stick_x + touch_input.move_x * 38, stick_y + touch_input.move_y * 38, 18);
    for (int index = 0; index < 4; ++index) {
        const touch::Box belt = chrome.belts[std::size_t(index)];
        halo(belt.x + belt.width * .5f, belt.y + belt.height * .5f, belt.width * .55f, 90);
        button(belt, std::to_string(index + 1), false, true);
    }
    const touch::Box attack_box = chrome.attack;
    halo(attack_box.x + attack_box.width * .5f, attack_box.y + attack_box.height * .5f, attack_box.width * .55f);
    button(attack_box, touch_input.attack_down ? "!" : "Atk", touch_input.attack_down, true);
    {
        const int cls = std::max(character.character_class, 0);
        if (cls >= 0 && cls < 7 && scene->skill_icons[std::size_t(cls)].frames_per_direction() > 0)
            blit_sprite(framebuffer, scene->skill_icons[std::size_t(cls)].frame(0, 0), palette,
                        int(attack_box.x) + 30, int(attack_box.y) + 30);
    }
    button(chrome.page, touch_input.alternate ? "II" : "I", false, true);
    if (touch_interact_visible(touch_interact_pick)) {
        const touch::Box& interact = chrome.interact;
        halo(interact.x + interact.width * .5f, interact.y + interact.height * .5f, interact.width * .62f, 130);
        ring(interact.x + interact.width * .5f, interact.y + interact.height * .5f, interact.width * .48f, true);
        button(interact, touch_interact_label(*this, touch_interact_pick), true, true, true);
    }
    for (int index = 0; index < 4; ++index) {
        const auto& target = touch_previews[std::size_t(index)];
        const auto* info = scene->skills.get(target.skill);
        const touch::Box skill_box = chrome.skills[std::size_t(index)];
        const float skill_cx = skill_box.x + skill_box.width * .5f, skill_cy = skill_box.y + skill_box.height * .5f;
        halo(skill_cx, skill_cy, touch::kSkillRing + 10, 90);
        ring(skill_cx, skill_cy, touch::kSkillRing, true);
        const std::string skill_label = info ? (info->name.size() > 6 ? info->name.substr(0, 5) + "." : info->name) :
                                              target.skill < -2 ? std::string(skillpad::action_label(target.skill, false)) : "-";
        button(skill_box, skill_label, info && info->id == view.aura, true);
        if (info) {
            int icon_class = -1;
            for (int column = 0; column < 7; ++column)
                if (info->cls == d2d::rules::kClassCode[std::size_t(column)]) icon_class = column;
            const auto& art = icon_class >= 0 ? scene->skill_icons[std::size_t(icon_class)] : scene->generic_skill_icons;
            if (info->icon >= 0 && std::uint32_t(info->icon) < art.frames_per_direction())
                blit_sprite(framebuffer, art.frame(0, std::uint32_t(info->icon)), palette,
                            int(skill_box.x) + 24, int(skill_box.y) + 18);
        }
    }
    if (view.aura > 0) {
        const auto* aura = scene->skills.get(view.aura);
        if (aura) font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, int(chrome.page.x), int(chrome.page.y) - 14, aura->name);
    }
    if (touch_attack_pick.kind == PadPick::kMonster) {
        const auto found = std::ranges::find(view.monsters, touch_attack_pick.id, &Monster::id);
        if (found != view.monsters.end()) {
            const float mx = found->unit.x, my = found->unit.y;
            const int sx = int(kScreenWidth) / 2 + int((mx - cam_x - my + cam_y) * kIsoW / 2);
            const int sy = kViewY + int((mx - cam_x + my - cam_y) * kIsoH / 2);
            ring(float(sx), float(sy), 14, false);
        }
    }
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
