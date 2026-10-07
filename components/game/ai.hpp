// SPDX-License-Identifier: GPL-3.0-or-later
// NPC behaviour: town NPCs walking their DS1 paths (rules::npc_think).
#pragma once

#include "game.hpp"
#include "gamedata.hpp"

#include <combat.hpp>
#include <missiles.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <town_npcs.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

// Where each world NPC is right now (index-aligned with Level::npcs):
// patrolling NPCs walk their DS1 path, pausing at each point.
struct UnitState {
    float x = 0, y = 0;
    UnitShape shape;                  // its collision (gamedata.hpp); a player's by default
    int dir = 0;
    bool walking = false;
    std::size_t next = 0;             // path point being walked to
    std::uint32_t wait_until = 0;     // ms; idle until then
    std::uint32_t mode_ms = 0;        // when the current mode (walk/idle) started
    std::uint32_t stuck_since = 0;    // ms the merc last got blocked, 0 = moving
    float goal_x = 0, goal_y = 0;     // where the route was planned to
    int budget = 0;                   // a monster walk's re-path budget, points (path +0x94)
    int path_points = 0;              // points the route had when made
    bool to_unit = false;             // walking up to a unit (near 1, the wall pather's 0x28 steps)
    bool hidden = false;              // a quest-gated NPC who isn't here (yet)
    bool alert = false;               // has something new to say on a quest: the balloon over its head
    std::string_view mode;            // an object's mode now, "" = its start mode (Npc::mode)
    std::uint16_t says = 0;           // string id over its head (a shrine's message), 0 none
    std::vector<std::pair<float, float>> path;   // the route being followed, cells
    d2d::rules::NpcBrain brain;       // a town NPC's AI commands (town_npcs.hpp)
};

std::vector<UnitState> npc_start(const Level& level);

// The units (the player, the merc, NPCs, monsters near) as game.exe's
// collision sees them: each stamps its footprint bit (UnitShape: 0x1000,
// or 0x2000 for the merc and the cow) on its subtile, or the plus for
// pattern 2 / 4; a mover is blocked where its own test shape (the plus, or
// its SizeX box) meets a footprint its mask holds. game.exe lifts the
// mover's own stamp first (FUN_00649970). So a player (0x1c09) walks
// through its merc; monsters (0x3c01) don't. Own bits (0x80 player, 0x100
// monster) are in no walker's mask, so aren't kept.
struct Crowd {
    std::vector<const UnitState*> units;
    [[nodiscard]] bool at(float x, float y, const UnitState* self) const {
        static const UnitShape player;
        const UnitShape& mover = self ? self->shape : player;
        const int sub_x = int(std::floor(x * 5)), sub_y = int(std::floor(y * 5));
        const int half = mover.size / 2;
        auto covers = [&](int cell_x, int cell_y) {
            if (mover.pattern == 0) return cell_x == sub_x && cell_y == sub_y;
            if (mover.box()) return cell_x >= sub_x - half && cell_x < sub_x - half + mover.size && cell_y >= sub_y - half && cell_y < sub_y - half + mover.size;
            return std::abs(cell_x - sub_x) + std::abs(cell_y - sub_y) <= 1;
        };
        for (const auto* other : units) {
            if (other == self || !other->shape.stamps() || !(other->shape.bit() & mover.mask)) continue;
            const int other_x = int(std::floor(other->x * 5)), other_y = int(std::floor(other->y * 5));
            if (covers(other_x, other_y)) return true;
            if (other->shape.box())
                for (const auto& [step_x, step_y] : { std::pair{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } })
                    if (covers(other_x + step_x, other_y + step_y)) return true;
        }
        return false;
    }
};

