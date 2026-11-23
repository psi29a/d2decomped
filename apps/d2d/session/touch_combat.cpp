// SPDX-License-Identifier: GPL-3.0-or-later
#include "session.hpp"

#include "common.hpp"
#include "input/gamepad_map.hpp"
#include "input/teleport_aim.hpp"
#include "scene.hpp"
#include "skills/pad.hpp"
#include "touch/gestures.hpp"
#include "touch/input.hpp"

#include <fight.hpp>
#include <gamedata.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <skills.hpp>
#include <spawning.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::client {
namespace {
bool centered_skill(const d2d::rules::Skill& skill) {
    using d2d::rules::ServerDoFunction;
    return self_cast(skill) || skill.srvdofunc == ServerDoFunction::kNova || skill.srvdofunc == ServerDoFunction::kWarCry
        || skill.srvdofunc == ServerDoFunction::kStaticField || skill.srvdofunc == ServerDoFunction::kStateAroundCaster;
}

bool repeatable_skill(const d2d::rules::Skill& skill, const Fight& fight) {
    using d2d::rules::ServerDoFunction;
    if (skill.aura || skill.passive || skill.target_corpse || skill.target_ally || skill.target_pet
        || !skill.summon.empty() || self_cast(skill) || fight.summon_skill(skill)) return false;
    switch (skill.srvdofunc) {
        case ServerDoFunction::kTeleport:
        case ServerDoFunction::kBladeSentinel:
        case ServerDoFunction::kGrimWard:
        case ServerDoFunction::kWakeOfDestruction:
            return false;
        case ServerDoFunction::kNova:
        case ServerDoFunction::kWarCry:
            return (skill.maxdam > 0 || skill.emax > 0) && fight.missile_skill(skill);
        case ServerDoFunction::kAttack:
        case ServerDoFunction::kDoubleThrow:
        case ServerDoFunction::kMindBlast:
        case ServerDoFunction::kPsychicHammer:
        case ServerDoFunction::kStaticField:
        case ServerDoFunction::kThrow:
            return true;
        default:
            return skill_built(skill) || fight.missile_skill(skill);
    }
}

pad::WorldStep touch_skill_offset(float dx, float dy) {
    // Limit the two-to-one drag to the visible viewport before projecting.
    const float screen_x = std::clamp(dx * 2.f, -touch::kSkillAimX, touch::kSkillAimX);
    const float screen_y = std::clamp(dy * 2.f, -touch::kSkillAimUp, touch::kSkillAimDown);
    return { screen_x / kIsoW + screen_y / kIsoH, screen_y / kIsoH - screen_x / kIsoW };
}

pad::WorldStep touch_facing(const UnitState& unit) {
    const auto sector = std::ranges::find(game::kDir16FromSector, unit.dir) - std::begin(game::kDir16FromSector);
    const float angle = float(sector) * std::numbers::pi_v<float> / 8.f;
    return pad::world_step(-std::sin(angle), std::cos(angle), kIsoW, kIsoH, 1.f);
}

pad::Aim touch_skill_aim(const d2d::rules::Skill& skill, const Fight& fight, bool steering) {
    if (!fight.missile_skill(skill) && !Fight::spot_skill(skill)) return {};
    return { 12.f, 14.f, steering ? pad::Aim{}.cone_cos : -2.f };
}

std::vector<pad::Candidate> touch_skill_foes(const View& view, const Scene& scene, const Level* level) {
    std::vector<pad::Candidate> foes;
    for (const auto& monster : view.monsters)
        if (monster.targetable() && monster.align != 2 && !monster.ally_not_interact(scene, level ? level->id : 0))
            foes.push_back({ monster.id, monster.unit.x, monster.unit.y });
    return foes;
}

} // namespace

