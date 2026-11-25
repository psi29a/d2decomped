// SPDX-License-Identifier: GPL-3.0-or-later
// Fighting: the Blood Moor's monsters and missiles, the player's combat
// modes (swing, flinch, block, death) and Fighter (components/rules/
// combat.hpp), hits and kills, damage over time, the merc in a fight,
// potions and regeneration, monster sounds.
#pragma once

#include "ai/units.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"
#include "loot.hpp"
#include "spawning.hpp"

#include <chat_commands.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <hire.hpp>
#include <hud_globe.hpp>
#include <overlays.hpp>
#include <rules.hpp>
#include <sequences.hpp>
#include <shadows.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

using d2d::rules::ServerDoFunction;
using d2d::rules::ServerStartFunction;

// Skill and weapon checks (fight/skill_checks.cpp: what game.exe gates each on).
// Whether a skill runs this start and do function pair.
inline bool runs(const d2d::rules::Skill& skill, ServerStartFunction start, ServerDoFunction action) {
    return skill.srvstfunc == start && skill.srvdofunc == action;
}
// Whirlwind, Charge, Leap Attack: the skills that move the player.
bool moving_skill(const d2d::rules::Skill& skill);
// The melee skills d2d swings as game.exe does so far; every other swings a plain attack.
bool skill_built(const d2d::rules::Skill& skill);
// A self cast (right click, no target): its aurastate on the caster.
bool self_cast(const d2d::rules::Skill& skill);
// A throwable: a stacking weapon with a weapons.txt missiletype.
bool throwable(const GameData& game_data, const std::string& code);
// Throw's weapon gate: the item it would throw is a throwable.
bool throw_ready(const GameData& game_data, const Character& character);
// One hand's item (null: none) against one of a skill's item-type sets, `other` the other hand's.
bool hand_fits(const GameData& game_data, const d2d::d2s::Item* item, const d2d::d2s::Item* other, const d2d::rules::Skill& skill,
               const std::array<std::string, 3>& itypes, const std::array<std::string, 2>& etypes, bool bare);
// The skills' weapon-type gate: the hands fit the skill's item-type sets a and b, either way round.
bool weapon_gate(const GameData& game_data, const Character& character, const d2d::rules::Skill& skill);
// A known skill the character can't use now: drawn greyed, and a cast stops with impossible_1.
bool skill_refused(const GameData& game_data, const Character& character, int id);
inline bool attack_mode(int mode) { return mode == kModeA1 || mode == kModeKK || mode == kModeS1 || mode == kModeS3 || mode == kModeS4; }
// A finishing move, which releases the charge-ups' charges: Attack, Dragon Talon, Dragon Tail, Dragon Claw.
bool finisher(const d2d::rules::Skill* skill);

// The character's points in a skill (a class skill's, Attack's and the class's start skills' 1), without gear.
int skill_base_level(const GameData& game_data, const Character& character, int id);
// The character's level in a skill: skill_base_level, then what the gear and `extra` (a shrine's +skills) give.
int skill_level(const GameData& game_data, const Character& character, int id, const std::vector<d2d::d2s::ItemProp>& extra);

struct Fight {
    const GameData* game_data;
    const Level* const& level;             // ClientSession's: where the player is
    d2d::rules::Rng& rng;
    Loot& loot;
    Cues& cues;

    std::vector<Monster> monsters;         // mon_level's (Level::spawns), kept while the game runs
    int next_id = 1;                       // the next monster's unit id
    struct MadeBoss { int id, type, level; };
    std::vector<MadeBoss> made_bosses;     // qualifying unique placements, in creation order (the Golden Bird)
    // A monster by unit id: its index in `monsters`, or -1.
    [[nodiscard]] int monster_index(int id) const;
    int add_monster(Monster monster);                         // its index in the list it joined (a minion's Monster::leader)
    int add_monster(Monster monster, const Level* where);   // into that level's (kept) monsters

