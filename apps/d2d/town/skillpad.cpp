// SPDX-License-Identifier: GPL-3.0-or-later
// The opt-in skill-button mode: casts, the wheel, the panel strip and the
// per-character .padskills file. The default pad path stays in input.cpp.
#include "town.hpp"

#include "common.hpp"
#include "gamepad_map.hpp"
#include "skillpad.hpp"
#include "skilltree.hpp"

#include <level_ids.hpp>
#include <log.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace d2d::client {

namespace {

std::filesystem::path sheet_path(const std::filesystem::path& directory, const std::string& name) {
    return directory / (name + ".padskills");
}

}  // namespace

auto Town::sheet_key(const std::string& name) const -> std::string {
    // The folder is part of the key, so two saves with one name do not share
    // a sheet. --no-save has no folder and never touches a file.
    if (key_dir.empty()) return "nosave/" + name;
    return key_dir.generic_string() + "/" + name;
}

auto Town::load_skill_sheet(const std::string& name) -> void {
    if (key_dir.empty() || name.empty()) return;
    const auto key = sheet_key(name);
    std::ifstream file(sheet_path(key_dir, name));
    if (!file) { skill_sheets.erase(key); return; }   // a removed file does not keep the old sheet
    const std::string text{ std::istreambuf_iterator<char>(file), {} };
    skill_sheets[key] = skillpad::read(text);
}

auto Town::save_skill_sheet(const std::string& name) const -> void {
    if (key_dir.empty() || name.empty()) return;
    const auto found = skill_sheets.find(sheet_key(name));
    if (found == skill_sheets.end()) return;
    const auto path = sheet_path(key_dir, name);
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream file(temp);
        if (!file) { d2d::log::warn("{}: not written", path.string()); return; }
        file << skillpad::write(found->second);
        file.close();
        if (!file) { d2d::log::warn("{}: not written", path.string()); std::error_code drop; std::filesystem::remove(temp, drop); return; }
    }
    std::error_code error;
    std::filesystem::rename(temp, path, error);   // replaces the sheet; the .d2s and .key are not touched
    if (error) { d2d::log::warn("{}: not written ({})", path.string(), error.message()); std::filesystem::remove(temp, error); }
}

auto Town::rebuild_wheel() -> void {
    wheel_ids.clear();
    if (wheel_actions) wheel_ids.assign(skillpad::kActions.begin(), skillpad::kActions.end());
    else if (scene)
        for (const auto& info : scene->skills.rows)
            if (info.name != "Identify" && info.name != "Book of Identify" && skill_known(focused().character.name, info.id)) wheel_ids.push_back(info.id);
    if (wheel_at < 0 || wheel_at >= int(wheel_ids.size())) wheel_at = 0;
}