touch::Target ClientSession::resolve_skill(PadPlayer& player_pad, int skill, float dx, float dy, bool manual, const UnitState* owner, std::string_view owner_name, int sticky) const {
    touch::Target target;
    target.skill = skill;
    if (skillpad::panel_index(skill) >= 0) { target.valid = true; return target; }
    if (!scene) return target;
    if (!skill_known(owner_name.empty() ? std::string_view(character.name) : owner_name, skill)) return target;
    const auto* info = scene->skills.get(skill);
    if (!info || info->passive) return target;
    if (skillpad::portal_name(info->name)) {
        target.valid = portal_tome(owner_name.empty() ? std::string_view(character.name) : owner_name) >= 0 && level && !d2d::rules::level_ids::is_town(level->id);
        return target;
    }
    const auto& unit = owner ? *owner : view.player;
    target.valid = true;
    target.x = unit.x; target.y = unit.y;
    target.left = skillpad::swings({ info->leftskill, !info->srvmissile.empty(), info->range });
    if (info->aura || centered_skill(*info)) return touch::caster_target(skill, unit.x, unit.y);
    const bool teleport = info->srvdofunc == d2d::rules::ServerDoFunction::kTeleport;
    const auto facing = owner || player_pad.move.active
        ? pad::WorldStep{ player_pad.face_x, player_pad.face_y } : touch_facing(unit);
    const float reach = teleport ? 6.f : 3.f;
    const auto offset = manual && (teleport || dx != 0 || dy != 0) ? touch_skill_offset(dx, dy)
        : pad::WorldStep{ facing.x * reach, facing.y * reach };
    target.x += offset.x;
    target.y += offset.y;
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
        const auto rooms = missile_rooms(*scene, *level, unit.x, unit.y, &world.fight.spawning);
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
    if (!teleport) {
        const auto foes = touch_skill_foes(view, *scene, level);
        const auto aim = touch_skill_aim(*info, world.fight, manual || player_pad.move.aim || player_pad.move.active);
        if (const int picked = pad::pick_target(unit.x, unit.y, offset.x, offset.y, foes, sticky >= 0 ? sticky : player_pad.target, aim); picked >= 0) {
            const auto& foe = foes[std::size_t(picked)];
            target.x = foe.x; target.y = foe.y; target.unit = foe.id;
        }
    }
    return target;
}

void ClientSession::touch_cancel_skill_chase() {
    if (touch_skill_hold.sent_to < 0 || !touch_skill_hold.chases) return;
    touch_skill_hold.sent_to = -1;
    touch_skill_hold.sent = false;
    touch_stop_chase = true;
}

void ClientSession::touch_stop_skill_chase(bool keep_walk) {
    if (!std::exchange(touch_stop_chase, false)) return;
    // A fresh stick destination clears the queued approach and keeps walking.
    if (keep_walk) touch_player.walking = false;
    else if (view.pmode < 0 && (!net_game || view.player.walking))
        send(cmd::Move{ view.player.x, view.player.y, true });
}

void ClientSession::touch_refresh_skill_aim() {
    const float dx = touch_input.aim_x - touch_input.origin_x, dy = touch_input.aim_y - touch_input.origin_y;
    const auto& preview = touch_input.preview;
    touch_input.preview = resolve_skill(touch_player, preview.skill, dx, dy, touch_input.dragged,
        nullptr, {}, preview.unit);
    if (touch_input.owns_skill(touch_skill_hold.finger)) touch_skill_hold.target = touch_input.preview.unit;
}

void ClientSession::drive_touch_skill_hold() {
    if (!touch_skill_hold.active(now_ms) || !scene || !level) return;
    auto& target = touch_input.preview;
    const auto* skill = scene->skills.get(target.skill);
    if (!skill || (!skill->in_town && d2d::rules::level_ids::is_town(level->id))) return;
    if (touch_input.dragged) {
        const auto offset = touch_skill_offset(touch_input.aim_x - touch_input.origin_x, touch_input.aim_y - touch_input.origin_y);
        // Returning to the button center keeps the drag's last direction.
        if (offset.x != 0 || offset.y != 0) touch_skill_hold.steer(offset.x, offset.y);
    } else if (touch_player.move.active) {
        touch_skill_hold.steer(touch_player.face_x * 3.f, touch_player.face_y * 3.f);
    }
    const auto foes = touch_skill_hold.at_caster ? std::vector<pad::Candidate>{} : touch_skill_foes(view, *scene, level);
    const auto aim = touch_skill_aim(*skill, world.fight, true);
    touch_skill_hold.resolve(target, view.player.x, view.player.y, foes, aim);
    constexpr std::uint32_t tick = d2d::game::kTickMs;
    bool ready = view.pmode < 0 && skill_known(character.name, skill->id);
    std::uint32_t interval = tick;
    if (lan_client || net_game) {
        // No remote cooldown timestamp is exposed. Use the row's delay
        // and space retries while waiting for an animation or chase update.
        if (ready) {
            const int lvl = std::max(1, skill_level(*scene, character, skill->id, {}));
            const int frames = d2d::rules::eval_calc(scene->skills, skill->delay, {}, skill->id, lvl);
            interval = std::max(4 * tick, std::uint32_t(std::max(0, frames)) * tick);
        }
    } else {
        const auto& actor = focused();
        const auto next_tick = world_ms + tick;
        const int index = touch_skill_hold.chases ? world.fight.monster_index(target.unit) : -1;
        const bool approaching = actor.pmode < 0 && touch_skill_hold.chases && index >= 0
            && actor.attack_mon == index && actor.attack_skill == skill->id;
        ready = !approaching && (actor.pmode < 0 || next_tick >= actor.pmode_until)
            && (skill->delay.empty() || next_tick >= actor.skill_delay) && skill_known(character.name, skill->id);
    }
    if (touch_skill_hold.dispatch(now_ms, ready, interval)) cast_resolved(touch_player, target);
}

