// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in touch prototype: separate per-character destinations, shared skill
// eligibility, targeting and gameplay commands. No platform code in the World.
#include "input/touch/ui_sound.hpp"
#include "session.hpp"

#include "common.hpp"
#include "input/gamepad_map.hpp"
#include "input/hold_confirm.hpp"
#include "input/pad_hold.hpp"
#include "input/touch/binding.hpp"
#include "input/touch/feedback.hpp"
#include "input/touch/gestures.hpp"
#include "input/touch/input.hpp"
#include "input/touch/potions.hpp"
#include "input/touch/trade.hpp"
#include "items/cursor.hpp"
#include "items/item_use.hpp"
#include "items/use_item.hpp"
#include "panels/panels.hpp"
#include "panels/store.hpp"
#include "scene.hpp"
#include "skills/pad.hpp"
#include "skills/tree.hpp"
#include "ui.hpp"

#include <inventory.hpp>

#include <SDL3/SDL_keycode.h>

#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <item_gen.hpp>
#include <log.hpp>
#include <points.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <wear.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::client {

namespace {

template <typename Items>
touch::PotionSummary potion_summary(const Items& items) {
    std::vector<touch::PotionEntry> entries;
    entries.reserve(items.size());
    for (const auto& item : items)
        entries.push_back({ item.code, item.location == d2d::d2s::item_location::kBelt ? touch::PotionPlace::Belt : touch::PotionPlace::Other, item.column });
    return touch::summarize_touch_potions(entries);
}


/** True when the contextual interact chrome should accept touches and block field taps. */
bool touch_interact_visible(const ClientSession::PadPick& pick) {
    return pick.kind == ClientSession::PadPick::kItem || pick.kind == ClientSession::PadPick::kThing;
}


} // namespace

auto ClientSession::touch_hides_classic_hud() const -> bool {
    return touch::touch_hud_visible(touch_enabled, last_device, touch::g_touch_classic_hud);
}

auto ClientSession::touch_shows_field_controls() const -> bool {
    return touch::touch_interface_visible(touch_enabled, last_device) && pad_field();
}

auto ClientSession::touch_compact_field() const -> bool {
    return touch_shows_field_controls();
}

auto ClientSession::touch_store_panel_x() const -> int {
    return kCharPanelX;
}

void ClientSession::touch_rebuild_bind_wheel() {
    touch_bind_ids.clear();
    if (scene)
        for (const auto& info : scene->skills.rows)
            if (skillpad::assignable_name(info.name) && skill_known(character.name, info.id))
                touch_bind_ids.push_back(info.id);
    touch_bind_ids.insert(touch_bind_ids.end(), skillpad::kActions.begin(), skillpad::kActions.end());
    touch_bind_wheel_page = 0; touch_bind_wheel_at = -1;
    touch_bind_finger = -1; touch_bind_hit = false;
}

void ClientSession::touch_open_bind() {
    const bool page = touch_input.alternate;
    touch_bind = TouchBindPhase::PickSlot;
    touch_bind_slot = -1;
    touch_bind_finger = -1; touch_bind_hit = false;
    touch_input.clear();
    touch_input.alternate = page;
    touch_feedback = "Tap a skill button";
    touch_rebuild_bind_wheel();
}

void ClientSession::touch_close_bind() {
    touch_bind = TouchBindPhase::Off;
    touch_bind_slot = -1;
    touch_bind_finger = -1; touch_bind_hit = false;
    touch_feedback.clear();
}

bool ClientSession::touch_apply_bind(int slot, int skill) {
    const auto key = sheet_key(character.name);
    const auto found = touch_sheets.find(key);
    skillpad::Sheet sheet = found == touch_sheets.end() ? skillpad::Sheet{} : found->second;
    if (!touch::assign_binding(sheet, slot, skill, touch_input.alternate,
            [&](const auto& candidate) { return save_touch_sheet(character.name, candidate); })) {
        touch_feedback = "Could not save assignment";
        touch_input.feedback.pulse(std::size_t(std::clamp(slot, 0, touch::kAttackBind)), touch::FeedbackKind::Rejected, now_ms);
        return false;
    }
    touch_sheets.insert_or_assign(key, std::move(sheet));
    touch_input.feedback.pulse(std::size_t(slot), touch::FeedbackKind::Accepted, now_ms);
    touch_play_ui_click(touch::UiClick::Assignment, slot + (touch_input.alternate ? touch::kAttackBind + 1 : 0));
    return true;
}

void ClientSession::touch_play_ui_click(touch::UiClick action, int destination) {
    if (last_device == touch::LastDevice::Touch && g_on_button_press
        && touch_clicks.accept(action, destination, now_ms)) g_on_button_press();
}

int ClientSession::touch_bound_skill(int slot) const {
    const auto found = touch_sheets.find(sheet_key(character.name));
    if (slot == touch::kAttackBind) {
        if (found == touch_sheets.end()) return skill_known(character.name, 0) ? 0 : std::max(0, int(character.header.left_skill));
        return touch::trigger_binding(found->second, touch_input.alternate,
            skill_known(character.name, 0) ? 0 : std::max(0, int(character.header.left_skill)));
    }
    if (slot < 0 || slot >= touch::kSkillButtons || found == touch_sheets.end()) return skillpad::kEmpty;
    return found->second.slot(touch_input.alternate, slot);
}

Scene::InvLayout ClientSession::touch_inv_layout() const {
    const int cls = std::max(character.character_class, 0);
    if (!scene) return {};
    return scene->inv_layout[std::size_t(cls)];
}

/**
 * Load the named character touch assignments independently of controller positions. Remove the
 * cached sheet if its sidecar cannot be opened.
 */
void ClientSession::load_touch_sheet(const std::string& name) {
    if (key_dir.empty() || name.empty()) return;
    std::ifstream file(key_dir / (name + ".touchskills"));
    if (file) touch_sheets[sheet_key(name)] = skillpad::read(std::string{ std::istreambuf_iterator<char>(file), {} });
    else touch_sheets.erase(sheet_key(name));
}

/**
 * Persist a cached touch sheet through a checked temporary file and rename. Leave the original
 * destination in place on a failed write and report the failure.
 */
