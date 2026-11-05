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

// A monster's velocitypercent (stat 0x43) as its stat initialiser sets it (FUN_00573cb0, SetStat(0x43, 0x4b) at 0x573f16).
inline constexpr int kMonsterVelocityPct = 75;

// Where each world NPC is right now (index-aligned with Level::npcs):
// patrolling NPCs walk their DS1 path, pausing at each point.
struct UnitState {
    float x = 0, y = 0;
    UnitShape shape;                  // its collision (gamedata.hpp); a player's by default
    int dir = 0;
    bool walking = false;
    bool greets = false;              // a town NPC's think greeted the player just now (unit sound 0x12; World plays it)
    std::size_t next = 0;             // path point being walked to
    std::uint32_t wait_until = 0;     // ms; idle until then
    bool filed = false;               // a town NPC's first think is filed (npc_patrol: 2 frames after it's met)
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
        return at(x, y, self ? self->shape : player, self);
    }
    // As something of shape `mover` would stand there, `self` not counted.
    [[nodiscard]] bool at(float x, float y, const UnitShape& mover, const UnitState* self) const {
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
            static constexpr std::pair<int, int> kNeighbours[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            if (other->shape.box())
                for (const auto& [step_x, step_y] : kNeighbours)
                    if (covers(other_x + step_x, other_y + step_y)) return true;
        }
        return false;
    }
};

// A town NPC's first think, 2 frames after `now_ms`: game.exe files it as
// the NPC's room comes into play (its first client, FUN_0053a8e0, or made
// in one already in play, FUN_00554850) through FUN_00573780: mode NU,
// event 2 at frame + 2 (0x5737c0). World files the states its rooms grow
// in a frame; npc_patrol files the rest the first time it meets them.
void file_first_think(UnitState& state, std::uint32_t now_ms);