// Town NPCs with the Npc AI think (rules::npc_think on their unit seed)
// when due: a stand thinks again n frames on, a walk at its end, a special
// mode (S1, S2) aidel (15) frames after it ends. An NPC with a "!" walks
// up to `player` within 16 and greets it; one in `busy` (menu, speech or
// store open on it) stops and stands (rules::npc_think's visitor).
// The walk is a monster's (set_off / walk_on: toward 0xd, wall 0xf) at
// Velocity.
// ponytail: an NPC's first think comes at once.
void npc_patrol(const GameData& game_data, const Level& level, std::vector<UnitState>& npcs, std::array<int, 3> busy,
                std::uint32_t now_ms, float elapsed, const Crowd& crowd = {}, const UnitState* player = nullptr);

// A player's walk from (x, y) to (gx, gy), in cells: game.exe's path type 7
// (rules::player_path: the toward pather, the search when close) over
// subtiles with the unit collision test, `to_unit` when walking up to a
// monster or NPC (near 1, FUN_006498a0). Points are subtile centres; the
// leading ones it stands on are skipped (FUN_0064fe40), and a repeat.
std::vector<std::pair<float, float>> player_walk(const Level& level, float x, float y, float goal_x, float goal_y, bool to_unit,
                                                 const Crowd& crowd = {}, const UnitState* self = nullptr);

// A walk as a monster's (town NPCs, Cain, the merc, pets) to (gx, gy) in
// cells: rules::monster_path's points as subtile centres, a budget of 0x14
// points (FUN_005a7c20 -> FUN_006490e0). False with no step to take.
bool set_off(const Level& level, UnitState& unit, float goal_x, float goal_y, bool to_unit, const Crowd& crowd = {});

// A frame of that walk (FUN_00650840): along its path; run out short of its
// end, it paths again while the budget lasts, the points walked coming off
// it (FUN_006503f0 -> FUN_00650350). Blocked, it ends (FUN_00650150).
// False once it's over.
bool walk_on(const Level& level, UnitState& unit, float step, const Crowd& crowd = {});

// Moves u along its path by `step` cells, dropping reached points and
// turning it to face the way; blocked by a wall or another unit, it
// drops the route. False once there (or stuck).
bool follow_path(const Level& level, UnitState& unit, float step, const Crowd& crowd = {});

// A pet follows the player about camp: it sets off when more than 3 cells
// behind and stops within 1.5, at `speed` cells/s on a monster's walk;
// more than 12 behind (a warp), or stuck for 1.5 s (no route), it's put
// next to the player.
// ponytail: the pets' AIs aren't traced (the merc's is: rules::hireable_think).
void pet_follow(const Level& level, UnitState& unit, float player_x, float player_y, float speed, std::uint32_t now_ms, float elapsed,
                 const Crowd& crowd = {});

// A monster in the level: its type (MonStats row), composite recipe with
// the components it rolled, where it is, its stats and what it's doing.
// Spawn areas' shared "seen" flags by ai.cpp's area key (search_target).
using AreaSeen = std::unordered_map<int, bool>;
// A monster that opens doors at a think (FUN_005b0f50): the world finds,
// and operates in reach, its door; true when it found one.
struct Monster;
using OpenDoor = std::function<bool(const Monster&, std::uint32_t now_ms)>;

