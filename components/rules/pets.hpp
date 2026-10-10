// SPDX-License-Identifier: GPL-3.0-or-later
// The pets' think (MonAI 67 "NecroPet", FUN_005e4cf0: the golems, the
// Valkyrie, the skeletons and skeletal mages) and how a pet follows its
// owner (FUN_005e3ea0). tools/emu/necropet.py checks it against game.exe.
#pragma once

#include "merc.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace d2d::rules {

// What the think sees, as MercView (subtiles). `cur`: the owner's path +0x10
// (where it's at on its path), `end` its end (+0x18). `arrive`: where the
// owner last arrived through a warp or teleport (player data +0x148,
// FUN_00554ea0). `pets`: how many pets the owner has (FUN_00574f40);
// `crowd`: how many of them stand within 2 of this one (FUN_005e3900).
// `velocity` / `run`: MonStats. The foe (FUN_005dd7f0): its spot, the
// search's distance and whether it's in melee (FUN_00622c40), `ignored`
// its +0xc4 bit 30; `clear` a line to it (FUN_005dc640).
struct PetView {
    int x = 0, y = 0, size = 1;
    int owner_x = 0, owner_y = 0, owner_mode = 1, cur_x = 0, cur_y = 0, end_x = 0, end_y = 0;
    std::array<std::pair<int, int>, 20> ring{};
    int cursor = 0, arrive_x = 0, arrive_y = 0, pets = 1, crowd = 0;
    bool town = false;
    int velocity = 0, run = 0;
    bool foe = false, foe_melee = false, foe_ignored = false, clear = true;
    int foe_x = 0, foe_y = 0, foe_distance = 0;
};

// The units a think deals with: itself, its owner, the foe its search
// found (FUN_005dd7f0), the one the merc's search or the AI driver found
// (FUN_005ddc30, AI params [2]), and one `nearby` the owner (FUN_005d2f80) or
// the corpse the Death Sentry blasts (FUN_0056e390).
enum class PetUnit { none, pet, owner, foe, foe2, nearby };

// A think's outcome: a move that found a path, every move failing, a stand
// (frames), a teleport (FUN_005e3ea0 mode 3), a swing (FUN_005ddf90 mode 4)
// or a walk / run at a unit (`chase`, the move's `unit`), a skill
// (FUN_005dead0 in `mode`, or the sequence FUN_005de000) at a unit or a
// spot, or death (mode 0, FUN_005ddfc0). `frames`: a stand's, or the wait
// after a swing or a skill (FUN_005de0f0). `unreachable`: a walk at a unit
// found no path (AI control +8 bit 1, FUN_005dd230).
struct PetAct {
    enum class Kind { moved, failed, stand, teleport, swing, chase, skill, seq, die } kind = Kind::failed;
    MercMove move;
    int frames = 0;
    bool unreachable = false;
    PetUnit unit = PetUnit::none;
    int skill = -1, mode = 0, x = 0, y = 0;
};

// How a pet follows its owner, on a PetView: FUN_005e3ea0 (`follow`) and
// FUN_005e45d0 (`decide`, follow or fight). Both put what they did in
// `act`; `level_at(x, y)` names the level under a subtile (FUN_0061b130);
// `try_move(move)` sets off, true when it finds a path.
template <class LevelAt, class TryMove>
class PetFollow {
public:
    PetFollow(const PetView& view, Rng& seed, LevelAt& level_at, TryMove& try_move)
        : view_(view), seed_(seed), level_at_(level_at), try_move_(try_move) {}
    PetAct act{ .kind = PetAct::Kind::failed };
    MercMove ctx;                                                 // FUN_005a6260: only what's not 0 is set

    void pace(int type, int pct, int steps) {
        if (type) ctx.type = type;
        if (pct) ctx.pct = pct;
        if (steps) ctx.steps = std::min(steps, 0x4d);
    }
    bool set_out(int x, int y, int mode = kMonsterWalk, PetUnit unit = PetUnit::none) {
        MercMove move = ctx;
        move.x = x; move.y = y; move.mode = mode; move.unit = int(unit);
        ctx = {};
        if (!try_move_(move)) return false;
        act = { .kind = unit != PetUnit::none ? PetAct::Kind::chase : PetAct::Kind::moved, .move = move, .unit = unit };
        return true;
    }
    // FUN_005df400 / df530 / de200: one step of the seed picks the axis
    // that's `reach` off (low bit 0: y), the other rand(reach), two more
    // steps their signs.
    bool wander(int centre_x, int centre_y, int reach) {
        int off_x = 0, off_y = 0;
        if ((seed_.next() & 1) == 0) { off_x = seed_(reach); off_y = reach; }
        else { off_x = reach; off_y = seed_(reach); }
        if (seed_.next() & 1) off_x = -off_x;
        if (seed_.next() & 1) off_y = -off_y;
        return set_out(centre_x + off_x, centre_y + off_y);
    }
    bool stand(int frames) { act = { .kind = PetAct::Kind::stand, .frames = frames }; return true; }
    // FUN_005defe0: `reach` off on each axis, away from (x, y); over 5, the
    // path's steps are `reach`.
    bool away(int from_x, int from_y, int reach) {
        const int away_x = view_.x < from_x ? -1 : from_x < view_.x ? 1 : 0, away_y = view_.y < from_y ? -1 : from_y < view_.y ? 1 : 0;
        if (reach > 5) pace(0, 0, reach);
        return set_out(view_.x + reach * away_x, view_.y + reach * away_y);
    }

