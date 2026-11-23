// SPDX-License-Identifier: GPL-3.0-or-later
// NPC behaviour: town NPCs walking their DS1 paths (rules::npc_think).
#pragma once

#include "ai/units.hpp"
#include "game.hpp"
#include "gamedata.hpp"

#include <combat.hpp>
#include <missiles.hpp>
#include <mon_equip.hpp>
#include <monsters.hpp>
#include <overlays.hpp>
#include <rules.hpp>
#include <town_npcs.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <initializer_list>
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

std::vector<UnitState> npc_start(const Level& level);

// A town NPC's first think, 2 frames after `now_ms`.
void file_first_think(UnitState& state, std::uint32_t now_ms);

// Town NPCs with the Npc AI think (rules::npc_think on their unit seed)
// when due: a stand thinks again n frames on, a walk at its end, a special
// mode (S1, S2) aidel (15) frames after it ends. An NPC with a visitor
// (by its index in `visitors`: rules::npc_visitor_pick, a player within 16
// with a "!" for them) walks up to it and greets it; one in `busy` (menu, speech or
// store open on it) stops and stands (rules::npc_think's visitor).
// The walk is a monster's (set_off / walk_on: toward 0xd, wall 0xf) at
// Velocity. Its first think comes 2 frames after the first call that
// meets it, unless file_first_think filed it already.
void npc_patrol(const GameData& game_data, const Level& level, std::vector<UnitState>& npcs, std::array<int, 3> busy,
                std::uint32_t now_ms, float elapsed, const Crowd& crowd = {}, std::span<const UnitState* const> visitors = {}, bool guard_aside = false);

// A player's walk from (x, y) to (gx, gy), in cells: game.exe's path type 7
// (rules::player_path: the toward pather, the search when close) over
// subtiles with the unit collision test, `to_unit` when walking up to a
// monster or NPC (near 1, FUN_006498a0), `target` that unit: its stamp,
// as its own, isn't in the way (flag 0x800, FUN_00649560). A blocked end is
// walked back toward the start (flag 0x1000, rules::end_walk_back). Points
// are subtile centres; the leading ones it stands on are skipped
// (FUN_0064fe40), and a repeat.
std::vector<std::pair<float, float>> player_walk(const Level& level, float x, float y, float goal_x, float goal_y, bool to_unit,
                                                 const Crowd& crowd = {}, const UnitState* self = nullptr, const UnitState* target = nullptr);

// A walk as a monster's (town NPCs, Cain, the merc, pets) to (gx, gy) in
// cells: rules::monster_path's points as subtile centres, a budget of 0x14
// points (FUN_005a7c20 -> FUN_006490e0). False with no step to take.
bool set_off(const Level& level, UnitState& unit, float goal_x, float goal_y, bool to_unit, const Crowd& crowd = {});

// A frame of that walk (FUN_00650840): along its path. False once it's
// over: at its last point or blocked (FUN_00650150), short of the goal or
// not; FUN_006507b0 zeroes the path's count then, so FUN_006503f0 (the
// re-path) doesn't run again. Further on is the next think's walk.
// With a `target` (a walk at a unit) its check comes first
// (rules::chase_check): within 0 of it (+0x93), over; more than 5 subtiles
// off the goal on either axis, a new path to it while the budget lasts.
bool walk_on(const Level& level, UnitState& unit, int velocity, const Crowd& crowd = {}, const UnitState* target = nullptr);

// A frame of u's walk along its path at `velocity` (path_velocity)
// (rules::walk_frame: FUN_00650840 in 16.16 subtiles), dropping reached
// points and turning it to face the way; a new subtile blocked by a wall or
// another unit ends it on the centre of the last free one (FUN_00650150),
// and `blocked`, if given, is set. False once it's over: no path left, and
// the unit on its subtile's centre (FUN_006507b0; with no path or no step
// too).
bool follow_path(const Level& level, UnitState& unit, int velocity, const Crowd& crowd = {}, bool* blocked = nullptr);
// A monster's frame off a walk: its turn on a step toward the goal, a dir set since taken at once.
void turn_frame(UnitState& unit);
// An action's facing: `dir` (16-way) taken at once, the walk's pending turn dropped.
void face(UnitState& unit, int dir);