struct Monster {
    int id = -1;                              // its unit id (D2's GUID: the server's, stable while the game runs)
    int type = -1;
    // Its mods in the fight (uniques.hpp): life as of the last tick (a drop
    // sets off Lightning Enchanted's bolts, at most every 10 frames), when
    // its death effect is due, its mana burn per hit.
    int last_hp = 0;
    std::uint32_t bolts_at = 0, fx_at = 0;
    bool fx_done = false;
    int mana_lo = 0, mana_hi = 0;
    d2d::rules::Boss boss = d2d::rules::Boss::none;   // champion, unique, superunique, minion
    std::vector<int> mods;                    // MonUMod ids
    int super = -1;                           // SuperUniques row
    std::array<int, 6> boss_res{};            // what its mods add to its resistances (ResDm..ResPo)
    int boss_speed = 0;                       // + speed % (fast, champion)
    Npc npc;
    UnitState unit;
    d2d::rules::MonStats stats;
    int hit_points = 1;
    int leader = -1;                          // index of its group's leader
    int difficulty = 0;
    float home_x = 0, home_y = 0;             // where it spawned: wandering stays near
    std::string_view mode = "NU";             // animation mode token
    std::string_view last_mode = "NU";        // as of the last sound check
    std::uint32_t mode_until = 0;             // ms: an attack / get-hit / death ends
    std::uint32_t next_act = 0;               // ms: may attack again (aidel)
    std::uint32_t flee_until = 0;             // ms: running from the player
    bool struck = false;                      // this attack's hit is resolved
    bool aware = false;                       // has noticed the player
    // AI control flag 8: it has found a target once, and needs no line of
    // sight from then on; its spawn area's key in AreaSeen (-2: not yet
    // looked up, -1: none). See ai.cpp search_target.
    // AI control flag 0x40: a walk with flag 1 couldn't set off
    // (FUN_005deb60 -> FUN_005dd230), so its next search tests sight
    // whatever the room. Then a move's path (D2DynamicPath, unit +0x2c): its
    // points (subtiles, +0x9c), the one it's walking to (+0x24), its re-path
    // budget (+0x94) and its end when pathed (SP3 +0x18; at a foe, the foe's
    // spot, SP2 +0x14 too); the foe a chase follows (an index into the foes).
    bool sighted = false, force_sight = false;
    int area = -2;
    std::vector<std::pair<int, int>> steps;
    int step = 0, budget = 0, end_x = 0, end_y = 0;
    int chase = -1;
    bool corpse_used = false;                 // raised (Raise Skeleton): its corpse is gone
    // Damage over time (life a millisecond, until when) and a chill.
    double poison_rate = 0, bleed_rate = 0, dot_acc = 0;
    std::uint32_t poison_until = 0, bleed_until = 0, chill_until = 0;
    std::uint32_t stun_until = 0;             // ms: stunned, it stands
    // Aura Enchanted (uniques.hpp boss_aura): its aura, level, next pulse;
    // and the damage / to-hit % it gets from bosses' auras this frame.
    int aura = 0, aura_lvl = 0;
    std::uint32_t aura_next = 0;
    int aura_dmg = 0, aura_th = 0;
    // Skills' states on it (their auratargetstate): one curse at a time
    // (Amplify Damage .. Lower Resist, Confuse, Attract), one other
    // (Battle Cry, Taunt, Inner Sight, Cloak of Shadows' blindness).
    struct SkillState { int skill = -1, level = 0; std::uint32_t until = 0; };
    SkillState curse, cry;
    // What they do to it, as of this frame (Fight::monster_states): its
    // damage % and speed %, the share of its melee damage it takes back
    // (Iron Maiden), and until when it can't see (Dim Vision, Cloak).
    int dmg_pct = 0, speed_pct = 0, reflect_pct = 0;
    std::uint32_t blind_until = 0;
    // Its alignment (stat 0xac: 0 evil, 1 neutral while Confuse or Attract
    // is on it) and its skill-set target (monster data +0x38 kind, +0x34
    // value, FUN_00573090: 2 a monster by id, Attract's; 3 Confuse's
    // search), cleared when due (event 10 -> FUN_00573120).
    int align = 0, set_kind = 0, set_id = 0;
    std::uint32_t set_until = 0;
    bool in_aura = false;                     // within the player's aura (its auratargetstate: Conviction's convicted ...)
    // Andariel's think (MonAI 34): the skill its mode is using (-1 a plain
    // swing), the last SC frame the spray fired, where its target stood
    // when the spray began (subtiles), a random walk under way (to
    // unit.goal) rather than a chase.
    int skill = -1, skill_frame = 0, skill_x = 0, skill_y = 0;
    bool wandering = false;
    // The MonAI thinks: the last mode it left other than NU (monster data
    // +0x54, FUN_005a68e0; GH: it got hit), the AI's scratch words (+0x14,
    // +0x18, +0x1c), its command (the Fallen's 1: charge), the corpse a
    // skill raises; the velocity % of the move its last think started
    // (rules::ThinkIn::pace), until when it's in Spider Lay's state (0x16).
    std::string_view left_mode = "NU";
    int ai_state = 0, ai_state2 = 0, ai_state3 = 0, ai_command = 0, skill_unit = -1;
    int move_pct = 0;
    std::uint32_t laid_until = 0;
    // Its unit seed (+0x20) as its look left it: what its thinks draw. A
    // special AI (AI control [0], FUN_005b0e00: the Countess's 0xd), its
    // map AI's points (subtiles), half freeze durations (stat 0x76).
    d2d::rules::Rng seed;
    int special = 0;
    std::vector<std::pair<int, int>> path;
    bool half_freeze = false;
    [[nodiscard]] bool alive() const { return hit_points > 0; }
    // As a target for the player's (or the merc's) hits.
    [[nodiscard]] d2d::rules::Target target(const GameData& game_data) const {
        const auto& type_info = game_data.monsters.types[std::size_t(type)];
        const auto& per_difficulty = type_info.diff[std::size_t(difficulty)];
        auto res = per_difficulty.res;
        for (std::size_t i = 0; i < 6; ++i) res[i] += boss_res[i];
        return { hit_points, stats.hit_points, stats.armor_class, stats.level, type_info.can_block ? per_difficulty.to_block : 0, res, per_difficulty.drain };
    }
};