auto Town::present_skill_pad() -> void {
    skillbar.inventory_context = inv_open;
    skillbar.mode_on = pad_config.skill_buttons && !game_menu.open && !controls.open && (!touch_enabled || skillbar.picking == 3 || skill_strip >= 0);
    skillbar.wheel_on = false;
    skillbar.strip_on = false;
    skillbar.legend_on = false;
    skillbar.mode_hint.clear();
    if (!pad_config.skill_buttons) return;
    const auto& who = focused().character;
    const auto stored = skill_sheets.find(sheet_key(who.name));
    const skillpad::Sheet sheet = stored == skill_sheets.end() ? skillpad::Sheet{} : stored->second;
    const bool alternate = skill_actions.modifier_held;
    skillbar.mode_modifier = alternate;
    const auto label_of = [&](pad::Action action) -> std::string {
        const auto& pair = pad_config.bindings[action];
        const auto button = pair[0] != pad::Bindings::kNone ? pair[0] : pair[1];
        return button == pad::Bindings::kNone ? std::string("-") : std::string(pad::label(pad_family, button));
    };
    const auto shown_id = [&](int skill) {
        if (skillpad::binds_action(skill)) return skill;
        if (skill == kResurrectSkill) return skillbar.coop ? skill : skillpad::kEmpty;
        if (skill == skillpad::kUnset) skill = int(who.header.right_skill);
        return skill_known(who.name, skill) ? skill : skillpad::kEmpty;
    };
    static constexpr std::array<pad::Action, skillpad::kSlots> kSlotActions = {
        pad::Action::SkillSouth, pad::Action::SkillEast, pad::Action::SkillWest,
        pad::Action::SkillNorth, pad::Action::SkillLeft, pad::Action::SkillRight };
    for (int index = 0; index < skillpad::kSlots; ++index) {
        const int skill = shown_id(sheet.slot(alternate, index));
        skillbar.mode_slots[std::size_t(index)] = skill;
        skillbar.mode_labels[std::size_t(index)] = label_of(kSlotActions[std::size_t(index)]);
    }
    const std::string trigger = std::string(pad::label(pad_family, pad::Button::LeftTrigger));
    const std::string main_label = label_of(pad::Action::MainSkill);
    skillbar.mode_main = shown_id(sheet.trigger(alternate));
    skillbar.mode_main_label = alternate ? trigger + "+" + main_label : main_label;
    skillbar.mode_set = trigger;
    skillbar.mode_aura = view.aura;
    rebuild_wheel();
    skillbar.wheel_ids = wheel_ids;
    skillbar.wheel_at = wheel_at;
    skillbar.wheel_page = wheel_page;
    skillbar.wheel_on = skillbar.picking == 3;
    skillbar.wheel_close = label_of(pad::Action::SkillPicker);
    skillbar.wheel_prompt.clear();
    if (skillbar.wheel_on) {
        skillbar.wheel_prompt = std::string(wheel_actions ? "Actions" : "Skills") + "  " +
            std::to_string(wheel_page / skillpad::kWheelShown + 1) + "/" +
            std::to_string(std::max(1, (int(wheel_ids.size()) + skillpad::kWheelShown - 1) / skillpad::kWheelShown)) +
            " . D-pad L/R: tab . U/D: browse";
        skillbar.wheel_controls = "Press a destination to assign . " + skillbar.wheel_close + " / Start: Done";
        skillbar.assignment_feedback = assignment_feedback;
    }
    skillbar.strip_on = skill_strip >= 0;
    skillbar.strip_at = skill_strip;
    skillbar.strip_rows.assign(skillpad::kPanels.begin(), skillpad::kPanels.end());
    skillbar.strip_prompt = std::string(pad::label(pad_family, pad::Button::South)) + ": open  "
                          + std::string(pad::label(pad_family, pad::Button::East)) + ": back";
    skillbar.legend_on = pad_playing && (pad_field() || skillbar.picking == 3);
    if (!scene || !pad_field()) return;
    contextual_pick = pad_pick(pad, focused().player, true);
    bool attack_bound = false;
    for (int index = 0; index < skillpad::kSlots; ++index)
        if (sheet.slot(alternate, index) == 0) {
            skillbar.mode_hint = skillbar.mode_labels[std::size_t(index)] + " . " + pick_label(contextual_pick);
            attack_bound = true;
            break;
        }
    if (!attack_bound && sheet.trigger(alternate) == 0)
        skillbar.mode_hint = skillbar.mode_main_label + " . " + pick_label(contextual_pick);
    if (skillbar.mode_hint.empty())
        skillbar.mode_hint = label_of(pad::Action::Interact) + " . " + pick_label(pad_pick(pad, focused().player, true, {}, false));
}

auto Town::pick_label(const PadPick& pick) const -> std::string {
    if (pick.kind == PadPick::kMonster) {
        const auto found = std::ranges::find(view.monsters, pick.id, &Monster::id);
        if (scene && found != view.monsters.end() && found->type >= 0 && std::size_t(found->type) < scene->monsters.types.size()) {
            const auto& key = scene->monsters.types[std::size_t(found->type)].name_key;
            const auto text = lookup_string(*scene, key);
            return "Attack " + (text ? u16_to_latin1(*text) : key);
        }
        return "Attack";
    }
    if (pick.kind == PadPick::kItem) {
        const auto found = std::ranges::find(view.ground, pick.id, &Loot::GroundItem::id);
        return "Pick up " + (found != view.ground.end() ? found->label : std::string("item"));
    }
    if (pick.kind == PadPick::kThing) {
        if (pick.id <= -3000) return "Pick up corpse";
        if (pick.id < 0) return "Use portal";
        if (level && std::size_t(pick.id) < level->npcs.size()) {
            const auto& npc = level->npcs[std::size_t(pick.id)];
            return std::string(npc.root == "objects" ? "Use " : "Talk to ") + npc.name;
        }
    }
    return "Attack / Use";
}