// Town NPCs with the Npc AI think (rules::npc_think on their unit seed)
// when due: a stand thinks again n frames on, a walk at its end, a special
// mode (S1, S2) aidel (15) frames after it ends. An NPC with a "!" walks
// up to `player` within 16 and greets it; one in `busy` (menu, speech or
// store open on it) stops and stands (rules::npc_think's visitor).
// The walk is a monster's (set_off / walk_on: toward 0xd, wall 0xf) at
// Velocity. Its first think comes 2 frames after the first call that
// meets it, unless file_first_think filed it already.
void npc_patrol(const GameData& game_data, const Level& level, std::vector<UnitState>& npcs, std::array<int, 3> busy,
                std::uint32_t now_ms, float elapsed, const Crowd& crowd = {}, const UnitState* player = nullptr, bool guard_aside = false);

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
// The traced pet AIs (rules::traced_pet_ai, NecroPet's necropet_think)
// think for themselves; this is the follow the untraced ones fall back on.
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
    std::uint32_t bolts_due = 0;              // Lightning Enchanted's event 7, queued by a hit in GH (0 none)
    bool fx_done = false;
    std::string_view swing_mode;               // its last attack mode (A1, A2, SC, S1..S4; "" none yet): the stats it set (FUN_005a4f50)
    std::array<bool, 3> element_on{};          // which of its elements that swing rolled in (roll_elements)
    std::array<int, 3> rand_kind{};            // a rand element's kind that swing rolled, 1..5
    // Its stats as the last swing set them: the elements it didn't roll in left out.
    // A rand one takes its rolled kind (FUN_005a4f50's table at 0x5a5444: 1 fire,
    // 2 lightning, 3 magic, 4 cold, 5 poison), a cold one 25 frames when it has none.
    // Past Normal a hostile one's swing grows by its players (FUN_005a4f50 in an
    // expansion game, game +0x70: stat 0x64 through FUN_005a4f20, table 0x6e21f4):
    // damage, to-hit and each element's min / max / length + n / 128 of it.
    // In a classic game it's classic_stats' cut instead.
    [[nodiscard]] d2d::rules::MonStats swing_stats() const {
        static constexpr std::array<int, 6> kRandType{ -2, 0, 1, 4, 2, 3 };   // kind -> d2d's El type (fire, ltng, cold, pois, mag)
        auto swung = classic_stats();
        if (const int more = !classic && align == 0 && difficulty > 0 ? d2d::rules::player_swing_128ths(players) : 0; more > 0) {
            for (int* value : { &swung.a1_min, &swung.a1_max, &swung.a2_min, &swung.a2_max, &swung.s1_min, &swung.s1_max, &swung.to_hit, &swung.to_hit2, &swung.to_hit_s1 })
                *value += *value * more / 128;
            for (auto& element : swung.elements)
                for (int* value : { &element.min, &element.max, &element.dur }) *value += *value * more / 128;
        }
        for (std::size_t element = 0; element < swung.elements.size(); ++element)
            if (auto& swung_element = swung.elements[element]; swung_element.mode == swing_mode) {
                if (!element_on[element]) { swung_element = {}; continue; }
                swung_element.pct = 100;
                if (swung_element.type == -3) {
                    swung_element.type = kRandType[std::size_t(std::clamp(rand_kind[element], 0, 5))];
                    if (swung_element.type == 2 && swung_element.dur == 0) swung_element.dur = 25;
                }
            }
        return swung;
    }
    // Its stats as a swing sets them in a classic game (FUN_005a4f50, game
    // +0x70 0: rules::classic_swing); its own stats in an expansion game.
    [[nodiscard]] d2d::rules::MonStats classic_stats() const {
        auto cut = stats;
        if (classic) d2d::rules::classic_swing(cut, difficulty, row_align);
        return cut;
    }
    std::string_view fx_mode = "NU";         // its mode as of the last boss_events (a scarab's GH / DT comes once)
    std::vector<std::uint32_t> scarab_due;    // when a scarab's bolts are due, one a GH / DT (game.exe queues each event 7)
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
    // Co-op clients get hit_points, not life_frac (replication.hpp): still
    // finer than game.exe's own client, which only ever sees life / 128 of
    // the maximum (net-packets.md "Monster life"; 0x0c / 0x0d / 0xab). The
    // server is authoritative.
    int life_frac = 0;                        // 256ths of a point already taken off hit_points (life = hit_points x 256 - life_frac)
    int leader = -1;                          // index of its group's leader
    int difficulty = 0;
    float home_x = 0, home_y = 0;             // where it spawned: wandering stays near
    std::string_view mode = "NU";             // animation mode token
    std::string_view last_mode = "NU";        // as of the last sound check
    bool sounded = false;                     // its sounds have started (its Init played, FUN_004cc380)
    bool fleeing = false, flee_sound = false;  // Fallen's flee event while a nearby monster dies
    int seq_sound = 0, seq_sounds = 0;        // a Jab sequence's event-3 rows passed and its dosound b: played or dropped each frame (Fight::monster_sounds)
    std::uint32_t sound_ms = 0, neutral_ms = 0, step_ms = 0;   // its last sound, Neutral, footstep (unit +0x7c / +0x80 / +0x84)
    std::uint32_t sound_tick = 0;             // the tick its Neutral last rolled
    int hit_class = 0;                        // the last hit's (unit +0xb0): the impact its GH / KB / DT plays
    int struck_by = -1;                       // the Foe::id that last struck at it, -1 none (unit +0xa0, FUN_00621d50 from FUN_0057c6c0)
    std::uint32_t mode_until = 0;             // ms: an attack / get-hit / death ends
    std::uint32_t next_act = 0;               // ms: may attack again (aidel)
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
    // Unit flags a skill's young carry (+0xc4): 0x4000000 no experience for
    // its kill (FUN_005a4ef0 skips FUN_0057e990), 0x20000 no drop
    // (FUN_005a6830 returns at once).
    bool no_exp = false, no_drop = false;
    int cage = -1;                            // a5q2's prisoner: its cage's slot (Spawn::cage)
    // Damage over time (life a millisecond, until when) and a chill.
    double poison_rate = 0, bleed_rate = 0, dot_acc = 0;
    std::uint32_t poison_until = 0, bleed_until = 0, chill_until = 0;
    bool shatter = false;                     // state 107 (FUN_0057af80's roll on a chill)
    std::uint32_t no_heal_until = 0;          // ms: state 0x34 (preventheal, FUN_0057d810)
    std::uint32_t stunned_until = 0;          // ms: a stun's end (state 21); a stand (below) ending at another time is a freeze's (state 1)
    std::uint32_t stun_until = 0;             // ms: stunned, it stands
    std::uint32_t just_hit_until = 0;         // ms: state 0x56 justhit (FUN_005adb00), NextHit missiles pass it by
    // Aura Enchanted (uniques.hpp boss_aura): its aura, level, next pulse;
    // and the damage / to-hit % it gets from bosses' auras this frame.
    int aura = 0, aura_lvl = 0;
    std::vector<std::string_view> host_states;   // a joined game's: the States.txt names the host has on it (0xa8 / 0xaa)
    std::uint8_t looks = 0;                      // its thinks' states: 0x5a "blue" (bit 1), 0x5b "red" (bit 2)
    int flash_overlay = -1;                      // its think's one-shot overlay (FUN_00621e40: an Overlay.txt row), from flash_at
    std::uint32_t flash_at = 0;
    std::uint32_t aura_next = 0;
    int aura_dmg = 0, aura_th = 0, aura_rate = 0;   // a boss's aura over it: damagepercent, item_tohit_percent, attackrate (Fanaticism)
    // Skills' states on it (their auratargetstate): one curse at a time
    // (Amplify Damage .. Lower Resist, Confuse, Attract), one other
    // (Battle Cry, Taunt, Inner Sight, Cloak of Shadows' blindness).
    // `swaps`: its end toggles the AI back (FUN_005c3370: Dim Vision's,
    // Terror's, Cloak of Shadows').
    struct SkillState { int skill = -1, level = 0; std::uint32_t until = 0; bool swaps = false; };
    SkillState curse, cry;
    // MonFrenzy's state (FUN_005d8c70: "monfrenzy", its charges stat 0xa9
    // as the level, up to the skill's own): its aurastats as curse's.
    SkillState frenzy;
    // A self state (do 18 with an aurastate, not Bone Armor: UberIzual's
    // Chilling Armor): its aurastats on it (skill_armor_percent) as a
    // curse's; a ReturnFire missile striking it is answered (Fight::return_fire).
    SkillState self_state;
    // What they do to it, as of this frame (Fight::monster_states): its
    // damage % and speed %, the share of its melee damage it takes back
    // (Iron Maiden), Terror's state (0x38) on it.
    int dmg_pct = 0, speed_pct = 0, reflect_pct = 0;
    int anim_pct = 0;                         // attackrate / other_animrate (stats 68 / 69): its modes play faster
    bool afraid = false;
    // Converted from a higher level (state 0x6d): its own level and life
    // (stats 0xb0 / 0xb1), put back as Conversion ends (FUN_005d01a0).
    int own_level = 0, own_max_life = 0;
    // Its alignment (stat 0xac: 0 evil, 1 neutral while Confuse or Attract
    // is on it, 2 good while Conversion is: in its caster's list, FUN_005b1900) and its skill-set target (monster data +0x38 kind, +0x34
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
    bool at_point = false;                    // its mode's shot at (skill_x, skill_y), not its foe (FUN_005ddfc0: the wisps' dance)
    bool wandering = false;
    // The MonAI thinks: the last mode it left other than NU (monster data
    // +0x54, FUN_005a68e0; GH: it got hit), the AI's scratch words (+0x14,
    // +0x18, +0x1c), its command (the Fallen's 1: charge), the corpse a
    // skill raises; the velocity % of the move its last think started
    // (rules::ThinkIn::pace), until when it's in Spider Lay's state (0x16).
    std::string_view left_mode = "NU";
    int ai_state = 0, ai_state2 = 0, ai_state3 = 0, ai_command = 0, skill_unit = -1;
    // AI command kind 1 (FUN_0058f730: the overseer's Cry Help, do 128): the
    // unit its group goes at (a Foe::id) till that frame.
    int command_foe = -1, command_until = 0;
    bool impregnated = false;                 // state 0x6e (Impregnate, do 133): dead, a painworm comes out (FUN_005d21b0)
    // A skill's sequence (MonSeq.txt): its next row, and the shot done
    // (unit flag 0x40: no more of its events 1 / 2, FUN_005a8670).
    int seq_done = 0;
    bool seq_once = false;
    // The row it's on, as drawn: its own mode and frame (FUN_00663410), so
    // a jab's A1 rows then its A2 ones; -1 while no sequence plays.
    std::string_view seq_mode;
    int seq_frame = -1;
    int charging = -1;                        // Charge (srvstfunc 31) under way: its skill, swung at its end
    // DiabRun under way (seq_diablorun): 1 winding up, 2 running, 3 stopping;
    // the phase's start.
    int diab_run = 0;
    std::uint32_t diab_run_ms = 0;
    // Leap (srvstfunc 40) under way: where it rose and where it lands (cells).
    bool leaping = false;
    // Nest's start (srvstfunc 49, FUN_005cbd10) till its lay (do 91, FUN_005cbe00).
    bool nesting = false;
    // State 0x36 (uninterruptable, FUN_005544b0): a whirl, leap or nest under way.
    [[nodiscard]] bool uninterruptable() const { return whirl || leaping || nesting; }
    // A sand maggot down (MagottDown's start, FUN_005cb270: unit flags 0xe
    // off, its 0x1000 lifted): no one targets, hits or walks into it till
    // MagottUp (FUN_005cb170) brings it up at the nearest free spot.
    bool buried = false;
    // A vulture aloft (FUN_005f2eb0's take-off: flags 0xe off, its stamps
    // lifted, pattern 5, path mask 0): untargetable, it flies over
    // anything till it lands (FUN_005f2fc0).
    bool aloft = false;
    // A siege beast's or barricade tower's imp and its mount, bound to each
    // other (unit +0x98, FUN_00621ce0): the pack index of the other, -1 none.
    // The imp rides (state 0x8f, FUN_005d18e0): flags 0xe off, it sits on
    // its mount and thinks the ride's think (special 16).
    int bound = -1;
    bool riding = false;
    // Gone from the game without dying (FUN_005ddfc0: the Dark Wanderer at
    // his walk's end): no corpse, no kill, not drawn.
    bool gone = false;
    // MonStats killable (game.exe: unit flag 0x4 off, FUN_005dca70's test):
    // the Dark Wanderer, the compelling orb. No one targets or hits it.
    bool killable = true;
    float leap_from_x = 0, leap_from_y = 0, leap_to_x = 0, leap_to_y = 0;
    std::uint32_t leap_until = 0;             // Leap Attack's landing (its sequence's row 11), else the mode's end
    // Whirlwind as a monster casts it (srvstfunc 38 / do 76): its A1 sequence
    // looping along its path since whirl_ms, the rows passed, the last unit hit.
    bool whirl = false;
    std::uint32_t whirl_ms = 0;
    int whirl_row = 0, whirl_last = -1;
    int move_pct = 0;
    // A breath (srvdofunc 95, FUN_005cc4e0; the fetish shaman's Inferno):
    // its srvmissilea every calc3 frames till calc2 frames on (FUN_005cc240's
    // end, skill +0x18), the mode held; state 0xc on till its think clears it.
    int breath = -1;
    std::uint32_t breath_until = 0, breath_next = 0;
    bool breath_state = false;
    // Act 4's: Diablo's kept point (his leader word, kind 10: his own spot at
    // his first think, FUN_0058ef40); a knight's Bone Armor up since its
    // think, and its absorb pool (stat 132, the cast's aurastat calc, in 256ths).
    bool has_point = false, armored = false;
    int armor_pool = 0;
    bool greeted = false;                     // Izual's first think (ThinkEffect::greet): a4q1's FUN_005b4390, the World's to take
    // A prisoner's (Wussie, a5q2) view, set by World::act5_update: its cage's
    // portal (FUN_00588d60, subtiles), one of its cage gone through it
    // (FUN_00588830), a player within 16 (FUN_005ddf20) and a broken door in
    // that player's rooms (FUN_00588e10); and what its think asked of the
    // record: the log 5 (FUN_00588dd0), the portal (FUN_005888d0), gone
    // through it (FUN_00588880).
    struct WussieView {
        bool portal = false, cage_left = false, player = false, door_broken = false;
        int portal_x = 0, portal_y = 0, player_id = 0;
        bool cried = false, open_portal = false, left = false;
    } wussie;
    bool asleep = false;                      // an Ancient while a5q5's record +0x11 is unset (FUN_0058cf90; World::act5_update)
    // Baal's (a5q6; World::act5_update takes them): a wave's superunique to
    // make at (x, y + 13) (skill 286), he's done on the throne (to
    // baalcrabstairs), the Chamber's portal's spot (object 563, subtiles)
    // and he's gone in.
    int baal_wave = -1;
    bool baal_leaves = false, baal_in = false;
    std::optional<std::pair<int, int>> chamber_portal;
    int point_x = 0, point_y = 0;
    std::uint32_t laid_until = 0;
    // Its unit seed (+0x20) as its look left it: what its thinks draw. A
    // special AI (AI control [0], FUN_005b0e00: the Countess's 0xd, Dim
    // Vision's / Cloak's 10, Terror's / Howl's 11) or its MonAI row's other
    // think (`other`, rules::ai_toggle), its map AI's points (subtiles),
    // `half_freeze`: the act boss / superunique FUN_00639db0(unit, 0x76, 1)
    // call (Blood Raven, the Countess), which sets state 118
    // (corpse_noselect, FUN_00625a70's state bits), not stat 0x76: nothing
    // reads it for a freeze.
    d2d::rules::Rng seed;
    int special = 0;
    bool other = false;
    std::vector<std::pair<int, int>> path;
    bool half_freeze = false;
    int variant = -1;                         // the knights' rand(4) as made (d2d::rules::monster_variant), -1 none
    d2d::rules::MonProps props;               // its MonProp row's (d2d::rules::monster_props)
    int players = 1;                          // the players in the game as it was made (stat 0x64 monster_playercount)
    int row_align = 0;                        // its MonStats Align (+0x4c), as made (align is its side now)
    bool classic = false;                     // made in a classic game (game +0x70 0: Fight::add_monster)
    int level_id = -1;                        // the Levels.txt id it was made in (-1: unknown): Nightmare / Hell start at that level's MonLvl (rules::start_level)
    [[nodiscard]] bool alive() const { return hit_points > 0 && !gone; }
    [[nodiscard]] bool targetable() const { return alive() && killable && !buried && !aloft && !riding; }   // unit flags 0xe: not a buried maggot, a vulture aloft, a riding imp, an unkillable one
    // The cursor drops it (FUN_00466870 -> FUN_00465c60, cursor-units.md): in town
    // nothing for alignment; elsewhere MonStats2 alSel passes, noSel refuses, else
    // the player's alignment (2: a converted monster, FUN_00650d70) refuses.
    [[nodiscard]] bool ally_not_interact(const GameData& game_data, int in_level) const {
        if (d2d::rules::level_ids::is_town(in_level) || type < 0 || std::size_t(type) >= game_data.monsters.types.size()) return false;
        const auto& type_info = game_data.monsters.types[std::size_t(type)];
        return !type_info.al_sel && (type_info.no_sel || align == 2);
    }
    // As a target for the player's (or the merc's) hits.
    [[nodiscard]] d2d::rules::Target target(const GameData& game_data) const {
        const auto& type_info = game_data.monsters.types[std::size_t(type)];
        const auto& per_difficulty = type_info.diff[std::size_t(difficulty)];
        auto res = per_difficulty.res;
        for (std::size_t i = 0; i < 6; ++i) res[i] += boss_res[i];
        return { hit_points, stats.hit_points, stats.armor_class, stats.level, type_info.can_block ? per_difficulty.to_block : 0, res, per_difficulty.drain,
                 crush_divisor(game_data), type_info.undead, type_info.demon };
    }
    // Crushing Blow's life divisor on it (FUN_005bffc0): 10 for a hireling
    // (FUN_0063ee90's classes 0x10f, 0x152, 0x167, 0x230, 0x231); else 4, 8
    // for a MonStats boss (+0xc & 0x40, FUN_0063e9f0) or a unique (data
    // +0x16 & 2, FUN_005a0180), raised by its monster_playercount share
    // (FUN_005738f0: 0 for one player, +50 % each after).
    [[nodiscard]] int crush_divisor(const GameData& game_data) const {
        if (type == 0x10f || type == 0x152 || type == 0x167 || type == 0x230 || type == 0x231) return 10;
        const auto& type_info = game_data.monsters.types[std::size_t(type)];
        const bool unique = boss == d2d::rules::Boss::unique || boss == d2d::rules::Boss::superunique;
        const int divisor = type_info.boss_column || unique ? 8 : 4;
        return divisor + divisor * d2d::rules::player_life_pct(std::max(players, 1)) / 100;
    }
};