bool ClientSession::save_touch_sheet(const std::string& name, const skillpad::Sheet& sheet) const {
    if (name.empty()) return false;
    if (key_dir.empty()) return true;
    const auto path = key_dir / (name + ".touchskills");
    auto temporary = path; temporary += ".tmp";
    std::ofstream file(temporary);
    if (!file) { d2d::log::warn("Touch assignments could not be written"); return false; }
    file << skillpad::write(sheet);
    file.close();
    std::error_code error;
    if (file) std::filesystem::rename(temporary, path, error);
    if (!file || error) {
        d2d::log::warn("Touch assignments could not be written");
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

/**
 * Resolve a controller/touch skill preview for the selected owner without issuing a command. Manual
 * aim uses screen drag distance; self casts, eligible corpses and Teleport each apply their own
 * target policy.
 */

/**
 * Dispatch a committed skill preview after rechecking availability and player state. Panel
 * shortcuts open UI, portal bindings use a carried tome or loose scroll, and auras update the selected hand rather
 * than attacking a target.
 */
void ClientSession::cast_resolved(PadPlayer& player_pad, const touch::Target& target) {
    if (target.valid && skillpad::panel_index(target.skill) >= 0) {
        skill_strip = skillpad::panel_index(target.skill);
        const std::array panels{ &char_open, &inv_open, &tree_open, &automap.open, &quest_log.open };
        const std::array clicks{ touch::UiClick::Stats, touch::UiClick::Inventory, touch::UiClick::Skills, touch::UiClick::Map, touch::UiClick::Quest };
        const bool was_open = *panels[std::size_t(skill_strip)];
        const auto index = std::size_t(skill_strip);
        pad::Actions open; open.confirm = true; drive_strip(open);
        if (was_open != *panels[index]) touch_play_ui_click(clicks[index], *panels[index]);
        touch_input.clear();
        return;
    }
    if (!target.valid || view.dead) return;
    if (target.skill == skillpad::kInteract) {
        const auto pick = pad_pick(player_pad, view.player, true, {}, false);
        if (pick.kind == PadPick::kItem) {
            if (!open_loot_chooser(player_pad, view.player, true)) send(cmd::Pickup{ pick.id });
        } else if (pick.kind == PadPick::kThing) send(cmd::Interact{ pick.id });
        return;
    }
    if (!scene || !skill_known(character.name, target.skill)) return;
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
    } else {
        const auto cast = touch_stationary(target.skill) ? touch::caster_target(target.skill, view.player.x, view.player.y) : target;
        send(cmd::UseSkill{ cast.skill, cast.x, cast.y, cast.unit, cast.left, -1, cast.unit < 0 });
    }
}

/**
 * Route a logical touch event to assignment, inventory/panel interaction or field gestures. Return
 * true for consumed events; field gestures retain separate movement and skill ownership.
 */
bool ClientSession::touch_event(std::int64_t finger, touch::Phase phase, float x, float y) {
    if (!touch_enabled) return false;
    if (loot_chooser.open) {
        if (touch_get_hold.finger == finger && (phase == touch::Phase::Up || phase == touch::Phase::Cancel)) touch_get_hold.clear();
        if (!loot_chooser.touch || !pad_field() || view.dead) { loot_chooser.clear(); return true; }
        const float legacy_x = touch::to_legacy_x(x);
        const auto box = LootChooser::list_box();
        const touch::Box close{ box.x + box.width - 70.f, box.y - 48.f, 70.f, 44.f };
        if (phase == touch::Phase::Down && close.contains(legacy_x, y)) { loot_chooser.clear(); return true; }
        if (const int picked = loot_chooser.touch_event(finger, phase, legacy_x, y); picked >= 0) commit_loot(picked);
        return true;
    }
    if (touch_cancelled_item_finger == finger) {
        if (phase == touch::Phase::Down) touch_cancelled_item_finger = -1;
        else {
            if (phase == touch::Phase::Up || phase == touch::Phase::Cancel) {
                touch_cancelled_item_finger = -1; touch_fingers = std::max(0, touch_fingers - 1);
            }
            return true;
        }
    }
    if (!touch_input.belt_expanded && touch_sheet.belt_index >= 0) touch_sheet = {};
    if (phase == touch::Phase::Down) ++touch_fingers;
    else if (phase == touch::Phase::Up || phase == touch::Phase::Cancel)
        touch_fingers = std::max(0, touch_fingers - 1);
    if ((touch_stock.tab >= 0 && touch_stock.index(store) < 0) || (touch_sell_vendor >= 0 && (touch_sell_vendor != store.npc || touch_sell_tab != store.tab || store.gamble))) {
        cancel_touch_item(); return true;
    }
    // Paths that return before event() still drop this finger. event() sees the Up first,
    // so a skill release can cast before its owner is cleared.
    struct ReleaseUnlessForwarded {
        touch::Input& input;
        std::int64_t finger;
        touch::Phase phase;
        bool armed = true;
        ~ReleaseUnlessForwarded() {
            if (armed && (phase == touch::Phase::Up || phase == touch::Phase::Cancel)) input.release_finger(finger);
        }
    } release{ touch_input, finger, phase };
    const touch::Layout chrome = touch::layout_boxes();
    if (phase == touch::Phase::Down && (chrome.menu.contains(x, y) || chrome.skills_bar.contains(x, y)
        || chrome.stats.contains(x, y) || chrome.quest.contains(x, y) || chrome.bind.contains(x, y) || chrome.done.contains(x, y)))
        cancel_touch_item();
    const auto acknowledge = [&] {
        if (phase != touch::Phase::Down) return;
        const std::array boxes{ chrome.menu, chrome.skills_bar, chrome.stats, chrome.quest,
            chrome.bind, chrome.done, chrome.map, chrome.page, chrome.belt_more };
        for (std::size_t index = 0; index < boxes.size(); ++index)
            if ((store.npc < 0 || index >= 5) && boxes[index].contains(x, y)) {
                touch_input.feedback.pulse(index + 7, touch::FeedbackKind::Accepted, now_ms);
                break;
            }
    };
    struct Acknowledge {
        const decltype(acknowledge)& callback;
        ~Acknowledge() { callback(); }
    } acknowledge_on_exit{ acknowledge };
    if (touch_hides_classic_hud() && touch_shows_field_controls() && !view.dead && !loot_chooser.open
        && touch_bind == TouchBindPhase::Off && touch_assign == skillpad::kUnset && touch_effect_event(finger, phase, x, y)) return true;
    if (pad_field()) touch_interact_pick = pad_pick(touch_player, view.player, true, {}, false);
    if (speech.npc >= 0 || npc_menu.npc >= 0) {
        if (phase == touch::Phase::Down && touch::touch_interface_visible(touch_enabled, last_device) && chrome.map.contains(x, y)) {
            touch_input.clear();
            automap.open = !automap.open;
            touch_play_ui_click(touch::UiClick::Map, automap.open);
            if (automap.open) { automap.mini = 0; automap.fade = 2; automap.panel_side = 0; automap.covered = false; }
            return true;
        }
        if (phase == touch::Phase::Down && chrome.done.contains(x, y)) {
            touch_input.clear();
            touch_item_finger = -1;
            touch_play_ui_click(touch::UiClick::Back);
        focus_keys.push_back(SDLK_ESCAPE);
            return true;
        }
        return false;
    }
    if (store.npc < 0 && phase == touch::Phase::Down && chrome.bind.contains(x, y)) {
        if (touch_bind != TouchBindPhase::Off) {
            touch_input.clear(); touch_item_finger = -1; touch_ground_finger = -1;
            touch_close_bind();
        } else {
            touch_open_bind();
        }
        touch_play_ui_click(touch::UiClick::Bind, touch_bind != TouchBindPhase::Off);
        return true;
    }
    const bool on_quest_track = chrome.quest_track.contains(x, y) && touch_hides_classic_hud()
        && pad_field() && !quest_log.open;
    if (store.npc < 0 && touch_bind == TouchBindPhase::Off && phase == touch::Phase::Down
        && (chrome.quest.contains(x, y) || on_quest_track)) {
        touch_input.clear();
        touch_sheet = {};
        toggle_quest_log();
        touch_play_ui_click(touch::UiClick::Quest, quest_log.open);
        return true;
    }
    if (store.npc < 0 && touch_bind == TouchBindPhase::Off && touch_assign == skillpad::kUnset && phase == touch::Phase::Down && chrome.menu.contains(x, y)) {
        touch_input.clear();
        touch_sheet = {};
        const bool was_open = inv_open;
        toggle_inventory();
        if (was_open != inv_open) touch_play_ui_click(touch::UiClick::Inventory, inv_open);
        return true;
    }
    if (store.npc < 0 && touch_bind == TouchBindPhase::Off && phase == touch::Phase::Down && chrome.skills_bar.contains(x, y)) {
        touch_input.clear();
        touch_sheet = {};
        if (store.npc >= 0) send(cmd::CloseTrade{});
        const bool was_open = tree_open;
        toggle_skill_tree();
        if (was_open != tree_open) touch_play_ui_click(touch::UiClick::Skills, tree_open);
        return true;
    }
    if (store.npc < 0 && touch_bind == TouchBindPhase::Off && touch_assign == skillpad::kUnset
        && phase == touch::Phase::Down && chrome.stats.contains(x, y)) {
        touch_input.clear(); touch_sheet = {};
        if (store.npc >= 0) send(cmd::CloseTrade{});
        const bool was_open = char_open;
        toggle_character();
        if (char_open) inv_open = tree_open = false;
        if (was_open != char_open) touch_play_ui_click(touch::UiClick::Stats, char_open);
        return true;
    }
    if (phase == touch::Phase::Down && chrome.done.contains(x, y)) {
        touch_input.clear(); touch_item_finger = -1; touch_ground_finger = -1;
        if (touch_bind != TouchBindPhase::Off) { touch_close_bind(); return true; }
        if (touch_assign != skillpad::kUnset) { touch_assign = skillpad::kUnset; tree_open = touch_assignment_from_tree; touch_assignment_from_tree = false; return true; }
        touch_play_ui_click(touch::UiClick::Back);
        focus_keys.push_back(SDLK_ESCAPE);
        return true;
    }
    if (touch_bind != TouchBindPhase::Off && phase == touch::Phase::Down && chrome.page.contains(x, y)) {
        touch_input.alternate = !touch_input.alternate;
        touch_play_ui_click(touch::UiClick::BindSet, touch_input.alternate);
        return true;
    }
    if (touch_bind == TouchBindPhase::PickSkill) {
        touch::BindingPicker touch_binding{ touch_bind_wheel_page, touch_bind_wheel_at, touch_bind_finger,
            touch_bind_hit, std::hypot(touch_bind_origin_x, touch_bind_origin_y) <= skillpad::kBindWheelHub };
        struct SaveBinding {
            touch::BindingPicker& binding;
            int& page;
            int& selected;
            std::int64_t& finger;
            bool& hit;
            ~SaveBinding() { page = binding.page; selected = binding.selected; finger = binding.finger; hit = binding.hit; }
        } save_binding{ touch_binding, touch_bind_wheel_page, touch_bind_wheel_at, touch_bind_finger, touch_bind_hit };
        const float wheel_cx = touch::to_viewport_x(380.f), wheel_cy = 270.f;
        const int count = int(touch_bind_ids.size());
        const auto track_wheel = [&](float point_x, float point_y) {
            touch_binding.track(point_x - wheel_cx, point_y - wheel_cy, count);
        };
        if (phase == touch::Phase::Down && touch::BindingPicker::pages(count) > 1
            && (touch::translate_legacy_box(touch::kBindingPrevious).contains(x, y) || touch::translate_legacy_box(touch::kBindingNext).contains(x, y))) {
            touch_binding.turn(count, touch::translate_legacy_box(touch::kBindingPrevious).contains(x, y) ? -1 : 1);
            touch_play_ui_click(touch::UiClick::WheelPage, touch_binding.page);
            touch_feedback = "Tap or drag a skill; choose Prev/Next for more";
            return true;
        }
        if (phase == touch::Phase::Down) {
            const int slot = touch::bind_slot_at(chrome, x, y);
            if (slot >= 0) {
                if (slot != touch_bind_slot) touch_play_ui_click(touch::UiClick::Destination, slot);
                touch_bind_slot = slot;
                touch_binding = {};
                touch_rebuild_bind_wheel();
                touch_feedback = "Drag or tap a skill on the wheel";
                return true;
            }
            touch_bind_origin_x = x - wheel_cx; touch_bind_origin_y = y - wheel_cy;
            touch_binding.begin(finger, touch_bind_origin_x, touch_bind_origin_y);
            track_wheel(x, y);
            return true;
        }
        if (finger == touch_binding.finger) {
            if (phase == touch::Phase::Cancel) { touch_binding.clear_press(); return true; }
            if (phase == touch::Phase::Move) track_wheel(x, y);
            if (phase == touch::Phase::Up && touch_bind_slot >= 0) {
                track_wheel(x, y);
                if (touch_binding.hit && count > 0) {
                    const int index = std::clamp(touch_binding.selected, 0, count - 1);
                    if (!touch_apply_bind(touch_bind_slot, touch_bind_ids[std::size_t(index)])) { touch_binding.clear_press(); return true; }
                    touch_bind = TouchBindPhase::PickSlot;
                    touch_binding.clear_press();
                    touch_feedback = "Tap another button, I/II, or Done";
                }
                touch_binding.clear_press();
            }
            return true;
        }
        return true;
    }
    if (touch_bind == TouchBindPhase::PickSlot) {
        if (phase == touch::Phase::Down) {
            const int slot = touch::bind_slot_at(chrome, x, y);
            if (slot >= 0) {
                if (slot != touch_bind_slot) touch_play_ui_click(touch::UiClick::Destination, slot);
                touch_bind_slot = slot;
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
            if (!touch_apply_bind(touch_input.destination, touch_assign)) { touch_input.destination = -1; return true; }
            const auto* info = scene ? scene->skills.get(touch_assign) : nullptr;
            const std::string name = info ? info->name : std::string(skillpad::action_label(touch_assign, true));
            touch_feedback = (touch_input.destination == touch::kAttackBind ? std::string("Attack")
                               : std::to_string(touch_input.destination + 1)) + " . " + name;
            touch_input.destination = -1;
        }
        return true;
    }
    if (!pad_field()) {
        const float viewport_x = x;
        x = touch::to_legacy_x(x);
        touch_input.clear();
        const auto follow_pointer = [&](bool press, bool lifted) {
            touch_pointer.active = !lifted;
            touch_pointer.press = press;
            touch_pointer.release = lifted;
            touch_pointer.x = x;
            touch_pointer.y = y;
        };
        const auto close_item_track = [&] {
            close_touch_item();
        };
        const auto place_held_at = [&](float point_x, float point_y) {
            if (!scene || !held) return;
            const auto [item_w, item_h] = d2d::rules::item_size(scene->rules, held->code);
            OpenPanels open{ inv_open, stash_open, cube_open, belt_open, character.expansion };
            auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                             open, int(point_x), int(point_y));
            if (!click.consumed) {
                float snapped_x = point_x, snapped_y = point_y;
                const auto try_grid = [&](const Scene::InvLayout& layout) {
                    touch::SnapGrid grid{ float(layout.grid_x), float(layout.grid_y), float(layout.box_w), float(layout.box_h),
                                          layout.cols, layout.rows, item_w, item_h };
                    int col = 0, row = 0;
                    if (!touch::snap_grid_cell(grid, point_x, point_y, col, row)) return false;
                    const auto snapped = touch::snap_grid_point(grid, col, row);
                    snapped_x = snapped.first; snapped_y = snapped.second;
                    return true;
                };
                bool snapped = false;
                if (inv_open) snapped = try_grid(touch_inv_layout());
                if (!snapped && cube_open) snapped = try_grid(scene->cube_layout);
                if (!snapped && stash_open) snapped = try_grid(scene->stash_layout[character.expansion ? 1 : 0]);
                if (!snapped && inv_open) {
                    const auto& inv = touch_inv_layout();
                    float best = 48.f;
                    int best_slot = -1;
                    for (int slot = d2d::d2s::body_location::kFirst; slot <= d2d::d2s::body_location::kLast; ++slot) {
                        const auto& rect = inv.slots[std::size_t(slot)];
                        if (rect[2] <= 0) continue;
                        const float centre_x = float(rect[0]) + float(rect[2]) * .5f, centre_y = float(rect[1]) + float(rect[3]) * .5f;
                        const float dist = std::hypot(point_x - centre_x, point_y - centre_y);
                        if (dist < best) { best = dist; best_slot = slot; }
                    }
                    if (best_slot >= 0) {
                        const auto& rect = inv.slots[std::size_t(best_slot)];
                        snapped_x = float(rect[0]) + float(rect[2]) * .5f;
                        snapped_y = float(rect[1]) + float(rect[3]) * .5f;
                        snapped = true;
                    }
                }
                if (snapped)
                    click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                                open, int(snapped_x), int(snapped_y));
            }
            if (click.cmd && !std::holds_alternative<cmd::ToCursor>(*click.cmd)) send(*click.cmd);
            else if (!click.consumed) {
                if (use_item.open() && use_item::in_slot(int(point_x), int(point_y))) {
                    const bool accepted = held->identified
                        && (use_item.hc_idx == use_item::kCharsi ? d2d::rules::imbuable(scene->rules, *held, character.expansion)
                            : use_item.hc_idx == use_item::kLarzuk ? d2d::rules::socketable(scene->rules, *held)
                            : use_item.hc_idx == use_item::kAnya && d2d::rules::nameable(scene->rules, *held));
                    const auto out = use_item::press(use_item, int(point_x), int(point_y), now_ms, false, false, true, accepted);
                    if (out.act == use_item::Act::kPlace) use_item.item = held->id;
                } else send(cmd::Drop{ held->id });
            }
        };
        const auto run_sheet_action = [&](touch::ItemAction action) {
            if (action == touch::ItemAction::Use && touch_item >= 0) {
                const auto found = std::ranges::find(character.items, touch_item, &d2d::d2s::Item::id);
                if (found == character.items.end()) return;
                const auto used = item_use(*found);
                if (used.kind == ItemUseKind::Identify) identify_with = found->id;
                else if (used.kind != ItemUseKind::None) send(cmd::UseItem{ found->id });
            }
        };
        if (touch_sheet.open) {
            touch::ItemAction trade_action;
            if (touch_sheet.trade_event(finger, phase, x, y, trade_action)) {
                if (trade_action != touch::ItemAction::None) {
                    touch_trade(trade_action); touch_sheet = {}; close_item_track();
                }
                return true;
            }
            if (phase == touch::Phase::Down) {
                const auto hit = touch_sheet.hit(x, y);
                if (hit == touch::ItemAction::Cancel || hit == touch::ItemAction::None) {
                    if (hit == touch::ItemAction::Cancel || !touch_sheet.contains(x, y)) {
                        if (touch_sheet.identifying_target) touch_identify_target = -1;
                        else close_item_track();
                        touch_sheet = {};
                    }
                    return true;
                }
                if (hit == touch::ItemAction::Confirm) {
                    if (touch_sheet.identifying_target) {
                        if (identify_with >= 0 && touch_identify_target >= 0)
                            send(cmd::IdentifyWith{ identify_with, touch_identify_target });
                        identify_with = -1; touch_identify_target = -1;
                        touch_sheet = {};
                        return true;
                    }
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
        if (touch_item_finger != -1 && finger != touch_item_finger) return true;
        if (touch_sold && finger == touch_item_finger) {
            if (phase == touch::Phase::Up || phase == touch::Phase::Cancel) close_item_track();
            return true;
        }
        if (held) {
            if (touch_item_finger != -1 && finger != touch_item_finger && phase != touch::Phase::Cancel)
                return true;
            if (phase == touch::Phase::Cancel) {
                if (touch_item_finger != -1 && finger != touch_item_finger) return true;
                cancel_touch_item();
                touch_pointer = {};
                return true;
            }
            follow_pointer(false, false);
            if (phase == touch::Phase::Up) {
                if (touch_item_drag && touch_sell_vendor >= 0) {
                    const auto drop = touch_bag_drop(x, y);
                    if (drop == touch::BagDrop::Sell) touch_trade(touch::ItemAction::Sell);
                    else if (drop == touch::BagDrop::Inventory) place_held_at(x, y);
                } else place_held_at(x, y);
                close_item_track();
                touch_pointer = {};
            }
            return true;
        }
        if (phase == touch::Phase::Cancel && finger == touch_item_finger) {
            cancel_touch_item();
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
                if (touch_stock.tab < 0 && click.cmd) send(*click.cmd);
                follow_pointer(false, false);
            }
            if (touch_item_drag) {
                follow_pointer(false, false);
                if (phase == touch::Phase::Up) {
                    if (touch_stock.tab >= 0) {
                        if (touch_trade_drop().contains(x, y)) touch_trade(touch::ItemAction::Buy);
                    } else if (touch_sell_vendor >= 0) {
                        const auto drop = touch_bag_drop(x, y);
                        if (drop == touch::BagDrop::Sell) touch_trade(touch::ItemAction::Sell);
                        else if (drop == touch::BagDrop::Inventory) place_held_at(x, y);
                    } else place_held_at(x, y);
                    close_item_track();
                    touch_pointer = {};
                }
                return true;
            }
            if (phase == touch::Phase::Move) return true;
            const auto* bag_item = touch_bag_item();
            const auto action = bag_item ? item_use(*bag_item) : ItemUse{};
            const bool can_use = bag_item && !action.label.empty() && action.kind != ItemUseKind::None;
            const bool can_sell = touch_sell_vendor == store.npc && touch_can_sell(bag_item);
            const bool can_buy = touch_stock.index(store) >= 0;
            touch_item_finger = -1;
            touch_hold.clear();
            touch_sheet = touch::make_item_sheet(can_use, can_sell, can_buy);
            if (!touch_sheet.open) {
                close_item_track();
                return true;
            }
            touch_item_hint = can_use && can_sell ? "Use then Confirm, or release Sell"
                : can_sell || can_buy ? "Release on Buy / Sell" : "Tap an action, then Confirm";
            return true;
        }
        if (phase == touch::Phase::Down && scene && identify_with >= 0 && !held) {
            const auto* item = bag_at(int(x), int(y));
            if (item && item->id != identify_with && !item->identified) {
                touch_identify_target = item->id;
                touch_sheet = touch::make_confirm_sheet();
                touch_item_hint = "Confirm to identify";
            }
            return true;
        }
        if (phase == touch::Phase::Down && scene && identify_with < 0 && !held) {
            const int stock_index = store.npc >= 0 ? store_item_at(*scene, store, int(x), int(y), touch_store_panel_x()) : -1;
            const auto* item = stock_index < 0 ? bag_at(int(x), int(y)) : nullptr;
            const auto action = item ? item_use(*item) : ItemUse{};
            const bool can_sell = touch_can_sell(item);
            if (stock_index >= 0 || (item && (!action.label.empty() || can_sell))) {
                touch_stock = StoreSelection::capture(store, stock_index);
                touch_sell_vendor = can_sell ? store.npc : -1;
                touch_sell_tab = can_sell ? store.tab : -1;
                touch_item_finger = finger; touch_item = item ? item->id : -1;
                touch_item_start = now_ms; touch_sold = false; touch_item_drag = false;
                touch_hold.clear();
                touch_hold_edge = true;
                touch_item_x = x; touch_item_y = y;
                touch_item_hint = touch_stock.tab >= 0 ? "Tap to buy, drag into inventory to buy" : std::string(action.label);
                if (can_sell) touch_item_hint = "Drag within inventory to move, into vendor grid to sell";
                return true;
            }
        }
        if (phase == touch::Phase::Down && chrome.map.contains(viewport_x, y)) {
            touch_input.clear();
            automap.open = !automap.open;
            touch_play_ui_click(touch::UiClick::Map, automap.open);
            if (automap.open) { automap.mini = 0; automap.fade = 2; automap.panel_side = 0; automap.covered = false; }
            return true;
        }
        if (tree_open && scene) {
            const int tree_panel_right = kTreeR;
            const int cls = std::max(character.character_class, 0);
            const auto clear_tree_preview = [&] {
                touch_learn_skill = -1;
                touch_tree_tap_skill = -1;
                touch_tree_tap_ms = 0;
                touch_selected = skillpad::kEmpty;
                touch_pointer = {};
            };
            if (phase == touch::Phase::Down && touch_learn_skill >= 0 && touch::kTreeLearnConfirm.contains(x, y)) {
                send(cmd::SkillPoint{ touch_learn_skill });
                clear_tree_preview();
                return true;
            }
            if (phase == touch::Phase::Down && touch::kTreeLearnCancel.contains(x, y)) {
                clear_tree_preview();
                return true;
            }
            if (phase == touch::Phase::Down && touch::kTreeAssign.contains(x, y) && touch_selected >= 0) {
                touch_play_ui_click(touch::UiClick::TreeAssign);
                touch_assign = touch_selected; touch_feedback.clear(); touch_assignment_from_tree = true; tree_open = false;
                clear_tree_preview();
                return true;
            }
            const int index = skill_at(*scene, cls, tree_tab, int(x), int(y), tree_panel_right);
            const auto& ids = scene->skills.class_ids[std::size_t(cls)];
            if (index >= 0 && std::size_t(index) < ids.size()) {
                if (phase == touch::Phase::Down) {
                    touch_pointer.x = x; touch_pointer.y = y; touch_pointer.active = true;
                    touch_pointer.press = touch_pointer.release = false;
                    const bool can = character.stats.get(d2d::d2s::kSkillPts) > 0
                        && d2d::rules::can_learn(scene->rules, cls, index, character.stats.skills, int(character.stats.get(d2d::d2s::kLevel)));
                    const bool double_tap = can && touch_tree_tap_skill == index
                        && now_ms >= touch_tree_tap_ms && now_ms - touch_tree_tap_ms <= touch::kTreeDoubleTapMs;
                    if (double_tap) {
                        send(cmd::SkillPoint{ index });
                        clear_tree_preview();
                    } else {
                        touch_tree_tap_skill = index;
                        touch_tree_tap_ms = now_ms;
                        touch_learn_skill = can ? index : -1;
                        touch_selected = skill_known(character.name, ids[std::size_t(index)]) ? ids[std::size_t(index)] : skillpad::kEmpty;
                    }
                }
                return true; // inspect first; spend only on Confirm or a second tap
            }
            if (phase == touch::Phase::Down
                && (touch_learn_skill >= 0 || touch_selected >= 0 || touch_pointer.active)
                && !touch::tree_preview_chrome_hit(x, y, touch_learn_skill >= 0, touch_selected >= 0)) {
                clear_tree_preview();
                // fall through: tabs and the tree close button still use pointer input
            }
        }
        return false; // other panels use their ordinary pointer input
    }
    const bool stamina_available = touch_hides_classic_hud() && !view.dead && pad_field() && !touch_sheet.open;
    if (touch_input.stamina_press.owns(finger) || (stamina_available && phase == touch::Phase::Down
        && touch::stamina_bar_box(chrome).contains(x, y))) {
        if (!stamina_available) { touch_input.stamina_press.clear(); return true; }
        if (touch_input.stamina_press.event(finger, phase, x, y, touch::stamina_bar_box(chrome))) {
            touch_input.run_toggle = true;
            touch_play_ui_click(touch::UiClick::Run, !view.running);
        }
        return true;
    }
    if (touch_input.potion_press.owns(finger) || (!touch_sheet.open && phase == touch::Phase::Down
        && (chrome.healing.contains(x, y) || chrome.mana.contains(x, y)))) {
        const auto pressed = touch_input.potion_press.event(finger, phase, x, y, chrome);
        if (pressed != touch::PotionButton::None && !view.dead) {
            const auto summary = potion_summary(character.items);
            const bool healing = pressed == touch::PotionButton::Healing;
            const auto slot = healing ? summary.strongest_healing_slot : summary.strongest_mana_slot;
            if (slot) send(cmd::UseBelt{ *slot });
            touch_input.feedback.pulse(healing ? 5 : 6,
                slot ? touch::FeedbackKind::Accepted : touch::FeedbackKind::Rejected, now_ms);
        }
        return true;
    }
    if (!touch_sheet.open && phase == touch::Phase::Down && chrome.belt_more.contains(x, y)) {
        const bool expand = !touch_input.belt_expanded;
        touch_input.clear(); touch_input.belt_expanded = expand;
        touch_play_ui_click(touch::UiClick::Belt, expand);
        return true;
    }
    const auto belt_potion = [&](std::string_view code) {
        return potion_code(code) || (scene && scene->rules.potions.contains(std::string(code)));
    };
    const auto belt_can_fill = [&] {
        if (!scene) return false;
        const int boxes = scene->belts[std::size_t(belt_index(*scene, character.items))].boxes;
        for (const auto& carried : character.items) {
            if (carried.location != d2d::d2s::item_location::kStored || !belt_potion(carried.code)) continue;
            auto copy = character.items;
            if (d2d::rules::auto_belt(scene->rules, copy, carried.id, boxes)) return true;
        }
        return false;
    };
    const auto belt_do_fill = [&] {
        if (!scene) return;
        auto trial = character.items;
        const int boxes = scene->belts[std::size_t(belt_index(*scene, character.items))].boxes;
        std::vector<int> moved;
        for (const auto& carried : character.items)
            if (carried.location == d2d::d2s::item_location::kStored && belt_potion(carried.code)
                && d2d::rules::auto_belt(scene->rules, trial, carried.id, boxes)) moved.push_back(carried.id);
        for (const int id : moved) send(cmd::AutoBelt{ id });
    };
    if (touch_input.belt_expanded && touch_sheet.open && touch_sheet.belt_index >= 0) {
        if (phase == touch::Phase::Down) {
            if (chrome.belts[std::size_t(touch_sheet.belt_index)].contains(x, y)) {
                send(cmd::UseBelt{ touch_sheet.belt_index });
                touch_sheet = {};
                return true;
            }
            const auto hit = touch_sheet.hit(touch::to_legacy_x(x), y);
            if (hit == touch::ItemAction::Cancel || hit == touch::ItemAction::None) {
                if (hit == touch::ItemAction::Cancel || !touch_sheet.contains(touch::to_legacy_x(x), y)) touch_sheet = {};
                return true;
            }
            if (hit == touch::ItemAction::Confirm) {
                if (touch_sheet.chosen == touch::ItemAction::Use) send(cmd::UseBelt{ touch_sheet.belt_index });
                else if (touch_sheet.chosen == touch::ItemAction::Fill) belt_do_fill();
                touch_sheet = {};
                return true;
            }
            if (hit == touch_sheet.chosen && touch_sheet.confirming) {
                if (hit == touch::ItemAction::Use) send(cmd::UseBelt{ touch_sheet.belt_index });
                else if (hit == touch::ItemAction::Fill) belt_do_fill();
                touch_sheet = {};
                return true;
            }
            touch_sheet.chosen = hit;
            touch_sheet.confirming = true;
            touch_item_hint = "Tap Confirm";
            return true;
        }
        return phase != touch::Phase::Move;
    }
    if (touch_input.belt_expanded && phase == touch::Phase::Down) {
        for (int index = 0; index < 4; ++index)
            if (chrome.belts[std::size_t(index)].contains(x, y)) {
                const d2d::d2s::Item* belt_item = nullptr;
                for (const auto& item : character.items)
                    if (item.location == d2d::d2s::item_location::kBelt && item.column == index) { belt_item = &item; break; }
                const bool can_fill = belt_can_fill();
                if (!belt_item && !can_fill) return true;
                touch_sheet = touch::make_item_sheet(belt_item != nullptr, false, false, can_fill);
                touch_sheet.belt_index = index;
                touch_item_hint = "Tap Use or Fill, then Confirm. Second tap on the slot uses.";
                return true;
            }
    }
    if (phase == touch::Phase::Down && chrome.map.contains(x, y)) {
        touch_input.clear();
        automap.open = !automap.open;
            touch_play_ui_click(touch::UiClick::Map, automap.open);
        if (automap.open) {
            automap.mini = 0;
            automap.fade = 2;
            automap.panel_side = 0;
            automap.covered = false;
        }
        return true;
    }
    if (pad_field()) {
        if (touch_get_hold.finger == finger) {
            touch_get_inside = touch_get_inside && chrome.interact.contains(x, y);
            if (phase == touch::Phase::Up || phase == touch::Phase::Cancel) {
                const bool activate = phase == touch::Phase::Up && touch_get_inside && !touch_get_hold.activated;
                touch_get_hold.clear();
                if (activate && touch_interact_pick.kind == PadPick::kItem) {
                    if (!open_loot_chooser(touch_player, view.player, true)) send(cmd::Pickup{ touch_interact_pick.id });
                }
            }
            return true;
        }
        if (phase == touch::Phase::Down && touch_interact_visible(touch_interact_pick) && chrome.interact.contains(x, y)) {
            if (touch_interact_pick.kind == PadPick::kItem) {
                touch_get_hold.begin(finger, now_ms); touch_get_inside = true;
            } else send(cmd::Interact{ touch_interact_pick.id });
            return true;
        }
        if (phase == touch::Phase::Down && !touch::chrome_hit(x, y, touch_interact_visible(touch_interact_pick), true, touch_input.belt_expanded)) {
            touch_ground_finger = finger; touch_ground_x = x; touch_ground_y = y;
        } else if (finger == touch_ground_finger) {
            if (phase == touch::Phase::Move &&
                std::hypot(x - touch_ground_x, y - touch_ground_y) > 14.f)
                touch_ground_finger = -1;
            if (phase == touch::Phase::Up && touch_ground_finger == finger) {
                const float iso_u = (touch::to_legacy_x(x) - float(kScreenWidth) / 2) / (kIsoW / 2);
                const float iso_v = float(y - kViewY) / (kIsoH / 2);
                const float world_x = cam_x + (iso_u + iso_v) / 2, world_y = cam_y + (iso_v - iso_u) / 2;
                send(cmd::Move{ world_x, world_y, true });
                touch_ground_finger = -1;
            }
            if (phase == touch::Phase::Cancel) touch_ground_finger = -1;
        }
    }
    touch::Target target;
    bool stationary = false;
    if (phase == touch::Phase::Down) {
        const int slot = touch::bind_slot_at(chrome, x, y);
        if (slot >= 0) {
            target = resolve_skill(touch_player, touch_bound_skill(slot));
            stationary = touch_stationary(target.skill);
        }
    }
    const auto aim_from_drag = [&] {
        if (!touch_input.owns_skill(finger)) return;
        touch_input.aim_x = x;
        touch_input.aim_y = y;
        const float dx = x - touch_input.origin_x, dy = y - touch_input.origin_y;
        if (touch_skill_hold.finger == finger && touch::beyond_skill_aim(dx, dy)) {
            touch_skill_hold.cancel();
            touch_cancel_skill_chase();
        }
        if (touch_skill_hold.cancelled) { touch_input.preview.valid = false; return; }
        touch_input.dragged = touch_input.dragged || (!touch_input.stationary && std::hypot(dx, dy) > 10);
        if (touch_skill_hold.release_casts()) touch_refresh_skill_aim();
    };
    if (phase == touch::Phase::Down && target.skill >= 0 && scene) {
        const auto* info = scene->skills.get(target.skill);
        if (info && info->srvdofunc == d2d::rules::ServerDoFunction::kTeleport) touch_teleport.clear();
    }
    if (phase == touch::Phase::Move || phase == touch::Phase::Up) aim_from_drag();
    if (phase == touch::Phase::Up && touch_input.owns_skill(finger) && scene) {
        const auto* info = scene->skills.get(touch_input.preview.skill);
        if (info && info->srvdofunc == d2d::rules::ServerDoFunction::kTeleport) {
            const float dx = touch_input.aim_x - touch_input.origin_x;
            const float dy = touch_input.aim_y - touch_input.origin_y;
            const auto current = resolve_skill(touch_player, touch_input.preview.skill, dx, dy, touch_input.dragged);
            if ((touch_input.dragged || touch_teleport.shown) && !touch_teleport.matches(current))
                touch_input.preview.valid = false;
            else touch_input.preview = current;
            touch_teleport.clear();
        }
    }
    if (phase == touch::Phase::Cancel && touch_input.owns_skill(finger)) touch_teleport.clear();
    auto input_phase = phase;
    if ((phase == touch::Phase::Up || phase == touch::Phase::Cancel) && touch_skill_hold.finger == finger) {
        if (touch_skill_hold.cancelled) input_phase = touch::Phase::Cancel;
        else if (!touch_skill_hold.release_casts()) touch_input.consume_release();
        touch_cancel_skill_chase();
        touch_skill_hold.clear();
    }
    release.armed = false;
    const bool was_alternate = touch_input.alternate;
    touch_input.event(finger, input_phase, x, y, target, stationary, false, now_ms);
    if (was_alternate != touch_input.alternate) touch_play_ui_click(touch::UiClick::BindSet, touch_input.alternate);
    if (phase == touch::Phase::Down) touch_begin_skill_hold(finger);
    return true;
}



void ClientSession::drive_touch_hold(float seconds) {
    touch_hold_fraction = 0.f;
    if (!touch_enabled || touch_item_finger == -1 || touch_sold || !scene || touch_sheet.open || touch_item_drag) {
        if (touch_item_finger == -1) { touch_hold.clear(); touch_hold_edge = false; }
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
        query.sellable = touch_sell_vendor == store.npc && touch_can_sell(item);
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
    const bool edge = std::exchange(touch_hold_edge, false);
    const hold_confirm::Step step = touch_hold.step(edge, true, eligible, item->id, int(kind), seconds);
    if (touch_hold.armed && !touch_hold.cancelled) touch_hold_fraction = touch_hold.fraction();
    if (step != hold_confirm::Step::Act) return;
    if (kind == PadHoldKind::Sell) { touch_sold = touch_trade(touch::ItemAction::Sell); }
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
void ClientSession::drive_touch() {
    if (!touch_enabled) return;
    // Tracked whoever is last: a panel a key opened must not eat the next finger's gesture.
    const int panel_state = (store.npc >= 0 ? store.npc + 2 : 0) + (inv_open ? 1000 : 0) + (tree_open ? 2000 : 0) +
                            (char_open ? 4000 : 0) + (game_menu.open ? 8000 : 0) + (skillbar.picking ? 16000 : 0)
                            + (store.npc >= 0 ? store.tab * 32768 + int(store.gamble) * 131072 : 0);
    if (panel_state != touch_panel_state) {
        cancel_touch_item();
        if (!tree_open) { touch_learn_skill = -1; touch_tree_tap_skill = -1; }
        touch_panel_state = panel_state;
    }
    const int effect_level = level ? level->id : -1;
    if (!touch_shows_field_controls() || view.dead || effect_level != touch_effect_level || loot_chooser.open) {
        touch_effect.cancel();
        touch_get_hold.clear();
        touch_input.stamina_press.clear();
    }
    if (touch_effect.modal) {
        const auto* selected = touch_effect.current(view.effects);
        if (!selected || !selected->dismissible) touch_effect.cancel();
    }
    touch_effect_level = effect_level;
    if (!pad_field() || last_device != touch::LastDevice::Touch || view.dead) touch_get_hold.clear();
    if (last_device != touch::LastDevice::Touch) {
        cancel_touch_item();
        touch_cancel_skill_chase(); touch_stop_skill_chase(false);
        touch_skill_hold.clear(); touch_teleport.clear();
        touch_input.clear();
        if (touch_sheet.belt_index >= 0) touch_sheet = {};
        return;
    }
    if (pad_field() && !view.dead && !touch_effect.modal && touch_get_inside
        && touch_get_hold.finger != -1 && !touch_get_hold.activated && touch_get_hold.update(now_ms))
        open_loot_chooser(touch_player, view.player, true);
    if (!pad_field() || view.dead || loot_chooser.open || touch_effect.modal) {
        touch_cancel_skill_chase();
        if (!view.dead) touch_stop_skill_chase(false);
        else touch_stop_chase = false;
        touch_skill_hold.clear(); touch_teleport.clear();
        const Send stop = [this](const Command& command) { send(command); };
        pad_walk(touch_player, view.player, view.dead, false, stop);
        touch::cancel_modal_gestures(touch_effect.modal, touch_input, touch_get_hold, touch_skill_hold, touch_ground_finger);
        if (touch_effect.modal) touch_get_inside = false;
        touch_input.clear();
        if (touch_sheet.belt_index >= 0) touch_sheet = {};
        return;
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
    if (touch_input.run_toggle) send(cmd::Run{ !view.running });
    if (touch_input.aiming && touch_skill_hold.release_casts()) touch_refresh_skill_aim();
    if (!touch_input.owns_skill(touch_skill_hold.finger)) {
        touch_cancel_skill_chase(); touch_skill_hold.clear();
    }
    touch_stop_skill_chase(touch_player.move.active);
    pad_walk(touch_player, view.player, false, true, out);
    drive_touch_skill_hold();
    touch_attack_pick = pad_pick(touch_player, view.player, false);
    touch_interact_pick = pad_pick(touch_player, view.player, true, {}, false);
    if (touch_input.belt >= 0) send(cmd::UseBelt{ touch_input.belt }); // belt_quick from the adapter; field tap uses the sheet
    if (touch_input.aiming && touch_input.preview.skill == skillpad::kShowItems) pad_show_items = true;
    const auto casts = std::exchange(touch_input.casts, {});
    for (const auto& target : casts) {
        if (target.valid && target.skill == skillpad::kShowItems) pad_show_items = true;
        cast_resolved(touch_player, target);
        if (loot_chooser.open || !pad_field()) break;
    }

}

} // namespace d2d::client
