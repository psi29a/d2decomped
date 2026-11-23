// SPDX-License-Identifier: GPL-3.0-or-later
// ClientSession (session.hpp): the client's unique events (S->C 0x89, the quest event
// table 0x7129d8) and the Horadric Staff's light show they start
// (docs/research/re/quests-act2.md "The orifice", rules/staff_show.hpp).
// The rest is in this directory.
#include "session.hpp"

#include "scene.hpp"

#include <cues.hpp>
#include <level.hpp>
#include <log.hpp>
#include <missiles.hpp>
#include <monsters.hpp>
#include <object_ids.hpp>
#include <shake.hpp>
#include <staff_show.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::client {

namespace {

// Missiles.txt names of rules::staff_show's rows (338, 340, 339, 150).
constexpr std::string_view kStaffName = "horadricstaff", kBeamName = "horadriclight", kLightningName = "horadriclightning", kDustName = "dust";

}  // namespace

// 0x89 {event}: 3, the staff in the orifice (0x46b100), finds the orifice
// (FUN_0065a620: class 0x98, in the player's room; d2d: OperateFn 25) and
// starts its light show there, light_jet on it once (FUN_00470390 kind 2);
// no orifice, nothing.
// The staff's seed (unit +0x20, the dust's spread): a client missile's, FUN_004cd540 -> FUN_00466730 ->
// FUN_00465fd0 (game-calls.tsv): client_unit_seed at the orifice.
auto ClientSession::unique_event(int event, std::uint32_t frame_ms) -> void {
    if (event != 3 || !level) return;
    const auto orifice = std::ranges::find(level->npcs, d2d::rules::operate_fn::kOrifice, &Npc::operate_fn);
    if (orifice == level->npcs.end()) return;
    staff_show = { .playing = true, .level = level, .orifice = int(orifice - level->npcs.begin()), .x = orifice->x, .y = orifice->y, .born = frame_ms,
                   .seed = client_unit_seed(*level, room_of(*level, orifice->x, orifice->y)) };
    d2d::log::info("The Horadric Staff: its light show starts");
}

// One client frame: the staff (missile 338) made the first, then its client
// function 31 (FUN_004d6820) on its frames left, then every shot moved,
// aged and gone at its life's end. Each one made plays its TravelSound
// (FUN_004cd540, record +0x12). Off its level, it's gone.
auto ClientSession::staff_show_tick(std::uint32_t frame_ms) -> void {
    namespace show = d2d::rules::staff_show;
    auto& state = staff_show;
    if (!state.playing || !scene) return;
    if (state.level != level) { state = {}; return; }
    const auto make = [&](std::string_view name, float x, float y, int dir, int life, float to_x, float to_y, int rises) {
        const auto found = scene->missiles.find(std::string(name));
        if (found == scene->missiles.end() || life <= 0) return;
        const auto& info = found->second;
        // (Vel + VelLev x lvl / 8) << 8, 75 % of it: path velocity / 4096 subtiles a frame (FUN_004cd540).
        const float step = float(d2d::rules::missile_velocity(info.vel, info.vel_lev, 0)) / 4096.f / 5.f;
        const float dx = to_x - x, dy = to_y - y, length = std::hypot(dx, dy);
        ShowShot shot{ .shot = { &info, x, y, dir, frame_ms }, .life = life, .rises = rises };
        if (step > 0 && length > 0) { shot.step_x = dx / length * step; shot.step_y = dy / length * step; }
        shot.shot.motion_z = -int(rises == 1 ? show::staff_height(0) : rises == 2 ? show::lightning_height(0) : 0.f);   // up the screen
        state.shots.push_back(shot);
        if (info.travel_sound > 0) cues.cue_on(0, info.travel_sound, frame_ms, x, y);
    };
    const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
    const int frame = state.frames++;
    const int remaining = show::kStaffRange - frame;
    if (frame == 0) make(kStaffName, state.x, state.y, 0, show::kStaffRange, state.x, state.y, 1);
    if (remaining > 0) {
        const auto made = show::step(remaining);
        const float way_x = state.x + float(show::kWayDownX) / 5, way_y = state.y + float(show::kWayDownY) / 5;
        if (made.shake) shake.start(show::kShakeStrength, 0, show::kShakeHoldMs, 0, frame_ms);   // FUN_00476a80
        if (made.beam >= 0) {
            const auto& beam = show::kBeams[std::size_t(made.beam)];
            const float beam_x = state.x + float(beam.dx) / 5, beam_y = state.y + float(beam.dy) / 5;
            make(kBeamName, beam_x, beam_y, beam.dir, remaining, beam_x, beam_y, 0);
        }
        if (made.lightning)
            if (const auto bolt = scene->missiles.find(std::string(kLightningName)); bolt != scene->missiles.end())
                make(kLightningName, state.x, state.y, 0x15, bolt->second.range, way_x, way_y, 2);
        if (made.dust)
            if (const auto dust = scene->missiles.find(std::string(kDustName)); dust != scene->missiles.end()) {
                const auto spread = show::dust_target(state.seed);
                const float to_x = way_x + float(spread.dx) / 5, to_y = way_y + float(spread.dy) / 5;
                make(kDustName, way_x, way_y, d2d::rules::direction64(subtile(way_x), subtile(way_y), subtile(to_x), subtile(to_y)), dust->second.range, to_x, to_y, 0);
            }
    }
    for (auto& shot : state.shots) {
        ++shot.age;
        shot.shot.x += shot.step_x;
        shot.shot.y += shot.step_y;
        if (shot.rises == 1) shot.shot.motion_z = -int(show::staff_height(shot.age));
        if (shot.rises == 2) shot.shot.motion_z = -int(show::lightning_height(shot.age));
    }
    std::erase_if(state.shots, [](const ShowShot& shot) { return shot.age >= shot.life; });
    if (remaining <= 0 && state.shots.empty()) state.playing = false;
}

}  // namespace d2d::client