// Whoever the monsters are after (the player, the merc), and what they
// did to it this frame. The player comes first and its pets after it, as in
// game.exe's player list (FUN_005b1900).
struct Foe {
    float x = 0, y = 0;
    int level = 1;
    bool alive = true, moving = false;        // moving: block falls to a third
    d2d::rules::Fighter fighter;                    // defense, block, reductions, resistances, thorns
    int damage = 0;                           // life lost this frame, whole points
    int poison = 0, poison_ticks = 0;         // poison taken this frame: life per tick (256ths), for ticks
    int chill_ticks = 0;                      // chilled this frame, ticks
    bool blocked = false;                     // blocked a hit this frame
    std::vector<const Monster*> melee_by;     // who struck at it in melee this frame (Frozen / Shiver Armor)
    int missile_hits = 0;                     // missiles that reached it this frame (Chilling Armor)
    int mana_burn = 0;                        // mana it lost to Mana Burn this frame
    int amplify = 0;                          // Amplify Damage cast on it this frame (a Cursed boss): its level
    bool pet = false;                         // the merc, a summon: in the player's list
    int size = 2;                             // FUN_00620510: a player's 2, a monster's MonStats2 SizeX
    int threat = 14;                          // FUN_005dc920: a player's 14, a monster's MonStats threat
    int life_pct = 100;                       // FUN_00621f20 (a Fetish's think): life x 100 / max life
    const Monster* of = nullptr;              // a monster in the fight (Confuse, Attract: monsters fight monsters)
    void take(const d2d::rules::Taken& taken) {
        blocked = blocked || taken.blocked;
        damage += taken.damage;
        if (taken.poison > 0 && taken.poison >= poison) { poison = taken.poison; poison_ticks = taken.poison_ticks; }   // the stronger (FUN_0057ac50)
        chill_ticks = std::max(chill_ticks, taken.chill_ticks);
    }
};

constexpr float kMeleeReach = 1.1f;           // cells between centres

