// SPDX-License-Identifier: GPL-3.0-or-later
// A missile's flight and the frame's missile update (missile_fly,
// missiles_update): templates on their callbacks, kept out of ai.hpp so an
// edit here recompiles only what flies missiles (fight/tick.cpp, its tests).
#pragma once

#include "ai.hpp"

#include <missiles.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::game {

// One frame of a moving missile's flight (FUN_005ae1f0's move): launched
// on its first, re-aimed at `retarget` when set (FUN_00649970 -> FUN_006492f0:
// the path's one point that unit's subtile, the fine position kept,
// FUN_0064fe40 -> FUN_0064fc60 from it to the point's centre, the room
// followed once the point is outside it; none over 99 subtiles off). Nothing
// moves a flying missile by hand (Blessed Hammer flies its route,
// rules::hammer_route). Its subtiles are the act's; `blocked` tests the 0x04 bit, and a
// subtile off its rooms reads 0x27 (blocked). False: the flight is over (a
// wall, or off its rooms).
template <class Reach>
bool missile_fly(const Level& level, Missile& missile, Reach&& reach) {
    const int origin_x = level.world_x * 5, origin_y = level.world_y * 5;
    const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
    if (!missile.launched) {
        const int from_x = origin_x + subtile(missile.x), from_y = origin_y + subtile(missile.y);
        const int path_velocity = int(std::lround(std::hypot(missile.velocity_x, missile.velocity_y) * 4096 / 5));
        const int max_velocity = missile.info ? missile.info->max_vel << 8 : path_velocity, accel = missile.info ? missile.info->accel : 0;
        const bool aimed = missile.to.has_value();
        int to_x = aimed ? origin_x + subtile(missile.to->first) : from_x, to_y = aimed ? origin_y + subtile(missile.to->second) : from_y;
        if (to_x == from_x && to_y == from_y) { ++to_x; ++to_y; }   // FUN_0059fa30: target = start moves + (1, 1)
        missile.flight = d2d::rules::MissileFlight::launch(from_x, from_y, to_x, to_y, path_velocity, max_velocity, accel);
        if (!missile.route.empty()) missile.flight.points = missile.route;
        if (missile.bolt >= 0 && aimed) {                    // FUN_005c9290 / FUN_005cd110: 77 frames at most, its wiggle
            missile.dies = std::min(missile.dies, missile.born + 77 * 40);
            d2d::rules::Rng seed{ std::uint32_t(to_x + missile.bolt) };
            if (!missile.wall_maker || seed.next() % 100 >= 20)
                missile.flight.points = d2d::rules::wiggle_points(from_x, from_y, to_x, to_y, int(missile.dies - missile.born) / 40, seed);
        }
        missile.rooms = reach(missile.x, missile.y);
        const auto& room = missile.rooms.empty() ? std::array<int, 4>{} : missile.rooms.front();
        missile.follow = to_x < room[0] || to_y < room[1] || to_x >= room[0] + room[2] || to_y >= room[1] + room[3];   // FUN_006492f0: path flag 1 when the target lies outside the room
        missile.launched = true;
    } else if (const auto retarget = std::exchange(missile.retarget, std::nullopt)) {
        const int to_x = origin_x + subtile(retarget->first), to_y = origin_y + subtile(retarget->second);
        const int at_x = missile.flight.subtile_x(), at_y = missile.flight.subtile_y();
        if (std::abs(to_x - at_x) <= 99 && std::abs(to_y - at_y) <= 99 && to_x != 0 && to_y != 0) {
            std::tie(missile.flight.aim_x, missile.flight.aim_y) = d2d::rules::missile_aim(missile.flight.x, missile.flight.y, (std::uint32_t(to_x) << 16) + 0x8000, (std::uint32_t(to_y) << 16) + 0x8000);
            const auto& room = missile.rooms.empty() ? std::array<int, 4>{} : missile.rooms.front();
            if (to_x < room[0] || to_y < room[1] || to_x >= room[0] + room[2] || to_y >= room[1] + room[3]) missile.follow = true;   // path flag 1
        }
    }
    const auto inside = [](const std::array<int, 4>& rect, int x, int y) { return x >= rect[0] && y >= rect[1] && x < rect[0] + rect[2] && y < rect[1] + rect[3]; };
    // FUN_00463740 reads the room's near list as it is now: a room come up since the list
    // was taken holds its room1, so a miss asks the list again, round its own room.
    const auto reached = [&](int x, int y) {
        const auto in_list = [&] { return std::ranges::any_of(missile.rooms, [&](const auto& rect) { return inside(rect, x, y); }); };
        if (missile.rooms.empty() || in_list()) return true;
        const auto& own = missile.rooms.front();
        missile.rooms = reach((float(own[0] + own[2] / 2 - origin_x) + 0.5f) / 5, (float(own[1] + own[3] / 2 - origin_y) + 0.5f) / 5);
        return missile.rooms.empty() || in_list();
    };
    bool flying = missile.flight.step([&](int x, int y) {
        return !reached(x, y) || level.blocked((float(x - origin_x) + 0.5f) / 5, (float(y - origin_y) + 0.5f) / 5, 0x04);
    });
    missile.x = float((double(missile.flight.x) / 65536 - origin_x) / 5);
    missile.y = float((double(missile.flight.y) / 65536 - origin_y) / 5);
    const int at_x = missile.flight.subtile_x(), at_y = missile.flight.subtile_y();
    if (flying && missile.follow && !missile.rooms.empty() && !inside(missile.rooms.front(), at_x, at_y)) {   // FUN_0064fad0
        if (reached(at_x, at_y)) missile.rooms = reach(missile.x, missile.y);
        else flying = missile.flight.stop();
    }
    return flying;
}

