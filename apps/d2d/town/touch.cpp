// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in touch prototype: separate per-character destinations, shared skill
// eligibility, targeting and gameplay commands. No platform code in the World.
#include "town.hpp"

#include "common.hpp"
#include "cursor.hpp"
#include "gamepad_map.hpp"
#include "hold_confirm.hpp"
#include "item_use.hpp"
#include "pad_hold.hpp"
#include "panels.hpp"
#include "skillpad.hpp"
#include "skilltree.hpp"
#include "store.hpp"
#include "touch_input.hpp"

#include <inventory.hpp>

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

auto Town::touch_hides_classic_hud() const -> bool {
    return touch_enabled && !touch::g_touch_classic_hud;
}

auto Town::touch_shows_field_controls() const -> bool {
    return touch_hides_classic_hud() && pad_field();
}

auto Town::touch_compact_field() const -> bool {
    return touch_shows_field_controls();
}

auto Town::touch_store_panel_x() const -> int {
    return kCharPanelX;
}

void Town::touch_rebuild_bind_wheel() {
    wheel_actions = false;
    rebuild_wheel();
    touch_bind_wheel_at = wheel_at;
    touch_bind_wheel_page = wheel_at / skillpad::kWheelShown * skillpad::kWheelShown;
}

void Town::touch_open_bind() {
    const bool page = touch_input.alternate;
    touch_bind = TouchBindPhase::PickSlot;
    touch_bind_slot = -1;
    touch_bind_finger = -1;
    touch_input.clear();
    touch_input.alternate = page;
    touch_feedback = "Tap a skill button";
    touch_rebuild_bind_wheel();
}

void Town::touch_close_bind() {
    touch_bind = TouchBindPhase::Off;
    touch_bind_slot = -1;
    touch_bind_finger = -1;
    touch_feedback.clear();
    save_touch_sheet(character.name);
}