// A monster's mode rate over its AnimData speed, % more (FUN_00623f50):
// anim_pct (attackrate / other_animrate from auras), and its MonProp
// IAS on its attack modes (A1, A2: 0x6e8b90 / 0x6e8cd0 mark them so for
// every monster) and FCR on its cast mode (SC, FUN_006216e0), each
// 120 x v / (120 + v) (FUN_00621740's table at 0x6e8e24). A skill's
// sequence plays as SQ, neither.
// ponytail: S1..S4 / SQ are attack modes too for a monster holding a
// weapon (FUN_00621580), never here; FUN_00623f50's 15..175 clamps and
// the cast mode's skipping attackrate / other_animrate aren't applied.
// A get-hit's length in ms: rules::get_hit_ticks on the mode's AnimData
// record (frames and speed, the COF's for a mode animdata lacks) and the
// unit's FHR, a tick 40 ms.
inline std::uint32_t get_hit_ms(const GameData::AnimTiming& timing, int fhr) {
    const int frames = int(timing.frames ? timing.frames : timing.cof_frames), speed = int(timing.speed ? timing.speed : timing.cof_speed);
    return std::uint32_t(d2d::rules::get_hit_ticks(frames, speed, fhr)) * 40u;
}

inline int anim_rate_pct(const Monster& monster, std::string_view mode) {
    if (mode == "A1" || mode == "A2") return monster.anim_pct + monster.aura_rate + d2d::rules::effective_speed(monster.props.ias);
    if (mode == "SC") return monster.anim_pct + d2d::rules::effective_speed(monster.props.fcr);
    return monster.anim_pct;
}