    // FUN_005e3ea0: after the owner, by mode (run: running moves). 0: 8 off
    // the owner's path end ahead of it, round the compass on its level, each
    // spot then halfway to it; else wander 4, halfway to the owner, the owner.
    // 1: the owner walking, its path end then halfway; then its footsteps
    // as the merc's (pace rand(40) + 40 unless given); none: wander a
    // quarter of the gap. 2: 10 in 100 wander 3..5, else its path end. 3: a
    // teleport. 4: with others crowding it, `reach` away, else wander
    // `reach`. 5: path type 7, `reach` about the owner, `reach` away, its
    // path end. 2 and 4 stand 15 when nothing took.
    bool follow(int mode, bool run, int pct, int reach) {
        const int move_mode = run ? kMonsterRun : kMonsterWalk;
        const int pet_x = view_.x, pet_y = view_.y, own_x = view_.owner_x, own_y = view_.owner_y, end_x = view_.end_x, end_y = view_.end_y;
        if (mode == 3) { act = { .kind = PetAct::Kind::teleport }; return true; }   // ponytail: d2d's teleport always lands
        if (mode == 0) {
            static constexpr std::array<std::pair<int, int>, 8> kAhead{ { { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 } } };
            int eighth = ((direction64(own_x, own_y, end_x, end_y) + 4) >> 3) & 7;
            const int level = level_at_(own_x, own_y);
            for (int turn = 0; turn < 8; ++turn, eighth = (eighth + 1) & 7) {
                const int x = end_x + kAhead[std::size_t(eighth)].first * 8, y = end_y + kAhead[std::size_t(eighth)].second * 8;
                if (level_at_(x, y) != level) continue;
                pace(0, pct, 0x28);
                if (set_out(x, y, move_mode)) return true;
                if (set_out(int(std::uint32_t(pet_x + x) >> 1), int(std::uint32_t(pet_y + y) >> 1), move_mode)) return true;
            }
            if (wander(pet_x, pet_y, 4)) return true;
            if (run && set_out((pet_x + own_x) / 2, (pet_y + own_y) / 2, kMonsterRun)) return true;
            if (set_out((pet_x + own_x) / 2, (pet_y + own_y) / 2)) return true;
            return set_out(own_x, own_y);
        }
        if (mode == 1) {
            if (view_.owner_mode == kPlayerWalk) {
                pace(0, 0, 100);
                if (set_out(end_x, end_y) || set_out((pet_x + end_x) / 2, (pet_y + end_y) / 2)) return true;
            }
            if (pct == 0) pct = seed_(0x28) + 0x28;
            bool tried = false;
            for (int looked = 0, cursor = view_.cursor;;) {
                cursor = cursor == 0 ? 0x13 : cursor - 1;
                const auto [x, y] = view_.ring[std::size_t(cursor)];
                if (x != 0 && y != 0 && npc_distance(pet_x, pet_y, x, y) > 5) {
                    pace(0, pct, 100);
                    if (set_out(x, y, move_mode)) return true;
                    pace(0xf, pct, 100);
                    if (set_out(x, y, move_mode)) return true;
                    if (!tried) {
                        tried = true;
                        pace(1, pct, 100);
                        if (set_out(x, y, move_mode)) return true;
                    }
                }
                if (++looked > 0x13) {
                    pace(0, 0xf, 0);
                    return wander(pet_x, pet_y, std::max(int(std::uint32_t(merc_gap(pet_x, pet_y, view_.size, own_x, own_y)) >> 2), 4));
                }
            }
        }
        if (mode == 2 && seed_(100) < 10) {
            if (wander(pet_x, pet_y, seed_(3) + 3)) return true;
            pace(0, 0, 0x28);
            if (set_out(end_x, end_y)) return true;
        }
        if (mode == 4 && view_.crowd > 0 && (away(own_x, own_y, reach) || wander(pet_x, pet_y, reach))) return true;
        if (mode == 5) {
            pace(7, 0, 0);
            if (wander(own_x, own_y, reach) || away(own_x, own_y, reach)) return true;
            pace(0, 0, 0x28);
            return set_out(end_x, end_y);
        }
        return stand(15);
    }
    // FUN_005e45d0: whether (and how) it follows instead of fighting.
    bool decide(bool foe, bool melee, bool stay, int reach) {
        const int pet_x = view_.x, pet_y = view_.y, own_x = view_.owner_x, own_y = view_.owner_y;
        const int gap = merc_gap(pet_x, pet_y, view_.size, own_x, own_y);
        reach = std::min(reach + (view_.pets >> 1), 0x24);
        if (gap <= 1 && view_.owner_mode == kMonsterNeutral && !melee) return follow(5, false, 0, reach);
        if (foe && !view_.town) return gap > 0x50 && follow(3, false, 0, reach);
        int mode = 2;
        if (view_.owner_mode == kPlayerWalk || view_.owner_mode == kPlayerTownWalk || view_.owner_mode == kPlayerRun) mode = 0;
        if (view_.cur_x != view_.end_x && view_.cur_y != view_.end_y) mode = 0;
        if (level_at_(own_x, own_y) != level_at_(pet_x, pet_y)) mode = 1;
        if (gap > reach) mode = 1;
        if (gap > 0x32) mode = 3;
        if (npc_distance(pet_x, pet_y, view_.arrive_x, view_.arrive_y) < 0x1c && ((mode == 1 && gap < 0x1e) || mode == 2)) mode = 4;
        if (stay && mode == 2) return false;
        return follow(mode, false, 0, reach);
    }

private:
    const PetView& view_;
    Rng& seed_;
    LevelAt& level_at_;
    TryMove& try_move_;
};

// The think (FUN_005e4cf0 → FUN_005e4830; +0x14 is only set by a talk, so
// FUN_005e4ac0 never runs for a pet). Over 50 from the owner: a teleport;
// 29 or over: after the owner (FUN_005e3ea0 mode 0) at MonStats Run x 100 /
// Velocity - 100 % pace (100 when that's over 99 or Velocity is 0). Else
// the foe: one within 24 the search found that's 6 or nearer, else any it
// found under 36 by merc_gap (not bit 30); none without a clear line. A
// step of its seed % 100 under 15 lets it stay put instead of following
// (and adds a subtile of reach). Then FUN_005e45d0 decides whether to follow;
// if not: with a foe outside town, in melee 80 in 100 a swing (else stand
// 10), out of melee a walk at it (pace 0xc steps, path type 0xd, 1 off) —
// none found marks it unreachable and 70 in 100 wanders 4, else stands 10.
// No foe: it wanders 4. `level_at(x, y)` names the level under a subtile
// (FUN_0061b130); `try_move(move)` sets off, true when it finds a path
// (`move.foe`: at the foe).
template <class LevelAt, class TryMove>
PetAct necropet_think(const PetView& view, Rng& seed, LevelAt&& level_at, TryMove&& try_move) {
    using Kind = PetAct::Kind;
    PetFollow brain(view, seed, level_at, try_move);
    const int pet_x = view.x, pet_y = view.y;
    const int gap = merc_gap(pet_x, pet_y, view.size, view.owner_x, view.owner_y);
    if (gap > 0x32) { brain.follow(3, false, 0, 0); return brain.act; }
    const int pct = view.velocity < 1 || view.run * 100 / view.velocity - 100 > 99 ? 100 : view.run * 100 / view.velocity - 100;
    if (gap >= 0x1d) { brain.follow(0, false, pct, 0); return brain.act; }
    bool foe = view.foe && view.foe_distance <= 0x18;
    if (!foe || view.foe_distance > 6) foe = view.foe && !view.foe_ignored && merc_gap(pet_x, pet_y, view.size, view.foe_x, view.foe_y) < 0x24;
    if (!view.clear) foe = false;
    const bool stay = seed.next() % 100 <= 14;
    if (brain.decide(foe, view.foe && view.foe_melee, stay, stay ? 8 : 7)) return brain.act;
    if (foe && !view.town) {
        if (view.foe_melee) {
            if (seed(100) > 0x4f) return { .kind = Kind::stand, .frames = 10 };
            return { .kind = Kind::swing, .unit = PetUnit::foe };
        }
        brain.pace(0, 0, 0xc);
        brain.pace(0xd, 0, 0);
        if (brain.set_out(view.foe_x, view.foe_y, kMonsterWalk, PetUnit::foe)) return brain.act;
        if (seed.next() % 100 < 0x46) brain.wander(pet_x, pet_y, 4);
        else brain.stand(10);
        brain.act.unreachable = true;
        return brain.act;
    }
    brain.wander(pet_x, pet_y, 4);
    return brain.act;
}