    const Level* mon_level = nullptr;                // whose monsters `monsters` are
    AreaSeen area_seen;                              // their spawn areas' "seen" flags (ai.cpp search_target)
    OpenDoor open_door;                              // a monster's door at a think (the world's doors)
    // One player in the fight (docs/design/local-coop.md): their unit, character
    // and merc, and everything their combat keeps from frame to frame. The
    // World's Player adds the rest of their state; `actor` is the one acting.
    struct Hero {
        int id = 0;                        // the player's unit id in the game (World::join): 1, 2 ...
        // Where the player is walking to: its path's target (+0x10, SP1); the target the
        // path was last worked out to (+0x18, SP3, FUN_00649970) is player.goal_x / goal_y.
        float target_x = 0, target_y = 0;
        bool reassigned = false;           // put somewhere at once (Teleport): the World tells its client (ev::Reassigned)
        bool storm_struck = false;         // a Thunder Storm bolt struck (its client do 84): the client's lightning (ev::Lightning)
        Character character;               // the World's own copy (the client's is a copy of the View's)
        std::optional<d2d::d2s::Item> held;   // the item in the player's hand (the cursor; inventory +0x20, FUN_0063c1e0)
        int kept_weapon = 0;               // a unit id, 0 none: the weapon wielded when an item last went on (player data +0x90, FUN_006233a0)
        UnitState player;                  // DS1 cells (x.5 = a cell centre)
        std::optional<Monster> merc;       // the save's mercenary, a monster on the player's side (merc_turn)
        const Npc* merc_npc = nullptr;
        int list_serial = 0, merc_listed = 0;   // when each pet entered the player's list (FUN_005b1900): the merc on hire, join and revive (spawn_merc), a summon where it is made
        // The player's / merc's curse from a monster (do 30, FUN_005c37c0: one
        // at a time, the newest): a Cursed boss's Amplify Damage, the
        // Summoner's Weaken. The player's aurastats join its self states'.
        struct Curse { int skill = -1, level = 0; std::uint32_t until = 0; int pick = -1; };   // pick: as Foe::Cast's
        std::array<Curse, 2> curses{};
        // A missile hit's state on the player / merc (hit 19, FUN_005ab110: the
        // skill's auratargetstate for auralen frames, at least 5, with its
        // aurastats): the finger mage's fingermagecurse (0x54).
        std::array<Curse, 2> marks{};
        // The player's one-shot overlays: its casts' and a monster's skill's
        // (Cry Help's srvoverlay, FUN_00621e40); the merc's and pets' are
        // their Monster::one_shots.
        d2d::rules::OneShots one_shots;
        std::uint32_t chilled = 0;                       // the player's chill (state 11, a monster's cold) runs out
        // State inferno (0xc, FUN_005c8e30): on 20 frames from an Inferno / Arctic Blast start, and
        // 6 frames past each start while it's on (a held cast). State skilldelay (0x79, FUN_0056ef90): till the frame
        // the last skill's do plus its delay.
        std::uint32_t burning = 0, skill_delay = 0;
        // The client's own state-inferno list (LAB_004e3060: FUN_006251f0 flags 2, +0x18 timeout 0x15 frames, on the
        // unit), counted down a frame at a time (FUN_00627460 from FUN_00480810) and set to 7 by each later start;
        // the frame it runs out (0 none). It clears the state as it goes (LAB_004e3050).
        std::uint32_t inferno_list = 0;
        // The player's stun (state 21, FUN_0057aae0: a monster's Smite) runs out.
        // It stops nothing; a hit while it runs is a get-hit (FUN_0057cee0), unless one is already running (FUN_0057edd0).
        std::uint32_t stunned = 0;
        int strafe_left = 0, strafe_at = -1;       // Strafe's arrows to come (one a restarted swing) and the next one's target
        int channel = -1, last_frame = -1;     // Inferno's skill while its cast lasts
        bool channel_armed = false;            // its do has run once (the skill entry's +0x18, FUN_005c8ca0): a flame each do from then
        // The monster being attacked (walked up to, then struck), the player's
        // non-walking mode (A1 attack, GH get-hit, BL block, DT dying, DD dead;
        // -1 none) and when it ends.
        int   attack_mon = -1;
        int   pmode = -1;
        std::uint32_t pmode_until = 0;
        // Knocked back (mode 0x13, drawn as GH: PlrMode.txt): from, to, cells.
        std::optional<std::array<float, 4>> knock;
        // Leap in flight (do 77): from, to, its start and landing, its skill and level.
        struct Leap { float from_x = 0, from_y = 0, to_x = 0, to_y = 0; std::uint32_t start = 0, lands = 0; int skill = -1, level = 1; };
        std::optional<Leap> leap;
        bool  pstruck = false;                 // this swing's hit is resolved
        struct Owed { int skill = -1, lvl = 0; } owed;   // a cast's mana, taken on its action frame (FUN_0056f7f0)
        std::int64_t last_paid = 0;                      // what the last pay() took (a refused Teleport gives it back)
        std::uint32_t action_ms = 0;           // the current mode's action frame, ms from its start
        float prate = 1.f;                     // the mode's animation rate (attack speed, FHR, FBR)
        d2d::rules::Fighter player_combat;                // the player in a fight, as of this frame
        d2d::rules::Fighter pf_kick;           // the same without the weapon (kicks: FUN_00646280 takes it off)
        // A Barbarian's or Assassin's two weapons, each alone (the other's stats off, FUN_00623c80): [0]
        // the right hand's, [1] the left's; `two_hands` while both hold a usable weapon.
        std::array<d2d::rules::Fighter, 2> hand_combat{};
        bool two_hands = false;
        int blow_hand = -1;                    // a blow that names its hand (Whirlwind's each weapon), -1 by the skill
        d2d::rules::StatSum psum{};            // the player's stats (gear, passives, masteries, states) as of this frame
        // Stat 0x148 (pierce_idx, not saved): each skill command adds 1 (C->S 0x05 / 0x06 / 0x0c / 0x0d,
        // FUN_00549d00), the seed of its missiles' pierce charges (FUN_0059f940).
        std::uint32_t pierce_idx = 0;
        float cast_x = 0, cast_y = 0;          // where a missile skill was sent
        int aura = 0;                          // the aura on (ClientSession: the right skill when it's one)
        // State 0x55 (nomanaregen): on while the player's Prayer pays for a
        // pulse that heals (prayer_pulse); a right skill picked clears it.
        bool no_mana_regen = false;
        // The player's summons (skills.md "Summons"): a Monster of the skill's
        // summon row on the player's side, the skill that raised it, and the
        // monster it's after.
        // A trap (do 45) shoots instead: its skill (a monster skill whose
        // missile carries the player's), at its level, `shots` times.
        struct Charge { int skill = 0, level = 0, count = 0; std::uint32_t until = 0; };   // see `charges`
        struct SelfState { int skill = 0, level = 0; std::uint32_t until = 0; };
        struct Pet {
            Monster monster; int skill = 0, level = 0; int target = -1; int shot_skill = -1, shot_level = 0, shots = 0;   // level: its skill's at the cast
            int listed = 0;                     // Hero::list_serial when it entered the player's list
            // Its sumskills d2d uses (FUN_0056deb0 at their sumsk calcs): a
            // missile it shoots (the skeletal mage's, the hydra's), an aura it
            // runs (Fire Golem's Holy Fire, the totems').
            int ranged = -1, ranged_level = 0, aura = -1, aura_level = 0;
            std::uint32_t aura_next = 0;
            // What the summoning skill's stats gave it (FUN_005c4470).
            std::array<int, 4> res{};                  // fire, lightning, cold, poison
            int thorns = 0, fire_lo = 0, fire_hi = 0, speed_pct = 0;
            int slow = 0;                              // item_slow (Clay Golem's aurastat): its damagedinmelee event (FUN_005c6760) slows the striker
            // Its A1 before damagepercent and to-hit before item_tohit_percent,
            // those percents, and its summoning skill's damage and to-hit in its
            // swings (a SkillDamage row's: pet_skill_damage) as they stand now.
            std::array<int, 2> a1_unscaled{};
            int to_hit_unscaled = 0, damage_pct = 0, to_hit_pct = 0;
            bool skill_damage = false;
            int skill_low = 0, skill_high = 0, skill_to_hit = 0;
            const Level* where = nullptr;              // the level it's on (pets follow the player across)
            std::uint32_t until = 0;                   // it goes then (Decoy, Revive, Hydra; 0 never)
            bool homing = false;                       // a revived monster on its walk back to its owner (FUN_005b0ff0)
            int variant = 0;                           // a skeletal mage's element (its +0xf, FUN_005ce0b0)
            int hits = 0;                              // a Raven's attacks left (0: no count)
            bool idle = false, mirror = false;         // Decoy stands; a Shadow Warrior swings the owner's blow
            bool chase = false;                        // walking at its target (FUN_005ded40)
            bool to_owner = false;                     // walking at its owner (PetFollow)
            std::array<int, 3> ctrl{};                 // its AI control's +0x14 / +0x18 / +0x1c (rules::PetBrain)
            int cast = -1;                             // the skill its attack mode does (a traced think's), -1 a swing
            float cast_x = 0, cast_y = 0;              // a skill at a spot (a wolf's Teleport)
            // A Shadow's Dragon Flight while it plays: its MonSeq sequence, the rows
            // past, the rate it steps at (1/256 rows a tick).
            const std::vector<GameData::MonSeqFrame>* seq = nullptr;
            int seq_done = 0, seq_rate = 256;
            // A Shadow's skills in its list's order (pet-ai.md "Their skill lists")
            // and the Shadow Master's left skill (FUN_00643bc0; -1 none).
            std::vector<d2d::rules::ShadowListed> skill_list;
            int left_skill = -1;
            std::vector<Charge> charges;               // a Shadow's charge-ups' charges (as the player's)
            std::vector<SelfState> buffs;              // a Shadow's self states (Burst of Speed, Fade, Venom, Cloak, Blade Shield)
            Curse curse;                               // a monster's curse on it (the newest, as the player's)
            std::unordered_map<int, int> buff_stats;   // their aurastats summed (pet_buffs)
            int bo_life = 0;                           // a war cry's maxhp percent on its life (pet_buffs)
            std::uint32_t blade_next = 0;              // its Blade Shield's next strike
        };
        // What a Shadow summons while the pets take their turns (traps, Blade
        // Sentinel), made once they're done.
        struct PetSummon { int skill = 0, level = 0; float x = 0, y = 0; };
        std::vector<PetSummon> pet_summons;
        std::vector<Pet> pets;
        std::uint32_t aura_next = 0;           // its next pulse
        std::uint32_t merc_aura_next = 0;      // the merc's aura's next pulse
        // The skill the player attacks with (Skills.txt id; 0 Attack), the one
        // this swing uses (Attack when it's not built or can't be paid for),
        // and the strikes still to come (Dragon Talon's kicks, Zeal's hits).
        int   attack_skill = 0, swing_skill = 0, kicks_left = 0;
        ChargedSkill item_charge;             // the charged entry the cast came from (UseSkill::item; item -1 none): its level, a charge for its cost
        ChargedSkill attack_charge;           // the queued attack's (attack_skill's) entry, item_charge once its swing starts
        int   struck_class = 0;               // the last hit on the player's hit class (unit +0xb0): its impact on GH / DT
        int   shown_class = 0;                // the hit class the client last had, any landed hit's (GH or S->C 0x0c): its block sound
        std::int64_t self_hurt = 0;            // life (256ths) the player's own skills cost (Sacrifice), taken with the monsters' hits
        // Who last hurt the player (FUN_0057cee0's attacker; kind 1 a monster, id its Monster::id;
        // 2 an object, id its Objects.txt row; -1 none), and so who killed them (FUN_00580ec0's
        // killer, the slain message FUN_0054d8a0 names).
        struct Striker { int kind = -1, id = -1; } struck_by, killer;
        int merc_hurt = 0;                     // life the merc lost besides the hits (thorns on its swings, its poison), taken with the monsters' hits
        // An SQ skill's sequence while it plays (sequences.hpp, for the weapon
        // class): its frames, each seq_frame_ms long, and the hits (event 1)
        // already struck.
        std::span<const d2d::rules::SeqFrame> seq{};
        std::uint32_t seq_frame_ms = 40;
        int seq_speed = 256;                   // the sequence's rate, 256ths of a row a frame (attack_speed: rules::seq_step_row)
        int seq_struck = 0;
        int seq_sounded = 0;                   // its event-3 frames already past (Jab's dosound a)
        bool seq_loop = false;                 // Whirlwind's plays over until it arrives
        // A skill that moves the player (Whirlwind, Charge, Leap Attack):
        // toward (tx, ty) at `speed` cells/s while `on`; `fly` ignores walls
        // (a leap); whirl_next is Whirlwind's next hit time (FUN_005d9320).
        struct SkillMove { float target_x = 0, target_y = 0, speed = 0; bool active = false, fly = false; std::vector<std::pair<float, float>> route; };   // route: Whirlwind's points, cells
        SkillMove smove;
        float move_x = 0, move_y = 0;          // where the click sent a moving skill
        std::uint32_t whirl_next = 0;
        // Whirlwind's skill node: +0x24 set (its first pass struck) and flag 0x2000 (weapsel 2's left hand), both clear at its start.
        bool whirl_begun = false, whirl_left = false;
        // A charge-up's charges (FUN_005d3320: its aurastate, the skill and
        // level in stats 0x15e / 0x15f, the count, at most 3, in aurastat1),
        // until auralencalc ticks after the last one.
        std::vector<Charge> charges;
        // Self states from a swing (aurastate): Concentrate's lasts while its
        // swing does (made with no length; its removal isn't traced), Berserk's
        // calc2 ticks (FUN_005d97f0, 10 when that's 0).
        std::vector<SelfState> self_states;
        int absorb_pool = 0, absorb_skill = -1;        // Bone / Cyclone Armor's damage left to absorb, whole points
        int absorb_frac = 0;                           // and the 256ths of a point the pool holds beyond them
        std::pair<int, int> blaze_subtile{ -1, -1 };   // the subtile the player stood in at the last Blaze check
        std::uint32_t storm_next = 0;                  // Thunder Storm's next bolt
        int storm_last = -1;                            // the unit id Thunder Storm struck last (FUN_00644560)
        std::uint32_t blade_next = 0;                  // Blade Shield's next strike
        std::uint32_t armageddon_next = 0, hurricane_next = 0;   // their timer events (type 5) next
        std::int64_t bo_life = 0, bo_mana = 0;         // Battle Orders' life and mana on the maxima (256ths)
        // What worn items (and passives) put on the maxima (256ths): the
        // character's own, as a save keeps them, are the maxima less these.
        std::array<std::int64_t, 3> item_max{};        // life, mana, stamina
        // The merc in a fight: its stats (hireling.txt at its level), the
        // monster it's after and whether it's walking at it. The rest is its
        // Monster's (World::merc): life, mode (NU / WL / RN follow, A1 attack,
        // GH, DT), mode_until, struck, its seed (+0x20, the think's rolls),
        // next_act (its next think), move_pct (a move's pace).
        d2d::rules::MercStats merc_st;
        int   merc_target = -1;
        bool  merc_chase = false;
        // Its hireling skills (Skills.txt ids, levels: rules::merc_skill_levels),
        // the AI control's +0x14 (the attack think's chance growth), the aura it
        // started (FUN_005701b0: running from then, its effect not built) and
        // the skill (and level) its attack mode carries (-1: a plain blow).
        std::array<int, 6> merc_ids{}, merc_levels{};
        int   merc_growth = 0, merc_aura = -1, merc_skill = -1, merc_skill_level = 0;
        // Stat 0x148 (FUN_0059f940's seed for a merc's Pierce missiles): FUN_00597e20 adds 1 as
        // it sends a client the merc's turn to NU (mode 1), so 1 for each player it's near.
        // Giving a client the unit (FUN_00571f90) sets it to its subtile x + y: the owner's
        // at merc_joins, another player's as it comes near (merc_turn, `merc_given`). The
        // give's queue replay (FUN_00571cd0) sends no mode, so it adds nothing.
        std::uint32_t merc_pierce_idx = 0;
        std::vector<int> merc_given;          // the heroes (Hero::id) whose clients have it now
        // An SQ skill's sequence while it plays (Jab: MonSeq seq_act2guardjab,
        // its MonStats Sk*mode), and the sequence frames already past.
        const std::vector<GameData::MonSeqFrame>* merc_seq = nullptr;
        int   merc_seq_done = 0;
        // A self cast's state on the merc (Frozen Armor, do 18 FUN_005c9480):
        // the skill, its level, until when. Its aurastats go on its fighter
        // (skill_armor_percent), a melee striker freezes or chills (merc_buff_events), and
        // the pick passes it over while it runs (FUN_005e4d30, its state on).
        struct MercBuff { int skill = -1, level = 0; std::uint32_t until = 0; } merc_buff;
        // A war cry's state on it (hit 18, FUN_005ab0b0 -> FUN_005d8290: Shout's
        // defense %, Battle Orders' life %, Battle Command's), apart from its own.
        MercBuff merc_cry;
        // What the merc's think reads of the player: its mode (2 walk, 3 run, 6
        // walk in town, 1 else) and footsteps (player data +0xa0 / +0xa8, 20
        // subtile spots, `footstep_cursor` the next to write), World's to fill.
        int   owner_mode = 1;
        std::array<std::pair<int, int>, 20> footsteps{};
        int   footstep_cursor = 0;
        std::uint32_t footstep_ms = 0;
        std::uint32_t step_sound_ms = 0;        // when its last footstep sounded (unit +0x84)
        std::uint32_t walk_from_ms = 0;         // when its walk began: its animation's phase for the footsteps
        bool          stepping = false;         // walking as of the last footstep tick
        // Where the player last arrived through a warp (player data +0x148 /
        // +0x14c, FUN_00554ea0; subtiles): a pet near it steps out of the crowd.
        std::pair<int, int> warp_spot{};
        // Potions working: life / mana (8.8 fixed) a millisecond, until when.
        struct Regen { double life = 0, mana = 0; std::uint32_t until = 0; bool poison = false; };
        struct StaminaPot { int bonus = 0; std::uint32_t until = 0; } stamina_pot;   // the staminapot state
        // The antidote / thawing states (pSpell 6, FUN_005beda0): their potion's
        // stats until when; [0] antidote, [1] thawing.
        struct PotionState { std::vector<std::pair<int, int>> stats; std::uint32_t until = 0; };
        std::array<PotionState, 2> potion_states;
        std::vector<Regen> regen;
        // A shrine's boost (Shrines.txt booster: its stats, FUN_00583b30), one
        // at a time, until when.
        struct Boost { int shrine = 0; std::vector<std::pair<int, int>> stats; std::uint32_t until = 0; } boost;
        double merc_heal_rate = 0, merc_heal_acc = 0;  // the merc's healthpot: life per ms, its fraction
        std::uint32_t merc_heal_until = 0;
        double merc_poison_rate = 0, merc_poison_acc = 0;  // the merc's poison (FUN_0057ac50, as the player's): life per ms, its fraction
        std::uint32_t merc_poison_until = 0;
        std::array<PotionState, 2> merc_potion_states;   // the merc's antidote / thawing states (merc_drinks)
        double regen_acc_life = 0, regen_acc_mana = 0;
        d2d::rules::LifeSent life_sent;                // the server's life update: what the client last heard (the globes' preview)
        bool portal_due = false;                       // Town Portal's action frame came
        // The wisps' dance's blessing on the player (FUN_005f39b0): stat 0x50
        // (magic find) for 0x1a5e00 frames, the last one's.
        struct Blessing { int find = 0; std::uint32_t until = 0; } wisp_blessing;
    };
    using Curse = Hero::Curse;
    using Leap = Hero::Leap;
    using Charge = Hero::Charge;
    using SelfState = Hero::SelfState;
    using Pet = Hero::Pet;
    using PetSummon = Hero::PetSummon;
    using SkillMove = Hero::SkillMove;
    using MercBuff = Hero::MercBuff;
    using Regen = Hero::Regen;
    using Boost = Hero::Boost;
    using Blessing = Hero::Blessing;
    // The players in the game (the World's, join order) and the one acting
    // now: the acting player's state is what `actor` points at, and the loot
    // drops and picks up for them too.
    std::vector<Hero*> heroes;
    // Each unit's place in its room's list (UnitState::room_stamp): a unit seen in a room
    // other than its last gets the next stamp (FUN_0064c2c0 puts it at the head).
    std::uint32_t room_stamps = 0;
    void restamp();
    Hero* actor = nullptr;
    void act_as(Hero& hero) {
        actor = &hero;
        loot.character = &hero.character; loot.player = &hero.player;
        loot.blessed_find = hero.wisp_blessing.find; loot.blessed_until = hero.wisp_blessing.until;
    }
    // The player of that id, or null (gone).
    [[nodiscard]] Hero* hero_of(int id) const {
        const auto found = std::ranges::find(heroes, id, &Hero::id);
        return found == heroes.end() ? nullptr : *found;
    }
    // The acting player's level in a skill: points, and with item bonuses
    // (ClientSession points these at its SkillBar).
    std::function<int(int)> skill_base, skill_level;
    // Chill's -50 attackrate / velocitypercent (FUN_0057af80 -> FUN_00623f50).
    [[nodiscard]] int chill_rate(std::uint32_t now_ms) const { return now_ms < actor->chilled ? -50 : 0; }
    // A healer's and a well's cure.
    bool cure(std::uint32_t now_ms, bool well);
    int game_difficulty = 0;                         // new_game's
    bool game_expansion = true;                      // new_game's: the acting character's (game +0x70); add_monster marks a classic game's
    bool remote_monsters = false;                    // a game joined on a game.exe host: its monsters (S->C 0xac), none made here
    // The chat box's /players count (DAT_00883d70, FUN_00535780): 0 until
    // set, never reset, so a later game keeps it (new_game leaves it).
    int players_setting = 0;
    // During a split-level turn (World::tick): the players on the other
    // levels, whom `heroes` (this level's) leaves out; 0 otherwise.
    int players_elsewhere = 0;
    // The players a monster's life and experience and NoDrop count
    // (FUN_00535790): every player in the game, or the /players count when
    // it's larger.
    [[nodiscard]] int game_players() const { return d2d::rules::game_players(int(heroes.size()) + players_elsewhere, players_setting); }

