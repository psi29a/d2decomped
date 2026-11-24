// SPDX-License-Identifier: GPL-3.0-or-later
// The units the AI moves: a unit's place (Fine), a walker (UnitState), the
// collision crowd, a monster, a foe and a missile. Their functions are in
// ai.hpp: a header that only holds these needn't rebuild when one is added.
#pragma once

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

// A monster's velocitypercent (stat 0x43) as its stat initialiser sets it (FUN_00573cb0, SetStat(0x43, 0x4b) at 0x573f16).
inline constexpr int kMonsterVelocityPct = 75;

// A unit's coordinate as game.exe keeps it: 16.16 subtiles (path +0 / +4),
// which a walk steps (rules::walk_frame). It reads as cells, kept on its
// subtile (a float holds 24 bits of the 27); cells written in go to the
// nearest 1/65536 subtile, and centre() is a subtile's centre exactly.
struct Fine {
    std::uint32_t raw = 0;
    Fine() = default;
    explicit Fine(float cells) : raw(std::uint32_t(std::llround(double(cells) * 5 * 65536))) {}
    Fine& operator=(float cells) { return *this = Fine(cells); }
    [[nodiscard]] static Fine centre(int subtile) {
        Fine fine;
        fine.raw = (std::uint32_t(subtile) << 16) + 0x8000;
        return fine;
    }
    [[nodiscard]] int subtile() const { return std::int32_t(raw) >> 16; }
    operator float() const {
        float cells = float(std::int32_t(raw)) / (65536.f * 5);
        while (int(std::floor(cells * 5)) > subtile()) cells = std::nextafter(cells, cells - 1);
        while (int(std::floor(cells * 5)) < subtile()) cells = std::nextafter(cells, cells + 1);
        return cells;
    }
    // Moved by cells in 16.16, so the place keeps the bits a float can't hold.
    Fine& operator+=(float cells) { raw += Fine(cells).raw; return *this; }
    Fine& operator-=(float cells) { raw -= Fine(cells).raw; return *this; }
};

// Where each world NPC is right now (index-aligned with Level::npcs):
// patrolling NPCs walk their DS1 path, pausing at each point.
struct UnitState {
    Fine x, y;                        // cells, held in 16.16 subtiles (Fine)
    UnitShape shape;                  // its collision (gamedata.hpp); a player's by default
    int dir = 0;                      // its facing drawn, 16 directions in DCC order
    d2d::rules::Facing facing;        // a walk's facing in 64 (path +0x64 / +0x65 / +0x66)
    bool turns_on = false;            // a monster's: a turn still going when its walk ends goes on a step a frame (turn_frame)
    bool walking = false;
    bool greets = false;              // a town NPC's think greeted the player just now (unit sound 0x12; World plays it)
    std::size_t next = 0;             // path point being walked to
    std::uint32_t wait_until = 0;     // ms; idle until then
    bool filed = false;               // a town NPC's first think is filed (npc_patrol: 2 frames after it's met)
    std::uint32_t mode_ms = 0;        // when the current mode (walk/idle) started
    std::uint32_t stuck_since = 0;    // ms the merc last got blocked, 0 = moving
    float goal_x = 0, goal_y = 0;     // where the route was planned to
    // Its place in its room's unit list (room +0x74, next +0xe8): FUN_0064c2c0 puts a
    // unit at the head as it's made or comes into a room, so each room's list
    // runs by room_stamp, highest first (Fight::restamp; rules::room_order).
    int room_seen = -2;
    const Level* room_level = nullptr;        // the level room_seen is a room of: crossing to another restamps
    std::uint32_t room_stamp = 0;
    std::uint32_t made_stamp = 0;             // its first room_stamp: still equal, it hasn't left the room it was made in
    int budget = 0;                   // a monster walk's re-path budget, points (path +0x94)
    int path_points = 0;              // points the route had when made
    std::uint16_t path_word = 0;      // path +0x54: the door bit (0x800) its steps met since its path was made (path_frame)
    bool to_unit = false;             // walking up to a unit (near 1, the wall pather's 0x28 steps)
    bool hidden = false;              // a quest-gated NPC who isn't here (yet)
    bool alert = false;               // has something new to say on a quest: the balloon over its head
    std::string_view mode;            // an object's mode now, "" = its start mode (Npc::mode)
    std::uint16_t says = 0;           // string id over its head (a shrine's message), 0 none
    std::vector<std::pair<float, float>> path;   // the route being followed, cells
    d2d::rules::NpcBrain brain;       // a town NPC's AI commands (town_npcs.hpp)
};