bool ClientSession::touch_stationary(int skill) const {
    const auto* info = scene ? scene->skills.get(skill) : nullptr;
    return skillpad::panel_index(skill) >= 0 || (info && (info->aura || centered_skill(*info) || skillpad::portal_name(info->name)));
}

void ClientSession::touch_begin_skill_hold(std::int64_t finger) {
    if (!touch_input.owns_skill(finger) || !touch_input.preview.valid || !scene) return;
    const auto* info = scene->skills.get(touch_input.preview.skill);
    if (!info || !repeatable_skill(*info, world.fight)) return;
    const auto facing = touch_player.move.active
        ? pad::WorldStep{ touch_player.face_x, touch_player.face_y } : touch_facing(view.player);
    touch_skill_hold.begin(finger, now_ms, touch_input.preview.unit, facing.x * 3.f, facing.y * 3.f, centered_skill(*info));
    touch_skill_hold.chases = !touch_stationary(info->id) && !world.fight.missile_skill(*info) && !Fight::spot_skill(*info);
}

void ClientSession::draw_combat_previews(std::vector<std::uint8_t>& framebuffer) {
    if (!scene || !have_world || !pad_field() || view.dead) return;
    const int marker_width = fb_width(framebuffer);
    const auto teleport_marker = [&](TeleportAim& aim) {
        aim.display();
        const auto& target = aim.preview;
        const int centre_x = marker_width / 2 + int((target.x - cam_x - target.y + cam_y) * kIsoW / 2);
        const int centre_y = kViewY + int((target.x - cam_x + target.y - cam_y) * kIsoH / 2);
        for (int y = -9; y <= 9; ++y) for (int x = -9; x <= 9; ++x) {
            const float distance = std::hypot(float(x), float(y));
            const bool mark = target.valid ? std::abs(distance - 7.f) < 1.2f
                                           : (std::abs(x - y) <= 1 || std::abs(x + y) <= 1) && std::abs(x) <= 6 && std::abs(y) <= 6;
            const int pixel_x = centre_x + x, pixel_y = centre_y + y;
            if (!mark || pixel_x < 0 || pixel_x >= marker_width || pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
            auto* pixel = &framebuffer[(std::size_t(pixel_y) * std::size_t(marker_width) + std::size_t(pixel_x)) * 4];
            pixel[0] = target.valid ? 220 : 235; pixel[1] = target.valid ? 205 : 80; pixel[2] = target.valid ? 120 : 80;
        }
    };
    if (pad.teleport.button >= 0 && (!touch_enabled || last_device == touch::LastDevice::Gamepad)) teleport_marker(pad.teleport);
    if (touch::touch_interface_visible(touch_enabled, last_device) && touch_input.aiming) {
        const auto* info = scene->skills.get(touch_input.preview.skill);
        if (info && info->srvdofunc == d2d::rules::ServerDoFunction::kTeleport) {
            touch_teleport.preview = touch_input.preview;
            if (touch_skill_hold.cancelled) touch_teleport.preview.valid = false;
            teleport_marker(touch_teleport);
        }
    }
}

} // namespace d2d::client