// A pet follows the player about camp: it sets off when more than 3 cells
// behind and stops within 1.5, at `speed` cells/s on a monster's walk;
// more than 12 behind (a warp), or stuck for 1.5 s (no route), it's put
// next to the player.
// The traced pet AIs (rules::traced_pet_ai, NecroPet's necropet_think)
// think for themselves; this is the follow the untraced ones fall back on.
void pet_follow(const Level& level, UnitState& unit, float player_x, float player_y, float speed, std::uint32_t now_ms, float elapsed,
                 const Crowd& crowd = {});

// A monster's mode rate over its AnimData speed, % more.
std::uint32_t get_hit_ms(const GameData::AnimTiming& timing, int fhr);

inline int anim_rate_pct(const Monster& monster, std::string_view mode, bool skill_attack_rate = false) {
    const bool skill_mode = mode == "S1" || mode == "S2" || mode == "S3" || mode == "S4" || mode == "SQ";
    if (mode == "A1" || mode == "A2" || (skill_attack_rate && skill_mode)) return std::clamp(100 + monster.attack_pct + monster.aura_rate + d2d::rules::effective_speed(monster.props.ias), 15, 175) - 100;
    if (mode == "SC") return std::min(100 + d2d::rules::effective_speed(monster.props.fcr), 175) - 100;
    return std::clamp(100 + monster.anim_pct, 15, 175) - 100;
}
// Its rate in a mode, the speed word FUN_00623f50 stores (rules::monster_mode_rate) on the mode's AnimData
// speed: its walk % (walk_pct), MonStats row (the walk / run base, npc, under 0x19a), current skill's
// UseAttackRate, MonProp IAS / FCR / FHR, attack_pct (+ aura_rate) and anim_pct. d2d's monsters have no
// FBR or state 0x65.
int mode_rate(const GameData& game_data, const Monster& monster, std::string_view mode);
// Whether a mode's rate in game.exe is other than a % of its AnimData speed: a moving one, KB, BL, DT, DD.
bool own_rate_mode(const GameData& game_data, const Monster& monster, std::string_view mode);
// Its chill's stats (FUN_0057af80: state 0xb with velocitypercent, attackrate and other_animrate each at this,
// 0x57b0a0..0x57b0bf): its MonStats row's coldeffect on its difficulty (0x57afe2); 0 is no chill.
int chill_pct(const GameData& game_data, const Monster& monster);
// The same with the monster's current skill (monster.skill: the one its mode plays) read for its UseAttackRate;
// an own_rate_mode as mode_rate over its AnimData speed, rounded up so it stays above -100 (GH keeps
// get_hit_ms / get_hit_rate).
int anim_rate_pct(const GameData& game_data, const Monster& monster, std::string_view mode);
// The ms from a mode's start to its action event (the strike, the shot): the tick game.exe files it on, at the
// mode's rate (mode_rate); halfway through a mode with no event frame.
std::uint32_t action_due_ms(const GameData& game_data, const Monster& monster, std::string_view mode);

// A knockback's pace: FUN_00623f50 gives a player in KB (mode 0x13) or a
// monster in KB (0xd) path velocity 0x1000 (FUN_00648690), whatever its
// speed, and FUN_00554ca0 -> FUN_00650840 steps it velocity x dir >> 8 a
// frame (FUN_006502d0): one subtile, 0.2 cells. The last step snaps onto
// the path's end once it's within a step on both axes (FUN_00650090), so
// `length` cells take ceil(5 x length) frames.
inline constexpr float kKnockCellsPerFrame = 0.2f;
inline float knock_moved(std::uint32_t elapsed_ms, float length) { return std::min(float(elapsed_ms) * kKnockCellsPerFrame / 40.f, length); }
inline std::uint32_t knock_ms(float length) { return std::uint32_t(std::ceil(length / kKnockCellsPerFrame - 1e-4f)) * 40; }

// A takeoff's flight (FUN_005d9f70: the path's velocity is MonStats Run (row
// +0x34) << 8, FUN_00648690; a frame moves it velocity / 4096 subtiles,
// FUN_006502d0): Run / 16 subtiles = Run / 80 cells a frame. 0 for no Run.
inline float leap_cells_per_frame(int run) { return float(std::max(run, 0)) / 80.f; }
// The cells a takeoff has flown `elapsed_ms` in, at `pace` cells a frame, up to its `length`.
inline float leap_moved(float pace, std::uint32_t elapsed_ms, float length) { return std::min(float(elapsed_ms) * pace / 40.f, length); }