// The units (the player, the merc, NPCs, monsters near) as game.exe's
// collision sees them: each stamps its footprint bit (UnitShape: 0x1000,
// or 0x2000 for the merc and the cow) on its subtile, or the plus for
// pattern 2 / 4; a mover is blocked where its own test shape (the plus, or
// the 3x3 box) meets a footprint its mask holds. game.exe lifts the
// mover's own stamp first (FUN_00649970). So a player (0x1c09) walks
// through its merc; monsters (0x3c01) don't. Own bits (0x80 player, 0x100
// monster) are in no walker's mask, so aren't kept.
struct Crowd {
    std::vector<const UnitState*> units;
    // Marks that count while their flag is on: a monster's leap landing (Monster::landing_mark while
    // `leaping`), so one taken mid-frame holds for the monsters that think after it.
    std::vector<std::pair<const UnitState*, const bool*>> held;
    [[nodiscard]] bool at(float x, float y, const UnitState* self) const;
    // As something of shape `mover` would stand there, `self` not counted.
    [[nodiscard]] bool at(float x, float y, const UnitShape& mover, const UnitState* self) const;
};

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
    // Its mods in the fight (uniques.hpp): event 4 since the last tick (it
    // sets off Lightning Enchanted's bolts, at most every 10 frames), when
    // its death effect is due, its mana burn per hit.
    bool struck_event = false;                // FUN_0057cee0's event 4 out of GH: a landed hit with no get-hit (Fight::land), a burner's soft hit (Fight::burn)
    std::uint32_t bolts_at = 0, fx_at = 0;
    std::uint32_t bolts_due = 0;              // Lightning Enchanted's event 7, queued going into GH (0 none)
    std::uint32_t killself_at = 0;            // MonUMod 21 (killself)'s event 7 (FUN_005a3aa0), 0 none
    int spc_to_hit = 0;                       // MonUMod 14 (spcdamage)'s stat 0x13 (rules::spcdamage), 0 not one
    std::uint32_t rise_at = 0;                // MonUMod 34 (ai_after_death)'s event 7 (FUN_005a3910), 0 none
    bool rising = false;                      // its S1 rise (skill 0x125): unit flags 0xe off, not targetable
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
        // MonUMod 14's hook 0 (FUN_005a3b50) runs after the swing's stats and puts stat 0x13 at level + 50 (at most 90).
        if (spc_to_hit > 0) swung.to_hit = swung.to_hit2 = swung.to_hit_s1 = spc_to_hit;
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
    int name_seed = 0;                        // a boss's name seed (make_boss: the 0xac packet's, unit data +0x14)
    std::array<int, 6> boss_res{};            // what its mods add to its resistances (ResDm..ResPo)
    std::array<int, 6> gear_res{};            // what its MonEquip gear adds to them (stats 36, 37, 39, 41, 43, 45: wear_gear_stats)
    std::array<int, 6> res_cut{};             // what its curse, cry and the auras on it add to them (Fight::target_of): the hover's immunity lines
    int boss_speed = 0;                       // + speed % (fast, champion)
    Npc npc;
    UnitState unit;
    d2d::rules::MonStats stats;
    int hit_points = 1;
    // Co-op clients get hit_points, not life_frac (replication.hpp): still
    // finer than game.exe's own client, which only ever sees life / 128 of
    // the maximum (net-packets.md "Monster life"; 0x0c / 0x0d / 0xab). The
    // server is authoritative.
    int max_life_frac = 0;                    // the same deficit from stats.hit_points for a boss's max life
    int life_frac = 0;                        // 256ths of a point already taken off hit_points (life = hit_points x 256 - life_frac)
    int leader = -1;                          // index of its group's leader
    bool leader_lost = false;                 // a minion's AI control +0xc cleared (rules::leader_follow): it no longer walks back
    // The Monster::id it's attached to as a minion (AI control +0x28's owner, FUN_0058f0d0): a Bone
    // Prison's later bones the first's (FUN_005b31b0); -1 none. `owner_born`: that owner's index in the
    // frame's born list, made the same frame (World ids come as they're added).
    int party_owner = -1, owner_born = -1;
    int difficulty = 0;
    float home_x = 0, home_y = 0;             // where it spawned: wandering stays near
    std::string_view mode = "NU";             // animation mode token
    std::string_view last_mode = "NU";        // as of the last sound check
    bool sounded = false;                     // its sounds have started (its Init played, FUN_004cc380)
    bool fleeing = false, flee_sound = false;  // Fallen's flee event while a nearby monster dies
    bool first_sight = false, taunt_sound = false;   // AI flag 0x10: its first-sight taunt (unit event 0x10) is sent
    bool help_sound = false;                  // a caged prisoner's cry (RescueQuest::kHelpMe)
    int skill_sound = -1;                     // the skill it just started (its start sounds, FUN_004c6140: Fight::monster_sounds), -1 none
    // In a game target list (+0xd0 not 0xb): an evil monster's search takes a
    // good one only from list 8. A prisoner (FUN_00588600's) is in none till its
    // think finds its door broken (FUN_005ee3c0 -> FUN_005b1990(8)).
    bool listed = true;
    int seq_sound = 0, seq_sounds = 0;        // a Jab sequence's event-3 rows passed and its dosound b: played or dropped each frame (Fight::monster_sounds)
    std::uint32_t sound_ms = 0, neutral_ms = 0, step_ms = 0;   // its last sound, Neutral, footstep (unit +0x7c / +0x80 / +0x84)
    std::uint32_t sound_tick = 0;             // the tick its Neutral last rolled
    int hit_class = 0;                        // the last hit's (unit +0xb0): the impact its GH / KB / DT plays
    int struck_by = -1;                       // the Foe::id that last struck at it, -1 none (unit +0xa0, FUN_00621d50 from FUN_0057c6c0)
    std::uint32_t mode_until = 0;             // ms: an attack / get-hit / death ends
    bool lifeless = false;                    // its life rolled 0 (FUN_00573cb0): held at one point, any landed hit kills it
    // Its think event (unit event 2): due at next_act, pending while that's
    // after thought_ms, when its think last ran. One due runs mid-mode too.
    std::uint32_t next_act = 0, thought_ms = 0;
    [[nodiscard]] bool think_pending() const { return next_act > thought_ms; }
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
    // Its path type (+0x3c): 0 the toward pather's (0xd), or 7 (a player's,
    // rules::player_path) when a think set that for its next move
    // (FUN_005de190 -> FUN_005a6260: mode ctx +0x18, 1 given as 7;
    // FUN_005a63f0 takes and clears it as the move starts); a type 7 path
    // found empty goes 0xf, the wall pather (FUN_005a6290).
    int path_type = 0, next_path_type = 0;
    // Its path's steps (+0x91, FUN_00648e70, at most 0x4d): every pather's
    // `steps`, a re-path's too. A walk's start sets 5 (FUN_005a63f0 ->
    // FUN_005a6290); a charge's run 0x14 (FUN_005cf6b0). Other moves keep it.
    int path_steps = 5;
    int chase = -1;
    bool corpse_used = false;                 // raised (Raise Skeleton): its corpse is gone
    // Unit flags a skill's young carry (+0xc4): 0x4000000 no experience for
    // its kill (FUN_005a4ef0 skips FUN_0057e990), 0x20000 no drop
    // (FUN_005a6830 returns at once).
    bool no_exp = false, no_drop = false;
    int cage = -1;                            // a5q2's prisoner: its cage's slot (Spawn::cage)
    bool preset = false;                      // made by its room's preset units (Spawn::preset)
    // Damage over time (life a millisecond, until when) and a chill.
    double poison_rate = 0, bleed_rate = 0, dot_acc = 0;
    std::uint32_t poison_until = 0, bleed_until = 0, chill_until = 0;
    bool shatter = false;                     // state 107 (FUN_0057af80's roll on a chill)
    std::uint32_t no_heal_until = 0;          // ms: state 0x34 (preventheal, FUN_0057d810)
    std::uint32_t stunned_until = 0;          // ms: a stun's end (state 21)
    std::uint32_t frozen_until = 0;           // ms: a freeze's end (state 1)
    std::uint32_t stun_until = 0;             // ms: stunned or frozen, it stands (the later end)
    std::uint32_t just_hit_until = 0;         // ms: state 0x56 justhit (FUN_005adb00), NextHit missiles pass it by
    // Aura Enchanted (uniques.hpp boss_aura): its aura, level, next pulse;
    // and the damage / to-hit % it gets from bosses' auras this frame.
    int aura = 0, aura_lvl = 0;
    std::vector<std::string_view> host_states;   // a joined game's: the States.txt names the host has on it (0xa8 / 0xaa)
    std::uint8_t looks = 0;                      // its thinks' states: 0x5a "blue" (bit 1), 0x5b "red" (bit 2)
    d2d::rules::OneShots one_shots;              // its one-shot overlays: its thinks' (FUN_00621e40), its casts'
    std::uint32_t aura_next = 0;
    int aura_dmg = 0, aura_th = 0, aura_rate = 0;   // a boss's aura over it: damagepercent, item_tohit_percent, attackrate (Fanaticism)
    // Skills' states on it (their auratargetstate): one curse state at a time
    // (Amplify Damage .. Lower Resist, Confuse, Attract, Battle Cry, Taunt,
    // Cloak of Shadows' blindness, Bloodlust); the others (Inner Sight, Slow
    // Missiles, Conversion, a monster's war cry) each in its own node, one a
    // state (FUN_0056e970), so they don't take each other's place.
    // `swaps`: its end toggles the AI back (FUN_005c3370: Dim Vision's,
    // Terror's, Cloak of Shadows').
    struct SkillState { int skill = -1, level = 0; std::uint32_t until = 0; bool swaps = false; };
    SkillState curse;
    std::vector<SkillState> states;
    [[nodiscard]] std::vector<SkillState> curse_and_states() const {   // its curse, then its other states
        std::vector<SkillState> all{ curse };
        all.insert(all.end(), states.begin(), states.end());
        return all;
    }
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
    int anim_pct = 0;                         // other_animrate (stat 69) from its states and the auras on it: its other modes play faster
    int attack_pct = 0;                       // attackrate (stat 68) from them: its attack modes (Decrepify's -50 alone, Bloodlust's)
    // State 0x18 (slowed) from a Clay Golem it struck (put_slow): -velocitypercent
    // and -attackrate / other_animrate until then.
    int slow_speed = 0, slow_rate = 0;
    std::uint32_t slow_until = 0;
    int walk_pct = kMonsterVelocityPct;       // its walk % this frame (stat 0x43 + FRW, at least 25; monster_update): its moving modes' rate
    bool afraid = false;
    // Converted from a higher level (state 0x6d): its own level and life
    // (stats 0xb0 / 0xb1), put back as Conversion ends (FUN_005d01a0).
    int own_level = 0, own_max_life = 0, own_max_life_frac = 0;
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
    // "hit": the last command 0x13 (FUN_005734c0), a hit that set no mode (a burner's soft hit).
    std::string_view left_mode = "NU";
    // FUN_005dd2b0: the last command 3 (GH) or 0x13.
    [[nodiscard]] bool got_hit() const { return left_mode == "GH" || left_mode == "hit"; }
    int ai_state = 0, ai_state2 = 0, ai_state3 = 0, ai_command = 0, skill_unit = -1;
    // AI command kind 1 (FUN_0058f730: the overseer's Cry Help, do 128): the
    // unit its group goes at (a Foe::id) till that frame.
    int command_foe = -1, command_until = 0;
    int command_corpse = -1;                  // AI command 0xe's unit: the corpse its fetish shaman raises (a Monster::id)
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
    float leap_pace = 0;                      // the takeoff's path pace in cells a frame (FUN_005d9f70: MonStats Run << 8, FUN_00648690), 0: the sequence's time
    // The landing held while it flies (FUN_005d9d50 -> FUN_0064ea90: its pattern with bits 0x80 there,
    // the unit bit 0x1000 on the centre subtile; cleared as it lands, FUN_005da120): in the crowd then.
    UnitState landing_mark;
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
    std::optional<std::pair<int, int>> wanderer_spot;   // the Dark Wanderer's, once found (FUN_005bd0d0: a3q7's record +2 set, +4 / +8)
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
    [[nodiscard]] int life_fine() const { return int(std::int64_t(hit_points) * 256 - life_frac); }
    void set_life_fine(int life) {
        hit_points = int((std::int64_t(life) + 255) >> 8);
        life_frac = int(std::int64_t(hit_points) * 256 - life);
    }
    [[nodiscard]] int max_life_fine() const { return int(std::int64_t(stats.hit_points) * 256 - max_life_frac); }
    [[nodiscard]] bool alive() const { return hit_points > 0 && !gone; }
    // A corpse the corpse searches take (FUN_0063a770, 0x63a770: no `udead`
    // state on it): dead, in DD, not used (raised, eaten, exploded: corpse_used
    // stands for revive / corpseexp / redeemed), not shattered (state 107).
    [[nodiscard]] bool usable_corpse() const { return !alive() && mode == "DD" && !corpse_used && !shatter; }
    [[nodiscard]] bool targetable() const { return alive() && killable && !buried && !aloft && !riding && !rising; }   // unit flags 0xe: not a buried maggot, a vulture aloft, a riding imp, an unkillable one
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
        for (std::size_t i = 0; i < 6; ++i) res[i] += boss_res[i] + gear_res[i];
        d2d::rules::Target built{ hit_points, stats.hit_points, stats.armor_class, stats.level, type_info.can_block ? per_difficulty.to_block : 0, res, per_difficulty.drain,
                                  crush_divisor(game_data), type_info.undead, type_info.demon, life_fine(),
                                  game_data.life_steal_divisor[std::size_t(difficulty)], game_data.mana_steal_divisor[std::size_t(difficulty)] };
        built.special = boss != d2d::rules::Boss::none && boss != d2d::rules::Boss::minion;
        return built;
    }
    // Crushing Blow's life divisor on it (FUN_005bffc0): 10 for a hireling
    // (FUN_0063ee90's classes 0x10f, 0x152, 0x167, 0x230, 0x231); else 4, 8
    // for a MonStats boss (+0xc & 0x40, FUN_0063e9f0) or a superunique (data
    // +0x16 & 2, FUN_005a0180: only the superunique branches of the creators
    // set the bit; a unique has 1 | 8, a champion 1 | 4 | 8), raised by its
    // monster_playercount share (FUN_005738f0: 0 for one player, +50 % each after).
    [[nodiscard]] int crush_divisor(const GameData& game_data) const {
        if (type == 0x10f || type == 0x152 || type == 0x167 || type == 0x230 || type == 0x231) return 10;
        const auto& type_info = game_data.monsters.types[std::size_t(type)];
        const bool superunique = boss == d2d::rules::Boss::superunique;
        const int divisor = type_info.boss_column || superunique ? 8 : 4;
        return divisor + divisor * d2d::rules::player_life_pct(std::max(players, 1)) / 100;
    }
};

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
    // Who struck at it in melee this frame (Shiver Armor), of those whose blow landed (damagedinmelee,
    // FUN_0057c6c0 0x57c7a5: result flag 1 (& 1); Clay Golem's slow) and whose landed with physical damage
    // (Frozen Armor: the record's +8 over 0 in 256ths, Taken::physical_fine; elements alone don't count;
    // ponytail: an absorb (Energy Shield, event 0xb before the reduction, 0x57c2fc) lowers +8 in game.exe first),
    // by Monster::id: monsters born this frame may move the vector.
    std::vector<int> melee_by, landed_by, damaged_by;
    std::vector<int> fired_by;                // the owners of the ReturnFire missiles that reached it this frame (Chilling Armor)
    std::vector<int> shot_by;                 // the owners of the monster missiles that hit it this frame (their MonProp events)
    std::vector<int> crushed;                 // Crushing Blows this frame, each its life divisor (MonProp crush, FUN_005bffc0)
    int knock_under = 0x40;                   // MonProp knock lands on a draw's low 7 bits under it (FUN_005bf960)
    int crush_divisor = 10;                   // MonProp crush's life divisor, before a missile's doubling (FUN_005bffc0)
    int hits = 0;                             // hits that landed this frame (each wears its armour)
    int soft_hits = 0;                        // a burner's soft hits this frame (result flag 0x4000, no 4): the struck sound, no get-hit
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
    int mana_burn = 0;                        // mana it lost to Mana Burn this frame, in 256ths
    struct Cast { int skill = -1, level = 0, pick = -1; };   // pick: a monster's random curse's (rules::monster_curse_stats), else -1
    Cast cursed;                              // a curse cast on it this frame (a Cursed boss's Amplify Damage, the Summoner's Weaken)
    Cast marked;                              // a missile's hit 19 state put on it this frame (the finger mage's spider)
    int flashed = -1;                         // a one-shot overlay put on it this frame (an Overlay.txt row: Cry Help's srvoverlay)
    std::vector<std::string_view> states;     // the states on it a think reads (its curse's, its mark's: auratargetstate names)
    bool pet = false;                         // the merc, a summon: in the player's list
    int size = 2;                             // FUN_00620510: a player's 2, a monster's MonStats2 SizeX
    int threat = 14;                          // FUN_005dc920: a player's 14, a monster's MonStats threat
    int life_pct = 100;                       // FUN_00621f20 (a Fetish's think): life x 100 / max life
    // Its full max life / mana in 256ths, stats 7 / 9 (FUN_00625d10 / FUN_00625d60):
    // a player's. 0 for the rest: the thinks that read them (Baal's act 8,
    // the Succubi's Skill3) take any non-player alike.
    int max_life = 0, max_mana = 0;
    const Monster* monster = nullptr;           // a monster in the fight (Confuse, Attract: monsters fight monsters)
    std::uint32_t room_stamp = 0;             // its unit's (UnitState::room_stamp): its place in its room's list
    bool a3q5_done = false;                   // a player, or its pet, with quest 0x15 bit 0 (the zealots spare it)
    int blessed = 0;                          // the wisps' dance's magic find (stat 0x50) put on it this frame
    d2d::rules::DiabloPick pick;              // what Diablo's target score reads off it (FUN_005e8530)
    int listed = 0;                           // a pet's or merc's place in the player's list: the later it entered (FUN_005b1900), the earlier it is tried
    int id = -1;                              // a pet's or monster's Monster::id; a player's -2 - 2 x Hero::id, their merc's one below
    int striker_id = -1;                      // the Monster::id of the last blow that landed this frame, -1 none (the killer, FUN_0057cee0)
    // `striker`: the Monster::id it came from (FUN_0057cee0's attacker), -1 none.
    void take(const d2d::rules::Taken& taken, int striker = -1) {
        blocked = blocked || taken.blocked;
        hits += taken.hit ? 1 : 0;
        if (taken.hit && striker >= 0) striker_id = striker;
        if (taken.hit) {
            Blow blow{ taken.damage, 0, taken.elemental > 0 || taken.poison > 0, taken.physical, taken.elemental, taken.poison > 0, taken.frac, taken.raw, fighter.dr_pct, fighter.dr_flat };
            for (std::size_t type = 0; type < blow.parts.size(); ++type)
                if (type != 3) blow.parts[type] = { taken.element_raw[type], taken.element_dealt[type], type < fighter.res.size() ? fighter.res[type] : 0, taken.element_raw[type] > 0 ? taken.element_fine[type] : d2d::rules::StruckBlow::kNoFine,
                                       fighter.absorb_pct[type], fighter.absorb_flat[type], taken.element_took[type] };
            blow.mdr = fighter.mdr;
            blow.sum = taken.sum;
            blow.striker = striker;
            blows.push_back(blow);
        }
        damage += taken.damage; frac += taken.frac; physical += taken.physical; elemental += taken.elemental;
        if (taken.poison > 0 && taken.poison >= poison) { poison = taken.poison; poison_ticks = taken.poison_ticks; }   // the stronger (FUN_0057ac50)
        chill_ticks = std::max(chill_ticks, taken.chill_ticks);
    }
};