// The pet AIs whose thinks are traced (tools/emu/pet_ais.py) and that d2d
// summons: rules::PetBrain runs them (NecroPet has necropet_think).
inline bool traced_pet_ai(std::string_view ai_name) {
    static constexpr std::array<std::string_view, 11> kTraced{ "Hydra", "AssassinSentry", "DeathSentry", "Raven", "DruidWolf", "Totem", "Vines", "CycleOfLife", "DruidBear",
                                                               "ShadowWarrior", "ShadowMaster" };
    return std::ranges::contains(kTraced, ai_name);
}

// What the other pets' thinks read (tools/emu/pet_ais.py feeds game.exe the
// same): the pet's class (MonStats hcIdx), its owner (or none) and its mode,
// town (the pet's room), the frame (game +0xa8); each unit's spot (by
// PetUnit), their gaps (FUN_005dc380: merc_gap from the first unit's size)
// and distances (FUN_006416d0: each axis less half of both sizes, then
// (short + 2 x long) / 2); the foe the search found (distance, in melee),
// the merc-style search's (`foe2`), the one near the owner, the AI driver's
// target (AI params [2] is `foe2`, [5] its distance, [6] in melee); per unit
// a clear line (FUN_005dc640), in melee of the pet (FUN_00622c40), dying
// (FUN_005541b0); the foe2 poisoned (state 2) and its poison resist (stat
// 0x2d), the pet slowed (state 0x3c) or in Fenris's rage (0x8a), the
// owner's life / mana (stats 6 / 8) and their max (FUN_00625d10 / d60);
// the owner's summoning skill: its calc (FUN_00646ca0: shots, hits, life
// frames, radius), level, mode (FUN_00644360); the Death Sentry's corpse
// (`nearby`, its id) and blast radius (FUN_004cc7c0); MonStats aip1..5 for
// the difficulty, Skill1 / 2, Sk1mode / 2, Velocity, Run; Blade Sentinel's
// two ends (FUN_0058ee80 +0xc / +0x14). The Shadows: the owner's left /
// right skill (FUN_00620190 / FUN_006201d0, -1 none) and class, the pet has Attack;
// per skill id its class (FUN_00645040, 7 none), AI use (FUN_005eabf0),
// mana (FUN_006459f0), kind (FUN_00645460), mode and delay; MonStats aip8 (NM) / (H)
// (row +0x82 / +0x84, whatever the difficulty).
struct ShadowSkill {
    int id = 0, cls = 7;
    bool ai_ok = false;
    int mana = 0, kind = 0, mode = 0, delay = 0;      // delay: Skills.txt delay at its level (+0x190)
};
struct PetScene {
    int cls = 0;
    bool owner = true;
    int owner_id = 0, owner_mode = 1;
    bool town = false;
    int frame = 0;
    std::array<std::pair<int, int>, 6> spot{};
    std::array<std::array<int, 6>, 6> gap{}, distance{};
    bool foe = false, foe_melee = false, foe2 = false, foe2_melee = false, nearby = false;
    int foe_distance = 0, foe2_distance = 0;
    bool driver = false, driver_melee = false;
    int driver_distance = 0;
    std::array<bool, 6> clear{}, melee_of{}, dying{};
    bool poisoned = false, slowed = false, raging = false;
    int poison_resist = 0, life = 0, mana = 0, max_life = 0, max_mana = 0;
    int skill_calc = 0, skill_level = 0, skill_mode = 0;
    int corpse_id = 0, radius = 0;
    std::array<int, 5> aip{};
    int skill1 = -1, skill2 = -1, mode1 = 0, mode2 = 0, velocity = 0, run = 0;
    bool ends = false;
    std::array<std::pair<int, int>, 2> end{};
    int left = -1, right = -1, owner_class = 0;
    bool pet_attack = false;
    int aip8_nightmare = 0, aip8_hell = 0;
    std::array<ShadowSkill, 4> skills{};
    [[nodiscard]] const ShadowSkill& skill_of(int id) const {
        static constexpr ShadowSkill kNone;
        const auto found = std::ranges::find(skills, id, &ShadowSkill::id);
        return found == skills.end() ? kNone : *found;
    }
};

// How many rows Skills.txt has (a skill id past it counts as none).
inline constexpr int kSkillRows = 400;

// A think on a PetScene. `world` answers: try_move(move) (a path found),
// follow(mode, run, pct, reach) (FUN_005e3ea0) and decide(foe, melee,
// stay, reach) (FUN_005e45d0, foe a PetUnit) — on true their act is
// world.followed() — and teleport() (FUN_00554ea0 into the owner's room);
// the Shadow Warrior's give_hands() (it takes the owner's left / right).
// `ctrl`: the AI control's +0x14 / +0x18 / +0x1c, kept between thinks.
template <class World>
class PetBrain {
public:
    PetBrain(const PetScene& seen, Rng& rolls, std::array<int, 3>& control, World& answers) : scene(seen), seed(rolls), ctrl(control), world(answers) {}
    const PetScene& scene;
    Rng& seed;
    std::array<int, 3>& ctrl;
    World& world;
    PetAct act{ .kind = PetAct::Kind::failed };
    MercMove ctx;