// How far along its line a takeoff has flown after `elapsed_ms` (0..1): at the takeoff's pace when it has one,
// else along `span_ms`, the sequence's time.
float leap_done(const Monster& monster, std::uint32_t elapsed_ms, std::uint32_t span_ms);

// A knocked-back unit's slide (mode KB, Fight::knock_back.
bool knock_slide(Monster& monster, std::uint32_t now_ms);

// A monster's MonProp hit events on a foe it hit: each draws on the monster's seed.
void monprop_hit(Monster& monster, Foe& foe, bool missile);

// What a Crushing Blow takes off `life`: life / divisor, less its share of the raw damageresist, not the 50 % a
// hit's reduction stops at.
std::int64_t crushing_blow(std::int64_t life, int divisor, int damage_resist);

// A monster unit (a pet, or a monster made a foe by Confuse or Attract) as MonProp knock and crush see it.
void monster_foe_props(const GameData& game_data, const Monster& monster, Foe& foe);

constexpr float kMeleeReach = 1.1f;           // cells between centres

bool soft_hit(Missile& missile, d2d::rules::Rng& rng);

// A missile's speed, cells/s, for its velocity: FUN_0059fa30's path
// velocity ((Vel + VelLev x lvl / 8) << 8) x 75 / 100 moves it velocity /
// 4096 subtiles a frame. The flight takes the path velocity back from it.
inline float missile_speed(int path_velocity) { return float(path_velocity) * 5 / 4096; }
// FUN_005a9370's 16 ways round a spot (x 0x6e2510, y 0x6e24d0): a ring of sub-missiles.
inline constexpr std::array<int, 16> kRingWayX{ 0, 1, 2, 2, 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -2, -1 };
inline constexpr std::array<int, 16> kRingWayY{ 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -2, -1, 0, 1, 2, 2 };
inline float missile_speed(const GameData::MissileInfo& info, int level) {
    return missile_speed(d2d::rules::missile_velocity(info.vel, info.vel_lev, level));
}
std::pair<float, float> offset_target(float x, float y, int offset_x, int offset_y);

// A foe of `size` at (x, y) cells is in a subtile a missile entered this frame.
bool missile_struck(const Level& level, const Missile& missile, float x, float y, int size);

// A standing missile (no velocity) strikes from its own subtile every frame.
bool standing_struck(const Missile& missile, float x, float y, int size);

// A missile's facing for a heading with no point to aim at (a velocity, a ring's or a burst's spoke).
int missile_heading(float dx, float dy);
// A missile's facing from (x, y) at the point (dx, dy) cells on.
int missile_facing(float x, float y, float dx, float dy);

// `element` onto a missile's stats: poison per frame / frames in MonStats::poison_*, any other type as an A2 El
// row in whole points (shared by missile_blow and the map's trap missiles).
void put_missile_element(d2d::rules::MonStats& stats, const d2d::rules::MissileDamage& element);
// A monster missile's blow (andariel_missile): its row's physical in 256ths, `element` (its row's or a carried
// skill's at the level); it always hits.
// `elements` entry of type 3 is the MonStats El convention, roll x 10 a frame
d2d::rules::MonStats missile_blow(const GameData::MissileInfo& info, const d2d::rules::MissileDamage& element, int monster_level);
// What a monster's MissA / MissS1 / MissC shot carries (Ai::tick, FUN_0059f900 ->
// FUN_0064b860): the physical part in 256ths, as FUN_0064af20 / FUN_0064aff0 make
// it: the owner's damage x SrcDamage / 128 (the stat is points, so x 256 first,
// truncated; none for a cast, MissC, or a row with no SrcDamage), plus the row's
// MinDamage / MaxDamage and its MinLevDam / MaxLevDam level brackets
// (FUN_00644b70, `lvl` the missile's level), shifted left by HitShift.
struct MonsterShotPhys { int low = 0, high = 0; };
MonsterShotPhys monster_shot_phys(const GameData::MissileInfo& info, int src_min, int src_max, bool cast, int lvl);
// The owner's stats as the missile carries them (what monster_blow rolls on impact).
d2d::rules::MonStats monster_shot_stats(const GameData::MissileInfo& info, d2d::rules::MonStats stats, bool first, bool cast, bool s1_shot, int lvl);
// `info` from (x, y) cells along (dx, dy), as `from`'s (its owner, level, monster level), with its own row's
// blow.
Missile missile_from(const Missile& from, const GameData::MissileInfo& info, float x, float y, float dx, float dy, std::uint32_t now_ms);