// Whoever the monsters are after (the player, the merc), and what they
// did to it this frame. The player comes first and its pets after it, as in
// game.exe's player list (FUN_005b1900).
struct Foe {
    float x = 0, y = 0;
    int level = 1;
    bool alive = true, moving = false;        // moving: walking or running (evade, not dodge)
    d2d::rules::Fighter fighter;                    // defense, block, reductions, resistances, thorns
    int damage = 0;                           // life lost this frame, whole points
    int physical = 0, elemental = 0;          // of it, physical and fire / lightning / cold (the absorbs')
    int frac = 0;                             // 256ths of a point the physical took beyond `damage` (Taken::frac); may pass 255
    int poison = 0, poison_ticks = 0;         // poison taken this frame: life per tick (256ths), for ticks
    int chill_ticks = 0;                      // chilled this frame, ticks
    int stun_ticks = 0;                       // stunned this frame (a monster's Smite), ticks
    std::optional<std::pair<float, float>> knocked_from;   // knocked back this frame by a unit there (result flag 8: Smite)
    bool blocked = false;                     // blocked a hit this frame
    // Who struck at it in melee this frame (Shiver Armor) and, of those, whose blow did damage (Frozen
    // Armor: the event's +8 over 0), by Monster::id: monsters born this frame may move the vector.
    std::vector<int> melee_by, damaged_by;
    std::vector<int> fired_by;                // the owners of the ReturnFire missiles that reached it this frame (Chilling Armor)
    std::vector<int> shot_by;                 // the owners of the monster missiles that hit it this frame (their MonProp events)
    std::vector<int> crushed;                 // Crushing Blows this frame, each its life divisor (MonProp crush, FUN_005bffc0)
    int knock_under = 0x40;                   // MonProp knock lands on a draw's low 7 bits under it (FUN_005bf960)
    int crush_divisor = 10;                   // MonProp crush's life divisor, before a missile's doubling (FUN_005bffc0)
    int hits = 0;                             // hits that landed this frame (each wears its armour)
    int hit_class = 0;                        // the last landed blow's this frame: a melee monster's MonStats2 HitClass, a missile's Missiles.txt one
    // Each landed blow this frame with its own damage, hit class, element mark and parts: game.exe
    // absorbs and tests the get-hit one damage record at a time (FUN_0057c6c0 then FUN_0057cee0 per
    // hit: 0x56ba73, 0x5ad8c6).
    using Blow = d2d::rules::StruckBlow;
    std::vector<Blow> blows;
    void struck_by(int blow_class) { hit_class = blow_class; if (!blows.empty()) blows.back().hit_class = blow_class; }
    bool running = false;                     // the player in RN: always hit by a swing, block a third
    bool neutral = true;                      // in mode 1 (NU): not walking, swinging, casting or struck (BaalTaunt's still)
    [[nodiscard]] d2d::rules::Motion motion() const { return running ? d2d::rules::Motion::running : moving ? d2d::rules::Motion::walking : d2d::rules::Motion::standing; }
    int mana_burn = 0;                        // mana it lost to Mana Burn this frame
    struct Cast { int skill = -1, level = 0, pick = -1; };   // pick: a monster's random curse's (rules::monster_curse_stats), else -1
    Cast cursed;                              // a curse cast on it this frame (a Cursed boss's Amplify Damage, the Summoner's Weaken)
    Cast marked;                              // a missile's hit 19 state put on it this frame (the finger mage's spider)
    std::vector<std::string_view> states;     // the states on it a think reads (its curse's, its mark's: auratargetstate names)
    bool pet = false;                         // the merc, a summon: in the player's list
    int size = 2;                             // FUN_00620510: a player's 2, a monster's MonStats2 SizeX
    int threat = 14;                          // FUN_005dc920: a player's 14, a monster's MonStats threat
    int life_pct = 100;                       // FUN_00621f20 (a Fetish's think): life x 100 / max life
    const Monster* of = nullptr;              // a monster in the fight (Confuse, Attract: monsters fight monsters)
    bool a3q5_done = false;                   // a player, or its pet, with quest 0x15 bit 0 (the zealots spare it)
    int blessed = 0;                          // the wisps' dance's magic find (stat 0x50) put on it this frame
    d2d::rules::DiabloPick pick;              // what Diablo's target score reads off it (FUN_005e8530)
    int id = -1;                              // a pet's or monster's Monster::id; a player's -2 - 2 x Hero::id, their merc's one below
    void take(const d2d::rules::Taken& taken) {
        blocked = blocked || taken.blocked;
        hits += taken.hit ? 1 : 0;
        if (taken.hit) {
            Blow blow{ taken.damage, 0, taken.elemental > 0 || taken.poison > 0, taken.physical, taken.elemental, taken.poison > 0, taken.frac, taken.raw, fighter.dr_pct, fighter.dr_flat };
            for (std::size_t type = 0; type < blow.parts.size(); ++type)
                if (type != 3) blow.parts[type] = { taken.element_raw[type], taken.element_dealt[type], type < fighter.res.size() ? fighter.res[type] : 0, taken.element_raw[type] > 0 ? taken.element_fine[type] : d2d::rules::StruckBlow::kNoFine };
            blow.mdr = fighter.mdr;
            blow.sum = taken.sum;
            blows.push_back(blow);
        }
        damage += taken.damage; frac += taken.frac; physical += taken.physical; elemental += taken.elemental;
        if (taken.poison > 0 && taken.poison >= poison) { poison = taken.poison; poison_ticks = taken.poison_ticks; }   // the stronger (FUN_0057ac50)
        chill_ticks = std::max(chill_ticks, taken.chill_ticks);
    }
};

// A knocked-back unit's slide (mode KB, Fight::knock_back: a monster, a
// pet or the merc): along to its leap_to over the mode. True while it lasts.
inline bool knock_slide(Monster& monster, std::uint32_t now_ms) {
    const float done = std::clamp(float(now_ms - monster.unit.mode_ms) / float(std::max<std::uint32_t>(monster.mode_until - monster.unit.mode_ms, 1)), 0.f, 1.f);
    monster.unit.x = monster.leap_from_x + (monster.leap_to_x - monster.leap_from_x) * done;
    monster.unit.y = monster.leap_from_y + (monster.leap_to_y - monster.leap_from_y) * done;
    return now_ms < monster.mode_until;
}

// A monster's MonProp hit events on a foe it hit (FUN_005c0c30 over its
// event list +0x90; the melee event, or the missile one, 6, for its
// missile's): each draws on the monster's seed.
// - Knockback (stat 81, func 7, FUN_005bf960): a draw's low 7 bits under
//   the foe's `knock_under`, result flag 8 from where the monster stands;
//   a player slides in Fight::hurt_side, a pet or the merc as a monster does.
// - Crushing Blow (stat 136, func 16, FUN_005bffc0): a draw % 100 under
//   the value; it takes current life / the foe's `crush_divisor`, doubled
//   for a missile, less its damageresist (crushing_blow), after the hit.
inline void monprop_hit(Monster& monster, Foe& foe, bool missile) {
    if (monster.props.knock > 0 && int(monster.seed.next() & 0x7f) < foe.knock_under) foe.knocked_from = std::pair{ monster.unit.x, monster.unit.y };
    if (monster.props.crush > 0 && int(monster.seed.next() % 100) < monster.props.crush)
        foe.crushed.push_back(foe.crush_divisor * (missile ? 2 : 1));
}