auto Town::open_assignment(int skill) -> void {
    assignment_from_tree = tree_open;
    tree_open = false;
    wheel_actions = skill < 0;
    rebuild_wheel();
    const auto found = std::ranges::find(wheel_ids, skill);
    wheel_at = found == wheel_ids.end() ? 0 : int(found - wheel_ids.begin());
    wheel_page = wheel_at / skillpad::kWheelShown * skillpad::kWheelShown;
    skillbar.picking = 3;
    assignment_feedback.clear();
}

auto Town::close_assignment() -> void {
    skillbar.picking = 0;
    if (assignment_from_tree) tree_open = true;
    assignment_from_tree = false;
}

auto Town::drive_skill_ui(const pad::Actions& actions, int mouse_x, int mouse_y) -> bool {
    if (!pad_config.skill_buttons) return false;
    if (actions.assign_skill && tree_open && scene) {
        const int cls = std::max(character.character_class, 0);
        const int index = skill_at(*scene, cls, tree_tab, mouse_x, mouse_y);
        const auto& ids = scene->skills.class_ids[std::size_t(cls)];
        if (index >= 0 && std::size_t(index) < ids.size() && skill_known(character.name, ids[std::size_t(index)])) {
            open_assignment(ids[std::size_t(index)]);
            return true;
        }
    }
    const bool start = actions.panel_strip || (skill_strip >= 0 && actions.cancel);
    if (skillpad::start_respawns(start, view.dead, view.ghost, view.all_dead)) {
        skillbar.picking = 0;
        skill_strip = -1;
        send(cmd::Resurrect{});
        return true;
    }
    if (skillbar.picking == 3) { drive_wheel(actions); return true; }
    if (skill_strip >= 0) { drive_strip(actions); return true; }
    if (actions.panel_strip && pad_field()) { skill_strip = 0; return true; }
    return false;
}

auto Town::drive_wheel(const pad::Actions& actions) -> void {
    if (actions.wheel_tab != 0) { wheel_actions = !wheel_actions; wheel_at = wheel_page = 0; }
    rebuild_wheel();
    const int count = int(wheel_ids.size());
    if (count <= 0) { skillbar.picking = 0; return; }
    if (actions.cancel || actions.skill_picker) { close_assignment(); return; }
    if (actions.wheel_step != 0) wheel_at = (wheel_at + (actions.wheel_step > 0 ? 1 : count - 1)) % count;
    wheel_page = wheel_at / skillpad::kWheelShown * skillpad::kWheelShown;
    if (actions.wheel_step == 0 && actions.aim_active) {
        const skillpad::WheelPage page{ wheel_page, std::min(skillpad::kWheelShown, count - wheel_page) };
        const int picked = skillpad::wedge(actions.aim_x, actions.aim_y, page.shown, 0.f);
        if (picked >= 0) wheel_at = page.start + picked;
    }
    const auto& who = focused().character;
    auto& sheet = skill_sheets[sheet_key(who.name)];
    const int chosen = wheel_ids[std::size_t(std::clamp(wheel_at, 0, count - 1))];
    bool changed = false;
    if (actions.skill_slot >= 0 && actions.skill_slot < skillpad::kSlots) {
        sheet.set_slot(actions.modifier_held, actions.skill_slot, chosen);
        static constexpr std::array<pad::Action, 6> kDestinations = {
            pad::Action::SkillSouth, pad::Action::SkillEast, pad::Action::SkillWest,
            pad::Action::SkillNorth, pad::Action::SkillLeft, pad::Action::SkillRight };
        const auto& buttons = pad_config.bindings[kDestinations[std::size_t(actions.skill_slot)]];
        const auto button = buttons[0] != pad::Bindings::kNone ? buttons[0] : buttons[1];
        assignment_feedback = std::string(pad::label(pad_family, button));
        changed = true;
    }
    if (actions.main_skill) {
        if (actions.modifier_held) sheet.alt_main = chosen;
        else sheet.main = chosen;
        const auto& buttons = pad_config.bindings[pad::Action::MainSkill];
        assignment_feedback = std::string(pad::label(pad_family, buttons[0] != pad::Bindings::kNone ? buttons[0] : buttons[1]));
        changed = true;
    }
    if (!changed) return;
    const auto* assigned = chosen >= 0 && scene ? scene->skills.get(chosen) : nullptr;
    assignment_feedback += " . " + (assigned ? assigned->name : std::string(skillpad::action_label(chosen, true)));
    if (sheet.main == skillpad::kUnset) sheet.main = int(who.header.right_skill);
    save_skill_sheet(who.name);
}