    [[nodiscard]] int aip(int which) const { return scene.aip[std::size_t(which)]; }
    [[nodiscard]] auto spot(PetUnit unit) const { return scene.spot[std::size_t(unit)]; }
    [[nodiscard]] int gap(PetUnit from, PetUnit unit) const { return scene.gap[std::size_t(from)][std::size_t(unit)]; }
    [[nodiscard]] int distance(PetUnit from, PetUnit unit) const { return scene.distance[std::size_t(from)][std::size_t(unit)]; }
    [[nodiscard]] int pct() const {
        return scene.velocity < 1 || scene.run * 100 / scene.velocity - 100 > 99 ? 100 : scene.run * 100 / scene.velocity - 100;
    }
    void pace(int type, int pct_now, int steps) {
        if (type) ctx.type = type;
        if (pct_now) ctx.pct = pct_now;
        if (steps) ctx.steps = std::min(steps & 0xff, 0x4d);
    }
    bool set_out(int x, int y, int mode = kMonsterWalk, PetUnit unit = PetUnit::none) {   // FUN_005a7c20
        MercMove move = ctx;
        move.x = x; move.y = y; move.mode = mode; move.unit = int(unit);
        ctx = {};
        if (!world.try_move(move)) return false;
        act = { .kind = unit != PetUnit::none ? PetAct::Kind::chase : PetAct::Kind::moved, .move = move, .unit = unit };
        if (mode == 4) act.kind = PetAct::Kind::swing;
        return true;
    }
    bool wander(PetUnit centre, int reach) {                      // FUN_005df400 / de200 / df530
        const auto [centre_x, centre_y] = spot(centre);
        int off_x = 0, off_y = 0;
        if ((seed.next() & 1) == 0) { off_x = seed(reach); off_y = reach; }
        else { off_x = reach; off_y = seed(reach); }
        if (seed.next() & 1) off_x = -off_x;
        if (seed.next() & 1) off_y = -off_y;
        return set_out(centre_x + off_x, centre_y + off_y);
    }
    bool away(PetUnit from, int reach) {                          // FUN_005defe0: reach off on each axis, away
        const auto [pet_x, pet_y] = spot(PetUnit::pet);
        const auto [from_x, from_y] = spot(from);
        const int away_x = pet_x < from_x ? -1 : from_x < pet_x ? 1 : 0, away_y = pet_y < from_y ? -1 : from_y < pet_y ? 1 : 0;
        reach &= 0xff;
        if (reach > 5) pace(0, 0, reach);
        return set_out(pet_x + reach * away_x, pet_y + reach * away_y);
    }
    // FUN_005deb60 at a unit (a walk, 1 off). Flags: 1 marks it unreachable
    // when no path is found, 2 then wanders 4 70 in 100, else stands 10.
    bool walk_at(PetUnit unit, int flags) {
        if (set_out(0, 0, kMonsterWalk, unit)) return true;
        if (flags & 1) act.unreachable = true;
        if (flags & 2) {
            if (seed.next() % 100 < 0x46) { wander(PetUnit::pet, 4); act.unreachable = (flags & 1) != 0; return true; }
            stand(10); act.unreachable = (flags & 1) != 0;
        }
        return false;
    }
    bool run_at(PetUnit unit) {                                   // FUN_005ded20: a run, a walk when slowed
        if (scene.slowed) ctx = {};
        return set_out(0, 0, scene.slowed ? kMonsterWalk : kMonsterRun, unit);
    }
    // FUN_005de4e0: toward or away from the unit until `want` off it (by
    // their gap), at most `most` a move, shared over the axes as they lie.
    bool keep_off(PetUnit unit, int most, int want) {
        const int now = gap(PetUnit::pet, unit), sign = now < want ? -1 : 1, step = std::min(std::abs(now - want), most);
        const auto [pet_x, pet_y] = spot(PetUnit::pet);
        const auto [unit_x, unit_y] = spot(unit);
        const int across = std::abs(unit_x - pet_x), down = std::abs(unit_y - pet_y), total = std::max(across + down, step);
        int move_x = 0, move_y = 0;
        if (total > 0) {
            move_x = across * step / total; move_y = down * step / total;
            while (move_x + move_y < step) { ++move_x; ++move_y; }
        }
        const int side_x = unit_x < pet_x ? -1 : pet_x < unit_x ? 1 : 0, side_y = unit_y < pet_y ? -1 : pet_y < unit_y ? 1 : 0;
        return set_out(pet_x + side_x * move_x * sign, pet_y + side_y * move_y * sign);
    }
    bool circle(int type) {                                       // FUN_005ecbc0: path type 5 / 6 round the owner
        pace(type, 0, 4);
        return set_out(0, 0, kMonsterWalk, PetUnit::owner);
    }
    bool line_off(PetUnit unit, int reach) {                      // FUN_005de6f0: reach off the unit toward the pet
        const int apart = distance(PetUnit::pet, unit);
        if (apart == 0) return false;
        const auto [pet_x, pet_y] = spot(PetUnit::pet);
        const auto [unit_x, unit_y] = spot(unit);
        return set_out(unit_x + (pet_x - unit_x) * reach / apart, unit_y + (pet_y - unit_y) * reach / apart);
    }
    void stand(int frames) { act = { .kind = PetAct::Kind::stand, .frames = frames }; }
    void wait(int frames) { act.frames = frames; }
    void skill(int mode, int skill_id, PetUnit unit, int x = 0, int y = 0) {
        act = { .kind = PetAct::Kind::skill, .unit = unit, .skill = skill_id, .mode = mode & 0xff, .x = x, .y = y };
    }
    void sequence(int skill_id, PetUnit unit) { act = { .kind = PetAct::Kind::seq, .unit = unit, .skill = skill_id, .mode = 0xe }; }
    void die() { act = { .kind = PetAct::Kind::die }; }
    bool follow(int mode, bool run, int pct_now, int reach) {
        if (!world.follow(mode, run, pct_now, reach)) return false;
        act = world.followed();
        return true;
    }
    bool decide(PetUnit foe, bool melee, bool stay, int reach) {
        if (!world.decide(foe, melee, stay, reach)) return false;
        act = world.followed();
        return true;
    }
};