// A random unique's name.
std::string unique_name(const GameData& game_data, int name_seed);

// A champion / unique / superunique / minion: a higher level, more life, damage and to-hit, resistances, speed,
// an element; its name.
void make_boss(const GameData& game_data, Monster& monster, d2d::rules::Boss kind, const std::vector<int>& mods, int super, int name_seed,
               int difficulty, d2d::rules::Rng& rng, int players = 1, const d2d::rules::GearSum* gear = nullptr);
// A made monster's MonEquip items (rules::SpawnGear), each made as a drop on its seeds, summed
// (rules::gear_sum).
[[nodiscard]] d2d::rules::GearSum spawn_gear(const GameData& game_data, std::span<const d2d::rules::SpawnGear> gear, int difficulty);
void wear_gear(d2d::rules::MonStats& stats, const d2d::rules::GearSum& gear);
// The rest of its gear's stats, after a boss's mods (its life scaling read the
// gear's life too, make_boss; the items' list adds on top): see ai.cpp.
void wear_gear_stats(Monster& monster, const d2d::rules::GearSum& gear);
[[nodiscard]] int gear_life_fine(const d2d::rules::GearSum& gear, int base_fine);
// A boss's base life (256ths) after FUN_005a0d20 for each of `pcts` (hpmultiply, the champion
// kind; 0: that mod isn't there, no call): each reads the whole max life, its gear's included.
[[nodiscard]] int boss_life_fine(int life, std::initializer_list<int> pcts, const d2d::rules::GearSum* gear);
// MonUMod poison hit's init: its own poison stats.
void poison_hit(const GameData& game_data, Monster& monster, int difficulty, bool unique);
// Whether a skill may put the monster on a special think (FUN_0056e2f0 ->
// FUN_005dd480; FUN_00623470, FUN_005b0da0): not state 0x36, MonStats2 mWL, not MonStats
// boss, SwitchAI, targetable (unit +0xc4 & 4) or dead, not a champion, unique or
// superunique (monster data +0x16 & 0xa), not an imp (BaseId 0x1ec) riding
// (state 0x8f). State 0x36 (uninterruptable, FUN_005544b0) comes on with a
// skill's start: Whirlwind (FUN_005d8f50), Leap (FUN_005d9d50), Leap Attack
// (FUN_005da540), Nest (FUN_005cbd10) and Jump (FUN_005cb730, no monster's),
// each clearing it at its end (Monster::uninterruptable).
bool swaps_ai(const GameData& game_data, const Monster& monster);
// The AI search's pick for `monster` among `foes`: its index, or -1.
int search_foe(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, const Crowd& crowd);
// The Cursed umod's hit hook, on a melee or missile hit that lands.
void cursed_hit(const GameData& game_data, Monster& monster, std::span<Foe> foes, const Foe& struck);
// A monster's skill missile toward (dx, dy) cells.
void andariel_missile(const GameData& game_data, const Monster& monster, const std::string& name, float dx, float dy, std::uint32_t now_ms,
                      std::vector<Missile>& missiles, int lvl, const d2d::rules::Skill* carried = nullptr);
// A monster turned into MonStats row `type` in place.
void morph_monster(const GameData& game_data, Monster& unit, int type, d2d::rules::Rng& rng, int players);
// Baal's Vile Effigy: baalclone +- 12 subtiles (two draws of his seed) about (at_x, at_y), a third of his life,
// no regen; none without the row.
std::optional<Monster> vile_effigy(const GameData& game_data, const Level& level, Monster& baal, int at_x, int at_y, d2d::rules::Rng& rng, int players);
// A monster's missile made at its target, (dx, dy) cells away, heading a subtile on at (+1, +1).
void missile_at_target(const GameData& game_data, const Monster& monster, const std::string& name, float dx, float dy, std::uint32_t now_ms,
                       std::vector<Missile>& missiles, int lvl, const d2d::rules::Skill* carried = nullptr);