auto Town::drive_strip(const pad::Actions& actions) -> void {
    const int count = int(skillpad::kPanels.size());
    if (actions.cancel) { skill_strip = -1; return; }
    if (actions.strip_step != 0) skill_strip = (skill_strip + (actions.strip_step > 0 ? 1 : count - 1) + count) % count;
    if (!actions.confirm) return;
    menu_handoff.armed = true;                         // this South press must not click the screen it opens
    switch (skill_strip) {
        case 0: char_open = !char_open; if (char_open) stash_open = cube_open = quest_log.open = hireling.open = false; break;
        case 1: inv_open = !inv_open; if (inv_open) tree_open = false; break;
        case 2: tree_open = !tree_open; if (tree_open) inv_open = false; break;
        case 3: automap.open = !automap.open; break;
        case 4: toggle_quest_log(); break;
        case 5:
            if (net_game || coop_hud_hidden()) {
                party.open = !party.open;
                if (party.open) char_open = stash_open = cube_open = quest_log.open = hireling.open = false;
            }
            break;
        case 6:
            if (character.expansion && view.merc_sheet) {
                hireling.open = !hireling.open;
                if (hireling.open) char_open = stash_open = cube_open = quest_log.open = false;
            }
            break;
        case 7: toggle_help(); break;
        case 8: open_game_menu(); break;
        default: break;
    }
    skill_strip = -1;
}