// MonAI 112 DruidBear (FUN_005ed730). Over 50 from its owner a teleport,
// over 28 after it at pace 100, over 18 after it walking (pace 100 when it
// runs). The foe: the search's within 28 with a clear line, else within 28
// by their gap. Out of melee a walk at it (aip2 in 100 at the pet's pace,
// 40 steps); in melee aip3 in 100 Skill1 as a sequence (Maul), else a swing
// and a wait of aip1. Else stand 15 within 16 of the owner, or follow.
template <class World>
void druid_bear_think(PetBrain<World>& brain) {
    const auto& scene = brain.scene;
    if (!scene.owner) { brain.stand(10); return; }
    brain.ctrl[2] = scene.owner_id;
    const int gap = brain.gap(PetUnit::pet, PetUnit::owner);
    if (gap > 0x32) { brain.follow(3, false, 0, 0); return; }
    const int pct = brain.pct();
    if (gap > 0x1c) { brain.follow(0, false, 100, 0); return; }
    if (gap > 0x12) {
        if ((scene.owner_mode == kPlayerWalk || scene.owner_mode == kPlayerTownWalk) && brain.follow(0, false, 0, 0)) return;
        if (scene.owner_mode == kPlayerRun && brain.follow(0, false, 100, 0)) return;
    }
    const bool melee = scene.foe && scene.foe_melee;
    bool foe = scene.foe && scene.foe_distance <= 0x1c && scene.clear[std::size_t(PetUnit::foe)];
    if (!foe && scene.foe && brain.gap(PetUnit::pet, PetUnit::foe) < 0x1c && scene.clear[std::size_t(PetUnit::foe)]) foe = true;
    if (!melee && foe) {
        if (brain.seed(100) < brain.aip(1)) brain.pace(0, pct, 0x28);
        brain.set_out(0, 0, kMonsterWalk, PetUnit::foe);
        return;
    }
    if (melee && foe) {
        if (brain.seed(100) < brain.aip(2)) { brain.sequence(scene.skill1, PetUnit::foe); return; }
        brain.set_out(0, 0, 4, PetUnit::foe);
        brain.wait(brain.aip(0));
        return;
    }
    if (gap < 0x11) brain.stand(15);
    else brain.follow(0, false, 0, 0);
}

// MonAI 108 DruidWolf for the Spirit Wolf (FUN_005ecee0, row 0x1a4). In
// town it only follows (else stands 33). The foe: the search's within aip4
// with a clear line; else, oddly, within aip4 by gap only when the line
// isn't clear. Over 50 from the owner Skill1 (Teleport) to the owner, a
// wait of 10; over aip5 it runs after it; over aip3 after it as it moves.
// Then FUN_005e45d0 (staying, reach 6); a foe in melee a swing and a wait
// of aip1; out of melee one the owner has within aip4 a run at it (the
// pet's pace), else over 10 from the owner it keeps 6 off it, else stand
// 15. No foe: aip2 in 100 wander 10, else stand 15.
template <class World>
void spirit_wolf_think(PetBrain<World>& brain) {
    const auto& scene = brain.scene;
    if (!scene.owner) { brain.stand(10); return; }
    brain.ctrl[2] = scene.owner_id;
    if (scene.town) {
        if (!brain.decide(PetUnit::none, false, false, 6)) brain.stand(0x21);
        return;
    }
    const int reach = brain.aip(3);
    const bool clear = scene.clear[std::size_t(PetUnit::foe)];
    bool foe = scene.foe && scene.foe_distance <= reach && clear;
    if (!foe) foe = scene.foe && brain.gap(PetUnit::pet, PetUnit::foe) < reach && !clear;
    const bool melee = scene.foe && scene.foe_melee;
    const int pct = brain.pct();
    const int apart_owner = brain.distance(PetUnit::owner, PetUnit::pet);
    if (scene.skill1 >= 0 && apart_owner > 0x32) {
        const auto [owner_x, owner_y] = brain.spot(PetUnit::owner);
        brain.skill(scene.mode1, scene.skill1, PetUnit::none, owner_x, owner_y);
        brain.wait(10);
        return;
    }
    if (apart_owner > brain.aip(4) && brain.follow(0, true, 100, 0)) return;
    if (apart_owner > brain.aip(2)) {
        if ((scene.owner_mode == kPlayerWalk || scene.owner_mode == kPlayerTownWalk) && brain.follow(0, false, 0, 0)) return;
        if (scene.owner_mode == kPlayerRun && brain.follow(0, true, 100, 0)) return;
    }
    if (brain.decide(foe ? PetUnit::foe : PetUnit::none, melee, true, 6)) return;
    if (foe) {
        if (melee) { brain.set_out(0, 0, 4, PetUnit::foe); brain.wait(brain.aip(0)); return; }
        if (brain.distance(PetUnit::owner, PetUnit::foe) < reach) { brain.pace(0, pct, 0); brain.run_at(PetUnit::foe); return; }
        if (apart_owner > 10) { brain.keep_off(PetUnit::owner, 8, 6); return; }
        brain.stand(15);
        return;
    }
    if (brain.seed(100) < brain.aip(1)) { brain.wander(PetUnit::pet, 10); return; }
    brain.stand(15);
}

// MonAI 108 DruidWolf for Fenris, the Dire Wolf (FUN_005ed2a0). The foe as
// the Spirit Wolf's but a gap fallback needs the clear line, and none past
// aip4 when both it and the pet are over aip4 from the owner. Over 50 from
// the owner Skill2 to it; over aip5 (or over aip4 with the owner running) a
// run after it, over aip4 with the owner walking a walk after it. Then
// FUN_005e45d0 (staying, reach 6). Its rage (Skill1, not while in state
// 0x8a; with a foe, while +0x14 is clear, only aip3 in 100): a unit near the
// owner (FUN_005d2f80, 10) under aip4 / 2 off in melee: Skill1 at it; out of
// melee a run at it (+0x14 1, +0x18 its id). Else out of melee a run at
// the foe (pet's pace), or aip2 in 100 wander 10, else stand 15; in melee
// a swing and a wait of aip1.
template <class World>
void fenris_think(PetBrain<World>& brain) {
    const auto& scene = brain.scene;
    if (!scene.owner) { brain.stand(10); return; }
    brain.ctrl[2] = scene.owner_id;
    if (scene.town) {
        if (!brain.decide(PetUnit::none, false, false, 6)) brain.stand(0x21);
        return;
    }
    const int reach = brain.aip(3);
    const bool clear = scene.clear[std::size_t(PetUnit::foe)];
    bool foe = scene.foe && scene.foe_distance <= reach && clear;
    if (!foe) foe = scene.foe && scene.foe_distance < reach && clear;
    const bool melee = scene.foe && scene.foe_melee;
    const int apart_owner = brain.distance(PetUnit::owner, PetUnit::pet);
    if (foe && reach < apart_owner && reach < brain.distance(PetUnit::owner, PetUnit::foe)) foe = false;
    const int pct = brain.pct();
    if (scene.skill2 >= 0 && apart_owner > 0x32) {
        const auto [owner_x, owner_y] = brain.spot(PetUnit::owner);
        brain.skill(scene.mode2, scene.skill2, PetUnit::none, owner_x, owner_y);
        brain.wait(10);
        return;
    }
    if (apart_owner > brain.aip(4) || (apart_owner > reach && scene.owner_mode == kPlayerRun)) { brain.follow(0, true, 100, 0); return; }
    if (apart_owner > reach && (scene.owner_mode == kPlayerWalk || scene.owner_mode == kPlayerTownWalk)) { brain.follow(0, false, 0, 0); return; }
    if (brain.decide(foe ? PetUnit::foe : PetUnit::none, melee, true, 6)) return;
    bool swing = false;
    if (scene.skill1 < 0 || scene.raging || (brain.seed(100) >= brain.aip(2) && foe && brain.ctrl[0] == 0)) {
        brain.ctrl[0] = 0;
    } else if (scene.nearby && brain.distance(PetUnit::nearby, PetUnit::pet) < reach / 2) {
        if (scene.melee_of[std::size_t(PetUnit::nearby)]) {
            brain.ctrl[0] = 0;
            brain.skill(scene.mode1, scene.skill1, PetUnit::nearby);
            return;
        }
        if (!melee) {
            brain.run_at(PetUnit::nearby);
            brain.ctrl[0] = 1;
            brain.ctrl[1] = int(PetUnit::nearby);
            return;
        }
        swing = true;
    }
    if (!swing && !melee) {
        if (foe) { brain.pace(0, pct, 0); brain.run_at(PetUnit::foe); return; }
        if (brain.seed(100) >= brain.aip(1)) brain.stand(15);
        else brain.wander(PetUnit::pet, 10);
        return;
    }
    brain.set_out(0, 0, 4, foe ? PetUnit::foe : PetUnit::none);
    brain.wait(brain.aip(0));
}