    std::unordered_map<const Level*, std::vector<Monster>> kept;   // the other levels', while the player is away
    std::unordered_map<const Level*, std::vector<Missile>> kept_missiles;   // a level's in flight while another has its turn (World::swap_to)
    Spawning spawning;                                   // the game's rooms in play and what they spawned (new_game)
    std::vector<Missile> missiles;         // in flight in the Blood Moor
    int missile_sounds = 0;                // the last Missile::sound handed out
    std::uint32_t neutral_ms = 0, neutral_gap = 0;   // any monster's last Neutral and the ticks till the next may (0x7c88b8 / 0x7c88c0)
    d2d::rules::Rng sound_rng{ 0x5d1u };   // the sounds' own rolls (game.exe's are the client's seed, FUN_004e40a0), apart from the game's
    // The floor's material at (x, y) cells: the first floor tile's DT1 flags, else the level's SoundEnviron
    // Material 1.
    [[nodiscard]] int floor_material_at(float x, float y) const;
    std::vector<Missile> pending;          // made this frame, joining `missiles` after its update
    std::vector<int> told;                 // skills logged as not built yet
    // The stats as a save keeps them: the maxima without items or Battle Orders.
    [[nodiscard]] d2d::d2s::Stats own_stats() const;
    [[nodiscard]] int merc_cry_stat(int stat) const;
    void merc_buff_events(Foe& foe, std::uint32_t now_ms);
    // Frozen Armor's event (2, FUN_005cac50) on the monster that struck its wearer: `length` the wearer's calc1, its cold pierce.
    void frozen_armor_freeze(std::size_t monster_index, int length, int cold_pierce, std::uint32_t now_ms);
    // A footstep at subtile (x, y): written over the oldest, the cursor on.
    void footstep(int x, int y, std::uint32_t now_ms) {
        actor->footsteps[std::size_t(actor->footstep_cursor)] = { x, y };
        actor->footstep_cursor = actor->footstep_cursor >= 0x13 ? 0 : actor->footstep_cursor + 1;
        actor->footstep_ms = now_ms;
    }