auto Town::apply_skill_pad(PadPlayer& player_pad, const UnitState& unit, bool dead, bool field, std::string_view name, int right_skill,
                           bool expansion, const pad::Actions& actions, const Send& out) -> void {
    if (!field || dead) return;
    const auto& move = player_pad.move;
    if (move.aim || move.active) {
        const auto face = move.aim ? pad::world_step(move.aim_x, move.aim_y, kIsoW, kIsoH, 1.f) : pad::world_step(move.x, move.y, kIsoW, kIsoH, 1.f);
        player_pad.face_x = face.x;
        player_pad.face_y = face.y;
    }
    if (actions.swap_tap && expansion) out(cmd::SwapWeapons{});
    const auto interact = [&]() {
        const PadPick pick = pad_pick(player_pad, unit, true, {}, false);
        if (pick.kind == PadPick::kItem) out(cmd::Pickup{ pick.id });
        else if (pick.kind == PadPick::kThing) out(cmd::Interact{ pick.id });
    };
    if (actions.interact_press) interact();
    const auto found = skill_sheets.find(sheet_key(std::string(name)));
    const skillpad::Sheet sheet = found == skill_sheets.end() ? skillpad::Sheet{} : found->second;
    const auto fire = [&](int skill, bool press, bool button_down, bool& used) {
        // Holding the button keeps sending the skill. The world continues the
        // attack only while a skill command is still arriving.
        if (skill < 0 || !scene || (!press && !button_down) || !skill_known(name, skill)) return;
        const auto* info = scene->skills.get(skill);
        if (!info || info->passive) return;
        if ((info->name == "Town Portal" || info->name == "Book of Townportal") && !inv_open) return;
        // A hold in town would repeat the "not in town" line every tick.
        if (!press && level && d2d::rules::level_ids::is_town(level->id)) return;
        if (skill != 0) {
            const auto target = resolve_skill(player_pad, skill, 0, 0, false, &unit, name);
            if (target.valid) out(cmd::UseSkill{ skill, target.x, target.y, target.unit, target.left, -1 });
            return;
        }
        const bool swing = skillpad::swings({ info->leftskill, !info->srvmissile.empty(), info->range });
        // Attack is also Use, as the left mouse button (FUN_004625b0).
        // A fresh press may interact; its held frames must leave that use queued.
        const PadPick pick = skill == 0 && press && &player_pad == &pad ? contextual_pick
            : pad_pick(player_pad, unit, skill == 0 && press, pad_aim(skill, move.aim || move.active));
        if (pick.kind == PadPick::kMonster) {
            player_pad.target = pick.id;
            out(cmd::UseSkill{ skill, pick.x, pick.y, pick.id, swing, -1 });
            return;
        }
        player_pad.target = -1;
        if (pick.kind == PadPick::kItem) { used = true; out(cmd::Pickup{ pick.id }); return; }
        if (pick.kind == PadPick::kThing) { used = true; out(cmd::Interact{ pick.id }); return; }
        out(cmd::UseSkill{ skill, unit.x + player_pad.face_x * 3.f, unit.y + player_pad.face_y * 3.f, -1, swing, -1 });
    };
    const auto cast = [&](int skill, bool press, bool button_down, bool& used) {
        if (press && scene && skill_known(name, skill)) {
            const auto* info = scene->skills.get(skill);
            if (info && info->aura) {
                const bool mine = name == skillbar.character.name;
                const int current = mine ? skillbar.right : right_skill;
                const auto* current_info = scene->skills.get(current);
                const bool current_aura = current_info && current_info->aura;
                const int active = mine ? view.aura : (current_aura ? current : 0);
                const skillpad::AuraChoice choice = skillpad::aura_choice(active, skill, current, current_aura, player_pad.aura_return);
                player_pad.aura_return = choice.remembered;
                if (mine) {
                    skillbar.choose(false, { choice.next, -1 });
                    if (net_game) net_game->hand(choice.next, false);
                }
                out(cmd::SelectSkill{ choice.next, false });
                return;
            }
        }
        // Aura selection is press-only: a hold must never fall into targeting.
        const auto* info = scene ? scene->skills.get(skill) : nullptr;
        if (info && info->aura) return;
        if (info && self_cast(*info)) {
            if (press) out(cmd::UseSkill{ skill, unit.x, unit.y, -1, false, -1 });
            return;
        }
        fire(skill, press, button_down, used);
    };
    const auto perform = [&](int id, bool press, bool button_down, int button) {
        auto& used = player_pad.used_hold[std::size_t(button)];
        if (press || !button_down) used = false;
        if (used) return;   // FUN_00462930: a held frame leaves the queued interact alone
        if (const int panel = skillpad::panel_index(id); panel >= 0) {
            if (press) player_pad.wants_panel = panel;
            return;
        }
        if (id == skillpad::kInteract) { if (press) interact(); return; }
        if (id == skillpad::kShowItems) { if (press || button_down) pad_show_items = true; return; }
        // Co-op only. Same reach as pad_act. Not SelectSkill: 0x7ffe stays out of the save's right-hand skill.
        if (id == kResurrectSkill) {
            if (press && skillbar.coop)
                if (const auto* body = body_near(unit.x, unit.y, 4.f)) out(cmd::UseSkill{ kResurrectSkill, body->x, body->y, body->id });
            return;
        }
        cast(id, press, button_down, used);
    };
    const int trigger_set = player_pad.trigger_hold.set_for(actions.main_skill, actions.main_skill_down, actions.modifier_held);
    if (trigger_set >= 0) {
        int skill = sheet.trigger(trigger_set == 1);
        if (skill == skillpad::kUnset) skill = right_skill;
        const bool keep = skillpad::binds_action(skill) || skill == kResurrectSkill || skill_known(name, skill);
        perform(keep ? skill : skillpad::kEmpty, actions.main_skill, actions.main_skill_down, skillpad::kSlots);
    }
    for (int index = 0; index < skillpad::kSlots; ++index) {
        const bool press = actions.skill_slot == index;
        const bool button_down = actions.slot_down[std::size_t(index)];
        const int set = player_pad.slot_hold.set_for(index, press, button_down, actions.modifier_held);
        if (set < 0) continue;
        perform(sheet.slot(set == 1, index), press, button_down, index);
    }
}

auto Town::skill_known(std::string_view name, int skill) const -> bool {
    if (!scene || skill < 0 || name.empty()) return false;
    const Character* who = name == character.name ? &character : nullptr;
    if (!who)
        for (const auto& other : world.players)
            if (other && other->character.name == name) { who = &other->character; break; }
    if (!who) return false;
    const auto* info = scene->skills.get(skill);
    if (!info || info->passive || info->name == "Identify" || info->name == "Book of Identify") return false;
    return skill_base_level(*scene, *who, skill) > 0 || skill_level(*scene, *who, skill, {}) > 0;
}

}  // namespace d2d::client