Scene::InvLayout Town::touch_inv_layout() const {
    const int cls = std::max(character.character_class, 0);
    if (!scene) return {};
    return scene->inv_layout[std::size_t(cls)];
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
    if (phase == touch::Phase::Down) ++touch_fingers;
    else if (phase == touch::Phase::Up || phase == touch::Phase::Cancel) {
        touch_fingers = std::max(0, touch_fingers - 1);
        touch_input.release_finger(finger);
    }
    const touch::Layout chrome = touch::layout_boxes();
    if (pad_field()) touch_interact_pick = pad_pick(touch_player, view.player, true, {}, false);
    if (speech.npc >= 0 || npc_menu.npc >= 0) {
        if (phase == touch::Phase::Down && touch_hides_classic_hud() && chrome.map.contains(x, y)) {
            touch_input.clear();
            automap.open = !automap.open;
            if (automap.open) { automap.mini = 0; automap.fade = 2; automap.panel_side = 0; automap.covered = false; }
            return true;
        }
        if (phase == touch::Phase::Down && chrome.done.contains(x, y)) {
            touch_input.clear();
            touch_item_finger = -1;
            focus_keys.push_back(SDLK_ESCAPE);
            return true;
        }
        return false;
    }
    if (phase == touch::Phase::Down && chrome.bind.contains(x, y)) {
        if (touch_bind != TouchBindPhase::Off) {
            touch_input.clear(); touch_item_finger = -1; touch_ground_finger = -1;
            touch_close_bind();
        } else {
            touch_open_bind();
        }
        return true;
    }
    if (touch_bind == TouchBindPhase::Off && phase == touch::Phase::Down && chrome.quest.contains(x, y)) {
        touch_input.clear();
        touch_sheet = {};
        toggle_quest_log();
        return true;
    }
    if (touch_bind == TouchBindPhase::Off && phase == touch::Phase::Down && chrome.menu.contains(x, y)) {
        touch_input.clear();
        touch_sheet = {};
        inv_open = true;
        tree_open = false;
        return true;
    }
    if (touch_bind == TouchBindPhase::Off && phase == touch::Phase::Down && chrome.skills_bar.contains(x, y)) {
        touch_input.clear();
        touch_sheet = {};
        if (store.npc >= 0) send(cmd::CloseTrade{});
        tree_open = true;
        inv_open = false;
        return true;
    }
    if (phase == touch::Phase::Down && chrome.done.contains(x, y)) {
        touch_input.clear(); touch_item_finger = -1; touch_ground_finger = -1;
        if (touch_bind != TouchBindPhase::Off) { touch_close_bind(); return true; }
        if (touch_assign != skillpad::kUnset) { touch_assign = skillpad::kUnset; tree_open = touch_assignment_from_tree; touch_assignment_from_tree = false; return true; }
        focus_keys.push_back(SDLK_ESCAPE);
        return true;
    }
    if (touch_bind != TouchBindPhase::Off && phase == touch::Phase::Down && chrome.page.contains(x, y)) {
        touch_input.alternate = !touch_input.alternate;
        return true;
    }
    if (touch_bind == TouchBindPhase::PickSkill) {
        constexpr float wheel_cx = 380.f, wheel_cy = 270.f;
        const int count = std::min(skillpad::kWheelShown, int(wheel_ids.size()));
        if (phase == touch::Phase::Down) {
            touch_bind_finger = finger;
            touch_bind_origin_x = x - wheel_cx;
            touch_bind_origin_y = y - wheel_cy;
            return true;
        }
        if (finger == touch_bind_finger) {
            if (phase == touch::Phase::Move && count > 0)
                touch_bind_wheel_at = skillpad::stable_wedge(x - wheel_cx, y - wheel_cy, count, touch_bind_wheel_at);
            if (phase == touch::Phase::Up && touch_bind_slot >= 0 && count > 0) {
                const int pick = std::clamp(touch_bind_wheel_at, 0, int(wheel_ids.size()) - 1);
                const int chosen = wheel_ids[std::size_t(pick)];
                auto [found, inserted] = touch_sheets.try_emplace(sheet_key(character.name));
                if (inserted) found->second.slots.fill(skillpad::kEmpty);
                found->second.set_slot(touch_input.alternate, touch_bind_slot, chosen);
                save_touch_sheet(character.name);
                const auto* info = scene ? scene->skills.get(chosen) : nullptr;
                touch_feedback = std::to_string(touch_bind_slot + 1) + " . " + (info ? info->name : std::string(skillpad::action_label(chosen, true)));
                touch_bind = TouchBindPhase::PickSlot;
                touch_bind_finger = -1;
                touch_feedback = "Tap another button, I/II, or Done";
            }
            return true;
        }
        return true;
    }
    if (touch_bind == TouchBindPhase::PickSlot) {
        if (phase == touch::Phase::Down) {
            for (int index = 0; index < 4; ++index)
                if (chrome.skills[std::size_t(index)].contains(x, y)) {
                    touch_bind_slot = index;
                    touch_rebuild_bind_wheel();
                    touch_bind = TouchBindPhase::PickSkill;
                    touch_feedback = "Drag or tap a skill on the wheel";
                    return true;
                }
        }
        return true;
    }
    if (touch_assign != skillpad::kUnset) {
        if (phase == touch::Phase::Down && chrome.menu.contains(x, y)) { touch_assign = skillpad::kInventory; return true; }
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
        const auto follow_pointer = [&](bool press, bool release) {
            touch_pointer.active = !release;
            touch_pointer.press = press;
            touch_pointer.release = release;
            touch_pointer.x = x;
            touch_pointer.y = y;
        };
        const auto close_item_track = [&] {
            touch_item_finger = -1; touch_item = touch_stock = -1; touch_item_drag = false; touch_hold.clear();
        };
        const auto run_sheet_action = [&](touch::ItemAction action) {
            if (action == touch::ItemAction::Buy && touch_stock >= 0 && store.npc >= 0) send(cmd::Buy{ touch_stock, store.tab });
            else if (action == touch::ItemAction::Sell && touch_item >= 0) send(cmd::Sell{ touch_item });
            else if (action == touch::ItemAction::Use && touch_item >= 0) {
                const auto found = std::ranges::find(character.items, touch_item, &d2d::d2s::Item::id);
                if (found == character.items.end()) return;
                const auto used = item_use(*found);
                if (used.kind == ItemUseKind::Identify) identify_with = found->id;
                else if (used.kind != ItemUseKind::None) send(cmd::UseItem{ found->id });
            }
        };
        if (touch_sheet.open) {
            if (phase == touch::Phase::Down) {
                const auto hit = touch_sheet.hit(x, y);
                if (hit == touch::ItemAction::Cancel || hit == touch::ItemAction::None) {
                    if (hit == touch::ItemAction::Cancel || !touch_sheet.contains(x, y)) { touch_sheet = {}; close_item_track(); }
                    return true;
                }
                if (hit == touch::ItemAction::Confirm) {
                    if (touch_sheet.chosen != touch::ItemAction::None) {
                        run_sheet_action(touch_sheet.chosen);
                        touch_sheet = {};
                        close_item_track();
                    }
                    return true;
                }
                if (hit == touch_sheet.chosen && touch_sheet.confirming) {
                    run_sheet_action(hit);
                    touch_sheet = {};
                    close_item_track();
                    return true;
                }
                touch_sheet.chosen = hit;
                touch_sheet.confirming = true;
                touch_item_hint = "Tap Confirm";
                return true;
            }
            return phase != touch::Phase::Move;
        }
        if (held) {
            follow_pointer(phase == touch::Phase::Down, phase == touch::Phase::Up || phase == touch::Phase::Cancel);
            return true;
        }
        if (phase == touch::Phase::Cancel && finger == touch_item_finger) {
            close_item_track();
            touch_pointer = {};
            return true;
        }
        if (finger == touch_item_finger && (phase == touch::Phase::Move || phase == touch::Phase::Up)) {
            if (!touch_item_drag && touch::item_dragged(touch_item_x, touch_item_y, x, y) && scene) {
                touch_item_drag = true;
                touch_hold.clear();
                const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                                       { inv_open, stash_open, cube_open, belt_open, character.expansion },
                                                       int(touch_item_x), int(touch_item_y));
                if (click.cmd) send(*click.cmd);
                follow_pointer(true, false);
            }
            if (touch_item_drag) {
                follow_pointer(false, phase == touch::Phase::Up);
                if (phase == touch::Phase::Up) close_item_track();
                return true;
            }
            if (phase == touch::Phase::Move) return true;
            const d2d::d2s::Item* bag_item = nullptr;
            if (touch_item >= 0) {
                const auto found_item = std::ranges::find(character.items, touch_item, &d2d::d2s::Item::id);
                if (found_item != character.items.end()) bag_item = &*found_item;
            }
            const auto action = bag_item ? item_use(*bag_item) : ItemUse{};
            const bool can_use = bag_item && !action.label.empty() && action.kind != ItemUseKind::None;
            const bool can_sell = bag_item && store.npc >= 0 && scene && d2d::rules::sellable(scene->rules, *bag_item)
                                  && !store.gamble && store.mode != 1 && store.mode != 3;
            const bool can_buy = touch_stock >= 0 && store.npc >= 0;
            touch_item_finger = -1;
            touch_hold.clear();
            touch_sheet = touch::make_item_sheet(can_use, can_sell, can_buy);
            if (!touch_sheet.open) {
                close_item_track();
                return true;
            }
            touch_item_hint = "Tap an action, then Confirm";
            return true;
        }
        if (phase == touch::Phase::Down && scene && identify_with < 0 && !held) {
            touch_stock = store.npc >= 0 ? store_item_at(*scene, store, int(x), int(y), touch_store_panel_x()) : -1;
            const auto* item = bag_at(int(x), int(y));
            const auto action = item ? item_use(*item) : ItemUse{};
            const bool can_sell = item && store.npc >= 0 && d2d::rules::sellable(scene->rules, *item);
            if (touch_stock >= 0 || (item && (!action.label.empty() || can_sell))) {
                touch_item_finger = finger; touch_item = item ? item->id : -1;
                touch_item_start = now_ms; touch_sold = false; touch_item_drag = false;
                touch_hold.clear();
                touch_item_x = x; touch_item_y = y;
                touch_item_hint = touch_stock >= 0 ? "Tap to buy, drag to inspect" : std::string(action.label);
                if (can_sell) touch_item_hint = "Tap for Use / Sell, drag to move";
                return true;
            }
        }
        if (phase == touch::Phase::Down && chrome.map.contains(x, y)) {
            touch_input.clear();
            automap.open = !automap.open;
            if (automap.open) { automap.mini = 0; automap.fade = 2; automap.panel_side = 0; automap.covered = false; }
            return true;
        }
        if (tree_open && scene) {
            const int tree_panel_right = kTreeR;
            const int assign_x = 16;
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
    if (phase == touch::Phase::Down && chrome.map.contains(x, y)) {
        touch_input.clear();
        automap.open = !automap.open;
        if (automap.open) {
            automap.mini = 0;
            automap.fade = 2;
            automap.panel_side = 0;
            automap.covered = false;
        }
        return true;
    }
    if (pad_field()) {
        if (phase == touch::Phase::Down && touch_interact_visible(touch_interact_pick) && chrome.interact.contains(x, y)) {
            if (touch_interact_pick.kind == PadPick::kItem) send(cmd::Pickup{ touch_interact_pick.id });
            else if (touch_interact_pick.kind == PadPick::kThing) send(cmd::Interact{ touch_interact_pick.id });
            return true;
        }
        if (phase == touch::Phase::Down && !touch::chrome_hit(x, y, touch_interact_visible(touch_interact_pick), true)) {
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
            if (chrome.skills[std::size_t(index)].contains(x, y)) {
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

void Town::drive_touch_hold(float seconds) {
    touch_hold_fraction = 0.f;
    if (!touch_enabled || touch_item_finger < 0 || touch_sold || !scene || touch_sheet.open || touch_item_drag) {
        if (touch_item_finger < 0) touch_hold.clear();
        return;
    }
    const auto found = std::ranges::find(character.items, touch_item, &d2d::d2s::Item::id);
    const d2d::d2s::Item* item = found != character.items.end() ? &*found : nullptr;
    if (!item) return;
    PadHoldQuery query;
    query.shop = store.npc >= 0;
    query.gamble = store.gamble;
    query.identify = identify_with >= 0;
    query.imbue = use_item.open();
    query.hand_empty = !held;
    query.bag = true;
    query.store_mode = store.mode;
    if (world.game_data) {
        query.sellable = d2d::rules::sellable(scene->rules, *item);
        query.potion = potion_code(item->code) || scene->rules.potions.contains(item->code);
        const int save_cls = std::max(character.character_class, 0);
        const int level_needed = d2d::game::required_level(*world.game_data, *item, save_cls);
        const auto worn_by = d2d::game::wearer(*world.game_data, save_cls, character.items, character.stats);
        query.equippable = d2d::rules::bag_equip_slot(scene->rules, character.items, held, item->id, worn_by, level_needed) >= 0;
    }
    query.equippable = pad_hold_can_equip(net_game != nullptr, query.equippable);
    const PadHoldKind kind = pad_hold_kind(query);
    if (kind == PadHoldKind::Sell) touch_hold.delay = pad::kHoldSellSeconds;
    else if (kind == PadHoldKind::Equip) touch_hold.delay = pad::kHoldEquipSeconds;
    else touch_hold.delay = pad::kHoldFillSeconds;
    const bool eligible = kind != PadHoldKind::None && inv_open;
    const hold_confirm::Step step = touch_hold.step(false, true, eligible, item->id, int(kind), seconds);
    if (touch_hold.armed && !touch_hold.cancelled) touch_hold_fraction = touch_hold.fraction();
    if (step != hold_confirm::Step::Act) return;
    if (kind == PadHoldKind::Sell) { send(cmd::Sell{ item->id }); touch_sold = true; }
    else if (kind == PadHoldKind::Equip) send(cmd::EquipFromBag{ item->id });
    else if (kind == PadHoldKind::Fill || kind == PadHoldKind::FillOne) {
        auto trial = character.items;
        const int boxes = scene->belts[std::size_t(belt_index(*scene, character.items))].boxes;
        std::vector<int> moved;
        const auto try_item = [&](int carried_id) {
            if (d2d::rules::auto_belt(scene->rules, trial, carried_id, boxes)) moved.push_back(carried_id);
        };
        const auto is_potion = [&](std::string_view code) {
            return potion_code(code) || scene->rules.potions.contains(std::string(code));
        };
        try_item(item->id);
        if (kind == PadHoldKind::Fill)
            for (const auto& carried : character.items)
                if (carried.id != item->id && is_potion(carried.code)) try_item(carried.id);
        for (const int carried_id : moved) send(cmd::AutoBelt{ carried_id });
    }
    touch_sold = kind == PadHoldKind::Sell;
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
        touch_item_finger = -1; touch_item_drag = false; touch_sheet = {}; touch_pointer = {}; touch_panel_state = panel_state;
    }
    if (!pad_field() || view.dead) {
        const Send stop = [this](const Command& command) { send(command); };
        pad_walk(touch_player, view.player, view.dead, false, stop);
        touch_input.clear(); return;
    }
    if (touch_input.menu) { touch_input.clear(); inv_open = true; tree_open = false; return; }
    const Send out = [this](const Command& command) { send(command); };
    if (!touch_input.move_held()) touch_input.move_x = touch_input.move_y = 0;
    const float move_len = std::hypot(touch_input.move_x, touch_input.move_y);
    touch_player.move = { touch_input.move_x, touch_input.move_y, touch_input.move_held() && move_len > 0.12f };
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
    touch::Layout chrome = touch::layout_boxes();
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
    const auto button = [&](touch::Box box, const std::string& text, bool active = false, bool round = false, bool gold = false, bool label = true) {
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
        if (!label || text.empty()) return;
        std::string clipped = text;
        while (!clipped.empty() && font.measure(clipped) > int(box.width) - 8) clipped.pop_back();
        const int label_x = round ? int(box.x + (box.width - font.measure(clipped)) / 2) : int(box.x) + 4;
        const int label_y = round ? int(box.y + box.height * .62f) - 6 : int(box.y) + 12;
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, palette, label_x, label_y, clipped,
                         gold ? 255 : active ? 255 : 199, gold ? 220 : active ? 220 : 179, gold ? 100 : 119);
    };
    const auto blit_skill_icon = [&](int skill_id, int pixel_x, int pixel_y) {
        const auto* info = scene->skills.get(skill_id);
        if (!info) return;
        int icon_class = -1;
        for (int column = 0; column < 7; ++column)
            if (info->cls == d2d::rules::kClassCode[std::size_t(column)]) icon_class = column;
        const auto& art = icon_class >= 0 ? scene->skill_icons[std::size_t(icon_class)] : scene->generic_skill_icons;
        if (info->icon >= 0 && std::uint32_t(info->icon) < art.frames_per_direction())
            blit_sprite(framebuffer, art.frame(0, std::uint32_t(info->icon)), palette, pixel_x, pixel_y);
    };
    const auto blit_skill_icon_centered = [&](const touch::Box& box, int skill_id) {
        const auto* info = scene->skills.get(skill_id);
        if (!info || info->icon < 0) return;
        int icon_class = -1;
        for (int column = 0; column < 7; ++column)
            if (info->cls == d2d::rules::kClassCode[std::size_t(column)]) icon_class = column;
        const auto& art = icon_class >= 0 ? scene->skill_icons[std::size_t(icon_class)] : scene->generic_skill_icons;
        if (std::uint32_t(info->icon) >= art.frames_per_direction()) return;
        const auto& frame = art.frame(0, std::uint32_t(info->icon));
        blit_sprite(framebuffer, frame, palette, int(box.x + (box.width - frame.width) * .5f), int(box.y + (box.height - frame.height) * .5f));
    };
    const auto draw_left_chips = [&] {
        button(chrome.menu, "Inv", false, true);
        button(chrome.skills_bar, "Skl", false, true);
        button(chrome.quest, quest_log.open ? "Q*" : "Quest", false, true);
        button(chrome.bind, touch_bind != TouchBindPhase::Off ? "Done" : "Bind", touch_bind != TouchBindPhase::Off, true);
    };
    const auto draw_sheet = [&] {
        if (!touch_sheet.open) return;
        if (touch_sheet.show_use) button(touch_sheet.use, "Use", touch_sheet.chosen == touch::ItemAction::Use);
        if (touch_sheet.show_sell) button(touch_sheet.sell, "Sell", touch_sheet.chosen == touch::ItemAction::Sell);
        if (touch_sheet.show_buy) button(touch_sheet.buy, "Buy", touch_sheet.chosen == touch::ItemAction::Buy);
        button(touch_sheet.confirm, "Confirm", touch_sheet.confirming);
        button(touch_sheet.cancel, "Cancel");
    };
    button(chrome.done, touch_bind != TouchBindPhase::Off || touch_assign != skillpad::kUnset || skillbar.picking == 3 ? "Done"
                      : pad_field() ? "..." : "Back", false, false);
    if (touch_bind != TouchBindPhase::Off) {
        draw_left_chips();
        button(chrome.map, automap.open ? "M*" : "Map", false, true);
        const auto bound = touch_sheets.find(sheet_key(character.name));
        for (int index = 0; index < 4; ++index) {
            const touch::Box& skill_box = chrome.skills[std::size_t(index)];
            const float cx = skill_box.x + skill_box.width * .5f, cy = skill_box.y + skill_box.height * .5f;
            halo(cx, cy, skill_box.width * .56f, 120);
            ring(cx, cy, skill_box.width * .52f, index == touch_bind_slot);
            const int id = bound == touch_sheets.end() ? skillpad::kEmpty : bound->second.slot(touch_input.alternate, index);
            const auto* info = scene->skills.get(id);
            if (info && info->icon >= 0) blit_skill_icon_centered(skill_box, id);
            else button(skill_box, std::to_string(index + 1), false, true, false, info == nullptr);
        }
        if (touch_bind == TouchBindPhase::PickSkill && !wheel_ids.empty()) {
            constexpr float wheel_cx = 380.f, wheel_cy = 270.f;
            const int count = std::min(skillpad::kWheelShown, int(wheel_ids.size()));
            const skillpad::WheelPage page{ touch_bind_wheel_page, count };
            for (int slot = 0; slot < page.shown; ++slot) {
                const int index = std::clamp(page.start + slot, 0, int(wheel_ids.size()) - 1);
                const float angle = skillpad::wedge_center(slot, page.shown);
                const int icon_x = int(wheel_cx + std::cos(angle) * 110.f);
                const int icon_y = int(wheel_cy + std::sin(angle) * 90.f);
                const int id = wheel_ids[std::size_t(index)];
                if (index == touch_bind_wheel_at) ring(float(icon_x), float(icon_y), 22.f, false);
                blit_skill_icon(id, icon_x - 12, icon_y - 12);
            }
            ring(wheel_cx, wheel_cy, 18.f, true);
        }
        button(chrome.page, touch_input.alternate ? "II" : "I", false, true);
        font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 280, 90, touch_feedback);
        return;
    }
    if (touch_assign != skillpad::kUnset) {
        button(chrome.menu, "Assign Inventory");
        button(chrome.page, touch_input.alternate ? "II" : "I", false, true);
        const auto found = touch_sheets.find(sheet_key(character.name));
        for (int index = 0; index < 4; ++index) {
            const int id = found == touch_sheets.end() ? -1 : found->second.slot(touch_input.alternate, index);
            const auto* info = scene->skills.get(id);
            const touch::Box& skill_box = chrome.skills[std::size_t(index)];
            const float cx = skill_box.x + skill_box.width * .5f, cy = skill_box.y + skill_box.height * .5f;
            halo(cx, cy, skill_box.width * .56f, 150);
            ring(cx, cy, skill_box.width * .52f, false);
            button(skill_box, std::to_string(index + 1) + ": " + (info ? info->name : "Empty"), false, true, true);
            if (info) blit_skill_icon(id, int(skill_box.x) + 24, int(skill_box.y) + 18);
        }
        const auto* info = scene->skills.get(touch_assign);
        font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 310, 170, "Assign: " + (info ? info->name : std::string(skillpad::action_label(touch_assign, true))));
        font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 310, 195, touch_feedback);
        return;
    }
    if (tree_open) {
        if (touch_hides_classic_hud()) {
            draw_left_chips();
            button(chrome.map, automap.open ? "M*" : "Map", false, true);
        }
        font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 12, 118, "Skills . Done closes");
        if (touch_selected >= 0) {
            button({ 16.f, 545.f, 240.f, 50.f }, "Assign to touch button");
        }
        return;
    }
    if (!pad_field()) {
        if (inv_open) font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 12, 36, "Inventory . Done closes");
        font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, 300, 75,
            identify_with >= 0 ? "Tap an unidentified item . Back cancels" : touch_item_hint);
        if (touch_hides_classic_hud()) {
            draw_left_chips();
            button(chrome.map, automap.open ? "M*" : "Map", false, true);
        }
        draw_sheet();
        if (touch_hold_fraction > 0.f && touch_item_finger >= 0)
            ring(touch_item_x, touch_item_y, 22.f + 18.f * touch_hold_fraction, false);
        return;
    }
    const auto found = touch_sheets.find(sheet_key(character.name));
    for (int index = 0; index < 4; ++index) {
        const int skill = found == touch_sheets.end() ? skillpad::kEmpty : found->second.slot(touch_input.alternate, index);
        touch_previews[std::size_t(index)] = resolve_skill(touch_player, skill);
    }
    if (touch_hides_classic_hud()) {
        using namespace d2d::d2s;
        const auto& stats = character.stats;
        const int life = int(stats.values[kLife] >> 8), max_life = std::max(1, int(stats.values[kMaxLife] >> 8));
        const int mana = int(stats.values[kMana] >> 8), max_mana = std::max(1, int(stats.values[kMaxMana] >> 8));
        const int bar_x = int(chrome.menu.x), bar_y = int(chrome.bind.y + chrome.bind.height + touch::kBarsBelowChip);
        const int bar_w = std::max(118, int(chrome.bind.x + chrome.bind.width - chrome.menu.x)), bar_h = 8;
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
        if (!quest_log.open) {
            struct QuestCard { std::string title, line; };
            std::array<QuestCard, 2> cards{};
            int count = 0;
            const int act = view_act();
            for (const auto& entry : kQuestLog) {
                if (entry.act != act || count >= 2) continue;
                const auto text = quest_text(quest_bits(), entry.quest, quest_state());
                if (text.shown != 3 && text.shown != 1) continue;
                QuestCard& card = cards[std::size_t(count++)];
                card.title = string_id(*scene, std::uint16_t(entry.name));
                if (text.string) {
                    const std::string body = string_id(*scene, std::uint16_t(text.string));
                    card.line = body.size() > 42 ? body.substr(0, 41) + "." : body;
                }
            }
            if (count > 0) {
                for (int pixel_y = int(chrome.quest_track.y); pixel_y < int(chrome.quest_track.y + chrome.quest_track.height); ++pixel_y)
                    for (int pixel_x = int(chrome.quest_track.x); pixel_x < int(chrome.quest_track.x + chrome.quest_track.width); ++pixel_x)
                        if (pixel_x >= 0 && pixel_y >= 0 && pixel_x < int(kScreenWidth) && pixel_y < int(kScreenHeight)) darken(pixel_x, pixel_y, 120);
                float card_y = chrome.quest_track.y + 6.f;
                for (int index = 0; index < count; ++index) {
                    font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, int(chrome.quest_track.x) + 6, int(card_y), cards[std::size_t(index)].title);
                    if (!cards[std::size_t(index)].line.empty())
                        font.draw(framebuffer, kScreenWidth, kScreenHeight, palette, int(chrome.quest_track.x) + 6, int(card_y) + 16, cards[std::size_t(index)].line);
                    card_y += 72.f;
                }
            }
        }
    }
    if (!touch_shows_field_controls()) {
        if (touch_hides_classic_hud()) draw_left_chips();
        button(chrome.map, automap.open ? "M*" : "Map", false, true);
        return;
    }
    draw_left_chips();
    button(chrome.map, automap.open ? "M*" : "Map", false, true);
    const touch::Box& move_pad = chrome.move_visual;
    const float stick_x = move_pad.x + move_pad.width * .5f;
    const float stick_y = move_pad.y + move_pad.height * .55f;
    halo(stick_x, stick_y, touch::kStickOuter + 4, 40);
    ring(stick_x, stick_y, touch::kStickOuter, true);
    ring(stick_x, stick_y, touch::kStickInner, true);
    ring(stick_x + touch_input.move_x * 32, stick_y + touch_input.move_y * 32, 16);
    for (int index = 0; index < 4; ++index) {
        const touch::Box belt = chrome.belts[std::size_t(index)];
        const d2d::d2s::Item* belt_item = nullptr;
        for (const auto& item : character.items)
            if (item.location == d2d::d2s::item_location::kBelt && item.column == index) { belt_item = &item; break; }
        halo(belt.x + belt.width * .5f, belt.y + belt.height * .5f, belt.width * .48f, 70);
        const bool has_icon = belt_item && scene->item_sprite(*belt_item);
        button(belt, has_icon ? "" : std::to_string(index + 1), false, true, false, !has_icon);
        if (belt_item) {
            if (const auto* spr = scene->item_sprite(*belt_item); spr && spr->frames_per_direction() > 0) {
                const auto& frame = spr->frame(0, 0);
                blit_sprite(framebuffer, frame, scene->item_pal(*belt_item, palette), int(belt.x) + 8, int(belt.y) + 8);
            }
        }
    }
    const touch::Box attack_box = chrome.attack;
    halo(attack_box.x + attack_box.width * .5f, attack_box.y + attack_box.height * .5f, attack_box.width * .46f);
    button(attack_box, touch_input.attack_down ? "!" : "", touch_input.attack_down, true, false, touch_input.attack_down);
    blit_skill_icon_centered(attack_box, skill_known(character.name, 0) ? 0 : std::max(0, int(character.header.left_skill)));
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
        halo(skill_cx, skill_cy, skill_box.width * .46f, 70);
        ring(skill_cx, skill_cy, touch::kSkillRing, true);
        const bool has_icon = info && info->icon >= 0;
        if (!has_icon) {
            const std::string skill_label = info ? (info->name.size() > 6 ? info->name.substr(0, 5) + "." : info->name) :
                                                  target.skill < -2 ? std::string(skillpad::action_label(target.skill, false)) : "-";
            button(skill_box, skill_label, info && info->id == view.aura, true);
        } else if (info && info->id == view.aura)
            ring(skill_cx, skill_cy, touch::kSkillRing + 2, false);
        if (info) blit_skill_icon_centered(skill_box, target.skill);
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
        button(chrome.cancel, "Drag here to cancel");
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
