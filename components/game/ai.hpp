// NPC behaviour: town NPCs patrolling their DS1 paths.
#pragma once

#include "game.hpp"
#include "gamedata.hpp"

#include <combat.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

// Where each world NPC is right now (index-aligned with Level::npcs):
// patrolling NPCs walk their DS1 path, pausing at each point.
struct UnitState {
    float x = 0, y = 0;
    int dir = 0;
    bool walking = false;
    std::size_t next = 0;             // path point being walked to
    std::uint32_t wait_until = 0;     // ms; idle until then
    std::uint32_t mode_ms = 0;        // when the current mode (walk/idle) started
    std::uint32_t stuck_since = 0;    // ms the merc last got blocked, 0 = moving
    float goal_x = 0, goal_y = 0;     // where the merc's route was planned to
    bool hidden = false;              // a quest-gated NPC who isn't here (yet)
    bool alert = false;               // has something new to say on a quest: the balloon over its head
    std::string_view mode;            // an object's mode now, "" = its start mode (Npc::mode)
    std::vector<std::pair<float, float>> path;   // a walk_path route being followed
};

std::vector<UnitState> npc_start(const Level& level);

// The units moving about (the player, the merc, patrolling NPCs): each
// blocks the others within 0.3 cells (1.5 subtiles) of its centre, so
// routes go round them and walkers wait. Standing NPCs are already in
// the walk grid.
// ponytail: centres only — D2 stamps each unit's collision pattern with
// its own bits (0x800 monsters, 0x1000 players) and walkers test mask
// 0x1c09.
struct Crowd {
    std::vector<const UnitState*> units;
    [[nodiscard]] bool at(float x, float y, const UnitState* self) const {
        for (const auto* other : units)
            if (other != self && std::abs(other->x - x) < 0.3f && std::abs(other->y - y) < 0.3f) return true;
        return false;
    }
};

// Patrolling NPCs: walk to the next DS1 path point, idle a few seconds
// there, move on. NPCs in `busy` (menu, speech or store open on them)
// stand still. ponytail: the per-point action (1..4 — likely S1
// specials like Charsi's hammering) isn't interpreted; pauses are 2-5 s.
void npc_patrol(const Level& level, std::vector<UnitState>& npcs, std::array<int, 3> busy,
                std::uint32_t now_ms, float elapsed, const Crowd& crowd = {});

// A walkable route from (x, y) to (gx, gy), in cells: rules::find_path
// over subtiles with the unit collision test, then string-pulled (a turn
// is dropped while the straight line past it stays clear). It ends on
// the goal itself when that's walkable, else as close as it gets.
std::vector<std::pair<float, float>> walk_path(const Level& level, float x, float y, float goal_x, float goal_y,
                                               const Crowd& crowd = {}, const UnitState* self = nullptr);

// Moves u along its path by `step` cells, dropping reached points and
// turning it to face the way; blocked by a wall or another unit, it
// drops the route. False once there (or stuck).
bool follow_path(const Level& level, UnitState& unit, float step, const Crowd& crowd = {});

// The mercenary follows the player: it sets off when more than 3 cells
// behind and stops within 1.5, at `speed` cells/s along a walk_path;
// more than 12 behind (a warp), or stuck for 1.5 s (no route), it's put
// next to the player.
// ponytail: D2's follow distances aren't traced.
void merc_follow(const Level& level, UnitState& unit, float player_x, float player_y, float speed, std::uint32_t now_ms, float elapsed,
                 const Crowd& crowd = {});