// A missile in flight (a quill rat's spike): straight on at its Missiles.txt
// velocity until it hits the foe, a wall, or runs out of range.
struct Missile {
    const GameData::MissileInfo* info = nullptr;
    float x = 0, y = 0, velocity_x = 0, velocity_y = 0;       // cells, cells/s
    int dir = 0;                              // 0..31, DCC order
    std::uint32_t born = 0, dies = 0;
    d2d::rules::MonStats src;                 // a monster's: its stats, A2 damage = the missile's
    int min = 0, max = 0, attack_rating = 0, level = 1;  // the merc's: damage, attack rating, level; a skill's level
    bool friendly = false;                    // the merc's, the player's: hits monsters, not the player
    bool by_merc = false;                     // a merc's skill: its weapon damage, attack rating and level, no synergies
    bool visual_only = false;                          // only a sight (a death blast's guts): hits nothing
    int skill = -1;                           // the player's: the skill whose damage it carries
    // A shrine's thrown potion: its Missiles.txt row damage, bursting over
    // `burst` subtiles where it lands (sHitPar1).
    std::optional<d2d::rules::MissileDamage> row;
    int burst = 0;
    // Monsters a flying-on missile already hit; a nova's missiles share
    // theirs (one hit a monster). -1: an explosion is under way.
    std::shared_ptr<std::vector<int>> struck = std::make_shared<std::vector<int>>();
    // The player's skill missiles' extras: the monster a guided one seeks
    // (-1 none), a chain's hops left, the damage % its skill adds (stat 25,
    // the do's callback 0x5db6a0), its element per frame in 256ths when the
    // row's own is replaced (Meteor's fire: FUN_005aaa90's 0x8001), the
    // last frame a spawner or burner ran.
    int target = -1, hops = 0, ed_pct = 0, fixed = -1, frame = -1;
    float target_x = 0, target_y = 0;                     // where it was sent (Molten Boulder), a spiral's centre
    float origin_x = 0, origin_y = 0;                     // where it came from (Blade Sentinel goes back and forth)
    int turn = 0;                             // Frozen Orb's direction index (do 15), a spiral's angle
    std::vector<std::pair<int, std::uint32_t>> hit_at;   // NextHit: when it last struck each monster
    const GameData::MissileInfo* sub = nullptr;          // a monster's spawner's SubMissile1 (the Countess's firewall maker)
    // Its flight in act subtiles, 16.16 (rules::MissileFlight), from its
    // first step: toward `to` (cells, FUN_0059fa30's target point) when
    // set, else along its velocity, at the velocity's path speed
    // (missile_speed). Charged Bolt's: `bolt` its index (FUN_005c9290).
    // `rooms`: its room, then the near list's (missile_rooms); `follow`:
    // sent outside that room, so the room moves with it.
    std::optional<std::pair<float, float>> to;
    int bolt = -1;
    d2d::rules::MissileFlight flight;
    bool launched = false, follow = true;
    float flown_x = 0, flown_y = 0, aimed_x = 0, aimed_y = 0;
    std::vector<std::array<int, 4>> rooms;
};

// A missile's speed, cells/s, for its velocity: FUN_0059fa30's path
// velocity ((Vel + VelLev x lvl / 8) << 8) x 75 / 100 moves it velocity /
// 4096 subtiles a frame. The flight takes the path velocity back from it.
inline float missile_speed(int path_velocity) { return float(path_velocity) * 5 / 4096; }
inline float missile_speed(const GameData::MissileInfo& info, int level) {
    return missile_speed(d2d::rules::missile_velocity(info.vel, info.vel_lev, level));
}