// Missiles fly (missile_fly; FUN_005ae1f0): a frame's move, then its
// range, then, from its Activate'th frame, a foe whose footprint holds a
// subtile it entered (rolling its to-hit) spends it either way
// (CollideKill), as does the missile barrier (0x04) or its rooms' end.
// `reach`: missile_rooms for (x, y) cells.
// A standing missile (no velocity) strikes from its own subtile
// (standing_struck). Its physical carries the owner's at SrcDamage / 128
// (monster_shot_phys).
// Flat on the ground: Missiles.txt Xoffset / Yoffset / Zoffset (row +0xa2 /
// +0xa4 / +0xa6) only shift the drawn sprite (the client's FUN_004dc7b0,
// unit type 3); no server code reads them, so a flight and its hits have
// no height.
// A friendly one asks `hits_monster` (true: it struck one, spent), and
// `flight_over` when its flight ends (FUN_00554ca0 gives 2: FUN_005adf10(0,
// 1), its hit with no unit); a monster's hit spawn goes off there too.
template <class Reach, class HitsMonster, class OwnerAlive, class FlightOver>
void missiles_update(const Level& level, std::vector<Missile>& ms_, std::span<Foe> foes, d2d::rules::Rng& rng,
                     std::uint32_t now_ms, Reach&& reach, HitsMonster&& hits_monster, OwnerAlive&& owner_alive, FlightOver&& flight_over) {
    std::vector<Missile> laid;
    const int frame = int(now_ms / 40);
    const auto spent = [&](Missile& missile) {
        missile.flight.crossed.clear();
        const bool moving = missile.velocity_x != 0 || missile.velocity_y != 0;
        // Its flight over, no unit is asked that frame (FUN_005ae1f0 returns after
        // FUN_005adf10(0, 1), before the subtiles' unit test).
        if (moving && !missile_fly(level, missile, reach)) {
            missile.flight_over = true;
            if (missile.friendly) flight_over(missile);
            return true;
        }
        if (now_ms >= missile.dies || (!moving && level.blocked(missile.x, missile.y, 0x04))) return true;
        // The client's do 55 (FUN_004d8260, "ancient death center"): every
        // third frame of its age an "ancient death cloud" (`sub`, flags 0x21)
        // from a subtile -1..0 off it each way, sent at that start's -10..9
        // off (x the first draw, y the second), the start x then y by the
        // third and fourth's bit 0: four steps of the missile's client seed
        // (+0x20: client_unit_seed at its spot, made as the client first runs it).
        if (missile.info && missile.info->clt_do == 55 && missile.sub && missile.frame != frame && (int(now_ms - missile.born) / 40) % 3 == 0) {
            missile.frame = frame;
            if (!missile.client_seeded) {
                missile.client_seed = client_unit_seed(level, room_of(level, missile.x, missile.y));
                missile.client_seeded = true;
            }
            auto& seed = missile.client_seed;
            const int off_x = int(seed.next() % 20) - 10, off_y = int(seed.next() % 20) - 10;
            const float from_x = std::floor(missile.x * 5) + float(seed.next() & 1) - 1, from_y = std::floor(missile.y * 5) + float(seed.next() & 1) - 1;
            Missile cloud = missile_from(missile, *missile.sub, (from_x + 0.5f) / 5, (from_y + 0.5f) / 5, 0, 0, now_ms);
            cloud.visual_only = true;
            cloud.to = std::pair{ (from_x + float(off_x) + 0.5f) / 5, (from_y + float(off_y) + 0.5f) / 5 };
            const float speed = missile_speed(*missile.sub, 0), dx = float(off_x), dy = float(off_y), length = std::max(std::hypot(dx, dy), 0.01f);
            cloud.velocity_x = dx / length * speed; cloud.velocity_y = dy / length * speed;
            laid.push_back(std::move(cloud));
        }
        if (missile.visual_only || (missile.info && int(now_ms - missile.born) < missile.info->activate * 40)) return false;
        if (missile.friendly) return hits_monster(missile);
        // A monster's guided one (missile do 7, FUN_005ae780: Bone Spirit)
        // turns to its foe every Param1 frames (0: 5) while that's 4..24 off
        // (FUN_006416d0: each axis less half the sizes, (short + 2 long) / 2);
        // its owner dead (FUN_005541b0), it ends.
        if (missile.info && missile.info->srv_do == 7 && !owner_alive(missile.owner)) return true;
        if (missile.info && missile.info->srv_do == 7 && missile.target >= 2) {          // past the player and merc: found again by id, or lost
            const auto found = std::ranges::find(foes, missile.target_id, &Foe::id);
            missile.target = found == foes.end() ? -1 : int(found - foes.begin());
        }
        if (missile.info && missile.info->srv_do == 7 && missile.target >= 0 && std::size_t(missile.target) < foes.size()
            && (int(now_ms - missile.born) / 40) % (missile.info->param1 < 1 ? 5 : missile.info->param1) == 0) {
            const auto& foe = foes[std::size_t(missile.target)];
            const float dx = foe.x - missile.x, dy = foe.y - missile.y;
            const int across = std::max(int(std::abs(dx) * 5) - foe.size / 2, 0), down = std::max(int(std::abs(dy) * 5) - foe.size / 2, 0);
            if (const int apart = (std::min(across, down) + 2 * std::max(across, down)) / 2; foe.alive && apart >= 4 && apart <= 24) {
                const float speed = std::hypot(missile.velocity_x, missile.velocity_y), length = std::max(std::hypot(dx, dy), 0.01f);
                missile.velocity_x = dx / length * speed; missile.velocity_y = dy / length * speed; missile.dir = missile_facing(missile.x, missile.y, dx, dy);
                missile.retarget = std::pair{ foe.x, foe.y };   // FUN_00649970 at it
            }
        }
        // A monster's fire wall: the maker (do 6) lays SubMissile1 where it
        // is each frame; the fire (do 5) burns a foe within half a cell each
        // frame, once a frame for the cast (they share `struck`), its row's
        // element in 256ths less magic damage reduction, then resistance
        // (FUN_0057bf80); the MDR scaled by the row's DamageRate / 1024
        // (FUN_0057c1e0, rules::frame_rate_flat). The life takes the record
        // total in 256ths (0057c852): the fraction is kept (Foe::frac), no draw.
        // A monster's maker (do 31: Baal's cold missiles) lays SubMissile1
        // where it is each frame, going its way; the maker strikes nothing.
        if (missile.info && missile.info->srv_do == 31) {
            if (missile.sub && missile.frame != frame) {
                missile.frame = frame;
                Missile trail = missile;
                trail.info = missile.sub; trail.sub = nullptr; trail.born = now_ms;
                trail.dies = now_ms + std::uint32_t(std::max(missile.sub->range, 1)) * 40;
                laid.push_back(std::move(trail));
            }
            return false;
        }
        // A control (do 34, FUN_005b04a0: Baal Taunt's): from its Param1'th
        // frame, each frame one of SubMissile1 / 2 by rand (those with a
        // delay, Param2 / Param3), laid where it is when its age divides by
        // that delay. The pick is on its seed (+0x20) set each frame to {its
        // subtile x, 666} (0x5b0561).
        if (missile.info && missile.info->srv_do == 34) {
            const int age = int(now_ms - missile.born) / 40;
            if (missile.frame == frame || age < missile.info->param1) return false;
            missile.frame = frame;
            const std::array delays{ missile.info->param2, missile.info->param3 };
            const int ways = !missile.info->subs[0] || delays[0] < 1 ? 0 : !missile.info->subs[1] || delays[1] < 1 ? 1 : 2;
            if (ways == 0) return true;
            d2d::rules::Rng seed{ std::uint32_t(int(std::floor(missile.x * 5))) };
            if (const int pick = seed(ways); age % delays[std::size_t(pick)] == 0)
                laid.push_back(missile_from(missile, *missile.info->subs[std::size_t(pick)], missile.x, missile.y, 0, 0, now_ms));
            return false;
        }
        if (missile.info && (missile.info->srv_do == 5 || missile.info->srv_do == 6) && missile.row) {
            if (missile.frame == frame) return false;
            missile.frame = frame;
            if (missile.sub) {
                Missile fire = missile;
                fire.info = missile.sub; fire.sub = nullptr; fire.velocity_x = fire.velocity_y = 0; fire.born = now_ms;
                fire.dies = now_ms + std::uint32_t(std::max(missile.sub->range + missile.sub->lev_range * missile.level, 1)) * 40;
                laid.push_back(std::move(fire));
                return false;
            }
            if (missile.info->srv_do != 5) return false;
            std::erase_if(*missile.struck, [&](int key) { return key / 16 < frame; });
            // FUN_005ae1f0 -> FUN_00641cb0: the first unit its footprint finds (standing_struck, by the
            // unit's size), that one alone this frame (FUN_005adf10); overlapping flames each burn.
            // ponytail: the first is the foes' order, not the near rooms' unit lists (newest first).
            for (auto& foe : foes) {
                const int key = frame * 16 + int(&foe - foes.data());
                if (!foe.alive || !standing_struck(missile, foe.x, foe.y, foe.size) || std::ranges::contains(*missile.struck, key)) continue;
                missile.struck->push_back(key);
                const auto& row = *missile.row;
                const int burn = d2d::rules::resisted_fine(rng.range(row.elo, row.ehi) - d2d::rules::frame_rate_flat(foe.fighter.mdr, missile.info->damage_rate),
                                                           foe.fighter.res[std::size_t(std::clamp(row.etype, 0, 2))]);
                const int dealt = burn >> 8;
                foe.damage += dealt; foe.elemental += dealt; foe.frac += burn & 255;
                d2d::rules::StruckBlow blow{ dealt, 0, true, 0, dealt, false, burn & 255 };   // its own record (absorbed alone), no get-hit
                blow.landed = false;
                blow.striker = missile.owner;
                foe.blows.push_back(blow);
                if (missile.owner >= 0) foe.striker_id = missile.owner;
                // Flag 1: FUN_0057cee0 puts its hit class on the unit (+0xb0), the last record's.
                // A soft hit sets unit flag 0x8000, so S->C 0x0c and the struck sound.
                foe.hit_class = missile.info->hit_class;
                if (soft_hit(missile, rng)) ++foe.soft_hits;
                break;
            }
            return false;
        }
        // A LastCollide cloud (rules' last_unit_rule) strikes one foe a frame, never the one it struck
        // last (FUN_005a8730, FUN_0064a400): two in it take turns. Spent only with CollideKill: each hit
        // function returns 3 (end) for it, else 2 (FUN_005adf10).
        const bool last_unit = missile.info && missile.info->last_unit_rule();
        if (last_unit && missile.last_hit_ms == now_ms) return false;
        for (auto& foe : foes) {
            if (!foe.alive || (moving ? !missile_struck(level, missile, foe.x, foe.y, foe.size) : !standing_struck(missile, foe.x, foe.y, foe.size))) continue;
            if (last_unit) {
                if (missile.last_hit == foe.id) continue;
                missile.last_hit = foe.id;
                missile.last_hit_ms = now_ms;
            } else {
                const int key = -100 - int(&foe - foes.data());     // a ring's missiles strike each foe once
                if (std::ranges::contains(*missile.struck, key)) continue;
                missile.struck->push_back(key);
            }
            const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), missile.src, true, rng, true);
            foe.take(taken, missile.owner);
            if (taken.hit && missile.owner >= 0) foe.shot_by.push_back(missile.owner);
            if (taken.hit && missile.info) foe.struck_by(missile.info->hit_class);
            if (missile.info && missile.info->hit_func == 19 && missile.skill >= 0) foe.marked = { missile.skill, missile.level };   // FUN_005ab110
            if (missile.info && missile.info->return_fire && missile.owner >= 0) foe.fired_by.push_back(missile.owner);
            return !missile.info || missile.info->collide_kill;
        }
        return false;
    };
    // Gone into a unit or a wall, its ExplosionMissile plays there; run
    // out, only when AlwaysExplode (FUN_004d30c0 -> FUN_004d2d70). An
    // explosion starts at frame 0: RandStart (+0x17c) is read only by the
    // client's grid maker FUN_004ced60, not by FUN_004cdba0 or FUN_0059fa30
    // (missiles.md).
    // ponytail: one that flies on through units (CollideKill 0) doesn't
    // burst at each; a guided one losing its owner bursts as a hit.
    // At its end Baal Taunt's lightning control (hit 58, FUN_005acdf0) sends
    // HitSubMissile1 at a random subtile within sHitPar1 (at least 1) of it,
    // drawn x then y on its seed (+0x20) set to {its subtile x, 666};
    // the poison one (hit 59, FUN_005acf20 -> FUN_005a9370) sends it the 8
    // ways of 0x6e2510 / 0x6e24d0's even entries at Param1 << 7, and every
    // sHitPar1'th odd one at Param2 << 7 (FUN_0059fa30 flags 0x14: x 75 / 100).
    const auto hit_spawn = [&](const Missile& missile) {
        const auto* sub = missile.info ? missile.info->hit_sub_info : nullptr;
        if (!sub || (missile.info->hit_func != 58 && missile.info->hit_func != 59)) return;
        const int spread = std::max(missile.info->hit_par1, 1);
        if (missile.info->hit_func == 58) {
            const int at_x = int(std::floor(missile.x * 5)), at_y = int(std::floor(missile.y * 5));
            d2d::rules::Rng spot{ std::uint32_t(at_x) };
            int spot_x = at_x - spread + spot(2 * spread + 1);
            int spot_y = at_y - spread + spot(2 * spread + 1);
            const float speed = missile_speed(*sub, missile.level);
            // FUN_0059fa30 (0x59fbbe): moving, with no target unit, a spot on its start subtile goes to (+1, +1).
            if (speed != 0 && spot_x == at_x && spot_y == at_y) { ++spot_x; ++spot_y; }
            const float to_x = float(spot_x) + 0.5f, to_y = float(spot_y) + 0.5f;
            const float dx = to_x / 5 - missile.x, dy = to_y / 5 - missile.y, span = std::max(std::hypot(dx, dy), 0.01f);
            laid.push_back(missile_from(missile, *sub, missile.x, missile.y, dx / span * speed, dy / span * speed, now_ms));
            laid.back().to = { missile.x + dx, missile.y + dy };
            return;
        }
        const auto ring = [&](int first, int step, int velocity) {
            const int at_x = int(std::floor(missile.x * 5)), at_y = int(std::floor(missile.y * 5));
            for (int way = first; way < 16; way += step) {
                const int dx = kRingWayX[std::size_t(way)], dy = kRingWayY[std::size_t(way)];
                const float span = std::hypot(float(dx), float(dy));
                const float speed = missile_speed((velocity << 7) * 75 / 100);   // flags 0x14: the velocity as is
                laid.push_back(missile_from(missile, *sub, missile.x, missile.y, float(dx) / span * speed, float(dy) / span * speed, now_ms));
                // FUN_005a9370 flags 0x17 include flag 2: this offset is the target, relative to the start.
                laid.back().to = { (float(at_x + dx) + 0.5f) / 5.f, (float(at_y + dy) + 0.5f) / 5.f };
            }
        };
        ring(0, 2, sub->param1);
        ring(1, spread, sub->param2);
    };
    std::erase_if(ms_, [&](Missile& missile) {
        if (!spent(missile)) return false;
        if (now_ms >= missile.dies || missile.flight_over) hit_spawn(missile);
        if (const auto* explosion = missile.info ? missile.info->explosion : nullptr; explosion && !missile.visual_only && (now_ms < missile.dies || missile.info->always_explode)) {
            Missile burst{ explosion, missile.x, missile.y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(explosion->range, 1)) * 40, {} };
            burst.visual_only = true;
            laid.push_back(std::move(burst));
        }
        return true;
    });
    std::ranges::move(laid, std::back_inserter(ms_));
}

}  // namespace d2d::game