// A monster in the level: its type (MonStats row), composite recipe with
// the components it rolled, where it is, its stats and what it's doing.
// Spawn areas' shared "seen" flags by ai.cpp's area key (search_target).
using AreaSeen = std::unordered_map<int, bool>;

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
    // whatever the room. Then a move's re-path budget left in cells (path
    // +0x94), and the foe a chase follows (an index into the foes).
    bool sighted = false, force_sight = false;
    int area = -2;
    float path_left = 0;
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
    bool in_aura = false;                     // within the player's aura (its auratargetstate: Conviction's convicted ...)
    // Andariel's think (MonAI 34): the skill its mode is using (-1 a plain
    // swing), the last SC frame the spray fired, where its target stood
    // when the spray began (subtiles), a random walk under way (to
    // unit.goal) rather than a chase.
    int skill = -1, skill_frame = 0, skill_x = 0, skill_y = 0;
    bool wandering = false;
    // The MonAI thinks: the last mode it left other than NU (monster data
    // +0x54, FUN_005a68e0; GH: it got hit), the AI's scratch words (+0x14,
    // +0x18), its command (the Fallen's 1: charge), the corpse a skill raises.
    std::string_view left_mode = "NU";
    int ai_state = 0, ai_state2 = 0, ai_command = 0, skill_unit = -1;
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
    int poison = 0, poison_ticks = 0;         // poison taken this frame: total, over ticks
    bool blocked = false;                     // blocked a hit this frame
    std::vector<const Monster*> melee_by;     // who struck at it in melee this frame (Frozen / Shiver Armor)
    int missile_hits = 0;                     // missiles that reached it this frame (Chilling Armor)
    int mana_burn = 0;                        // mana it lost to Mana Burn this frame
    int amplify = 0;                          // Amplify Damage cast on it this frame (a Cursed boss): its level
    bool pet = false;                         // the merc, a summon: in the player's list
    int size = 2;                             // FUN_00620510: a player's 2, a monster's MonStats2 SizeX
    void take(const d2d::rules::Taken& taken) {
        blocked = blocked || taken.blocked;
        damage += taken.damage;
        if (taken.poison > 0) { poison += taken.poison; poison_ticks = std::max(poison_ticks, taken.poison_ticks); }
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
};

// Direction 0..31 in D2's DCC order for a world step, like direction16:
// screen sector clockwise from straight down through D2's ordering.
inline int direction32(float dx, float dy) {
    constexpr int kFromSector[32] = { 4, 16, 8, 17, 0, 18, 9, 19, 5, 20, 10, 21, 1, 22, 11, 23,
                                      6, 24, 12, 25, 2, 26, 13, 27, 7, 28, 14, 29, 3, 30, 15, 31 };
    const float screen_x = (dx - dy) * (kIsoW / 2), screen_y = (dx + dy) * (kIsoH / 2);
    const int sector = int(std::lround(std::atan2(-screen_x, screen_y) / (2 * 3.14159265f / 32)));
    return kFromSector[std::size_t((sector % 32 + 32) % 32)];
}

// Missiles fly; one reaching the foe rolls its to-hit and is spent
// either way (CollideKill), as is one hitting the missile barrier (0x04)
// or out of range.
// ponytail: the barrier bit is read off the Blood Moor's DT1 flags (0x05
// cliffs vs 0x01 camp clutter); the missile collision code isn't traced.
// ponytail: flat on the ground (no missile height); SrcDamage taken as
// 128ths of the attack's damage.
// A friendly one asks `hits_monster` (true: it struck one, spent).
template <class HitsMonster>
void missiles_update(const Level& level, std::vector<Missile>& ms_, std::span<Foe> foes, d2d::rules::Rng& rng,
                     std::uint32_t now_ms, float elapsed, HitsMonster&& hits_monster) {
    std::vector<Missile> laid;
    const int frame = int(now_ms / 40);
    std::erase_if(ms_, [&](Missile& missile) {
        missile.x += missile.velocity_x * elapsed; missile.y += missile.velocity_y * elapsed;
        if (now_ms >= missile.dies || level.blocked(missile.x, missile.y, 0x04)) return true;
        if (missile.visual_only) return false;
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
            if (!foe.alive || std::hypot(foe.x - missile.x, foe.y - missile.y) > 0.4f) continue;
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
// ponytail: an AI whose think isn't traced (rules::traced_ai) gets one
// melee think (MonStats aip1..8 unread); distances and timings by eye;
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
bool monster_update(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, d2d::rules::Rng& rng,
                    std::uint32_t now_ms, float elapsed, const Crowd& crowd, std::vector<Missile>& missiles, std::span<Monster> pack = {},
                    std::vector<Monster>* born = nullptr, AreaSeen* seen = nullptr);

// The merc's name: its hireling row's NameFirst key (merc01, merca201,
// MercX101, ...) counted on by the save's name index.
std::string merc_name(const GameData& game_data, const GameData::Merc& merc, int index);

}  // namespace d2d::game