// One frame of a moving missile's flight (FUN_005ae1f0's move): launched
// on its first, turned when its velocity's way changed, started over from
// where it was put when moved by hand. Its subtiles are the act's;
// `blocked` tests the 0x04 bit, and a subtile off its rooms reads 0x27
// (blocked). False: the flight is over (a wall, or off its rooms).
// ponytail: one aimed along its velocity, not at a point, takes the float
// ratio for FUN_0064fc60's and counts as sent outside its room.
template <class Reach>
bool missile_fly(const Level& level, Missile& missile, Reach&& reach) {
    const int origin_x = level.world_x * 5, origin_y = level.world_y * 5;
    const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
    const bool moved_by_hand = missile.launched && (missile.x != missile.flown_x || missile.y != missile.flown_y);
    if (!missile.launched || moved_by_hand) {
        const int from_x = origin_x + subtile(missile.x), from_y = origin_y + subtile(missile.y);
        const int path_velocity = int(std::lround(std::hypot(missile.velocity_x, missile.velocity_y) * 4096 / 5));
        const int max_velocity = missile.info ? missile.info->max_vel << 8 : path_velocity, accel = missile.info ? missile.info->accel : 0;
        const bool aimed = missile.to && !moved_by_hand;
        const int to_x = aimed ? origin_x + subtile(missile.to->first) : from_x, to_y = aimed ? origin_y + subtile(missile.to->second) : from_y;
        missile.flight = d2d::rules::MissileFlight::launch(from_x, from_y, to_x, to_y, path_velocity, max_velocity, accel);
        if (!aimed) std::tie(missile.flight.aim_x, missile.flight.aim_y) = d2d::rules::missile_aim_along(missile.velocity_x, missile.velocity_y);
        if (missile.bolt >= 0 && aimed) {                    // FUN_005c9290: 77 frames at most, its wiggle
            const int bolt_to_x = to_x == from_x && to_y == from_y ? to_x + 1 : to_x, bolt_to_y = to_x == from_x && to_y == from_y ? to_y + 1 : to_y;
            missile.dies = std::min(missile.dies, missile.born + 77 * 40);
            missile.flight.points = d2d::rules::wiggle_points(from_x, from_y, bolt_to_x, bolt_to_y, int(missile.dies - missile.born) / 40,
                                                              d2d::rules::Rng{ std::uint32_t(bolt_to_x + missile.bolt) });
        }
        missile.rooms = reach(missile.x, missile.y);
        const auto& room = missile.rooms.empty() ? std::array<int, 4>{} : missile.rooms.front();
        missile.follow = !aimed || to_x < room[0] || to_y < room[1] || to_x >= room[0] + room[2] || to_y >= room[1] + room[3];   // FUN_006492f0: path flag 1
        missile.launched = true;
        missile.aimed_x = missile.velocity_x; missile.aimed_y = missile.velocity_y;
    } else if (missile.velocity_x != missile.aimed_x || missile.velocity_y != missile.aimed_y) {
        std::tie(missile.flight.aim_x, missile.flight.aim_y) = d2d::rules::missile_aim_along(missile.velocity_x, missile.velocity_y);
        missile.flight.velocity = int(std::lround(std::hypot(missile.velocity_x, missile.velocity_y) * 4096 / 5));
        missile.aimed_x = missile.velocity_x; missile.aimed_y = missile.velocity_y;
    }
    const auto inside = [](const std::array<int, 4>& rect, int x, int y) { return x >= rect[0] && y >= rect[1] && x < rect[0] + rect[2] && y < rect[1] + rect[3]; };
    const auto reached = [&](int x, int y) { return missile.rooms.empty() || std::ranges::any_of(missile.rooms, [&](const auto& rect) { return inside(rect, x, y); }); };
    bool flying = missile.flight.step([&](int x, int y) {
        return !reached(x, y) || level.blocked((float(x - origin_x) + 0.5f) / 5, (float(y - origin_y) + 0.5f) / 5, 0x04);
    });
    missile.x = missile.flown_x = float((double(missile.flight.x) / 65536 - origin_x) / 5);
    missile.y = missile.flown_y = float((double(missile.flight.y) / 65536 - origin_y) / 5);
    const int at_x = missile.flight.subtile_x(), at_y = missile.flight.subtile_y();
    if (flying && missile.follow && !missile.rooms.empty() && !inside(missile.rooms.front(), at_x, at_y)) {   // FUN_0064fad0
        if (reached(at_x, at_y)) missile.rooms = reach(missile.x, missile.y);
        else flying = missile.flight.stop();
    }
    return flying;
}

// A foe of `size` at (x, y) cells is in a subtile a missile entered this
// frame (FUN_005ae1f0 -> FUN_00641cb0).
inline bool missile_struck(const Level& level, const Missile& missile, float x, float y, int size) {
    const int foe_x = level.world_x * 5 + int(std::floor(x * 5)), foe_y = level.world_y * 5 + int(std::floor(y * 5));
    return std::ranges::any_of(missile.flight.crossed, [&](const auto& spot) { return d2d::rules::missile_touches(spot.first, spot.second, foe_x, foe_y, size); });
}