// MonAI 107 Raven (FUN_005ecc10). +0x14: hits left (Skill1's calc at the
// first think, 3 without a Skill1); none left, it dies. Over 50 from the
// owner a teleport, over 28 after it. The driver's target, past the +0x18
// frame: aip4 in 100 and under aip5 off, in melee a swing (a hit spent, the
// next not for aip3 x 10 frames), else a walk at it. Between aip2 and aip1
// off the owner it circles it (path types 5 / 6, swapping +0x1c when one
// fails); else to (aip1 + aip2) / 2 off the owner toward itself, else after
// the owner's footsteps.
template <class World>
void raven_think(PetBrain<World>& brain) {
    const auto& scene = brain.scene;
    if (!scene.owner) { brain.stand(10); return; }
    if (brain.ctrl[0] == -1) brain.ctrl[0] = scene.skill1 >= 0 ? scene.skill_calc : 3;
    else if (brain.ctrl[0] == 0) { brain.die(); return; }
    const int gap = brain.gap(PetUnit::pet, PetUnit::owner);
    if (gap > 0x32) { brain.follow(3, false, 0, 0); return; }
    if (gap > 0x1c) { brain.follow(0, false, brain.pct(), 0); return; }
    const int reach = (brain.aip(1) + brain.aip(0)) / 2;
    if (scene.driver && brain.ctrl[1] < scene.frame && brain.seed(100) < brain.aip(3) && scene.driver_distance < brain.aip(4)) {
        if (scene.driver_melee) {
            brain.set_out(0, 0, 4, PetUnit::foe2);
            --brain.ctrl[0];
            brain.ctrl[1] = scene.frame + brain.aip(2) * 10;
            return;
        }
        brain.walk_at(PetUnit::foe2, 0);
        return;
    }
    if (gap <= brain.aip(0) && gap >= brain.aip(1)) {
        if (brain.circle(brain.ctrl[2] ? 5 : 6)) return;
        brain.ctrl[2] = brain.ctrl[2] == 0 ? 1 : 0;
        if (brain.circle(brain.ctrl[2] ? 5 : 6)) return;
    }
    if (!brain.line_off(PetUnit::owner, reach)) brain.follow(1, false, 0, 0);
}

// MonAI 86 Hydra (FUN_005e9e60): past its last frame (+0x14) it dies; the
// driver's target under 25 off, 60 in 100 Skill1 at it; else stand 10.
template <class World>
void hydra_think(PetBrain<World>& brain) {
    const auto& scene = brain.scene;
    if (brain.ctrl[0] < scene.frame) { brain.die(); return; }
    if (scene.driver && scene.driver_distance < 0x19 && brain.seed.next() % 100 < 0x3c) { brain.skill(scene.mode1, scene.skill1, PetUnit::foe2); return; }
    brain.stand(10);
}

// MonAI 109 Totem (FUN_005ed9e0: Oak Sage, Heart of Wolverine, Spirit of
// Barbs). A foe within 24 in melee: aip1 in 100 it steps 6 away from it.
// aip2 in 100 it forgets the foe. Over aip3 from the owner it teleports to
// it (then stands 25); over aip4 it walks after it as it moves (pace 60
// when it runs). Then FUN_005e45d0 (reach 6), else stand 25.
template <class World>
void totem_think(PetBrain<World>& brain) {
    const auto& scene = brain.scene;
    if (!scene.owner) { brain.stand(10); return; }
    bool foe = scene.foe && scene.foe_distance <= 0x18;
    bool melee = scene.foe && scene.foe_melee;
    if (melee && foe && int(brain.seed.next() % 100) < brain.aip(0) && brain.away(PetUnit::foe, 6)) return;
    if (int(brain.seed.next() % 100) < brain.aip(1)) { foe = false; melee = false; }
    const int apart_owner = brain.distance(PetUnit::pet, PetUnit::owner);
    if (apart_owner > brain.aip(2) && brain.world.teleport()) { brain.stand(0x19); return; }
    if (apart_owner > brain.aip(3)) {
        if ((scene.owner_mode == kPlayerWalk || scene.owner_mode == kPlayerTownWalk) && brain.follow(0, false, 0, 0)) return;
        if (scene.owner_mode == kPlayerRun && brain.follow(0, false, 0x3c, 0)) return;
    }
    if (brain.decide(foe ? PetUnit::foe : PetUnit::none, melee, false, 6)) return;
    brain.stand(0x19);
}