    // A fresh game at `difficulty`: its monsters, nothing in flight.
    void new_game(int difficulty);
    // The save's merc joins: its fighting stats at its experience.
    void merc_joins();
    // A player coming near the merc: its client is given it (FUN_00571f90),
    // SetStat(0x148, (x + y) & 0xffff); one gone off forgets it (merc_given).
    void merc_gives();
    [[nodiscard]] bool merc_near(const Hero& hero) const;   // within the 30-cell window a client is sent it in
    // The merc's stats and skill levels for its experience.
    void merc_level_stats();
    // Back in camp after dying: full life, mana and stamina, no potions or poison working.
    void revive(std::uint32_t now_ms);

    // Keys 1-4 drink the belt's bottom-row potion in that column.
    void drink(int col, std::uint32_t now_ms);
    // Shift with the key: the merc drinks it.
    void feed_merc(int col, std::uint32_t now_ms);
    // A healing, antidote or thawing potion in hand given to the merc: true when it drank it.
    bool feed_merc_item(const d2d::d2s::Item& item, std::uint32_t now_ms);
    void merc_drinks(const std::string& code, std::uint32_t now_ms);
    void drink_item(int id, std::uint32_t now_ms);
    void potion(const std::string& code, std::uint32_t now_ms);
    // Potions and poison, then the steady regeneration: replenish life (hpregen, N/256 a tick) and mana.
    void apply_regen(std::uint32_t now_ms, std::uint32_t last_ms);
    // A monster's new mode sounds off (MonSounds.txt): an attack's cry (at
    // its chance) and weapon, get-hit, death, each after its delay in ticks.
    void monster_sounds(Monster& monster, std::uint32_t now_ms);
    // A skill start's cast overlay on its caster's one-shots `shown`: the player's, a monster's, the merc's or a
    // pet's.
    void start_cast_overlay(const d2d::rules::Skill& skill, bool from_item, d2d::rules::OneShots& shown, std::uint32_t now_ms) const;
    // What the character wears (the save's appearance, else the class's starting gear).
    [[nodiscard]] const GameData::Appearance& gfx() const;
    [[nodiscard]] const GameData::AnimTiming& player_anim(int mode) const;
    // A swing takes attack_ticks for the item attack speed and weapon speed; a cast takes cast_ticks (FCR),
    // get-hit and block recover faster with FHR / FBR (rules::speed_frames on the base frame count).
    void set_pmode(int mode, std::uint32_t now_ms);
    // ms from the mode's start to its action frame: an attack's is the tick FUN_005539b0 files it on (attack_action_tick), any other mode's the
    // action frame scaled by prate.
    [[nodiscard]] std::uint32_t action_at() const;
    // Impact and the class cry.
    void play_player_hit(std::uint32_t now_ms);
    void do_sounds(std::uint32_t now_ms);           // the skill's do: its dosound, and tgtsound on its target
    [[nodiscard]] bool dead() const;
    // State 0x36 (uninterruptable, FUN_005544b0) on the player: a Whirlwind,
    // Leap or Leap Attack under way (their starts set it, their ends clear it).
    [[nodiscard]] bool uninterruptable() const { return actor->leap || (actor->smove.active && (actor->smove.fly || actor->whirl_next != 0)); }
    // Life 0 and the death plays (DT, then DD).
    void die(std::uint32_t now_ms);

    // The player as combat sees them.
    [[nodiscard]] d2d::rules::Fighter player_fighter(d2d::rules::Fighter* kick = nullptr,
                                                     const d2d::rules::StatSum* states = nullptr,
                                                     const std::vector<d2d::rules::PassiveStat>* passives = nullptr,
                                                     d2d::rules::StatSum* sum_out = nullptr,
                                                     const d2d::d2s::Item* only_hand = nullptr, bool melee = false) const;
    // `only_hand`: that weapon in hand as the wielded one and the other hand's weapon's stats off, as
    // FUN_00627910 sets them for one hand of two (FUN_004e9260 / FUN_004e9300, the panel's dual line).
    // `melee`: the melee masteries whatever the skill in use (the panel's lines pick their own).
    // The player's fighter this frame, with the self states' aurastats
    // (FUN_005c6cc0): skill_armor_percent and armor_override_percent as %
    // of the panel defense (the override after), damageresist to DR %;
    // every other aurastat but life joins the stat sum (below).
    void update_fighters(std::uint32_t now_ms);

