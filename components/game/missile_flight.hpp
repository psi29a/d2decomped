// SPDX-License-Identifier: GPL-3.0-or-later
// A missile's flight and the frame's missile update (missile_fly,
// missiles_update): templates on their callbacks, kept out of ai.hpp so an
// edit here recompiles only what flies missiles (fight/tick.cpp, its tests).
#pragma once

#include "ai.hpp"
#include "level.hpp"

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
        const bool aimed = missile.aim_at.has_value();
        int to_x = aimed ? origin_x + subtile(missile.aim_at->first) : from_x, to_y = aimed ? origin_y + subtile(missile.aim_at->second) : from_y;
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
    // FUN_005a8730, its unit filter: with an owner (EDI), only a unit hostile to it (FUN_00554200)
    // unless the row has CollideFriend (+0x187); never the owner itself (FUN_00554200 returns 0
    // when both are one unit, a pet's owner standing for it). The foes past the
    // players' side are the monsters, with their alignments (Fight::world's monster_foes).
    const auto hostile = [](const Missile& missile, const Foe& foe) {
        if (missile.owner < 0) return true;
        if (foe.monster && foe.monster->id == missile.owner) return false;
        return (missile.info && missile.info->collide_friend) || !d2d::rules::friends(missile.owner_align, foe.monster ? foe.monster->align : 2);
    };
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
            cloud.aim_at = std::pair{ (from_x + float(off_x) + 0.5f) / 5, (from_y + float(off_y) + 0.5f) / 5 };
            const float speed = missile_speed(*missile.sub, 0), dx = float(off_x), dy = float(off_y), length = std::max(std::hypot(dx, dy), 0.01f);
            cloud.velocity_x = dx / length * speed; cloud.velocity_y = dy / length * speed;
            laid.push_back(std::move(cloud));
        }
        // The client's do 32 (FUN_004d6c00, radamentdeath): its frames left (FUN_0064a380), the first
        // entry of 0x6db848 under them {300, 25}, {250, 10}, {100, 8}, {75, 6}, {30, 4} (the walk past the
        // end reads {0, 100}), and on a frame left that its period divides, a radamenthandofgod (row 0x15c,
        // `sub`) where it stands (FUN_004cd540, flags 1).
        if (missile.info && missile.info->clt_do == 32 && missile.sub && missile.frame != frame) {
            missile.frame = frame;
            static constexpr std::array<std::array<int, 2>, 6> kHands{ { { 300, 25 }, { 250, 10 }, { 100, 8 }, { 75, 6 }, { 30, 4 }, { 0, 100 } } };
            const int left = int((missile.dies - std::min(now_ms, missile.dies)) / 40);
            const auto entry = std::ranges::find_if(kHands.begin(), kHands.end() - 1, [&](const auto& row) { return row[0] < left; });
            if (left % (*entry)[1] == 0) {
                Missile hand = missile_from(missile, *missile.sub, missile.x, missile.y, 0, 0, now_ms);
                hand.visual_only = true;
                laid.push_back(std::move(hand));
            }
        }
        // The client's do 26 (FUN_004d4f20, mephistodeathcontrol), n its CltParam3: when its CltSubMissile1
        // count (data +0x28) is out, one (FUN_004ce530, flags 2) aimed n-ish subtiles off, rand(2n + 1) - n
        // on each axis on its client seed (both 0: x rand(n - 1) + 1), its way that offset, the count CltParam1;
        // likewise its CltSubMissile2 (data +0x2c, flags 0x102: a rand(5) + 1 drawn first, the offset, then its
        // data +0x28 rand(5) once made: its hit 33's count), the count CltParam2; each count steps down a frame.
        const auto client_seed = [&]() -> d2d::rules::Rng& {
            if (!missile.client_seeded) {
                missile.client_seed = client_unit_seed(level, room_of(level, missile.x, missile.y));
                missile.client_seeded = true;
            }
            return missile.client_seed;
        };
        const auto aimed = [&](const GameData::MissileInfo& info, int off_x, int off_y) {
            const float length = std::max(std::hypot(float(off_x), float(off_y)), 0.01f), speed = missile_speed(info, missile.level);
            Missile made = missile_from(missile, info, missile.x, missile.y, float(off_x) / length * speed, float(off_y) / length * speed, now_ms);
            made.visual_only = true;
            made.aim_at = std::pair{ missile.x + float(off_x) / 5, missile.y + float(off_y) / 5 };
            made.way_x = off_x; made.way_y = off_y;
            made.subtile_x = int(std::floor(missile.x * 5)); made.subtile_y = int(std::floor(missile.y * 5));
            return made;
        };
        if (missile.info && missile.info->clt_do == 26 && missile.frame != frame) {
            missile.frame = frame;
            auto& seed = client_seed();
            const int spread = missile.info->clt_params[2];
            const auto offset = [&] {
                int off_x = int(seed(2 * spread + 1)) - spread;
                const int off_y = int(seed(2 * spread + 1)) - spread;
                if (off_x == 0 && off_y == 0) off_x = int(seed(spread - 1)) + 1;
                return std::pair{ off_x, off_y };
            };
            if (const auto* maker = missile.info->clt_sub_info) {
                if (missile.clt_count[0] <= 0) {
                    const auto [off_x, off_y] = offset();
                    laid.push_back(aimed(*maker, off_x, off_y));
                    missile.clt_count[0] = missile.info->clt_params[0];
                }
                --missile.clt_count[0];
            }
            if (const auto* rock = missile.info->clt_sub2_info) {
                if (missile.clt_count[1] <= 0) {
                    (void)seed(5);                                  // record +0x30
                    const auto [off_x, off_y] = offset();
                    laid.push_back(aimed(*rock, off_x, off_y));
                    laid.back().clt_count[0] = int(seed(5));        // the rock's data +0x28
                    missile.clt_count[1] = missile.info->clt_params[1];
                }
                --missile.clt_count[1];
            }
        }
        // The client's do 27 (FUN_004d5090, mephistofirewallmaker): on a frame it entered a new subtile its
        // CltSubMissile1 there (mephistofirewall); every CltParam1 frames of its age a client seed step turns
        // its way: & 3 == 0 to ((x - y) x 3 / 4, (y + x) x 3 / 4), == 2 to ((x + y) x 3 / 4, (y - x) x 3 / 4)
        // (each truncated toward 0), else on; either way the flight re-aimed at its subtile plus the way
        // (FUN_00648ad0, FUN_00649970: `retarget`).
        if (missile.info && missile.info->clt_do == 27 && missile.frame != frame) {
            missile.frame = frame;
            const int at_x = int(std::floor(missile.x * 5)), at_y = int(std::floor(missile.y * 5));
            if (const auto* fire = missile.info->clt_sub_info; fire && (at_x != missile.subtile_x || at_y != missile.subtile_y)) {
                missile.subtile_x = at_x; missile.subtile_y = at_y;
                Missile flame = missile_from(missile, *fire, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0, 0, now_ms);
                flame.visual_only = true;
                laid.push_back(std::move(flame));
            }
            if (int(now_ms - missile.born) / 40 % std::max(missile.info->clt_params[0], 1) == 0) {
                const std::uint32_t pick = client_seed().next() & 3;
                const int way_x = missile.way_x, way_y = missile.way_y;
                if (pick == 0) { missile.way_x = (way_x - way_y) * 3 / 4; missile.way_y = (way_y + way_x) * 3 / 4; }
                else if (pick == 2) { missile.way_x = (way_x + way_y) * 3 / 4; missile.way_y = (way_y - way_x) * 3 / 4; }
                missile.retarget = std::pair{ (float(at_x + missile.way_x) + 0.5f) / 5, (float(at_y + missile.way_y) + 0.5f) / 5 };   // FUN_00648ad0, FUN_00649970
            }
        }
        if (missile.visual_only || (missile.info && int(now_ms - missile.born) < missile.info->activate * 40)) return false;
        if (missile.friendly) return hits_monster(missile);
        // A monster's guided one (missile do 7, FUN_005ae780: Bone Spirit)
        // turns to its foe every Param1 frames (0: 5) while that's 4..24 off
        // (FUN_006416d0: each axis less half the sizes, (short + 2 long) / 2);
        // its owner dead (FUN_005541b0), FUN_005ae780 returns 2 and FUN_005adbb0
        // frees it (FUN_00555600): no hit function (FUN_005adf10 isn't run), no
        // HitSubMissile, and no remove packet (S->C 0x0a, FUN_0053bda0, sends
        // nothing for a missile), so the client's copy flies on to its own end:
        // here the server half goes, the drawn half stays.
        if (missile.info && missile.info->srv_do == 7 && !owner_alive(missile.owner)) {
            missile.visual_only = true;
            return false;
        }
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
                fire.struck = std::make_shared<std::vector<int>>();   // each flame its own (FUN_005ae1f0 keeps nothing across missiles)
                laid.push_back(std::move(fire));
                return false;
            }
            if (missile.info->srv_do != 5) return false;
            // Keys this frame's: (frame mod 0x40000) x 4096 + the foe's index (the monsters' past the players'
            // side when they're foes); an older frame's go, a wrapped one's at the latest a round later.
            const int keyed = frame % 0x40000;
            std::erase_if(*missile.struck, [&](int key) { return key < 0 || key / 4096 != keyed; });
            // FUN_005ae1f0 -> FUN_00641cb0: the first unit its footprint finds (standing_struck, by the
            // unit's size), that one alone this frame (FUN_005adf10); overlapping flames each burn.
            // FUN_00641cb0 walks its room's near list (room +0 / +0x24: missile.rooms past its own
            // rect, in that order), each room's units newest first (room +0x74; rules::room_order).
            std::vector<int> walk;
            if (missile.rooms.empty()) missile.rooms = reach(missile.x, missile.y);   // a standing one: where it lies
            if (missile.rooms.size() > 1) {
                const int origin_x = level.world_x * 5, origin_y = level.world_y * 5;
                std::vector<int> rooms;
                for (int listed = 1; listed < int(missile.rooms.size()); ++listed) rooms.push_back(listed);
                std::vector<d2d::rules::RoomUnit> listed_units;
                listed_units.reserve(foes.size());
                for (const auto& foe : foes) {
                    const int at_x = origin_x + int(std::floor(foe.x * 5)), at_y = origin_y + int(std::floor(foe.y * 5));
                    const auto holds = std::ranges::find_if(rooms, [&](int listed) {
                        const auto& rect = missile.rooms[std::size_t(listed)];
                        return at_x >= rect[0] && at_y >= rect[1] && at_x < rect[0] + rect[2] && at_y < rect[1] + rect[3];
                    });
                    listed_units.push_back({ holds == rooms.end() ? -1 : *holds, true, foe.room_stamp });
                }
                walk = d2d::rules::room_order(listed_units, rooms);
            } else {
                for (int index = 0; index < int(foes.size()); ++index) walk.push_back(index);   // no rooms kept: the foes' order
            }
            for (const int index : walk) {
                auto& foe = foes[std::size_t(index)];
                const int key = keyed * 4096 + index;
                if (!foe.alive || !hostile(missile, foe) || !standing_struck(missile, foe.x, foe.y, foe.size) || std::ranges::contains(*missile.struck, key)) continue;
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
            if (!hostile(missile, foe)) continue;
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
            const bool ends = !missile.info || missile.info->collide_kill;
            // The client's copy meets the unit too (ClientCol, FUN_004d30c0 -> FUN_004d2d70): with
            // CollideKill 0 it flies on, but its ExplosionMissile goes off at its spot first.
            if (!ends && moving && missile.info->client_col && missile.info->explosion && !missile.visual_only) {
                const auto* explosion = missile.info->explosion;
                Missile burst{ explosion, missile.x, missile.y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(explosion->range, 1)) * 40, {} };
                burst.visual_only = true;
                laid.push_back(std::move(burst));
            }
            return ends;
        }
        return false;
    };
    // Gone into a unit or a wall, its ExplosionMissile plays there; run
    // out, only when AlwaysExplode (FUN_004d30c0 -> FUN_004d2d70). An
    // explosion starts at frame 0: RandStart (+0x17c) is read only by the
    // client's grid maker FUN_004ced60, not by FUN_004cdba0 or FUN_0059fa30
    // (missiles.md).
    // One that flies on through units (CollideKill 0) with ClientCol bursts
    // at each unit the client's copy meets (FUN_004d2d70 runs ExplosionMissile
    // before it tests CollideKill, then returns 0); a row with no ClientCol
    // (impfireball, spidergoolay) tests no units there. Not traced: the unit
    // flag the client reads (+0xc4 bit 2) and FUN_004cd310's change to the
    // hit mask; a friendly missile's burst per unit (Fight, not here).
    // At its end Baal Taunt's lightning control (hit 58, FUN_005acdf0) sends
    // HitSubMissile1 at a random subtile within sHitPar1 (at least 1) of it,
    // drawn x then y on its seed (+0x20) set to {its subtile x, 666};
    // the poison one (hit 59, FUN_005acf20 -> FUN_005a9370) sends it the 8
    // ways of 0x6e2510 / 0x6e24d0's even entries at Param1 << 7, and every
    // sHitPar1'th odd one at Param2 << 7 (FUN_0059fa30 flags 0x14: x 75 / 100). Both lay nothing
    // without an owner (FUN_00552fd0 gates each, FUN_005acdf0 / FUN_005acf20: missile.owner < 0).
    const auto hit_spawn = [&](const Missile& missile) {
        const auto* sub = missile.info ? missile.info->hit_sub_info : nullptr;
        if (!sub || (missile.info->hit_func != 58 && missile.info->hit_func != 59) || missile.owner < 0) return;
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
            laid.back().aim_at = { missile.x + dx, missile.y + dy };
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
                laid.back().aim_at = { (float(at_x + dx) + 0.5f) / 5.f, (float(at_y + dy) + 0.5f) / 5.f };
            }
        };
        ring(0, 2, sub->param1);
        ring(1, spread, sub->param2);
    };
    // The client's hit 33 (FUN_004d07e0: mephistoflyingrocksbig's) as it goes off (FUN_004d2d70): its
    // data +0x28 (the count do 26 gave it) CltHitSubMissile1s (flags 0x102) from where it is, each a
    // rand(5) + 1 for its record +0x30, then aimed rand(2n + 1) - n subtiles off on each axis (both 0:
    // x rand(n) + 1), n cHitPar1 (at least 3), its way that offset; all on its client seed (+0x20).
    const auto rock_shards = [&](Missile& missile) {
        const auto* shard = missile.info ? missile.info->clt_hit_subs[0] : nullptr;
        if (!shard || missile.info->clt_hit != 33) return;
        if (!missile.client_seeded) {
            missile.client_seed = client_unit_seed(level, room_of(level, missile.x, missile.y));
            missile.client_seeded = true;
        }
        auto& seed = missile.client_seed;
        const int spread = std::max(missile.info->clt_hit_pars[0], 3);
        const float speed = missile_speed(*shard, missile.level);
        for (int made = 0; made < missile.clt_count[0]; ++made) {
            (void)seed(5);                                          // record +0x30
            int off_x = int(seed(2 * spread + 1)) - spread;
            const int off_y = int(seed(2 * spread + 1)) - spread;
            if (off_x == 0 && off_y == 0) off_x = int(seed(spread)) + 1;
            const float length = std::max(std::hypot(float(off_x), float(off_y)), 0.01f);
            Missile small = missile_from(missile, *shard, missile.x, missile.y, float(off_x) / length * speed, float(off_y) / length * speed, now_ms);
            small.visual_only = true;
            small.aim_at = std::pair{ missile.x + float(off_x) / 5, missile.y + float(off_y) / 5 };
            small.way_x = off_x; small.way_y = off_y;
            small.subtile_x = int(std::floor(missile.x * 5)); small.subtile_y = int(std::floor(missile.y * 5));
            laid.push_back(std::move(small));
        }
    };
    std::erase_if(ms_, [&](Missile& missile) {
        if (!spent(missile)) return false;
        if (now_ms >= missile.dies || missile.flight_over) hit_spawn(missile);
        if (now_ms < missile.dies || (missile.info && missile.info->always_explode)) rock_shards(missile);
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