// A missile in flight (a quill rat's spike): straight on at its Missiles.txt
// velocity until it hits the foe, a wall, or runs out of range.
// A missile's seed (Missile::seed, unit +0x20, {low, high}): the draws
// game.exe takes from it (pSrvDmgFunc 3's soft hit, FUN_005ad220) step it,
// not the fight's rng.
// ponytail: a missile Fight::missile_tick has not seeded (a client-made or
// bare one) takes one step of the fight's rng for its first value (soft_hit).
struct Missile {
    const GameData::MissileInfo* info = nullptr;
    float x = 0, y = 0, velocity_x = 0, velocity_y = 0;       // cells, cells/s
    int dir = 0;                              // 0..63 (missile_facing), game.exe's path +0x64
    std::uint32_t born = 0, dies = 0;
    d2d::rules::MonStats src;                 // a monster's: its stats, A2 damage = the missile's
    int owner = -1;                           // a monster's: its Monster::id (unit +0x98, FUN_00552fd0)
    int owner_align = 0;                      // a monster's: its owner's alignment (FUN_005a8730 strikes only its foes)
    int min = 0, max = 0, attack_rating = 0, level = 1;  // the merc's: damage, attack rating, level; a skill's level
    bool friendly = false;                    // the merc's, the player's: hits monsters, not the player
    int caster = 0;                           // a friendly one's player (Fight::Hero::id), whose credit, leech and skills it carries
    bool by_merc = false;                     // a merc's skill: its weapon damage, attack rating and level, no synergies
    int striker = -1;                         // a summon's (a Shadow's): its Monster::id, whom its hits are struck by
    bool visual_only = false;                          // only a sight (a death blast's guts): hits nothing
    bool healed = false;                               // a recycler delay's heal given (missile do 29 / 33)
    // Its unit seed (+0x20): {a game seed step, 666}, as FUN_0059fa30 makes it
    // (FUN_00555230 type 3 -> FUN_00552df0 -> FUN_00650e40). Fight::missile_tick
    // gives it to all but a visual-only one.
    std::optional<d2d::rules::Rng> seed;
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
    int turn = 0;                             // Frozen Orb's direction index (do 15)
    int subtile_x = 0, subtile_y = 0;         // a Bone Wall maker's (do 13) subtile as it last laid or started; `hops` its pieces left, `target_id` the first piece
    std::array<int, 2> clt_count{};           // client do 26's frames to its next CltSubMissile1 / 2 (missile data +0x28 / +0x2c); a rock's [0], its hit 33's count
    int way_x = 0, way_y = 0;                 // client do 27's way in subtiles (data +0x28 / +0x2c): its target is its spot plus it
    int pierce = 0;                           // the player's: pierce charges left (stat 0x148: FUN_0059f940 sets, FUN_005ada80 spends)
    int last_hit = -1;                        // the monster it last struck (missile data +0x1c, FUN_0064a400), -1 none
    bool flight_over = false;                 // its flight ended (missile_fly false): its hit with no unit goes off
    std::uint32_t last_hit_ms = ~0u;          // when
    const GameData::MissileInfo* sub = nullptr;          // a monster's spawner's SubMissile1 (the Countess's firewall maker)
    // Its flight in act subtiles, 16.16 (rules::MissileFlight), from its
    // first step: toward `aim_at` (cells, FUN_0059fa30's target point) when
    // set, else FUN_0064fc60 at the start plus (1, 1), at the velocity's
    // path speed (missile_speed). Charged Bolt's: `bolt` its index (FUN_005c9290).
    // `rooms`: its room, then the near list's (missile_rooms); `follow`:
    // the target point lay outside that room, so the room moves with it.
    std::optional<std::pair<float, float>> aim_at;
    std::vector<std::pair<int, int>> route;   // a laid route in act subtiles (do 143's, rules::wobble_route; Blessed Hammer's, rules::hammer_route): its flight's points
    std::optional<std::pair<float, float>> retarget;   // cells: a point it's re-aimed at in flight (missile_fly), its velocity turned to match
    int bolt = -1;
    bool wall_maker = false;                    // FUN_005cd110: a draw first, under 20 its path stays straight
    d2d::rules::MissileFlight flight;
    int sound = 0;                            // its id for its sounds (0: not seen yet): its TravelSound stops when it's gone
    int prog_sound_phase = -1;                // last animation phase that played Missiles.txt ProgSound
    bool launched = false, follow = true;
    std::vector<std::array<int, 4>> rooms;
    d2d::rules::Rng client_seed;              // the client unit's (FUN_00465fd0, client_unit_seed), once client_seeded
    bool client_seeded = false;
};

}  // namespace d2d::game

// A Fine prints as its cells (the logs, devctl). Any context: libc++ 18
// (the Android NDK's) tests formattable with one that isn't format_context.
template <>
struct std::formatter<d2d::game::Fine> : std::formatter<float> {
    auto format(d2d::game::Fine fine, auto& context) const { return std::formatter<float>::format(float(fine), context); }
};