// What a Crushing Blow takes off `life` (FUN_005bffc0): life / divisor,
// less its share of the raw damageresist (stat 0x24, 100 at most; none
// when 0 or less), not the 50 % a hit's reduction stops at.
inline std::int64_t crushing_blow(std::int64_t life, int divisor, int damage_resist) {
    const std::int64_t crush = life / std::max(divisor, 1);
    return crush - (damage_resist > 0 ? crush * std::min(damage_resist, 100) / 100 : 0);
}

// A monster unit (a pet, or a monster made a foe by Confuse or Attract) as
// MonProp knock and crush see it. Knock: under 0x20 for a MonStats2 large
// one, 0x80 (always) a small one (FUN_005bf960: MonStats2 +5 & 8 / & 4,
// [0x6ce274] / [0x6ce270]). Crush: Monster::crush_divisor.
inline void monster_foe_props(const GameData& game_data, const Monster& monster, Foe& foe) {
    const auto& type_info = game_data.monsters.types[std::size_t(monster.type)];
    foe.neutral = monster.mode == "NU";
    foe.knock_under = type_info.large ? 0x20 : type_info.small ? 0x80 : 0x40;
    foe.crush_divisor = monster.crush_divisor(game_data);
}

constexpr float kMeleeReach = 1.1f;           // cells between centres