// MonAI 110 Vines (FUN_005ec6c0: Poison Creeper). aip5 or over from the
// owner, a teleport. In town it follows, else stands aip3. The merc-style
// search's foe under aip2 off: FUN_005e45d0 first; a poisoned or
// poison-immune foe it leaves aip4 away; out of melee a walk at it; in
// melee Skill1 at it every aip1 frames (+0x18 the last). Else stand aip3.
template <class World>
void poison_creeper_think(PetBrain<World>& brain) {
    const auto& scene = brain.scene;
    if (!scene.owner) { brain.stand(0x19); return; }
    if (brain.distance(PetUnit::pet, PetUnit::owner) >= brain.aip(4) && brain.follow(3, false, 0, 6)) return;
    if (scene.town) {
        if (!brain.decide(PetUnit::none, false, false, 6)) brain.stand(brain.aip(2));
        return;
    }
    const bool foe = scene.foe2 && scene.foe2_distance < brain.aip(1);
    const bool melee = scene.foe2 && scene.foe2_melee;
    if (brain.decide(foe ? PetUnit::foe2 : PetUnit::none, melee, false, 6)) return;
    if (foe) {
        if (scene.poisoned || scene.poison_resist == 100) { brain.away(PetUnit::foe2, brain.aip(3)); return; }
        if (!melee) { brain.walk_at(PetUnit::foe2, 7); return; }
        if (scene.skill1 >= 0 && brain.aip(0) + brain.ctrl[1] < scene.frame) {
            brain.skill(scene.mode1, scene.skill1, PetUnit::foe2);
            brain.ctrl[1] = scene.frame;
            return;
        }
    }
    brain.stand(brain.aip(2));
}

// MonAI 111 CycleOfLife (FUN_005ec8c0: Carrion Vine 0x1aa, Solar Creeper
// 0x1ab). aip5 or over from the owner, a teleport. The driver's target
// unless dying. A corpse near the owner (FUN_005d2f80 within Skill1's calc,
// 5..50) under aip2 off: FUN_005e45d0 first; in melee of it, while the
// owner's life (Carrion Vine) or mana (Solar Creeper) is short of its max,
// Skill1 in mode 8 every aip1 frames. Else hit by the target, 25 in 100
// aip4 away from it; no corpse, stand aip3; else a walk at it.
template <class World>
void cycle_vine_think(PetBrain<World>& brain) {
    const auto& scene = brain.scene;
    if (!scene.owner) return;
    bool target = scene.driver, hit = scene.driver_melee;
    if (brain.distance(PetUnit::pet, PetUnit::owner) >= brain.aip(4) && brain.follow(3, false, 0, 6)) return;
    if (target && scene.dying[std::size_t(PetUnit::foe2)]) { target = false; hit = false; }
    bool corpse = false, melee = false;
    int apart = 0;
    if (scene.skill1 > 0 && scene.skill1 < kSkillRows && scene.nearby) {
        corpse = true;
        apart = brain.distance(PetUnit::pet, PetUnit::nearby);
    }
    if (apart < brain.aip(1)) melee = corpse && scene.melee_of[std::size_t(PetUnit::nearby)];
    else corpse = false;
    if (brain.decide(corpse ? PetUnit::nearby : PetUnit::none, melee, false, 6)) return;
    bool feed = true;
    if (scene.cls == 0x1aa) feed = scene.life < scene.max_life;
    else if (scene.cls == 0x1ab) feed = scene.mana < scene.max_mana;
    if (corpse && melee && feed && brain.aip(0) + brain.ctrl[1] < scene.frame) {
        brain.skill(8, scene.skill1, PetUnit::nearby);
        brain.ctrl[1] = scene.frame;
        return;
    }
    if (hit && target && brain.seed(100) < 0x19) { brain.away(PetUnit::foe2, brain.aip(3)); return; }
    if (!corpse) { brain.stand(brain.aip(2)); return; }
    brain.walk_at(PetUnit::nearby, 7);
}

// FUN_005ea2b0: a trap's shots (+0x18, Skill1's calc when below 0). No
// owner, the owner in town, or none left: it dies (true). `spend` takes one.
template <class World>
bool trap_spent(PetBrain<World>& brain, bool spend) {
    const auto& scene = brain.scene;
    if (!scene.owner || scene.town) { brain.die(); return true; }
    if (brain.ctrl[1] < 0) {
        if (scene.skill1 < 0 || scene.skill1 >= kSkillRows) { brain.die(); return true; }
        brain.ctrl[1] = scene.skill_calc;
    }
    if (brain.ctrl[1] > 0) {
        if (spend) --brain.ctrl[1];
        return false;
    }
    brain.die();
    return true;
}

// MonAI 101 AssassinSentry (FUN_005ea3d0: Charged Bolt, Lightning, Wake of
// Fire and Inferno Sentries). The merc-style search's foe under aip4 off:
// aip1 in 100 a shot (Skill1 in the skill's own mode), else stand aip2;
// no foe, stand aip3.
template <class World>
void sentry_think(PetBrain<World>& brain) {
    const auto& scene = brain.scene;
    if (trap_spent(brain, false)) return;
    if (!scene.foe2 || scene.foe2_distance >= brain.aip(3)) { brain.stand(brain.aip(2)); return; }
    if (brain.seed(100) >= brain.aip(0)) { brain.stand(brain.aip(1)); return; }
    if (!trap_spent(brain, true)) brain.skill(scene.skill_mode, scene.skill1, PetUnit::foe2);
}

// MonAI 104 DeathSentry (FUN_005ea980). No skill level, nothing. No foe,
// stand aip2. A corpse (not the last one, +0x14) within half the blast
// radius of the foe: Skill1 at it in mode 9 (the explosion). The foe under
// aip4 off, aip3 in 100 Skill2 at it in mode 0xe (its lightning). Else stand
// aip2.
template <class World>
void death_sentry_think(PetBrain<World>& brain) {
    const auto& scene = brain.scene;
    if (trap_spent(brain, false) || scene.skill_level <= 0) return;
    if (!scene.foe2) { brain.stand(brain.aip(1)); return; }
    if (scene.nearby && scene.corpse_id != brain.ctrl[0] && brain.distance(PetUnit::foe2, PetUnit::nearby) < scene.radius / 2) {
        if (trap_spent(brain, true)) return;
        brain.ctrl[0] = scene.corpse_id;
        brain.skill(9, scene.skill1, PetUnit::nearby);
        return;
    }
    if (scene.foe2_distance < brain.aip(3) && brain.seed(100) < brain.aip(2)) {
        if (trap_spent(brain, true)) return;
        brain.skill(0xe, scene.skill2, PetUnit::foe2);
        return;
    }
    brain.stand(brain.aip(1));
}