// Direction 0..31 in D2's DCC order for a world step, like direction16:
// screen sector clockwise from straight down through D2's ordering.
inline int direction32(float dx, float dy) {
    constexpr int kFromSector[32] = { 4, 16, 8, 17, 0, 18, 9, 19, 5, 20, 10, 21, 1, 22, 11, 23,
                                      6, 24, 12, 25, 2, 26, 13, 27, 7, 28, 14, 29, 3, 30, 15, 31 };
    const float screen_x = (dx - dy) * (kIsoW / 2), screen_y = (dx + dy) * (kIsoH / 2);
    const int sector = int(std::lround(std::atan2(-screen_x, screen_y) / (2 * 3.14159265f / 32)));
    return kFromSector[std::size_t((sector % 32 + 32) % 32)];
}

// Missiles fly (missile_fly; FUN_005ae1f0): a frame's move, then its
// range, then, from its Activate'th frame, a foe whose footprint holds a
// subtile it entered (rolling its to-hit) spends it either way
// (CollideKill), as does the missile barrier (0x04) or its rooms' end.
// `reach`: missile_rooms for (x, y) cells.
// ponytail: flat on the ground (no missile height); SrcDamage taken as
// 128ths of the attack's damage; a standing missile (no velocity) strikes
// what's within 0.4 cells (srvdofunc 1 never strikes standing).
// A friendly one asks `hits_monster` (true: it struck one, spent).
template <class Reach, class HitsMonster>
void missiles_update(const Level& level, std::vector<Missile>& ms_, std::span<Foe> foes, d2d::rules::Rng& rng,
                     std::uint32_t now_ms, Reach&& reach, HitsMonster&& hits_monster) {
    std::vector<Missile> laid;
    const int frame = int(now_ms / 40);
    std::erase_if(ms_, [&](Missile& missile) {
        missile.flight.crossed.clear();
        const bool moving = missile.velocity_x != 0 || missile.velocity_y != 0;
        if (moving && !missile_fly(level, missile, reach)) return true;
        if (now_ms >= missile.dies || (!moving && level.blocked(missile.x, missile.y, 0x04))) return true;
        if (missile.visual_only || (missile.info && int(now_ms - missile.born) < missile.info->activate * 40)) return false;
        if (missile.friendly) return hits_monster(missile);
        // A monster's fire wall: the maker (do 6) lays SubMissile1 where it
        // is each frame; the fire (do 5) burns a foe within half a cell each
        // frame, once a frame for the cast (they share `struck`), its row's
        // element in 256ths less resistance and magic damage reduction.
        // ponytail: the 256ths a burn leaves over round up by chance, not
        // kept on the foe's life; DamageRate unread.
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
            for (auto& foe : foes) {
                const int key = frame * 16 + int(&foe - foes.data());
                if (!foe.alive || std::hypot(foe.x - missile.x, foe.y - missile.y) > 0.5f || std::ranges::contains(*missile.struck, key)) continue;
                missile.struck->push_back(key);
                const auto& row = *missile.row;
                const int burn = std::max(d2d::rules::resisted(rng.range(row.elo, row.ehi), foe.fighter.res[std::size_t(std::clamp(row.etype, 0, 2))]) - foe.fighter.mdr * 256, 0);
                foe.damage += burn >> 8;
                if (int(rng(256)) < (burn & 255)) ++foe.damage;
            }
            return false;
        }
        for (auto& foe : foes) {
            if (!foe.alive || (moving ? !missile_struck(level, missile, foe.x, foe.y, foe.size) : std::hypot(foe.x - missile.x, foe.y - missile.y) > 0.4f)) continue;
            const int key = -100 - int(&foe - foes.data());         // a ring's missiles strike each foe once
            if (std::ranges::contains(*missile.struck, key)) continue;
            missile.struck->push_back(key);
            foe.take(d2d::rules::monster_blow(foe.fighter, foe.level, foe.moving, missile.src, true, rng, true));
            ++foe.missile_hits;
            return true;
        }
        return false;
    });
    std::ranges::move(laid, std::back_inserter(ms_));
}