// A missile in flight (a quill rat's spike): straight on at its Missiles.txt
// velocity until it hits the foe, a wall, or runs out of range.
struct Missile {
    const GameData::MissileInfo* info = nullptr;
    float x = 0, y = 0, velocity_x = 0, velocity_y = 0;       // cells, cells/s
    int dir = 0;                              // 0..63 (missile_facing), game.exe's path +0x64
    std::uint32_t born = 0, dies = 0;
    d2d::rules::MonStats src;                 // a monster's: its stats, A2 damage = the missile's
    int owner = -1;                           // a monster's: its Monster::id (unit +0x98, FUN_00552fd0)
    int min = 0, max = 0, attack_rating = 0, level = 1;  // the merc's: damage, attack rating, level; a skill's level
    bool friendly = false;                    // the merc's, the player's: hits monsters, not the player
    int caster = 0;                           // a friendly one's player (Fight::Hero::id), whose credit, leech and skills it carries
    bool by_merc = false;                     // a merc's skill: its weapon damage, attack rating and level, no synergies
    int striker = -1;                         // a summon's (a Shadow's): its Monster::id, whom its hits are struck by
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
    // the do's callback 0x5db6a0) and its attack rating % (stat 19), its element per frame in 256ths when the
    // row's own is replaced (-1: its row's or skill's), the last frame a
    // spawner or burner ran.
    int target = -1, hops = 0, ed_pct = 0, ar_pct = 0, fixed = -1, frame = -1;
    int influence = 0;                        // Blessed Hammer's: the caster's Concentration share of its damage, % (FUN_006461d0)
    int target_id = -1;                       // its target's Foe::id: the foes are rebuilt each frame, pets come and go
    int anim_rate = 0;                        // its animation in 256ths of a frame a tick (unit +0x4c, FUN_00621780), 0 its row's AnimSpeed
    float target_x = 0, target_y = 0;                     // where it was sent (Molten Boulder), a spiral's centre
    float origin_x = 0, origin_y = 0;                     // where it came from (Blade Sentinel goes back and forth)
    int turn = 0;                             // Frozen Orb's direction index (do 15), a spiral's angle
    int last_hit = -1;                        // the monster it last struck (missile data +0x1c, FUN_0064a400), -1 none
    std::uint32_t last_hit_ms = ~0u;          // when
    const GameData::MissileInfo* sub = nullptr;          // a monster's spawner's SubMissile1 (the Countess's firewall maker)
    // Its flight in act subtiles, 16.16 (rules::MissileFlight), from its
    // first step: toward `to` (cells, FUN_0059fa30's target point) when
    // set, else along its velocity, at the velocity's path speed
    // (missile_speed). Charged Bolt's: `bolt` its index (FUN_005c9290).
    // `rooms`: its room, then the near list's (missile_rooms); `follow`:
    // sent outside that room, so the room moves with it.
    std::optional<std::pair<float, float>> to;
    int bolt = -1;
    bool wall_maker = false;                    // FUN_005cd110: a draw first, under 20 its path stays straight
    d2d::rules::MissileFlight flight;
    int sound = 0;                            // its id for its sounds (0: not seen yet): its TravelSound stops when it's gone
    int prog_sound_phase = -1;                // last animation phase that played Missiles.txt ProgSound
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
        if (missile.bolt >= 0 && aimed) {                    // FUN_005c9290 / FUN_005cd110: 77 frames at most, its wiggle
            const int bolt_to_x = to_x == from_x && to_y == from_y ? to_x + 1 : to_x, bolt_to_y = to_x == from_x && to_y == from_y ? to_y + 1 : to_y;
            missile.dies = std::min(missile.dies, missile.born + 77 * 40);
            d2d::rules::Rng seed{ std::uint32_t(bolt_to_x + missile.bolt) };
            if (!missile.wall_maker || seed.next() % 100 >= 20)
                missile.flight.points = d2d::rules::wiggle_points(from_x, from_y, bolt_to_x, bolt_to_y, int(missile.dies - missile.born) / 40, seed);
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
    const int missile_size = missile.info ? missile.info->size : 1;
    return std::ranges::any_of(missile.flight.crossed, [&](const auto& spot) { return d2d::rules::missile_touches(spot.first, spot.second, foe_x, foe_y, size, missile_size); });
}

// A standing missile (no velocity) strikes from its own subtile every frame
// (FUN_005ae1f0, nothing crossed): a Size 2 shard takes a size 1 unit on its
// own and the 4 neighbouring subtiles, a size 2 unit on 13, size 3 on 21; a
// Size 1 Fire Wall flame 1 / 5 / 9. A row with no Size 1..3 keeps the half
// cell it always had here.
inline bool standing_struck(const Missile& missile, float x, float y, int size) {
    const int missile_size = missile.info ? missile.info->size : 0;
    if (missile_size < 1 || missile_size > 3) return std::hypot(x - missile.x, y - missile.y) <= 0.5f;
    const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
    return d2d::rules::missile_touches(subtile(missile.x), subtile(missile.y), subtile(x), subtile(y), size, missile_size);
}

// A missile's facing for a heading with no point to aim at (a velocity, a
// ring's or a burst's spoke): the 64-step direction of its angle, scaled
// to 255 subtiles along (direction64's 32-bit sums wrap past 258).
inline int missile_heading(float dx, float dy) {
    const float longer = std::max(std::abs(dx), std::abs(dy));
    if (longer <= 0) return 0;
    return d2d::rules::direction64(0, 0, int(std::lround(dx * 255 / longer)), int(std::lround(dy * 255 / longer)));
}
// A missile's facing from (x, y) at the point (dx, dy) cells on: game.exe's
// 64-step direction between their subtiles (rules::direction64, as
// FUN_0059fa30 aims at the target; 0 straight down, clockwise on screen),
// floored as missile_fly flies it. The draw takes its frame from it
// (missile_direction). A step of a cell or less is a unit heading (a
// spoke's cos / sin), not a point: its angle.
inline int missile_facing(float x, float y, float dx, float dy) {
    if (std::hypot(dx, dy) <= 1.01f) return missile_heading(dx, dy);
    const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
    return d2d::rules::direction64(subtile(x), subtile(y), subtile(x + dx), subtile(y + dy));
}

// `element` onto a missile's stats: poison per frame / frames in MonStats::poison_*,
// any other type as an A2 El row in whole points (shared by missile_blow and the
// map's trap missiles).
inline void put_missile_element(d2d::rules::MonStats& stats, const d2d::rules::MissileDamage& element) {
    if (element.etype == 3) {
        stats.poison_min = element.elo; stats.poison_max = element.ehi; stats.poison_len = element.elen;
    } else if (element.etype >= 0)
        stats.elements[0] = { element.etype, 100, element.elo >> 8, element.ehi >> 8, element.elen, "A2" };
}
// A monster missile's blow (andariel_missile): its row's physical in
// 256ths, `element` (its row's or a carried skill's at the level); it always
// hits. Poison goes in MonStats::poison_*: per frame in 256ths for ELen frames,
// the record's +0x34 / +0x38 / +0x3c, which monster_blow applies as it is (an
// `elements` entry of type 3 is the MonStats El convention, roll x 10 a frame
// for 2 x dur frames, up to 62x as much).
inline d2d::rules::MonStats missile_blow(const GameData::MissileInfo& info, const d2d::rules::MissileDamage& element, int monster_level) {
    d2d::rules::MonStats stats;
    stats.level = monster_level;
    stats.no_hit_roll = !info.to_hit;                                // a ToHit row rolls its owner's attack rating (the caller's)
    stats.a2_min = info.min >> 8; stats.a2_max = std::max(info.max >> 8, stats.a2_min);
    put_missile_element(stats, element);
    return stats;
}
// What a monster's MissA / MissS1 / MissC shot carries (Ai::tick, FUN_0059f900 ->
// FUN_0064b860): the physical part in 256ths, as FUN_0064af20 / FUN_0064aff0 make
// it: the owner's damage x SrcDamage / 128 (the stat is points, so x 256 first,
// truncated; none for a cast, MissC, or a row with no SrcDamage), plus the row's
// MinDamage / MaxDamage and its MinLevDam / MaxLevDam level brackets
// (FUN_00644b70, `lvl` the missile's level), shifted left by HitShift.
struct MonsterShotPhys { int low = 0, high = 0; };
inline MonsterShotPhys monster_shot_phys(const GameData::MissileInfo& info, int src_min, int src_max, bool cast, int lvl) {
    const int shift = info.hitshift & 31;
    const auto part = [&](int src, int row, const std::array<int, 5>& lev) {
        const int owner = cast || info.src_damage <= 0 ? 0 : int(std::int64_t(src) * 256 * info.src_damage / 128);
        return owner + ((row + d2d::rules::level_bonus(lev, lvl)) << shift);
    };
    return { part(src_min, info.min, info.min_lev), part(src_max, info.max, info.max_lev) };
}
// The owner's stats as the missile carries them (what monster_blow rolls on
// impact): the physical as a2_min / a2_max whole points, the owner's own
// El rows of the shot's mode as A2's, and the row's own element (FUN_0064b100 /
// 0064b1d0 / 0064b2a0 at the missile's level: Sand Maggot poison, the pots) in a
// free slot; poison goes into MonStats::poison_min / poison_max / poison_len
// instead (per frame in 256ths for ELen frames, what the record's +0x34 / +0x38 /
// +0x3c hold and what monster_blow applies on every hit); the owner's own poison is
// dropped on a SrcDamage-0 row and summed in otherwise (the rule below).
inline d2d::rules::MonStats monster_shot_stats(const GameData::MissileInfo& info, d2d::rules::MonStats stats, bool first, bool cast, bool s1_shot, int lvl) {
    const int src_min = first ? stats.a1_min : s1_shot ? stats.s1_min : stats.a2_min;
    const int src_max = first ? stats.a1_max : s1_shot ? stats.s1_max : stats.a2_max;
    const auto phys = monster_shot_phys(info, src_min, src_max, cast, lvl);
    stats.a2_min = phys.low >> 8; stats.a2_max = phys.high >> 8;
    if (s1_shot) stats.to_hit = stats.to_hit_s1;                    // S1TH (monster_blow rolls to_hit)
    stats.no_hit_roll = !info.to_hit;
    if (first || cast || s1_shot)                                   // and its elements (a Skeleton Mage's El1 A1): the blow rolls A2's
        for (auto& element : stats.elements) element.mode = element.mode == (cast ? "SC" : s1_shot ? "S1" : "A1") ? "A2" : "";
    const auto row = d2d::rules::row_damage(info.etype, info.emin, info.emax, info.emin_lev, info.emax_lev, info.hitshift, info.elen, info.elen_lev, lvl);
    // The owner's poison (game.exe, V read: FUN_0064b860 / FUN_0064a930 / FUN_0064aa60; V run: the
    // re-verifier's vm_game2 oracle, goospit1 with owner poison 40 / 40 / 300, whole OP set): added
    // only when SrcDamage != 0 (+0x12d; all 32 poison rows of Missiles.txt have 0, so the owner's
    // is dropped); then the rate pair (owner + row) is scaled by SrcDamage / 128 (truncated toward
    // 0) and the LENGTH (owner + row) is summed, unscaled. U: whether a cast (MissC) shot takes the same path.
    // SrcDamage is read as an unsigned byte there (V read: movzx at 0x64b8ed), so a -1 row is 255; the
    // other branch (a missile with a Skill, 0x64b9da) maps 0xff to 0, which this plain-shot path does not
    // take. No monster Miss row has -1 (only player skill missiles do), so it is not reachable today.
    const int src_damage = info.src_damage & 0xff;
    const bool row_poison = row.etype == 3 && (row.ehi > 0 || row.elo > 0);
    if (src_damage == 0) stats.poison_min = stats.poison_max = stats.poison_len = 0;
    if (row_poison) { stats.poison_min += row.elo; stats.poison_max += row.ehi; stats.poison_len += row.elen; }
    if (src_damage != 0) {
        stats.poison_min = int(std::int64_t(stats.poison_min) * src_damage / 128);
        stats.poison_max = int(std::int64_t(stats.poison_max) * src_damage / 128);
    }
    if (!row_poison && row.etype >= 0 && (row.ehi > 0 || row.elo > 0))
        if (const auto free_slot = std::ranges::find_if(stats.elements, [](const auto& element) { return element.type < 0 || element.mode.empty(); }); free_slot != stats.elements.end())
            *free_slot = { row.etype, 100, row.elo >> 8, row.ehi >> 8, row.elen, "A2" };
    return stats;
}
// `info` from (x, y) cells along (dx, dy), as `from`'s (its owner, level,
// monster level), with its own row's blow.
inline Missile missile_from(const Missile& from, const GameData::MissileInfo& info, float x, float y, float dx, float dy, std::uint32_t now_ms) {
    const auto element = d2d::rules::row_damage(info.etype, info.emin, info.emax, info.emin_lev, info.emax_lev, info.hitshift, info.elen, info.elen_lev, from.level);
    Missile out{ &info, x, y, dx, dy, missile_facing(x, y, dx, dy), now_ms,
                 now_ms + std::uint32_t(std::max(info.range + info.lev_range * from.level, 1)) * 40, missile_blow(info, element, from.src.level) };
    out.level = from.level; out.owner = from.owner;
    return out;
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
template <class Reach, class HitsMonster, class OwnerAlive>
void missiles_update(const Level& level, std::vector<Missile>& ms_, std::span<Foe> foes, d2d::rules::Rng& rng,
                     std::uint32_t now_ms, Reach&& reach, HitsMonster&& hits_monster, OwnerAlive&& owner_alive) {
    std::vector<Missile> laid;
    const int frame = int(now_ms / 40);
    const auto spent = [&](Missile& missile) {
        missile.flight.crossed.clear();
        const bool moving = missile.velocity_x != 0 || missile.velocity_y != 0;
        if (moving && !missile_fly(level, missile, reach)) return true;
        if (now_ms >= missile.dies || (!moving && level.blocked(missile.x, missile.y, 0x04))) return true;
        // The client's do 55 (FUN_004d8260, "ancient death center"): every
        // third frame of its age an "ancient death cloud" (`sub`, flags 0x21)
        // from a subtile -1..0 off it each way, sent at that start's -10..9
        // off (x the first draw, y the second).
        // ponytail: the update's rng, not the missile's seed.
        if (missile.info && missile.info->clt_do == 55 && missile.sub && missile.frame != frame && (int(now_ms - missile.born) / 40) % 3 == 0) {
            missile.frame = frame;
            const int off_x = int(rng.next() % 20) - 10, off_y = int(rng.next() % 20) - 10;
            const float from_x = std::floor(missile.x * 5) + float(rng.next() & 1) - 1, from_y = std::floor(missile.y * 5) + float(rng.next() & 1) - 1;
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
            }
        }
        // A monster's fire wall: the maker (do 6) lays SubMissile1 where it
        // is each frame; the fire (do 5) burns a foe within half a cell each
        // frame, once a frame for the cast (they share `struck`), its row's
        // element in 256ths less resistance and magic damage reduction.
        // ponytail: the 256ths a burn leaves over round up by chance, not
        // kept on the foe's life; DamageRate unread.
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
        // that delay.
        // ponytail: the update's rng, not the missile's seed.
        if (missile.info && missile.info->srv_do == 34) {
            const int age = int(now_ms - missile.born) / 40;
            if (missile.frame == frame || age < missile.info->param1) return false;
            missile.frame = frame;
            const std::array delays{ missile.info->param2, missile.info->param3 };
            const int ways = !missile.info->subs[0] || delays[0] < 1 ? 0 : !missile.info->subs[1] || delays[1] < 1 ? 1 : 2;
            if (ways == 0) return true;
            if (const int pick = int(rng(std::uint32_t(ways))); age % delays[std::size_t(pick)] == 0)
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
            for (auto& foe : foes) {
                const int key = frame * 16 + int(&foe - foes.data());
                if (!foe.alive || std::hypot(foe.x - missile.x, foe.y - missile.y) > 0.5f || std::ranges::contains(*missile.struck, key)) continue;
                missile.struck->push_back(key);
                const auto& row = *missile.row;
                const int burn = std::max(d2d::rules::resisted(rng.range(row.elo, row.ehi), foe.fighter.res[std::size_t(std::clamp(row.etype, 0, 2))]) - foe.fighter.mdr * 256, 0);
                const int dealt = (burn >> 8) + (int(rng(256)) < (burn & 255) ? 1 : 0);
                foe.damage += dealt; foe.elemental += dealt;
                foe.blows.push_back({ dealt, 0, true, 0, dealt });   // its own record, for its get-hit
            }
            return false;
        }
        for (auto& foe : foes) {
            if (!foe.alive || (moving ? !missile_struck(level, missile, foe.x, foe.y, foe.size) : std::hypot(foe.x - missile.x, foe.y - missile.y) > 0.4f)) continue;
            const int key = -100 - int(&foe - foes.data());         // a ring's missiles strike each foe once
            if (std::ranges::contains(*missile.struck, key)) continue;
            missile.struck->push_back(key);
            const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, foe.motion(), missile.src, true, rng, true);
            foe.take(taken);
            if (taken.hit && missile.owner >= 0) foe.shot_by.push_back(missile.owner);
            if (taken.hit && missile.info) foe.struck_by(missile.info->hit_class);
            if (missile.info && missile.info->hit_func == 19 && missile.skill >= 0) foe.marked = { missile.skill, missile.level };   // FUN_005ab110
            if (missile.info && missile.info->return_fire && missile.owner >= 0) foe.fired_by.push_back(missile.owner);
            return true;
        }
        return false;
    };
    // Gone into a unit or a wall, its ExplosionMissile plays there; run
    // out, only when AlwaysExplode (FUN_004d30c0 -> FUN_004d2d70).
    // ponytail: one that flies on through units (CollideKill 0) doesn't
    // burst at each; RandStart isn't applied; a guided one losing its
    // owner bursts as a hit.
    // At its end Baal Taunt's lightning control (hit 58, FUN_005acdf0) sends
    // HitSubMissile1 at a random subtile within sHitPar1 (at least 1) of it;
    // the poison one (hit 59, FUN_005acf20 -> FUN_005a9370) sends it the 8
    // ways of 0x6e2510 / 0x6e24d0's even entries at Param1 << 7, and every
    // sHitPar1'th odd one at Param2 << 7 (FUN_0059fa30 flags 0x14: x 75 / 100).
    // ponytail: the update's rng, not the missile's seed.
    const auto hit_spawn = [&](const Missile& missile) {
        const auto* sub = missile.info ? missile.info->hit_sub_info : nullptr;
        if (!sub || (missile.info->hit_func != 58 && missile.info->hit_func != 59)) return;
        const int spread = std::max(missile.info->hit_par1, 1);
        if (missile.info->hit_func == 58) {
            const float to_x = float(int(std::floor(missile.x * 5)) - spread + int(rng(std::uint32_t(2 * spread + 1)))) + 0.5f;
            const float to_y = float(int(std::floor(missile.y * 5)) - spread + int(rng(std::uint32_t(2 * spread + 1)))) + 0.5f;
            const float dx = to_x / 5 - missile.x, dy = to_y / 5 - missile.y, span = std::max(std::hypot(dx, dy), 0.01f), speed = missile_speed(*sub, missile.level);
            laid.push_back(missile_from(missile, *sub, missile.x, missile.y, dx / span * speed, dy / span * speed, now_ms));
            laid.back().to = { missile.x + dx, missile.y + dy };
            return;
        }
        static constexpr std::array<int, 16> kWayX{ 0, 1, 2, 2, 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -2, -1 };
        static constexpr std::array<int, 16> kWayY{ 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -2, -1, 0, 1, 2, 2 };
        const auto ring = [&](int first, int step, int velocity) {
            for (int way = first; way < 16; way += step) {
                const float dx = float(kWayX[std::size_t(way)]), dy = float(kWayY[std::size_t(way)]), span = std::hypot(dx, dy);
                const float speed = missile_speed((velocity << 7) * 75 / 100);   // flags 0x14: the velocity as is
                laid.push_back(missile_from(missile, *sub, missile.x, missile.y, dx / span * speed, dy / span * speed, now_ms));
            }
        };
        ring(0, 2, sub->param1);
        ring(1, spread, sub->param2);
    };
    std::erase_if(ms_, [&](Missile& missile) {
        if (!spent(missile)) return false;
        if (now_ms >= missile.dies) hit_spawn(missile);
        if (const auto* explosion = missile.info ? missile.info->explosion : nullptr; explosion && !missile.visual_only && (now_ms < missile.dies || missile.info->always_explode)) {
            Missile burst{ explosion, missile.x, missile.y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(explosion->range, 1)) * 40, {} };
            burst.visual_only = true;
            laid.push_back(std::move(burst));
        }
        return true;
    });
    std::ranges::move(laid, std::back_inserter(ms_));
}