// MonAI 102 BladeCreeper (FUN_005ea540: Blade Sentinel). +0x14 its last
// frame (Skill1's calc on), then it dies. Its missile is made once (+0x1c).
// It walks to one end of its line, then the other (+0x18 which; path type
// 0xf, 20 steps), else 5 about the owner (2 about itself with none), else
// stands 5; no line, stand 3.
template <class World>
void blade_sentinel_think(PetBrain<World>& brain) {
    const auto& scene = brain.scene;
    if (scene.skill1 < 0 || scene.skill1 >= kSkillRows) { brain.die(); return; }
    if (brain.ctrl[0] < 0) brain.ctrl[0] = scene.skill_calc + scene.frame;
    if (scene.frame > brain.ctrl[0]) { brain.die(); return; }
    if (brain.ctrl[2] == 0) brain.ctrl[2] = 1;
    if (!scene.ends) { brain.stand(3); return; }
    const auto first = scene.end[brain.ctrl[1] == 0 ? 0 : 1], second = scene.end[brain.ctrl[1] == 0 ? 1 : 0];
    brain.pace(0xf, 0, 0x14);
    if (brain.set_out(first.first, first.second)) return;
    brain.ctrl[1] = brain.ctrl[1] == 0 ? 1 : 0;
    if (brain.set_out(second.first, second.second)) return;
    if (!(scene.owner ? brain.wander(PetUnit::owner, 5) : brain.wander(PetUnit::pet, 2))) brain.stand(5);   // no owner: FUN_005de200(2)
}

// FUN_005ead50: may the Shadow use skill `id`? Its class the owner's, its
// AI type allows it (FUN_005eabf0), then Attack always; else a mana roll
// (mana x 160 / 100 in 100 fails), +0x14 its next frame, and a roll under
// the +0x18 load (kept between aip8 (NM) and aip8 (H) x 32), which then
// grows by the skill's mana.
template <class World>
bool shadow_may_use(PetBrain<World>& brain, int id) {
    const auto& scene = brain.scene;
    const auto& skill = scene.skill_of(id);
    if (skill.cls != scene.owner_class || !skill.ai_ok) return false;
    if (id == 0) return true;
    if (brain.seed(100) > 100 - skill.mana * 0xa0 / 100) return false;
    if (scene.frame < brain.ctrl[0]) return false;
    const int least = scene.aip8_nightmare < 2 ? 1 : std::min(scene.aip8_nightmare, 0x80);
    const int ceiling = std::clamp(scene.aip8_hell, 1, 0x100);
    if (brain.ctrl[1] < least || ceiling * 0x20 < brain.ctrl[1]) brain.ctrl[1] = least;
    if (const int load = brain.seed(brain.ctrl[1]); brain.seed(100) < load) return false;
    brain.ctrl[1] += (0x140 - brain.ctrl[2]) * skill.mana / (brain.ctrl[2] + 100);
    return true;
}

// MonAI 105 ShadowWarrior (FUN_005eafa0). +0x18 drops by aip4 + 1 a think
// (back to 0 under 0 or over aip8 (H) x 64). The AI driver's target, if
// within aip1 and the owner within aip2, else none, goes to decide (reach
// 6). Then, the owner with both a left and a right skill: one at random,
// Attack (aip3 - 2 x +0x1c, 5..100, in 100) in melee; not usable, the
// other, then Attack. A missile kind (1) out of melee runs at the foe, else
// the skill in its mode and +0x14 the frame its calc / 3 + 18 on. Else
// stand 25; no owner, stand 100.
template <class World>
void shadow_warrior_think(PetBrain<World>& brain) {
    const auto& scene = brain.scene;
    if (!scene.owner) { brain.stand(100); return; }
    const int most = scene.aip8_hell < 2 ? 1 : std::min(scene.aip8_hell, 0x100);
    brain.ctrl[1] += -1 - brain.aip(3);
    if (brain.ctrl[1] < 0 || most * 0x40 < brain.ctrl[1]) brain.ctrl[1] = 0;
    const bool melee = scene.driver_melee;
    PetUnit foe = scene.driver ? PetUnit::foe2 : PetUnit::none;
    if (brain.aip(0) < scene.driver_distance || brain.aip(1) < brain.distance(PetUnit::pet, PetUnit::owner)) foe = PetUnit::none;
    if (brain.decide(foe, melee, false, 6)) return;
    if (foe == PetUnit::none || scene.left < 0 || scene.right < 0) { brain.stand(0x19); return; }
    brain.world.give_hands();                                     // FUN_005eaf00 / FUN_00647280: the owner's hands, each think
    const bool has_attack = scene.pet_attack || scene.left == 0 || scene.right == 0;
    int id = brain.seed(2) ? scene.left : scene.right;
    const int chance = std::clamp(brain.aip(2) - 2 * std::max(brain.ctrl[2], 1), 5, 100);
    if (melee && brain.seed(100) < chance) id = 0;
    if (!((id != 0 || has_attack) && shadow_may_use(brain, id))) {
        id = id == scene.left ? scene.right : scene.left;
        if (!shadow_may_use(brain, id)) id = 0;                   // FUN_00647280 gives it Attack if need be
    }
    const auto& skill = scene.skill_of(id);
    if (skill.kind == 1 && !melee) { brain.run_at(foe); return; }
    brain.skill(skill.mode, id, foe);
    brain.ctrl[0] = skill.delay / 3 + 0x12 + scene.frame;
}

// FUN_005dc640: a clear line from the pet to its foe. Lines (sight_blocked
// from the pet's size to a size-2 end, `blocked` the collision under mask
// 0x805, FUN_006229f0) to the foe's spot, then to the spots beside it
// across the line, 2..4 off by their gap (merc_gap: 2 under 3, 3 under 11,
// 4 under 25, else 3); any one clear will do. tools/emu/pet_line.py checks
// it against game.exe.
template <class Blocked>
bool pet_line_clear(int pet_x, int pet_y, int size, int foe_x, int foe_y, Blocked&& blocked) {
    const int gap = merc_gap(pet_x, pet_y, size, foe_x, foe_y);
    const int step = gap < 3 ? 2 : gap < 11 ? 3 : gap < 25 ? 4 : 3;
    const int step_x = pet_x < foe_x ? -step : foe_x < pet_x ? step : 0, step_y = pet_y < foe_y ? -step : foe_y < pet_y ? step : 0;
    const std::array<std::pair<int, int>, 3> ends{ { { foe_x, foe_y }, { foe_x - step_y, foe_y + step_x }, { foe_x + step_y, foe_y - step_x } } };
    return std::ranges::any_of(ends, [&](const std::pair<int, int>& end) { return !sight_blocked(pet_x, pet_y, size, end.first, end.second, 2, blocked); });
}

}  // namespace d2d::rules