// A random unique's name (the client's FUN_004ac870): on {name seed, 666},
// a suffix then a prefix into string 0x6b9 ("%0 %1"); then rand(100) < 50
// builds it again, appellation, suffix, prefix, into 0x6ba ("%0 %1 %2").
std::string unique_name(const GameData& game_data, int name_seed);

// A champion / unique / superunique / minion (rules/uniques.hpp,
// FUN_005a2120): a higher level, more life, damage and to-hit,
// resistances, speed, an element; its name. At full life.
void make_boss(const GameData& game_data, Monster& monster, d2d::rules::Boss kind, const std::vector<int>& mods, int super, int name_seed,
               int difficulty, d2d::rules::Rng& rng);

// A plain monster of MonStats row `type` at (x, y) cells: its components
// and stats rolled (a boss's by make_boss instead: `stats` false).
Monster make_monster(const GameData& game_data, int type, float x, float y, d2d::rules::Rng& rng, int difficulty, bool stats = true);

// Spawns as monsters, each looking as its unit seed picks from `region`
// (rules::monster_look).
std::vector<Monster> spawn_monsters(const GameData& game_data, std::span<const d2d::rules::Spawn> spawns, const d2d::rules::Region& region,
                                    d2d::rules::Rng& rng, int difficulty);

void set_mode(const GameData& game_data, Monster& monster, std::string_view mode, std::uint32_t now_ms);

// Damage to a monster: it dies (DT, then its corpse, DD), or recoils (GH)
// when the hit takes an eighth of its life or more; either way it notices.
// True when this killed it.
// ponytail: the eighth is the commonly given threshold, not traced.
bool hurt(const GameData& game_data, Monster& monster, int damage, std::uint32_t now_ms);

// A monster that blocked plays its block (BL), when it has one.
void block_anim(const GameData& game_data, Monster& monster, std::uint32_t now_ms);

// One step toward (tx, ty) at `speed` cells/s, straight on; false when
// something's in the way.
bool monster_step(const Level& level, Monster& monster, float target_x, float target_y, float step, const Crowd& crowd);

// A monster's frame: finish an attack / get-hit / death; flee; notice the
// player within 8 cells (then keep after them within 16), walk up and
// attack (A1: the hit lands on AnimData's event frame, MonStats A1TH vs
// the foe's defense, then block, damage reduction, resistances), wait
// aidel ticks between attacks;
// otherwise wander near home (Levels.txt MonWndr): stand 2-5 s, walk to a
// random spot within 3 cells.
// ponytail: later acts only (every Act 1 MonStats AI is traced): an AI
// whose think isn't traced (rules::traced_ai) gets one melee think (MonStats aip1..8 unread); distances and timings by eye;
// chasing goes straight at the player, sliding to a stop at walls.
// A unique's attack starting (the mode-change hook, event 0): Spectral Hit
// picks this attack's element (uniques.hpp kSpectralElement), in el[2].
void attack_starts(const GameData& game_data, Monster& monster, std::string_view mode, d2d::rules::Rng& rng);

// Returns true when the foe's thorns killed it.
// Andariel and the traced MonAI types think their own way instead: see
// rules::andariel_think / rules::mon_think. A1 / A2 fire MissA1 / MissA2.
// `pack`: all the level's monsters, `monster` among them (its group, the
// dying, corpses to raise); `born`: gets what it lays (a nest's young).
// `seen`: the spawn areas' shared "seen" flags (monster data +0x50's +0x24).
// `open_door`: its door at a think (OpenDoor).
bool monster_update(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, d2d::rules::Rng& rng,
                    std::uint32_t now_ms, float elapsed, const Crowd& crowd, std::vector<Missile>& missiles, std::span<Monster> pack = {},
                    std::vector<Monster>* born = nullptr, AreaSeen* seen = nullptr, const OpenDoor* open_door = nullptr);

// The merc's name: its hireling row's NameFirst key (merc01, merca201,
// MercX101, ...) counted on by the save's name index.
std::string merc_name(const GameData& game_data, const GameData::Merc& merc, int index);

}  // namespace d2d::game