// A random unique's name (the client's FUN_004ac870): on {name seed, 666},
// a suffix then a prefix into string 0x6b9 ("%0 %1"); then rand(100) < 50
// builds it again, appellation, suffix, prefix, into 0x6ba ("%0 %1 %2").
std::string unique_name(const GameData& game_data, int name_seed);

// A champion / unique / superunique / minion (rules/uniques.hpp,
// FUN_005a2120): a higher level, more life, damage and to-hit,
// resistances, speed, an element; its name. At full life. `players`: in
// the game (rules::monster_stats' bonus).
void make_boss(const GameData& game_data, Monster& monster, d2d::rules::Boss kind, const std::vector<int>& mods, int super, int name_seed,
               int difficulty, d2d::rules::Rng& rng, int players = 1);
// MonUMod poison hit's init (FUN_005a1e00): its own poison stats.
void poison_hit(const GameData& game_data, Monster& monster, int difficulty, bool unique);
// Whether a skill may put the monster on a special think (FUN_0056e2f0 ->
// FUN_005dd480; FUN_00623470, FUN_005b0da0): not state 0x36, MonStats2 mWL, not MonStats
// boss, SwitchAI, targetable (unit +0xc4 & 4) or dead, not a unique or
// superunique (monster data +0x16 & 0xa), not an imp (BaseId 0x1ec) riding
// (state 0x8f). State 0x36 (uninterruptable, FUN_005544b0) comes on with a
// skill's start: Whirlwind (FUN_005d8f50), Leap (FUN_005d9d50), Leap Attack
// (FUN_005da540), Nest (FUN_005cbd10) and Jump (FUN_005cb730, no monster's),
// each clearing it at its end (Monster::uninterruptable).
bool swaps_ai(const GameData& game_data, const Monster& monster);
// The Cursed umod's hit hook (FUN_005a2530), on a melee or missile hit that
// lands: a unique's or superunique's 3 in 4 on its own seed (a draw's low 2
// bits), Amplify Damage (skill 66) at level mlvl / 5 + 1 on every foe within
// its aurarangecalc (1..40 subtiles) of the one struck (FUN_0056dbc0 round
// FUN_0056d2c0's target; FUN_005a23d0, enemies only).
void cursed_hit(const GameData& game_data, Monster& monster, std::span<Foe> foes, const Foe& struck);
// A monster's skill missile toward (dx, dy) cells (FUN_0059fa30, ai.cpp).
void andariel_missile(const GameData& game_data, const Monster& monster, const std::string& name, float dx, float dy, std::uint32_t now_ms,
                      std::vector<Missile>& missiles, int lvl, const d2d::rules::Skill* carried = nullptr);