// A monster's skill strike (Whirlwind, Leap Attack): its A1 swing at the skill's damage, to-hit and element (ai.cpp).
d2d::rules::MonStats skill_strike(const GameData& game_data, const Monster& monster, const d2d::rules::Skill& skill, int lvl);
// Fire Hit's and Jab's hit: the swing's S1 damage, to-hit and S1 elements as A1's, plus the skill's to-hit as
// points.
void s1_strike(d2d::rules::MonStats& stats, const d2d::rules::SkillTables& skill_tables, const d2d::rules::Skill& skill, int lvl);
// FUN_005b0e00(ctrl, special): rules::ai_toggle on the monster.
void ai_toggle(const GameData& game_data, Monster& monster, int special);
// A timed state on a monster: a curse state in its curse slot, any other in its state's node; and a state's end
// (fight/).
bool put_state(const GameData& game_data, Monster& monster, Monster::SkillState next, std::uint32_t now_ms);
// A monster's war cry nova (do 68) this frame: its state on each ally of its owner it reaches (hit 18).
void war_cry_reach(const GameData& game_data, const Level& level, std::span<const Missile> missiles, std::span<Monster> monsters, std::uint32_t now_ms);
void end_state(const GameData& game_data, Monster& monster, Monster::SkillState& slot);
// State 0x18 (slowed) put by an item_slow event (FUN_005c6760) for
// rules::kClaySlowFrames: none on a MonStats npc or a MonStats2 row without
// isAtt (FUN_0056e970 0x56e9cc..0x56ea08); one already on (skill 0, level 1)
// only runs longer, its velocity cut kept, its rates set anew (0x56ea87).
void put_slow(const GameData& game_data, Monster& monster, int slow, std::uint32_t now_ms);

// A plain monster of MonStats row `type` at (x, y) cells: its components and stats rolled (a boss's by make_boss
// instead: `stats` false), for `players` in the game.
[[nodiscard]] int area_monster_level(const GameData& game_data, int level_id, int difficulty);

Monster make_monster(const GameData& game_data, int type, float x, float y, d2d::rules::Rng& rng, int difficulty, bool stats = true, int players = 1, int level_id = -1);
// A monster move's path to subtile (to_x, to_y) by its path type and Monster::path_steps, into Monster::steps
// (ai.cpp).
bool path_to(const Level& level, Monster& monster, int to_x, int to_y, bool at_foe, const Crowd& crowd);
// The players among the foes (not a pet, not a monster): the game's.
[[nodiscard]] int players_in(std::span<const Foe> foes);

// Spawns as monsters, each looking as its unit seed picks from `region`
// (rules::monster_look).
std::vector<Monster> spawn_monsters(const GameData& game_data, std::span<const d2d::rules::Spawn> spawns, const d2d::rules::Region& region,
                                    d2d::rules::Rng& rng, int difficulty, int players = 1, int level_id = -1);

// Baal Corpse Explode's corpses.
bool baal_corpse_spent(const Monster& corpse, int off_x, int off_y);
void set_mode(const GameData& game_data, Monster& monster, std::string_view mode, std::uint32_t now_ms);
// The frame of a mode's end event (unit event 1) as the mode starts at
// `frame`: its frames on, plus one (the mode starts' FUN_005417d0(1, (unit
// +0xdc >> 8) + 1 + frame), FUN_0054fe10). FUN_005415a0(1) reads it back.
int mode_end_event(const GameData& game_data, const Monster& monster, std::string_view mode, int frame);

// Damage to a monster: it dies (DT, then its corpse, DD), or recoils (GH) by rules::get_hit; either way it
// notices.
// `hit_class` and `marked` (an element or a doubled hit) are the blow's, for
// `crush` (256ths) is a Crushing Blow's share of the damage: the life it takes
bool hurt(const GameData& game_data, Monster& monster, int damage, std::uint32_t now_ms, int fraction = 0, int hit_class = 0, bool marked = false, int crush = 0);
// A landed player blow on a monster (Fight::land): Bone Armor's pool takes the physical, then `hurt`, the crush kept out of the get-hit's size.
bool blow_hurt(const GameData& game_data, Monster& monster, const d2d::rules::Blow& blow, std::uint32_t now_ms);
// Life back (FUN_0057a980): `points` whole points, capped at max life; a heal
// the cap cuts leaves it at full life, no 256ths taken off.
void heal(Monster& monster, int points);
// A freeze (FUN_0057b230: state 1 to `until`, a longer one kept): it stands till then.
inline void freeze(Monster& monster, std::uint32_t until) {
    monster.frozen_until = std::max(monster.frozen_until, until);
    monster.stun_until = std::max(monster.stun_until, until);
}
// Bone Armor's absorb on a monster: the pool takes the hit's physical; at 0 the state goes.
int soak(Monster& monster, int physical, int damage);