    // What calcs ask of the player (skills.hpp).
    [[nodiscard]] d2d::rules::CalcEnv calc_env();
    // The merc's: its level, no skills or stats behind it (mercs have no synergies).
    [[nodiscard]] d2d::rules::CalcEnv merc_calc_env();
    // A swing starts: the skill in use when it's built, paid for from mana; short of mana, a skill with
    // AttackNoMana swings a plain attack instead, any other doesn't swing.
    bool start_swing(std::uint32_t now_ms, bool shown = false);
    // Short of mana for a skill (the client's FUN_00647960 reason 1:
    // FUN_00647540's cost, FUN_006440f0's startmana) the class says so: message
    // 0x15 (table 0x711ddc) -> FUN_004cb9c0, the class table's +0xc,
    // <class>_needmana_1 (class_voice).
    void need_mana(std::uint32_t now_ms);
    // Reason 0x13 (status 2, skill_refused): the class table's +8,
    // <class>_impossible_1 (amazon 0xb89), as need_mana.
    void impossible(std::uint32_t now_ms);
    // FUN_004cb9c0's messages 0x13..0x18: the class line on the player, none
    // while one of its speech lines plays (FUN_004b9e90: Cue::hush); the
    // last line again within 75 client frames of it is skipped, on the
    // client (Cues::repeat_line), as game.exe's client does it.
    void class_voice(std::string_view name, std::uint32_t now_ms);
    // An SQ skill plays its sequence at the attack's speed; with no frames for the weapon it swings once.
    void start_sequence(std::uint32_t now_ms);
    [[nodiscard]] std::span<const d2d::rules::SeqFrame> skill_sequence() const;   // the swing skill's frames for the weapon, none for a plain mode
    // The movement a moving skill starts with (its srvstfunc).
    void start_move(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    // Where the frames stand: the frame index (looping or held on the
    // last) and how many event-1 frames have passed since the start.
    [[nodiscard]] std::size_t seq_at(std::uint32_t now_ms) const;
    [[nodiscard]] int seq_events(std::uint32_t now_ms, int event = 1) const;   // the frames with `event` past
    // What the player shows in a sequence: its frame's mode, and a start
    // time that lands the renderer (frame = elapsed / ms_per_frame) on it.
    [[nodiscard]] std::pair<int, std::uint32_t> seq_view(std::uint32_t now_ms) const;
    // The swing's animation: KK for kicks, S1 for Smite, else A1.
    [[nodiscard]] int swing_mode() const;
    [[nodiscard]] bool skill_in_use() const;     // the unit's mode holds a used skill (FUN_00620250 non-null): a swing or a cast
    [[nodiscard]] bool hit_interrupts() const;   // a big hit may put the player in a get-hit now (FUN_005809d0: FUN_0057edd0 and FUN_0057eec0)
    // What the swing's skill adds to the blow.
    [[nodiscard]] d2d::rules::Swing swing();
    // A melee skill's hit at `lvl` with `env`'s calcs: swing()'s, and the merc's (Jab, Bash, Stun).
    [[nodiscard]] d2d::rules::Swing skill_swing(const d2d::rules::Skill& skill, int lvl, const d2d::rules::CalcEnv& env, bool by_player);

    // A hit on monster i (the player's or the merc's): blocked, it blocks; otherwise the damage, life/mana leech
    // (the player's), poison and bleeding over time, a chill, a knockback.
    // `striker`: the Foe::id it came from (-1 none), kept even on a miss or
    bool land(std::size_t monster_index, const d2d::rules::Blow& blow, bool by_player, std::uint32_t now_ms, int striker);
    void player_thorns(std::size_t target, const d2d::rules::Blow& blow);   // a MonProp thorns monster's flat value back on the player's melee blow that landed, through the whole hero's DR
    void merc_strike(std::size_t target, const d2d::rules::Swing& swing, std::uint32_t now_ms);   // the merc's melee blow on a monster, and its thorns back
    // Clay Golem's damagedinmelee event, on a pet whose summoning skill hooks it.
    void clay_slows(const Pet& pet, const Foe& foe, std::uint32_t now_ms);
    void pet_strike(Pet& pet, std::size_t target, const d2d::rules::Blow& blow, std::uint32_t now_ms);   // a pet's melee blow, and its thorns back
    // A monster with GH and KB modes knocked back away from (from_x, from_y): land's knockback, a monster foe's (Smite, MonProp knock).
    void knock_back(Monster& target, float from_x, float from_y, std::uint32_t now_ms);
    // A landed hit's event 4 on it (Monster::struck_event), after its get-hit and knockback.
    void struck_event(Monster& target, bool knockback) const;
    // A monster foe's landed blows of a tick, one damage record each; true when they kill it.
    auto take_blows(Monster& struck, const Foe& foe, std::uint32_t now_ms) -> bool;

    // Poison and bleeding tick on the monsters near the player.
    void monster_dots(std::uint32_t now_ms, float elapsed);

    // The player's swing: the hit lands on the attack's event frame (at the
    // swing's own speed) — player_blow: hit chance, the monster's block,
    // deadly strike, resistances, elemental damage, crushing blow, leech.
    // The swing's hits: a sequence's on each of its event-1 frames, else
    // one on the attack's event frame.
    void strike(std::uint32_t now_ms);
    void hit(std::uint32_t now_ms, float reach = -1.f);   // reach < 0: game.exe's (FUN_0057ec10); else the cells

    // Charged Strike: calc1 of its missile from the monster struck, on away from the player (to twice its spot
    // less the player's), each wandering as Charged Bolt's.
    void strike_bolts(const d2d::rules::Skill& skill, std::size_t monster_index, std::uint32_t now_ms);
    // A monster Conversion or Mind Blast turned (its state already on): good,
    // no target, and down to the caster's level when above it.
    void turn_good(Monster& turned, int caster_level);
    // Corpse i goes up (Corpse Explosion, Death Sentry's 'mon death sentry'): rules::corpse_blast_fine, calc3 %
    // of it fire and the rest physical, on what its aurafilter takes within half the aurarange.
    void corpse_blast(const d2d::rules::Skill& skill, int lvl, std::size_t corpse_index, int caster_level, int fire_pierce, std::uint32_t now_ms);
    // Skills that act where they're cast, with no missile of their own.
    [[nodiscard]] static bool spot_skill(const d2d::rules::Skill& skill);
    // Who casts a skill: where from, at what level, at which monster (-1
    // none) and point, and whether it's the merc's (its missiles strike with
    // its damage, attack rating and level).
    // pet: a summon's cast; unit_id its Monster::id, the striker its hits
    // record (FUN_00621d50 keeps the summon's own unit).
    struct Caster { float x = 0, y = 0; int level = 1, target = -1; float to_x = 0, to_y = 0; bool merc = false, pet = false; int unit_id = -1; };
    [[nodiscard]] int striker_of(const Caster& caster) const { return caster.unit_id >= 0 ? caster.unit_id : caster.merc ? merc_foe_id() : player_foe_id(); }
    [[nodiscard]] Caster player_caster(int skill) const;
    bool casting_merc = false;             // launch() marks what's fired as the merc's
    int flight_part = 3;                   // spot's Dragon Flight: 1 the flight, 2 the kick (one event's, FUN_005d7850), 3 both
    int casting_unit = -1;                 // ... and as a summon's (Missile::striker)
    void spot(const d2d::rules::Skill& skill, std::uint32_t now_ms) { spot(skill, now_ms, player_caster(skill.id)); }
    void spot(const d2d::rules::Skill& skill, std::uint32_t now_ms, const Caster& caster);
    // The skill's own element on its hit (FUN_0056e0c0: EMin..EMax with
    // brackets, synergy and the element's mastery (flag 1); Power Strike's
    // lightning, Poison Dagger's poison). Stun is elsewhere.
    void skill_element(d2d::rules::Fighter& fighter, const d2d::rules::Skill& skill);
    void add_state(const d2d::rules::Skill& skill, int lvl, std::uint32_t until);
    void self_state(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    // Werewolf / Werebear's shift back.
    bool shift_back(const d2d::rules::Skill& skill);
    // A state's States.txt group, 0 none.
    [[nodiscard]] int state_group(const std::string& aurastate) const;

    // A self cast's state: auralen frames — none (Bone Armor, Cyclone Armor) lasts until its absorb is spent.
    void start_state(const d2d::rules::Skill& skill, int lvl, std::uint32_t now_ms);
    [[nodiscard]] const SelfState* state_of(std::string_view name) const;
    // The player's buffs as the fight goes.
    int absorb(Foe& foe, std::uint32_t now_ms);
    void buff_events(Foe& foe, std::uint32_t now_ms);
    void frozen_armor_events(const Foe& foe, std::uint32_t now_ms);   // Frozen Armor's freeze, after the absorbs (hurt_side)
    // Chilling Armor's answer, after the frame's missiles have struck.
    void return_fire(const Foe& foe, std::uint32_t now_ms);
    // A monster's answer (its self state's srvmissilea) to the player's or
    // merc's ReturnFire missile striking it.
    void monster_return_fire(std::size_t monster_index, const Missile& missile, bool by_merc, std::uint32_t now_ms);
    void buff_tick(std::uint32_t now_ms);
    // The skill states on the monsters (their curse and cry slots): what
    // they add up to this frame.
    void monster_states(std::uint32_t now_ms);
    // A charge-up's hit lands: one more charge (up to 3), for auralencalc
    // ticks more (FUN_005d3320), and its prgsound row for the count on the
    // unit (`unit`, at x, y; rules::sound_ids::charge_up).
    void charge(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    void charge(std::vector<Charge>& held, const d2d::rules::Skill& skill, int lvl, std::uint32_t now_ms, std::uint64_t unit, float x, float y);
    // What the charges add to a finishing blow (FUN_005d3ba0 / FUN_005d3ac0,
    // at the higher of the stored level and today's), and progressive_tohit's
    // par4 a charge as the swing's to-hit %.
    void add_charges(d2d::rules::Fighter& fighter, d2d::rules::Swing& swing) { add_charges(actor->charges, fighter, swing, true); }
    // The player's charges' progressive_tohit: Dragon Flight's to-hit % on top of its own.
    [[nodiscard]] int progressive_tohit();
    void add_charges(const std::vector<Charge>& held, d2d::rules::Fighter& fighter, d2d::rules::Swing& swing, bool player_levels);
    // A row with no Skill: its own physical and element at level lvl with their synergies (DmgSymPerCalc,
    // EDmgSymPerCalc on `env`), the weapon at its SrcDamage.
    [[nodiscard]] d2d::rules::MissileDamage row_damage(const GameData::MissileInfo& missile_info, int lvl, const d2d::rules::CalcEnv& env, int skill_id) const;
    // The skill whose damage a missile's hit takes (FUN_0064b860), null for a row with no Skill (row_damage's).
    [[nodiscard]] const d2d::rules::Skill* damage_skill(const Missile& missile) const;
    // A finishing hit on monster `on` releases every charge (FUN_005d5220):
    // with n charges (1..3) the skill's srvprgfunc n runs — prgstack skills
    // (Fists of Fire, Claws of Thunder, Blades of Ice) run 1..n, each with
    // the count set to its own — at the charges' level or the skill's now,
    // whichever is higher; then the charges are gone. d2d runs:
    //   38 (FUN_005d3e80): the skill's physical and element (FUN_0056e170,
    //      FUN_0056e0c0) on every monster within prgcalc n subtiles of it;
    //   36 (FUN_005d4db0): a nova of the count's missile round it
    //      (FUN_0056d400), its range + calc1;
    //   39 (FUN_005d3f90): prgcalc n squared tries at points within that
    //      many subtiles (rules::release_scatter_offset), each a missile of
    //      the count's (fire, ice cubes). game.exe draws them on a stepped
    //      copy of the caster's seed, d2d on its rng (deviations.md
    //      approximation 18);
    //   40 / 41 (FUN_005d5010 / FUN_005d5080, Royal Strike's first and
    //      third) and 37 / 143 (FUN_005d4e70 / FUN_005d4f40): prg's notes.
    // The count's missile (FUN_005d3cf0): 1 srvmissilea, 2 b, 3 c.
    void release(std::size_t monster_index, std::uint32_t now_ms) { release(actor->charges, monster_index, now_ms, true); }
    void release(std::vector<Charge>& held, std::size_t monster_index, std::uint32_t now_ms, bool player_levels);
    void prg(const d2d::rules::Skill& skill, ServerDoFunction func, int count, int lvl, float target_x, float target_y, std::uint32_t now_ms);
    // Dragon Tail's kick hit.
    void dragon_tail(const d2d::rules::Skill& skill, std::size_t target, int phys, std::uint32_t now_ms);

    // A unique's mods in the fight (uniques.hpp, monsters.md "Boss mods in the fight"), each tick.
    void boss_events(std::span<Foe> foes, std::uint32_t now_ms);
    // A boss's missile from where it stands, at level mlvl / 2 (at least 1), the row's own element.
    void boss_missile(const Monster& monster, const char* name, float dx, float dy, const std::shared_ptr<std::vector<int>>& ring, std::uint32_t now_ms, int missile_level = -1);
    // Fire Enchanted's death blast (uniques.hpp fire_blast): physical and fire, each a 64th of the roll in
    // 256ths, on everyone within difficulty + 4 subtiles; the corpse's guts (monstercorpseexplode).
    void fire_blast(const Monster& monster, std::span<Foe> foes, std::uint32_t now_ms);
    // Umod 33's (a suicide minion's) death blast: its A1 roll as physical
    // and its aip1's element, within its aip4 (fight/).
    void suicide_blast(Monster& monster, std::span<Foe> foes, std::uint32_t now_ms);
    [[nodiscard]] bool blast_reaches(const Monster& monster, const Foe& foe, int radius) const;
    // MonUMod 22's death hook (rules::killself_hooks): killself on the
    // monsters about the boss; Radament's and Mephisto's death missiles.
    void quest_death(Monster& boss, std::uint32_t now_ms);

    // Monster i died (the player's or the merc's doing): the player gets the
    // experience, its pack may scatter, it drops its loot.
    void killed(std::size_t monster_index, std::uint32_t now_ms, bool credit = true);
    // The killer's merc's share of it (rules::merc_kill_exp) and its level; the owner's
    // unit event 0x5b on a level-up (FUN_00553380) isn't traced or played.
    void merc_share(const Monster& monster);
    bool merc_blow = false;                // the blow being landed is the merc's (its strike, arrow or skill missile)
    // The players in the game (game_players: the /players count when it's larger) and those of them alive (the
    // party on the level: one screen), for NoDrop (Loot::drop); alone, game_players() and 1.
    [[nodiscard]] std::pair<int, int> drop_counts();
    // The acting player's share of a kill: its level penalty (kill_exp),
    // item_addexperience, the experience (levels gained, their sound).
    void gain_exp(std::int64_t share, const Monster& monster, std::uint32_t now_ms);
    struct Kill { int type; float x, y; int level; int super = -1; int id = -1; int killer = 0; };   // super: its SuperUniques row; id: its unit id; killer: the player's (Hero::id), 0 a monster's
    std::vector<Kill> kills;                       // since the World last looked (its quests' death hooks)
    std::vector<int> cloaks;                       // Cloak of Shadows' state (153) put on a player or a Shadow since then: its auralen

    // The merc as a fighter: its hireling damage, attack rating, defense.
    [[nodiscard]] d2d::rules::Fighter merc_fighter() const;
    // The friendly auras (do 65) covering (x, y) cells: the player's, the
    // merc's and a totem's, each while (x, y) is within its aurarangecalc
    // subtiles of its holder, at the holder's level and calc env.
    struct AuraOn { const d2d::rules::Skill* skill = nullptr; int level = 0; d2d::rules::CalcEnv env; };
    [[nodiscard]] std::vector<AuraOn> friendly_auras(float x, float y) const;
    // Concentration's share at (x, y) of a skill with that Param1, %: Blessed Hammer's.
    [[nodiscard]] int concentration_share(float x, float y, int param1) const;
    // The enemy auras (do 66, Holy Freeze's 81) over the monster the same way.
    [[nodiscard]] std::vector<AuraOn> enemy_auras(const Monster& monster) const;
    // `taker`: for an enemy aura, the monster its filter must take.
    [[nodiscard]] std::vector<AuraOn> auras_at(float x, float y, bool enemy, const Monster* taker = nullptr) const;
    // An enemy aura's pulse (do 66 / 81 with an EType): its element at every
    // live monster within its aurarange of (x, y), struck by `striker`.
    void enemy_pulse(const d2d::rules::Skill& skill, int lvl, const d2d::rules::CalcEnv& env, float x, float y, const std::array<int, 4>& pierce, int striker, std::uint32_t now_ms);
    // What the merc wears (World::character.merc_items): its items' stats
    // summed (sockets, set bonuses), the weapon's own, its weapon and shield,
    // the items' own defence (after their enhanced defence), defence per level.
    struct MercGear {
        d2d::rules::StatSum sum{}, weapon_sum{};
        const d2d::d2s::Item *weapon = nullptr, *shield = nullptr;
        std::int64_t item_def = 0, per_level = 0;
    };
    [[nodiscard]] MercGear merc_gear(const Hero& hero) const;   // any player's merc (a party member's FHR)
    [[nodiscard]] MercGear merc_gear() const { return merc_gear(*actor); }
    // Its maximum life with its gear.
    [[nodiscard]] int merc_max_life() const;
    // The Act 1 merc's shot (RogueMissile, do 110) at `skill_lvl`, with its gear's stats 157 / 158: the Missiles row and
    // the level it's made at; null with no row.
    struct MercShot { const GameData::MissileInfo* info = nullptr; int level = 0; };
    [[nodiscard]] MercShot merc_shot(const d2d::rules::Skill& skill, int magic, int exploding, int skill_lvl) const;

    // The merc's turn.
    void merc_turn(std::uint32_t now_ms, float elapsed, const Crowd& crowd, bool town);

    // The player's combat modes this frame.
    bool player_modes(bool held, std::uint32_t now_ms, float elapsed);
    // A step of a moving skill.
    void skill_step(std::uint32_t now_ms, float elapsed);
    // A moving skill's event frame (its srvdofunc).
    void move_event(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    // FUN_0056bd10 handed the last target: the enemy in reach with the next
    // higher unit id, else the lowest (round the ring; the same one when
    // it's alone).
    void other_target();
    // The fighter a blow swings with: with two weapons, the hand the skill's weapsel picks.
    [[nodiscard]] d2d::rules::Fighter blow_fighter(const d2d::rules::Skill* skill) const;
    // One hand's throw: that slot's throwing weapon flies.
    void throw_hand(const d2d::rules::Skill& skill, int lvl, int slot, std::uint32_t now_ms);
    // Impale's price: calc2 % of the time the weapon loses calc3 durability (stat 72); a throwing weapon one of
    // its quantity instead.
    void impale_wear(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    // Ammo.
    [[nodiscard]] int ammo_index() const;
    // A bow's or crossbow's shot: magicarrow with stat 157 (item_magicarrow), explodingarrow with 158, else
    // arrow / bolt; null for any other weapon.
    [[nodiscard]] const GameData::MissileInfo* weapon_missile(int* shot_level = nullptr) const;
    // Whether the shot takes ammo: the plain attack's, a skill with decquant, Throw; the ammo checks too.
    [[nodiscard]] bool needs_ammo(const d2d::rules::Skill& skill) const;
    void spend_ammo(int index = -1);   // that item, or ammo_index()'s
    // Ammo run out.
    void out_of_ammo(std::size_t index);
    // FUN_0057ff70 (ReEquip): the kept weapon (Hero::kept_weapon) back on,
    // when the pack holds it and it finds a slot (rules::reequip_slot); the
    // kept id cleared either way. Whether it went on.
    bool reequip();
    // A landed hit's wear.
    void wear_weapon(std::uint32_t now_ms);
    void wear_armor(std::uint32_t now_ms);
    // One item's wear: armour 10 %, a throwable 10 %, any other weapon 4 % to lose 1 durability.
    void wear(std::size_t index, std::uint32_t now_ms);
    // The missile a skill fires, when d2d builds it.
    // `any_owner`: a trap's shot, whose row names the player's skill.
    [[nodiscard]] const GameData::MissileInfo* skill_missile(const d2d::rules::Skill& skill, bool any_owner = false) const;
    [[nodiscard]] bool missile_skill(const d2d::rules::Skill& skill) const;
    // Casting one at (tx, ty).
    bool cast_missile(int skill, float target_x, float target_y, std::uint32_t now_ms);
    // A skill missile of row `mi` from (x, y) toward (x + dx, y + dy) for `range` ticks (0 velocity: it stays
    // put).
    [[nodiscard]] int pierce_chance(bool for_merc) const;
    [[nodiscard]] bool pierce_armed(bool for_merc) const;
    void seed_pierce(Missile& missile, bool for_merc);
    // Missiles from `from` on with no seed yet take their unit seed off the game seed, in order (a
    // visual-only one none): a monster's as its think returns, any left at the frame's missile tick.
    void seed_missiles(std::size_t from);
    Missile& launch(const GameData::MissileInfo& missile_info, const d2d::rules::Skill& skill, int lvl, float x, float y, float dx, float dy,
                    int range, std::uint32_t now_ms);
    [[nodiscard]] int calc(const d2d::rules::Skill& skill, const d2d::rules::Calc& calc_row, int lvl);
    // The skill's missiles from the caster: toward the cast point, across its line, all round, at a monster;
    // the player's spends its ammo first.
    void fire(const d2d::rules::Skill& skill, std::uint32_t now_ms) {   // the player's: its ammo spent first
        if (needs_ammo(skill)) spend_ammo();
        fire(skill, now_ms, player_caster(skill.id));
    }
    void fire(const d2d::rules::Skill& skill, std::uint32_t now_ms, const Caster& caster);
    // A lobbed row that lands (hit function 36: Fire Blast, Shock Web) comes
    // down at its target: its range is the frames to get there.
    [[nodiscard]] static int land_range(const GameData::MissileInfo& missile_info, float dx, float dy);
    // Once a frame for the player's skill missiles (their pSrvDoFunc).
    void player_shots(std::uint32_t now_ms);
    void breath_do(const d2d::rules::Skill& skill, std::uint32_t now_ms);   // one do of Inferno's channel (FUN_005c8ca0)
    bool breath_again(const d2d::rules::Skill& skill, float target_x, float target_y, std::uint32_t now_ms);   // a held re-cast while it burns
    void inferno_client_start(const d2d::rules::Skill& skill, std::uint32_t now_ms);   // cltstfunc 15's list (LAB_004e3060)
    bool bolt_heals_ally(Missile& missile, std::uint32_t now_ms);   // Holy Bolt on another player it crossed this frame
    int missile_frame = -1;                // the frame missile_tick last ran
    void missile_tick(std::uint32_t now_ms);
    // An element in 256ths a frame on monster i (a burner's): less its resistance, gathered until whole points
    // come off (no hit recovery: its record has result flag 1 alone).
    void burn(std::size_t monster_index, const d2d::rules::MissileDamage& damage, std::uint32_t now_ms, bool soft = false);
    [[nodiscard]] std::array<int, 4> pierce() const;
    // A skill's missile reaching monster i.
    bool skill_missile_hits(Missile& missile, std::size_t monster_index, std::uint32_t now_ms);
    // A missile with no skill on monster i: the merc's plain arrow, its bow's
    // blow.
    bool plain_missile_hits(Missile& missile, std::size_t monster_index, std::uint32_t now_ms);
    // A skill missile's damage on monster i: with a weapon share (the
    // skill's SrcDam, or a row with no Skill its SrcDamage) the weapon's
    // blow at that share, attack rating rolled; else a sure hit (ToHit rows
    // roll the attack rating); then the skill's damage (missile_blow).
    // `freeze`: a Glacial Spike's freeze, frames. `roll`: the burst's one
    // record, drawn by the first blow and resisted by each unit.
    void strike(const Missile& missile, std::size_t monster_index, std::uint32_t now_ms, int freeze = 0, d2d::rules::MissileRoll* roll = nullptr);
    // A blow's freeze length on monster i: its chill goes on the blow; stand() then stands it for the freeze,
    // after the blow lands.
    [[nodiscard]] d2d::rules::Freeze freeze_on(std::size_t monster_index, const d2d::rules::Target& target, d2d::rules::Blow& blow, int freeze);
    void stand(std::size_t monster_index, const d2d::rules::Freeze& frozen, std::uint32_t now_ms);
    // Every monster within r subtiles of (x, y) takes the missile's damage;
    // `one_roll`: from one roll for all of them (hit functions 1 and 13).
    void area(const Missile& missile, float x, float y, int radius, std::uint32_t now_ms, int freeze = 0, bool one_roll = false);
    // A missile's hit function where it is.
    // `struck`: the monster the missile struck, -1 none.
    void burst(Missile& missile, std::uint32_t now_ms, int struck = -1);
    static constexpr std::array<int, 15> kNoUnitHit{ 1, 2, 3, 4, 9, 13, 14, 20, 22, 26, 29, 36, 48, 51, 56 };
    // The aura's pulse, every perdelay ticks while it's on.
    void aura_pulse(std::uint32_t now_ms);
    // When an aura pulsing now (at level `lvl`) pulses next, ms (FUN_0056cda0).
    [[nodiscard]] std::uint32_t aura_due(const d2d::rules::Skill& skill, int lvl, std::uint32_t now_ms) {
        return std::uint32_t(d2d::rules::aura_next_frame(int(now_ms / 40), calc(skill, skill.perdelay_calc, lvl))) * 40;
    }
    // A friendly aura's life (Prayer, aurastat hitpoints) on a pulse: every
    // perdelay frames, on the player and the merc within its aurarange.
    void prayer_pulse(const d2d::rules::Skill& skill, int lvl, const d2d::rules::CalcEnv& env, float from_x, float from_y, bool player_cast = false);
    // Whether the skill's aurafilter takes the monster (aura_filter.hpp).
    [[nodiscard]] bool aura_takes(const d2d::rules::Skill& skill, const Monster& monster, float from_x, float from_y) const;
    // Whether FUN_0056b7e0's walk with a FUN_0056b3e0 filter, centred on a
    // unit at (from_x, from_y), takes the monster (aura_filter.hpp): its
    // mode (DD a corpse, any other without life dying), MonStats npc, unit
    // flags 4 and 8 as `targetable` (flags 0xe), the level a town, and a
    // clear line, mask 4, when the filter asks.
    [[nodiscard]] bool filter_takes(std::uint32_t filter, const Monster& monster, float from_x, float from_y) const;
    // The player's aura over the monster: in range and taken by its filter,
    // so its auratargetstate shows (Conviction's; never Might's).
    [[nodiscard]] bool in_aura(const Monster& monster);
    // Monster i as the player's blows see it.
    [[nodiscard]] d2d::rules::Target target_of(std::size_t monster_index);
    // The same under the merc's blow: a MonStats boss takes the game's HireableBossDamagePercent.
    [[nodiscard]] d2d::rules::Target merc_target(std::size_t monster_index);
    void apply_target_stats(const d2d::rules::Skill& skill_row, int lvl, d2d::rules::Target& target, const std::array<int, 6>& base, const d2d::rules::CalcEnv* env = nullptr);
    // A summoning skill d2d builds.
    [[nodiscard]] bool summon_skill(const d2d::rules::Skill& skill) const;
    // A trap's shooting skill.
    [[nodiscard]] int trap_shot(const d2d::rules::Skill& skill) const;
    // Casting one at (tx, ty) (the corpse there for Raise Skeleton): its
    // mana and SC, the summon on the action frame.
    bool cast_summon(int skill, float target_x, float target_y, std::uint32_t now_ms);
    // The unraised corpse within 2 cells of (x, y) nearest it, or -1; with a corpse skill's start, one its test passes.
    [[nodiscard]] int corpse_near(float x, float y, d2d::rules::ServerStartFunction start = d2d::rules::ServerStartFunction::kNone) const;
    [[nodiscard]] bool corpse_takes(d2d::rules::ServerStartFunction start, const Monster& corpse) const;
    // The pet arrives.
    [[nodiscard]] const d2d::rules::Skill* skill_named(std::string_view name) const;
    // The missile row a pet's skill shoots.
    [[nodiscard]] const GameData::MissileInfo* pet_missile(const d2d::rules::Skill& skill, int variant = 0) const;
    void summon(const d2d::rules::Skill& skill, std::uint32_t now_ms) { summon(skill, skill_level ? skill_level(skill.id) : 1, actor->cast_x, actor->cast_y, now_ms); }
    void summon(const d2d::rules::Skill& skill, int lvl, float x, float y, std::uint32_t now_ms);
    [[nodiscard]] static int pet_buff_stat(const Pet& pet, int stat) { const auto found = pet.buff_stats.find(stat); return found == pet.buff_stats.end() ? 0 : found->second; }
    void pet_buffs(Pet& pet);
    // A Shadow's: the owner's fighter with its own damagepercent (a curse's) in.
    [[nodiscard]] static d2d::rules::Fighter shadow_fighter(const Pet& pet, d2d::rules::Fighter fighter);
    [[nodiscard]] d2d::rules::Fighter pet_swing(const Pet& pet) const;   // its own A1 blow, a curse's damagepercent in
    void blade_shield(const d2d::rules::Skill& skill, int lvl, float x, float y, const d2d::rules::Fighter& fighter, bool by_player, int striker, std::uint32_t now_ms);
    int summon_one(const d2d::rules::Skill& skill, int type, int lvl, const d2d::rules::CalcEnv& env, float x, float y, std::uint32_t now_ms, int corpse = -1);   // the pet's id; Revive's corpse
    // Iron Golem's item.
    [[nodiscard]] int golem_item_at(float x, float y) const;
    std::optional<d2d::d2s::Item> golem_item;   // the one do 57 took off the floor, for summon_one's golem to wear
    // A pet as the monsters see it: its defense, its MonStats resistances
    // (traps aren't there to hit).
    [[nodiscard]] Foe pet_foe(const Pet& pet) const;
    void pet_hurt(Pet& pet, int damage, std::uint32_t now_ms, int hit_class = 0, bool marked = false, int fraction = 0);
    // A Crushing Blow on a pet (crushing_blow at `divisor` less `damage_resist`): off its life, no get-hit, no regen event.
    void pet_crushed(Pet& pet, int divisor, int damage_resist, std::uint32_t now_ms);
    // The poison its foe took this frame (Foe::poison, poison_ticks) on a pet, its life event filed for the next frame.
    void pet_poisoned(Pet& pet, const Foe& foe, std::uint32_t now_ms);
    // The player crossed from `from` to `to` (shifted by dx, dy): the pets
    // with them come along, traps stay where they were set.
    void pets_cross(const Level* from, const Level* destination, float dx, float dy);
    // A pet's enemy aura (Fire Golem's Holy Fire: srvdofunc 66) pulses
    // round the pet, as the player's does round the player (aura_pulse).
    void pet_aura(Pet& pet, std::uint32_t now_ms);
    // The pets' turn, the merc's way.
    void pets_turn(std::uint32_t now_ms, float elapsed, const Crowd& crowd);
    // A SkillDamage pet's swing damage and to-hit at its owner's level in the summoning skill now, run each
    // frame before it acts.
    void pet_skill_damage(Pet& pet);
    void necropet_turn(Pet& pet, std::uint32_t now_ms, float elapsed, const Crowd& crowd);
    bool pet_walking(Pet& pet, std::uint32_t now_ms, float elapsed, const Crowd& crowd);
    void pet_think_turn(Pet& pet, std::uint32_t now_ms, float elapsed, const Crowd& crowd);
    // The owner's other pets, the merc among them, within 2 of this one: a think's mode 4 crowd.
    [[nodiscard]] int pet_crowd(const Pet& pet) const;
    // The Fire Golem's death blast and a trap's, 4 frames after it dies.
    [[nodiscard]] bool blast_due(const Monster& golem) const;
    bool golem_blast(Pet& pet, std::uint32_t now_ms);
    // A Shadow's, from golem_blast.
    void shadow_nova(Pet& pet, std::uint32_t now_ms);
    // FUN_005ddc30's foe (an index into monsters, -1 none) for a pet of `size` at x, y (cells); `gap` its distance.
    [[nodiscard]] int pet_second_foe(const Monster& searcher, int size, const Crowd& crowd, int& gap) const;
    void pet_cast(Pet& pet, std::uint32_t now_ms, const Crowd& crowd);
    void shadow_give(Pet& pet, int skill, int lvl) const;
    void shadow_cast(Pet& pet, std::uint32_t now_ms, const Crowd& crowd, int dragon_part = 3);
    void shadow_sequence(Pet& pet, std::uint32_t now_ms);   // a Shadow's Dragon Flight: its MonSeq sequence starts
    void trap_fire(Pet& pet, std::size_t target, std::uint32_t now_ms);
    // Holy Shield: the holyshield state for auralencalc ticks, its aurastats (toblock dm56) on the player.
    bool cast(int skill, std::uint32_t now_ms);
    // A cast's cost: a charged entry's (Hero::charge, for that skill) is a charge and no mana, else the skill's
    // mana.
    [[nodiscard]] bool from_charges(int skill) const;
    // The cast's entry still there at cast time: its item active, the same level, charges left.
    [[nodiscard]] bool charge_live() const;
    // A cast's level where it's paid for: 0 (no cast) from an entry that
    // went between the command and the cast (used up, unequipped, swapped).
    [[nodiscard]] int cast_level(int skill) const;
    // What the check asks (FUN_0056c160: no minmana floor); pay takes what FUN_0056bfe0 takes.
    [[nodiscard]] int cost_of(const d2d::rules::Skill& skill, int lvl) const;
    // Whether the player's skill can go now, game.exe's status.
    [[nodiscard]] int skill_status(int skill, std::uint32_t now_ms);
    // The player in state 0x72: a succubus witch's Blood Mana (srvdofunc 30) as its curse, while it lasts.
    [[nodiscard]] bool blood_mana() const;
    // FUN_005d2b60, a cost paid in state 0x72 (FUN_0056bfe0, FUN_0056c110): `cost` 256ths off life.
    bool blood_pay(std::int64_t cost);
    // The client's ask before a skill goes: status 1, 2 or 4 on a skill with AttackNoMana asks again for Attack
    // (0), which goes in its place, silently.
    int ask(int skill, std::uint32_t now_ms);
    // The end of the swing skill's do: its delay (Skills.txt `delay`, frames) above 0 puts the player in
    // skilldelay till now plus it.
    void start_delay(std::uint32_t now_ms);
    void pay(const d2d::rules::Skill& skill, int lvl);
    void owe(const d2d::rules::Skill& skill, int lvl);
    void settle_cast();
    void cancel_swing();
    // Frenzy's state: each hit that lands raises it a level, up to the skill's, for auralencalc ticks; its
    // aurastats (velocitypercent dm34, attackrate dm56) are taken at that level.
    void frenzy(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    // Who the next strike of a chain goes for: Zeal's and Fend's next hit
    // (do 13) the next in reach by FUN_0056bd10's pick (other_target);
    // the rest (Talon's next kick) stay on their monster while it lives.
    bool next_target();
    // Do 13's next hit, on the hit's action frame.
    void repeat_hit(int struck, std::uint32_t now_ms);
    // The swing starting over now: from (100 - keep) % of the frames played.
    void restart_swing(int keep, std::uint32_t now_ms);
    // Strafe's next arrow: at strafe_at, calc2 its damage %; then, with arrows left, the next target, none
    // ending the run.
    void strafe_shot(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    [[nodiscard]] std::vector<int> strafe_reach(const d2d::rules::Skill& skill, int lvl);   // the monsters within its aurarange
    // The monsters in reach, by unit id (d2d's index), as FUN_0056b7e0
    // walks the units in the rooms around the player with flags 0x2a783:
    // within FUN_0056e510's radius (the weapon's range + 4, squared
    // subtiles, FUN_006492a0), alive and targetable, hostile (0x8000), out
    // of town (0x100 / 0x2000), in melee (0x20000, FUN_00622c40: within the
    // range + 1 by FUN_00641530 and a clear line, mask 0x804; under 1 always;
    // a tentacle or its head, classes 0x102 / 0x105, within the range + 8),
    // the bare line to it clear of mask 4 (0x200, FUN_0064e260).
    [[nodiscard]] std::vector<int> in_reach() const;
    // The range of the weapon in hand, 0 with none.
    [[nodiscard]] int weapon_range() const;
    // Closing in on the monster being attacked: in reach, swing; else the
    // point to walk to (nullopt: nothing to do).
    std::optional<std::pair<float, float>> engage(std::uint32_t now_ms);
    // The monster is in the player's melee reach, as the walk's end has it (`blow` false) or as the blow's
    // to-hit test does (true).
    [[nodiscard]] bool melee_reach_to(const Monster& target, bool blow) const;
    // The monsters around the player, in the crowd that blocks walkers.
    void crowd(Crowd& crowd_out);
    // The same about (x, y): the monsters within 12 of it and every hero's pets on the level.
    void crowd_around(Crowd& crowd_out, float x, float y);
    // One frame of the fight outside town (`in_moor`: in the level whose
    // monsters these are), and the merc's turn (following, in town).
    void world(bool in_moor, std::uint32_t now_ms, float elapsed, const Crowd& crowd);
    // The acting player and their merc as the monsters see them this frame.
    [[nodiscard]] Foe player_foe(std::uint32_t now_ms);
    [[nodiscard]] Foe merc_foe(std::uint32_t now_ms);
    [[nodiscard]] int player_foe_id() const { return -2 - 2 * actor->id; }   // below the monsters' ids (a guided missile finds it again)
    [[nodiscard]] int merc_foe_id() const { return -3 - 2 * actor->id; }
    void on_foe_states(Foe& foe, std::size_t which, std::uint32_t now_ms) const;
    [[nodiscard]] bool zealots_spare() const;
    // Whether (x, y) is within 30 cells of a player (the rooms in play).
    [[nodiscard]] bool near_players(float x, float y) const;
    void hurt_side(std::span<Foe> side, std::uint32_t now_ms);
    [[nodiscard]] Hero::Curse monster_curse(const Foe::Cast& cursed, std::uint32_t now_ms);
    // The stats a monster's curse puts on the player's side: its skill's
    // aurastats at its level, or a random curse's own by its pick.
    [[nodiscard]] std::vector<std::pair<int, int>> curse_stats(const Hero::Curse& curse, const d2d::rules::CalcEnv& env) const;
    // Bosses' auras (Aura Enchanted), each at its level within its aurarange.
    void monster_auras(std::vector<Foe>& foes, std::uint32_t now_ms);
    // A magic shrine's missiles from (x, y), at level clvl / 5 (1..8).
    void shrine_missiles(int code, float x, float y, int clvl, std::uint32_t now_ms);
    // The player went to `to`: an outdoor level's monsters come back, the last one's are kept as they were;
    // nothing in flight follows.
    void enter(const Level* destination);
    // Another level's turn (lan-host stage 9, World::swap_to): its monsters
    // and missiles in, this one's kept, nobody's target dropped.
    void take_turn(const Level* destination);
    // The player at (x, y) in `here`: the rooms coming into play round the player populate (player_moved), their
    // monsters joining `monsters` or the level's `kept`.
    void rooms_up(const Level& here, float x, float y, bool arrived);
};

}  // namespace d2d::game