// A monster turned into MonStats row `type` in place (FUN_00574370: the same unit id,
// its group and spot, a new init for the class, FUN_00574250).
void morph_monster(const GameData& game_data, Monster& unit, int type, d2d::rules::Rng& rng, int players);
// Baal's Vile Effigy (FUN_005fc860): baalclone +- 12 subtiles (two draws of his seed) about
// (at_x, at_y), a third of his life, no regen (stat 0x4a 0); none without the row.
std::optional<Monster> vile_effigy(const GameData& game_data, const Level& level, Monster& baal, int at_x, int at_y, d2d::rules::Rng& rng, int players);
// A monster's missile made at its target, (dx, dy) cells away (FUN_0056ede0 -> FUN_0059fa30
// flag 1: the record's +0x14 / +0x18), heading a subtile on at (+1, +1).
void missile_at_target(const GameData& game_data, const Monster& monster, const std::string& name, float dx, float dy, std::uint32_t now_ms,
                       std::vector<Missile>& missiles, int lvl, const d2d::rules::Skill* carried = nullptr);
// A monster's skill strike (Whirlwind, Leap Attack): its A1 swing at the skill's damage, to-hit and element (ai.cpp).
d2d::rules::MonStats skill_strike(const GameData& game_data, const Monster& monster, const d2d::rules::Skill& skill, int lvl);
// Fire Hit's and Jab's hit (FUN_005cae40, FUN_005db2d0): the swing's S1 damage,
// to-hit and S1 elements as A1's, plus the skill's to-hit as points (FUN_006449f0
// into FUN_0057ec10, as skill_strike).
inline void s1_strike(d2d::rules::MonStats& stats, const d2d::rules::SkillTables& skill_tables, const d2d::rules::Skill& skill, int lvl) {
    stats.a1_min = stats.s1_min; stats.a1_max = stats.s1_max;
    stats.to_hit = stats.to_hit_s1 + d2d::rules::skill_tohit(skill_tables, skill, {}, lvl);
    for (auto& element : stats.elements) element.mode = element.mode == "S1" ? "A1" : "";
}
// FUN_005b0e00(ctrl, special): rules::ai_toggle on the monster.
void ai_toggle(const GameData& game_data, Monster& monster, int special);
// A timed state on a monster in its slot (FUN_0056e970) and a state's end (fight/).
bool put_state(const GameData& game_data, Monster& monster, Monster::SkillState& slot, Monster::SkillState next, std::uint32_t now_ms);
void end_state(const GameData& game_data, Monster& monster, Monster::SkillState& slot);

// A plain monster of MonStats row `type` at (x, y) cells: its components
// and stats rolled (a boss's by make_boss instead: `stats` false), for
// `players` in the game.
// Levels.txt MonLvl1Ex / MonLvl2Ex / MonLvl3Ex of the level `level_id` for the
// difficulty (what FUN_0061dca0 returns in an expansion game; 1 for an id off
// the table or under 1, as it does); -1 when the level isn't known.
[[nodiscard]] inline int area_monster_level(const GameData& game_data, int level_id, int difficulty) {
    if (level_id < 0) return -1;
    if (level_id == 0 || std::size_t(level_id) >= game_data.area_level.size()) return 1;
    return game_data.area_level[std::size_t(level_id)][std::size_t(std::clamp(difficulty, 0, 2))];
}

Monster make_monster(const GameData& game_data, int type, float x, float y, d2d::rules::Rng& rng, int difficulty, bool stats = true, int players = 1, int level_id = -1);
// The players among the foes (not a pet, not a monster): the game's.
[[nodiscard]] int players_in(std::span<const Foe> foes);

// Spawns as monsters, each looking as its unit seed picks from `region`
// (rules::monster_look).
std::vector<Monster> spawn_monsters(const GameData& game_data, std::span<const d2d::rules::Spawn> spawns, const d2d::rules::Region& region,
                                    d2d::rules::Rng& rng, int difficulty, int players = 1, int level_id = -1);

void set_mode(const GameData& game_data, Monster& monster, std::string_view mode, std::uint32_t now_ms);

// Damage to a monster: it dies (DT, then its corpse, DD), or recoils (GH)
// by rules::get_hit (FUN_0057cb00); either way it notices.
// True when this killed it.
// `hit_class` and `marked` (an element or a doubled hit) are the blow's, for
// its get-hit (rules::get_hit); a source without them (thorns, a blast) is class 0.
bool hurt(const GameData& game_data, Monster& monster, int damage, std::uint32_t now_ms, int fraction = 0, int hit_class = 0, bool marked = false);
// Life back (FUN_0057a980): `points` whole points, capped at max life; a heal
// the cap cuts leaves it at full life, no 256ths taken off.
void heal(Monster& monster, int points);
// Bone Armor's absorb on a monster (event 22, FUN_005c6370): the pool takes
// the hit's physical; at 0 the state goes. All in 256ths: the damage left.
int soak(Monster& monster, int physical, int damage);

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
// An AI whose think isn't traced (rules::traced_ai) gets one melee think
// (MonStats aip1..8 unread); distances and timings by eye; chasing goes
// straight at the player, sliding to a stop at walls. No monster a level
// places has one (every level, map seeds 1..4 and 7): it's a safety net;
// town NPCs, pets and quest NPCs think elsewhere.
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
                    std::vector<Monster>* born = nullptr, AreaSeen* seen = nullptr, const OpenDoor* open_door = nullptr,
                    Spawning::LevelState* rooms_in_play = nullptr);

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