// A monster that blocked plays its block (BL), when it has one.
void block_anim(const GameData& game_data, Monster& monster, std::uint32_t now_ms);

// One step toward (tx, ty) at `speed` cells/s, straight on; false when
// something's in the way.
bool monster_step(const Level& level, Monster& monster, float target_x, float target_y, float step, const Crowd& crowd);

// A monster's frame: finish an attack / get-hit / death; flee; notice the player within 8 cells (then keep after
// them within 16), walk up and attack (A1: the hit lands on AnimData's event frame, MonStats A1TH vs the foe's
// defense, then block, damage reduction, resistances), wait aidel ticks between attacks; otherwise wander near
// home (Levels.txt MonWndr): stand 2-5 s, walk to a random spot within 3 cells.
void attack_starts(const GameData& game_data, Monster& monster, std::string_view mode, d2d::rules::Rng& rng);

// Returns true when the foe's thorns killed it.
// The traced MonAI types think their own way instead: see
// rules::mon_think. A1 / A2 fire MissA1 / MissA2.
// `pack`: all the level's monsters, `monster` among them (its group, the
// dying, corpses to raise); `born`: gets what it lays (a nest's young).
// `seen`: the spawn areas' shared "seen" flags (monster data +0x50's +0x24).
// `open_door`: its door at a think (OpenDoor). `players_counted`: the
// game's player count (Fight::game_players: every level's, or the /players
// count), for what it makes in play; 0 counts the players in `foes`.
// `spawning`: every level's rooms come up (missile_rooms' near rooms).
bool monster_update(const GameData& game_data, const Level& level, Monster& monster, std::span<Foe> foes, d2d::rules::Rng& rng,
                    std::uint32_t now_ms, float elapsed, const Crowd& crowd, std::vector<Missile>& missiles, std::span<Monster> pack = {},
                    std::vector<Monster>* born = nullptr, AreaSeen* seen = nullptr, const OpenDoor* open_door = nullptr,
                    Spawning::LevelState* rooms_in_play = nullptr, int players_counted = 0, const Spawning* spawning = nullptr);

// FUN_005b2f20 -> FUN_005b2a00 for a monster made in play: rings of 3
// subtiles out to 3 x radius round (x, y) (radius -1: the spot alone), each
// walked from a point drawn on the room's seed (rules::monster_detail::place;
// three draws at the spot alone), the first spot inside `room`'s rect
// (Level::rooms index; the callers pass the room FUN_00620bb0 names) where a
// monster of `type` fits: FUN_0064d9b0, its shape on the grid and among the
// units. FUN_005fd350 only refuses the nest BaseIds (0xce, 0xe4, 0x12a,
// 0x14e, 0x210), none made in play. `rooms_in_play`: the level's rooms as
// play left them (Spawning::LevelState: up, their seeds); a room not up
// places nothing (game.exe has no room there, FUN_00463740).
bool spawn_spot(const GameData& game_data, const Level& level, const Crowd& crowd, Spawning::LevelState* rooms_in_play, int type, int x, int y, int radius,
                int room, int& out_x, int& out_y);

// FUN_0064e7b0: the subtile nearest (x, y) where something `size` across
// (FUN_00620510: a player 2, a monster its MonStats2 SizeX) has no bit of
// `mask` (FUN_0064d9b0 by size: 0 / 1 the subtile, 2 the plus, 3 the 3 x 3
// box, more never), by nearest_spot's rings. The grid's bits are Level's,
// the units' (0x1000 / 0x2000) the Crowd's, `self` not counted (its own
// stamp is lifted or elsewhere when its callers search). None: nullopt.
std::optional<std::pair<int, int>> free_spot_near(const Level& level, const Crowd& crowd, const UnitState* self, int x, int y, int size, std::uint16_t mask);

// The merc's name: its hireling row's NameFirst key (merc01, merca201,
// MercX101, ...) counted on by the save's name index.
std::string merc_name(const GameData& game_data, const GameData::Merc& merc, int index);

}  // namespace d2d::game
